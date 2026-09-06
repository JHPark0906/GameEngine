#pragma once

// editor-layer: 3 (Shell)

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <functional>
#include <string>

#include "App/IGameBootstrap.h"
#include "Rules/EditorCloseDecision.h"
#include "Runtime/Game.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Shell/EditorShell.h"
#include "Rules/EditorConfirmation.h"
#include "Views/EditorConfirmationView.h"
#include "Rules/EditorFileDialogs.h"
#include "Views/EditorMenuBarView.h"
#include "Views/EditorToolbarView.h"

namespace GameEditor
{

class EditorContext;

/// <summary>
/// 공통 엔진 Application 위에 에디터를 세운다.
///
/// 에디터의 주 프레임은 장면이 아니라 UI다: CreateRenderFrontends가 셸의 UI를 싣는
/// 프론트엔드를 내놓아 창에는 에디터가 그려지고, 게임과 씬은 캡처 뷰로 렌더링되어 이미지로
/// 셸에 도착한다. 그래서 에디터는 창을 직접 만들거나 플랫폼 컨트롤을 쓰지 않는다 — 엔진이
/// 그리는 모든 곳에서 에디터도 그려진다.
/// </summary>
class EditorBootstrap final : public GameEngine::App::IGameBootstrap
{
public:
    EditorBootstrap();
    ~EditorBootstrap() override;

    [[nodiscard]] bool Initialize(
        GameEngine::Runtime::Game& game,
        GameEngine::Platform::IWindow& window) override;

    /// <summary>
    /// 주 프레임을 셸의 UI로 채우는 프론트엔드다. Application이 Initialize보다 먼저 부르므로,
    /// 프론트엔드는 셸을 직접 받는 대신 셸이 놓일 자리를 받는다.
    /// </summary>
    [[nodiscard]] std::vector<std::unique_ptr<GameEngine::Rendering::IRenderFrontend>>
        CreateRenderFrontends(
            std::shared_ptr<GameEngine::Rendering::TextRasterizationCache> worldTextCache)
            override;

    /// <summary>
    /// 편집기가 자기 UI에 쓰는 세 폰트를 sceneTextCache에도 등록한다.
    /// 폰트 없는 캐시는 어떤 문자열도 그릴 수 없으므로 Game View의 장면 텍스트에도 필요하다.
    /// </summary>
    void RegisterSceneFonts(
        GameEngine::Rendering::TextRasterizationCache& worldTextCache) override;

    /// <summary>
    /// 에디터 설정 파일이 말하는 그래픽 백엔드다. 에디터는 편집 대상 프로젝트를 열기 전에 창을
    /// 세우므로, 그 프로젝트의 설정이 아니라 자기 설정이 이 물음에 답한다.
    /// </summary>
    [[nodiscard]] std::optional<std::string> GetGraphicsBackendSetting() const override;

    /// <summary>편집 대상 프로젝트의 런타임을 진행시킨다. 엔진이 부팅한 런타임은 엔진이 돌린다.</summary>
    void Update(float deltaTime) override;

    /// <summary>0번이 주 뷰(게임 뷰), 1번이 편집 카메라를 실은 씬 뷰다. 둘 다 프로젝트 런타임을 그린다.</summary>
    void CollectCaptureViews(std::vector<CaptureView>& views) override;
    void OnViewCaptured(
        std::size_t viewIndex, const GameEngine::Rendering::CapturedImage& image) override;

    /// <summary>
    /// 실행이 곧 끝난다는 알림을 받아, 저장되지 않은 편집을 복구 파일로 남긴다.
    /// </summary>
    /// <param name="reason">무엇이 실패했는지다.</param>
    void OnUnrecoverableFailure(const char* reason) override;

