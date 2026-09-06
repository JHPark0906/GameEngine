#include "UiRequirementTests.h"

#include <iostream>
#include <memory>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "Diagnostics/Debug.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/ComponentType.h"
#include "Runtime/ContentFit.h"
#include "Runtime/Dropdown.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/InputField.h"
#include "Runtime/LayoutElement.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectMask.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/ScrollRect.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "TestSupport.h"
#include "Serialization/SceneSerializer.h"

using TestSupport::Expect;

namespace
{

    /// <summary>이 타입이 사각형을 요구한다고 선언했는지다.</summary>
    [[nodiscard]] bool RequiresRect(const GameEngine::Runtime::ComponentType& type)
    {
        for (const GameEngine::Runtime::ComponentType* const required :
            type.CollectRequiredComponents())
        {
            if (required == &GameEngine::Runtime::RectTransform::StaticType())
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>돌아가는 동안 기록된 경고들을 모은다.</summary>
    class WarningCollector final
    {
    public:
        WarningCollector()
            : mId(GameEngine::Diagnostics::Debug::AddLogListener(
                  [this](const GameEngine::Diagnostics::LogEntry& entry)
                  {
                      if (entry.level == GameEngine::Diagnostics::LogLevel::Warning)
                      {
                          mWarnings.push_back(entry.message);
                      }
                  }))
        {
        }

        ~WarningCollector() { GameEngine::Diagnostics::Debug::RemoveLogListener(mId); }

        WarningCollector(const WarningCollector&) = delete;
        WarningCollector& operator=(const WarningCollector&) = delete;

        [[nodiscard]] std::size_t CountContaining(const std::string& text) const
        {
            std::size_t count = 0;
            for (const std::string& warning : mWarnings)
            {
                if (warning.find(text) != std::string::npos)
                {
                    ++count;
                }
            }
            return count;
        }

    private:
        std::vector<std::string> mWarnings;
        GameEngine::Diagnostics::Debug::LogListenerId mId;
    };
}

bool RunComponentRequirementTests()
{
    using namespace GameEngine::Runtime;

    std::cout << "running component requirement tests\n";

    // 사각형 없이는 아무 일도 하지 않는 것들이다. 배치가 사각형으로 요소를 찾고, 히트 테스트도
    // 사각형이 있는 것만 후보로 세므로, 사각형이 없으면 소리 없이 죽는다.
    struct Named
    {
        const char* name;
        const ComponentType* type;
    };
    const Named requiring[]{
        { "Button", &Button::StaticType() },
        { "Dropdown", &Dropdown::StaticType() },
        { "InputField", &InputField::StaticType() },
        { "ScrollRect", &ScrollRect::StaticType() },
        { "RectMask", &RectMask::StaticType() },
        { "ContentFit", &ContentFit::StaticType() },
        { "LayoutElement", &LayoutElement::StaticType() },
    };
    bool passed = true;
    for (const Named& one : requiring)
    {
        std::cout << "  " << one.name << " requires a rectangle: "
                  << (RequiresRect(*one.type) ? "yes" : "no") << "\n";
        passed = Expect(RequiresRect(*one.type), "a screen UI component should require a rectangle")
            && passed;
    }

    // 사각형이 필요 없는 것들이다. 이들에게까지 붙이면 월드에 놓인 스프라이트마다 화면 사각형이
    // 생긴다 — 고침이 아니라 새 결함이다.
    const Named notRequiring[]{
        { "SpriteRenderer", &SpriteRenderer::StaticType() },
        { "TextRenderer", &TextRenderer::StaticType() },
        // Canvas는 특히 아니다. 캔버스에 사각형이 붙으면 그것이 뿌리 사각형이 되고, 기본
        // 크기로 서면 그 아래가 전부 잘린다.
        { "Canvas", &Canvas::StaticType() },
        { "Transform", &Transform::StaticType() },
    };
    for (const Named& one : notRequiring)
    {
        passed =
            Expect(!RequiresRect(*one.type), "a component that works without a rectangle should"
                " not demand one") && passed;
    }

    return passed;
}

bool RunCanvaslessScreenUiWarningTests()
{
    using namespace GameEngine::Runtime;

    std::cout << "running canvasless screen UI warning tests\n";

    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    ObjectRegistry registry;
    Input input;
    RuntimeContext context{ registry, input };

    // RectTransform은 있지만 배치를 시작할 Canvas 조상이 없는 계층이다.
    const std::string canvasless =
        R"({"sceneName": "Canvasless", "gameObjects": [)"
        R"({"id": 1, "name": "Stranded", "isActive": true, "components": [)"
        R"({"type": "Transform", "position": [0, 0, 0], "rotation": [0, 0, 0],)"
        R"( "rotationUnit": "degrees", "rotationOrder": "rollPitchYaw", "scale": [1, 1, 1]},)"
        R"({"type": "RectTransform"}]}]})";

    std::size_t warnings = 0;
    {
        const WarningCollector collector;
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(canvasless.data()), canvasless.size());
        const std::unique_ptr<Scene> scene =
            GameEngine::Serialization::SceneSerializer::LoadFromBytes(
                bytes, "Canvasless.scene", context);
        warnings = collector.CountContaining("Stranded");
    }
    std::cout << "  a rectangle with no canvas above it warned " << warnings << " time(s)\n";
    bool passed =
        Expect(warnings == 1, "a screen rectangle with no canvas above it should warn once");

    // 제대로 세운 장면은 조용해야 한다. 늘 경고가 나오면 아무도 읽지 않게 되고, 그러면 이
    // 경고는 없는 것과 같다.
    const std::string proper =
        R"({"sceneName": "Proper", "gameObjects": [)"
        R"({"id": 1, "name": "Root", "isActive": true, "components": [)"
        R"({"type": "Transform", "position": [0, 0, 0], "rotation": [0, 0, 0],)"
        R"( "rotationUnit": "degrees", "rotationOrder": "rollPitchYaw", "scale": [1, 1, 1]},)"
        R"({"type": "Canvas"}]},)"
        R"({"id": 2, "name": "Placed", "isActive": true, "parent": 1, "components": [)"
        R"({"type": "Transform", "position": [0, 0, 0], "rotation": [0, 0, 0],)"
        R"( "rotationUnit": "degrees", "rotationOrder": "rollPitchYaw", "scale": [1, 1, 1]},)"
        R"({"type": "RectTransform"}]}]})";

    std::size_t quietWarnings = 0;
    {
        const WarningCollector collector;
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(proper.data()), proper.size());
        const std::unique_ptr<Scene> scene =
            GameEngine::Serialization::SceneSerializer::LoadFromBytes(
                bytes, "Proper.scene", context);
        quietWarnings = collector.CountContaining("Placed");
    }
    std::cout << "  a rectangle under a canvas warned " << quietWarnings << " time(s)\n";
    passed = Expect(
        quietWarnings == 0, "a rectangle under a canvas should not warn") && passed;

