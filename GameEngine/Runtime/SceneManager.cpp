#include "pch.h"
#include "SceneManager.h"

#include <algorithm>

#include "Scene.h"
#include "../Platform/IContentSource.h"
#include "RuntimeContext.h"
#include "../Diagnostics/Debug.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{

namespace
{
    class FlagScope final
    {
    public:
        explicit FlagScope(bool& flag) : mFlag(flag), mPrevious(std::exchange(flag, true)) {}
        ~FlagScope() { mFlag = mPrevious; }

    private:
        bool& mFlag;
        bool mPrevious;
    };
}

SceneManager::SceneManager(RuntimeContext& runtimeContext)
    : mRuntimeContext(&runtimeContext)
{
}

SceneManager::~SceneManager()
{
    mIsDestroying = true;
    auto removed = std::move(mActiveScenes);
    mActiveScenes.clear();
    mPendingLoads.clear();
    mPendingUnloads.clear();
    mScenesLoadedDuringUpdate.clear();
    // 콜백이 매니저를 직접 보관할 수 있으므로 모든 장면을 먼저 조회 목록에서 뺀다.
    // 지역 소유자의 파괴는 매니저의 조회와 대기 컨테이너가 살아 있을 때 완료된다.
    removed.clear();
}

std::unique_ptr<Scene> SceneManager::LoadSceneFile(const std::filesystem::path& relativeScenePath)
{
    std::vector<std::byte> sceneBytes;
    if (!mContentSource || !mContentSource->Read(relativeScenePath, sceneBytes))
    {
        Diagnostics::Debug::LogError(
            "Failed to read a scene file. path=", relativeScenePath.string());
        return nullptr;
    }
    if (!mSceneLoader)
    {
        Diagnostics::Debug::LogError(
            "No scene loader is set; the runtime cannot read scene files. path=",
            relativeScenePath.string());
        return nullptr;
    }
    return mSceneLoader(sceneBytes, relativeScenePath, *mRuntimeContext);
}

bool SceneManager::RegisterScenePaths(
    const Platform::IContentSource& source,
    const std::unordered_map<unsigned int, std::filesystem::path>& scenePaths)
{
    if (mIsDestroying || mIsRetainingScene)
    {
        return false;
    }
    std::unordered_map<unsigned int, std::filesystem::path> validatedPaths;

    // Scenes are read from the same source as the assets they reference, so a scene and its
    // assets are resolved against the same root.
    for (const auto& [sceneId, scenePath] : scenePaths)
    {
        // AddScene이 발급하는 범위의 id는 프로젝트가 쓸 수 없다. 허용하면 프로젝트의 장면과
        // 에디터가 추가한 장면이 같은 id를 두고 다투게 된다.
        if (sceneId >= FirstAddedSceneId)
        {
            Diagnostics::Debug::LogError(
                "Scene ids at or above ", FirstAddedSceneId,
                " are reserved for added scenes. sceneId=", sceneId);
            return false;
        }
        const std::filesystem::path relativeScenePath = scenePath.lexically_normal();
        if (!source.Exists(relativeScenePath))
        {
            Diagnostics::Debug::LogError(
                "Scene file is missing or invalid. sceneId=" + std::to_string(sceneId) +
                ", path=" + relativeScenePath.string());
            return false;
        }
        validatedPaths.emplace(sceneId, relativeScenePath);
    }

    mContentSource = &source;
    mScenePaths = std::move(validatedPaths);
    return true;
}

SceneLoadResult SceneManager::LoadScene(const unsigned int sceneId)
{
    if (mIsDestroying || mIsRetainingScene)
    {
        return SceneLoadResult::Failed;
    }
    if (!mScenePaths.contains(sceneId))
    {
        Diagnostics::Debug::LogError("Scene with id " + std::to_string(sceneId) + " not found.");
        return SceneLoadResult::Failed;
    }
    if (IsSceneLoaded(sceneId) || std::ranges::find(mPendingLoads, sceneId) != mPendingLoads.end())
    {
        Diagnostics::Debug::LogError("Scene with id " + std::to_string(sceneId) + " is already loaded.");
        return SceneLoadResult::Failed;
    }

    if (mIsUpdating)
    {
        mPendingLoads.push_back(sceneId);
        return SceneLoadResult::Queued;
    }
    return LoadSceneImmediate(sceneId) ? SceneLoadResult::Loaded : SceneLoadResult::Failed;
}

bool SceneManager::UnloadScene(const unsigned int sceneId)
{
    if (mIsDestroying || mIsRetainingScene)
    {
        return false;
    }
    const auto pendingLoad = std::ranges::find(mPendingLoads, sceneId);
    if (pendingLoad != mPendingLoads.end())
    {
        mPendingLoads.erase(pendingLoad);
        return true;
    }

    if (!IsSceneLoaded(sceneId))
    {
        return false;
    }

    if (mIsUpdating)
    {
        mPendingUnloads.insert(sceneId);
        return true;
    }

    return UnloadSceneImmediate(sceneId);
}

