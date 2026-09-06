#include "Document/EditorAssetMaintenance.h"

#include "Rules/EditorFileWrite.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iterator>
#include <ranges>
#include <span>
#include <system_error>
#include <unordered_set>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "App/EditorSettings.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetIdentityIssue.h"
#include "Assets/AssetMoveMatching.h"
#include "Platform/TextFile.h"
#include "Runtime/SceneManager.h"
#include "Assets/AssetReference.h"
#include "Platform/RelativePath.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Game.h"
#include "Runtime/Scene.h"
#include "Serialization/SceneSerializer.h"

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 백업 파일 이름에 붙일 시각이다. 초까지 적는 이유는 같은 분에 두 번 이관해도 앞의 사본을
    /// 덮지 않기 위해서다 — 덮으면 되돌아갈 자리가 사라진다.
    /// </summary>
    [[nodiscard]] std::string MakeFileTimestamp()
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::tm parts{};
        if (localtime_s(&parts, &now) != 0)
        {
            return "unknown";
        }
        std::array<char, 32> text{};
        const std::size_t written =
            std::strftime(text.data(), text.size(), "%Y%m%d-%H%M%S", &parts);
        return written > 0 ? std::string(text.data(), written) : std::string("unknown");
    }
}

bool EditorAssetMaintenance::IsSurveyedProject(const ProjectHandles& project) const
{
    return project.file && !mSurveyedProjectPath.empty() &&
        mSurveyedProjectPath == project.file->filePath;
}

void EditorAssetMaintenance::RememberSurveyedProject(const ProjectHandles& project)
{
    mSurveyedProjectPath = project.file ? project.file->filePath : std::filesystem::path{};
}

std::size_t EditorAssetMaintenance::WriteMissingSidecars(const ProjectHandles& project)
{
    if (!project.game || !project.file || !project.content)
    {
        return 0;
    }
    // 규칙은 Assets에 하나다. 패키징도 같은 함수를 부르므로, 편집기를 한 번도 거치지 않은
    // 프로젝트도 사람 손을 기다리지 않고 빌드된다.
    return GameEngine::Assets::IssueMissingIdentities(
        project.game->GetAssetDatabase(), *project.content);
}


std::size_t EditorAssetMaintenance::MoveSidecarsForMovedAssets(
    const ProjectHandles& project, const GameEngine::Assets::AssetDatabase& rescanned)
{
    if (!project.game || !project.file)
    {
        return 0;
    }
    const GameEngine::Assets::AssetDatabase& live = project.game->GetAssetDatabase();
    const std::filesystem::path& projectRoot = live.GetProjectRootPath();

    // 사라진 쪽: 정체성을 가졌는데 새 스캔에 그 경로가 없는 에셋들이다.
    std::vector<GameEngine::Assets::DepartedAsset> departed;
    for (const std::unique_ptr<GameEngine::Assets::Asset>& asset : live.GetAssets())
    {
        if (asset->GetGuid().IsValid() && !rescanned.FindAsset(asset->GetRelativePath()))
        {
            departed.push_back({ asset->GetRelativePath(), asset->GetContentHash(),
                asset->GetGuid() });
        }
    }
    if (departed.empty())
    {
        return 0;
    }

    // 나타난 쪽: 새 스캔에만 있고 아직 정체성이 없는 파일들이다. 정체성을 이미 가진 것은 자기
    // 사이드카와 함께 옮겨진 것이므로 받을 자리가 아니다.
    std::vector<GameEngine::Assets::ArrivedFile> arrived;
    for (const std::unique_ptr<GameEngine::Assets::Asset>& asset : rescanned.GetAssets())
    {
        if (!asset->GetGuid().IsValid() && !live.FindAsset(asset->GetRelativePath()))
        {
            arrived.push_back({ asset->GetRelativePath(), asset->GetContentHash(),
                !rescanned.GetSidecarPath(*asset).empty() });
        }
    }

    std::size_t moved = 0;
    for (const GameEngine::Assets::AssetMove& move :
        GameEngine::Assets::MatchAssetMoves(departed, arrived))
    {
        const GameEngine::Assets::Asset* asset = live.FindAsset(move.from);
        const std::filesystem::path sidecar = asset ? live.GetSidecarPath(*asset)
                                                    : std::filesystem::path();
        if (move.refusal != GameEngine::Assets::MoveRefusal::None)
        {
            // 안 옮긴 것도 말한다. 조용히 넘어가면 사람은 기능이 고장 난 것과 구별할 수 없고,
            // 이 판정들은 대개 사람이 파일을 하나 더 옮기면 풀린다.
            GameEngine::Diagnostics::Debug::LogWarning(
                "Kept asset metadata where it was. path=", move.from.generic_string(),
                ", reason=", GameEngine::Assets::DescribeMoveRefusal(move.refusal));
            continue;
        }
        if (sidecar.empty())
        {
            // 파일과 함께 사이드카도 지워졌다. 옮길 것이 없으므로 새 자리는 새 정체성을 받는다.
            continue;
        }

        // 새 이름은 옛 파일 이름 뒤에 붙어 있던 꼬리를 그대로 물려받는다. 옛 이름의 사이드카를
        // 개명하는 것은 여기서 할 일이 아니라, 이름이 아니라 자리만 바꾼다.
        const std::string sidecarName = sidecar.filename().string();
        const std::string sourceName = move.from.filename().string();
        if (!sidecarName.starts_with(sourceName))
        {
            continue;
        }
        std::filesystem::path destination = projectRoot / move.to;
        destination += sidecarName.substr(sourceName.size());
        std::error_code error;
        if (std::filesystem::exists(destination, error))
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Kept asset metadata where it was. path=", move.from.generic_string(),
                ", reason=", GameEngine::Assets::DescribeMoveRefusal(
                    GameEngine::Assets::MoveRefusal::DestinationHasSidecar));
            continue;
        }
        std::filesystem::rename(projectRoot / sidecar, destination, error);
        if (error)
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Could not move asset metadata. path=", sidecar.generic_string(),
                ", error=", error.message());
            continue;
        }
        GameEngine::Diagnostics::Debug::Log(
            "Asset moved, and its metadata followed. from=", move.from.generic_string(),
            ", to=", move.to.generic_string(), ", guid=", move.guid.ToString());
        ++moved;
    }
    return moved;
}

