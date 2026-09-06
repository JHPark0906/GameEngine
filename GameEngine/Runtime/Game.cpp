#include "pch.h"
#include "Game.h"

#include "../Assets/AssetDatabase.h"
#include "../Platform/IAudioOutput.h"
#include "../Platform/ITextMeasure.h"
#include "../Platform/IClipboard.h"
#include "../Platform/IContentSource.h"
#include "../Platform/PlatformServices.h"
#include "AudioSystem.h"
#include "Physics2DSystem.h"
#include "Physics3DSystem.h"
#include "UILayoutSystem.h"

#include "UIEventSystem.h"
#include "Camera.h"
#include "Input.h"
#include "RuntimeContext.h"
#include "Object.h"
#include "ObjectRegistry.h"
#include "Scene.h"
#include "SceneManager.h"
#include "../Diagnostics/Debug.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{

Game::Game(
    std::unique_ptr<Platform::IAudioOutput> audioOutput,
    std::unique_ptr<Platform::ITextMeasure> textMeasure)
{
    mAssetDatabase = std::make_unique<Assets::AssetDatabase>();
    mInput = std::make_unique<Input>();
    mObjectRegistry = std::make_unique<ObjectRegistry>();
    // What a component is allowed to reach, assembled once from what this owns.
    mRuntimeContext = std::make_unique<RuntimeContext>(*mObjectRegistry, *mInput);
    mSceneManager = std::make_unique<SceneManager>(*mRuntimeContext);
    // 출력 초기화는 첫 재생까지 미뤄지므로, 소리 낼 일 없는 런타임 — 에디터 UI — 은 오디오
    // 장치를 열지 않는다.
    mAudioSystem = std::make_unique<AudioSystem>(std::move(audioOutput));
    // 글자를 재는 잣대는 런타임이 소유하고 시스템들이 빌려 쓴다. 배치가 먼저 쓰고, 글자를
    // 다루는 다음 시스템도 같은 것을 받는다 — 잣대가 둘이면 캐럿이 그려진 글자와 어긋난다.
    mTextMeasure = std::move(textMeasure);
    mUILayoutSystem = std::make_unique<UILayoutSystem>();
    mPhysics2DSystem = std::make_unique<Physics2DSystem>();
    mPhysics3DSystem = std::make_unique<Physics3DSystem>();
    // 텍스트 필드가 복사·붙여넣기를 하려면 클립보드가 필요하다. 플랫폼 설비이므로 그 자리의
    // 팩토리에서 받는다 — 구체 구현을 이름 부르지 않는다.
    mUIEventSystem =
        std::make_unique<UIEventSystem>(Platform::PlatformServices::CreateClipboard());
}

Game::~Game()
{
    // 재생 중인 보이스는 장면의 클립 참조 위에서 산다. 장면보다 먼저 거둔다.
    mAudioSystem.reset();
    // Scenes own the objects registered with mObjectRegistry, so they must go first.
    mSceneManager.reset();
}

Object* Game::FindObject(const unsigned int instanceId) const
{
    return mObjectRegistry->FindObject(instanceId);
}

bool Game::Initialize(
    const Platform::IContentSource& projectContent,
    const std::unordered_map<unsigned int,
    std::filesystem::path>& scenePaths)
{
    if (!LoadAssets(projectContent))
    {
        return false;
    }

    if (!mSceneManager->RegisterScenePaths(mAssetDatabase->GetContentSource(), scenePaths))
    {
        return false;
    }

    return true;
}

void Game::SetSceneLoader(SceneLoader loader)
{
    mSceneManager->SetSceneLoader(std::move(loader));
}

