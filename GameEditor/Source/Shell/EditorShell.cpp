#include "Shell/EditorShell.h"

#include "Rules/EditorFonts.h"

#include <utility>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "Views/EditorDockLayout.h"
#include "Rules/EditorFloatingWindowRect.h"
#include "Rules/EditorPanelCommon.h"
#include "Diagnostics/Debug.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/PlatformServices.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Game.h"
#include "Runtime/Input.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"

namespace GameEditor
{

namespace
{
    using GameEngine::UI::UIRect;
}

EditorShell::EditorShell(EditorContext& context, ConfirmationQueue& confirmations)
    : mContext(context)
    // 에디터는 애플리케이션이므로 구체 구현을 고를 자격이 있다. UI 자신은 고르지 않는다.
    , mUI(
          GameEngine::Platform::PlatformServices::CreateClipboard(),
          GameEngine::Platform::PlatformServices::CreateTextRasterizer())
    // 패널은 셸 전체가 아니라 자기가 쓰는 것만 받는다. 인자 목록이 곧 그 패널이 셸에 무엇을
    // 기대는지의 목록이고, 아무것도 기대지 않는 둘은 아무것도 받지 않는다.
    , mHierarchy(*this, *this, context, mUI, confirmations)
    , mInspector(*this, *this, *this, context, mUI)
    , mContentBrowser(*this, *this, context, mUI)
    , mConsole(*this, context, mUI)
    , mSceneView(*this, *this, *this, context, mUI)
    , mGameView(context, mUI)
    , mTilemapTool(context)
    , mTilePalette(mUI, mTilemapTool)
{
    // 저장돼 있던 패널 배치를 되살린다. 크기가 다르거나 순열이 아니면 — 첫 실행, 손상된 파일,
    // 패널 수가 바뀐 구버전 — TrySetAssignment가 거절하고 기본 배정이 남는다.
    static_cast<void>(mPanelLayout.TrySetAssignment(context.GetSettings().panelInSlot));

    // 위젯이 스스로 그리는 글자 — 버튼 레이블, 선택 항목, 텍스트 필드 — 도 에디터의 본문
    // 크기를 쓴다. 문맥의 기본값은 엔진의 것이고, 그 값을 고르는 것은 애플리케이션의 일이다.
    mUI.SetBodyFontSize(RowFontSize);

    LoadEditorFonts();
}

void EditorShell::LoadEditorFonts()
{
    // 편집기가 읽는 자리는 실행 파일 옆의 자기 콘텐츠다. 무엇을 어떻게 싣는지는
    // EditorFonts가 알고, 셸은 어디서 읽을지만 말한다 — 그래야 시험이 다른 자리를 주며 같은
    // 코드를 부를 수 있다.
    const GameEngine::Platform::DirectoryContentSource content(
        GameEngine::Platform::PlatformServices::GetExecutableDirectory());
    GameEditor::LoadEditorFonts(content, mUI);
}

EditorShell::~EditorShell() = default;

void EditorShell::Collect(
    const GameEngine::Runtime::Game& game, GameEngine::Rendering::RenderFrameBuilder& builder)
{
    // 프로젝트 디렉터리의 변동을 프레임마다 듣는다. 렌더 타깃 검사보다 앞인 이유는, 그릴 수
    // 없는 프레임에도 파일은 바뀌기 때문이다.
    mContext.PollProjectAssetChanges();

    const GameEngine::Rendering::RenderTargetSize size = builder.GetRenderTargetSize();
    if (!size.IsValid())
    {
        return;
    }
    // 유지 모드 UI가 이번 프레임의 포인터를 가져갔으면 즉시 모드는 그 클릭을 보지 않는다.
    // 판정이 "가져갔다"로 잘못 기울면 에디터 전체가 눌리지 않으므로, 그 판정은 UI 요소가 커서
    // 아래 있을 때만 참이 되는 쪽 — 애매하면 거짓 — 으로 만들어져 있다.
    mUI.SetPointerConsumedExternally(game.DidUIConsumePointer());
    mUI.BeginFrame(game.GetInput(), size);

    // 포커스 세대: 포커스 필드가 바뀔 때마다 오른다. 같은 필드로 돌아온 타이핑이 이전 세션의
    // 편집과 한 undo로 합쳐지지 않는 경계가 이 수다.
    if (mUI.GetFocusedField() != mObservedFocusField)
    {
        mObservedFocusField = mUI.GetFocusedField();
        ++mFieldFocusGeneration;
    }
    HandleUndoShortcuts(game);

    const auto width = static_cast<float>(size.width);
    const auto height = static_cast<float>(size.height);
    mLastClientWidth = width;
    mLastClientHeight = height;

    // 슬롯 레이아웃: 좌측 컬럼 셋, 중앙 둘, 우측 하나 — Toolbar만 상단 고정이다. 비율과 한계는
    // EditorDockLayout이 값으로 들고 있고 여기서는 화면 배율만 곱한다: 그 계산이 창 없이
    // 시험되어야, 기본값을 바꿀 때 "인스펙터가 몇 줄을 담게 되는가"를 눈이 아니라 산술로 답할 수
    // 있다. 어느 패널이 어느 슬롯에 놓이는지만 mPanelLayout의 상태이고, 제목줄 드래그가 두
    // 패널의 슬롯을 맞바꾼다.
    DockSlotMetrics metrics;
    // 툴바가 말한 높이를 그대로 쓴다. 여기서 상수를 읽으면 접힌 툴바 아래에 패널이 깔린다.
    metrics.toolbarHeight = S(mToolbarHeight);
    metrics.leftMinimum = S(metrics.leftMinimum);
    metrics.leftMaximum = S(metrics.leftMaximum);
    metrics.rightMinimum = S(metrics.rightMinimum);
    metrics.rightMaximum = S(metrics.rightMaximum);
    metrics.minimumExtent = S(metrics.minimumExtent);

    // 툴바는 에디터 자신의 런타임에 놓인 유지 모드 UI 컴포넌트로 그린다.
    // 도킹 계산은 툴바 높이만큼 아래 패널의 자리를 비워 둔다.

    const std::array<UIRect, DockSlotCount> slotRects =
        ComputeDockSlotRects(width, height, metrics);
    for (std::size_t slot = 0; slot < DockSlotCount; ++slot)
    {
        DrawDockedPanel(static_cast<DockPanel>(mPanelLayout.GetPanelInSlot(slot)), slotRects[slot]);
    }
    // 떠 있는 창의 몸통은 도킹 패널을 모두 선언한 뒤에 선언한다. 즉시 모드끼리의 순서에는 새
    // 규칙이 필요 없다 — "커서 아래 마지막으로 선언된 것이 포인터를 갖는다"가 그대로 이 창에
    // 우선권을 주고, 그리는 순서도 같은 순서라 눈에 보이는 것과 눌리는 것이 갈라지지 않는다.
    DrawFloatingPanels();
    UpdatePanelDrag();
    UpdateAssetDrag();

    mUI.EndFrame(builder);
}

const char* EditorShell::GetDockPanelTitle(const DockPanel panel)
{
    switch (panel)
    {
    case DockPanel::Hierarchy: return "Hierarchy";
    case DockPanel::Inspector: return "Inspector";
    case DockPanel::ContentBrowser: return "Content Browser";
    case DockPanel::Scene: return "Scene";
    case DockPanel::Console: return "Console";
    case DockPanel::Game: return "Game";
    case DockPanel::TilePalette: return "Tile Palette";
    }
    return "Panel";
}

void EditorShell::DrawFloatingPanels()
{
    if (!mFloatingConsole.IsVisible())
    {
        return;
    }
    if (mFloatingConsole.WasDockRequested())
    {
        mFloatingConsole.SetVisible(false);
        mWindowDrag.Release();
        // 자리는 그대로 적어 둔다. 다시 띄울 때 마지막에 놓았던 곳에서 열린다.
        SaveConsoleWindowSetting(false);
        return;
    }
    // 제목줄은 유지 모드가 그렸고, 몸통은 여기서 그린다 — 바탕까지 함께. 유지 모드는 즉시
    // 모드 전체 위에 얹히므로, 몸통의 바탕을 저쪽에 두면 그것이 이쪽 내용을 덮는다.
    const UIRect content = mFloatingConsole.GetContentRect(mScale);
    mUI.DrawPanel(content, PanelColor);
    mConsole.Draw(content);
    UpdateFloatingWindowDrag();
}

void EditorShell::SaveConsoleWindowSetting(const bool floating)
{
    // 문서 모델은 UI를 모르므로 사각형이 아니라 수 넷을 받는다. 그것을 푸는 자리가 여기 하나다.
    const UIRect rect = mFloatingConsole.GetRect();
    mContext.UpdateConsoleWindowSetting(floating, rect.x, rect.y, rect.width, rect.height);
}

void EditorShell::UpdateFloatingWindowDrag()
{
    // 제목줄이 눌리기 시작한 프레임에 창을 앞으로 가져오고 손짓을 시작한다. 창이 그때 있던
    // 자리를 함께 적어 두는 이유는, 손짓이 답하는 것이 "얼마나 움직였는가"이고 "어디서부터"는
    // 창의 것이기 때문이다.
    // 포인터를 읽는 것은 즉시 모드의 값이 아니라 원시 값이다. 이 제스처를 시작한 것은 유지
    // 모드의 제목줄이고, 그 위에 커서가 있는 동안 즉시 모드는 "포인터를 남이 가져갔다"로 읽어
    // 커서를 화면 밖에 두고 버튼을 내려놓는다 — 그 값으로 끌면 창은 한 번도 움직이지 않는다.
    if (mFloatingConsole.ConsumeTitleBarPressStart())
    {
        mFloatingConsole.Raise();
        mWindowDragOriginX = mFloatingConsole.GetRect().x;
        mWindowDragOriginY = mFloatingConsole.GetRect().y;
        mWindowDrag.Release();
        mWindowDrag.Press(mUI.GetPointerX(), mUI.GetPointerY());
    }

    const GameEditor::DragGesture::Result result =
        mWindowDrag.Update(mUI.GetPointerX(), mUI.GetPointerY(), mUI.IsPointerDown());
    // 끌기가 끝난 프레임에 한 번만 적는다. 움직이는 매 프레임 저장하면 설정 파일이 초당 수십 번
    // 쓰인다 — 남길 값은 손을 뗀 자리 하나다.
    if (result.dropped || result.cancelled)
    {
        SaveConsoleWindowSetting(true);
    }
    if (!mWindowDrag.IsDragging())
    {
        return;
    }

    UIRect moved = mFloatingConsole.GetRect();
    // 손짓은 화면 픽셀로 말하고 창은 논리 픽셀로 산다. 배율로 나누는 자리가 여기 하나다.
    const float scale = mScale > 0.0f ? mScale : 1.0f;
    moved.x = mWindowDragOriginX + (mUI.GetPointerX() - mWindowDrag.GetStartX()) / scale;
    moved.y = mWindowDragOriginY + (mUI.GetPointerY() - mWindowDrag.GetStartY()) / scale;
    // 화면 밖으로 완전히 끌어내면 제목줄이 사라져 다시 잡을 수 없다 — 되돌릴 방법이 없는
    // 상태이므로 물린다. 배율이 곱해지기 전의 논리 픽셀로 재는 것이 창이 사는 단위다.
    mFloatingConsole.SetRect(ClampFloatingWindow(
        moved, mLastClientWidth / scale, mLastClientHeight / scale, MinimumVisibleWindow));
}

void EditorShell::DrawDockedPanel(const DockPanel panel, const GameEngine::UI::UIRect& rect)
{
    const UIRect content = DrawDockablePanelFrame(panel, rect);
    // 떠 있는 패널의 칸은 비워 두되 자리는 지킨다. 칸이 사라지면 도킹 산술과 저장된 배정이 함께
    // 흔들리고, 무엇보다 돌아올 자리가 없어진다 — 그 자리를 사람이 볼 수 있어야 되돌릴 수 있다.
    if (panel == DockPanel::Console && mFloatingConsole.IsVisible())
    {
        mUI.DrawLabel(
            { content.x + S(Padding), content.y + S(Padding), content.width - 2.0f * S(Padding),
              S(RowHeight) },
            "Floating — press Dock to return here", DimTextColor, SecondaryFontSize);
        return;
    }
    switch (panel)
    {
    case DockPanel::Hierarchy: mHierarchy.Draw(content); break;
    case DockPanel::Inspector: mInspector.Draw(content); break;
    case DockPanel::ContentBrowser: mContentBrowser.Draw(content); break;
    case DockPanel::Scene: mSceneView.Draw(content); break;
    case DockPanel::Console: mConsole.Draw(content); break;
    case DockPanel::Game: mGameView.Draw(content); break;
    case DockPanel::TilePalette: mTilePalette.Draw(content); break;
    }
}

GameEngine::UI::UIRect EditorShell::DrawDockablePanelFrame(
    const DockPanel panel, const GameEngine::UI::UIRect& rect)
{
    const std::size_t panelIndex = static_cast<std::size_t>(panel);
    mPanelRects[panelIndex] = rect;

    const float headerHeight = S(HeaderHeight);
    const float padding = S(Padding);
    const UIRect headerRect{ rect.x, rect.y, rect.width, headerHeight };
    mUI.DrawPanel(rect, PanelColor);
    mUI.DrawPanel(headerRect, HeaderColor);
    // 제목줄은 위젯이다: 눌림을 소비하므로 아래 위젯이 같은 눌림을 자기 것으로 읽지 않고,
    // 여기서 시작된 눌림은 패널 드래그의 시작 후보가 된다.
    const GameEngine::UI::UIContext::SelectableResult titleBar = mUI.DrawSelectable(
        GameEngine::UI::MakeWidgetId("panel-titlebar", panelIndex), headerRect, "", false);
    if (titleBar.clicked)
    {
        // 새로 집는다. 계층 창과 같은 이유로 먼저 놓는다.
        mPanelDragIndex = panelIndex;
        mPanelDrag.Release();
        mPanelDrag.Press(mUI.GetMouseX(), mUI.GetMouseY());
    }
    // 제목줄을 두 번 누르면 그 패널이 칸을 떠나 창으로 뜬다. 한 번 누르는 것은 이미 끌기의
    // 시작이라, 띄우기는 그것과 구별되는 몸짓이어야 한다.
    if (titleBar.doubleClicked && panel == DockPanel::Console && !mFloatingConsole.IsVisible())
    {
        mPanelDragIndex = DockSlotCount;
        mPanelDrag.Release();
        mFloatingConsole.SetVisible(true);
        mFloatingConsole.Raise();
        SaveConsoleWindowSetting(true);
    }
    // 패널을 끌고 이 패널 위에 있으면, 놓으면 슬롯이 맞바뀐다는 뜻으로 제목줄을 밝힌다.
    if (mPanelDrag.IsDragging() && mPanelDragIndex != panelIndex &&
        rect.Contains(mUI.GetMouseX(), mUI.GetMouseY()))
    {
        mUI.DrawPanel(headerRect, DropTargetColor);
    }
    // 패널 제목은 화면의 뼈대라 제목 폰트다 — 본문보다 굵어 패널의 경계가 눈에 먼저 든다.
    mUI.DrawLabel(
        { rect.x + padding, rect.y, rect.width - 2.0f * padding, headerHeight },
        GetDockPanelTitle(panel), TextColor, RowFontSize, GameEngine::UI::TextAlign::Left,
        GameEngine::UI::UIFontRole::Title);
    return { rect.x, rect.y + headerHeight, rect.width, rect.height - headerHeight };
}

void EditorShell::BeginAssetDrag(GameEngine::Assets::AssetReference reference)
{
    if (!reference.IsValid())
    {
        return;
    }
    mDraggedAsset = std::move(reference);
    mAssetDrag.Press(mUI.GetMouseX(), mUI.GetMouseY());
}

bool EditorShell::IsDraggingAsset() const
{
    return mAssetDrag.IsDragging() && mDraggedAsset.IsValid();
}

GameEngine::Assets::AssetReference EditorShell::TakeDraggedAsset()
{
    if (!IsDraggingAsset())
    {
        return {};
    }
    GameEngine::Assets::AssetReference taken = std::move(mDraggedAsset);
    mDraggedAsset = {};
    mAssetDrag.Release();
    return taken;
}

void EditorShell::UpdateAssetDrag()
{
    const float mouseX = mUI.GetMouseX();
    const float mouseY = mUI.GetMouseY();
    const GameEditor::DragGesture::Result result =
        mAssetDrag.Update(mouseX, mouseY, mUI.IsMouseDown(), S(6.0f));

    // 끌던 것을 아무도 받지 않은 채 놓았거나, 문턱을 못 넘고 끝났다. 어느 쪽이든 집은 것을
    // 놓는다 — 받은 경우에는 받는 쪽이 이미 가져갔으므로 여기 남아 있지 않다.
    if (result.dropped || result.cancelled)
    {
        mDraggedAsset = {};
    }
    if (!IsDraggingAsset())
    {
        return;
    }
    // 집은 에셋의 이름이 커서를 따라다닌다. 패널들보다 나중에 그려지므로 언제나 위에 보인다.
    mUI.DrawLabel(
        { mouseX + S(12.0f), mouseY - S(RowHeight) * 0.5f, S(320.0f), S(RowHeight) },
        mDraggedAsset.ToString(), TextColor, RowFontSize);
}

void EditorShell::UpdatePanelDrag()
{
    if (mPanelDragIndex >= DockSlotCount)
    {
        return;
    }
    const float mouseX = mUI.GetMouseX();
    const float mouseY = mUI.GetMouseY();

    // 계층 창과 같다: 뗌 표시 없이 버튼이 올라와 있으면 놓기가 아니라 취소다.
    if (!mUI.WasMouseReleased() && !mUI.IsMouseDown())
    {
        mPanelDrag.Release();
        mPanelDragIndex = DockSlotCount;
        return;
    }

    // 문턱은 계층 창의 것과 같다. 손이 떨리는 정도로는 패널이 움직이지 않아야 한다.
    const GameEditor::DragGesture::Result result =
        mPanelDrag.Update(mouseX, mouseY, mUI.IsMouseDown(), S(6.0f));

    if (mPanelDrag.IsDragging() || result.dropped)
    {
        // 집은 패널의 이름이 커서를 따라다닌다. 패널들보다 나중에 선언되므로 언제나 위에 보인다.
        mUI.DrawLabel(
            { mouseX + S(12.0f), mouseY - S(RowHeight) * 0.5f, S(240.0f), S(RowHeight) },
            GetDockPanelTitle(static_cast<DockPanel>(mPanelDragIndex)), TextColor, RowFontSize);
    }

    if (result.dropped || result.cancelled)
    {
        if (result.dropped)
        {
            for (std::size_t other = 0; other < DockSlotCount; ++other)
            {
                if (other != mPanelDragIndex && mPanelRects[other].Contains(mouseX, mouseY))
                {
                    if (mPanelLayout.SwapPanels(mPanelDragIndex, other))
                    {
                        // 배치가 바뀌는 사건이 곧 저장 시점이다. 다음 실행이 이 배치로 뜬다.
                        std::vector<std::size_t> panelInSlot(DockSlotCount);
                        for (std::size_t slot = 0; slot < DockSlotCount; ++slot)
                        {
                            panelInSlot[slot] = mPanelLayout.GetPanelInSlot(slot);
                        }
                        mContext.UpdatePanelLayoutSetting(std::move(panelInSlot));
                    }
                    break;
                }
            }
        }
        mPanelDragIndex = DockSlotCount;
    }
}

GameEngine::Rendering::RenderTargetSize EditorShell::GetGameViewSize() const
{
    return mGameView.GetViewSize();
}

GameEngine::UI::UIRect EditorShell::GetGameViewRect() const
{
    return mGameView.GetViewRect();
}

GameEngine::Rendering::RenderTargetSize EditorShell::GetSceneViewSize() const
{
    return mSceneView.GetViewSize();
}

GameEngine::Rendering::CameraRenderData EditorShell::GetSceneCamera() const
{
    return mSceneView.GetCamera();
}

void EditorShell::SetContentScale(const float scale)
{
    mScale = scale > 0.0f ? scale : 1.0f;
    mUI.SetScale(mScale);
}

void EditorShell::PresentGameImage(const GameEngine::Rendering::CapturedImage& image)
{
    mGameView.PresentImage(image);
}

void EditorShell::PresentSceneImage(const GameEngine::Rendering::CapturedImage& image)
{
    mSceneView.PresentImage(image);
}

void EditorShell::ApplyProperty(
    GameEngine::Runtime::Component& component,
    const GameEngine::Runtime::PropertyDescriptor& descriptor,
    const GameEngine::Runtime::PropertyValue& value,
    const std::uint64_t mergeKey)
{
    // 위젯은 종류에 맞는 값만 만들므로 거절은 내부 오류다.
    const GameEngine::Runtime::PropertyValue before = descriptor.Get(component);
    if (!descriptor.TrySet(component, value))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The inspector could not apply a property. property=", descriptor.GetName());
        return;
    }
    // 값이 그대로면 편집이 아니다. "1"을 "1.0"으로 고쳐 쓰는 타이핑이 기록되면 그 push가 redo
    // 스택을 지운다 — 아무것도 바꾸지 않은 손놀림이 역사를 가르면 안 된다.
    if (before == value)
    {
        return;
    }
    // Play 중의 편집은 라이브 튜닝이다: 적용은 하되 기록하지 않는다 — Play 이탈이 어차피 장면을
    // 스냅숏으로 되돌린다. 그 판단은 RecordEdit이 한다.
    mContext.RecordEdit(std::make_unique<PropertyEditCommand>(
        mContext, component.GetInstanceId(), std::string(descriptor.GetName()),
        before, value, mergeKey));
}

