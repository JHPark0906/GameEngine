#include "pch.h"
#include "ModelInstantiation.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "GameObject.h"
#include "MeshRenderer.h"
#include "Scene.h"
#include "Transform.h"
#include "../Assets/Asset.h"
#include "../Assets/AssetDatabase.h"
#include "../Assets/AssetReference.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Runtime
{

namespace
{
    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.generic_u8string();
        return {
            reinterpret_cast<const char*>(value.data()),
            reinterpret_cast<const char*>(value.data() + value.size())
        };
    }

    /// <summary>
    /// 파일 속 메시 하나를 그리는 객체 하나를 만든다. 메시 참조가 로컬 id를 싣고 있는데, 모델의
    /// 부품을 가리키는 일이 가능한 이유의 전부가 그것이다.
    /// </summary>
    [[nodiscard]] GameObject* CreateMeshObject(
        Scene& scene,
        const std::filesystem::path& modelPath,
        const std::uint32_t localId,
        const std::string& name)
    {
        GameObject* const gameObject = scene.CreateGameObject(name);
        if (!gameObject)
        {
            return nullptr;
        }

        MeshRenderer* const renderer = gameObject->AddComponent<MeshRenderer>();
        if (!renderer)
        {
            static_cast<void>(scene.RemoveGameObject(gameObject->GetInstanceId()));
            return nullptr;
        }

        renderer->SetMesh(Assets::AssetReference(modelPath, localId));
        return gameObject;
    }
}

GameObject* InstantiateModel(
    Scene& scene,
    const Assets::AssetDatabase& assetDatabase,
    const std::filesystem::path& modelPath)
{
    const Assets::Asset* const model = assetDatabase.FindAsset(modelPath);
    if (!model)
    {
        Diagnostics::Debug::LogError(
            "Cannot instantiate a model that is not a registered asset. path=", modelPath.string());
        return nullptr;
    }

    const std::vector<Assets::SubAsset>& subAssets = model->GetSubAssets();
    std::vector<std::uint32_t> meshLocalIds;
    for (std::uint32_t localId = 0; localId < subAssets.size(); ++localId)
    {
        // A file may hold assets of more than one kind one day; only the meshes become objects.
        if (subAssets[localId].type == Assets::AssetType::Mesh)
        {
            meshLocalIds.push_back(localId);
        }
    }

    if (meshLocalIds.empty())
    {
        Diagnostics::Debug::LogError(
            "A model holds no mesh to instantiate. path=", modelPath.string(),
            ", assets=", subAssets.size());
        return nullptr;
    }

    // The relative path is what a reference stores, so the objects keep pointing at the right file
    // no matter which spelling of the path the caller used.
    const std::filesystem::path relativePath = model->GetRelativePath();
    const std::string modelName = PathToUtf8(relativePath.stem());

    if (meshLocalIds.size() == 1)
    {
        GameObject* const single = CreateMeshObject(
            scene, relativePath, meshLocalIds.front(), modelName);
        if (!single)
        {
            Diagnostics::Debug::LogError(
                "Failed to instantiate a model. path=", modelPath.string());
        }
        return single;
    }

    GameObject* const root = scene.CreateGameObject(modelName);
    if (!root)
    {
        Diagnostics::Debug::LogError(
            "Failed to create the root of an instantiated model. path=", modelPath.string());
        return nullptr;
    }

    // Removing an object does not remove its children, so a rollback has to name every part it
    // created. Half a model in the scene would be worse than none: the parts that attached look
    // complete, and the ones that did not sit loose at the origin.
    std::vector<unsigned int> createdInstanceIds{ root->GetInstanceId() };
    const auto rollBack = [&scene, &createdInstanceIds]()
    {
        for (const unsigned int instanceId : createdInstanceIds)
        {
            static_cast<void>(scene.RemoveGameObject(instanceId));
        }
    };

    for (const std::uint32_t localId : meshLocalIds)
    {
        const std::string& subAssetName = subAssets[localId].name;
        GameObject* const part = CreateMeshObject(
            scene,
            relativePath,
            localId,
            subAssetName.empty() ? modelName + " " + std::to_string(localId) : subAssetName);
        if (part)
        {
            createdInstanceIds.push_back(part->GetInstanceId());
        }
        if (!part || !part->GetTransform().SetParent(&root->GetTransform()))
        {
            Diagnostics::Debug::LogError(
                "Failed to instantiate part of a model. path=", modelPath.string(),
                ", localId=", localId);
            rollBack();
            return nullptr;
        }
    }

    Diagnostics::Debug::Log(
        "Instantiated a model. path=", relativePath.string(), ", meshes=", meshLocalIds.size());
    return root;
}

}
