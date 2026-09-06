#include "UIStackOrderTests.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/Dropdown.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "Runtime/UIWindow.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

namespace
{
using namespace GameEngine;
using TestSupport::Expect;

bool SharedOrderAtScale(const float scale,
    const std::shared_ptr<Rendering::TextRasterizationCache>& cache)
{
    Runtime::Game game(nullptr, nullptr);
    auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "UI stack");
    auto* canvas = scene->CreateGameObject("Canvas");
    canvas->AddComponent<Runtime::Canvas>()->SetScaleFactor(scale);
    const auto addWindow = [&](const char* name, const int sortingOrder)
    {
        auto* object = scene->CreateGameObject(name);
        static_cast<void>(object->GetTransform().SetParent(&canvas->GetTransform()));
        object->AddComponent<Runtime::RectTransform>()->SetOffsetMax({ 100.0f, 100.0f });
        static_cast<void>(object->AddComponent<Runtime::UIWindow>());
        auto* sprite = object->AddComponent<Runtime::SpriteRenderer>();
        sprite->SetSpace(Runtime::SpriteRenderer::Space::Screen);
        sprite->SetSortingOrder(sortingOrder);
        auto* text = object->AddComponent<Runtime::TextRenderer>();
        text->SetSpace(Runtime::TextRenderer::Space::Screen);
        text->SetSortingOrder(sortingOrder + 1);
        text->SetFontFamily("UIStackFont");
        text->SetText(name);
        text->SetBackgroundColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        return object;
    };
    auto* first = addWindow("First", 100);
    auto* firstButton = first->AddComponent<Runtime::Button>();
    auto* second = addWindow("Second", -100);
    auto* dropdown = second->AddComponent<Runtime::Dropdown>();
    dropdown->SetOptions({ "Choice", "Other" });
    dropdown->SetValue(0);
    dropdown->Open();