std::uint64_t EditorShell::MakeMergeKey(const GameEngine::UI::WidgetId source) const
{
    return source == 0
        ? 0
        : GameEngine::UI::MakeChildWidgetId(source, mFieldFocusGeneration);
}

void EditorShell::ResetFieldEditingState()
{
    mInspector.ClearFieldTexts();
    mUI.ClearFieldFocus();
}

void EditorShell::HandleUndoShortcuts(const GameEngine::Runtime::Game& game)
{
    // 텍스트 필드가 포커스를 갖는 동안의 키 입력은 필드 편집의 몫이고, Play 중의 장면은 이
    // 스택이 겨눈 문서가 아니다.
    if (mUI.IsAnyTextFieldFocused() || mContext.IsPlaying())
    {
        return;
    }
    const GameEngine::Runtime::Input& input = game.GetInput();
    if (!input.GetKey(GameEngine::Platform::Key::Control))
    {
        return;
    }
    const bool shift = input.GetKey(GameEngine::Platform::Key::Shift);
    if (input.GetKeyDown(GameEngine::Platform::Key::Z) && !shift)
    {
        PerformUndo();
    }
    else if (input.GetKeyDown(GameEngine::Platform::Key::Y) ||
        (shift && input.GetKeyDown(GameEngine::Platform::Key::Z)))
    {
        PerformRedo();
    }
    // 저장이 툴바 버튼 하나뿐이면 손이 자주 가지 않는다. 저장되지 않은 편집이 잃는 것이므로,
    // 저장을 누르기 쉽게 만드는 것도 그것을 막는 일의 일부다.
    else if (input.GetKeyDown(GameEngine::Platform::Key::S) && mContext.HasOpenScene())
    {
        if (!mContext.SaveOpenScene())
        {
            GameEngine::Diagnostics::Debug::LogError("Failed to save the open scene.");
        }
    }
}