bool SceneManager::UnloadSceneImmediate(const unsigned int sceneId)
{
    const auto found = mActiveScenes.find(sceneId);
    if (found == mActiveScenes.end())
    {
        return false;
    }
    std::unique_ptr<Scene> removed = std::move(found->second);
    mActiveScenes.erase(found);
    removed.reset();
    return true;
}

bool SceneManager::RetainOnlyScene(const unsigned int sceneId)
{
    if (mIsDestroying || mIsRetainingScene || mIsUpdating || mIsFlushingChanges ||
        !mActiveScenes.contains(sceneId))
    {
        return false;
    }

    std::vector<std::unique_ptr<Scene>> removed;
    removed.reserve(mActiveScenes.size() - 1);
    const FlagScope retaining(mIsRetainingScene);
    for (auto scene = mActiveScenes.begin(); scene != mActiveScenes.end();)
    {
        if (scene->first == sceneId)
        {
            ++scene;
        }
        else
        {
            removed.push_back(std::move(scene->second));
            scene = mActiveScenes.erase(scene);
        }
    }
    mPendingLoads.clear();
    mPendingUnloads.clear();
    mScenesLoadedDuringUpdate.clear();
    // 콜백은 유지 대상 하나만 조회할 수 있고 그 집합을 바꿀 수 없다.
    removed.clear();
    return true;
}

void SceneManager::Update(const float deltaTime)
{
    if (mIsDestroying || mIsRetainingScene)
    {
        return;
    }
    mIsUpdating = true;
    // 장면 id 순으로. 해시 순서는 프로세스마다 달라 두 장면의 상대 순서가 재현되지 않는다.
    std::vector<unsigned int> order;
    order.reserve(mActiveScenes.size());
    for (const auto& sceneId : mActiveScenes | std::views::keys)
    {
        order.push_back(sceneId);
    }
    std::ranges::sort(order);
    for (const unsigned int sceneId : order)
    {
        if (!mPendingUnloads.contains(sceneId))
        {
            mActiveScenes.at(sceneId)->Update(deltaTime);
        }
    }
    mIsUpdating = false;
    FlushPendingChanges();
}

Scene* SceneManager::GetScene(const unsigned int sceneId)
{
    const auto iterator = mActiveScenes.find(sceneId);
    return iterator == mActiveScenes.end() ? nullptr : iterator->second.get();
}

const Scene* SceneManager::GetScene(const unsigned int sceneId) const
{
    const auto iterator = mActiveScenes.find(sceneId);
    return iterator == mActiveScenes.end() ? nullptr : iterator->second.get();
}

bool SceneManager::IsSceneLoaded(const unsigned int sceneId) const
{
    return mActiveScenes.contains(sceneId) &&
        !mPendingUnloads.contains(sceneId);
}

bool SceneManager::LoadSceneImmediate(const unsigned int sceneId)
{
    Diagnostics::Debug::Log("Loading scene " + std::to_string(sceneId) + "...");
    std::unique_ptr<Scene> scene =
        LoadSceneFile(mScenePaths.at(sceneId));
    if (!scene)
    {
        Diagnostics::Debug::LogError("Failed to load scene " + std::to_string(sceneId) + ".");
        return false;
    }

    mActiveScenes.emplace(sceneId, std::move(scene));
    return true;
}

unsigned int SceneManager::AddScene(std::unique_ptr<Scene> scene)
{
    if (mIsDestroying || mIsRetainingScene || !scene)
    {
        return 0;
    }
    if (mIsUpdating)
    {
        Diagnostics::Debug::LogError(
            "A scene cannot be added while scenes are updating. scene=", scene->GetName());
        return 0;
    }

    while (mActiveScenes.contains(mNextAddedSceneId))
    {
        ++mNextAddedSceneId;
    }
    const unsigned int sceneId = mNextAddedSceneId++;
    mActiveScenes.emplace(sceneId, std::move(scene));
    return sceneId;
}

std::vector<unsigned int> SceneManager::TakeScenesLoadedDuringUpdate()
{
    return std::exchange(mScenesLoadedDuringUpdate, {});
}

void SceneManager::FlushPendingChanges()
{
    const FlagScope flushing(mIsFlushingChanges);
    const auto pendingUnloads = std::exchange(mPendingUnloads, {});
    for (const unsigned int sceneId : pendingUnloads)
    {
        static_cast<void>(UnloadSceneImmediate(sceneId));
    }

    const std::vector<unsigned int> pendingLoads = std::move(mPendingLoads);
    mPendingLoads.clear();
    for (const unsigned int sceneId : pendingLoads)
    {
        if (LoadSceneImmediate(sceneId))
        {
            mScenesLoadedDuringUpdate.push_back(sceneId);
        }
        else
        {
            Diagnostics::Debug::LogError(
                "Deferred scene load failed. sceneId=" + std::to_string(sceneId));
        }
    }
}

}
