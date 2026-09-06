#include <algorithm>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "../GameEngine/Runtime/Button.h"
#include "../GameEngine/Runtime/Canvas.h"
#include "../GameEngine/Runtime/ComponentType.h"
#include "../GameEngine/Runtime/Dropdown.h"
#include "../GameEngine/Runtime/GameObject.h"
#include "../GameEngine/Runtime/Input.h"
#include "../GameEngine/Runtime/InputField.h"
#include "../GameEngine/Runtime/ObjectRegistry.h"
#include "../GameEngine/Runtime/PropertyDescriptor.h"
#include "../GameEngine/Runtime/RectTransform.h"
#include "../GameEngine/Runtime/RuntimeContext.h"
#include "../GameEngine/Runtime/Scene.h"
#include "../GameEngine/Runtime/SceneManager.h"
#include "../GameEngine/Runtime/Selectable.h"
#include "../GameEngine/Runtime/TextRenderer.h"
#include "../GameEngine/Runtime/Transform.h"
#include "../GameEngine/Runtime/UIEventSystem.h"
#include "../GameEngine/Runtime/UILayoutSystem.h"

#include "SelectableTests.h"
#include "TestSupport.h"

using GameEngine::Runtime::Button;
using GameEngine::Runtime::ComponentType;
using GameEngine::Runtime::Dropdown;
using GameEngine::Runtime::InputField;
using GameEngine::Runtime::RectTransform;
using GameEngine::Runtime::Selectable;
using TestSupport::Expect;

namespace
{

/// <summary>그 타입과 그 기반들이 요구하는 것 중에 그것이 있는지다.</summary>
[[nodiscard]] bool RequiresComponent(const ComponentType& type, const ComponentType& required)
{
    const std::vector<const ComponentType*> collected = type.CollectRequiredComponents();
    return std::find(collected.begin(), collected.end(), &required) != collected.end();
}

/// <summary>그 타입과 그 기반들이 선언한 속성 중에 그 이름이 있는지다.</summary>
[[nodiscard]] bool DeclaresProperty(const ComponentType& type, const std::string_view name)
{
    for (const GameEngine::Runtime::PropertyDescriptor* const descriptor :
         GameEngine::Runtime::CollectProperties(type))
    {
        if (descriptor && descriptor->GetName() == name)
        {
            return true;
        }
    }
    return false;
}

/// <summary>그 타입 <b>자신이</b> 선언한 속성 중에 그 이름이 있는지다. 기반의 것은 세지 않는다.</summary>
[[nodiscard]] bool DeclaresPropertyItself(const ComponentType& type, const std::string_view name)
{
    for (const GameEngine::Runtime::PropertyDescriptor& descriptor : type.GetOwnProperties())
    {
        if (descriptor.GetName() == name)
        {
            return true;
        }
    }
    return false;
}

/// <summary>
/// 자리를 가진 자식 오브젝트다. left가 0이면 화면 원점을 덮으며, 커서가 그 자리에 있다 —
/// 마우스 자리를 넣을 길이 Input에 없으므로 겹침을 재려면 원점에 놓아야 한다.
/// </summary>
GameEngine::Runtime::GameObject* AddPlacedChild(
    GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
    const char* const name, const float left = 0.0f)
{
    GameEngine::Runtime::GameObject* const object = scene.CreateGameObject(name);
    static_cast<void>(object->GetTransform().SetParent(&parent.GetTransform()));
    RectTransform* const rect = object->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ left, 0.0f });
    rect->SetOffsetMax({ left + 100.0f, 50.0f });
    return object;
}

}

