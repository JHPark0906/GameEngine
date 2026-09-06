#include "Shell/EditorBootstrap.h"

#include <optional>
#include <utility>

#include "Diagnostics/Debug.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/PlatformServices.h"

#include "Rendering/TextRasterizationCache.h"
#include "SceneRendering/RenderFrontendFactory.h"

#include "Document/EditorContext.h"
#include "Rules/EditorFonts.h"
#include "Rules/EditorPlayInputFocus.h"
#include "Rules/EditorRecovery.h"
#include "Views/EditorRecoveryPrompt.h"
#include "Rules/EditorRecovery.h"
#include "Shell/EditorShell.h"

#include "Rendering/TextRasterizationCache.h"
#include "Math/Color.h"
#include "Runtime/Canvas.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "App/EditorSettings.h"
#include "App/ProjectFile.h"
#include "Platform/IWindow.h"
#include "Platform/PlatformServices.h"
#include "Runtime/Game.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 에디터 프로세스 자신의 런타임에서 활성 장면 하나를 고른다. 에디터 프로젝트의 장면은
    /// 하나뿐이고 비어 있다 — 툴바가 놓일 자리가 그것이다.
    /// </summary>
    [[nodiscard]] GameEngine::Runtime::Scene* FindEditorScene(GameEngine::Runtime::Game& game)
    {
        for (const auto& [sceneId, scene] : game.GetSceneManager().GetActiveScenes())
        {
            if (scene)
            {
                return scene.get();
            }
        }
        return nullptr;
    }
}

EditorBootstrap::EditorBootstrap() = default;

EditorBootstrap::~EditorBootstrap()
{
    // 창을 닫는 것도 장면을 떠나는 길인데, 이 길에는 물을 자리가 없다 — 닫기는 이미 일어났다.
    // 그래서 묻는 대신 남긴다: 저장되지 않은 편집이 있으면 복구 파일로 쓰고 경로를 말한다.
    // 프로젝트의 장면 파일 위에 쓰지 않는 이유는 사용자가 저장을 누른 적이 없기 때문이다.
    if (mContext && mContext->HasUnsavedChanges())
    {
        const std::filesystem::path recoveryPath =
            mContext->SaveRecoverySnapshot(GetRecoveryDirectory());
        if (!recoveryPath.empty())
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "The editor closed with unsaved changes; they were written to a recovery file. "
                "path=", recoveryPath.string());
        }
    }

    // 프론트엔드가 자리를 더 오래 쥐고 있어도, 사라진 셸을 가리키지 않게 비운다.
    mShellSlot->shell = nullptr;
}


namespace
{
}

