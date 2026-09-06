#include "ProjectIconResolutionTests.h"

#include <filesystem>
#include <string>

#include "Build/CMakeLocation.h"
#include "TestSupport.h"

bool RunProjectIconResolutionTests()
{
    using TestSupport::Expect;
    const auto cmake = GameEngine::Build::ResolveCMakePath({}, {});
    if (!Expect(cmake.has_value(), "project icon resolution requires the configured CMake tool")) return false;
    const auto repository = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto resolver = repository / "cmake" / "ResolveProjectIcon.cmake";
    const TestSupport::TemporaryDirectory temporary("project-icon-resolution");
    constexpr auto guid = "0123456789abcdef0123456789abcdef";
    bool passed = true;
    for (int scenario = 0; scenario < 8; ++scenario)
    {
        const auto directory = temporary.GetPath() / std::to_string(scenario);
        const auto content = directory / "Content";
        const auto icon = content / "Renamed Folder" / "A different name.ico";
        const auto descriptor = content / "Project.gameproject";
        const auto resultPathFile = directory / "resolved-path.txt";
        const auto resultGuidFile = directory / "resolved-guid.txt";
        const auto script = directory / "resolve.cmake";
        const auto sidecar = std::filesystem::path(icon.string() + ".meta");
        std::string settings = "{\"projectName\":\"Icon fixture\",\"icon\":\"" + std::string(guid) + "\"}";
        if (scenario == 0) settings = "{\"projectName\":\"No icon\"}";
        if (scenario == 4) settings = "{\"icon\":\"not-an-asset-guid\"}";
        if (scenario == 6) settings = "{broken";
        if (scenario != 7 && !TestSupport::WriteFile(descriptor, settings)) return false;
        if (scenario == 1 || scenario == 2 || scenario == 3 || scenario == 5)
        {
            const std::string metadata = "{\"format\":\"gameengine-meta/1\",\"guid\":\"" +
                std::string(scenario == 2 ? "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" : guid) + "\"}";
            if (!TestSupport::WriteFile(sidecar, metadata)) return false;
            if (scenario != 5 && !TestSupport::WriteFile(icon, "resolver checks identity, RC validates image bytes")) return false;
            if (scenario == 3)
            {
                if (!TestSupport::WriteFile(content / "Duplicate.ico.meta", metadata) ||
                    !TestSupport::WriteFile(content / "Duplicate.ico", "duplicate fixture")) return false;
            }
        }
        const std::string commands = "cmake_minimum_required(VERSION 3.28)\ninclude([==[" + resolver.generic_string() +
            "]==])\ngameengine_resolve_project_icon([==[" + content.generic_string() +
            "]==] icon_path icon_guid)\nfile(WRITE [==[" + resultPathFile.generic_string() +
            "]==] \"${icon_path}\")\nfile(WRITE [==[" + resultGuidFile.generic_string() +
            "]==] \"${icon_guid}\")\n";
        if (!TestSupport::WriteFile(script, commands)) return false;
        const auto result = TestSupport::RunCommand(*cmake, { "-P", script.string() });
        const bool expectedSuccess = scenario == 0 || scenario == 1 || scenario == 7;
        passed &= Expect((result.exitCode == 0) == expectedSuccess,
            ("icon resolution should accept only unambiguous valid project references: scenario " +
                std::to_string(scenario) + "\n" + result.output).c_str());
        if (expectedSuccess)
        {
            // Keep values separate: CMake's file(WRITE) emits native CRLF on Windows,
            // while ReadFile intentionally preserves bytes without newline conversion.
            const std::string expectedPath = scenario == 1 ? icon.generic_string() : "";
            const std::string expectedGuid = scenario == 1 ? guid : "";
            passed &= Expect(TestSupport::ReadFile(resultPathFile) == expectedPath &&
                    TestSupport::ReadFile(resultGuidFile) == expectedGuid,
                ("icon GUID must resolve by sidecar identity, regardless of asset name or folder: scenario " +
                    std::to_string(scenario)).c_str());
        }
    }
    return passed;
}

static const TestSupport::Registration gProjectIconResolutionTests{
    "ProjectBuilder", "project icon GUID resolution tests should pass", RunProjectIconResolutionTests };
