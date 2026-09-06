#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
#include <string>

#include "../GameEngine/Platform/IAudioOutput.h"
#include "../GameEngine/Platform/IInput.h"
#include "../GameEngine/Platform/ITextMeasure.h"
#include "../GameEngine/Platform/PlatformServices.h"
#include "../GameEngine/Rendering/RenderFrame.h"
#include "../GameEngine/Rendering/RenderFrameBuilder.h"
#include "../GameEngine/Rendering/TextRasterizationCache.h"
#include "../GameEngine/SceneRendering/SceneRenderPass.h"
#include "../GameEngine/Runtime/Button.h"
#include "../GameEngine/Runtime/Canvas.h"
#include "../GameEngine/Runtime/Game.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/RectTransform.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SceneManager.h"
#include "../GameEngine/Runtime/SpriteRenderer.h"
#include "../GameEngine/Runtime/TextRenderer.h"
#include "../GameEngine/Runtime/Transform.h"
#include "../GameEngine/Runtime/UIEventSystem.h"
#include "../GameEngine/Runtime/UILayoutSystem.h"
#include "../GameEngine/Runtime/UIWindow.h"

#include "Views/EditorFloatingPanelView.h"
#include "Rules/EditorPanelCommon.h"

#include "EditorFloatingPanelTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::TextRenderer;
    using GameEngine::Runtime::Transform;

    [[nodiscard]] bool Nearly(const float left, const float right)
    {
        return std::abs(left - right) < 0.01f;
    }

    /// <summary>이름으로 자손을 찾는다. 뷰가 자기 조각을 내보이지 않으므로 계층으로 묻는다.</summary>
    [[nodiscard]] GameObject* FindDescendant(Transform& root, const std::string& name)
    {
        if (GameObject* const owner = root.GetGameObject(); owner && owner->GetName() == name)
        {
            return owner;
        }
        for (Transform* const child : root.GetChildren())
        {
            if (child)
            {
                if (GameObject* const found = FindDescendant(*child, name))
                {
                    return found;
                }
            }
        }
        return nullptr;
    }
}

namespace
{
    /// <summary>
    /// 플랫폼 입력을 대신한다. 유지 모드 버튼을 누르려면 커서와 버튼을 이 프레임에 세워야 하고,
    /// 실제 <c>Runtime::Input</c>은 그것을 플랫폼에서만 받는다.
    /// </summary>
    class FakePointer final : public GameEngine::Platform::IInput
    {
    public:
        void ReadState(GameEngine::Platform::InputState& state) override
        {
            state = mState;
            state.hasFocus = true;
            mState.TakeAccumulated();
        }

        void SetCursor(const int x, const int y) { mState.cursor = { x, y }; }
        void SetLeftButton(const bool down)
        {
            mState.SetMouseButton(GameEngine::Platform::MouseButton::Left, down);
        }

    private:
        GameEngine::Platform::InputState mState;
    };
}

