#include "AssetIdentityIssueTests.h"

#include <filesystem>
#include <string>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetIdentityIssue.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

bool RunAssetIdentityIssueTests()
{
    namespace Assets = GameEngine::Assets;

    TemporaryDirectory temporaryDirectory("identity-issue");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    // 넷을 둔다: 사이드카가 없는 것, 이미 정체성이 있는 것, 설정은 있는데 정체성이 없는 것,
    // 그리고 옛 이름의 사이드카를 가진 것.
    const std::string keptGuid = "0a1b2c3d4e5f60718293a4b5c6d7e8f9";
    const std::string kept = R"({"format":"gameengine-meta/1","guid":")" + keptGuid +
        R"(","pixelsPerUnit":7.0})";
    const bool wrote =
        WriteFile(root / "Identity.gameproject", "{}") &&
        WriteFile(root / "Bare.png", "png-data") &&
        WriteFile(root / "Kept.png", "png-data-two") &&
        WriteFile(root / "Kept.png.meta", kept) &&
        WriteFile(root / "Settings.png", "png-data-three") &&
        WriteFile(root / "Settings.png.meta", R"({"pixelsPerUnit": 33.0})") &&
        WriteFile(root / "Old.png", "png-data-four") &&
        WriteFile(root / "Old.png.sprite.json", R"({"pixelsPerUnit": 9.0})");
    if (!Expect(wrote, "the identity issue test project should be written"))
    {
        return false;
    }

    // 🔴 편집기는 여기 없다. 데이터베이스와 콘텐츠 소스뿐이고, 그것이 이 단위의 요점이다.
    const GameEngine::Platform::DirectoryContentSource content(root);
    Assets::AssetDatabase database;
    if (!Expect(database.Refresh(content), "the test project should scan"))
    {
        return false;
    }

    const std::size_t issued = Assets::IssueMissingIdentities(database, content);

    // 사이드카가 없던 것에는 놓인다. 판본과 형식의 기본값도 함께.
    const std::string bare = ReadFile(root / "Bare.png.meta");
    const bool wroteMissing = bare.find("gameengine-meta/1") != std::string::npos &&
        bare.find("guid") != std::string::npos &&
        bare.find("pixelsPerUnit") != std::string::npos;

    // 🔴 이미 정체성이 있는 것은 바이트가 그대로다. 다시 발급하면 그것을 가리키던 참조가 전부
    // 아무것도 가리키지 않게 된다 — 이 단위에서 가장 무서운 실패다.
    const bool keptUntouched = ReadFile(root / "Kept.png.meta") == kept;

    // 설정만 있던 것은 그 파일에 정체성을 받고, 적어 둔 값은 남는다.
    const std::string settings = ReadFile(root / "Settings.png.meta");
    const bool gainedIdentityKeepingSettings = settings.find("guid") != std::string::npos &&
        settings.find("33") != std::string::npos;

    // 옛 이름의 것도 그 파일에 정체성을 받는다. 새 이름의 파일이 생기지는 않는다.
    const std::string old = ReadFile(root / "Old.png.sprite.json");
    const bool legacyGainedIdentityInPlace = old.find("guid") != std::string::npos &&
        old.find("9") != std::string::npos &&
        !std::filesystem::exists(root / "Old.png.meta");

    // 다시 읽으면 발급된 것들이 정체성으로 조회된다 — 부르는 쪽이 다시 스캔해야 한다는 계약이다.
    Assets::AssetDatabase reread;
    const bool rescanned = reread.Refresh(content);
    bool everyAssetIdentified = rescanned;
    if (rescanned)
    {
        for (const auto& asset : reread.GetAssets())
        {
            everyAssetIdentified = everyAssetIdentified && asset->GetGuid().IsValid();
        }
    }

    // 🔴 두 번째 발급은 아무것도 쓰지 않는다. 그러지 않으면 스캔과 발급이 서로를 부른다.
    const std::size_t second = Assets::IssueMissingIdentities(reread, content);

    return Expect(issued > 0, "a project with no identities should receive them") &&
        Expect(wroteMissing, "an asset with no sidecar should get one carrying an identity") &&
        Expect(keptUntouched, "an identity already recorded should be left byte for byte") &&
        Expect(
            gainedIdentityKeepingSettings,
            "a sidecar holding settings but no identity should gain one and keep them") &&
        Expect(
            legacyGainedIdentityInPlace,
            "a sidecar under the old name should gain its identity in place") &&
        Expect(everyAssetIdentified, "after a rescan every asset should carry an identity") &&
        Expect(second == 0, "a second pass should issue nothing");
}

static const TestSupport::Registration gAssetIdentityIssueTests{
    "AssetDatabase", "asset identity issue tests should pass", RunAssetIdentityIssueTests };
