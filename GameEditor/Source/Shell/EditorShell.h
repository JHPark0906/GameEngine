#pragma once

// editor-layer: 3 (Shell)

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "Views/EditorConsolePanel.h"
#include "Views/EditorFloatingPanelView.h"
#include "Assets/AssetReference.h"
#include "Core/DragGesture.h"
#include "Views/EditorContentBrowserPanel.h"
#include "Views/EditorGameViewPanel.h"
#include "Views/EditorHierarchyPanel.h"
#include "Views/EditorDockLayout.h"
#include "Rules/EditorToolbarLayout.h"
#include "Rules/EditorConfirmation.h"
#include "Rules/EditorPanelHosts.h"
#include "Views/EditorInspectorPanel.h"
#include "Views/EditorSceneViewPanel.h"
#include "Views/EditorTilemapTool.h"
#include "Views/EditorTilePalettePanel.h"
#include "Rendering/IRenderFrontend.h"
#include "Rendering/RenderFrame.h"
#include "Runtime/PropertyDescriptor.h"
#include "UI/PanelSlotLayout.h"
#include "UI/UIContext.h"

namespace GameEngine::Runtime
{
class Scene;
class Game;
class Component;
}

namespace GameEditor
{

class EditorContext;

/// <summary>
/// 엔진이 그리는 에디터 UI 전체이다.
///
/// 각 패널의 UIContext 그리기 목록을 주 프레임의 builder에 싣는다.
/// 이 셸은 엔진의 렌더링과 입력을 사용한다.
///
/// 패널 하나하나는 자기 상태를 가진 별도 클래스다. 셸에 남는 것은 패널들이 공유하는 것들이다:
/// 슬롯 레이아웃과 제목줄 드래그, 화면 배율, 캡처 뷰 계약의 중계, 그리고 undo — 속성 쓰기가
/// 기록되는 ApplyProperty 길목과 Ctrl+Z/Ctrl+Y. 선택은 패널들의 공유 상태지만 셸의 것도
/// 아니다: 문서 상태라서 EditorContext가 소유한다.
///
/// 게임 뷰와 씬 뷰의 그림은 셸이 그리지 않는다: 엔진 루프가 캡처 뷰로 렌더링해 준 이미지를
/// 뷰 패널이 받아 두었다가, 다음 프레임에 동적 텍스처로 UI에 붙인다. 뷰의 크기와 씬 뷰의 편집
/// 카메라는 뷰 패널이 마지막 프레임의 레이아웃에서 답하고, 셸은 그 답을 중계한다.
/// </summary>
class EditorShell final
    : public IEditorScale
    , public IPropertyEditHost
    , public IAssetDragHost
    , public ISceneToolHost
{
public:
    EditorShell(EditorContext& context, ConfirmationQueue& confirmations);
    ~EditorShell();

    /// <summary>UI 프레임 전체를 선언하고 builder에 싣는다. 렌더 프론트엔드가 호출한다.</summary>
    void Collect(
        const GameEngine::Runtime::Game& game, GameEngine::Rendering::RenderFrameBuilder& builder);

    /// <summary>게임 뷰가 이번 프레임에 캡처받을 픽셀 크기이다. 첫 프레임 전에는 무효다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderTargetSize GetGameViewSize() const;

    /// <summary>
    /// 게임 뷰가 이번 프레임에 그려진 자리다. 물리 픽셀이고 원점은 그리는 면의 좌상단이라,
    /// 창 입력의 커서 좌표와 같은 단위로 비교할 수 있다.
    /// </summary>
    [[nodiscard]] GameEngine::UI::UIRect GetGameViewRect() const;

    /// <summary>씬 뷰가 이번 프레임에 캡처받을 픽셀 크기이다. 첫 프레임 전에는 무효다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderTargetSize GetSceneViewSize() const;

    /// <summary>씬 뷰의 편집 카메라이다. 궤도 상태 — 피벗, 거리, 요, 피치 — 에서 만든다.</summary>
    [[nodiscard]] GameEngine::Rendering::CameraRenderData GetSceneCamera() const;

    void PresentGameImage(const GameEngine::Rendering::CapturedImage& image);
    void PresentSceneImage(const GameEngine::Rendering::CapturedImage& image);

    /// <summary>창의 콘텐츠 배율이다. HiDPI 화면에서 글자와 위젯이 같은 크기로 보이게 한다.</summary>
    void SetContentScale(float scale);

    /// <summary>
    /// 툴바가 이번 프레임에 차지한 높이다. 논리 픽셀이며, 도킹은 이 높이만큼을 비워 둔다.
    ///
    /// 셸이 스스로 정하지 않고 받아 두는 이유는, 그 높이를 아는 쪽이 툴바이기 때문이다.
    /// 접히면 두 줄이 되고 그때 패널들이 내려가야 하는데, 셸이 자기 상수를 들고 있으면
    /// 그 상수는 툴바가 두 줄이 된 사실을 알 길이 없다.
    /// </summary>
    void SetToolbarHeight(float logicalHeight) { mToolbarHeight = logicalHeight; }

    /// <summary>
    /// 유지 모드로 그려지는 패널 조각들을 에디터 자신의 런타임 장면에 세운다. 부트스트랩이
    /// 그 장면을 찾은 뒤 한 번 부른다.
    /// </summary>
    /// <param name="scene">에디터 프로세스 자신의 런타임 장면이다.</param>
    void BuildRetainedViews(GameEngine::Runtime::Scene& scene);

    /// <summary>유지 모드 조각들의 프레임 상태를 맞춘다. 배치 전에, 매 프레임 부른다.</summary>
    /// <param name="contentScale">창의 콘텐츠 배율이다.</param>
    void SynchronizeRetainedViews(float contentScale);


private:
    // 패널들이 쓰는 셸의 서비스는 아래 인터페이스들로 나간다. 재정의는 접근 지정과 무관하므로
    // 여기 private에 선언해도 인터페이스를 통한 호출은 되고, 셸 자신의 공개 표면은 bootstrap과
    // 프론트엔드가 쓰는 위의 계약 그대로 남는다.

    /// <summary>96 DPI 기준의 논리 길이를 이 화면의 픽셀로 바꾼다.</summary>
    [[nodiscard]] float S(const float logical) const override { return logical * mScale; }

    // ---- 에셋 끌어 놓기
    //
    // 상태를 셸이 쥐는 이유는 집는 곳과 놓는 곳이 서로 다른 패널이기 때문이다. 콘텐츠 브라우저가
    // 집고 인스펙터가 받는데, 둘 중 하나가 상태를 들고 있으면 다른 쪽이 읽을 수 없다. 두 패널을
    // 모두 그리는 것이 셸이고, 패널 제목줄 드래그도 같은 이유로 여기 있다.

    /// <summary>
    /// 커서 아래의 에셋을 집는다. 콘텐츠 브라우저가 행을 누른 프레임에 부른다. 문턱을 넘기
    /// 전까지는 끌기가 아니므로, 이 호출만으로는 화면에 아무 일도 일어나지 않는다.
    /// </summary>
    /// <param name="reference">집은 에셋을 가리키는 참조다.</param>
    void BeginAssetDrag(GameEngine::Assets::AssetReference reference) override;

    /// <summary>문턱을 넘어 실제로 에셋을 끌고 있는지다. 받을 수 있는 칸이 강조를 켜는 조건이다.</summary>
    [[nodiscard]] bool IsDraggingAsset() const override;

    /// <summary>끌고 있는 에셋이다. 끌고 있지 않으면 빈 참조다.</summary>
    [[nodiscard]] const GameEngine::Assets::AssetReference& GetDraggedAsset() const override
    {
        return mDraggedAsset;
    }

    /// <summary>
    /// 끌던 에셋을 받아 가고 끌기를 끝낸다. 받는 칸이 뗌을 확인한 프레임에 한 번 부른다.
    /// 끌고 있지 않으면 빈 참조를 돌려주고 아무것도 바꾸지 않는다.
    /// </summary>
    [[nodiscard]] GameEngine::Assets::AssetReference TakeDraggedAsset() override;

    /// <summary>
    /// 타일 팔레트와 씬 뷰가 함께 쓰는 페인팅 도구라 셸이 소유한다.
    /// 팔레트는 타일셋과 붓·지우개를 그리기 위해 구체 타입을 받고,
    /// 씬 뷰는 입력 계약인 <see cref="GetSceneTool"/>로 받는다.
    /// </summary>
    [[nodiscard]] EditorTilemapTool& GetTilemapTool() { return mTilemapTool; }

    /// <summary>
    /// 씬 뷰의 입력을 나눠 갖는 도구다. 지금은 타일 페인팅 하나뿐이지만, 씬 뷰가 아는 것은 캡처
    /// 계약뿐이라 그것이 어떤 도구인지는 씬 뷰의 관심 밖이다.
    /// </summary>
    [[nodiscard]] ISceneTool& GetSceneTool() override { return mTilemapTool; }

    // ---- 패널들이 쓰는 undo 서비스 ----

    /// <summary>
    /// 인스펙터의 모든 속성 쓰기가 지나는 유일한 길목이다 — 그래서 undo 기록도 여기 한 곳에
    /// 걸린다. mergeKey가 같은 연속 편집은 스택에서 한 단계로 합쳐진다.
    /// </summary>
    void ApplyProperty(
        GameEngine::Runtime::Component& component,
        const GameEngine::Runtime::PropertyDescriptor& descriptor,
        const GameEngine::Runtime::PropertyValue& value,
        std::uint64_t mergeKey) override;

    /// <summary>
    /// 편집의 병합 키를 만든다: 편집이 온 텍스트 필드와 현재 포커스 세대를 섞는다. 필드 밖의
    /// 편집(source 0)은 키 0 — 병합 없음 — 이다. 세대는 포커스 필드가 바뀔 때마다 오르므로,
    /// 같은 필드라도 포커스를 잃었다 얻으면 새 undo 단계가 된다.
    /// </summary>
    [[nodiscard]] std::uint64_t MakeMergeKey(GameEngine::UI::WidgetId source) const override;

    /// <summary>
    /// 칸 텍스트와 필드 포커스를 버린다. 장면 상태가 위젯 밑에서 바뀌는 곳 — undo/redo, 객체
    /// 삭제 — 이 부른다.
    /// </summary>
    void ResetFieldEditingState() override;

    void PerformUndo() override;
    void PerformRedo() override;
    /// <summary>undo/redo 뒤, 더는 해석되지 않는 선택 id를 거둔다.</summary>
    void ClearDanglingSelection();

    /// <summary>Ctrl+Z / Ctrl+Y(또는 Ctrl+Shift+Z)다. 텍스트 필드 포커스 중에는 비켜 간다.</summary>
    void HandleUndoShortcuts(const GameEngine::Runtime::Game& game);

    // ---- 패널 도킹 ----

    /// <summary>
    /// 도킹 슬롯에 놓이는 에디터 패널이다. 열거 순서가 곧 기본 배정이다: 패널 i가 슬롯 i에서
    /// 시작한다. 슬롯의 순서는 좌상·좌중·좌하·중앙상·중앙하·우측이고, Toolbar는 슬롯이 아니라
    /// 언제나 상단 고정이다.
    /// </summary>
    enum class DockPanel : std::size_t
    {
        Hierarchy,
        Inspector,
        ContentBrowser,
        Scene,
        Console,
        Game,
        /// <summary>타일 팔레트 패널이다.</summary>
        TilePalette,
    };
    // 패널은 슬롯 하나씩을 차지하므로 패널 수를 따로 세지 않는다 — 슬롯 수가 곧 패널 수다.
    // 두 값을 나란히 적어 두면 한쪽만 고쳐도 아무도 알려 주지 않는다. 열거자를 늘렸는데
    // 슬롯을 늘리지 않으면 이 assert가 그 자리에서 막는다.
    static_assert(
        static_cast<std::size_t>(DockPanel::TilePalette) + 1 == DockSlotCount,
        "패널 열거자 수와 도킹 슬롯 수는 같아야 한다 — 패널 하나가 슬롯 하나에 놓인다.");

    /// <summary>슬롯에 배정된 패널 하나를 그 자리에 그린다: 제목줄 프레임, 그리고 패널 내용.</summary>
    /// <summary>에디터 UI의 역할별 폰트를 자기 콘텐츠에서 읽어 배정한다. 생성 때 한 번이다.</summary>
    void LoadEditorFonts();
    void DrawDockedPanel(DockPanel panel, const GameEngine::UI::UIRect& rect);
    /// <summary>
    /// 패널의 바탕과 제목줄을 그리고, 남는 내용 영역을 반환한다. 제목줄은 위젯이다: 여기서의
    /// 눌림이 패널 드래그의 시작 후보이고, 눌림을 소비하므로 드래그의 뗌이 다른 위젯의 클릭으로
    /// 읽히지 않는다. 패널을 끌던 중이면 드롭 대상 패널의 제목줄을 강조색으로 밝힌다.
    /// </summary>
    [[nodiscard]] GameEngine::UI::UIRect DrawDockablePanelFrame(
        DockPanel panel, const GameEngine::UI::UIRect& rect);
    /// <summary>떠 있는 창들의 몸통을 그린다. 도킹 패널을 모두 선언한 뒤에 부른다.</summary>
    void DrawFloatingPanels();
    /// <summary>콘솔 창의 상태와 자리를 설정에 남긴다. 사각형을 수 넷으로 푸는 자리다.</summary>
    void SaveConsoleWindowSetting(bool floating);
    /// <summary>제목줄을 잡고 창을 옮긴다. 손짓은 DragGesture가 쥔다.</summary>
    void UpdateFloatingWindowDrag();
    /// <summary>제목줄 드래그를 진행시킨다. 다른 패널 위에서 놓으면 두 패널의 슬롯이 맞바뀐다.</summary>
    void UpdatePanelDrag();
    /// <summary>에셋 끌기를 한 프레임 진행하고, 끌고 있으면 커서 옆에 그 이름을 그린다.</summary>
    void UpdateAssetDrag();
    [[nodiscard]] static const char* GetDockPanelTitle(DockPanel panel);

    EditorContext& mContext;
    GameEngine::UI::UIContext mUI;
    float mScale = 1.0f;

    // 패널들. mUI 뒤에 선언된다 — 생성자에서 mUI의 참조를 받는다.
    /// <summary>계층 패널의 저장 질문이다. 유지 모드라 패널보다 먼저 살아야 한다.</summary>
    EditorHierarchyPanel mHierarchy;
    EditorInspectorPanel mInspector;
    EditorContentBrowserPanel mContentBrowser;
    EditorConsolePanel mConsole;
    EditorSceneViewPanel mSceneView;
    EditorGameViewPanel mGameView;
    EditorTilemapTool mTilemapTool;
    /// <summary>도구보다 뒤에 선언된다: 패널이 도구를 참조하므로 도구가 먼저 있어야 한다.</summary>
    EditorTilePalettePanel mTilePalette;

    // undo 병합 상태: 포커스가 몇 번째 세대인지. 편집이 어느 필드에서 왔는지는 인스펙터가 안다.
    /// <summary>지난 프레임에 관찰한 포커스 필드다. 달라질 때마다 세대가 오른다.</summary>
    GameEngine::UI::WidgetId mObservedFocusField = 0;
    /// <summary>포커스 세대다. 같은 필드라도 포커스를 잃었다 얻으면 병합이 끊긴다.</summary>
    std::uint64_t mFieldFocusGeneration = 0;

    /// <summary>
    /// 패널 → 슬롯 배정이다. 제목줄 드래그의 드롭이 배정을 바꾸고 설정에 저장한다.
    /// </summary>
    GameEngine::UI::PanelSlotLayout mPanelLayout{ DockSlotCount };
    /// <summary>이번 프레임에 각 패널이 그려진 사각형이다. 드롭 대상 판정과 강조가 읽는다.</summary>
    std::array<GameEngine::UI::UIRect, DockSlotCount> mPanelRects{};
    // 패널 제목줄 드래그: 어느 패널을 어디서 집었고, 드래그로 인정될 만큼 움직였는지.
    /// <summary>집고 있는 패널의 번호다. <c>DockSlotCount</c>면 아무것도 집지 않았다.</summary>
    std::size_t mPanelDragIndex = DockSlotCount;
    /// <summary>그 패널을 끄는 손짓이다. 계층 창의 드래그와 같은 규칙을 쓴다.</summary>
    GameEngine::Core::DragGesture mPanelDrag;
    /// <summary>에셋 끌기의 손짓이다. 문턱과 놓기 판정을 이것이 쥔다.</summary>
    GameEngine::Core::DragGesture mAssetDrag;
    /// <summary>끌고 있는 에셋이다. 집지 않았으면 빈 참조다.</summary>
    GameEngine::Assets::AssetReference mDraggedAsset;

    /// <summary>떠 있는 창들이 모이는 층이다. 창끼리의 순서가 이 층의 형제 순서다.</summary>
    GameEngine::Runtime::GameObject* mFloatingLayer = nullptr;
    /// <summary>콘솔이 도킹을 떠났을 때 그 몸통이 사는 창이다.</summary>
    EditorFloatingPanelView mFloatingConsole;
    /// <summary>
    /// 창을 옮기는 손짓이다. 패널 제목줄·에셋 끌기와 같은 설비를 쓴다 — 누름 자리도 문턱도
    /// 잡힘도 여기서 다시 만들지 않는다.
    /// </summary>
    GameEngine::Core::DragGesture mWindowDrag;
    /// <summary>
    /// 끌기가 시작된 순간 창이 있던 자리다. 이동량은 손짓이 답하고, 어디서부터 움직이는지는
    /// 창의 것이라 여기에 둔다.
    /// </summary>
    float mWindowDragOriginX = 0.0f;
    float mWindowDragOriginY = 0.0f;
    /// <summary>이번 프레임에 그릴 수 있는 자리의 크기다. 창을 화면 안으로 물리는 데 쓴다.</summary>
    float mLastClientWidth = 0.0f;
    /// <summary>툴바가 말한 이번 프레임의 높이다. 논리 픽셀이다.</summary>
    float mToolbarHeight = ToolbarLayoutMetrics{}.rowHeight;
    float mLastClientHeight = 0.0f;
};

/// <summary>
/// 셸이 놓이는 자리다. bootstrap과 프론트엔드가 함께 쥔다: bootstrap은 셸을 만들 때 채우고
/// 사라질 때 비우며, 프론트엔드는 비어 있으면 아무것도 싣지 않는다. 자리 자체가 공유
/// 소유라서 둘 중 무엇이 먼저 사라지든 대롱거리는 참조가 없다.
/// </summary>
struct EditorShellSlot
{
    EditorShell* shell = nullptr;
};

/// <summary>
/// 에디터 주 프레임의 렌더 프론트엔드이다: 장면 대신 셸의 UI를 싣는다. 셸은 bootstrap이
/// Initialize에서 만들므로, 그 전에 프레임이 돌면 아무것도 싣지 않는다.
/// </summary>
class EditorUIFrontend final : public GameEngine::Rendering::IRenderFrontend
{
public:
    explicit EditorUIFrontend(std::shared_ptr<EditorShellSlot> slot) : mSlot(std::move(slot)) {}

    void Collect(
        const GameEngine::Runtime::Game& game,
        GameEngine::Rendering::RenderFrameBuilder& builder) override
    {
        if (mSlot && mSlot->shell)
        {
            mSlot->shell->Collect(game, builder);
        }
    }

private:
    std::shared_ptr<EditorShellSlot> mSlot;
};

}