bool RunEditorFloatingPanelPressTests()
{
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "FloatingPress");
    GameObject* const layer = scene->CreateGameObject("EditorFloatingLayer");
    if (!layer || !layer->AddComponent<GameEngine::Runtime::Canvas>())
    {
        return Expect(false, "the floating layer should assemble");
    }

    GameEditor::EditorFloatingPanelView view;
    view.Build(*scene, *layer, "Console");
    view.SetVisible(true);
    // 커서는 원점에 선다. 창을 원점에 두어 제목줄이 그 자리를 덮게 한다.
    constexpr float Scale = 1.0f;
    view.SetRect({ 0.0f, 0.0f, 420.0f, 260.0f });
    view.Synchronize(Scale);

    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the floating press scene should be added");
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    FakePointer pointer;
    const auto step = [&]()
    {
        input.BeginFrame(pointer);
        layout.Synchronize(sceneManager, 1600.0f, 900.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        view.Synchronize(Scale);
    };

    // 커서만 얹은 프레임. 아직 누르지 않았으므로 시작 신호가 없어야 한다.
    pointer.SetCursor(10, 5);
    step();
    const bool quietWhileHovering = !view.ConsumeTitleBarPressStart();

    // 누른 프레임. 여기서 참이 되지 않으면 셸은 끌기를 시작할 기회를 영영 얻지 못한다 —
    // 화면에서 창이 한 픽셀도 움직이지 않은 것이 이 신호가 오지 않았을 때의 모습이다.
    pointer.SetLeftButton(true);
    step();
    const bool pressStarts = view.ConsumeTitleBarPressStart();

    // 누르고 있는 동안은 시작이 아니다. 매 프레임 참이면 끌기가 프레임마다 다시 시작되어
    // 창이 커서에 붙지 않고 제자리에서 떤다.
    step();
    const bool onlyTheRisingEdge = !view.ConsumeTitleBarPressStart();

    // 신호를 세우는 쪽이 한 프레임에 두 번 돌아도 살아남아야 한다. 세우는 곳과 읽는 곳이
    // 프레임 안의 다른 단계라서, 두 번째 갱신이 첫 번째의 모서리를 지우면 읽는 쪽은 눌림을
    // 한 번도 보지 못한다 — 제목줄이 눌린 표시를 내면서 창은 움직이지 않는 그 모습이다.
    // 뗀 프레임에도 라우터는 잡고 있던 요소에 눌림을 그대로 준다 — 클릭이 그 프레임에
    // 완성되기 때문이다. 그래서 눌림이 실제로 풀리는 것은 그다음 프레임이고, 새 모서리를
    // 만들려면 여기서 한 프레임을 더 흘려야 한다.
    pointer.SetLeftButton(false);
    step();
    step();
    static_cast<void>(view.ConsumeTitleBarPressStart());
    pointer.SetLeftButton(true);
    input.BeginFrame(pointer);
    layout.Synchronize(sceneManager, 1600.0f, 900.0f);
    static_cast<void>(events.Synchronize(sceneManager, input));
    view.Synchronize(Scale);
    view.Synchronize(Scale);  // 같은 프레임의 두 번째 갱신.
    const bool survivesASecondUpdate = view.ConsumeTitleBarPressStart();

    return Expect(quietWhileHovering, "hovering the title bar alone should not start a drag") &&
        Expect(pressStarts, "pressing the title bar should tell the shell a drag can start") &&
        Expect(onlyTheRisingEdge, "holding the button should not restart the drag every frame") &&
        Expect(
            survivesASecondUpdate,
            "the press signal should survive a second update before the shell reads it");
}

bool RunEditorFloatingPanelDrawTests()
{
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UILayoutSystem;

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout
            << "  editor floating panel draw tests skipped: no bundled font on this machine\n";
        return true;
    }
    const std::shared_ptr<GameEngine::Rendering::TextRasterizationCache> textCache =
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer));

    GameEngine::Runtime::Game game{ nullptr, nullptr };
    auto scene = std::make_unique<Scene>(game.GetRuntimeContext(), "FloatingDraws");
    GameObject* const layer = scene->CreateGameObject("EditorFloatingLayer");
    if (!layer || !layer->AddComponent<GameEngine::Runtime::Canvas>())
    {
        return Expect(false, "the floating layer should assemble");
    }

    GameEditor::EditorFloatingPanelView view;
    view.Build(*scene, *layer, "Console");
    view.SetVisible(true);
    constexpr float Scale = 2.0f;
    view.SetRect({ 240.0f, 120.0f, 420.0f, 260.0f });
    view.Synchronize(Scale);

    if (game.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the floating draw scene should be added");
    }
    const UILayoutSystem layout;
    layout.Synchronize(game.GetSceneManager(), 1600.0f, 900.0f);

    GameEngine::SceneRendering::SceneRenderPass pass{ textCache };
    GameEngine::Rendering::RenderFrameBuilder builder;
    builder.SetRenderTargetSize({ 1600, 900 });
    pass.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();

    // 화면 공간으로 그려지는 UI는 Overlay 패스에 실린다.
    const std::vector<const GameEngine::Rendering::SpriteDraw*> sprites =
        frame.GetDraws<GameEngine::Rendering::SpriteDraw>(GameEngine::Rendering::RenderPass::Overlay);
    const std::vector<const GameEngine::Rendering::TextDraw*> texts =
        frame.GetDraws<GameEngine::Rendering::TextDraw>(GameEngine::Rendering::RenderPass::Overlay);

    // 스프라이트는 단언하지 않고 세어서 적기만 한다. 이 시험 환경에는 에디터의 콘텐츠가 없어
    // `Sprites/panel-32.png`가 해석되지 않고, 그러면 프레임에 draw가 나오지 않는다 — 그 0은
    // "없어서 없음"이지 "버그로 없음"이 아니다. 둘을 구별할 수 없는 단언은 시험이 아니라
    // 환경 검사가 되므로 여기서는 하지 않는다. 제목줄이 어느 스프라이트를 어떤 모드로 쓰는지는
    // 컴포넌트 상태 시험이 답하고, 그것이 실제로 그려지는 모양은 화면 확인의 몫이다.
    std::cout << "  floating panel draw tests: screen-space sprite draws = " << sprites.size()
              << " (0 means the editor content is not staged beside the tests)\n";

    // 글자는 콘텐츠가 필요 없다 — 래스터라이저만 있으면 글리프가 나온다. 그래서 여기는
    // 단언한다. 제목과 Dock, 둘. 글리프가 비어 있으면 draw는 있으나 아무것도 안 보인다 —
    // "시험은 통과하는데 화면에 글자가 없다"가 그 모양이고, 그것이 이 시험이 메우는 틈이다.
    std::size_t textsWithGlyphs = 0;
    for (const GameEngine::Rendering::TextDraw* const text : texts)
    {
        if (text && text->glyphs && !text->glyphs->empty())
        {
            ++textsWithGlyphs;
        }
    }
    if (texts.empty() && textsWithGlyphs == 0)
    {
        std::cout << "  floating panel draw tests: no text reached the frame "
                     "(no rasterizer on this machine?)\n";
    }
    const bool bothLabelsDrawGlyphs = textsWithGlyphs >= 2;

    return Expect(
        bothLabelsDrawGlyphs,
        "the title and the dock label should both reach the frame with glyphs");
}