void EditorShell::PerformUndo()
{
    UndoStack& stack = mContext.GetUndoStack();
    if (!stack.CanUndo())
    {
        return;
    }
    if (!stack.Undo())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Undo failed: the edit's target could no longer be resolved.");
    }
    // 되돌리기도 장면을 파일과 다르게 만든다 — 되돌려 파일과 같아졌더라도 그것을 셀 수는 없다.
    mContext.MarkEdited();
    // 값이 위젯 밑에서 바뀌었다. 남아 있는 칸 텍스트와 포커스는 이전 값의 것이다.
    ResetFieldEditingState();
    ClearDanglingSelection();
}

void EditorShell::PerformRedo()
{
    UndoStack& stack = mContext.GetUndoStack();
    if (!stack.CanRedo())
    {
        return;
    }
    if (!stack.Redo())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Redo failed: the edit's target could no longer be resolved.");
    }
    mContext.MarkEdited();
    ResetFieldEditingState();
    ClearDanglingSelection();
}

void EditorShell::ClearDanglingSelection()
{
    // undo/redo가 선택된 객체를 지웠을 수 있다. 재생성된 객체라면 별칭이 해석해 주므로, 여기
    // 걸리는 것은 정말로 사라진 id뿐이다.
    if (mContext.GetSelectedInstanceId() != 0 && !mContext.GetSelectedObject())
    {
        mContext.SelectObject(0);
    }
}

}

