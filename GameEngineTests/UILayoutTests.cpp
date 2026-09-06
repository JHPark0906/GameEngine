#include <cmath>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>
#include <vector>

#include "../GameEngine/Rendering/CachedTextMeasure.h"
#include "../GameEngine/Rendering/TextRasterizationCache.h"
#include "../GameEngine/Rendering/RenderFrame.h"
#include "../GameEngine/Rendering/RenderFrameBuilder.h"
#include "../GameEngine/Platform/IAudioOutput.h"
#include "../GameEngine/Platform/PlatformServices.h"
#include "../GameEngine/SceneRendering/SceneRenderPass.h"
#include "../GameEngine/Serialization/SceneSerializer.h"
#include "../GameEngine/Runtime/Canvas.h"
#include "../GameEngine/Runtime/ContentFit.h"
#include "../GameEngine/Runtime/Game.h"
#include "../GameEngine/Runtime/TextRenderer.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/Object.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/LayoutElement.h"
#include "../GameEngine/Runtime/RectMask.h"
#include "../GameEngine/Runtime/RectTransform.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SpriteRenderer.h"
#include "../GameEngine/Runtime/SceneManager.h"
#include "../GameEngine/Runtime/Transform.h"
#include "../GameEngine/Runtime/UILayoutSystem.h"


#include "UILayoutTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::UILayoutSystem;

    /// <summary>배치는 픽셀 계산이라 정수에 가깝지만, 비율이 섞이므로 여유를 둔다.</summary>
    [[nodiscard]] bool NearlyEqual(const float left, const float right)
    {
        return std::fabs(left - right) <= 0.001f;
    }

    [[nodiscard]] bool ExpectRect(
        const RectTransform::Rect& rect,
        const float x, const float y, const float width, const float height,
        const char* const message)
    {
        const bool matches = NearlyEqual(rect.x, x) && NearlyEqual(rect.y, y) &&
            NearlyEqual(rect.width, width) && NearlyEqual(rect.height, height);
        if (!matches)
        {
            std::cerr << "  rect was (" << rect.x << ", " << rect.y << ", " << rect.width << ", "
                      << rect.height << "), expected (" << x << ", " << y << ", " << width << ", "
                      << height << ")\n";
        }
        return Expect(matches, message);
    }

    /// <summary>속성만 세운 RectTransform 하나를 만든다. 계층 없이 배치 식만 보는 테스트용이다.</summary>
    class LoneRectTransform final
    {
    public:
        LoneRectTransform(
            const GameEngine::Math::Vector2& anchorMin,
            const GameEngine::Math::Vector2& anchorMax,
            const GameEngine::Math::Vector2& offsetMin,
            const GameEngine::Math::Vector2& offsetMax)
        {
            mRectTransform.SetAnchorMin(anchorMin);
            mRectTransform.SetAnchorMax(anchorMax);
            mRectTransform.SetOffsetMin(offsetMin);
            mRectTransform.SetOffsetMax(offsetMax);
        }

        [[nodiscard]] const RectTransform& Get() const { return mRectTransform; }

    private:
        RectTransform mRectTransform;
    };
}

bool RunRectResolutionTests()
{
    std::cout << "running rect resolution tests\n";
    bool passed = true;

    const RectTransform::Rect parent{ 0.0f, 0.0f, 800.0f, 600.0f };

    {
        // 앵커가 한 점이면 오프셋 둘이 곧 좌상단과 우하단이다 — 크기가 고정된 요소다.
        const LoneRectTransform fixedSize{ { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 8.0f, 8.0f },
            { 208.0f, 38.0f } };
        passed = ExpectRect(
            UILayoutSystem::ResolveRect(fixedSize.Get(), parent, 1.0f),
            8.0f, 8.0f, 200.0f, 30.0f,
            "a point-anchored element should take its size from the two offsets") && passed;
    }

    {
        // 앵커가 부모 전체면 오프셋은 안쪽 여백이 된다 — 부모와 함께 늘어나는 요소다.
        const LoneRectTransform stretched{ { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 10.0f, 20.0f },
            { -10.0f, -20.0f } };
        passed = ExpectRect(
            UILayoutSystem::ResolveRect(stretched.Get(), parent, 1.0f),
            10.0f, 20.0f, 780.0f, 560.0f,
            "a fully anchored element should stretch with its parent, inset by the offsets") &&
            passed;
    }

    {
        // 오른쪽 끝에 붙은 고정 크기 요소는 창이 넓어져도 오른쪽 끝에 남는다.
        const LoneRectTransform rightAligned{ { 1.0f, 0.0f }, { 1.0f, 0.0f }, { -120.0f, 4.0f },
            { -8.0f, 28.0f } };
        passed = ExpectRect(
            UILayoutSystem::ResolveRect(rightAligned.Get(), parent, 1.0f),
            680.0f, 4.0f, 112.0f, 24.0f,
            "an element anchored to the right edge should sit against that edge") && passed;
    }

    {
        // 배율은 픽셀에만 곱해진다. 앵커는 이미 해상도와 무관한 비율이라 그대로다.
        const LoneRectTransform scaled{ { 0.5f, 0.0f }, { 0.5f, 0.0f }, { 0.0f, 0.0f },
            { 100.0f, 40.0f } };
        passed = ExpectRect(
            UILayoutSystem::ResolveRect(scaled.Get(), parent, 2.0f),
            400.0f, 0.0f, 200.0f, 80.0f,
            "the scale factor should multiply offsets and leave anchors alone") && passed;
    }

    {
        // 뒤집힌 선언은 사각형이 아니다. 음수 크기를 흘려보내지 않고 여기서 0으로 누른다.
        const LoneRectTransform inverted{ { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 100.0f, 100.0f },
            { 20.0f, 20.0f } };
        passed = ExpectRect(
            UILayoutSystem::ResolveRect(inverted.Get(), parent, 1.0f),
            100.0f, 100.0f, 0.0f, 0.0f,
            "an inverted declaration should collapse to zero size, not a negative one") && passed;
    }

    {
        // 부모가 원점에 있지 않아도 계산은 부모의 자리에서 시작한다.
        const RectTransform::Rect offsetParent{ 200.0f, 100.0f, 400.0f, 300.0f };
        const LoneRectTransform centered{ { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f },
            { 0.0f, 0.0f } };
        passed = ExpectRect(
            UILayoutSystem::ResolveRect(centered.Get(), offsetParent, 1.0f),
            200.0f, 100.0f, 400.0f, 300.0f,
            "a child should be placed relative to where its parent actually is") && passed;
    }

    return passed;
}