std::unique_ptr<GameEngine::Runtime::Scene> EditorAssetMaintenance::LoadSceneForMigration(
    const ProjectHandles& project, const std::filesystem::path& relativeScenePath) const
{
    if (!project.content || !project.game || !project.file)
    {
        return nullptr;
    }
    std::vector<std::byte> bytes;
    if (!project.content->Read(relativeScenePath, bytes))
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "A scene could not be read while looking at its asset references. scene=",
            relativeScenePath.generic_string());
        return nullptr;
    }
    return GameEngine::Serialization::SceneSerializer::LoadFromBytes(
        bytes, project.file->GetAssetRootPath() / relativeScenePath,
        project.game->GetRuntimeContext());
}

void EditorAssetMaintenance::SurveySceneMigration(const ProjectHandles& project)
{
    RememberSurveyedProject(project);
    mSceneMigrationPlan.reset();
    mSceneReferencesMigrated = false;
    if (!project.game || !project.content || !project.file)
    {
        return;
    }
    const GameEngine::Assets::AssetDatabase& database = project.game->GetAssetDatabase();

    SceneMigrationPlan plan;
    std::unordered_set<std::string> alreadyNamed;
    for (const auto& relativePath : project.file->settings.scenePaths | std::views::values)
    {
        const std::unique_ptr<GameEngine::Runtime::Scene> scene =
            LoadSceneForMigration(project, relativePath);
        if (!scene)
        {
            continue;
        }
        SceneReferenceSurvey survey =
            SurveySceneReferences(*scene, database, (*project.schemas));
        plan.convertible += survey.convertible;
        plan.unresolved += survey.unresolved;
        plan.opaqueComponents += survey.opaqueComponents;
        for (const std::string& typeName : survey.opaqueTypeNames)
        {
            if (std::ranges::find(plan.opaqueTypeNames, typeName) == plan.opaqueTypeNames.end())
            {
                plan.opaqueTypeNames.push_back(typeName);
            }
        }
        for (const std::filesystem::path& asset : survey.assetsWithoutIdentity)
        {
            if (alreadyNamed.insert(asset.generic_string()).second)
            {
                plan.assetsWithoutIdentity.push_back(asset);
            }
        }
        if (survey.convertible > 0 || !survey.assetsWithoutIdentity.empty() ||
            survey.opaqueComponents > 0)
        {
            plan.scenes.push_back({ relativePath, std::move(survey) });
        }
    }
    // scenePaths는 순서 없는 표라 실행마다 다른 차례로 나온다. 질문 줄과 로그가 실행마다 달라
    // 보이지 않도록 여기서 한 번 세운다.
    std::ranges::sort(
        plan.scenes,
        [](const SceneMigrationEntry& left, const SceneMigrationEntry& right)
        {
            return left.scenePath < right.scenePath;
        });
    std::ranges::sort(plan.assetsWithoutIdentity);

    // 「이관됐다」의 뜻은 「바꿀 것이 남아 있지 않다」이다. 가리키는 것이 없는 참조는 바꿀 방법이
    // 없으므로 이 답을 영원히 거짓으로 붙들지 않는다 — 그러면 새 참조도 영원히 경로로 적힌다.
    // 반대로 들여다볼 수 없는 컴포넌트가 있으면 거짓이다: 그 안을 보지 못한 채 「끝났다」고 하면
    // 새 참조가 정체성으로 적히기 시작하고, 보이지 않던 경로 참조와 한 장면에서 섞인다.
    mSceneReferencesMigrated = plan.convertible == 0 && plan.assetsWithoutIdentity.empty() &&
        plan.opaqueComponents == 0;
    if (plan.opaqueComponents > 0)
    {
        // 물을 것이 없어도 이 사실은 말한다. 사람이 할 일이 있고 — 게임 프로젝트를 한 번 빌드
        // — 하지 않으면 그 장면의 참조는 계속 경로로 남는다.
        GameEngine::Diagnostics::Debug::LogWarning(
            "Some scene components have no schema, so their asset references cannot be migrated. ",
            plan.Describe());
    }
    if (plan.HasSomethingToAsk())
    {
        GameEngine::Diagnostics::Debug::Log("Scene reference migration: ", plan.Describe());
        for (const SceneMigrationEntry& entry : plan.scenes)
        {
            GameEngine::Diagnostics::Debug::Log(
                "  scene=", entry.scenePath.generic_string(),
                ", convertible=", entry.survey.convertible,
                ", unresolved=", entry.survey.unresolved,
                ", alreadyIdentities=", entry.survey.alreadyMigrated);
        }
        mSceneMigrationPlan = std::move(plan);
    }
}

