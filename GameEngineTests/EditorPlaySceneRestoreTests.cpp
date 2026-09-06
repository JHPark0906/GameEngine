#include "EditorPlaySceneRestoreTests.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "App/ProjectFile.h"
#include "Document/EditorSceneDocument.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

namespace
{
    struct StopCallbackState
    {
        GameEngine::Runtime::Game* game = nullptr;
        bool armed = false;
        int calls = 0;
        bool onlyRestoredVisible = false;
        bool mutationsRejected = false;
    };

    class StopCallbackProbe final : public GameEngine::Runtime::MonoBehaviour
    {
    public:
        static const GameEngine::Runtime::ComponentType& StaticType()
        {
            static const GameEngine::Runtime::ComponentType type{
                "StopCallbackProbe", &MonoBehaviour::StaticType() };
            return type;
        }
        const GameEngine::Runtime::ComponentType& GetComponentType() const override { return StaticType(); }
        std::shared_ptr<StopCallbackState> state;

    protected:
        void OnDestroy() override
        {
            if (!state || !state->armed)
            {
                return;
            }
            ++state->calls;
            auto& game = *state->game;
            auto& manager = game.GetSceneManager();
            unsigned int restoredId = 0;
            for (const auto& [id, scene] : manager.GetActiveScenes())
            {
                if (scene->FindGameObject("Unsaved edit"))
                {
                    restoredId = id;
                }
            }
            state->onlyRestoredVisible = restoredId != 0 && manager.GetActiveScenes().size() == 1;
            const bool loadRejected = game.LoadScene(1) == GameEngine::Runtime::SceneLoadResult::Failed;
            const bool unloadRejected = !game.UnloadScene(restoredId);
            const bool addRejected = game.AddScene(std::make_unique<GameEngine::Runtime::Scene>(
                game.GetRuntimeContext(), "Callback-created scene")) == 0;
            const bool nestedRejected = !game.RetainOnlyScene(restoredId);
            state->mutationsRejected = loadRejected && unloadRejected && addRejected && nestedRejected;
        }
    };
}