void Game::Update(const float deltaTime)
{
    mSceneManager->Update(deltaTime);

    // A load requested during the update was queued and performed just now, inside
    // SceneManager::Update, where the immediate path's pre-load cannot see it. Pre-loading here —
    // still outside any frame's rendering — keeps the promise that a frame never has to import.
    for (const unsigned int sceneId : mSceneManager->TakeScenesLoadedDuringUpdate())
    {
        if (const Scene* const scene = mSceneManager->GetScene(sceneId))
        {
            LoadSceneAssets(*scene);
        }
    }

    // Swept rather than done once when a scene unloads, because at that moment a backend is usually
    // still holding what the scene was drawing: its resolved resources are retired only after they
    // have gone unused for a while. Unloading once would therefore reclaim nothing and never look
    // again. The sweep itself is a walk of the loaded scenes and a map scan, so it is cheap enough
    // to do on a timer and rare enough not to matter.
    mUnusedAssetSweepTime += deltaTime;
    if (mUnusedAssetSweepTime >= UnusedAssetSweepInterval)
    {
        mUnusedAssetSweepTime = 0.0f;
        UnloadUnusedAssets();
    }

    // 일반 Behaviour의 Update가 정한 입력·속도를 고정 스텝 물리가 소비한다. 물리가 위치를
    // 바꾸므로, 뒤의 소리와 렌더링 계층은 해소된 최종 Transform을 읽는다. 두 물리 세계는
    // 콜라이더를 섞지 않고 차례로 최종 overlap Enter/Exit를 갱신한다.
    mPhysics2DSystem->Simulate(*mSceneManager, deltaTime);
    mPhysics3DSystem->Simulate(*mSceneManager, deltaTime);

    // 업데이트와 물리가 끝난 상태가 이번 프레임의 진실이다. 그 상태를 소리에 맞춘다.
    mAudioSystem->Synchronize(*mSceneManager, *mAssetDatabase);

    // 배치도 같은 이유로 여기다: 이번 프레임에 UI 계층이 어떻게 바뀌었든, 그리기가 읽는 것은
    // 업데이트가 끝난 뒤의 사각형이어야 한다.
    if (mTextMeasure)
    {
        // 잣대가 결과를 보관한다면 그 수명이 여기서 한 칸 넘어간다. 프레임을 소유한 쪽이
        // 프레임의 시작을 알리는 것이므로 이 자리다.
        mTextMeasure->BeginFrame();
    }
    mUILayoutSystem->Synchronize(
        *mSceneManager, mRenderSurfaceWidth, mRenderSurfaceHeight, mTextMeasure.get());
    // 반응은 배치 바로 뒤다: 커서를 맞힐 사각형이 있어야 하고, 그 사각형은 방금 계산됐다.
    // 한 프레임 늦은 자리로 클릭을 판정하면 눈에 보이는 것과 눌리는 것이 어긋난다.
    mUIConsumedPointer = mUIEventSystem->Synchronize(*mSceneManager, *mInput, mTextMeasure.get(), deltaTime);
}

SceneLoadResult Game::LoadScene(unsigned int sceneId)
{
    const SceneLoadResult result = mSceneManager->LoadScene(sceneId);
    if (result == SceneLoadResult::Loaded)
    {
        if (const Scene* const scene = mSceneManager->GetScene(sceneId))
        {
            LoadSceneAssets(*scene);
        }
    }
    return result;
}

unsigned int Game::AddScene(std::unique_ptr<Scene> scene)
{
    const unsigned int sceneId = mSceneManager->AddScene(std::move(scene));
    if (sceneId != 0)
    {
        if (const Scene* const added = mSceneManager->GetScene(sceneId))
        {
            LoadSceneAssets(*added);
        }
    }
    return sceneId;
}

void Game::UnloadUnusedAssets()
{
    // What every loaded scene references, which is the set worth keeping. Recomputed rather than
    // tracked: a scene's objects and their references change while it runs, and a count kept up to
    // date through every one of those changes is a count that will eventually be wrong.
    std::unordered_set<Assets::AssetKey> referenced;
    std::vector<Assets::AssetReference> references;
    for (const auto& scene : mSceneManager->GetActiveScenes() | std::views::values)
    {
        for (const auto& gameObject : scene->GetGameObjects() | std::views::values)
        {
            for (const std::unique_ptr<Component>& component : gameObject->GetAllComponents())
            {
                component->CollectAssetReferences(references);
            }
        }
    }
    for (const Assets::AssetReference& reference : references)
    {
        if (const Assets::Asset* const asset = mAssetDatabase->FindAsset(reference))
        {
            referenced.insert(asset->GetId());
        }
    }

    mAssetDatabase->UnloadUnreferenced(referenced);
}