EditorAssetMaintenance::SceneMigrationOutcome EditorAssetMaintenance::ApplySceneMigration(
    const ProjectHandles& project)
{
    if (!mSceneMigrationPlan || mSceneMigrationPlan->IsBlocked() || !project.game ||
        !project.file)
    {
        return {};
    }
    if (!IsSurveyedProject(project))
    {
        // 이 계획은 다른 프로젝트를 훑고 세운 것이다. 그대로 쓰면 이 프로젝트의 장면 파일에
        // 남의 조사 결과를 쓰게 된다. 「프로젝트를 열 때 누군가 계획을 지워 준다」에 기대지
        // 않으려고 계획이 자기가 무엇을 조사했는지 들고 있다.
        GameEngine::Diagnostics::Debug::LogWarning(
            "The migration plan was surveyed for another project, so it was not applied. "
            "surveyed=", mSurveyedProjectPath.string(),
            ", open=", project.file->filePath.string());
        return {};
    }
    const GameEngine::Assets::AssetDatabase& database = project.game->GetAssetDatabase();

    // 먼저 전부를 메모리에서 바꿔 본다. 하나라도 실패하면 파일은 하나도 건드리지 않는다 — 절반만
    // 이관된 프로젝트가 바로 이 관문이 막으려던 상태다.
    struct RewrittenScene
    {
        std::filesystem::path relativePath;
        std::string text;
    };
    std::vector<RewrittenScene> rewritten;
    for (const SceneMigrationEntry& entry : mSceneMigrationPlan->scenes)
    {
        if (entry.survey.convertible == 0)
        {
            continue;
        }
        const std::unique_ptr<GameEngine::Runtime::Scene> scene =
            LoadSceneForMigration(project, entry.scenePath);
        if (!scene)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Migration stopped and nothing was written: a scene could not be read. scene=",
                entry.scenePath.generic_string());
            return {};
        }
        const std::size_t migrated =
            MigrateSceneReferences(*scene, database, (*project.schemas));
        if (migrated != entry.survey.convertible)
        {
            // 승인받은 수와 바꾼 수가 다르다. 사람이 답한 질문과 다른 일을 하는 셈이므로 멈춘다.
            GameEngine::Diagnostics::Debug::LogError(
                "Migration stopped and nothing was written: a scene changed since it was counted. "
                "scene=", entry.scenePath.generic_string(), ", expected=", entry.survey.convertible,
                ", migrated=", migrated);
            return {};
        }
        rewritten.push_back(
            { entry.scenePath, GameEngine::Serialization::SceneSerializer::SaveToText(*scene) });
    }

    const std::filesystem::path projectRoot = project.file->GetAssetRootPath();
    const std::string stamp = MakeFileTimestamp();
    for (const RewrittenScene& item : rewritten)
    {
        const std::filesystem::path scenePath = projectRoot / item.relativePath;
        std::filesystem::path backupPath = scenePath;
        backupPath += ".bak-" + stamp;
        std::error_code error;
        std::filesystem::copy_file(scenePath, backupPath, error);
        if (error)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Migration stopped: the scene could not be backed up first. scene=",
                item.relativePath.generic_string(), ", error=", error.message());
            return {};
        }
        if (!WriteFileAtomically(scenePath, item.text))
        {
            return {};
        }
        GameEngine::Diagnostics::Debug::Log(
            "Scene references migrated to identities. scene=", item.relativePath.generic_string(),
            ", backup=", backupPath.filename().string());
    }

    mSceneMigrationPlan.reset();
    mSceneReferencesMigrated = true;
    // 열려 있던 장면은 방금 파일이 바뀐 그 장면일 수 있다. 메모리의 것은 아직 경로를 쥐고 있어,
    // 다시 읽지 않으면 다음 저장이 이관을 되돌려 쓴다. 다시 읽는 것은 부르는 쪽의 일이다 —
    // 장면을 여는 일을 여기로 들이면 문서의 수명 전체가 딸려 온다.
    return { true, true };
}

