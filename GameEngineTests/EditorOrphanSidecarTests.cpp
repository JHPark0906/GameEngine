#include "EditorOrphanSidecarTests.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Document/EditorContext.h"
#include "Rules/OrphanSidecars.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "TestSupport.h"

using TestSupport::Expect;

using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{

    /// <summary>
    /// 스프라이트 하나를 가리키는 장면 하나짜리 프로젝트다. 정리가 물어야 하는 것은 「장면이
    /// 이 정체성을 가리키는가」 하나뿐이라, 픽스처도 그 하나를 세우는 데만 쓴다.
    /// </summary>
    [[nodiscard]] std::filesystem::path WriteSidecarProject(
        const std::filesystem::path& root, const std::string& spriteReference)
    {
        const std::filesystem::path projectFile = root / "OrphanTest.gameproject";
        const std::string scene =
            R"({"sceneName": "First", "gameObjects": [)"
            R"( {"id": 1, "name": "Sprite", "isActive": true, "components": [)"
            R"( {"type": "Transform"},)"
            R"( {"type": "SpriteRenderer", "sprite": ")" + spriteReference + R"("} ]} ]})";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "OrphanTest",)"
                R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/First.scene" } ]})") &&
            WriteFile(root / "Scenes" / "First.scene", scene) &&
            WriteFile(root / "Textures" / "Albedo.png", "albedo-bytes");
        return wrote ? projectFile : std::filesystem::path{};
    }

    [[nodiscard]] std::string Sidecar(const std::string& guid)
    {
        return R"({"format":"gameengine-meta/1","guid":")" + guid + R"("})";
    }
}

bool RunOrphanSidecarTests()
{
    namespace Assets = GameEngine::Assets;
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    const std::string keptGuid = "1122334455667788aabbccddeeff0011";

    // 이름에서 주인을 되찾는 규칙부터. 치우기가 이름 뒤에 꼬리 하나를 더할 뿐이므로, 치우기
    // 전과 후가 같은 답을 내야 「돌아왔는가」를 물을 수 있다.
    const bool readsOwnerFromName =
        GameEditor::SidecarOwnerPath("Textures/Albedo.png.meta") ==
            std::filesystem::path("Textures/Albedo.png") &&
        GameEditor::SidecarOwnerPath("Textures/Albedo.png.meta.orphan") ==
            std::filesystem::path("Textures/Albedo.png") &&
        GameEditor::SidecarOwnerPath("Old.png.sprite.json") ==
            std::filesystem::path("Old.png") &&
        GameEditor::SidecarOwnerPath("Textures/Albedo.png").empty();
    const bool knowsWhatIsSetAside =
        GameEditor::IsSetAsideSidecar("Textures/Albedo.png.meta.orphan") &&
        !GameEditor::IsSetAsideSidecar("Textures/Albedo.png.meta") &&
        !GameEditor::IsSetAsideSidecar("Textures/Albedo.png");
    if (!Expect(readsOwnerFromName, "a sidecar's name should name the asset it belonged to") ||
        !Expect(knowsWhatIsSetAside, "a set-aside sidecar should be told apart by its name"))
    {
        return false;
    }

    // 이제 진짜 프로젝트로. 셋을 둔다: 장면이 가리키는 정체성의 고아, 아무도 안 가리키는 고아,
    // 그리고 옛 이름의 고아.
    TemporaryDirectory temporaryDirectory("orphan-sidecars");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteSidecarProject(root, keptGuid);
    const bool wrote = !projectFile.empty() &&
        // 가리키는 장면이 있는 정체성인데 그 파일이 사라진 경우다.
        WriteFile(root / "Textures" / "Albedo.png.meta", Sidecar(keptGuid)) &&
        WriteFile(root / "Textures" / "Unused.png.meta",
            Sidecar("5566778899aabbccddeeff0011223344")) &&
        WriteFile(root / "Textures" / "Legacy.png.sprite.json",
            R"({"guid":"aabbccddeeff00112233445566778899","pixelsPerUnit":9.0})");
    std::error_code error;
    // 장면이 가리키는 그 에셋의 파일만 지운다 — 사이드카는 남긴다.
    const bool removed =
        wrote && std::filesystem::remove(root / "Textures" / "Albedo.png", error);
    if (!Expect(removed, "the orphan sidecar test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the orphan test project should open"))
    {
        return false;
    }

    const GameEditor::OrphanSidecarPlan* plan = context.GetOrphanSidecarPlan();
    const auto namesSidecar = [](const std::vector<GameEditor::OrphanSidecar>& list,
                                  const std::filesystem::path& path)
    {
        return std::ranges::any_of(
            list,
            [&path](const GameEditor::OrphanSidecar& orphan)
            {
                return orphan.sidecarPath == path;
            });
    };

    // 🔴 지시받은 것: 장면이 그 정체성을 가리키면 어느 단계도 손대지 않는다. 그것은 고아가
    // 아니라 파일이 없는 에셋이고, 이 사이드카가 그 사실을 아는 마지막 자리다.
    const bool keepsWhatScenesPointAt = plan &&
        namesSidecar(plan->keptForReferences, "Textures/Albedo.png.meta") &&
        !namesSidecar(plan->toSetAside, "Textures/Albedo.png.meta");
    // 아무도 안 가리키는 것들은 치울 대상이고, 옛 이름도 같은 길을 탄다.
    const bool offersTheRest = plan && !plan->IsAskingToDelete() &&
        namesSidecar(plan->toSetAside, "Textures/Unused.png.meta") &&
        namesSidecar(plan->toSetAside, "Textures/Legacy.png.sprite.json");
    const bool saysWhatIsKept =
        plan && plan->Describe().find("kept because scenes") != std::string::npos;

    // 거부하면 파일이 그대로다.
    context.DismissOrphanSidecarCleanup();
    const bool refusingChangesNothing =
        std::filesystem::exists(root / "Textures" / "Unused.png.meta") &&
        context.GetOrphanSidecarPlan() == nullptr;

    // 1단계: 이름만 바뀐다. 원본은 사라지고, 가리켜지는 것은 그대로 남는다.
    const bool rescanned = context.RefreshProjectAssets();
    const bool setAside = rescanned && context.ApplyOrphanSidecarCleanup();
    const bool movedAside = setAside &&
        std::filesystem::exists(root / "Textures" / "Unused.png.meta.orphan") &&
        !std::filesystem::exists(root / "Textures" / "Unused.png.meta") &&
        std::filesystem::exists(root / "Textures" / "Legacy.png.sprite.json.orphan") &&
        // 🔴 그리고 가리켜지는 것은 손대지 않았다.
        std::filesystem::exists(root / "Textures" / "Albedo.png.meta") &&
        !std::filesystem::exists(root / "Textures" / "Albedo.png.meta.orphan");

    // 2단계: 다시 훑으면 이번에는 지울지를 묻는다.
    const bool rescannedAgain = context.RefreshProjectAssets();
    plan = context.GetOrphanSidecarPlan();
    const bool asksToDelete = rescannedAgain && plan && plan->IsAskingToDelete() &&
        namesSidecar(plan->toDelete, "Textures/Unused.png.meta.orphan") &&
        plan->Describe().find("Delete") != std::string::npos;
    const bool deleted = asksToDelete && context.ApplyOrphanSidecarCleanup() &&
        !std::filesystem::exists(root / "Textures" / "Unused.png.meta.orphan") &&
        std::filesystem::exists(root / "Textures" / "Albedo.png.meta");

    return Expect(
               keepsWhatScenesPointAt,
               "a sidecar a scene still points at should never be touched") &&
        Expect(offersTheRest, "sidecars nothing points at should be offered, old names included") &&
        Expect(saysWhatIsKept, "the question should say how many were kept and why") &&
        Expect(refusingChangesNothing, "refusing should leave every file where it was") &&
        Expect(movedAside, "the first step should only rename, and only what is unclaimed") &&
        Expect(asksToDelete, "a set-aside sidecar still unclaimed should be offered for deletion") &&
        Expect(deleted, "the second step should delete only what it asked about");
}