bool EditorBootstrap::Initialize(
    GameEngine::Runtime::Game& game,
    GameEngine::Platform::IWindow& window)
{
    // 편집기는 일반 메시지를 남긴다. 기본값은 「디버그 빌드인가」인데 그 질문이 노린 것은
    // 출하된 게임이고, 편집기도 Release로 빌드되므로 그대로 두면 콘솔에 경고와 오류만 도착한다.
    // 사람이 엔진이 무엇을 하는지 보려고 여는 창이 바로 그 콘솔이라, 여기서 켠다.
    GameEngine::Diagnostics::Debug::SetMessagesEnabled(true);

    // 엔진이 부팅한 런타임은 에디터 프로세스 자신의 것이다. 편집 대상은 EditorContext가 자기
    // 런타임에 띄우므로 편집에는 쓰이지 않지만, 에디터 자신의 UI 계층이 사는 곳이 바로
    // 이 런타임이다.
    mGame = &game;
    mWindow = &window;
    mContext = std::make_unique<EditorContext>();
    mShell = std::make_unique<EditorShell>(*mContext, mConfirmations);
    mShellSlot->shell = mShell.get();

    // 명령줄의 첫 .gameproject가 시작하며 열 프로젝트다. 진입점은 엔진의 것이라 인수를 직접
    // 받을 수 없고, 플랫폼 서비스에서 읽는다.
    for (const std::string& argument :
         GameEngine::Platform::PlatformServices::GetCommandLineArguments())
    {
        const std::filesystem::path candidate(std::u8string(argument.begin(), argument.end()));
        if (!GameEngine::App::ProjectFile::HasProjectExtension(candidate))
        {
            continue;
        }
        if (!mContext->OpenProject(candidate))
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Failed to open the project passed on the command line. path=", candidate.string());
        }
        break;
    }

    // 명령줄이 이긴다. 인자가 없거나 그 프로젝트를 못 열었으면, 지난번에 열었던 프로젝트와
    // 장면으로 돌아간다 — 에디터를 다시 열면 어제의 자리다. 설정은 OpenProject/OpenScene이
    // 덮어쓰므로 사본으로 읽는다.
    if (!mContext->HasOpenProject())
    {
        const GameEngine::App::EditorSettingsData settings = mContext->GetSettings();
        if (!settings.lastProjectPath.empty() && mContext->OpenProject(settings.lastProjectPath))
        {
            // OpenProject는 프로젝트의 시작 장면을 연다. 마지막으로 보던 장면이 따로 있으면
            // 그 장면으로 옮긴다.
            if (settings.hasLastScene &&
                (!mContext->HasOpenScene() ||
                    mContext->GetOpenProjectSceneId() != settings.lastSceneId))
            {
                if (!mContext->OpenScene(settings.lastSceneId))
                {
                    // 마지막 장면을 다시 열지 못했음을 원인과 함께 알린다.
                    // OpenScene의 실패가 다른 장면이 열린 화면으로 이어지는 이유를 설명한다.
                    GameEngine::Diagnostics::Debug::LogError(
                        "The scene you had open last time could not be reopened, so the editor "
                        "is showing the project's starting scene instead. sceneId=",
                        settings.lastSceneId);
                }
            }
        }
    }

    // 툴바를 에디터 자신의 런타임 장면에 세운다. 그 장면은 비어 있고 아무도 편집하지 않는다.
    // 물음이 서는 창을 툴바보다 먼저 세운다. 계층에서 뒤가 위이므로, 나중에 세운 것이 위에
    // 서야 물음이 툴바를 덮는다.
    mConfirmationView = std::make_unique<EditorConfirmationView>(mConfirmations);
    mToolbar = std::make_unique<EditorToolbarView>(
        *mContext, *mShell, mConfirmations, mFileDialogs);
    mMenuBar = std::make_unique<EditorMenuBarView>();
    if (GameEngine::Runtime::Scene* const editorScene = FindEditorScene(game))
    {
        mToolbar->Build(*editorScene);
        mShell->BuildRetainedViews(*editorScene);
        // 메뉴 막대는 패널보다 뒤에 세운다. 계층에서 뒤가 위이므로, 펼친 목록이 자기가 덮은
        // 패널을 이기려면 그 패널들보다 뒤여야 한다. 자기 캔버스를 갖는 것은 다른 뷰들과
        // 같은 모양이며, 그래야 이 막대의 배율을 따로 맞출 수 있다.
        mMenuBarCanvas = editorScene->CreateGameObject("MenuBarCanvas");
        if (mMenuBarCanvas)
        {
            static_cast<void>(mMenuBarCanvas->AddComponent<GameEngine::Runtime::Canvas>());
            mMenuBar->Build(*editorScene, *mMenuBarCanvas, RowFontSize);
        }
        // 물음의 창은 맨 마지막에 세운다. 계층에서 뒤가 위이므로 이것이 무엇 위에든 선다.
        mConfirmationView->Build(*editorScene);
    }
    else
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The editor has no scene of its own, so its toolbar cannot be placed.");
    }

    GameEngine::Diagnostics::Debug::Log("GameEditor initialized.");
    return true;
}

std::vector<std::unique_ptr<GameEngine::Rendering::IRenderFrontend>>
    EditorBootstrap::CreateRenderFrontends(
        std::shared_ptr<GameEngine::Rendering::TextRasterizationCache> worldTextCache)
{
    std::vector<std::unique_ptr<GameEngine::Rendering::IRenderFrontend>> frontends;
    frontends.push_back(std::make_unique<EditorUIFrontend>(mShellSlot));
    // 그리고 에디터 자신의 런타임을 그리는 장면 패스 하나. 그 런타임의 장면에 에디터의
    // UI 계층이 살고, 이것이 그 계층이 화면에 나타나는 유일한 길이다 — 즉시 모드 UI는
    // 자기 그리기 목록만 싣지 컴포넌트를 알지 못한다.
    //
    // 편집 중인 프로젝트가 여기서 한 번 더 그려지지는 않는다: 주 프레임의 프론트엔드는
    // 애플리케이션이 부팅한 런타임 — 에디터 자신의 것 — 을 받고, 프로젝트의 런타임은
    // EditorContext가 따로 세운 별개의 것이다.
    //
    // 캐시를 새로 만들지 않고 받은 것을 나눠 쥔다. 같은 세계에 캐시가 둘이면 배치가 잰
    // 폭과 이 패스가 그리는 폭이 갈라진다.
    frontends.push_back(
        std::make_unique<GameEngine::SceneRendering::SceneRenderPass>(
            std::move(worldTextCache)));
    return frontends;
}