std::size_t EditorAssetMaintenance::RestoreSetAsideSidecars(const ProjectHandles& project)
{
    if (!project.content || !project.file)
    {
        return 0;
    }
    const std::filesystem::path projectRoot = project.file->GetAssetRootPath();
    std::size_t restored = 0;
    for (const std::filesystem::path& relativePath : project.content->List())
    {
        if (!IsSetAsideSidecar(relativePath))
        {
            continue;
        }
        const std::filesystem::path ownerPath = SidecarOwnerPath(relativePath);
        std::error_code error;
        if (ownerPath.empty() || !std::filesystem::is_regular_file(projectRoot / ownerPath, error))
        {
            continue;
        }
        // 주인이 돌아왔다. 꼬리를 떼어 원래 이름으로 되돌린다.
        std::filesystem::path restoredPath = projectRoot / relativePath;
        const std::string name = restoredPath.filename().string();
        restoredPath.replace_filename(
            name.substr(0, name.size() - GameEngine::Assets::OrphanedSidecarSuffix.size()));
        if (std::filesystem::exists(restoredPath, error))
        {
            // 돌아온 파일이 이미 자기 사이드카를 갖고 있다. 남의 것을 덮지 않는다.
            continue;
        }
        std::filesystem::rename(projectRoot / relativePath, restoredPath, error);
        if (error)
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Set-aside asset metadata could not be restored. path=",
                relativePath.generic_string(), ", error=", error.message());
            continue;
        }
        GameEngine::Diagnostics::Debug::Log(
            "The asset came back, so its metadata was restored. from=",
            relativePath.generic_string(), ", to=", restoredPath.filename().string());
        ++restored;
    }
    return restored;
}