bool RunUILayoutHierarchyTests()
{
    std::cout << "running UI layout hierarchy tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
    GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "UIScene");

    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const panelObject = scene->CreateGameObject("Panel");
    GameEngine::Runtime::GameObject* const groupObject = scene->CreateGameObject("Group");
    GameEngine::Runtime::GameObject* const labelObject = scene->CreateGameObject("Label");
    GameEngine::Runtime::GameObject* const looseObject = scene->CreateGameObject("Loose");
    if (!Expect(
            canvasObject && panelObject && groupObject && labelObject && looseObject,
            "the layout stage should create its objects"))
    {
        return false;
    }

    static_cast<void>(canvasObject->AddComponent<Canvas>());

    RectTransform* const panelRect = panelObject->AddComponent<RectTransform>();
    // 창 왼쪽에 붙어 세로로 늘어나는 폭 240의 패널이다.
    panelRect->SetAnchorMin({ 0.0f, 0.0f });
    panelRect->SetAnchorMax({ 0.0f, 1.0f });
    panelRect->SetOffsetMin({ 0.0f, 0.0f });
    panelRect->SetOffsetMax({ 240.0f, 0.0f });
    static_cast<void>(panelObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    // 자리를 주장하지 않는 순수 묶음 오브젝트다. 아래의 자식은 그래도 배치돼야 한다.
    static_cast<void>(groupObject->GetTransform().SetParent(&panelObject->GetTransform()));

    RectTransform* const labelRect = labelObject->AddComponent<RectTransform>();
    labelRect->SetAnchorMin({ 0.0f, 0.0f });
    labelRect->SetAnchorMax({ 1.0f, 0.0f });
    labelRect->SetOffsetMin({ 8.0f, 8.0f });
    labelRect->SetOffsetMax({ -8.0f, 28.0f });
    static_cast<void>(labelObject->GetTransform().SetParent(&groupObject->GetTransform()));

    // 어떤 캔버스에도 속하지 않은 RectTransform이다. 자기 자리를 알 수 없으므로 배치되지 않는다.
    RectTransform* const looseRect = looseObject->AddComponent<RectTransform>();
    looseRect->SetOffsetMax({ 50.0f, 50.0f });

    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 1280.0f, 720.0f);

    passed = ExpectRect(
        panelRect->GetResolvedRect(), 0.0f, 0.0f, 240.0f, 720.0f,
        "a panel anchored to the left edge should span the surface height") && passed;
    passed = ExpectRect(
        labelRect->GetResolvedRect(), 8.0f, 8.0f, 224.0f, 20.0f,
        "a child under a group object should still resolve against the group's parent") && passed;
    passed = ExpectRect(
        looseRect->GetResolvedRect(), 0.0f, 0.0f, 0.0f, 0.0f,
        "a rect outside every canvas should not be laid out") && passed;

    // 화면이 바뀌면 같은 선언이 새 크기를 따른다 — 배치가 매 프레임 다시 계산되는 이유다.
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    passed = ExpectRect(
        panelRect->GetResolvedRect(), 0.0f, 0.0f, 240.0f, 600.0f,
        "the layout should follow the surface when it changes size") && passed;
    passed = ExpectRect(
        labelRect->GetResolvedRect(), 8.0f, 8.0f, 224.0f, 20.0f,
        "an element whose declaration does not depend on the surface should not move") && passed;

    return passed;
}