void EditorBootstrap::RegisterSceneFonts(
    GameEngine::Rendering::TextRasterizationCache& worldTextCache)
{
    // 편집기 자신의 UI가 읽는 것과 같은 자리, 같은 파일이다 — LoadEditorFonts와 이 함수가
    // 갈린 목록을 각자 관리하면 언젠가 하나만 고쳐진다.
    const GameEngine::Platform::DirectoryContentSource content(
        GameEngine::Platform::PlatformServices::GetExecutableDirectory());
    // 이 클래스 자신의 이름과 같은 자유 함수라 한정해서 부른다 — 안 그러면 클래스 범위의
    // 이름이 이 이름 공간의 것을 가려 인자 수로 골라지지 않는다.
    GameEditor::RegisterSceneFonts(content, worldTextCache);
}

std::optional<std::string> EditorBootstrap::GetGraphicsBackendSetting() const
{
    // 컨텍스트는 아직 없다 — 이 물음은 그래픽 장치보다 앞서므로, 파일을 여기서 직접 읽는다.
    const std::filesystem::path settingsPath = EditorContext::GetSettingsFilePath();
    std::string graphicsApi =
        GameEngine::App::EditorSettings::Load(settingsPath).graphicsApi;

    // 어느 파일의 어느 값이 무엇을 정했는지 남긴다. "왜 백엔드를 묻지 않지"가 로그로 답이 된다.
    GameEngine::Diagnostics::Debug::Log(
        "Editor graphics backend setting. file=", settingsPath.string(),
        ", graphicsApi=", graphicsApi,
        graphicsApi == "Select" ? " -> asking which backend to use"
                                : " -> starting without asking");
    return graphicsApi;
}

void EditorBootstrap::RoutePlayInput()
{
    if (!mGame || !mContext)
    {
        return;
    }
    GameEngine::Runtime::Input& editorInput = mGame->GetInput();
    const GameEngine::Platform::InputState state = editorInput.GetState();

    PlayInputFocusFrame frame;
    frame.isPlaying = mContext->IsPlaying();
    // 창이 뒤로 물러난 것은 입력이 이미 알고 있다. Win32Input이 WM_KILLFOCUS에서 이 표시를
    // 내리고 눌린 것들도 함께 풀어 주므로, 창에 같은 질문을 다시 만들지 않는다.
    frame.windowHasFocus = state.hasFocus;
    frame.escapePressed = editorInput.GetKeyDown(GameEngine::Platform::Key::Escape);
    if (mShell)
    {
        const GameEngine::UI::UIRect view = mShell->GetGameViewRect();
        frame.pressedInsideGameView =
            editorInput.GetMouseButtonDown(GameEngine::Platform::MouseButton::Left) &&
            view.Contains(static_cast<float>(state.cursor.x), static_cast<float>(state.cursor.y));
    }

    const PlayInputFocusResult result = ResolvePlayInputFocus(frame, mContext->IsPlayInputCaptured());
    mContext->SetPlayInputCaptured(result.captured);

    GameEngine::Runtime::Game* const projectGame = mContext->GetProjectGame();
    if (!projectGame)
    {
        return;
    }

    if (!result.captured)
    {
        // 빈 상태로 채우는 것과 채우지 않는 것은 다르다. 채우지 않으면 마지막으로 누른 것이
        // 눌린 채로 남아, 다음에 잡았을 때 게임이 누르지도 않은 키를 붙들고 있다.
        projectGame->GetInput().BeginFrameWithState({});
        return;
    }

    GameEngine::Platform::InputState forGame = state;
    // 커서를 뷰 원점 기준으로 옮기고, 게임이 그려지는 크기로 환산한다. 두 크기가 같은 지금은
    // 비율이 1이지만, 비율로 적어 두어야 캡처 크기가 뷰와 달라지는 날에도 맞는다.
    if (mShell)
    {
        const GameEngine::UI::UIRect view = mShell->GetGameViewRect();
        const GameEngine::Rendering::RenderTargetSize captured = mShell->GetGameViewSize();
        const float scaleX = view.width > 0.0f
            ? static_cast<float>(captured.width) / view.width
            : 1.0f;
        const float scaleY = view.height > 0.0f
            ? static_cast<float>(captured.height) / view.height
            : 1.0f;
        forGame.cursor.x =
            static_cast<int>((static_cast<float>(state.cursor.x) - view.x) * scaleX);
        forGame.cursor.y =
            static_cast<int>((static_cast<float>(state.cursor.y) - view.y) * scaleY);
    }
    if (result.swallowClick)
    {
        // 포커스를 주려고 누른 클릭이 게임 안에서 발사가 되면 사람은 놀란다. 그 한 번만 지운다.
        forGame.SetMouseButton(GameEngine::Platform::MouseButton::Left, false);
        forGame.mousePresses[static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left)] = 0;
        forGame.mouseReleases[static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left)] =
            0;
    }
    projectGame->GetInput().BeginFrameWithState(forGame);

    // 게임이 쥐고 있는 동안 에디터는 이 프레임의 입력을 받지 않는다. 즉시 모드 UI가 프레임
    // 뒤쪽에서 이것을 읽으므로, 비우는 자리는 그보다 앞인 여기다 — 둘 다 받으면 단축키가
    // 게임을 조작한다.
    editorInput.BeginFrameWithState({});
}