void EditorAssetMaintenance::SurveyOrphanSidecars(const ProjectHandles& project)
{
    mOrphanSidecarPlan.reset();
    RememberSurveyedProject(project);
    mMissingAssetPaths.clear();
    if (!project.game || !project.content || !project.file)
    {
        return;
    }
    const GameEngine::Assets::AssetDatabase& database = project.game->GetAssetDatabase();
    const std::filesystem::path projectRoot = project.file->GetAssetRootPath();

    // 장면들이 가리키는 정체성을 먼저 모은다. 이것이 「고아」와 「파일이 없는 에셋」을 가른다.
    std::vector<GameEngine::Core::Guid> referenced;
    for (const auto& scenePath : project.file->settings.scenePaths | std::views::values)
    {
        const std::unique_ptr<GameEngine::Runtime::Scene> scene =
            LoadSceneForMigration(project, scenePath);
        if (!scene)
        {
            // 읽지 못한 장면이 무엇을 가리키는지 모르는 채로 치우면, 그 장면이 가리키던 정체성을
            // 잃는다. 하나라도 못 읽으면 이번에는 아무것도 묻지 않는다.
            GameEngine::Diagnostics::Debug::LogWarning(
                "Skipping the metadata cleanup this time: a scene could not be read, so what it "
                "points at is unknown. scene=", scenePath.generic_string());
            return;
        }
        for (const GameEngine::Core::Guid& guid :
            CollectReferencedGuids(*scene, (*project.schemas)))
        {
            if (std::ranges::find(referenced, guid) == referenced.end())
            {
                referenced.push_back(guid);
            }
        }
    }

    const auto readGuid = [this, &project](const std::filesystem::path& relativePath)
    {
        std::vector<std::byte> bytes;
        if (!project.content->Read(relativePath, bytes))
        {
            return GameEngine::Core::Guid{};
        }
        try
        {
            const GameEngine::Core::Json parsed = GameEngine::Core::Json::ParseBytes(bytes);
            if (parsed.IsObject())
            {
                const std::string text = parsed.Value("guid", std::string{});
                if (const std::optional<GameEngine::Core::Guid> guid =
                        GameEngine::Core::Guid::Parse(text))
                {
                    return *guid;
                }
            }
        }
        catch (const std::exception&)
        {
            // 읽을 수 없는 사이드카다. 정체성을 모르므로 참조 여부도 알 수 없고, 아래에서
            // 안전한 쪽 — 손대지 않는 쪽 — 으로 처리된다.
        }
        return GameEngine::Core::Guid{};
    };

    OrphanSidecarPlan plan;
    for (const std::filesystem::path& relativePath : project.content->List())
    {
        const bool setAside = IsSetAsideSidecar(relativePath);
        if (!setAside && database.FindSidecarOwner(relativePath) != nullptr)
        {
            continue;
        }
        const std::filesystem::path ownerPath = SidecarOwnerPath(relativePath);
        if (ownerPath.empty())
        {
            // 사이드카 이름이 아니다. 프로젝트의 여느 파일이다.
            continue;
        }
        if (setAside && database.FindAsset(ownerPath) != nullptr)
        {
            // 주인이 돌아왔다. 되돌리기가 이미 이름을 고쳤어야 하므로 여기 오는 것은 되돌리지
            // 못한 경우뿐이고, 그런 것은 지우지 않는다.
            continue;
        }

        OrphanSidecar orphan{ relativePath, ownerPath, readGuid(relativePath) };
        if (!orphan.guid.IsValid() ||
            std::ranges::find(referenced, orphan.guid) != referenced.end())
        {
            // 정체성을 읽지 못했거나, 장면이 그것을 가리키고 있다. 어느 쪽도 치울 것이 아니다 —
            // 앞은 무엇인지 모르는 것이고, 뒤는 고아가 아니라 파일이 없는 에셋이다.
            if (orphan.guid.IsValid())
            {
                plan.keptForReferences.push_back(std::move(orphan));
            }
            continue;
        }
        (setAside ? plan.toDelete : plan.toSetAside).push_back(std::move(orphan));
    }

    // 열거 순서는 파일시스템의 것이라 실행마다 다를 수 있다. 질문 줄과 로그가 흔들리지 않도록
    // 한 번 세운다.
    const auto byPath = [](const OrphanSidecar& left, const OrphanSidecar& right)
    {
        return left.sidecarPath < right.sidecarPath;
    };
    std::ranges::sort(plan.toSetAside, byPath);
    std::ranges::sort(plan.toDelete, byPath);
    std::ranges::sort(plan.keptForReferences, byPath);

    for (const OrphanSidecar& kept : plan.keptForReferences)
    {
        // 인스펙터가 읽을 것을 여기서 챙긴다. 계획은 답을 받으면 사라지지만 이 사실은 남아야
        // 한다 — 파일이 없는 에셋은 사람이 고칠 때까지 계속 그 자리에 있다.
        mMissingAssetPaths.emplace_back(kept.guid, kept.ownerPath);
        // 왜 안 치웠는지가 사후의 유일한 근거다.
        GameEngine::Diagnostics::Debug::Log(
            "Kept asset metadata whose file is gone: a scene still points at it. path=",
            kept.sidecarPath.generic_string(), ", guid=", kept.guid.ToString());
    }
    if (plan.HasSomethingToAsk())
    {
        GameEngine::Diagnostics::Debug::Log("Asset metadata cleanup: ", plan.Describe());
        mOrphanSidecarPlan = std::move(plan);
    }
}

