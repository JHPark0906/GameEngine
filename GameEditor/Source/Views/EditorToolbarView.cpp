#include "Views/EditorToolbarView.h"

#include "Views/EditorToolbarButton.h"

#include "Rules/EditorScriptTemplate.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>

#include "Document/EditorContext.h"
#include "Rules/EditorRecovery.h"
#include "Views/EditorRecoveryPrompt.h"
#include "Rules/EditorPanelCommon.h"
#include "Rules/EditorToolbarQuestions.h"
#include "App/ProjectFile.h"
#include "Assets/AssetReference.h"
#include "Core/RelativePath.h"
#include "Diagnostics/Debug.h"
#include "Platform/PlatformServices.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/LayoutElement.h"
#include "Runtime/LayoutGroup.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"

namespace GameEditor
{

namespace
{
    using GameEngine::Math::Vector2;
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::LayoutElement;
    using GameEngine::Runtime::LayoutGroup;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::TextRenderer;

    constexpr float ButtonInset = 4.0f;


    /// <summary>
    /// 한 줄의 높이다. 선언은 <see cref="ToolbarLayoutMetrics"/>에 하나뿐이며, 도킹이
    /// 비워 두는 높이도 툴바가 말한 값에서 나온다 — 두 곳이 각자 32를 적어 두면 한쪽을
    /// 고칠 때 조용히 갈라진다.
    /// </summary>
    constexpr float ToolbarRowHeight = ToolbarLayoutMetrics{}.rowHeight;
    constexpr float LabelFontSize = ToolbarLabelFontSize;

    /// <summary>글자 상자를 왼쪽에서 들여 놓는 거리다. 글자가 테두리에 붙지 않게 한다.</summary>
    constexpr float LabelLeftInset = 8.0f;

    /// <summary>
    /// 글자를 재지 못했을 때 버튼이 갖는 폭이다. 첫 프레임과 글꼴을 열지 못한 화면이
    /// 그런 자리이며, 이 값은 「무엇이 들어갈 만큼」이 아니라 「눌러 볼 수 있을 만큼」이다.
    /// </summary>
    constexpr float MinimumButtonWidth = 48.0f;

    /// <summary>제목 표시줄이 잡아 두는 폭이다. 창 폭을 모르므로 넉넉히 잡고 왼쪽 정렬로 둔다.</summary>
    constexpr float TitleWidth = 1200.0f;

    /// <summary>
    /// 버튼 하나의 바탕 그림이다. 9-슬라이스이며, 채움이 흰색이라 버튼의 틴트가 그대로 통과한다.
    /// </summary>
    constexpr const char* ButtonSprite = "Sprites/button-32.png";

    /// <summary>
    /// 툴바 띠의 바탕 그림이다. 버튼과 같은 모양의 9-슬라이스지만 테두리 선이 한 단계 진해서,
    /// 띠 위에 놓인 버튼이 띠와 구분된다.
    /// </summary>
    constexpr const char* PanelSprite = "Sprites/panel-32.png";

