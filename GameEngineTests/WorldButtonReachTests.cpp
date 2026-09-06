#include "WorldButtonReachTests.h"

#include <cstddef>
#include <span>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Platform/IInput.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>커서 자리만 정해 주는 가짜 입력이다. 누르지는 않는다.</summary>
    class PointerOnlyInput final : public GameEngine::Platform::IInput
    {
    public:
        void ReadState(GameEngine::Platform::InputState& state) override
        {
            state = mState;
            mState.TakeAccumulated();
        }

        void SetCursor(const int x, const int y)
        {
            mState.cursor.x = x;
            mState.cursor.y = y;
        }

    private:
        GameEngine::Platform::InputState mState;
    };

    /// <summary>
    /// 월드 공간 StartButton의 크기와 배치를 고정한 시험 데이터다.
    /// 저장소 밖 프로젝트 파일의 변경이나 존재 여부에 따라 시험 결과가 달라지지 않도록 모양을 직접 구성한다.
    /// </summary>
    constexpr const char* SummitStartButtonScene =
        R"({"sceneName": "MainMenu", "gameObjects": [)"
        R"({"id": 4, "name": "StartButton", "isActive": true, "components": [)"
        R"({"type": "Transform", "position": [0, 0, 0], "rotation": [0, 0, 0],)"
        R"( "rotationUnit": "degrees", "rotationOrder": "rollPitchYaw", "scale": [1, 1, 1]},)"
        R"({"type": "Button", "enabled": true, "interactable": true,)"
        R"( "normalColor": [1, 1, 1, 1], "hoveredColor": [0.9, 0.94, 1, 1],)"
        R"( "pressedColor": [0.7, 0.78, 0.92, 1], "disabledColor": [0.6, 0.6, 0.6, 1]},)"
        R"({"type": "SpriteRenderer", "enabled": true, "visible": true, "sortingOrder": 0,)"
        R"( "size": [1, 1], "drawMode": "simple", "space": "world", "frame": 0,)"
        R"( "flipX": false, "flipY": false, "color": [1, 1, 1, 1]})"
        R"(]}]})";
}

bool RunWorldButtonReachTests()
{
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::Scene;
    using GameEngine::Runtime::UIEventSystem;
    using GameEngine::Runtime::UILayoutSystem;

    std::cout << "running world button reach tests\n";

    // 컴포넌트 팩토리가 등록되어 있어야 저장된 Button이 되살아난다. 등록 없이 읽으면 그
    // 컴포넌트가 조용히 빠지고, 시험은 "버튼이 없어서 안 맞는다"라는 다른 것을 재게 된다.
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    const std::string text = SummitStartButtonScene;
    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    std::unique_ptr<Scene> scene = GameEngine::Serialization::SceneSerializer::LoadFromBytes(
        bytes, "MainMenu.scene", context);
    if (!Expect(scene != nullptr, "the saved button should load"))
    {
        return false;
    }

    GameObject* buttonObject = nullptr;
    for (GameObject* const object : scene->GetRootGameObjects())
    {
        if (object && object->GetName() == "StartButton")
        {
            buttonObject = object;
        }
    }
    if (!Expect(buttonObject != nullptr, "the saved scene should hold the button"))
    {
        return false;
    }

    Button* const button = buttonObject->GetComponent<Button>();
    const bool hasRect = buttonObject->GetComponent<RectTransform>() != nullptr;
    std::cout << "  saved button has Button: " << (button ? "yes" : "no")
              << ", RectTransform: " << (hasRect ? "yes" : "no") << "\n";
    bool passed = Expect(button != nullptr, "the saved object should carry a Button");
    // 저장된 모양을 먼저 확인한다. 이것이 참이 아니면 아래 측정은 다른 것을 재는 것이다.
    passed = Expect(!hasRect, "the saved button should have no screen rectangle") && passed;

    if (sceneManager.AddScene(std::move(scene)) == 0 || !button)
    {
        return Expect(false, "the saved scene should become the active scene") && passed;
    }

    PointerOnlyInput source;
    UIEventSystem events;
    const UILayoutSystem layout;

    // 한 점이 아니라 화면 전체를 훑는다. 재려는 것은 "여기서 안 맞는다"가 아니라 "어디서도
    // 맞지 않는다"이고, 사각형이 없는 요소에 대해서는 그것이 참이어야 한다.
    std::size_t takenCount = 0;
    std::size_t hoveredCount = 0;
    for (int y = 0; y <= 720; y += 60)
    {
        for (int x = 0; x <= 1280; x += 80)
        {
            source.SetCursor(x, y);
            input.BeginFrame(source);
            layout.Synchronize(sceneManager, 1280.0f, 720.0f);
            if (events.Synchronize(sceneManager, input))
            {
                ++takenCount;
            }
            if (button->IsHovered())
            {
                ++hoveredCount;
            }
        }
    }
    std::cout << "  swept the screen: pointer taken " << takenCount << " times, button hovered "
              << hoveredCount << " times\n";
    passed = Expect(
        takenCount == 0, "a button with no screen rectangle should never take the pointer") &&
        passed;
    passed = Expect(
        hoveredCount == 0, "a button with no screen rectangle should never look hovered") && passed;

    // 이 시험이 눈먼 것이 아님을 보인다: 같은 Button 컴포넌트를 제대로 세우면 곧바로 맞는다.
    // 세우는 데 필요한 것이 둘이라는 사실도 여기서 드러난다 — 사각형 하나로는 부족하고,
    // 배치가 흐르기 시작하는 뿌리인 Canvas가 조상에 있어야 그 사각형이 풀린다. 저장된 버튼은
    // 그 둘을 다 갖고 있지 않다.
    GameEngine::Runtime::ObjectRegistry reachRegistry;
    GameEngine::Runtime::Input reachInput;
    GameEngine::Runtime::RuntimeContext reachContext{ reachRegistry, reachInput };
    GameEngine::Runtime::SceneManager reachSceneManager{ reachContext };

    auto reachScene = std::make_unique<Scene>(reachContext, "Reachable");
    GameObject* const canvasObject = reachScene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());
    GameObject* const reachable = reachScene->CreateGameObject("StartButton");
    static_cast<void>(reachable->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const rect = reachable->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ 0.0f, 0.0f });
    rect->SetOffsetMax({ 200.0f, 100.0f });
    Button* const reachableButton = reachable->AddComponent<Button>();

    if (reachSceneManager.AddScene(std::move(reachScene)) == 0 || !reachableButton)
    {
        return Expect(false, "the reachable scene should assemble") && passed;
    }

    PointerOnlyInput reachSource;
    UIEventSystem reachEvents;
    reachSource.SetCursor(100, 50);
    reachInput.BeginFrame(reachSource);
    layout.Synchronize(reachSceneManager, 1280.0f, 720.0f);
    const bool takenWithRect = reachEvents.Synchronize(reachSceneManager, reachInput);
    std::cout << "  a button under a canvas with a rectangle at (100,50): taken "
              << (takenWithRect ? "yes" : "no") << ", hovered "
              << (reachableButton->IsHovered() ? "yes" : "no") << "\n";
    passed = Expect(
        takenWithRect && reachableButton->IsHovered(),
        "a button under a canvas with a rectangle does take the pointer") && passed;

    return passed;
}

static const TestSupport::Registration gWorldButtonReachTests{
    "UIEvent", "world button reach tests should pass", RunWorldButtonReachTests };
