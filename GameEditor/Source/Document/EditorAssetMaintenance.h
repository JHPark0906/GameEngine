#pragma once

// editor-layer: 1 (Document)

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "App/ProjectFile.h"
#include "Core/Guid.h"
#include "Platform/DirectoryContentSource.h"
#include "Serialization/ComponentSchema.h"

#include "Rules/OrphanSidecars.h"
#include "Rules/SceneReferenceMigration.h"

namespace GameEngine::Assets
{
class AssetDatabase;
class AssetReference;
}

namespace GameEngine::Runtime
{
class Game;
class Scene;
}

namespace GameEditor
{

/// <summary>
/// 프로젝트의 에셋을 정돈하는 일이다: 없는 사이드카를 쓰고, 옮겨진 에셋의 사이드카를 따라
/// 옮기고, 장면의 참조를 정체성으로 이관하고, 주인 없는 사이드카를 치운다.
///
/// 문서 상태는 부르는 쪽이 관리한다. 저장하지 않은 편집이 있으면 이관하지 않는 전제도,
/// <see cref="SceneMigrationOutcome"/>이 알리는 열린 장면 다시 읽기도 호출자의 몫이다.
/// 장면을 열고 닫는 일을 이 부품에 주면 문서의 수명 전체가 딸려 들어온다.
///
/// <b>프로젝트는 상태가 아니라 인자다.</b> 프로젝트를 다시 열면 파일·런타임·콘텐츠·스키마가
/// 통째로 바뀌는데, 이 부품이 그것을 쥐고 있으면 조사한 프로젝트와 적용하는 프로젝트가 어긋날
/// 수 있다. 호출마다 받으면 그런 상태가 존재할 수 없다.
///
/// 대신 <b>조사 결과는 이 부품의 상태다</b> — 계획 둘, 이관이 끝났다는 표시, 그리고 없어진
/// 에셋의 옛 경로. 넷 다 「조사한 답을 사람이 답할 때까지 들고 있는」 것이라 한 수명을 산다.
/// 그래서 조사 결과에는 자기가 무엇을 조사했는지가 함께 적힌다: 다른 프로젝트의 손잡이로
/// 적용하려 하면 거절한다. 그 검사가 없으면 「프로젝트를 열 때 누군가 지워 준다」에 기대게
/// 되고, 그것은 잊을 수 있는 규칙이다.
/// </summary>
class EditorAssetMaintenance final
{
public:
    /// <summary>
    /// 지금 열려 있는 프로젝트를 이루는 것들이다. 넷 다 읽기만 하며, 하나라도 없으면 이 부품은
    /// 아무 일도 하지 않는다 — 프로젝트가 없는 편집기에는 정돈할 에셋도 없다.
    /// </summary>
    struct ProjectHandles
    {
        const GameEngine::App::ProjectFileData* file = nullptr;
        GameEngine::Runtime::Game* game = nullptr;
        GameEngine::Platform::DirectoryContentSource* content = nullptr;
        const std::vector<GameEngine::Serialization::ComponentSchema>* schemas = nullptr;
    };

    /// <summary>이관을 적용한 결과다. 부르는 쪽이 이어서 할 일이 여기 적힌다.</summary>
    struct SceneMigrationOutcome
    {
        /// <summary>실제로 이관했는지다. 거짓이면 아무 파일도 바뀌지 않았다.</summary>
        bool applied = false;
        /// <summary>
        /// 열려 있던 장면을 다시 읽어야 하는지다. 방금 파일이 바뀐 그 장면일 수 있고, 메모리의
        /// 것은 아직 옛 참조를 쥐고 있어 다시 읽지 않으면 다음 저장이 이관을 되돌려 쓴다.
        /// </summary>
        bool shouldReloadOpenScene = false;
    };

    /// <summary>이관 계획이다. 세우지 않았거나 물러났으면 null이다.</summary>
    [[nodiscard]] const SceneMigrationPlan* GetSceneMigrationPlan() const
    {
        return mSceneMigrationPlan ? &*mSceneMigrationPlan : nullptr;
    }

    /// <summary>이관 계획을 물린다. 사람이 지금은 하지 않겠다고 답한 자리다.</summary>
    void DismissSceneMigration() { mSceneMigrationPlan.reset(); }