void Game::LoadSceneAssets(const Scene& scene)
{
    // Loaded here rather than when a frame first draws them. The frame is the wrong place to read
    // and parse a model: it is the one place with a deadline.
    std::vector<Assets::AssetReference> references;
    for (const auto& gameObject : scene.GetGameObjects() | std::views::values)
    {
        for (const std::unique_ptr<Component>& component : gameObject->GetAllComponents())
        {
            component->CollectAssetReferences(references);
        }
    }

    std::size_t loaded = 0;
    for (const Assets::AssetReference& reference : references)
    {
        const Assets::Asset* const asset = mAssetDatabase->FindAsset(reference);
        if (!asset)
        {
            continue;
        }
        // Which of the two to load is the asset's own answer; asking for the wrong one costs a
        // lookup and returns nothing.
        switch (asset->GetType())
        {
        case Assets::AssetType::Mesh:
            loaded += mAssetDatabase->LoadMesh(reference) != nullptr ? 1 : 0;
            break;
        case Assets::AssetType::Sprite:
            loaded += mAssetDatabase->LoadTexture(reference) != nullptr ? 1 : 0;
            break;
        case Assets::AssetType::AudioClip:
            loaded += mAssetDatabase->LoadAudioClip(reference) != nullptr ? 1 : 0;
            break;
        default:
            break;
        }
    }

    Diagnostics::Debug::Log(
        "Loaded a scene's assets. scene=", scene.GetName(),
        ", referenced=", references.size(), ", loaded=", loaded);
}

bool Game::UnloadScene(unsigned int sceneId)
{
    const bool unloaded = mSceneManager->UnloadScene(sceneId);
    if (unloaded)
    {
        // 내려간 장면의 소리는 지금 멎어야 한다. 다음 Update의 동기화에 맡기면, 업데이트가 더는
        // 돌지 않는 호출자 — Play를 끝낸 에디터 — 에서 반복 재생이 영원히 남는다.
        mAudioSystem->Synchronize(*mSceneManager, *mAssetDatabase);
        // What that scene was the only one referencing goes with it. What another loaded scene
        // still references stays, and what a backend has not let go of yet is collected by the
        // sweep in Update.
        UnloadUnusedAssets();
    }
    return unloaded;
}

bool Game::RetainOnlyScene(const unsigned int sceneId)
{
    if (!mSceneManager->RetainOnlyScene(sceneId))
    {
        return false;
    }
    mAudioSystem->Synchronize(*mSceneManager, *mAssetDatabase);
    UnloadUnusedAssets();
    return true;
}

bool Game::LoadAssets(const Platform::IContentSource& source)
{
    // A manifest is what a packaged build ships, and scanning is what a project under development
    // gets. Asking the source rather than the filesystem is what lets a packed build answer at all.
    const std::filesystem::path manifestPath{ Assets::AssetDatabase::ManifestRelativePath };
    const bool hasManifest = source.Exists(manifestPath);
    const bool loaded = hasManifest
        ? mAssetDatabase->LoadManifest(source, manifestPath)
        : mAssetDatabase->Refresh(source);
    if (!loaded)
    {
        Diagnostics::Debug::LogError(
            "Failed to initialize the asset database. root=", source.GetDescription().string(),
            ", mode=", hasManifest ? "manifest" : "scan");
        return false;
    }
    return true;
}

void Game::SetRenderAspectRatio(const float aspectRatio)
{
    mRenderAspectRatio = std::isfinite(aspectRatio) && aspectRatio > 0.0f ? aspectRatio : 1.0f;
    for (const auto& scene : mSceneManager->GetActiveScenes() | std::views::values)
    {
        for (const auto& gameObject : scene->GetGameObjects() | std::views::values)
        {
            for (Camera* camera : gameObject->GetComponents<Camera>())
            {
                camera->SetAspectRatio(mRenderAspectRatio);
            }
        }
    }
}

}