    const auto addStandalone = [&](const char* name, const int order)
    {
        auto* object = scene->CreateGameObject(name);
        auto* text = object->AddComponent<Runtime::TextRenderer>();
        text->SetSpace(Runtime::TextRenderer::Space::Screen);
        text->SetFontFamily("UIStackFont");
        text->SetText("I");
        text->SetSortingOrder(order);
        return text->GetInstanceId();
    };
    const auto standaloneHigh = addStandalone("Independent high", 200);
    const auto standaloneLow = addStandalone("Independent low", -200);
    static_cast<void>(game.AddScene(std::move(scene)));
    Runtime::UILayoutSystem layout;
    Runtime::UIEventSystem events;
    SceneRendering::SceneRenderPass frontend(cache);
    const auto collect = [&]()
    {
        layout.Synchronize(game.GetSceneManager(), 640.0f, 480.0f);
        static_cast<void>(events.Synchronize(game.GetSceneManager(), game.GetInput()));
        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ 640, 480 });
        frontend.Collect(game, builder);
        return std::move(builder).Build();
    };
    const auto checkTop = [&](const Rendering::RenderFrame& frame, Runtime::GameObject& top,
        Runtime::GameObject& bottom)
    {
        const auto topSprite = top.GetComponent<Runtime::SpriteRenderer>()->GetInstanceId();
        const auto topText = top.GetComponent<Runtime::TextRenderer>()->GetInstanceId();
        const auto bottomSprite = bottom.GetComponent<Runtime::SpriteRenderer>()->GetInstanceId();
        const auto bottomText = bottom.GetComponent<Runtime::TextRenderer>()->GetInstanceId();
        bool topStarted = false;
        bool bottomSeen = false;
        bool backgroundSeen = false;
        bool glyphSeen = false;
        bool orderValid = true;
        std::uint64_t topOrder = 0;
        for (const auto& packet : frame.GetDrawPackets(Rendering::RenderPass::Overlay))
        {
            if (packet.instanceId == bottomSprite || packet.instanceId == bottomText)
            {
                bottomSeen = true;
                orderValid = !topStarted && orderValid;
            }
            if (packet.instanceId == topSprite || packet.instanceId == topText)
            {
                topStarted = true;
                if (topOrder == 0) topOrder = packet.overlayStackOrder;
                orderValid = packet.overlayStackOrder == topOrder && topOrder != 0 && orderValid;
            }
            if (packet.instanceId == topText)
            {
                if (std::holds_alternative<Rendering::SpriteDraw>(packet.payload)) backgroundSeen = !glyphSeen;
                else if (std::holds_alternative<Rendering::TextDraw>(packet.payload))
                {
                    orderValid = backgroundSeen && orderValid;
                    glyphSeen = true;
                }
            }
        }
        return Expect(frame.Validate().IsValid() && orderValid && topStarted && bottomSeen &&
            backgroundSeen && glyphSeen,
            "window background, sprites and glyphs must share the input stack and retain local draw order");
    };
    auto initial = collect();
    if (!checkTop(initial, *second, *first) || !Expect(dropdown->IsHovered() && !firstButton->IsHovered(),
        "the later window must own both the visible dropdown and the pointer")) return false;
    const auto& initialPackets = initial.GetDrawPackets(Rendering::RenderPass::Overlay);
    if (!Expect(initialPackets.size() >= 2 && initialPackets[0].instanceId == standaloneLow &&
        initialPackets[1].instanceId == standaloneHigh && initialPackets[0].overlayStackOrder == 0 &&
        initialPackets[1].overlayStackOrder == 0,
        "standalone screen renderers must retain their explicit sortingOrder")) return false;

    first->GetTransform().SetAsLastSibling();
    const auto raised = collect();
    if (!checkTop(raised, *first, *second) || !Expect(firstButton->IsHovered() && !dropdown->IsHovered(),
        "raising a window must move all its visuals and its hit target together")) return false;
    second->GetComponent<Runtime::UIWindow>()->SetModal(true);
    const auto modal = collect();
    if (!checkTop(modal, *second, *first) || !Expect(dropdown->IsHovered() && !firstButton->IsHovered(),
        "a modal below its sibling must render above it and exclusively receive the pointer")) return false;
    auto* nested = second->GetScene()->CreateGameObject("Modal child window");
    static_cast<void>(nested->GetTransform().SetParent(&second->GetTransform()));
    nested->AddComponent<Runtime::RectTransform>()->SetOffsetMax({ 100.0f, 100.0f });
    static_cast<void>(nested->AddComponent<Runtime::UIWindow>());
    auto* nestedSprite = nested->AddComponent<Runtime::SpriteRenderer>();
    nestedSprite->SetSpace(Runtime::SpriteRenderer::Space::Screen);
    auto* lateContent = second->GetScene()->CreateGameObject("Later parent content");
    static_cast<void>(lateContent->GetTransform().SetParent(&second->GetTransform()));
    lateContent->AddComponent<Runtime::RectTransform>()->SetOffsetMax({ 100.0f, 100.0f });
    auto* lateButton = lateContent->AddComponent<Runtime::Button>();
    auto* lateSprite = lateContent->AddComponent<Runtime::SpriteRenderer>();
    lateSprite->SetSpace(Runtime::SpriteRenderer::Space::Screen);
    lateSprite->SetSortingOrder(1000);
    for (const bool isModal : { true, false })
    {
        second->GetComponent<Runtime::UIWindow>()->SetModal(isModal);
        if (!isModal) second->GetTransform().SetAsLastSibling();
        const auto nestedFrame = collect();
        const auto& nestedPackets = nestedFrame.GetDrawPackets(Rendering::RenderPass::Overlay);
        const auto latePacket = std::ranges::find(nestedPackets, lateSprite->GetInstanceId(),
            &Rendering::DrawPacket::instanceId);
        if (!Expect(nestedFrame.Validate().IsValid() && latePacket != nestedPackets.end() &&
            nestedPackets.back().instanceId == nestedSprite->GetInstanceId() &&
            latePacket->overlayStackOrder < nestedPackets.back().overlayStackOrder &&
            !lateButton->IsHovered() && !dropdown->IsHovered() && !firstButton->IsHovered(),
            "nested window backgrounds must cover parent content created later in both modal and ordinary windows")) return false;
    }
    lateContent->SetActive(false);
    nested->SetActive(false);
    second->GetComponent<Runtime::UIWindow>()->SetModal(false);
    first->GetTransform().SetAsLastSibling();
    if (!checkTop(collect(), *first, *second)) return false;

    auto malformed = raised;
    auto& packets = const_cast<std::vector<Rendering::DrawPacket>&>(
        malformed.GetDrawPackets(Rendering::RenderPass::Overlay));
    std::swap(packets.front(), packets.back());
    return Expect(malformed.Validate().error == Rendering::RenderFrameValidationError::PacketSortOrder,
        "frame validation must reject packet order that disagrees with the shared overlay stack");
}
}

bool RunUIStackOrderTests()
{
    auto rasterizer = TestSupport::CreateTestTextRasterizer("UIStackFont");
    if (!TestSupport::Expect(rasterizer != nullptr, "UI stack tests require the bundled test font")) return false;
    auto cache = std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer));
    return TestSupport::ForEachUiScale([&](const float scale) { return SharedOrderAtScale(scale, cache); });
}

static const TestSupport::Registration gUIStackOrderTests{
    "UIEvent", "rendering and input should share the UI stack", RunUIStackOrderTests };