    return passed;
}

bool RunAddComponentRequirementTests()
{
    using namespace GameEngine::Runtime;

    std::cout << "running add component requirement tests\n";

    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    TestSupport::TemporaryDirectory temporaryDirectory("ui-requires-rect");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = root / "UiRequirementTest.gameproject";
    const bool wrote =
        TestSupport::WriteFile(projectFile,
            R"({"projectName": "UiRequirementTest",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/First.scene" } ]})") &&
        TestSupport::WriteFile(root / "Scenes" / "First.scene",
            R"({"sceneName": "First", "gameObjects": []})");
    if (!Expect(wrote, "the requirement test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the requirement test project should open") ||
        !Expect(context.HasOpenScene(), "opening the project should open its scene"))
    {
        return false;
    }

    auto create = std::make_unique<GameEditor::CreateGameObjectCommand>(context, "Target");
    bool passed = Expect(create->Apply(), "creating the target object should succeed");
    context.RecordEdit(std::move(create));

    Scene* const scene = context.GetOpenScene();
    if (!scene)
    {
        return Expect(false, "the scene should be open") && passed;
    }
    GameObject* target = nullptr;
    for (GameObject* const object : scene->GetRootGameObjects())
    {
        if (object && object->GetName() == "Target")
        {
            target = object;
        }
    }
    if (!target)
    {
        return Expect(false, "the target object should be in the scene") && passed;
    }

    const unsigned int targetId = target->GetInstanceId();
    auto add = std::make_unique<GameEditor::AddComponentCommand>(context, targetId, "Button");
    passed = Expect(add->Apply(), "adding a Button should succeed") && passed;

    const bool hasButton = target->GetComponent<Button>() != nullptr;
    const bool hasRect = target->GetComponent<RectTransform>() != nullptr;
    std::cout << "  after adding a Button: Button " << (hasButton ? "yes" : "no")
              << ", RectTransform " << (hasRect ? "yes" : "no") << "\n";
    passed = Expect(hasButton, "the Button should be there") && passed;
    // 사각형이 함께 붙어야 한다. 붙지 않으면 사람은 눌리지 않는 버튼을 만들고 그 사실을 모른다.
    passed = Expect(hasRect, "the rectangle the Button needs should come with it") && passed;

    passed = Expect(add->Revert(), "undoing the add should succeed") && passed;
    const bool buttonGone = target->GetComponent<Button>() == nullptr;
    const bool rectGone = target->GetComponent<RectTransform>() == nullptr;
    std::cout << "  after undo: Button " << (buttonGone ? "gone" : "still there")
              << ", RectTransform " << (rectGone ? "gone" : "still there") << "\n";
    passed = Expect(buttonGone, "undo should take the Button back") && passed;
    // 되돌리기가 절반만 되돌리면 사람이 놓지 않은 컴포넌트가 남는다.
    passed = Expect(rectGone, "undo should also take back what came with it") && passed;

    // 사람이 이미 붙여 둔 사각형은 되돌리기가 걷지 않는다 — 그것은 이 추가가 놓은 것이 아니다.
    RectTransform* const mine = target->AddComponent<RectTransform>();
    passed = Expect(mine != nullptr, "the person should be able to place a rectangle first") &&
        passed;
    auto addAgain = std::make_unique<GameEditor::AddComponentCommand>(context, targetId, "Button");
    passed = Expect(addAgain->Apply(), "adding a Button beside an existing rectangle should"
        " succeed") && passed;
    passed = Expect(addAgain->Revert(), "undoing that add should succeed") && passed;
    const bool mineSurvived = target->GetComponent<RectTransform>() != nullptr;
    std::cout << "  a rectangle the person placed first survived undo: "
              << (mineSurvived ? "yes" : "no") << "\n";
    passed = Expect(
        mineSurvived, "undo should not take back a component the person placed itself") && passed;

    return passed;
}

static const TestSupport::Registration gComponentRequirementTests{
    "UIEvent", "component requirement tests should pass", RunComponentRequirementTests };

static const TestSupport::Registration gCanvaslessScreenUiWarningTests{
    "UIEvent", "canvasless screen UI warning tests should pass", RunCanvaslessScreenUiWarningTests };

static const TestSupport::Registration gAddComponentRequirementTests{
    "UIEvent", "add component requirement tests should pass", RunAddComponentRequirementTests };