bool RunEditorPlaySceneRestoreTests()
{
    using namespace GameEngine;
    using TestSupport::Expect;
    TestSupport::TemporaryDirectory directory("editor-play-scene-restore");
    const auto& root = directory.GetPath();
    if (!Expect(
        TestSupport::WriteFile(root / "Restore.gameproject",
            R"({"projectName":"Restore","initialSceneId":0,"window":{"width":320,"height":240},)"
            R"("scenes":[{"id":0,"path":"Scenes/First.scene"},{"id":1,"path":"Scenes/Second.scene"}]})") &&
        TestSupport::WriteFile(root / "Scenes/First.scene",
            R"({"sceneName":"First","gameObjects":[]})") &&
        TestSupport::WriteFile(root / "Scenes/Second.scene",
            R"({"sceneName":"Second","gameObjects":[]})"),
        "play restore scene fixtures should be written"))
    {
        return false;
    }
    static_cast<void>(Serialization::RegisterRuntimeComponentFactories());
    Platform::DirectoryContentSource source(root);
    Runtime::Game game(nullptr, nullptr);
    game.SetSceneLoader(Serialization::SceneSerializer::MakeSceneLoader());
    App::ProjectFileData project;
    project.filePath = root / "Restore.gameproject";
    project.settings.scenePaths = { { 0, "Scenes/First.scene" }, { 1, "Scenes/Second.scene" } };
    if (!Expect(game.Initialize(source, project.settings.scenePaths), "play runtime should initialize"))
    {
        return false;
    }
    GameEditor::EditorSceneDocument document;
    const GameEditor::EditorSceneDocument::ProjectHandles handles{ &project, &game, &source };
    if (!Expect(document.OpenScene(handles, 0), "the edit scene should open"))
    {
        return false;
    }
    auto* const editMarker = document.GetOpenScene(handles)->CreateGameObject("Unsaved edit");
    if (!Expect(editMarker != nullptr, "the unsaved edit marker should be created"))
    {
        return false;
    }
    editMarker->GetTransform().SetPosition({ 3.0f, 4.0f, 5.0f });
    document.MarkEdited();

    // 문서 밖에서 켠 장면은 Play 진입 실패로 보존한다.
    bool passed = Expect(game.LoadScene(1) == Runtime::SceneLoadResult::Loaded,
        "the extra edit-time scene should load");
    passed &= Expect(!game.RetainOnlyScene(123456) &&
        game.GetSceneManager().GetActiveScenes().size() == 2,
        "retaining a missing scene must not discard any active scene");
    passed &= Expect(!document.EnterPlayMode(handles) && !document.IsPlaying() &&
        game.GetSceneManager().GetActiveScenes().size() == 2,
        "play should reject multiple edit-time scenes without discarding them");
    passed &= Expect(game.UnloadScene(1), "the edit-time extra scene should unload");

    // 원본 유지와 원본 언로드를 각각 왕복하며 다음 Play의 같은 장면 로드까지 확인한다.
    for (const bool unloadOriginal : { false, true, false })
    {
        if (!Expect(document.EnterPlayMode(handles), "play should start with one edit scene"))
        {
            return false;
        }
        auto* const played = document.GetOpenScene(handles);
        auto* const playedMarker = played->FindGameObject("Unsaved edit");
        if (!Expect(playedMarker != nullptr, "each play session should start with the edit marker"))
        {
            return false;
        }
        playedMarker->SetName("Runtime mutation");
        playedMarker->GetTransform().SetPosition({ 30.0f, 40.0f, 50.0f });
        auto* const transient = played->CreateGameObject("Transient");
        if (!Expect(transient != nullptr, "the runtime object should be created"))
        {
            return false;
        }
        const unsigned int transientId = transient->GetInstanceId();
        if (!Expect(game.LoadScene(1) == Runtime::SceneLoadResult::Loaded,
            "the same registered scene should load on every play session"))
        {
            return false;
        }
        auto* const extraObject = game.GetSceneManager().GetScene(1)->CreateGameObject("Extra");
        if (!Expect(extraObject != nullptr, "the additional scene should create an object"))
        {
            return false;
        }
        const unsigned int extraId = extraObject->GetInstanceId();
        const auto callbackState = std::make_shared<StopCallbackState>();
        callbackState->game = &game;
        extraObject->AddComponent<StopCallbackProbe>()->state = callbackState;
        if (unloadOriginal)
        {
            unsigned int originalId = 0;
            for (const auto& [sceneId, scene] : game.GetSceneManager().GetActiveScenes())
            {
                if (scene.get() == played)
                {
                    originalId = sceneId;
                }
            }
            passed &= Expect(originalId != 0 && game.UnloadScene(originalId),
                "play may unload the original edit scene");
        }

        const GameEditor::EditorSceneDocument::ProjectHandles invalidHandles{ nullptr, &game, &source };
        passed &= Expect(!document.ExitPlayMode(invalidHandles) && document.IsPlaying(),
            "a failed stop must preserve the active session for retry");
        const auto recovery = document.SaveRecoverySnapshot(handles, root / "Recovery");
        passed &= Expect(!recovery.empty() &&
            TestSupport::ReadFile(recovery).find("Unsaved edit") != std::string::npos,
            "a failed stop must preserve the original editable recovery snapshot");
        callbackState->armed = true;
        const bool stopped = document.ExitPlayMode(handles);
        callbackState->armed = false;
        if (!Expect(stopped, "stop should retry and restore the edit scene"))
        {
            return false;
        }
        const auto* const restored = document.GetOpenScene(handles);
        const auto* const restoredMarker = restored ? restored->FindGameObject("Unsaved edit") : nullptr;
        passed &= Expect(!document.IsPlaying() && document.HasUnsavedChanges() &&
            restored && restored->GetName() == "First" && restoredMarker &&
            restoredMarker->GetTransform().GetPosition() == Math::Vector3{ 3.0f, 4.0f, 5.0f },
            "stop should restore the unsaved edit snapshot and leave edit mode active");
        passed &= Expect(game.GetSceneManager().GetActiveScenes().size() == 1 &&
            !game.GetSceneManager().IsSceneLoaded(1) &&
            !game.FindObject(transientId) && !game.FindObject(extraId),
            "stop should remove every played scene and its runtime objects");
        passed &= Expect(callbackState->calls == 1 && callbackState->onlyRestoredVisible &&
            callbackState->mutationsRejected,
            "stop callbacks must see only the restored scene and cannot load, add, unload, or retain scenes");
    }
    return passed;
}

static const TestSupport::Registration gEditorPlaySceneRestore{
    "EditorDocument", "Play restores the single edit scene after additive loads and unloads",
    RunEditorPlaySceneRestoreTests };