bool RunSelectableTests()
{
    std::cout << "running selectable tests\n";
    bool passed = true;

    // ---- 선언이 한 곳에 있는가 ----
    //
    // 값을 세지 않고 관계를 센다: 셋 중 누구도 자기 자리에 적어 두지 않았는데 셋 모두가
    // 갖는다면, 그것을 주는 곳은 기반 하나뿐이다.
    const ComponentType* const concrete[]{
        &Button::StaticType(), &Dropdown::StaticType(), &InputField::StaticType() };
    for (const ComponentType* const type : concrete)
    {
        passed = Expect(
            type->IsDerivedFrom(Selectable::StaticType()),
            "each pointer element should be a Selectable") && passed;
        passed = Expect(
            RequiresComponent(*type, RectTransform::StaticType()),
            "each pointer element should still require a RectTransform") && passed;
        passed = Expect(
            type->GetOwnRequiredComponents().empty(),
            "the RectTransform requirement should be declared once, on the base") && passed;
        passed = Expect(
            DeclaresProperty(*type, "interactable"),
            "each pointer element should still carry the interactable property") && passed;
        passed = Expect(
            !DeclaresPropertyItself(*type, "interactable"),
            "the interactable property should be declared once, on the base") && passed;
        passed = Expect(
            type->IsCreatable(), "each pointer element should still be attachable") && passed;
    }
    passed = Expect(
        RequiresComponent(Selectable::StaticType(), RectTransform::StaticType()),
        "the base should be where the RectTransform requirement is written") && passed;

    // 기반은 이름일 뿐 붙일 수 있는 것이 아니다. 생성 훅이 없다는 것이 그 구분이며, Add
    // Component 목록과 장면 로더가 읽는 것이 이것이다.
    passed = Expect(
        !Selectable::StaticType().IsCreatable(),
        "the base itself should not be attachable") && passed;

    // ---- 규칙이 부류를 가리지 않는가 ----
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Scene;

    GameEngine::Runtime::ObjectRegistry registry;
    GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<Scene>(context, "Selectable");
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<GameEngine::Runtime::Canvas>());

    // 아래에 버튼, 그 위에 입력 필드. 계층 순서의 뒤가 위이므로 필드가 커서를 받는다. 커서를
    // 넣을 자리가 Input에 없으므로 자리는 원점이며, 두 사각형이 모두 그 자리를 덮는다.
    GameObject* const lowerObject = AddPlacedChild(*scene, *canvasObject, "Lower");
    Button* const lower = lowerObject->AddComponent<Button>();
    GameObject* const upperObject = AddPlacedChild(*scene, *canvasObject, "Upper");
    static_cast<void>(upperObject->AddComponent<GameEngine::Runtime::TextRenderer>());
    InputField* const upper = upperObject->AddComponent<InputField>();

    // 드롭다운은 겹침에 쓰지 않는다. 여기서 그것으로 재는 것은 끄는 순간의 정리다.
    GameObject* const listObject = AddPlacedChild(*scene, *canvasObject, "List", 200.0f);
    static_cast<void>(listObject->AddComponent<GameEngine::Runtime::TextRenderer>());
    Dropdown* const list = listObject->AddComponent<Dropdown>();
    list->SetOptions({ "One", "Two" });

    const unsigned int sceneId = sceneManager.AddScene(std::move(scene));
    if (!Expect(sceneId != 0 && lower && upper && list, "the selectable scene should assemble"))
    {
        return false;
    }

    const GameEngine::Runtime::UILayoutSystem layout;
    GameEngine::Runtime::UIEventSystem events;
    const auto step = [&layout, &events, &sceneManager, &input]()
    {
        layout.Synchronize(sceneManager, 800.0f, 600.0f);
        static_cast<void>(events.Synchronize(sceneManager, input));
    };

    step();
    passed = Expect(
        !lower->IsHovered(),
        "an element of another kind lying on top should take the cursor") && passed;

    // 받지 않게 된 요소는 커서를 삼키지 않는다. 서로 다른 두 부류에서 같은 규칙이
    // 지켜지는지 확인한다.
    upper->SetInteractable(false);
    step();
    passed = Expect(
        lower->IsHovered(),
        "a non-interactable element should let the one beneath it receive the cursor") && passed;

    // ---- 끄는 순간 저마다 쥔 것을 놓는가 ----
    //
    // 무엇을 놓아야 하는지는 부류가 안다. 기반이 하는 것은 "지금 놓아라"고 말하는 것뿐이며,
    // 그 말이 셋 모두에게 가는지를 여기서 본다.
    list->Open();
    passed = Expect(list->IsOpen(), "the list should be open before it is switched off") && passed;
    list->SetInteractable(false);
    passed = Expect(
        !list->IsOpen(),
        "a list that stops receiving input should not stay open, since nobody could close it")
        && passed;

    lower->SetInteractable(false);
    step();
    passed = Expect(
        !lower->IsHovered() && !lower->IsPressed(),
        "a button that stops receiving input should not stay lit") && passed;

    return passed;
}

static const TestSupport::Registration gSelectableTests{
    "RuntimeObject", "selectable tests should pass", RunSelectableTests };
