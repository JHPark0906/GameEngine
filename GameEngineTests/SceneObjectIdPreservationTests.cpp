#include <cstddef>
#include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Platform/IInput.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"

#include "SceneObjectIdPreservationTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::Input;
    using GameEngine::Runtime::ObjectRegistry;
    using GameEngine::Runtime::RuntimeContext;
    using GameEngine::Runtime::Scene;
    using GameEngine::Serialization::SceneSerializer;

    [[nodiscard]] std::unique_ptr<Scene> LoadScene(
        const std::string& text, RuntimeContext& runtimeContext)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(text.data()), text.size());
        return SceneSerializer::LoadFromBytes(bytes, "Test.scene", runtimeContext);
    }

    /// <summary>
    /// 편집 없는 왕복 — 열어서 곧바로 다시 저장하면 원문과 바이트 단위로 같아야 한다.
    ///
    /// 저장은 <c>SaveToText</c>가 그 자리에서 만든 텍스트 그대로를 「원문」으로 삼는다. 손으로
    /// 쓴 JSON을 원문으로 쓰면 공백·줄바꿈이 저장 형식과 달라 애초에 같을 수 없는 비교가 된다 —
    /// 재는 것은 「저장이 안정적인가」이지 「내가 저장 형식을 흉내 낼 수 있는가」가 아니다.
    /// </summary>
    [[nodiscard]] bool RunNoEditRoundTripTests()
    {
        ObjectRegistry registry;
        const Input input;
        RuntimeContext runtimeContext(registry, input);

        // id는 로드 순서와 반대로 정하고 1부터 시작하지 않게 한다. id가 우연히 1, 2, 3이면
        // 저장된 id를 무시하고 순위로 다시 매기는 구현도 같은 결과를 내므로 구별할 수 없다.
        const std::string source =
            R"({"sceneName": "RoundTrip", "gameObjects": [)"
            R"({"id": 77, "name": "Root", "isActive": true, "components": [{"type": "Transform"}]},)"
            R"({"id": 5, "name": "Child", "isActive": true, "parent": 77, "components": [)"
            R"({"type": "Transform"}]}]})";

        const std::unique_ptr<Scene> firstLoad = LoadScene(source, runtimeContext);
        if (!Expect(firstLoad != nullptr, "the fixture scene should load"))
        {
            return false;
        }
        const std::string original = SceneSerializer::SaveToText(*firstLoad);

        // 같은 그래프를 두 번 저장한 결과만 비교하면 생성 순서대로 ID를 다시 매기는 오류를 구분할 수 없다.
        // 첫 저장부터 입력 파일의 ID 77과 5가 그대로 유지되는지 확인한다.
        bool passed = Expect(
            original.find("\"id\": 77") != std::string::npos &&
                original.find("\"id\": 5,") != std::string::npos,
            "the first save should still carry the ids the file gave it");
        if (!passed)
        {
            std::cout << "  first save:\n" << original << "\n";
        }

        // 다른 세션이라면 이 레지스트리의 카운터는 이미 다른 값에서 시작했을 것이다. 그 차이를
        // 흉내 내려고, 다시 여는 사이에 무관한 오브젝트를 몇 개 만들어 인스턴스 id를 밀어 둔다
        // — 파일에서 읽은 오브젝트가 그 id를 그대로 돌려주는지, 인스턴스 id를 슬쩍 따라가지
        // 않는지를 이것으로 가른다.
        for (int i = 0; i < 5; ++i)
        {
            static_cast<void>(std::make_unique<GameObject>(runtimeContext, "Decoy"));
        }

        const std::unique_ptr<Scene> secondLoad = LoadScene(original, runtimeContext);
        if (!Expect(secondLoad != nullptr, "the re-saved text should itself load"))
        {
            return false;
        }
        const std::string resaved = SceneSerializer::SaveToText(*secondLoad);

        if (original != resaved)
        {
            std::cout << "  first save:\n" << original << "\n  second save:\n" << resaved << "\n";
        }
        passed = Expect(
            original == resaved,
            "loading and re-saving without edits should reproduce the same bytes") && passed;
        return passed;
    }

    /// <summary>부모-자식 연결이 셋 이상인 왕복에서도 각 관계가 보존되는지.</summary>
    [[nodiscard]] bool RunParentChildPreservationTests()
    {
        ObjectRegistry registry;
        const Input input;
        RuntimeContext runtimeContext(registry, input);

        // 세 오브젝트: Root(id 10) - Middle(id 20, parent 10) - Leaf(id 30, parent 20). id 값을
        // 인스턴스 id가 절대 우연히 맞힐 수 없는 숫자로 골라서, 저장이 실제로 저장된 id를 쓰지
        // 인스턴스 id를 쓰지 않는다는 것을 가른다.
        const std::string source =
            R"({"sceneName": "Hierarchy", "gameObjects": [)"
            R"({"id": 10, "name": "Root", "isActive": true, "components": [{"type": "Transform"}]},)"
            R"({"id": 20, "name": "Middle", "isActive": true, "parent": 10, "components": [)"
            R"({"type": "Transform"}]},)"
            R"({"id": 30, "name": "Leaf", "isActive": true, "parent": 20, "components": [)"
            R"({"type": "Transform"}]}]})";

        const std::unique_ptr<Scene> scene = LoadScene(source, runtimeContext);
        if (!Expect(scene != nullptr, "the hierarchy fixture should load"))
        {
            return false;
        }

        const std::string resaved = SceneSerializer::SaveToText(*scene);
        const std::unique_ptr<Scene> reloaded = LoadScene(resaved, runtimeContext);
        if (!Expect(reloaded != nullptr, "the re-saved hierarchy should itself load"))
        {
            return false;
        }

        GameObject* root = nullptr;
        GameObject* middle = nullptr;
        GameObject* leaf = nullptr;
        for (const auto& gameObject : reloaded->GetGameObjects() | std::views::values)
        {
            if (gameObject->GetName() == "Root") root = gameObject.get();
            if (gameObject->GetName() == "Middle") middle = gameObject.get();
            if (gameObject->GetName() == "Leaf") leaf = gameObject.get();
        }

        bool passed = Expect(
            root && middle && leaf, "all three named objects should survive the round trip");
        if (!passed)
        {
            return false;
        }
        passed = Expect(
            middle->GetTransform().GetParent() == &root->GetTransform(),
            "Middle should still be parented to Root") && passed;
        passed = Expect(
            leaf->GetTransform().GetParent() == &middle->GetTransform(),
            "and Leaf should still be parented to Middle") && passed;
        passed = Expect(
            root->GetSerializedId() == 10u && middle->GetSerializedId() == 20u &&
                leaf->GetSerializedId() == 30u,
            "and each object should still answer to the id the file gave it") && passed;
        return passed;
    }

    /// <summary>
    /// 새 오브젝트의 저장 id가 파일에서 읽은 id와 충돌하지 않는지 확인한다.
    /// 기존 오브젝트의 저장 id를 레지스트리가 다음에 발급할 인스턴스 id와 같게 둔다.
    /// GameObject와 필수 Transform이 번호를 하나씩 쓰므로 다음 GameObject는 두 번호 뒤다.
    /// 새 오브젝트를 만들고 저장해도 두 객체가 서로 다른 저장 id를 가져야 한다.
    /// </summary>
    [[nodiscard]] bool RunNewObjectIdCollisionAvoidanceTests()
    {
        ObjectRegistry registry;
        const Input input;
        RuntimeContext runtimeContext(registry, input);

        // "Loaded" GameObject 하나가 인스턴스 id 1을, 그 필수 Transform이 2를 받는다. 그러니 이
        // 레지스트리가 다음에 매길 GameObject는 3이다 — 그 번호를 "Loaded"의 저장 id로 미리
        // 박아 둔다.
        const std::string source =
            R"({"sceneName": "Collision", "gameObjects": [)"
            R"({"id": 3, "name": "Loaded", "isActive": true, "components": [{"type": "Transform"}]}]})";
        const std::unique_ptr<Scene> scene = LoadScene(source, runtimeContext);
        if (!Expect(scene != nullptr, "the collision fixture should load"))
        {
            return false;
        }

        GameObject* const newObject = scene->CreateGameObject("New");
        if (!Expect(newObject != nullptr, "a new object should be creatable in the scene"))
        {
            return false;
        }
        const bool collisionEngineered = newObject->GetInstanceId() == 3;
        if (!Expect(
                collisionEngineered,
                "the new object's live instance id should land exactly on the loaded id"))
        {
            std::cout << "  new object's instance id was " << newObject->GetInstanceId()
                      << ", not 3 as the test needs\n";
            return false;
        }

        const std::string saved = SceneSerializer::SaveToText(*scene);
        const std::unique_ptr<Scene> reloaded = LoadScene(saved, runtimeContext);
        bool passed = Expect(
            reloaded != nullptr,
            "the saved file should not contain a duplicate id and should reload");
        if (reloaded)
        {
            std::size_t objectCount = 0;
            for (const auto& gameObject : reloaded->GetGameObjects() | std::views::values)
            {
                static_cast<void>(gameObject);
                ++objectCount;
            }
            passed = Expect(
                objectCount == 2, "and both objects should have come back, none dropped") &&
                passed;
        }
        else
        {
            std::cout << "  saved text:\n" << saved << "\n";
        }
        return passed;
    }

    /// <summary>텍스트를 줄로 쪼갠다. 개행은 버린다.</summary>
    [[nodiscard]] std::vector<std::string_view> SplitLines(const std::string_view text)
    {
        std::vector<std::string_view> lines;
        std::size_t start = 0;
        while (start <= text.size())
        {
            const std::size_t newline = text.find('\n', start);
            if (newline == std::string_view::npos)
            {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, newline - start));
            start = newline + 1;
        }
        return lines;
    }

    /// <summary>
    /// 오브젝트 하나만 편집하면 저장 결과의 해당 오브젝트 줄만 달라져야 한다.
    /// SaveToText는 id·name·isActive·parent·components를 오브젝트별 한 줄에 기록하므로
    /// 관련 없는 오브젝트의 ID나 내용이 바뀌지 않는지 줄 단위로 비교한다.
    /// </summary>
    [[nodiscard]] bool RunSingleEditDiffStabilityTests()
    {
        ObjectRegistry registry;
        const Input input;
        RuntimeContext runtimeContext(registry, input);

        const std::string source =
            // id를 창조 순서(A,B,C)와 다른 등수로 고른다 — 저장이 저장된 id를 무시하고 그냥
            // 창조 순서로 다시 매겨도, 셋 다 rank 1,2,3으로 우연히 원래 값과 같아지면 이 시험도
            // (그리고 아래 "before"/"after" 안 id 확인도) 그 결함을 놓친다.
            R"({"sceneName": "DiffStability", "gameObjects": [)"
            R"({"id": 50, "name": "A", "isActive": true, "components": [{"type": "Transform"}]},)"
            R"({"id": 8, "name": "B", "isActive": true, "components": [{"type": "Transform"}]},)"
            R"({"id": 23, "name": "C", "isActive": true, "components": [{"type": "Transform"}]}]})";

        const std::unique_ptr<Scene> firstLoad = LoadScene(source, runtimeContext);
        if (!Expect(firstLoad != nullptr, "the diff-stability fixture should load"))
        {
            return false;
        }
        const std::string before = SceneSerializer::SaveToText(*firstLoad);
        bool idsSurviveTheFirstSave =
            before.find("\"id\": 50") != std::string::npos &&
            before.find("\"id\": 8,") != std::string::npos &&
            before.find("\"id\": 23") != std::string::npos;
        if (!Expect(idsSurviveTheFirstSave, "the ids the file gave should survive the first save"))
        {
            std::cout << "  before:\n" << before << "\n";
            return false;
        }

        const std::unique_ptr<Scene> editLoad = LoadScene(before, runtimeContext);
        if (!Expect(editLoad != nullptr, "the saved fixture should itself reload for editing"))
        {
            return false;
        }
        GameObject* moved = nullptr;
        for (const auto& gameObject : editLoad->GetGameObjects() | std::views::values)
        {
            if (gameObject->GetName() == "B") moved = gameObject.get();
        }
        if (!Expect(moved != nullptr, "the object to move should be present after reload"))
        {
            return false;
        }
        // 진짜 편집: 위치를 실제로 옮긴다. 이름을 바꾸거나 다시 만드는 것이 아니라, 사람이 씬
        // 뷰에서 오브젝트를 끌 때 일어나는 바로 그 변화다.
        moved->GetTransform().SetPosition({ 3.0f, 0.0f, 0.0f });
        const std::string after = SceneSerializer::SaveToText(*editLoad);
        bool passed = Expect(
            after.find("\"id\": 50") != std::string::npos &&
                after.find("\"id\": 8,") != std::string::npos &&
                after.find("\"id\": 23") != std::string::npos,
            "and moving one object should not renumber any of them, including itself");
        if (!passed)
        {
            std::cout << "  after:\n" << after << "\n";
        }

        const std::vector<std::string_view> beforeLines = SplitLines(before);
        const std::vector<std::string_view> afterLines = SplitLines(after);
        const bool sameLineCount = Expect(
            beforeLines.size() == afterLines.size(),
            "moving one object should not add or remove any line");
        passed = sameLineCount && passed;
        if (!sameLineCount)
        {
            return false;
        }

        std::size_t changedLines = 0;
        std::size_t changedIndex = 0;
        for (std::size_t i = 0; i < beforeLines.size(); ++i)
        {
            if (beforeLines[i] != afterLines[i])
            {
                ++changedLines;
                changedIndex = i;
            }
        }
        if (changedLines != 1)
        {
            std::cout << "  " << changedLines << " line(s) changed, expected exactly 1\n";
            for (std::size_t i = 0; i < beforeLines.size(); ++i)
            {
                if (beforeLines[i] != afterLines[i])
                {
                    std::cout << "  - " << beforeLines[i] << "\n  + " << afterLines[i] << "\n";
                }
            }
        }
        passed = Expect(
            changedLines == 1,
            "moving one object should change exactly one line, the way git diff would show it") &&
            passed;
        if (changedLines == 1)
        {
            passed = Expect(
                afterLines[changedIndex].find("\"name\": \"B\"") != std::string_view::npos,
                "and that one line should be the moved object's own line") && passed;
        }
        return passed;
    }
}