std::string EditorAssetMaintenance::DescribeAssetReference(
    const ProjectHandles& project, const GameEngine::Assets::AssetReference& reference) const
{
    const GameEngine::Assets::AssetDatabase* const database = (project.file && project.game ? &project.game->GetAssetDatabase() : nullptr);
    if (!database)
    {
        return reference.ToString();
    }
    if (reference.IsGuidReference() && !database->FindAsset(reference))
    {
        // 데이터베이스가 모르는 정체성이다. 남아 있는 사이드카가 그것이 무엇이었는지 안다.
        for (const auto& [guid, path] : mMissingAssetPaths)
        {
            if (guid == reference.GetGuid())
            {
                return path.generic_string() + " (missing)";
            }
        }
    }
    return GameEngine::Assets::DescribeAssetReference(*database, reference);
}

bool EditorAssetMaintenance::ApplyOrphanSidecarCleanup(const ProjectHandles& project)
{
    if (!mOrphanSidecarPlan || !project.file)
    {
        return false;
    }
    if (!IsSurveyedProject(project))
    {
        // 이관 계획과 같은 이유다. 다른 프로젝트를 훑고 세운 계획으로 이 프로젝트의 파일을
        // 치우면, 사람이 본 적 없는 목록이 지워진다.
        GameEngine::Diagnostics::Debug::LogWarning(
            "The sidecar cleanup plan was surveyed for another project, so it was not applied. "
            "surveyed=", mSurveyedProjectPath.string(),
            ", open=", project.file->filePath.string());
        return false;
    }
    const std::filesystem::path projectRoot = project.file->GetAssetRootPath();
    const bool deleting = mOrphanSidecarPlan->IsAskingToDelete();
    const std::vector<OrphanSidecar>& files =
        deleting ? mOrphanSidecarPlan->toDelete : mOrphanSidecarPlan->toSetAside;

    // 계획을 버리기 전에 세어 둔다. files는 그 계획 안을 가리키고 있어서, 버린 뒤에 크기를
    // 물으면 이미 사라진 것에게 묻는 셈이다.
    const std::size_t asked = files.size();
    std::size_t done = 0;
    for (const OrphanSidecar& orphan : files)
    {
        const std::filesystem::path fullPath = projectRoot / orphan.sidecarPath;
        std::error_code error;
        if (deleting)
        {
            if (!std::filesystem::remove(fullPath, error) || error)
            {
                GameEngine::Diagnostics::Debug::LogWarning(
                    "Set-aside asset metadata could not be deleted. path=",
                    orphan.sidecarPath.generic_string(), ", error=", error.message());
                continue;
            }
            GameEngine::Diagnostics::Debug::Log(
                "Deleted set-aside asset metadata. path=", orphan.sidecarPath.generic_string(),
                ", guid=", orphan.guid.ToString());
        }
        else
        {
            std::filesystem::path asidePath = fullPath;
            asidePath += std::string(GameEngine::Assets::OrphanedSidecarSuffix);
            if (std::filesystem::exists(asidePath, error))
            {
                continue;
            }
            std::filesystem::rename(fullPath, asidePath, error);
            if (error)
            {
                GameEngine::Diagnostics::Debug::LogWarning(
                    "Asset metadata could not be set aside. path=",
                    orphan.sidecarPath.generic_string(), ", error=", error.message());
                continue;
            }
            GameEngine::Diagnostics::Debug::Log(
                "Set aside asset metadata whose asset is gone. from=",
                orphan.sidecarPath.generic_string(), ", to=", asidePath.filename().string(),
                ", guid=", orphan.guid.ToString());
        }
        ++done;
    }

    mOrphanSidecarPlan.reset();
    return done == asked;
}

}