    /// <summary>글자 하나를 사각형 안에 놓는다. 화면 공간이라 픽셀이 곧 자리다.</summary>
    void ConfigureLabel(
        TextRenderer& label, const std::string& text, const GameEngine::Math::Color& color,
        const TextRenderer::Alignment alignment)
    {
        label.SetText(text);
        label.SetColor(color);
        label.SetSpace(TextRenderer::Space::Screen);
        label.SetAlignment(alignment);
        // 세로 가운데는 TextRenderer가 잡는다. 여기서 어림할 수 없는 값이다 — 가운데를 잡으려면
        // 배치된 글자 블록의 높이를 알아야 하고, 그것은 글꼴과 크기가 정한다.
        label.SetVerticalAlignment(TextRenderer::VerticalAlignment::Middle);
        label.SetFontSize(LabelFontSize);
    }
}

EditorToolbarView::EditorToolbarView(
    EditorContext& context, IPropertyEditHost& propertyEdit, ConfirmationQueue& confirmations,
    IFileDialogs& fileDialogs)
    : mContext(context), mPropertyEdit(propertyEdit), mConfirmations(confirmations),
      mFileDialogs(fileDialogs)
{
}

float EditorToolbarView::GetToolbarHeight() const
{
    return mRowHeight;
}

GameObject* EditorToolbarView::AddRect(
    GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name,
    const Vector2& anchorMin, const Vector2& anchorMax, const Vector2& offsetMin,
    const Vector2& offsetMax)
{
    GameObject* const object = scene.CreateGameObject(name);
    if (!object)
    {
        return nullptr;
    }
    static_cast<void>(object->GetTransform().SetParent(&parent.GetTransform()));
    RectTransform* const rect = object->AddComponent<RectTransform>();
    if (!rect)
    {
        return nullptr;
    }
    rect->SetAnchorMin(anchorMin);
    rect->SetAnchorMax(anchorMax);
    rect->SetOffsetMin(offsetMin);
    rect->SetOffsetMax(offsetMax);
    return object;
}

EditorToolbarView::ToolbarButton EditorToolbarView::AddButton(
    GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name)
{
    const ToolbarButtonParts parts = BuildToolbarButton(scene, parent, name, LabelFontSize);
    ToolbarButton entry;
    entry.object = parts.object;
    entry.rect = parts.rect;
    entry.button = parts.button;
    entry.label = parts.label;
    entry.labelRect = parts.labelRect;
    return entry;
}

TextRenderer* EditorToolbarView::AddLabel(
    GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name, const float x,
    const float width, const bool centered)
{
    // 버튼과 같은 높이의 사각형이다. 그 안에서 세로 가운데에 서면 옆 버튼의 글자와 줄이 맞는다.
    GameObject* const object = AddRect(
        scene, parent, name, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { x, ButtonInset },
        { x + width, ToolbarRowHeight - ButtonInset });
    if (!object)
    {
        return nullptr;
    }
    TextRenderer* const label = object->AddComponent<TextRenderer>();
    if (label)
    {
        ConfigureLabel(
            *label, name, DimTextColor,
            centered ? TextRenderer::Alignment::Center : TextRenderer::Alignment::Left);
    }
    return label;
}

EditorToolbarView::ToolbarButton EditorToolbarView::AddRightButton(
    GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name,
    const float right, const float width)
{
    // 확인 줄의 버튼은 폭을 선언한다. 글자에서 폭을 얻는 평상시 버튼과 달리, 이 줄은 무엇을
    // 묻는지가 남는 폭을 전부 가져야 하므로 버튼 쪽이 자기 자리를 양보한다.
    const ToolbarButtonParts parts = BuildToolbarButton(scene, parent, name, LabelFontSize);
    ToolbarButton entry;
    entry.object = parts.object;
    entry.rect = parts.rect;
    entry.button = parts.button;
    entry.label = parts.label;
    entry.labelRect = parts.labelRect;
    if (entry.rect)
    {
        // 오른쪽 끝에 붙는다: 앵커가 부모의 오른쪽 변이고 오프셋은 거기서 왼쪽으로 잰다.
        entry.rect->SetAnchorMin({ 1.0f, 0.0f });
        entry.rect->SetAnchorMax({ 1.0f, 0.0f });
        entry.rect->SetOffsetMin({ right - width, ButtonInset });
        entry.rect->SetOffsetMax({ right, ToolbarRowHeight - ButtonInset });
    }
    return entry;
}

TextRenderer* EditorToolbarView::AddStretchedLabel(
    GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name, const float left,
    const float right)
{
    // 왼쪽 끝에서 시작해 오른쪽 앵커까지 늘어나는 글자다. 창이 넓어지면 이 상자도 넓어진다.
    GameObject* const object = AddRect(
        scene, parent, name, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { left, ButtonInset },
        { right, ToolbarRowHeight - ButtonInset });
    if (!object)
    {
        return nullptr;
    }
    TextRenderer* const label = object->AddComponent<TextRenderer>();
    if (label)
    {
        ConfigureLabel(*label, name, DimTextColor, TextRenderer::Alignment::Left);
    }
    return label;
}

void EditorToolbarView::Build(GameEngine::Runtime::Scene& scene)
{
    mCanvasObject = scene.CreateGameObject("EditorUI");
    if (!mCanvasObject || !mCanvasObject->AddComponent<GameEngine::Runtime::Canvas>())
    {
        GameEngine::Diagnostics::Debug::LogError("The editor toolbar could not create its canvas.");
        return;
    }

    // 툴바는 창 위쪽에 가로로 꽉 찬 띠다. 좌우 앵커가 갈라져 있으므로 창을 넓히면 함께 넓어진다.
    GameObject* const toolbar = AddRect(
        scene, *mCanvasObject, "Toolbar", { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f },
        { 0.0f, ToolbarRowHeight });
    if (!toolbar)
    {
        GameEngine::Diagnostics::Debug::LogError("The editor toolbar could not create its strip.");
        return;
    }
    mStripRect = toolbar->GetComponent<RectTransform>();
    if (SpriteRenderer* const background = toolbar->AddComponent<SpriteRenderer>())
    {
        background->SetSprite(GameEngine::Assets::AssetReference::Parse(PanelSprite));
        background->SetDrawMode(SpriteRenderer::DrawMode::Sliced);
        background->SetSpace(SpriteRenderer::Space::Screen);
        background->SetColor(HeaderColor);
    }

    // 평상시 버튼들이 사는 줄이다. 확인 물음은 별도의 떠 있는 창에 표시한다.
    mButtonGroup = AddRect(
        scene, *toolbar, "ToolbarButtons", { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f },
        { 0.0f, 0.0f });
    if (!mButtonGroup)
    {
        GameEngine::Diagnostics::Debug::LogError("The editor toolbar could not create its group.");
        return;
    }
    // 툴바에는 편집 중 자주 쓰는 네 버튼을 둔다. 프로젝트와 장면 명령은 메뉴에서
    // 접근하며, 그 동작을 수행하는 이 클래스의 함수를 호출한다.
    mUndo = AddButton(scene, *mButtonGroup, "Undo");
    mRedo = AddButton(scene, *mButtonGroup, "Redo");
    mSnap = AddButton(scene, *mButtonGroup, "Snap: Off");
    mPlay = AddButton(scene, *mButtonGroup, "Play");
    // 배치가 읽을 순서다. 선언 순서가 곧 화면의 순서이며, 접힘도 이 순서로 줄을 넘긴다 —
    // 넷이 한 줄에 드는 화면이 대부분이겠지만, 접힘을 걷어내면 좁은 창에서 사라지는 것이
    // 줄 끝의 Play가 된다.
    mRowButtons = { &mUndo, &mRedo, &mSnap, &mPlay };

    // 제목 표시줄은 왼쪽 정렬로 만들고, 실제 자리는 LayoutToolbarRow가
    // 마지막 버튼 뒤로 맞춘다.
    mTitle = AddLabel(scene, *mButtonGroup, "Title", ButtonInset, TitleWidth, false);
    if (mTitle && mTitle->GetGameObject())
    {
        mTitleRect = mTitle->GetGameObject()->GetComponent<RectTransform>();
    }

}



void EditorToolbarView::Synchronize(const float contentScale)
{
    if (!mCanvasObject)
    {
        return;
    }
    if (GameEngine::Runtime::Canvas* const canvas =
            mCanvasObject->GetComponent<GameEngine::Runtime::Canvas>())
    {
        // 오프셋은 논리 픽셀이고, 화면 배율은 캔버스에 적용한다.
        canvas->SetScaleFactor(contentScale);
    }

    // 빌드는 확인 줄보다 먼저 본다. 질문이 떠 있는 동안에도 도구는 계속 돌고 있고, 그동안
    // 줄을 가져가지 않으면 파이프가 차서 빌드가 멈춘다.
    mProjectBuild.Poll();

    SynchronizeTextMetrics(contentScale);
    LayoutToolbarRow(contentScale);

    // 사람이 방금 한 제스처의 답이 먼저다. 이관과 정리는 프로젝트를 여는 동안 스스로 떠오른
    // 것이라 한 프레임 더 기다려도 되고, 기다리게 하지 않으면 사람이 Delete Scene을 눌렀는데
    // 이관 물음이 뜨는 일이 생긴다.
    const bool waitingForAPerson =
        mPendingAction != PendingAction::None || mSceneAwaitingDelete.has_value();
    if (!waitingForAPerson && !mConfirmations.IsAsking())
    {
        // 이관이 정리보다 먼저다. 이관이 끝나야 「장면이 이 정체성을 가리키는가」가 정확해지고,
        // 그 답이 무엇을 치우면 안 되는지를 정한다.
        AskAboutMigrationIfNeeded();
        AskAboutCleanupIfNeeded();
    }

    SynchronizeButtons();
}

void EditorToolbarView::LayoutToolbarRow(const float contentScale)
{
    if (mRowButtons.empty() || !mButtonGroup)
    {
        return;
    }
    const RectTransform* const groupRect = mButtonGroup->GetComponent<RectTransform>();
    if (!groupRect)
    {
        return;
    }
    // 재기가 적어 둔 요구 크기를 그대로 넘긴다. 픽셀과 논리를 맞추는 일은
    // ComputeToolbarRowRects 한 곳에서만 일어난다 — 그 환산이 두 곳에 있으면 한쪽이 한 번 더
    // 곱해도 화면에서는 "글꼴이 큰가"로만 보인다.
    std::vector<float> desiredWidths;
    desiredWidths.reserve(mRowButtons.size());
    for (const ToolbarButton* const entry : mRowButtons)
    {
        desiredWidths.push_back(entry->rect ? entry->rect->GetDesiredSize().width : 0.0f);
    }

    ToolbarLayoutMetrics metrics;
    metrics.inset = ButtonInset;
    metrics.rowHeight = ToolbarRowHeight;
    const ToolbarLayout layout = ComputeToolbarRowRects(
        desiredWidths, groupRect->GetResolvedRect().width, contentScale, metrics);

    for (std::size_t index = 0; index < mRowButtons.size(); ++index)
    {
        RectTransform* const rect = mRowButtons[index]->rect;
        if (!rect)
        {
            continue;
        }
        const GameEngine::UI::UIRect& box = layout.buttons[index];
        rect->SetOffsetMin({ box.x, box.y });
        rect->SetOffsetMax({ box.x + box.width, box.y + box.height });
    }
    // 띠도 버튼 줄 수에 맞춰 자란다. 도킹 높이만 늘리면 둘째 줄 버튼이
    // 띠 밖의 아래 패널 배경 위에 놓인다.
    //
    // 위에서부터의 자리는 메뉴 막대가 비워 달라고 한 만큼 내려온다. 띠의 위아래를 함께
    // 옮겨야 띠가 그만큼 얇아지지 않는다.
    if (mStripRect)
    {
        mStripRect->SetOffsetMin({ 0.0f, mTopInset });
        mStripRect->SetOffsetMax({ 0.0f, mTopInset + layout.height });
    }
    // 도킹에 말하는 높이는 <b>비워 둔 자리까지 합한</b> 값이다. 띠의 높이만 말하면 패널이
    // 메뉴 막대의 두께만큼 위로 올라가 막대 아래에 깔린다.
    mRowHeight = mTopInset + layout.height;

    // 제목은 마지막 버튼 뒤에 이어 붙는다. 버튼 폭이 프레임마다 달라지므로 그 자리도 여기서
    // 정해야 하고, 접혀서 줄이 늘면 제목도 마지막 줄로 함께 내려간다.
    if (mTitleRect && !layout.buttons.empty())
    {
        const GameEngine::UI::UIRect& last = layout.buttons.back();
        const float titleLeft = last.x + last.width + ButtonInset * 2.0f;
        mTitleRect->SetOffsetMin({ titleLeft, last.y });
        mTitleRect->SetOffsetMax({ titleLeft + TitleWidth, last.y + last.height });
    }
}

void EditorToolbarView::SynchronizeTextMetrics(const float contentScale)
{
    // 글꼴 크기는 논리 단위로 설정한다. 줄바꿈 폭은 배치된 사각형의 폭을 사용한다.
    //
    // 줄바꿈 폭을 세우는 것이 특히 중요하다. 가운데 정렬은 "이 폭 안에서 가운데"라는 뜻이라,
    // 폭이 0(제한 없음)이면 글자가 자기 버튼이 아니라 알 수 없는 넓은 상자의 가운데에 놓인다.
    const auto applyTo = [contentScale](
        GameEngine::Runtime::TextRenderer* const label,
        const GameEngine::Runtime::RectTransform* const rect)
    {
        if (!label)
        {
            return;
        }
        label->SetFontSize(ToolbarLabelFontSize);
        if (rect)
        {
            label->SetMaxWidth(rect->GetResolvedRect().width);
        }
    };

    for (const ToolbarButton* const entry :
         { &mUndo, &mRedo, &mSnap, &mPlay })
    {
        applyTo(entry->label, entry->labelRect);
    }
    applyTo(mTitle, mTitleRect);
}

void EditorToolbarView::SynchronizeButtons()
{
    // Undo/Redo는 스택이 빈 쪽을 비활성화하고 disabled 색으로 표시한다.

    const bool inEditMode = !mContext.IsPlaying();
    if (mUndo.button)
    {
        mUndo.button->SetInteractable(inEditMode && mContext.GetUndoStack().CanUndo());
    }
    if (mRedo.button)
    {
        mRedo.button->SetInteractable(inEditMode && mContext.GetUndoStack().CanRedo());
    }

    const bool snapping = mContext.GetSettings().gridSnapEnabled;
    if (mSnap.label)
    {
        mSnap.label->SetText(snapping ? "Snap: On" : "Snap: Off");
    }
    if (mPlay.label)
    {
        mPlay.label->SetText(mContext.IsPlaying() ? "Stop" : "Play");
    }

    // 제목 표시줄: 어느 프로젝트의 어느 장면을 편집 중인지, 저장되지 않은 편집이 있는지.
    if (mTitle)
    {
        std::string title = "GameEditor";
        if (const GameEngine::App::ProjectFileData* project = mContext.GetOpenProject())
        {
            title = ToUtf8(project->settings.projectName);
            if (mContext.HasOpenScene())
            {
                const auto scenePath =
                    project->settings.scenePaths.find(mContext.GetOpenProjectSceneId());
                if (scenePath != project->settings.scenePaths.end())
                {
                    title += " - " + scenePath->second.generic_string();
                }
            }
            if (mContext.HasUnsavedChanges())
            {
                title += " *";
            }
            if (mContext.IsPlaying())
            {
                title += "   [Playing]";
            }
        }
        mTitle->SetText(std::move(title));
    }

    // 툴바 클릭을 처리한다. 프로젝트와 장면의 메뉴 명령은
    // <see cref="RunMenuCommand"/>가 이 클래스의 동작 함수를 호출한다.
    if (mUndo.button && mUndo.button->WasClickedThisFrame())
    {
        mPropertyEdit.PerformUndo();
    }
    if (mRedo.button && mRedo.button->WasClickedThisFrame())
    {
        mPropertyEdit.PerformRedo();
    }
    if (mSnap.button && mSnap.button->WasClickedThisFrame())
    {
        mContext.SetGridSnapEnabled(!snapping);
    }
    if (mPlay.button && mPlay.button->WasClickedThisFrame())
    {
        if (mContext.IsPlaying())
        {
            static_cast<void>(mContext.ExitPlayMode());
        }
        else if (mContext.HasOpenScene())
        {
            static_cast<void>(mContext.EnterPlayMode());
        }
        mPropertyEdit.ResetFieldEditingState();
    }
}

void EditorToolbarView::AskAboutMigrationIfNeeded()
{
    const SceneMigrationPlan* const plan = mContext.GetSceneMigrationPlan();
    if (!plan || mAskedAboutMigration)
    {
        return;
    }
    // 계획이 나타난 프레임에 한 번만 묻는다. 매 프레임 다시 물으면 사람이 답할 틈이 없다.
    mAskedAboutMigration = true;

    ConfirmationRequest request;
    request.title = "Scene references";
    request.question = plan->Describe();
    request.choices = { "Migrate" };
    request.cancelLabel = "Not now";
    // 막혀 있으면 진행은 보이되 눌리지 않는다. 무엇이 막고 있는지는 위의 글이 말하고 있고,
    // 누를 수 있는 버튼을 두면 눌러도 아무 일이 없는 버튼이 된다.
    if (plan->IsBlocked())
    {
        request.disabledChoices = { 0 };
    }
    request.onAnswered = [this](const std::optional<std::size_t> chosen)
    {
        if (!chosen)
        {
            mContext.DismissSceneMigration();
            return;
        }
        if (!mContext.ApplySceneMigration())
        {
            GameEngine::Diagnostics::Debug::LogError(
                "The scene references could not be migrated; the log says which scene stopped it.");
        }
    };
    mConfirmations.Ask(std::move(request));
}

void EditorToolbarView::AskAboutCleanupIfNeeded()
{
    const OrphanSidecarPlan* const plan = mContext.GetOrphanSidecarPlan();
    if (!plan || mAskedAboutCleanup)
    {
        return;
    }
    mAskedAboutCleanup = true;

    ConfirmationRequest request;
    request.title = "Leftover metadata";
    request.question = plan->Describe();
    // 되돌릴 수 없는 단계는 버튼이 그렇게 말한다. 두 단계가 같은 글자를 달고 있으면 사람이
    // "지난번처럼 치우는 것"이라 읽고 지우기를 누른다.
    request.choices = { plan->IsAskingToDelete() ? "Delete" : "Set aside" };
    request.cancelLabel = "Not now";
    request.onAnswered = [this](const std::optional<std::size_t> chosen)
    {
        if (!chosen)
        {
            // 아무것도 적지 않는다. 다음 스캔에서 같은 질문을 다시 받는다.
            mContext.DismissOrphanSidecarCleanup();
            return;
        }
        if (!mContext.ApplyOrphanSidecarCleanup())
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Some metadata files were left alone; the log names each one.");
        }
    };
    mConfirmations.Ask(std::move(request));
}