bool RunSetAsideSidecarReturnTests()
{
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    // 치운 사이드카의 주인이 돌아오면 정체성이 돌아와야 한다. 되돌리지 않으면 돌아온 파일이 새
    // 정체성을 발급받고, 그것을 가리키던 참조는 전부 끊긴다 — 치우는 일이 잃게 만드는 셈이다.
    TemporaryDirectory temporaryDirectory("orphan-return");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::string guid = "0011223344556677889900aabbccddee";
    const std::filesystem::path projectFile = WriteSidecarProject(root, "Textures/Albedo.png");
    const bool prepared = !projectFile.empty() &&
        WriteFile(root / "Textures" / "Albedo.png.meta.orphan", Sidecar(guid));
    if (!Expect(prepared, "the returning asset test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "it should open"))
    {
        return false;
    }
    // 🔴 이름이 되돌아왔고, 그 에셋은 옛 정체성을 그대로 쓴다 — 새로 발급받지 않았다.
    const GameEngine::Assets::AssetDatabase* const database = context.GetProjectAssetDatabase();
    const GameEngine::Assets::Asset* const asset =
        database ? database->FindAsset("Textures/Albedo.png") : nullptr;
    const bool restored = std::filesystem::exists(root / "Textures" / "Albedo.png.meta") &&
        !std::filesystem::exists(root / "Textures" / "Albedo.png.meta.orphan") && asset &&
        asset->GetGuid().ToString() == guid;

    return Expect(
        restored, "a set-aside sidecar should come back when its asset does, identity and all");
}

bool RunMissingAssetDisplayTests()
{
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    // GUID가 가리키는 파일이 사라지면 32자리 16진수만으로는 파일 이름을 알 수 없다.
    // 남아 있는 사이드카의 이름을 인스펙터 표시의 단서로 사용한다.
    TemporaryDirectory temporaryDirectory("missing-asset-display");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::string guid = "abcdef0123456789abcdef0123456789";
    const std::filesystem::path projectFile = WriteSidecarProject(root, guid);
    std::error_code error;
    const bool prepared = !projectFile.empty() &&
        WriteFile(root / "Textures" / "Albedo.png.meta", Sidecar(guid)) &&
        std::filesystem::remove(root / "Textures" / "Albedo.png", error);
    if (!Expect(prepared, "the missing asset test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "it should open"))
    {
        return false;
    }
    const GameEngine::Assets::AssetReference reference =
        GameEngine::Assets::AssetReference::Parse(guid);
    const std::string shown = context.DescribeAssetReference(reference);
    // 🔴 사람이 읽는 것은 잃어버린 파일의 경로이지 정체성이 아니다.
    const bool readable = shown.find("Textures/Albedo.png") != std::string::npos &&
        shown.find("missing") != std::string::npos &&
        shown.find(guid) == std::string::npos;

    // 해석되는 참조는 평소대로 보인다 — 이 함수가 그 답을 가로채서는 안 된다.
    const GameEngine::Assets::AssetDatabase* const database = context.GetProjectAssetDatabase();
    const GameEngine::Assets::Asset* const scene =
        database ? database->FindAsset("Scenes/First.scene") : nullptr;
    const bool leavesOthersAlone = scene &&
        context.DescribeAssetReference(
            GameEngine::Assets::AssetReference(scene->GetGuid()))
            .find("Scenes/First.scene") != std::string::npos;

    return Expect(readable, "a reference whose file is gone should read as its old path") &&
        Expect(leavesOthersAlone, "a reference that resolves should still read as its asset");
}

bool RunSidecarIdentityIsNeverOverwrittenTests()
{
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());

    // 발급은 「정체성이 없는 에셋」에게만 준다. 그런데 그 판단은 데이터베이스가 하고,
    // 데이터베이스는 오래됐을 수 있다 — 사이드카가 방금 제자리로 돌아온 순간이 그렇다. 그때
    // 파일에 이미 적힌 정체성을 덮어쓰면 그것을 가리키던 참조가 전부 끊긴다.
    TemporaryDirectory temporaryDirectory("sidecar-identity-kept");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::string guid = "0f0e0d0c0b0a09080706050403020100";
    const std::filesystem::path projectFile = WriteSidecarProject(root, guid);
    // 사이드카는 정체성을 담고 있지만, 이 프로젝트가 열릴 때 데이터베이스는 아직 그것을 모른다.
    const bool prepared =
        !projectFile.empty() && WriteFile(root / "Textures" / "Albedo.png.meta", Sidecar(guid));
    if (!Expect(prepared, "the identity test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "it should open"))
    {
        return false;
    }
    const auto identityOf = [&context]
    {
        const GameEngine::Assets::AssetDatabase* const database =
            context.GetProjectAssetDatabase();
        const GameEngine::Assets::Asset* const asset =
            database ? database->FindAsset("Textures/Albedo.png") : nullptr;
        return asset ? asset->GetGuid().ToString() : std::string{};
    };
    const bool keptOnOpen = identityOf() == guid;

    // 🔴 재스캔을 몇 번 돌려도 같다. 발급이 도는 자리마다 이 성질이 필요하다.
    bool keptOnRescan = true;
    for (int attempt = 0; attempt < 3 && keptOnRescan; ++attempt)
    {
        keptOnRescan = context.RefreshProjectAssets() && identityOf() == guid;
    }
    std::ifstream stream(root / "Textures" / "Albedo.png.meta", std::ios::binary);
    const std::string text{ std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>() };
    const bool fileUnchanged = text.find(guid) != std::string::npos;

    return Expect(keptOnOpen, "an identity written in a sidecar should survive opening") &&
        Expect(keptOnRescan, "an identity written in a sidecar should survive every rescan") &&
        Expect(fileUnchanged, "the sidecar file should still carry the identity it had");
}

static const TestSupport::Registration gOrphanSidecarTests{
    "EditorDocument", "orphan sidecar tests should pass", RunOrphanSidecarTests };

static const TestSupport::Registration gSetAsideSidecarReturnTests{
    "EditorDocument", "set-aside sidecar return tests should pass", RunSetAsideSidecarReturnTests };

static const TestSupport::Registration gMissingAssetDisplayTests{
    "EditorDocument", "missing asset display tests should pass", RunMissingAssetDisplayTests };

static const TestSupport::Registration gSidecarIdentityIsNeverOverwrittenTests{
    "EditorDocument", "sidecar identity is never overwritten tests should pass", RunSidecarIdentityIsNeverOverwrittenTests };
