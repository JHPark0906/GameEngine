#include "BuildOutputRootTests.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Core/Json.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;

namespace
{
    /// <summary>빌드 트리 하나가 만드는 자리다.</summary>
    struct OutputPlace
    {
        std::string root;
        std::string configuration;

        [[nodiscard]] bool operator<(const OutputPlace& other) const
        {
            return root == other.root ? configuration < other.configuration : root < other.root;
        }
    };

    /// <summary>
    /// 구성을 고정하지 않은 프리셋은 여러 구성을 만드는 트리다. 그 트리는 자기 루트 아래의
    /// 모든 구성을 쓰므로, 어느 구성에서든 남과 겹치면 겹치는 것이다.
    /// </summary>
    constexpr std::string_view EveryConfiguration = "<every configuration>";

    [[nodiscard]] std::string TextOf(const GameEngine::Core::Json& value)
    {
        return value.IsString() ? std::string(value.AsString()) : std::string();
    }
}

bool RunBuildOutputRootTests()
{
    // 이 파일은 <repo>/GameEngineTests/BuildOutputRootTests.cpp에 있다.
    const std::filesystem::path repository =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::string text = ReadFile(repository / "CMakePresets.json");
    if (!Expect(!text.empty(), "the repository should have a CMakePresets.json"))
    {
        return false;
    }

    const GameEngine::Core::Json presets = GameEngine::Core::Json::Parse(text);
    const GameEngine::Core::Json* const configurePresets =
        presets.IsObject() ? presets.Find("configurePresets") : nullptr;
    if (!Expect(
            configurePresets != nullptr && configurePresets->IsArray(),
            "CMakePresets.json should list configure presets"))
    {
        return false;
    }

    // 프리셋이 정하지 않으면 루트는 CMakeLists의 기본값이다. 이름을 그대로 쓰는 것으로 충분하다:
    // 여기서 묻는 것은 경로가 무엇이냐가 아니라 <b>두 프리셋이 같은 것을 가리키느냐</b>이다.
    constexpr std::string_view DefaultRoot = "${sourceDir}/x64";
    std::map<OutputPlace, std::vector<std::string>> places;
    for (const GameEngine::Core::Json& preset : configurePresets->AsArray())
    {
        if (!preset.IsObject())
        {
            continue;
        }
        const GameEngine::Core::Json* const name = preset.Find("name");
        const GameEngine::Core::Json* const cache = preset.Find("cacheVariables");
        std::string root(DefaultRoot);
        std::string configuration(EveryConfiguration);
        if (cache != nullptr && cache->IsObject())
        {
            if (const GameEngine::Core::Json* const declared = cache->Find("GAMEENGINE_OUTPUT_ROOT"))
            {
                root = TextOf(*declared);
            }
            if (const GameEngine::Core::Json* const buildType = cache->Find("CMAKE_BUILD_TYPE"))
            {
                configuration = TextOf(*buildType);
            }
        }
        places[OutputPlace{ root, configuration }].push_back(name ? TextOf(*name) : std::string());
    }

    bool everyTreeHasItsOwnPlace = !places.empty();
    for (const auto& [place, presetNames] : places)
    {
        // 구성을 고정하지 않은 트리는 그 루트의 모든 구성을 쓴다. 같은 루트를 쓰는 다른 트리가
        // 하나라도 있으면 겹친다.
        std::size_t sharing = presetNames.size();
        if (place.configuration == EveryConfiguration)
        {
            for (const auto& [other, otherNames] : places)
            {
                if (other.root == place.root && other.configuration != place.configuration)
                {
                    sharing += otherNames.size();
                }
            }
        }
        if (sharing > 1)
        {
            std::cerr << "FAILED: these build trees write the same place. root=" << place.root
                      << ", configuration=" << place.configuration << ", presets=";
            for (const std::string& presetName : presetNames)
            {
                std::cerr << ' ' << presetName;
            }
            std::cerr << '\n';
            everyTreeHasItsOwnPlace = false;
        }
    }

    // Visual Studio 트리는 x64에 남는다. 스크립트와 문서가 그 경로를 이름으로 부르고, 사람이
    // 「빌드」라고 할 때 뜻하는 것이 그것이다 — 옮기면 그 전부가 조용히 다른 것을 가리킨다.
    const bool visualStudioKeepsX64 =
        places.contains(OutputPlace{ std::string(DefaultRoot), std::string(EveryConfiguration) });

    return Expect(
            everyTreeHasItsOwnPlace,
            "no two build trees should write the same output root and configuration") &&
        Expect(
            visualStudioKeepsX64,
            "the multi-configuration Visual Studio tree should still build into x64");
}

static const TestSupport::Registration gBuildOutputRootTests{
    "Core", "build output root tests should pass", RunBuildOutputRootTests };