bool RunEditorFloatingPanelChromeTests()
{
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "FloatingChrome");
    GameObject* const layer = scene->CreateGameObject("EditorFloatingLayer");
    if (!layer || !layer->AddComponent<GameEngine::Runtime::Canvas>())
    {
        return Expect(false, "the floating layer should assemble");
    }

    GameEditor::EditorFloatingPanelView view;
    view.Build(*scene, *layer, "Console");
    view.SetVisible(true);

    // 배율 2는 이 기계의 화면이 실제로 쓰는 값이고, 껍데기와 몸통이 같은 단위를 쓰는지는
    // 1에서는 드러나지 않는다 — 어긋나 있어도 답이 같아지기 때문이다.
    constexpr float Scale = 2.0f;
    const GameEngine::UI::UIRect logical{ 240.0f, 120.0f, 420.0f, 260.0f };
    view.SetRect(logical);
    view.Synchronize(Scale);

    Transform& layerTransform = layer->GetTransform();
    GameObject* const windowObject = FindDescendant(layerTransform, "FloatingPanel");
    GameObject* const header = FindDescendant(layerTransform, "FloatingPanelHeader");
    GameObject* const dockLabel = FindDescendant(layerTransform, "FloatingPanelDockLabel");
    if (!windowObject || !header || !dockLabel)
    {
        return Expect(false, "the floating window should have a header and a dock label");
    }

    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the floating chrome scene should be added");
    }
    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 1600.0f, 900.0f);

    // ① 껍데기와 몸통이 같은 단위인가. 배치가 푼 창의 사각형은 논리 자리에 배율을 곱한 것이어야
    // 하고, 몸통이 그려질 자리는 그 안에서 제목줄 바로 아래여야 한다. 둘이 다른 단위를 쓰면
    // 껍데기는 제자리에 서고 몸통만 딴 곳에 그려져, 창이 빈 것처럼 보인다.
    const RectTransform::Rect windowRect = windowObject->GetComponent<RectTransform>()->GetVisibleRect();
    const bool chromePlaced = Nearly(windowRect.x, logical.x * Scale) &&
        Nearly(windowRect.y, logical.y * Scale) && Nearly(windowRect.width, logical.width * Scale) &&
        Nearly(windowRect.height, logical.height * Scale);

    const GameEngine::UI::UIRect content = view.GetContentRect(Scale);
    const float headerHeight = GameEditor::HeaderHeight * Scale;
    const bool bodyInsideChrome = Nearly(content.x, windowRect.x) &&
        Nearly(content.y, windowRect.y + headerHeight) && Nearly(content.width, windowRect.width) &&
        Nearly(content.height, windowRect.height - headerHeight);

    // 떠 있는 창의 몸통 배경은 즉시 모드에서 그려야 한다.
    // 유지 모드 배경은 즉시 모드 몸통 위에 겹쳐 내용을 덮을 수 있다.
    const bool bodyHasNoRetainedBackground =
        windowObject->GetComponent<SpriteRenderer>() == nullptr;

    // 제목줄 높이와 Dock 버튼 폭은 논리 단위에서 콘텐츠 배율을 적용해야 한다.
    // 배율 1은 변환 누락을 구분할 수 없으므로 배율 2에서도 크기를 확인한다.
    const RectTransform::Rect headerRect = header->GetComponent<RectTransform>()->GetVisibleRect();
    const bool headerScaled = Nearly(headerRect.height, GameEditor::HeaderHeight * Scale);
    const RectTransform::Rect dockRect =
        FindDescendant(layerTransform, "FloatingPanelDock")->GetComponent<RectTransform>()
            ->GetVisibleRect();
    // Dock 버튼은 제목줄 오른쪽 끝에 붙고, 그 안에 보조 크기의 "Dock"이 들어가야 한다.
    const bool dockScaled = dockRect.width > 0.0f &&
        Nearly(dockRect.x + dockRect.width, headerRect.x + headerRect.width) &&
        Nearly(dockRect.height, headerRect.height);

    // ② 제목줄이 그림을 갖는가. 툴바 띠와 같은 9-슬라이스를 화면 공간으로 그린다.
    const SpriteRenderer* const headerSprite = header->GetComponent<SpriteRenderer>();
    const bool headerHasPanelSprite = headerSprite &&
        headerSprite->GetSprite().ToString() == "Sprites/panel-32.png" &&
        headerSprite->GetDrawMode() == SpriteRenderer::DrawMode::Sliced &&
        headerSprite->GetSpace() == SpriteRenderer::Space::Screen;

    // ③ Dock 글자가 있는가. 되돌아가는 길이 이 한 단어이므로, 비면 사람은 창을 앉힐 방법을 잃는다.
    const TextRenderer* const dockText = dockLabel->GetComponent<TextRenderer>();
    const bool dockLabelReads = dockText && dockText->GetText() == "Dock" &&
        dockText->GetFontSize() > 0.0f &&
        // 글꼴 크기는 논리 단위로 선언된다. 배율은 그리는 가장자리에서 곱해지므로, 선언된 값에
        // 배율이 섞여 있으면 그것은 두 번 곱해진다는 뜻이다.
        Nearly(dockText->GetFontSize(), GameEditor::SecondaryFontSize);

    // 글자의 실제 크기는 선언된 크기에 Canvas 배율을 곱한 값이어야 한다.
    // 상자 오프셋만 키우면 글자는 작게 남으므로 사각형 검사와 별도로 텍스트 배율을 확인한다.
    const GameEngine::Runtime::Canvas* const layerCanvas =
        layerTransform.GetGameObject()->GetComponent<GameEngine::Runtime::Canvas>();
    const bool textTakesTheScale =
        layerCanvas && Nearly(layerCanvas->GetScaleFactor(), Scale);

    // 그리고 Dock 글자가 설 자리가 실제로 있는가. 상자가 비어 있거나 제목줄 밖이면 글꼴이
    // 아무리 옳아도 화면에는 아무것도 없다.
    const RectTransform::Rect dockLabelRect =
        dockLabel->GetComponent<RectTransform>()->GetVisibleRect();
    const bool dockLabelHasRoom = dockLabelRect.width > 0.0f && dockLabelRect.height > 0.0f &&
        dockLabelRect.x >= dockRect.x - 0.01f && dockLabelRect.y >= dockRect.y - 0.01f &&
        dockLabelRect.x + dockLabelRect.width <= dockRect.x + dockRect.width + 0.01f &&
        dockLabelRect.y + dockLabelRect.height <= dockRect.y + dockRect.height + 0.01f;

    // 창이라는 표시도 함께 확인한다. 이것이 없으면 가림도 모달도 이 창을 모른다.
    const bool markedAsWindow = windowObject->GetComponent<GameEngine::Runtime::UIWindow>() != nullptr;

    return Expect(chromePlaced, "the window chrome should sit at its logical rect times the scale") &&
        Expect(bodyInsideChrome, "the body rect should sit inside the chrome, below the header") &&
        Expect(
            bodyHasNoRetainedBackground,
            "the window body should have no retained background to cover its immediate content") &&
        Expect(headerScaled, "the title bar's height should follow the content scale") &&
        Expect(dockScaled, "the dock button should fill the title bar's right end at any scale") &&
        Expect(headerHasPanelSprite, "the title bar should be drawn with the sliced panel sprite") &&
        Expect(dockLabelReads, "the dock button should carry readable text at the secondary size") &&
        Expect(
            textTakesTheScale,
            "the floating layer's canvas should carry the scale, or its text never grows") &&
        Expect(dockLabelHasRoom, "the dock label should have room inside the dock button") &&
        Expect(markedAsWindow, "the floating panel should be marked as a window");
}