void EditorBootstrap::UpdateMenuBar(const float contentScale)
{
    if (!mMenuBar || !mGame || !mContext)
    {
        return;
    }
    if (mMenuBarCanvas)
    {
        if (GameEngine::Runtime::Canvas* const canvas =
                mMenuBarCanvas->GetComponent<GameEngine::Runtime::Canvas>())
        {
            canvas->SetScaleFactor(contentScale);
        }
    }

    // 무엇을 고를 수 있는지는 문서 모델이 안다. 메뉴는 그 답을 받아 쓰기만 하며, 여기서 다시
    // 판단하면 툴바가 회색으로 두는 것과 메뉴가 회색으로 두는 것이 갈라질 자리가 생긴다.
    const bool inEditMode = !mContext->IsPlaying();
    MenuCommandAvailability availability;
    availability.hasProject = mContext->GetOpenProject() != nullptr;
    availability.hasOpenScene = mContext->HasOpenScene();
    availability.hasSelectedScene = mContext->GetSelectedSceneId().has_value();
    availability.canUndo = inEditMode && mContext->GetUndoStack().CanUndo();
    availability.canRedo = inEditMode && mContext->GetUndoStack().CanRedo();
    availability.isBuilding = mToolbar && mToolbar->IsBuildRunning();

    // 자리는 넘기지 않는다 — 커서가 무엇 위에 있는지는 이벤트 시스템이 이미 답했다. 여기서
    // 넘기는 것은 「클릭이 끝났다」와 「Esc가 눌렸다」는 입력 그 자체의 사실뿐이다.
    GameEngine::Runtime::Input& input = mGame->GetInput();
    const MenuPointer pointer{
        input.GetMouseButtonUp(GameEngine::Platform::MouseButton::Left),
        input.GetKeyDown(GameEngine::Platform::Key::Escape),
    };

    const std::optional<EditorMenuCommand> chosen = mMenuBar->Update(
        pointer, availability, static_cast<float>(mGame->GetRenderSurfaceWidth()), contentScale);

    // 툴바가 위에 비워 둘 높이다. 막대는 자기 글자에서 높이를 얻으므로 상수가 아니며, 그 값은
    // 배율이 곱해진 픽셀로 나오므로 논리 단위로 되돌려 넘긴다 — 받는 쪽이 다시 곱한다.
    if (mToolbar && mMenuBar->GetBar().rect && contentScale > 0.0f)
    {
        mToolbar->SetTopInset(
            mMenuBar->GetBar().rect->GetResolvedRect().height / contentScale);
    }

    if (!chosen.has_value())
    {
        return;
    }
    if (*chosen == EditorMenuCommand::Exit)
    {
        // RequestClose는 승인된 종료를 요청한다. X 단추가 Application을 통해 거치는
        // 같은 종료 판단을 먼저 호출하고, 미저장 확인의 답은 AskAboutClosing이 처리한다.
        if (mWindow && ShouldClose())
        {
            mWindow->RequestClose();
        }
        return;
    }
    if (mToolbar)
    {
        mToolbar->RunMenuCommand(*chosen);
    }
}

