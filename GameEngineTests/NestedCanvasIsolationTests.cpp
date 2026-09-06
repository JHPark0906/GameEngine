#include "NestedCanvasIsolationTests.h"

#include <cmath>
#include <memory>
#include <utility>

#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/LayoutElement.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UILayoutSystem.h"
#include "TestSupport.h"

bool RunNestedCanvasIsolationTests()
{
    namespace Runtime = GameEngine::Runtime;
    return TestSupport::ForEachUiScale([](const float scale)
    {
        bool passed = true;
        for (const bool innerFirst : { false, true })
        {
            Runtime::ObjectRegistry registry;
            Runtime::Input input;
            Runtime::RuntimeContext context(registry, input);
            Runtime::SceneManager manager(context);
            auto scene = std::make_unique<Runtime::Scene>(context, "NestedCanvas");
            auto* first = scene->CreateGameObject("First");
            auto* second = scene->CreateGameObject("Second");
            auto* outer = innerFirst ? second : first;
            auto* inner = innerFirst ? first : second;
            auto* outerCanvas = outer->AddComponent<Runtime::Canvas>();
            outerCanvas->SetScaleFactor(scale);
            auto* outerRect = outer->AddComponent<Runtime::RectTransform>();
            outerRect->SetOffsetMin({ 50.0f, 40.0f });
            outerRect->SetOffsetMax({ 250.0f, 240.0f });
            inner->AddComponent<Runtime::Canvas>()->SetScaleFactor(3.0f * scale);
            auto* innerRect = inner->AddComponent<Runtime::RectTransform>();
            innerRect->SetAnchorMax({ 1.0f, 1.0f });
            innerRect->SetOffsetMax({ 0.0f, 0.0f });
            static_cast<void>(inner->GetTransform().SetParent(&outer->GetTransform()));
            auto* child = scene->CreateGameObject("Child");
            static_cast<void>(child->GetTransform().SetParent(&inner->GetTransform()));
            auto* childRect = child->AddComponent<Runtime::RectTransform>();
            childRect->SetOffsetMin({ 10.0f, 20.0f });
            childRect->SetOffsetMax({ 50.0f, 60.0f });
            auto* demand = child->AddComponent<Runtime::LayoutElement>();
            demand->SetFit(Runtime::LayoutElement::Fit::Both);
            demand->SetMinimumSize({ 60.0f, 70.0f });
            if (!TestSupport::Expect(manager.AddScene(std::move(scene)) != 0,
                "nested canvas fixture must load")) return false;

            const Runtime::UILayoutSystem layout;
            for (int frame = 0; frame < 3; ++frame)
            {
                outerCanvas->SetScaleFactor(scale * static_cast<float>(frame + 1));
                layout.Synchronize(manager, 800.0f * scale, 600.0f * scale);
                const auto& root = innerRect->GetResolvedRect();
                const auto& rect = childRect->GetResolvedRect();
                const auto near = [](const float firstValue, const float secondValue)
                { return std::abs(firstValue - secondValue) < 0.001f; };
                passed = TestSupport::Expect(near(root.x, 0.0f) && near(root.y, 0.0f) &&
                    near(root.width, 800.0f * scale) && near(root.height, 600.0f * scale) &&
                    near(rect.x, 30.0f * scale) && near(rect.y, 60.0f * scale) &&
                    near(rect.width, 180.0f * scale) && near(rect.height, 210.0f * scale) &&
                    near(childRect->GetDesiredSize().height, 210.0f * scale),
                    "nested canvas measurement and placement must use its own surface and scale in either creation order") && passed;
            }
        }
        return passed;
    });
}

static const TestSupport::Registration gNestedCanvasIsolationTests{
    "UIEvent", "nested canvases must own independent layout boundaries", RunNestedCanvasIsolationTests };