void EditorToolbarView::AskAboutDeletingScene(const unsigned int sceneId)
{
    mSceneAwaitingDelete = sceneId;

    // 무엇을 지우는지 이름으로 적는다. "정말 지울까"만으로는 사람이 무엇에 답하는지 모르고,
    // 이것은 이 에디터에서 사람의 파일을 없애는 유일한 자리다.
    std::optional<std::string> scenePath;
    if (const GameEngine::App::ProjectFileData* const project = mContext.GetOpenProject())
    {
        const auto entry = project->settings.scenePaths.find(sceneId);
        if (entry != project->settings.scenePaths.end())
        {
            scenePath = entry->second.generic_string();
        }
    }

    ConfirmationRequest request;
    request.title = "Delete scene";
    request.question = MakeDeleteSceneQuestion(scenePath);
    request.choices = { "Delete scene" };
    request.cancelLabel = "Cancel";
    request.onAnswered = [this](const std::optional<std::size_t> chosen)
    {
        if (chosen)
        {
            DeleteConfirmedScene();
        }
        mSceneAwaitingDelete.reset();
    };
    mConfirmations.Ask(std::move(request));
}

void EditorToolbarView::RunMenuCommand(const EditorMenuCommand command)
{
    // 장면을 떠나는 명령들은 저장되지 않은 편집 앞에서 먼저 묻는다. 그 물음을 여기서 새로
    // 만들지 않고 붙잡아 두는 기존 길을 쓰는 이유는, 물음이 두 벌이 되면 답이 온 뒤에 무엇을
    // 할지도 두 벌이 되기 때문이다 — 그리고 그 둘 중 하나만 고쳐지는 날이 온다.
    const auto askOrRun = [this](const PendingAction action, void (*run)(
                                     EditorContext&, IFileDialogs&))
    {
        if (mContext.HasUnsavedChanges())
        {
            AskBeforeLeavingScene(action);
            return;
        }
        run(mContext, mFileDialogs);
    };

// 표에 명령을 더하고 여기 붙이는 것을 잊으면 <b>빌드가 선다.</b> /W4는 이 경고를 내지 않으므로
// 이 자리에서만 오류로 올린다 — 「눌러도 아무 일이 없는 메뉴 항목」은 화면에서 고장과 구별되지
// 않고, 그것을 사람보다 먼저 잡을 수 있는 곳이 여기뿐이다.
#pragma warning(push)
#pragma warning(error : 4061 4062)
    switch (command)
    {
    case EditorMenuCommand::NewProject:
        askOrRun(PendingAction::NewProject, &CreateProjectWithDialog);
        return;

    case EditorMenuCommand::OpenProject:
        // 프로젝트 열기만은 확인 줄까지 받는다. 열고 나서 사고가 남긴 사본이 있으면 묻기
        // 때문이고, 그 물음은 파일 대화상자가 아니라 확인 줄이 세운다.
        if (mContext.HasUnsavedChanges())
        {
            AskBeforeLeavingScene(PendingAction::OpenProject);
        }
        else
        {
            OpenProjectWithDialog(
                mContext, mFileDialogs, mConfirmations, GetRecoveryDirectory());
        }
        return;

    case EditorMenuCommand::NewScene:
        askOrRun(PendingAction::NewScene, &CreateSceneWithDialog);
        return;

    case EditorMenuCommand::SaveScene:
        if (!mContext.SaveOpenScene())
        {
            GameEngine::Diagnostics::Debug::LogError("Failed to save the open scene.");
        }
        return;

    case EditorMenuCommand::RenameScene:
        askOrRun(PendingAction::RenameScene, &RenameSceneWithDialog);
        return;

    case EditorMenuCommand::DeleteScene:
        // 지우기는 두 번 묻는다. 저장되지 않은 편집이 있으면 떠나는 것을 먼저 묻고, 그 답이
        // 온 뒤에 무엇을 지우는지 이름으로 다시 묻는다 — 되돌릴 수 없는 쪽이 뒤에 온다.
        if (mContext.HasUnsavedChanges())
        {
            AskBeforeLeavingScene(PendingAction::DeleteScene);
        }
        else
        {
            AskToDeleteSelectedScene();
        }
        return;

    case EditorMenuCommand::Build:
    case EditorMenuCommand::CancelBuild:
        // 한 함수가 둘을 다 한다. 도는 동안 부르면 그만두게 하고 아니면 시작하므로, 두 항목이
        // 갈라지는 곳은 <b>어느 쪽이 활성인가</b>뿐이다 — 그 판정은 표가 한 번만 답한다.
        ToggleProjectBuild();
        return;

    case EditorMenuCommand::NewScript:
        // 스크립트를 만드는 것은 장면을 떠나지 않는다. 파일 하나가 프로젝트에 더해질 뿐이다.
        CreateScriptWithDialog(mContext, mFileDialogs);
        return;

    case EditorMenuCommand::NewMaterial:
        // 스크립트와 같은 이유로 장면을 떠나지 않는다.
        CreateMaterialWithDialog(mContext, mFileDialogs);
        return;

    case EditorMenuCommand::Undo:
        mPropertyEdit.PerformUndo();
        return;
    case EditorMenuCommand::Redo:
        mPropertyEdit.PerformRedo();
        return;

    case EditorMenuCommand::Exit:
        // 닫기는 툴바의 것이 아니다. 창을 쥔 곳이 부르며, 그 경로가 물음을 세운다.
        return;
    }
}
#pragma warning(pop)