    /// <summary>이 프로젝트의 장면 참조가 정체성으로 옮겨졌는지다.</summary>
    [[nodiscard]] bool AreSceneReferencesMigrated() const { return mSceneReferencesMigrated; }

    /// <summary>주인 없는 사이드카 정리 계획이다. 세우지 않았거나 물러났으면 null이다.</summary>
    [[nodiscard]] const OrphanSidecarPlan* GetOrphanSidecarPlan() const
    {
        return mOrphanSidecarPlan ? &*mOrphanSidecarPlan : nullptr;
    }

    /// <summary>정리 계획을 물린다.</summary>
    void DismissOrphanSidecarCleanup() { mOrphanSidecarPlan.reset(); }

    /// <summary>정체성 없는 에셋에 사이드카를 쓴다. 만든 파일 수를 답한다.</summary>
    std::size_t WriteMissingSidecars(const ProjectHandles& project);

    /// <summary>옮겨진 에셋의 사이드카를 따라 옮긴다. 실제로 옮긴 수를 답한다.</summary>
    /// <param name="rescanned">방금 스캔했고 아직 갈아 끼우지 않은 데이터베이스다.</param>
    std::size_t MoveSidecarsForMovedAssets(
        const ProjectHandles& project, const GameEngine::Assets::AssetDatabase& rescanned);

    /// <summary>치워 두었던 사이드카를 제자리로 되돌린다. 되돌린 수를 답한다.</summary>
    std::size_t RestoreSetAsideSidecars(const ProjectHandles& project);

    /// <summary>프로젝트의 장면들을 훑어 이관 계획을 세운다.</summary>
    void SurveySceneMigration(const ProjectHandles& project);

    /// <summary>
    /// 계획대로 이관한다. 저장하지 않은 편집이 있는지는 <b>부르는 쪽이 먼저 본다</b> — 이관은
    /// 열린 장면의 파일도 다시 쓰므로, 그 전제가 깨진 채로 부르면 사람의 편집이 사라진다.
    /// </summary>
    [[nodiscard]] SceneMigrationOutcome ApplySceneMigration(const ProjectHandles& project);

    /// <summary>주인 없는 사이드카를 훑어 정리 계획을 세운다.</summary>
    void SurveyOrphanSidecars(const ProjectHandles& project);

    /// <summary>계획대로 치운다. 실제로 치웠으면 참이다.</summary>
    [[nodiscard]] bool ApplyOrphanSidecarCleanup(const ProjectHandles& project);

    /// <summary>사람이 읽을 참조 설명이다. 없어진 에셋이면 옛 경로까지 말한다.</summary>
    [[nodiscard]] std::string DescribeAssetReference(
        const ProjectHandles& project, const GameEngine::Assets::AssetReference& reference) const;

    /// <summary>이관을 위해 장면 하나를 읽는다. 읽지 못하면 null이다.</summary>
    [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Scene> LoadSceneForMigration(
        const ProjectHandles& project, const std::filesystem::path& relativeScenePath) const;

private:
    /// <summary>
    /// 그 손잡이가 조사할 때의 것과 같은 프로젝트인지다. 다르면 계획을 적용하지 않는다 —
    /// 앞 프로젝트에서 세운 계획을 다음 프로젝트의 파일에 쓰는 것이 이 검사가 막는 것이다.
    /// </summary>
    [[nodiscard]] bool IsSurveyedProject(const ProjectHandles& project) const;

    /// <summary>지금 조사한 프로젝트를 적어 둔다. 조사하는 두 자리가 부른다.</summary>
    void RememberSurveyedProject(const ProjectHandles& project);

    /// <summary>계획이 어느 프로젝트를 조사한 것인지다. 비어 있으면 조사한 적이 없다.</summary>
    std::filesystem::path mSurveyedProjectPath;

    std::optional<SceneMigrationPlan> mSceneMigrationPlan;
    bool mSceneReferencesMigrated = false;
    std::optional<OrphanSidecarPlan> mOrphanSidecarPlan;
    /// <summary>
    /// 없어진 에셋의 옛 경로다. 정리가 끝난 뒤에도 남는다 — 인스펙터는 계속 그것을 보여야 한다.
    /// </summary>
    std::vector<std::pair<GameEngine::Core::Guid, std::filesystem::path>> mMissingAssetPaths;
};

}