static const TestSupport::Registration gEditorFloatingPanelChromeTests{
    "EditorDocument", "editor floating panel chrome tests should pass", RunEditorFloatingPanelChromeTests };

static const TestSupport::Registration gEditorFloatingPanelDrawTests{
    "EditorDocument", "editor floating panel draw tests should pass", RunEditorFloatingPanelDrawTests };

static const TestSupport::Registration gEditorFloatingPanelPressTests{
    "EditorDocument", "editor floating panel press tests should pass", RunEditorFloatingPanelPressTests };

bool RunEditorFloatingPanelDragTests()
{
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "FloatingDrag");
    GameObject* const layer = scene->CreateGameObject("EditorFloatingLayer");
    if (!layer || !layer->AddComponent<GameEngine::Runtime::Canvas>())
    {
        return Expect(false, "the floating layer should assemble");
    }

    GameEditor::EditorFloatingPanelView view;
    view.Build(*scene, *layer, "Console");
    view.SetVisible(true);
    constexpr float Scale = 1.0f;
    // 커서는 원점에 선다. 창을 원점에 두어 제목줄이 그 자리를 덮게 한다.
    view.SetRect({ 0.0f, 0.0f, 420.0f, 260.0f });
    view.Synchronize(Scale);

    if (sceneManager.AddScene(std::move(scene)) == 0)
    {
        return Expect(false, "the floating drag scene should be added");
    }

    const UILayoutSystem layout;
    UIEventSystem events;
    FakePointer pointer;
    const auto step = [&]()
    {
        input.BeginFrame(pointer);
        layout.Synchronize(sceneManager, 1600.0f, 900.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
        view.Synchronize(Scale);
    };

    pointer.SetCursor(10, 5);
    step();
    static_cast<void>(view.ConsumeTitleBarPressStart());

    // 제목줄을 누른다. 여기서 몸짓이 한 번 시작한다.
    pointer.SetLeftButton(true);
    step();
    const bool gestureStarts = view.ConsumeTitleBarPressStart();

    // 이제 끌기가 도는 모습을 그대로 흉내 낸다. 창은 커서를 따라가려 하지만 자리를 싣는 것과
    // 그 자리로 판정하는 것이 한 프레임 어긋나므로, 제목줄이 커서 아래를 <b>떠났다가 돌아오기를
    // 되풀이한다.</b> 그것이 이 결함이 사는 자리다: 떠난 프레임에 눌림이 풀리고, 돌아온 프레임에
    // 눌림이 다시 서면서 새 몸짓이 시작된다.
    //
    // 멀리 보냈다가 도로 데려오는 것으로 그 왕복을 만든다. 한 방향으로만 밀면 제목줄이 한 번
    // 떠난 뒤 영영 돌아오지 않아, 다시 서는 모서리가 아예 생기지 않는다 — 통과하지만 아무것도
    // 재지 않는 시험이 된다.
    bool startedAgain = false;
    for (int frame = 0; frame < 4; ++frame)
    {
        view.SetRect({ 300.0f, 300.0f, 420.0f, 260.0f });
        step();
        if (view.ConsumeTitleBarPressStart())
        {
            startedAgain = true;
        }
        view.SetRect({ 0.0f, 0.0f, 420.0f, 260.0f });
        step();
        if (view.ConsumeTitleBarPressStart())
        {
            startedAgain = true;
        }
    }

    // 손을 떼면 몸짓이 끝난다. 그다음에 다시 누르면 그때는 새 몸짓이 맞다 — 끝나지 않는
    // 몸짓은 놓을 수 없는 창이 된다.
    pointer.SetLeftButton(false);
    step();
    step();
    static_cast<void>(view.ConsumeTitleBarPressStart());
    // 창을 아는 자리에 세우고 커서를 그 제목줄에 얹는다. 위의 고리가 창을 어디에 두고
    // 끝났는지에 이 검사가 매이면, 고리의 걸음 수를 바꾸는 날 이유 없이 붉어진다.
    view.SetRect({ 0.0f, 0.0f, 420.0f, 260.0f });
    pointer.SetCursor(10, 5);
    step();
    static_cast<void>(view.ConsumeTitleBarPressStart());
    pointer.SetLeftButton(true);
    step();
    const bool startsAgainAfterRelease = view.ConsumeTitleBarPressStart();

    return Expect(gestureStarts, "pressing the title bar should start one gesture") &&
        Expect(
            !startedAgain,
            "moving the window out from under the cursor should not start the gesture again") &&
        Expect(
            startsAgainAfterRelease,
            "after letting go, pressing the title bar again should start a new gesture");
}

static const TestSupport::Registration gEditorFloatingPanelDragTests{
    "EditorDocument", "editor floating panel drag tests should pass",
    RunEditorFloatingPanelDragTests };