void EditorToolbarView::AskBeforeLeavingScene(const PendingAction action)
{
    mPendingAction = action;
    AskAboutUnsavedChanges({});
}

void EditorToolbarView::AskAboutUnsavedChanges(const std::string& failure)
{
    ConfirmationRequest request;
    request.title = "Unsaved changes";
    // 실패는 물음에 한 줄로 붙는다. 실패했는데 물음이 사라지면 사람은 진행된 줄 알고, 그것이
    // 이 물음이 막으려던 바로 그 손실이다.
    request.question = failure.empty()
        ? MakeUnsavedChangesQuestion()
        : MakeUnsavedChangesQuestion() + "\n" + failure;
    request.choices = { "Save and continue", "Discard and continue" };
    request.cancelLabel = "Cancel";
    request.onAnswered = [this](const std::optional<std::size_t> chosen)
    {
        if (!chosen)
        {
            mPendingAction = PendingAction::None;
            return;
        }
        if (*chosen == 0 && !mContext.SaveOpenScene())
        {
            // 저장이 실패했는데 그대로 진행하면 저장한 줄 알고 잃는다. 붙잡은 동작을 쥔 채
            // 다시 묻는다 — 답 안에서 묻는 것이 이 줄의 다음 자리가 된다.
            GameEngine::Diagnostics::Debug::LogError(
                "Failed to save the open scene; the project was not changed.");
            AskAboutUnsavedChanges("Saving failed, so nothing has changed. See the Console.");
            return;
        }
        RunPendingAction();
    };
    mConfirmations.Ask(std::move(request));
}