    /// <summary>
    /// 저장되지 않은 편집이 있으면 물어보고, 사람이 그만두기를 고르면 닫지 않는다.
    /// 저장할 것이 없으면 묻지 않고 닫는다.
    /// </summary>
    [[nodiscard]] bool ShouldClose() override;

public:
    /// <summary>
    /// 종료할 때 저장 여부를 묻는 자리다. 기본값은 진짜 대화상자를 띄우고, 시험은 여기에 답을
    /// 정해 둔 것을 끼운다 — 정적 호출로 두면 시험이 사람을 기다리는 창을 띄우게 된다.
    /// </summary>
    /// <param name="ask">물음 한 번에 답 하나를 주는 것이다.</param>

private:
    /// <summary>
    /// Play 중 이번 프레임의 입력을 에디터와 게임 중 누가 받는지 가른다. 게임 뷰를 클릭하면
    /// 게임이 쥐고, Esc로 놓는다 — 규칙 자체는 <c>EditorPlayInputFocus</c>에 있고 창을 모른다.
    /// </summary>
    void RoutePlayInput();

    /// <summary>
    /// 메뉴 막대의 이번 프레임이다. 무엇을 고를 수 있는지를 문서 모델에서 받아 넘기고, 골라진
    /// 명령이 있으면 그것을 <b>이미 그 명령을 쥔 곳</b>으로 보낸다 — 툴바가 쥔 것은 툴바로,
    /// 닫기는 창으로. 메뉴 자신은 아무 동작도 갖지 않는다.
    ///
    /// 툴바보다 먼저 도는 이유는 툴바가 자기 자리를 정할 때 위에 비워 둘 높이를 이미 알고
    /// 있어야 하기 때문이다. 뒤집으면 막대가 두꺼워진 프레임에 툴바가 한 프레임 늦게 내려온다.
    /// </summary>
    /// <param name="contentScale">창의 콘텐츠 배율이다.</param>
    void UpdateMenuBar(float contentScale);

    /// <summary>
    /// 저장되지 않은 편집을 두고 닫을지 묻는다. 저장이 실패하면 실패를 한 줄 붙여 다시 묻는다.
    /// </summary>
    /// <param name="failure">붙일 실패 문구다. 비어 있으면 처음 묻는 것이다.</param>
    void AskAboutClosing(const std::string& failure);

private:

    /// <summary>되살리기를 이미 물었는지다. 첫 프레임이 그려진 뒤 한 번만 묻는다.</summary>
    bool mAskedAboutRecovery = false;
    /// <summary>닫을지 이미 묻고 있는지다. 닫기를 연타해도 물음은 하나다.</summary>
    bool mAskingAboutClosing = false;

    std::unique_ptr<EditorContext> mContext;
    std::unique_ptr<EditorShell> mShell;
    /// <summary>유지 모드 상단 툴바다. 에디터 자신의 런타임 장면에 산다.</summary>
    /// <summary>
    /// 사람에게 묻는 것들이 서는 줄이다. 툴바와 계층이 물음을 올리고,
    /// 아래의 뷰가 맨 앞의 물음 하나를 공통 창으로 세운다.
    /// </summary>
    ConfirmationQueue mConfirmations;
    /// <summary>플랫폼의 파일 대화상자다. 툴바가 경로를 물을 때 지나는 길이다.</summary>
    PlatformFileDialogs mFileDialogs;
    std::unique_ptr<EditorConfirmationView> mConfirmationView;

    std::unique_ptr<EditorToolbarView> mToolbar;

    /// <summary>
    /// 메뉴 막대다. 툴바 위에 서고, 고른 명령을 툴바와 창에게 넘긴다 — 자기 동작은 없다.
    /// </summary>
    std::unique_ptr<EditorMenuBarView> mMenuBar;

    /// <summary>메뉴 막대가 사는 캔버스다. 배율을 프레임마다 맞춘다.</summary>
    GameEngine::Runtime::GameObject* mMenuBarCanvas = nullptr;
    /// <summary>엔진이 부팅한 이 프로세스 자신의 런타임이다. 에디터 UI가 사는 곳이다.</summary>
    GameEngine::Runtime::Game* mGame = nullptr;
    /// <summary>프론트엔드와 공유하는 셸 자리이다. Initialize가 채우고 소멸자가 비운다.</summary>
    std::shared_ptr<EditorShellSlot> mShellSlot = std::make_shared<EditorShellSlot>();
    /// <summary>콘텐츠 배율을 프레임마다 묻는 창이다. Initialize에서 받는다.</summary>
    GameEngine::Platform::IWindow* mWindow = nullptr;
};

}