void EditorBootstrap::Update(const float deltaTime)
{
    // Play 중 입력을 누가 받는지 여기서 가른다. 즉시 모드 UI는 프레임 뒤쪽에서 에디터 입력을
    // 읽으므로, 게임이 쥐고 있다면 그보다 앞인 여기서 갈라야 한다.
    RoutePlayInput();

    // 툴바는 유지 모드다: 배치와 클릭 판정은 엔진의 UI 시스템이 방금 마쳤고, 여기서는 이번
    // 프레임의 상태를 맞추고 눌린 버튼의 동작을 수행한다.
    const float contentScale = mWindow ? mWindow->GetContentScale() : 1.0f;

    // 메뉴가 툴바보다 먼저다. 툴바가 자기 자리를 정할 때 위에 비워 둘 높이를 이미 알고 있어야
    // 하며, 그러지 않으면 막대가 두꺼워진 프레임에 툴바가 그 아래에서 한 프레임 늦게 내려온다.
    UpdateMenuBar(contentScale);

    if (mToolbar)
    {
        mToolbar->Synchronize(contentScale);
        // 툴바가 방금 정한 높이를 셸에게 넘긴다. 이 순서여야 도킹이 이번 프레임의 높이를
        // 쓴다 — 뒤집으면 접힌 첫 프레임에 패널이 둘째 줄을 덮는다.
        if (mShell)
        {
            mShell->SetToolbarHeight(mToolbar->GetToolbarHeight());
        }
    }
    if (mShell)
    {
        mShell->SynchronizeRetainedViews(mWindow ? mWindow->GetContentScale() : 1.0f);
    }

    // 물음의 창은 스스로 폭을 셈하므로 면 크기와 잣대가 필요하다. 배치가 쓰는 것과 같은 것을
    // 받아야 잰 폭과 그려진 폭이 어긋나지 않는다.
    if (mConfirmationView && mGame)
    {
        mConfirmationView->Synchronize(
            mWindow ? mWindow->GetContentScale() : 1.0f, mGame->GetRenderSurfaceWidth(),
            mGame->GetRenderSurfaceHeight(), mGame->GetTextMeasure());
    }

    // 사고가 남긴 사본은 첫 프레임이 그려진 뒤에 묻는다. 빈 창을 보고 물음을 받는 것과 그려진
    // 에디터 위에서 받는 것은 다르고, 뒤가 맞다.
    //
    // 편집 모드의 프레임은 문서를 바꾸지 않아야 한다. 프로젝트 런타임은 멈춰 있고,
    // 그리기는 컴포넌트를 읽기만 한다. 테스트도 이 전제를 검증한다.
    if (!mAskedAboutRecovery && mContext->HasOpenProject())
    {
        mAskedAboutRecovery = true;
        AskAboutRecoverySnapshots(*mContext, mConfirmations, GetRecoveryDirectory());
    }

    // 편집 모드에서는 프로젝트 런타임이 멈춰 있다. 렌더링은 컴포넌트 상태를 직접 읽으므로
    // 업데이트 없이도 뷰는 현재 장면을 보인다.
    if (!mContext || !mContext->IsPlaying())
    {
        return;
    }
    if (GameEngine::Runtime::Game* const projectGame = mContext->GetProjectGame())
    {
        projectGame->Update(deltaTime);
    }
}

void EditorBootstrap::CollectCaptureViews(std::vector<CaptureView>& views)
{
    if (!mShell)
    {
        return;
    }
    // 프레임마다 묻는다: 창이 다른 배율의 모니터로 옮겨 가면 그 프레임부터 UI가 따라간다.
    if (mWindow)
    {
        mShell->SetContentScale(mWindow->GetContentScale());
    }
    // 자리는 고정이다: 0번이 주 뷰(게임 뷰), 1번이 씬 뷰. 숨겨진 뷰는 크기가 무효라 루프가
    // 렌더링을 생략하지만, 자리를 지워 뒤 뷰의 번호를 흔들지는 않는다.
    // 두 뷰 모두 편집 대상 프로젝트의 런타임을 그린다. 프로젝트가 없으면 엔진의 빈 게임이
    // 그려진다 — 빈 뷰다.
    GameEngine::Runtime::Game* const projectGame = mContext->GetProjectGame();
    views.push_back({ mShell->GetGameViewSize(), std::nullopt, projectGame, true });
    // 씬 뷰는 자체 카메라와 뷰 크기로 draw 수집·컬링·렌더링을 모두 수행한다.
    // 게임의 화면 공간 UI는 편집 카메라와 무관하게 뷰를 가리므로 포함하지 않는다.
    views.push_back({ mShell->GetSceneViewSize(), mShell->GetSceneCamera(), projectGame, false });
}