void EditorToolbarView::RunPendingAction()
{
    const PendingAction action = mPendingAction;
    mPendingAction = PendingAction::None;
    switch (action)
    {
    case PendingAction::NewProject:
        CreateProjectWithDialog(mContext, mFileDialogs);
        break;
    case PendingAction::OpenProject:
        OpenProjectWithDialog(
            mContext, mFileDialogs, mConfirmations, GetRecoveryDirectory());
        break;
    case PendingAction::NewScene:
        CreateSceneWithDialog(mContext, mFileDialogs);
        break;
    case PendingAction::RenameScene:
        RenameSceneWithDialog(mContext, mFileDialogs);
        break;
    case PendingAction::DeleteScene:
        AskToDeleteSelectedScene();
        break;
    case PendingAction::None:
        break;
    }
}


void EditorToolbarView::AskToDeleteSelectedScene()
{
    // 묻는 것이 전부다. 실제 삭제는 사람이 확인 줄에서 한 번 더 눌러야 일어난다.
    if (const std::optional<unsigned int> sceneId = mContext.GetSelectedSceneId())
    {
        AskAboutDeletingScene(*sceneId);
        return;
    }
    GameEngine::Diagnostics::Debug::LogError("Choose a scene in the hierarchy to delete.");
}

void EditorToolbarView::DeleteConfirmedScene()
{
    const GameEngine::App::ProjectFileData* const project = mContext.GetOpenProject();
    if (!project || !mSceneAwaitingDelete)
    {
        mSceneAwaitingDelete.reset();
        return;
    }
    const unsigned int sceneId = *mSceneAwaitingDelete;
    mSceneAwaitingDelete.reset();

    const bool wasOpen = mContext.HasOpenScene() && mContext.GetOpenProjectSceneId() == sceneId;
    const unsigned int startBefore = project->settings.initialSceneId;
    const std::optional<GameEngine::App::ProjectFileData> updated =
        GameEngine::App::ProjectFile::RemoveScene(*project, sceneId);
    if (!updated)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene could not be deleted. A project must keep at least one scene.");
        return;
    }
    // 시작 장면이 옮겨 갔다면 조용히 두지 않는다 — 사람이 요청하지 않은 변경이다.
    if (updated->settings.initialSceneId != startBefore)
    {
        GameEngine::Diagnostics::Debug::Log(
            "The deleted scene was the starting scene; the project now starts at scene ",
            updated->settings.initialSceneId, ".");
    }
    mContext.ClearSceneSelection();
    // 지운 것이 편집 중이던 장면이면 다른 것을 열어 준다. 없어진 장면을 계속 편집하고 있는
    // 것처럼 보이는 상태로 두지 않는다.
    if (wasOpen)
    {
        const auto entry = updated->settings.scenePaths.find(updated->settings.initialSceneId);
        if (entry != updated->settings.scenePaths.end())
        {
            ReopenProjectAtScene(
                mContext, *updated, updated->GetAssetRootPath() / entry->second);
            return;
        }
    }
    if (!mContext.OpenProject(updated->filePath))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The scene was deleted, but the project could not be reopened.");
    }
}


void EditorToolbarView::ToggleProjectBuild()
{
    if (mProjectBuild.IsRunning())
    {
        mProjectBuild.Cancel();
        return;
    }
    const GameEngine::App::ProjectFileData* const project = mContext.GetOpenProject();
    if (!project)
    {
        GameEngine::Diagnostics::Debug::LogError("Open a project before building.");
        return;
    }
    // 빌드하는 것은 프로젝트의 CMake 타깃이고, 그 이름은 프로젝트 이름이다 — 에디터가 쓴
    // 빌드 스크립트가 그렇게 이름 짓는다.
    static_cast<void>(mProjectBuild.Start(
        // 빌드가 받는 것은 에셋 루트가 아니라 <b>코드</b>가 사는 디렉터리다. 둘은 서로 다른
        // 물음이고, 프로젝트 파일이 각각 따로 답한다 — 여기서 짐작하지 않는다.
        EditorBuildTree::OfThisEditor(), project->GetSourceRootPath(),
        ToUtf8(project->settings.projectName)));
}


}