namespace GameEditor
{

void EditorShell::BuildRetainedViews(GameEngine::Runtime::Scene& scene)
{

    // 떠 있는 창들이 모이는 층이다. 툴바의 캔버스보다 뒤에 만들어지므로 계층 순서에서 뒤 —
    // 곧 위 — 이고, 그것이 떠 있는 창이 툴바 위에 그려지고 툴바의 버튼을 가리는 근거다.
    mFloatingLayer = scene.CreateGameObject("EditorFloatingLayer");
    if (!mFloatingLayer || !mFloatingLayer->AddComponent<GameEngine::Runtime::Canvas>())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "The editor could not create the layer its floating panels live on.");
        return;
    }
    mFloatingConsole.Build(scene, *mFloatingLayer, GetDockPanelTitle(DockPanel::Console));

    // 지난 실행에서 콘솔이 떠 있었으면 그 자리에 다시 세운다. 자리를 잃으면 사람이 배치한 것이
    // 매 실행 사라지고, 그러면 창을 옮기는 기능 자체를 쓰지 않게 된다.
    const GameEngine::App::EditorSettingsData& settings = mContext.GetSettings();
    if (settings.consoleFloating)
    {
        mFloatingConsole.SetRect({ settings.consoleWindowX, settings.consoleWindowY,
                                   settings.consoleWindowWidth, settings.consoleWindowHeight });
        mFloatingConsole.SetVisible(true);
    }
}

void EditorShell::SynchronizeRetainedViews(const float contentScale)
{
    mFloatingConsole.Synchronize(contentScale);
}

}
