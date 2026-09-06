#include "ProjectIconFieldTests.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "../GameEngine/App/ProjectFile.h"
#include "../GameEngine/App/ProjectSettingsLoader.h"

#include "TestSupport.h"

using TestSupport::Expect;

bool RunProjectIconFieldTests()
{
    std::cout << "running project icon field tests\n";

    // 32자리 소문자 16진수 — 실제로 발급받은 것이 아니어도 된다. 이 시험이 재는 것은 값이
    // 왕복하는가이지, 그 값이 진짜 에셋을 가리키는가가 아니다.
    constexpr std::string_view iconGuid = "0123456789abcdef0123456789abcdef";

    GameEngine::App::ProjectSettings withIcon;
    withIcon.projectName = L"IconTest";
    withIcon.icon = iconGuid;
    withIcon.scenePaths.emplace(0u, "Scenes/Main.scene");
    const std::string writtenWithIcon = GameEngine::App::ProjectFile::Serialize(withIcon);

    GameEngine::App::ProjectSettings withoutIcon = withIcon;
    withoutIcon.icon.clear();
    const std::string writtenWithout = GameEngine::App::ProjectFile::Serialize(withoutIcon);

    const bool rewriteKeepsTheIcon =
        writtenWithIcon.find("\"icon\": \"" + std::string(iconGuid) + "\"") != std::string::npos;
    const bool rewriteAddsNothingByDefault =
        writtenWithout.find("\"icon\"") == std::string::npos;

    // 쓴 것을 다시 읽으면 같은 값이 나오는지까지 본다 — 직렬화만 재면 파싱 쪽의 어긋남을
    // 놓친다.
    const std::span<const std::byte> writtenBytes{
        reinterpret_cast<const std::byte*>(writtenWithIcon.data()), writtenWithIcon.size() };
    const std::optional<GameEngine::App::ProjectSettings> readBack =
        GameEngine::App::ProjectSettingsLoader::Load(writtenBytes, {});
    const bool roundTrips = readBack && readBack->icon == iconGuid;

    return Expect(rewriteKeepsTheIcon, "rewriting a project should keep the icon it was written with") &&
        Expect(
            rewriteAddsNothingByDefault,
            "and should not add the field to a project that never had one") &&
        Expect(roundTrips, "reading a written icon back should give the same guid");
}

static const TestSupport::Registration gProjectIconFieldTests{
    "AssetDatabase", "project icon field tests should pass", RunProjectIconFieldTests };