bool RunSceneObjectIdPreservationTests()
{
    std::cout << "running scene object id preservation tests\n";
    // 표는 프로세스 전역이다. RegistryScope로 감싸는 이유는 되돌리기 위해서다 — 그러지 않으면
    // 이 등록이 이 프로세스에 계속 남아, 「이 프로세스는 이 타입을 모른다」를 전제로 하는 다른
    // 시험(예: 게임 컴포넌트가 PreservedComponent로 보존되는지 재는 시험)이 이 시험 뒤에 돌 때
    // 더는 참이 아닌 전제 위에서 돌게 된다.
    //
    // 장면 로더가 "Transform" 같은 실제 컴포넌트 타입을 만들려면 이 등록이 있어야 한다. 없으면
    // 모든 컴포넌트가 이 프로세스가 모르는 타입으로 취급되어 PreservedComponent로 보존되고,
    // 그것도 자기 인스턴스 id를 하나 더 쓰므로 이 파일이 재는 번호 계산이 전부 틀어진다.
    const TestSupport::RegistryScope registries;
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    bool passed = RunNoEditRoundTripTests();
    passed = RunParentChildPreservationTests() && passed;
    passed = RunNewObjectIdCollisionAvoidanceTests() && passed;
    passed = RunSingleEditDiffStabilityTests() && passed;
    return passed;
}

static const TestSupport::Registration gSceneObjectIdPreservationTests{
    "RuntimeObject", "a scene save should not renumber the objects it already had ids for",
    RunSceneObjectIdPreservationTests };