bool RunUISurfaceSubmissionTests()
{
    std::cout << "running UI surface submission tests\n";

    GameEngine::Runtime::Game game{ nullptr, nullptr };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(
        game.GetRuntimeContext(), "UISurfaceScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const labelObject = scene->CreateGameObject("Label");
    if (!Expect(canvasObject && labelObject, "the surface stage should create its objects"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    RectTransform* const labelRect = labelObject->AddComponent<RectTransform>();
    labelRect->SetAnchorMin({ 0.0f, 0.0f });
    labelRect->SetAnchorMax({ 0.0f, 0.0f });
    labelRect->SetOffsetMin({ 40.0f, 24.0f });
    labelRect->SetOffsetMax({ 240.0f, 48.0f });
    GameEngine::Runtime::TextRenderer* const label =
        labelObject->AddComponent<GameEngine::Runtime::TextRenderer>();
    label->SetText("UI");
    static_cast<void>(labelObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    if (!Expect(game.AddScene(std::move(scene)) != 0, "the surface scene should be added"))
    {
        return false;
    }

    game.SetRenderSurfaceSize(1280.0f, 720.0f);
    game.Update(0.0f);

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  UI surface submission tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::Rendering::RenderFrameBuilder builder;
    GameEngine::SceneRendering::SceneRenderPass frontend{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)) };
    frontend.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();

    const std::vector<const GameEngine::Rendering::TextDraw*> overlayDraws =
        frame.GetDraws<GameEngine::Rendering::TextDraw>(
            GameEngine::Rendering::RenderPass::Overlay);
    bool passed = Expect(
        !overlayDraws.empty(),
        "a screen-space text element under a canvas should reach the overlay pass");
    if (overlayDraws.empty())
    {
        return false;
    }

    // 세계에는 아무것도 놓이지 않는다: 화면 공간 UI가 투명 패스로 새면 세계만 보는 뷰가 UI를
    // 함께 그리게 된다.
    passed = Expect(
        frame.GetDraws<GameEngine::Rendering::TextDraw>(
            GameEngine::Rendering::RenderPass::Transparent).empty(),
        "screen-space UI should not also appear in the world pass") && passed;

    // 이동 하나만으로는 글자가 어디에 놓였는지 알 수 없다. 글리프 quad가 블록 중심을 원점으로
    // 만들어지므로 같은 이동이라도 블록 크기에 따라 잉크가 다른 자리에 간다 — 이동을 사각형의
    // 좌상단과 비교하면 코드가 그 틀린 요구를 정확히 따르게 된다. 그래서 이동이 아니라 글리프가
    // 실제로 덮는 상자를 본다.
    float inkLeft = 1.0e9f;
    float inkTop = 1.0e9f;
    float inkRight = -1.0e9f;
    float inkBottom = -1.0e9f;
    for (const GameEngine::Rendering::TextDraw* const draw : overlayDraws)
    {
        if (!draw->glyphs)
        {
            continue;
        }
        const float originX = draw->localToWorld.GetElement(3, 0);
        const float originY = draw->localToWorld.GetElement(3, 1);
        for (const GameEngine::Rendering::TextGlyphQuad& glyph : *draw->glyphs)
        {
            // 글리프의 로컬 y는 위가 +이고 화면 픽셀은 아래가 +라 부호가 뒤집힌다 — 백엔드가
            // 하는 것과 같은 계산이다.
            const float centerX = originX + glyph.centerX;
            const float centerY = originY - glyph.centerY;
            inkLeft = (std::min)(inkLeft, centerX - glyph.width * 0.5f);
            inkRight = (std::max)(inkRight, centerX + glyph.width * 0.5f);
            inkTop = (std::min)(inkTop, centerY - glyph.height * 0.5f);
            inkBottom = (std::max)(inkBottom, centerY + glyph.height * 0.5f);
        }
    }

    const RectTransform::Rect& labelBox = labelRect->GetResolvedRect();
    passed = Expect(
        inkRight > inkLeft && inkBottom > inkTop,
        "the label should put some ink on the frame") && passed;
    // 사각형이 문장보다 넉넉하므로 실제 잉크가 영역 안에 온전히 들어가야 한다.
    passed = Expect(
        inkLeft >= labelBox.x - 0.5f && inkTop >= labelBox.y - 0.5f,
        "the label's ink should start inside its rect, not half a block above and left") && passed;
    passed = Expect(
        inkRight <= labelBox.GetRight() + 0.5f,
        "the label's ink should not run past the right edge of its rect") && passed;
    // 아래쪽은 담지 못할 수도 있다: 이 요소는 높이를 요구하지 않았고, 선언한 24픽셀보다 글자
    // 블록이 높으면 그만큼 넘친다. 그것은 선언의 결과이지 기준점의 문제가 아니므로, 여기서
    // 보는 것은 잉크가 위 모서리에서 <b>아래로</b> 자란다는 사실이다.
    passed = Expect(
        inkBottom > labelBox.y,
        "the label's ink should grow downward from the top edge, not upward") && passed;

    // 화면이 넓어져도 왼쪽 위에 붙은 요소는 같은 픽셀에 남는다.
    game.SetRenderSurfaceSize(1920.0f, 1080.0f);
    game.Update(0.0f);
    passed = ExpectRect(
        labelRect->GetResolvedRect(), 40.0f, 24.0f, 200.0f, 24.0f,
        "the element should keep its pixel rect when the surface grows") && passed;

    return passed;
}

bool RunRectMaskTests()
{
    std::cout << "running rect mask tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
    GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "MaskScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const viewportObject = scene->CreateGameObject("Viewport");
    GameEngine::Runtime::GameObject* const insideObject = scene->CreateGameObject("Inside");
    GameEngine::Runtime::GameObject* const straddlingObject = scene->CreateGameObject("Straddling");
    GameEngine::Runtime::GameObject* const outsideObject = scene->CreateGameObject("Outside");
    if (!Expect(
            canvasObject && viewportObject && insideObject && straddlingObject && outsideObject,
            "the mask stage should create its objects"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    // 화면 좌표 (0,0)~(200,100)을 자르는 창이다.
    RectTransform* const viewportRect = viewportObject->AddComponent<RectTransform>();
    viewportRect->SetOffsetMin({ 0.0f, 0.0f });
    viewportRect->SetOffsetMax({ 200.0f, 100.0f });
    static_cast<void>(viewportObject->AddComponent<GameEngine::Runtime::RectMask>());
    static_cast<void>(viewportObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    const auto addRow = [](GameEngine::Runtime::GameObject& object,
                            GameEngine::Runtime::GameObject& parent,
                            const float top, const float bottom)
    {
        RectTransform* const rect = object.AddComponent<RectTransform>();
        rect->SetOffsetMin({ 0.0f, top });
        rect->SetOffsetMax({ 200.0f, bottom });
        static_cast<void>(object.GetTransform().SetParent(&parent.GetTransform()));
        return rect;
    };
    RectTransform* const insideRect = addRow(*insideObject, *viewportObject, 10.0f, 40.0f);
    RectTransform* const straddlingRect = addRow(*straddlingObject, *viewportObject, 80.0f, 140.0f);
    RectTransform* const outsideRect = addRow(*outsideObject, *viewportObject, 200.0f, 240.0f);

    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 400.0f, 300.0f);

    // 자리는 마스크와 무관하게 선언 그대로다. 잘림이 자리를 줄이면 그 아래 형제들이 위로
    // 딸려 올라오므로, 잘린 것은 결과에만 적용되어야 한다.
    passed = ExpectRect(
        straddlingRect->GetResolvedRect(), 0.0f, 80.0f, 200.0f, 60.0f,
        "a masked element should keep the rect its declaration asks for") && passed;

    passed = ExpectRect(
        insideRect->GetVisibleRect(), 0.0f, 10.0f, 200.0f, 30.0f,
        "an element fully inside the mask should be visible in full") && passed;
    passed = ExpectRect(
        straddlingRect->GetVisibleRect(), 0.0f, 80.0f, 200.0f, 20.0f,
        "an element straddling the mask edge should be visible only up to that edge") && passed;
    passed = Expect(
        outsideRect->GetVisibleRect().IsEmpty(),
        "an element fully outside the mask should have nothing visible") && passed;

    // 가려진 자리의 점은 그 요소를 맞히지 않는다 — 클릭 억제가 히트 테스트에 따로 적히지 않고
    // 이 한 값에서 나온다.
    passed = Expect(
        !outsideRect->GetVisibleRect().Contains(100.0f, 220.0f),
        "a point over a fully masked element should not be on it") && passed;
    passed = Expect(
        straddlingRect->GetVisibleRect().Contains(100.0f, 90.0f) &&
            !straddlingRect->GetVisibleRect().Contains(100.0f, 120.0f),
        "the visible half of a straddling element should be hit and the hidden half should not") &&
        passed;

    // 마스크가 없는 계층은 아무것도 잘리지 않는다 — 캔버스 안이면 자리가 곧 보이는 부분이다.
    passed = ExpectRect(
        viewportRect->GetVisibleRect(), 0.0f, 0.0f, 200.0f, 100.0f,
        "a mask should not clip itself") && passed;

    return passed;
}

bool RunLayoutElementTests()
{
    std::cout << "running layout element tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
    GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "FitScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const labelObject = scene->CreateGameObject("Label");
    GameEngine::Runtime::GameObject* const roomyObject = scene->CreateGameObject("Roomy");
    GameEngine::Runtime::GameObject* const floorObject = scene->CreateGameObject("Floor");
    GameEngine::Runtime::GameObject* const plainObject = scene->CreateGameObject("Plain");
    if (!Expect(
            canvasObject && labelObject && roomyObject && floorObject && plainObject,
            "the fit stage should create its objects"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    constexpr std::string_view LongSentence = "Unsaved changes - open anyway?";

    // 선언된 자리보다 문장이 긴 요소다. 가로로 내용에 맞춘다.
    const auto addLabel = [&](GameEngine::Runtime::GameObject& object, const float declaredWidth)
    {
        RectTransform* const rect = object.AddComponent<RectTransform>();
        rect->SetOffsetMin({ 10.0f, 20.0f });
        rect->SetOffsetMax({ 10.0f + declaredWidth, 40.0f });
        GameEngine::Runtime::TextRenderer* const text =
            object.AddComponent<GameEngine::Runtime::TextRenderer>();
        text->SetText(std::string(LongSentence));
        text->SetFontSize(13.0f);
        static_cast<void>(object.GetTransform().SetParent(&canvasObject->GetTransform()));
        return rect;
    };

    RectTransform* const labelRect = addLabel(*labelObject, 4.0f);
    GameEngine::Runtime::LayoutElement* const labelFit =
        labelObject->AddComponent<GameEngine::Runtime::LayoutElement>();
    labelFit->SetFit(GameEngine::Runtime::LayoutElement::Fit::Horizontal);

    // 이미 넉넉한 자리를 가진 같은 문장이다. 맞춤은 자라는 방향으로만 손대야 한다.
    RectTransform* const roomyRect = addLabel(*roomyObject, 4000.0f);
    GameEngine::Runtime::LayoutElement* const roomyFit =
        roomyObject->AddComponent<GameEngine::Runtime::LayoutElement>();
    roomyFit->SetFit(GameEngine::Runtime::LayoutElement::Fit::Horizontal);

    // 글자가 없는 요소다. 선언된 최소 크기만 지켜져야 한다.
    RectTransform* const floorRect = floorObject->AddComponent<RectTransform>();
    floorRect->SetOffsetMin({ 0.0f, 0.0f });
    floorRect->SetOffsetMax({ 5.0f, 5.0f });
    GameEngine::Runtime::LayoutElement* const floorFit =
        floorObject->AddComponent<GameEngine::Runtime::LayoutElement>();
    floorFit->SetFit(GameEngine::Runtime::LayoutElement::Fit::Both);
    floorFit->SetMinimumSize({ 64.0f, 24.0f });
    static_cast<void>(floorObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    // LayoutElement가 없는 요소다. 아무것도 달라지지 않아야 한다.
    RectTransform* const plainRect = addLabel(*plainObject, 4.0f);

    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  layout element tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::Rendering::CachedTextMeasure measure{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)) };
    const UILayoutSystem measuring;
    measuring.Synchronize(sceneManager, 1280.0f, 720.0f, &measure);

    const float fittedWidth = labelRect->GetResolvedRect().width;
    passed = Expect(
        fittedWidth > 40.0f,
        "an element that fits its content should grow to hold the sentence") && passed;
    passed = Expect(
        labelRect->GetResolvedRect().x == 10.0f && labelRect->GetResolvedRect().y == 20.0f,
        "growing should move the far edge, never the top-left corner") && passed;
    passed = Expect(
        NearlyEqual(roomyRect->GetResolvedRect().width, 4000.0f),
        "a rect already wider than its content should keep its width") && passed;
    passed = ExpectRect(
        floorRect->GetResolvedRect(), 0.0f, 0.0f, 64.0f, 24.0f,
        "an element with no content should still keep its declared minimum") && passed;
    passed = ExpectRect(
        plainRect->GetResolvedRect(), 10.0f, 20.0f, 4.0f, 20.0f,
        "an element without a LayoutElement should be left exactly as declared") && passed;

    // 잣대가 없는 런타임 — 폰트 스택을 열 수 없는 곳 — 에서도 배치는 계속 돈다. 잰 크기가 없을
    // 뿐이고, 선언된 최소 크기는 그대로 지켜진다.
    const UILayoutSystem unmeasured;
    unmeasured.Synchronize(sceneManager, 1280.0f, 720.0f);
    passed = ExpectRect(
        labelRect->GetResolvedRect(), 10.0f, 20.0f, 4.0f, 20.0f,
        "without a measure the element falls back to what it declared") && passed;
    passed = ExpectRect(
        floorRect->GetResolvedRect(), 0.0f, 0.0f, 64.0f, 24.0f,
        "a declared minimum should survive a runtime that cannot measure text") && passed;

    // ContentFit과 LayoutElement가 한 오브젝트에 함께 붙으면 같은 필드를 두고 다툰다.
    // 자식을 가진 것은 컨테이너이므로 자식에서 유도한 축이 이긴다 — 호출 순서가 아니라
    // 이 규칙이 승자를 정한다는 것을 여기서 고정한다.
    {
        auto container = std::make_unique<GameEngine::Runtime::Scene>(
            runtimeContext, "ContainerScene");
        GameEngine::Runtime::GameObject* const root = container->CreateGameObject("Canvas");
        GameEngine::Runtime::GameObject* const listObject = container->CreateGameObject("List");
        GameEngine::Runtime::GameObject* const rowA = container->CreateGameObject("RowA");
        GameEngine::Runtime::GameObject* const rowB = container->CreateGameObject("RowB");
        if (!Expect(root && listObject && rowA && rowB, "the container stage should build"))
        {
            return false;
        }
        static_cast<void>(root->AddComponent<Canvas>());

        RectTransform* const listRect = listObject->AddComponent<RectTransform>();
        listRect->SetOffsetMin({ 0.0f, 0.0f });
        listRect->SetOffsetMax({ 4.0f, 4.0f });
        GameEngine::Runtime::ContentFit* const contentFit =
            listObject->AddComponent<GameEngine::Runtime::ContentFit>();
        contentFit->SetDirection(GameEngine::Runtime::ContentFit::Direction::Vertical);
        contentFit->SetItemSize(10.0f);
        contentFit->SetSpacing(0.0f);
        contentFit->SetPadding(0.0f);
        GameEngine::Runtime::TextRenderer* const listText =
            listObject->AddComponent<GameEngine::Runtime::TextRenderer>();
        listText->SetText(std::string(LongSentence));
        listText->SetFontSize(13.0f);
        GameEngine::Runtime::LayoutElement* const listFit =
            listObject->AddComponent<GameEngine::Runtime::LayoutElement>();
        listFit->SetFit(GameEngine::Runtime::LayoutElement::Fit::Both);
        static_cast<void>(listObject->GetTransform().SetParent(&root->GetTransform()));
        static_cast<void>(rowA->GetTransform().SetParent(&listObject->GetTransform()));
        static_cast<void>(rowB->GetTransform().SetParent(&listObject->GetTransform()));

        static_cast<void>(sceneManager.AddScene(std::move(container)));
        measuring.Synchronize(sceneManager, 1280.0f, 720.0f, &measure);

        passed = Expect(
            NearlyEqual(listRect->GetResolvedRect().height, 20.0f),
            "a container should take its height from its children, not from its own text") &&
            passed;
        passed = Expect(
            listRect->GetResolvedRect().width > 40.0f,
            "the axis the container does not drive should still fit the text") && passed;
    }

    return passed;
}

bool RunSharedTextCacheTests()
{
    std::cout << "running shared text cache tests\n";

    // 등록된 글꼴이 재는 쪽에도 보이는지다. 재는 캐시와 그리는 캐시가 둘이면 등록은 한쪽에만
    // 닿고, 같은 문자열이 두 값의 폭을 갖는다 — 로그도 실패도 없이. 그 어긋남을 여기서 막는다.
    std::vector<std::byte> fontBytes;
    {
        std::ifstream file(R"(C:\Windows\Fonts\consola.ttf)", std::ios::binary);
        if (!file)
        {
            std::cout << "  skipped: no font file to register\n";
            return true;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        fontBytes.resize(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(fontBytes.data()), size);
    }

    // 번들 폰트를 먼저 등록해 둔다 — 빈 이름의 요청이 떨어질 자리다(FontLibrary는 등록 안 된
    // 이름을 가장 먼저 등록된 폰트로 보낸다). 이걸 두지 않으면 아래에서 "SharedMeasureFont"
    // 하나만 등록된 채 빈 이름을 재게 되고, 그러면 빈 이름도 결국 같은 폰트로 떨어져 두 폭이
    // 우연히 같아진다 — 이 시험이 잡으려는 「등록이 재는 쪽에 안 닿았다」와 구별이 안 된다.
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  skipped: no bundled font on this machine\n";
        return true;
    }
    auto sharedCache =
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer));
    GameEngine::Rendering::CachedTextMeasure measure{ sharedCache };

    // 그리는 쪽이 하는 일이다: 프로젝트의 글꼴을 이 세계의 캐시에 등록한다.
    if (!Expect(
            sharedCache->RegisterFont("SharedMeasureFont", fontBytes),
            "the shared cache should accept a font registration"))
    {
        return false;
    }

    GameEngine::Platform::TextRasterizationRequest registered;
    registered.text = "Unsaved changes - open anyway?";
    registered.fontFamily = "SharedMeasureFont";
    registered.fontSize = 13.0f;
    GameEngine::Platform::TextRasterizationRequest fallback = registered;
    fallback.fontFamily.clear();

    const GameEngine::Platform::TextExtent registeredExtent = measure.Measure(registered);
    const GameEngine::Platform::TextExtent fallbackExtent = measure.Measure(fallback);

    bool passed = Expect(
        registeredExtent.width > 0.0f && fallbackExtent.width > 0.0f,
        "both the registered and the default family should measure to something");
    // Consolas와 번들 폰트는 같은 문장을 다른 폭으로 그린다. 두 값이 같다면 등록이 재는 쪽에
    // 닿지 않아 빈 이름도 "SharedMeasureFont"로 떨어졌다는 뜻이고, 그것이 캐시가 둘일 때
    // 일어나는 일이다.
    passed = Expect(
        registeredExtent.width != fallbackExtent.width,
        "a font registered on the shared cache should change what the measure answers") && passed;

    return passed;
}

bool RunContentDemandTests()
{
    std::cout << "running content demand tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
    GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "DemandScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const listObject = scene->CreateGameObject("List");
    GameEngine::Runtime::GameObject* const shortRow = scene->CreateGameObject("ShortRow");
    GameEngine::Runtime::GameObject* const tallRow = scene->CreateGameObject("TallRow");
    GameEngine::Runtime::GameObject* const plainRow = scene->CreateGameObject("PlainRow");
    if (!Expect(
            canvasObject && listObject && shortRow && tallRow && plainRow,
            "the demand stage should build its objects"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    RectTransform* const listRect = listObject->AddComponent<RectTransform>();
    listRect->SetOffsetMin({ 0.0f, 0.0f });
    listRect->SetOffsetMax({ 200.0f, 4.0f });
    GameEngine::Runtime::ContentFit* const fit =
        listObject->AddComponent<GameEngine::Runtime::ContentFit>();
    fit->SetDirection(GameEngine::Runtime::ContentFit::Direction::Vertical);
    fit->SetItemSize(10.0f);
    fit->SetSpacing(0.0f);
    fit->SetPadding(0.0f);
    static_cast<void>(listObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    // 요구하는 크기가 제각각인 두 행이다. 개수로 세면 둘 다 10이지만, 각자의 요구는 30과 50이다.
    const auto addRow = [&](GameEngine::Runtime::GameObject& object, const float demandHeight)
    {
        static_cast<void>(object.AddComponent<RectTransform>());
        GameEngine::Runtime::LayoutElement* const element =
            object.AddComponent<GameEngine::Runtime::LayoutElement>();
        element->SetFit(GameEngine::Runtime::LayoutElement::Fit::Vertical);
        element->SetMinimumSize({ 0.0f, demandHeight });
        static_cast<void>(object.GetTransform().SetParent(&listObject->GetTransform()));
    };
    addRow(*shortRow, 30.0f);
    addRow(*tallRow, 50.0f);

    // 아무것도 요구하지 않는 행이다. 균일한 항목 크기로 세어져야 한다.
    static_cast<void>(plainRow->AddComponent<RectTransform>());
    static_cast<void>(plainRow->GetTransform().SetParent(&listObject->GetTransform()));

    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 1280.0f, 720.0f);

    // 30 + 50 + 10(요구 없는 행) = 90. 개수로 셌다면 3 * 10 = 30이었을 것이다.
    passed = Expect(
        NearlyEqual(listRect->GetResolvedRect().height, 90.0f),
        "a container should sum what its children ask for, not count them") && passed;

    // 간격이 있으면 자식 사이에만 들어간다. 셋이면 두 군데다.
    fit->SetSpacing(4.0f);
    layout.Synchronize(sceneManager, 1280.0f, 720.0f);
    passed = Expect(
        NearlyEqual(listRect->GetResolvedRect().height, 98.0f),
        "spacing should still land between the children only") && passed;

    return passed;
}

bool RunTextWrappingTests()
{
    std::cout << "running text wrapping tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
    GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "WrapScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const wideObject = scene->CreateGameObject("WideQuestion");
    GameEngine::Runtime::GameObject* const narrowObject = scene->CreateGameObject("NarrowQuestion");
    if (!Expect(
            canvasObject && wideObject && narrowObject,
            "the wrapping stage should build its objects"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    // 줄바꿈되는 긴 문장이 좁은 자리에서도 뒷부분을 보존하는지 확인한다.
    constexpr std::string_view SaveQuestion = "Unsaved changes - open anyway?";

    const auto addWrapped = [&](GameEngine::Runtime::GameObject& object, const float width)
    {
        RectTransform* const rect = object.AddComponent<RectTransform>();
        rect->SetOffsetMin({ 0.0f, 0.0f });
        rect->SetOffsetMax({ width, 8.0f });
        GameEngine::Runtime::TextRenderer* const text =
            object.AddComponent<GameEngine::Runtime::TextRenderer>();
        text->SetText(std::string(SaveQuestion));
        text->SetFontSize(13.0f);
        GameEngine::Runtime::LayoutElement* const element =
            object.AddComponent<GameEngine::Runtime::LayoutElement>();
        element->SetFit(GameEngine::Runtime::LayoutElement::Fit::Vertical);
        element->SetWrapping(true);
        static_cast<void>(object.GetTransform().SetParent(&canvasObject->GetTransform()));
        return rect;
    };
    RectTransform* const wideRect = addWrapped(*wideObject, 600.0f);
    RectTransform* const narrowRect = addWrapped(*narrowObject, 90.0f);

    static_cast<void>(sceneManager.AddScene(std::move(scene)));

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  text wrapping tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::Rendering::CachedTextMeasure measure{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)) };
    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 1280.0f, 720.0f, &measure);

    // 접히는 요소는 가로를 요구하지 않는다: 받은 폭 그대로다.
    passed = Expect(
        NearlyEqual(narrowRect->GetResolvedRect().width, 90.0f),
        "a wrapping element should accept the width it is given") && passed;
    passed = Expect(
        NearlyEqual(wideRect->GetResolvedRect().width, 600.0f),
        "a wide wrapping element should keep its width too") && passed;

    // 그리고 좁을수록 높아진다 — 접힌 높이가 폭의 함수라는 것이 이 단위의 전부다.
    const float narrowHeight = narrowRect->GetResolvedRect().height;
    const float wideHeight = wideRect->GetResolvedRect().height;
    passed = Expect(
        narrowHeight > wideHeight,
        "the same sentence should need more height in a narrower slot") && passed;
    passed = Expect(
        narrowHeight > 8.0f,
        "a wrapped element should grow past the height its anchors declared") && passed;

    // 접지 않는 같은 문장은 폭을 요구한다. 두 성질이 섞이지 않는다는 확인이다.
    GameEngine::Runtime::LayoutElement* const narrowElement =
        narrowObject->GetComponent<GameEngine::Runtime::LayoutElement>();
    narrowElement->SetWrapping(false);
    narrowElement->SetFit(GameEngine::Runtime::LayoutElement::Fit::Both);
    layout.Synchronize(sceneManager, 1280.0f, 720.0f, &measure);
    passed = Expect(
        narrowRect->GetResolvedRect().width > 90.0f,
        "without wrapping the same element asks for width instead") && passed;

    return passed;
}

bool RunTextCacheFootprintTests()
{
    std::cout << "running text cache footprint tests\n";
    bool passed = true;

    // 재기와 그리기가 같은 캐시를 나눠 쥔다. 세는 것이 이 시험의 전부다: 같은 문자열이 한
    // 항목인지 두 항목인지는 짐작이 아니라 세어서 답해야 한다.
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  text cache footprint tests skipped: no bundled font on this machine\n";
        return true;
    }
    auto sharedCache =
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer));
    GameEngine::Rendering::CachedTextMeasure measure{ sharedCache };

    GameEngine::Runtime::Game game{ nullptr, nullptr };
    auto scene = std::make_unique<GameEngine::Runtime::Scene>(
        game.GetRuntimeContext(), "FootprintScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    if (!Expect(canvasObject != nullptr, "the footprint stage should build its canvas"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    // 서로 다른 문장 여섯. 절반은 접히고 절반은 접히지 않는다 — 접기가 키를 가르는지를 함께 본다.
    constexpr int LabelCount = 6;
    for (int index = 0; index < LabelCount; ++index)
    {
        GameEngine::Runtime::GameObject* const labelObject =
            scene->CreateGameObject("Label" + std::to_string(index));
        if (!labelObject)
        {
            return Expect(false, "the footprint stage should build its labels");
        }
        RectTransform* const rect = labelObject->AddComponent<RectTransform>();
        rect->SetOffsetMin({ 0.0f, static_cast<float>(index) * 40.0f });
        rect->SetOffsetMax({ 160.0f, static_cast<float>(index) * 40.0f + 30.0f });
        GameEngine::Runtime::TextRenderer* const text =
            labelObject->AddComponent<GameEngine::Runtime::TextRenderer>();
        text->SetText("Unsaved changes in scene " + std::to_string(index) + "?");
        text->SetFontSize(13.0f);
        GameEngine::Runtime::LayoutElement* const element =
            labelObject->AddComponent<GameEngine::Runtime::LayoutElement>();
        element->SetFit(GameEngine::Runtime::LayoutElement::Fit::Vertical);
        element->SetWrapping(index % 2 == 0);
        static_cast<void>(labelObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    }

    // 재기만 하고 그려지지 않는 문장 하나다. 캔버스 밖으로 밀어 두면 보이는 부분이 비어
    // draw가 건너뛰어진다 — 배치만 이 문장을 안다.
    {
        GameEngine::Runtime::GameObject* const hidden = scene->CreateGameObject("Hidden");
        RectTransform* const rect = hidden->AddComponent<RectTransform>();
        rect->SetOffsetMin({ 4000.0f, 4000.0f });
        rect->SetOffsetMax({ 4200.0f, 4030.0f });
        GameEngine::Runtime::TextRenderer* const text =
            hidden->AddComponent<GameEngine::Runtime::TextRenderer>();
        text->SetText("A sentence nobody will ever see");
        text->SetFontSize(13.0f);
        GameEngine::Runtime::LayoutElement* const element =
            hidden->AddComponent<GameEngine::Runtime::LayoutElement>();
        element->SetFit(GameEngine::Runtime::LayoutElement::Fit::Both);
        static_cast<void>(hidden->GetTransform().SetParent(&canvasObject->GetTransform()));
    }

    if (!Expect(game.AddScene(std::move(scene)) != 0, "the footprint scene should be added"))
    {
        return false;
    }
    game.SetRenderSurfaceSize(1280.0f, 720.0f);

    // 배치가 여섯 문장을 잰다.
    const UILayoutSystem layout;
    layout.Synchronize(game.GetSceneManager(), 1280.0f, 720.0f, &measure);
    const std::size_t afterMeasure = sharedCache->GetShapedEntryCount();

    // 그리기가 같은 여섯 문장을 지난다. 같은 캐시이므로 새 항목이 생기지 않아야 한다.
    GameEngine::Rendering::RenderFrameBuilder builder;
    GameEngine::SceneRendering::SceneRenderPass frontend{ sharedCache };
    frontend.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();
    const std::size_t afterDraw = sharedCache->GetShapedEntryCount();

    std::cout << "  shaped entries: after measure=" << afterMeasure
              << ", after draw=" << afterDraw << " (labels=" << LabelCount << ")\n";

    // 글리프 비트맵은 배치 상자를 몇 픽셀 넘을 수 있다 — 어센더와 악센트가 그렇다. 그래서 이
    // 여유를 둔다. 잡으려는 어긋남은 블록의 절반이라 수십 픽셀이므로, 이 여유로 가려지지 않는다.
    constexpr float GlyphOverhang = 4.0f;

    // 줄바꿈한 글자도 같은 기준점을 사용해야 한다.
    // 모든 라벨이 음수가 아닌 좌표에 놓이므로 블록 높이가 달라져도 실제 잉크가 음수 좌표로 새면 안 된다.
    for (const GameEngine::Rendering::TextDraw* const draw :
         frame.GetDraws<GameEngine::Rendering::TextDraw>(
             GameEngine::Rendering::RenderPass::Overlay))
    {
        if (!draw->glyphs)
        {
            continue;
        }
        const float originX = draw->localToWorld.GetElement(3, 0);
        const float originY = draw->localToWorld.GetElement(3, 1);
        for (const GameEngine::Rendering::TextGlyphQuad& glyph : *draw->glyphs)
        {
            const float left = originX + glyph.centerX - glyph.width * 0.5f;
            const float top = originY - glyph.centerY - glyph.height * 0.5f;
            passed = Expect(
                left >= -GlyphOverhang && top >= -GlyphOverhang,
                "wrapped and unwrapped labels alike should keep their ink on the surface") &&
                passed;
        }
    }

    passed = Expect(
        afterMeasure == static_cast<std::size_t>(LabelCount) + 1,
        "measuring should leave one entry per sentence, hidden ones included") && passed;
    // 그리기가 같은 폭에서 접으므로 키가 같다. 두 경로가 다른 폭을 넣었다면 여기서 열둘이 된다.
    passed = Expect(
        afterDraw == afterMeasure,
        "drawing the same sentences should not add a second entry for any of them") && passed;

    return passed;
}

bool RunEditorSurfaceIsolationTests()
{
    std::cout << "running editor surface isolation tests\n";
    bool passed = true;

    // 에디터는 런타임을 둘 쥔다: 자기 UI가 사는 것과, 사람이 편집하는 프로젝트의 것. 둘이
    // 섞이면 우리 UI가 사용자 장면 파일에 실려 나가거나 계층 패널에 사용자 오브젝트인 척
    // 나타난다. 되돌리기 어려운 종류의 사고라서 짐작이 아니라 시험으로 건다.
    GameEngine::Runtime::Game editorRuntime{ nullptr, nullptr };
    GameEngine::Runtime::Game projectRuntime{ nullptr, nullptr };

    auto editorScene = std::make_unique<GameEngine::Runtime::Scene>(
        editorRuntime.GetRuntimeContext(), "EditorSurface");
    GameEngine::Runtime::GameObject* const canvasObject =
        editorScene->CreateGameObject("Editor UI");
    GameEngine::Runtime::GameObject* const markerObject =
        editorScene->CreateGameObject("Surface Marker");
    if (!Expect(canvasObject && markerObject, "the editor surface should assemble"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());
    static_cast<void>(markerObject->AddComponent<RectTransform>());
    GameEngine::Runtime::TextRenderer* const markerText =
        markerObject->AddComponent<GameEngine::Runtime::TextRenderer>();
    markerText->SetText("component surface");
    static_cast<void>(markerObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    const unsigned int canvasId = canvasObject->GetInstanceId();
    static_cast<void>(editorRuntime.AddScene(std::move(editorScene)));

    // 사람이 편집하는 장면이다. 오브젝트 하나만 있고, 에디터 UI는 여기 없어야 한다.
    auto projectScene = std::make_unique<GameEngine::Runtime::Scene>(
        projectRuntime.GetRuntimeContext(), "PlayerScene");
    GameEngine::Runtime::GameObject* const playerObject =
        projectScene->CreateGameObject("Player");
    if (!Expect(playerObject != nullptr, "the project scene should assemble"))
    {
        return false;
    }
    GameEngine::Runtime::Scene* const projectSceneView = projectScene.get();
    static_cast<void>(projectRuntime.AddScene(std::move(projectScene)));

    // ① 편집 대상 런타임의 어떤 장면에도 Canvas가 없다.
    bool projectHasCanvas = false;
    std::size_t projectObjectCount = 0;
    for (const auto& [sceneId, scene] : projectRuntime.GetSceneManager().GetActiveScenes())
    {
        static_cast<void>(sceneId);
        if (!scene)
        {
            continue;
        }
        for (const auto& [objectId, gameObject] : scene->GetGameObjects())
        {
            static_cast<void>(objectId);
            ++projectObjectCount;
            if (gameObject && gameObject->GetComponent<Canvas>())
            {
                projectHasCanvas = true;
            }
        }
    }
    passed = Expect(!projectHasCanvas, "the edited project should hold no editor canvas") && passed;
    passed = Expect(
        projectObjectCount == 1,
        "the edited project should hold only what the person put there") && passed;

    // ② 인스턴스 id는 런타임 안에서만 유일하다. 두 레지스트리가 각각 1부터 세므로 같은
    //    수가 양쪽에 있고, 그래서 "id로 못 찾는다"는 확인은 성립하지 않는다 — 실제로
    //    편집 대상 런타임도 그 id로 무언가를 돌려준다. 지켜야 하는 것은 그것이 <b>우리
    //    UI가 아니라는 것</b>이다. 이 사실 자체가 함정이라 여기 적어 둔다: 두 런타임의
    //    id를 섞어 쓰는 코드는 조용히 엉뚱한 오브젝트를 잡는다.
    //
    //    오늘 그것을 막는 것은 한 자리다: id를 해석하는 유일한 경로인
    //    EditorContext::FindObject가 편집 대상 런타임만 본다. 무엇이 막고 있는지가
    //    적혀 있어야 그것이 사라질 때 알아챌 수 있으므로 여기 적어 둔다.
    GameEngine::Runtime::Object* const projectResolved = projectRuntime.FindObject(canvasId);
    passed = Expect(
        projectResolved != static_cast<GameEngine::Runtime::Object*>(canvasObject),
        "an id resolved in the project runtime should never be an editor UI object") &&
        passed;
    passed = Expect(
        editorRuntime.FindObject(canvasId) != nullptr,
        "the editor runtime should still hold its own UI object") && passed;

    // ③ 사용자 장면을 저장한 글에 우리 UI의 흔적이 없다. 파일로 새는 것이 가장 되돌리기 어렵다.
    const std::string saved =
        GameEngine::Serialization::SceneSerializer::SaveToText(*projectSceneView);
    passed = Expect(
        saved.find("Canvas") == std::string::npos &&
            saved.find("RectTransform") == std::string::npos &&
            saved.find("component surface") == std::string::npos,
        "a saved project scene should carry no trace of the editor's own UI") && passed;
    passed = Expect(
        saved.find("Player") != std::string::npos,
        "a saved project scene should still carry what the person made") && passed;

    return passed;
}

bool RunSolidRectTests()
{
    std::cout << "running solid rect tests\n";
    bool passed = true;

    // 그림 없는 UI 요소가 단색 사각형으로 그려지는지다. 버튼의 바탕도 패널도 구분선도 이것이라,
    // 여기서 에셋을 요구하면 UI를 만들 때마다 1픽셀짜리 그림을 하나씩 만들게 된다.
    GameEngine::Runtime::Game game{ nullptr, nullptr };
    auto scene = std::make_unique<GameEngine::Runtime::Scene>(
        game.GetRuntimeContext(), "SolidRectScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const panelObject = scene->CreateGameObject("Panel");
    GameEngine::Runtime::GameObject* const worldObject = scene->CreateGameObject("WorldSprite");
    if (!Expect(canvasObject && panelObject && worldObject, "the solid rect scene should assemble"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    RectTransform* const panelRect = panelObject->AddComponent<RectTransform>();
    panelRect->SetOffsetMin({ 20.0f, 30.0f });
    panelRect->SetOffsetMax({ 140.0f, 70.0f });
    GameEngine::Runtime::SpriteRenderer* const panelSprite =
        panelObject->AddComponent<GameEngine::Runtime::SpriteRenderer>();
    panelSprite->SetSpace(GameEngine::Runtime::SpriteRenderer::Space::Screen);
    panelSprite->SetColor({ 0.25f, 0.5f, 0.75f, 1.0f });
    static_cast<void>(panelObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    // 월드에 놓인 렌더러가 에셋을 잃은 것은 고쳐야 할 실수다. 단색으로 대신하지 않는다.
    static_cast<void>(worldObject->AddComponent<GameEngine::Runtime::SpriteRenderer>());

    if (!Expect(game.AddScene(std::move(scene)) != 0, "the solid rect scene should be added"))
    {
        return false;
    }
    game.SetRenderSurfaceSize(800.0f, 600.0f);
    game.Update(0.0f);

    GameEngine::Rendering::RenderFrameBuilder builder;
    builder.SetRenderTargetSize({ 800, 600 });
    GameEngine::SceneRendering::SceneRenderPass frontend{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
            GameEngine::Platform::PlatformServices::CreateTextRasterizer()) };
    frontend.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();

    const std::vector<const GameEngine::Rendering::SpriteDraw*> overlay =
        frame.GetDraws<GameEngine::Rendering::SpriteDraw>(
            GameEngine::Rendering::RenderPass::Overlay);
    const std::vector<const GameEngine::Rendering::SpriteDraw*> world =
        frame.GetDraws<GameEngine::Rendering::SpriteDraw>(
            GameEngine::Rendering::RenderPass::Transparent);

    passed = Expect(
        overlay.size() == 1,
        "a UI element with no sprite should still draw one rect") && passed;
    passed = Expect(
        world.empty(),
        "a world renderer with no sprite should stay skipped, not become a coloured box") &&
        passed;
    if (overlay.empty())
    {
        return false;
    }

    // 색은 틴트가 정한다. 그림은 흰 픽셀 하나이므로 틴트가 곧 화면의 색이다.
    const GameEngine::Rendering::SpriteDraw& drawn = *overlay.front();
    passed = Expect(
        NearlyEqual(drawn.tint.r, 0.25f) && NearlyEqual(drawn.tint.g, 0.5f) &&
            NearlyEqual(drawn.tint.b, 0.75f),
        "the rect's colour should be the renderer's own") && passed;
    // 배치가 준 사각형에 맞는다: 이동은 그 중심이고, 배율은 1픽셀 그림을 그 크기로 늘린다.
    passed = Expect(
        NearlyEqual(drawn.localToWorld.GetElement(3, 0), 80.0f) &&
            NearlyEqual(drawn.localToWorld.GetElement(3, 1), 50.0f),
        "the rect should sit at the centre of its resolved rect") && passed;
    passed = Expect(
        NearlyEqual(drawn.localToWorld.GetElement(0, 0), 120.0f) &&
            NearlyEqual(drawn.localToWorld.GetElement(1, 1), 40.0f),
        "the rect should stretch the single pixel to the size layout gave it") && passed;

    return passed;
}

bool RunLayoutStackingTests()
{
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::ContentFit;
    using GameEngine::Runtime::LayoutElement;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running layout stacking tests\n";
    bool passed = true;

    GameEngine::Runtime::ObjectRegistry objectRegistry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext runtimeContext{ objectRegistry, input };
    GameEngine::Runtime::SceneManager sceneManager{ runtimeContext };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(runtimeContext, "StackScene");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    GameEngine::Runtime::GameObject* const stackObject = scene->CreateGameObject("Stack");
    GameEngine::Runtime::GameObject* const first = scene->CreateGameObject("First");
    GameEngine::Runtime::GameObject* const second = scene->CreateGameObject("Second");
    GameEngine::Runtime::GameObject* const third = scene->CreateGameObject("Third");
    if (!Expect(
            canvasObject && stackObject && first && second && third,
            "the stacking scene should assemble"))
    {
        return false;
    }
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    // 쌓는 상자. 윗변만 선언하고 높이는 자식이 정한다.
    RectTransform* const stackRect = stackObject->AddComponent<RectTransform>();
    stackRect->SetOffsetMin({ 10.0f, 100.0f });
    stackRect->SetOffsetMax({ 210.0f, 100.0f });
    ContentFit* const fit = stackObject->AddComponent<ContentFit>();
    fit->SetDirection(ContentFit::Direction::Vertical);
    fit->SetItemSize(0.0f);
    fit->SetSpacing(4.0f);
    fit->SetPadding(6.0f);
    static_cast<void>(stackObject->GetTransform().SetParent(&canvasObject->GetTransform()));

    // 세 줄. 각자 높이를 요구하고, 세로 자리는 선언하지 않는다 — 그것이 쌓는 상자의 몫이다.
    const auto addRow = [&](GameEngine::Runtime::GameObject& object, const float height)
    {
        RectTransform* const rect = object.AddComponent<RectTransform>();
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 1.0f, 0.0f });
        rect->SetOffsetMin({ 2.0f, 0.0f });
        rect->SetOffsetMax({ -2.0f, 0.0f });
        LayoutElement* const element = object.AddComponent<LayoutElement>();
        element->SetFit(LayoutElement::Fit::Vertical);
        element->SetMinimumSize({ 0.0f, height });
        static_cast<void>(object.GetTransform().SetParent(&stackObject->GetTransform()));
        return rect;
    };
    RectTransform* const firstRect = addRow(*first, 20.0f);
    RectTransform* const secondRect = addRow(*second, 30.0f);
    RectTransform* const thirdRect = addRow(*third, 10.0f);

    static_cast<void>(sceneManager.AddScene(std::move(scene)));
    const UILayoutSystem layout;
    layout.Synchronize(sceneManager, 800.0f, 600.0f);

    // 자리는 앞의 줄들이 정한다: 여백 6, 첫 줄 20, 간격 4, 둘째 30, 간격 4, 셋째 10, 여백 6.
    passed = Expect(
        NearlyEqual(firstRect->GetResolvedRect().y, 106.0f) &&
            NearlyEqual(firstRect->GetResolvedRect().height, 20.0f),
        "the first row should sit after the padding with its own height") && passed;
    passed = Expect(
        NearlyEqual(secondRect->GetResolvedRect().y, 130.0f) &&
            NearlyEqual(secondRect->GetResolvedRect().height, 30.0f),
        "the second row should follow the first plus the spacing") && passed;
    passed = Expect(
        NearlyEqual(thirdRect->GetResolvedRect().y, 164.0f),
        "the third row should follow the second plus the spacing") && passed;
    passed = Expect(
        NearlyEqual(stackRect->GetResolvedRect().height, 80.0f),
        "the stack should be exactly as tall as what it holds") && passed;
    // 다른 축은 자식의 앵커가 정한다: 상자 폭 200에서 양쪽 2씩 들인 196.
    passed = Expect(
        NearlyEqual(secondRect->GetResolvedRect().x, 12.0f) &&
            NearlyEqual(secondRect->GetResolvedRect().width, 196.0f),
        "the cross axis should still come from the child's own anchors") && passed;

    // 가운데 줄을 끄면 그 자리가 사라지고 아래 줄이 올라온다. 꺼진 줄은 자리를 차지하지 않는다.
    second->SetActive(false);
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    passed = Expect(
        NearlyEqual(thirdRect->GetResolvedRect().y, 130.0f),
        "an inactive row should give its place to the rows below") && passed;
    passed = Expect(
        NearlyEqual(stackRect->GetResolvedRect().height, 46.0f),
        "an inactive row should not count toward the stack's height") && passed;

    // 다시 켜면 돌아온다.
    second->SetActive(true);
    layout.Synchronize(sceneManager, 800.0f, 600.0f);
    passed = Expect(
        NearlyEqual(thirdRect->GetResolvedRect().y, 164.0f),
        "a row switched back on should take its place again") && passed;

    return passed;
}

static const TestSupport::Registration gRectResolutionTests{
    "UILayout", "rect resolution tests should pass", RunRectResolutionTests };

static const TestSupport::Registration gUILayoutHierarchyTests{
    "UILayout", "UI layout hierarchy tests should pass", RunUILayoutHierarchyTests };

static const TestSupport::Registration gUISurfaceSubmissionTests{
    "UILayout", "UI surface submission tests should pass", RunUISurfaceSubmissionTests };

static const TestSupport::Registration gRectMaskTests{
    "UILayout", "rect mask tests should pass", RunRectMaskTests };

static const TestSupport::Registration gLayoutElementTests{
    "UILayout", "layout element tests should pass", RunLayoutElementTests };

static const TestSupport::Registration gSharedTextCacheTests{
    "UILayout", "shared text cache tests should pass", RunSharedTextCacheTests };

static const TestSupport::Registration gContentDemandTests{
    "UILayout", "content demand tests should pass", RunContentDemandTests };

static const TestSupport::Registration gTextWrappingTests{
    "UILayout", "text wrapping tests should pass", RunTextWrappingTests };

static const TestSupport::Registration gTextCacheFootprintTests{
    "UILayout", "text cache footprint tests should pass", RunTextCacheFootprintTests };

static const TestSupport::Registration gEditorSurfaceIsolationTests{
    "UILayout", "editor surface isolation tests should pass", RunEditorSurfaceIsolationTests };

static const TestSupport::Registration gSolidRectTests{
    "UILayout", "solid rect tests should pass", RunSolidRectTests };

static const TestSupport::Registration gLayoutStackingTests{
    "UILayout", "layout stacking tests should pass", RunLayoutStackingTests };