void EditorBootstrap::OnViewCaptured(
    const std::size_t viewIndex, const GameEngine::Rendering::CapturedImage& image)
{
    if (!mShell)
    {
        return;
    }
    if (viewIndex == 0)
    {
        mShell->PresentGameImage(image);
    }
    else if (viewIndex == 1)
    {
        mShell->PresentSceneImage(image);
    }
}

bool EditorBootstrap::ShouldClose()
{
    const bool hasUnsavedChanges = mContext && mContext->HasUnsavedChanges();
    const CloseDecision decision = DecideOnCloseRequest(hasUnsavedChanges, mAskingAboutClosing);
    if (decision.shouldAsk)
    {
        AskAboutClosing({});
    }
    // 묻는 동안에는 닫지 않는다. 답이 오면 그때 창에 닫아 달라고 말한다 — 이 함수 안에서
    // 답을 기다릴 수는 없다. 물음은 유지 모드 창이고, 그것이 그려지려면 프레임이 돌아야 하며,
    // 그 프레임은 이 함수가 돌아가야 돈다.
    return decision.mayCloseNow;
}

void EditorBootstrap::AskAboutClosing(const std::string& failure)
{
    mAskingAboutClosing = true;

    // 사본을 묻기 전에 쓴다. 그래야 물음이 앞으로 일어날 일을 약속하지 않고 이미 일어난 일을
    // 말한다 — 쓰기는 실패할 수 있고, 실패했는데 "사본을 남긴다"고 적혀 있으면 그 글이 사람을
    // 잃게 만든다.
    const std::filesystem::path copy =
        mContext ? mContext->SaveRecoverySnapshot(GetRecoveryDirectory())
                 : std::filesystem::path{};
    const bool copyWasKept = !copy.empty();

    ConfirmationRequest request;
    request.title = "Unsaved changes";
    request.question = failure.empty()
        ? MakeCloseQuestion(copyWasKept)
        : MakeCloseQuestion(copyWasKept) + "\n" + failure;
    // 두 번째 버튼의 글은 실제로 일어날 일이다. 사본은 이미 써 본 뒤이므로, 남았을 때만
    // 남는다고 말한다 — 약속이 아니라 사실이다.
    request.choices = { "Save and close",
        copyWasKept ? "Close and keep a copy" : "Close and lose them" };
    request.cancelLabel = "Keep editing";
    request.onAnswered = [this, copy, copyWasKept](const std::optional<std::size_t> chosen)
    {
        mAskingAboutClosing = false;

        const UnsavedChoice choice = !chosen              ? UnsavedChoice::Cancel
            : *chosen == 0                                ? UnsavedChoice::Save
                                                          : UnsavedChoice::Discard;
        bool closing = choice == UnsavedChoice::Discard;
        if (choice == UnsavedChoice::Save)
        {
            if (mContext && mContext->SaveOpenScene())
            {
                closing = true;
            }
            else
            {
                // 저장이 실패했는데 닫으면 저장한 줄 알고 잃는다. 창을 남기고 다시 고르게 한다.
                GameEngine::Diagnostics::Debug::LogError(
                    "Saving failed, so the editor stayed open. Nothing was closed.");
            }
        }

        // 사본은 그것이 유일한 되돌릴 길일 때만 남긴다.
        if (copyWasKept && !ShouldKeepTheRecoveryCopy(choice, closing))
        {
            static_cast<void>(DeleteRecoverySnapshot(copy));
        }

        if (choice == UnsavedChoice::Save && !closing)
        {
            AskAboutClosing("Saving failed, so nothing was closed. See the Console.");
            return;
        }
        if (closing && mWindow)
        {
            // 답으로 종료가 승인되면 창에 닫기를 요청한다.
            mWindow->RequestClose();
        }
    };
    mConfirmations.Ask(std::move(request));
}

void EditorBootstrap::OnUnrecoverableFailure(const char* const reason)
{
    if (!mContext)
    {
        return;
    }
    const std::filesystem::path recoveryPath =
        mContext->SaveRecoverySnapshot(GetRecoveryDirectory());
    if (recoveryPath.empty())
    {
        // 쓸 것이 없었을 수도 있고(열린 장면 없음), 쓰지 못했을 수도 있다. 어느 쪽이든 이미
        // 무너지는 중이므로 여기서 더 할 수 있는 일은 없다 — 실패의 이유는 Context가 말했다.
        return;
    }
    GameEngine::Diagnostics::Debug::LogError(
        "The editor saved the open scene to a recovery file before exiting. reason=", reason,
        ", path=", recoveryPath.string());
}

}
