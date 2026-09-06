#include "EditorLayerTests.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;

namespace
{
    /// <summary>
    /// 편집기의 층이다. 위에 있을수록 아래 층이며, 한 단위는 자기 층이나 그 아래만 include할 수
    /// 있다.
    ///
    /// 이름은 PROJECT_STRUCTURE.md의 설명과 같아야 한다 — 두 곳이 다른 이름을 쓰면 사람은 문서를
    /// 읽고 시험은 다른 것을 지킨다.
    ///
    /// <b>층은 의미가 놓고, 그래프는 검사만 한다.</b> 지역 include가 없다는 것은 어디에 두어도
    /// 된다는 뜻이지 맨 아래라는 뜻이 아니다 — 문서의 상태 조각들(설정 저장소, 플레이
    /// 세션)은 지역 include가 없지만 문서의 상태이므로 Document다. 그래프가 답하는 것은 「이
    /// 자리가 가능한가」뿐이고, 「어느 자리인가」는 그것이 무엇인지가 답한다. 그래서 자리는
    /// 파일이 선언하고, 이 시험은 그 선언이 가능한지를 묻는다.
    /// </summary>
    struct EditorLayerName
    {
        int layer = 0;
        std::string_view name;
        std::string_view what;
    };

    constexpr std::array LayerNames{
        EditorLayerName{ 0, "Rules", "데이터를 받아 답하는 결정과 배치." },
        EditorLayerName{ 1, "Document", "열려 있는 것과 그것에 할 수 있는 일." },
        EditorLayerName{ 2, "Views", "패널과 도구, 그리고 그것들을 그리는 뷰." },
        EditorLayerName{ 3, "Shell", "뷰들을 소유하는 프레임과 그것을 세우는 부트스트랩." },
    };

    /// <summary>한 단위가 선언한 자리다. 선언이 없으면 <c>layer</c>가 비어 있다.</summary>
    struct DeclaredUnit
    {
        std::string unit;
        std::filesystem::path declaringFile;
        std::filesystem::path directory;
        int layer = -1;
    };

    [[nodiscard]] std::filesystem::path EditorSourceRoot()
    {
        // 이 파일은 <repo>/GameEngineTests/EditorLayerTests.cpp에 있다.
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "GameEditor" /
            "Source";
    }

    [[nodiscard]] const EditorLayerName* FindLayer(const int layer)
    {
        const auto found = std::ranges::find(LayerNames, layer, &EditorLayerName::layer);
        return found == LayerNames.end() ? nullptr : &*found;
    }

    /// <summary>한 파일에서 읽어 낸 자리 표시다. <c>count</c>가 0이면 표시가 없다.</summary>
    struct LayerMark
    {
        int count = 0;
        int layer = -1;
        std::string name;
    };

    /// <summary>
    /// 파일이 적어 둔 자리를 읽는다. 표시는 <c>// editor-layer: 2 (Views)</c> 꼴이며, 숫자와
    /// 이름을 함께 적게 한 이유는 <b>파일을 연 사람도 답을 얻게</b> 하기 위해서다 — 숫자만
    /// 있으면 다른 곳을 찾아봐야 한다. 둘이 어긋나면 이 시험이 잡는다.
    ///
    /// 표시를 <b>세는</b> 것은 둘 이상 적힌 파일을 잡기 위해서다. 두 번 적힌 자리는 언젠가
    /// 하나만 고쳐지고, 그때 어느 쪽이 참인지 답할 수 있는 사람이 없다.
    /// </summary>
    [[nodiscard]] LayerMark ReadLayerMark(const std::filesystem::path& file)
    {
        static const std::regex markPattern{ R"raw(//\s*editor-layer:\s*(\d+)\s*\(([^)]*)\))raw" };
        const std::string text = ReadFile(file);
        LayerMark mark;
        for (std::sregex_iterator match(text.begin(), text.end(), markPattern), end; match != end;
            ++match)
        {
            ++mark.count;
            mark.layer = std::stoi((*match)[1].str());
            mark.name = (*match)[2].str();
        }
        return mark;
    }

    /// <summary>
    /// 편집기 소스의 단위들을 모은다. 한 단위는 헤더 하나가 보통이고, 헤더 없이 구현만 있는
    /// 것도 있다(등록 지점이 그렇다). 표가 아니라 <b>디렉터리가 목록</b>이므로, 파일을 더하면
    /// 그 순간 이 시험의 물음 대상이 된다.
    ///
    /// 소스는 층별 하위 디렉토리(Rules/Document/Views/Shell)로 나뉘어 있어 <b>재귀로</b>
    /// 훑는다. 그래서 단위 이름만이 아니라 <b>그 단위가 실제로 있는 디렉토리</b>도 함께
    /// 돌려준다 — 헤더와 구현의 자리를 다시 물을 때 "루트 바로 아래"라고 가정하면 안 된다.
    /// </summary>
    [[nodiscard]] std::map<std::string, std::filesystem::path> CollectUnits(
        const std::filesystem::path& root, std::error_code& error)
    {
        std::map<std::string, std::filesystem::path> units;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(root, error))
        {
            const std::filesystem::path& path = entry.path();
            if (path.extension() != ".h" && path.extension() != ".cpp")
            {
                continue;
            }
            units.emplace(path.stem().string(), path.parent_path());
        }
        return units;
    }
}

bool RunEditorLayerTests()
{
    const std::filesystem::path root = EditorSourceRoot();
    std::error_code error;
    const std::map<std::string, std::filesystem::path> unitDirectories =
        CollectUnits(root, error);
    if (!Expect(!error && !unitDirectories.empty(), "the editor's sources should be readable"))
    {
        return false;
    }

    // 모든 편집기 단위는 자기 파일에서 계층을 선언해야 한다.
    // 선언과 include 관계를 함께 검사해 허용된 의존 방향을 확인한다.
    std::vector<DeclaredUnit> declared;
    bool everyUnitDeclaresItsLayer = true;
    for (const auto& [unit, unitDirectory] : unitDirectories)
    {
        const std::filesystem::path header = unitDirectory / (unit + ".h");
        const std::filesystem::path implementation = unitDirectory / (unit + ".cpp");
        const bool hasHeader = std::filesystem::is_regular_file(header, error);
        const std::filesystem::path declaring = hasHeader ? header : implementation;

        const LayerMark mark = ReadLayerMark(declaring);
        if (mark.count == 0)
        {
            std::cerr << "FAILED: this editor file does not say which layer it is in. add"
                         " \"// editor-layer: <n> (<name>)\" near the top. file="
                      << declaring.filename().string() << '\n';
            everyUnitDeclaresItsLayer = false;
            continue;
        }
        if (mark.count > 1)
        {
            std::cerr << "FAILED: this editor file says its layer more than once; one of them will"
                         " go stale. file="
                      << declaring.filename().string() << '\n';
            everyUnitDeclaresItsLayer = false;
            continue;
        }

        const EditorLayerName* const named = FindLayer(mark.layer);
        if (named == nullptr)
        {
            std::cerr << "FAILED: this editor file claims a layer that does not exist. file="
                      << declaring.filename().string() << ", layer=" << mark.layer << '\n';
            everyUnitDeclaresItsLayer = false;
            continue;
        }
        if (mark.name != named->name)
        {
            std::cerr << "FAILED: the layer number and the name beside it disagree. file="
                      << declaring.filename().string() << ", says=\"" << mark.layer << " ("
                      << mark.name << ")\", layer " << named->layer << " is called " << named->name
                      << '\n';
            everyUnitDeclaresItsLayer = false;
            continue;
        }

        // 헤더가 있는 단위는 헤더가 자리를 말한다. 구현이 또 말하면 둘이 갈릴 수 있다.
        if (hasHeader && std::filesystem::is_regular_file(implementation, error) &&
            ReadLayerMark(implementation).count > 0)
        {
            std::cerr << "FAILED: this unit says its layer twice, in the header and in the"
                         " implementation; the header is where it belongs. unit="
                      << unit << '\n';
            everyUnitDeclaresItsLayer = false;
            continue;
        }

        declared.push_back(DeclaredUnit{ unit, declaring, unitDirectory, mark.layer });
    }

    // 헤더가 선언한 층과 실제 include 그래프를 대조한다.
    // 헤더가 0층을 선언해도 그 include가 층 규칙을 어기면 검사에 실패해야 한다.
    const std::regex includePattern{ R"raw(#include\s+"([^"]+)")raw" };
    const auto layerOf = [&declared](const std::string& unit) -> const DeclaredUnit*
    {
        const auto found = std::ranges::find(declared, unit, &DeclaredUnit::unit);
        return found == declared.end() ? nullptr : &*found;
    };

    bool everyIncludePointsDown = true;
    const auto checkIncludes = [&](const DeclaredUnit& from, const std::filesystem::path& file,
                                   const char* const what)
    {
        if (!std::filesystem::is_regular_file(file, error))
        {
            return;
        }
        const std::string text = ReadFile(file);
        for (std::sregex_iterator match(text.begin(), text.end(), includePattern), end;
            match != end; ++match)
        {
            const std::filesystem::path included((*match)[1].str());
            if (included.extension() != ".h")
            {
                continue;
            }
            const std::string other = included.stem().string();
            if (other == from.unit)
            {
                continue;
            }
            const DeclaredUnit* const to = layerOf(other);
            if (to == nullptr)
            {
                // 편집기 밖의 헤더다. 그것은 엔진 층 규칙이 지킨다.
                continue;
            }
            if (to->layer > from.layer)
            {
                std::cerr << "FAILED: this editor " << what << " includes a header from a higher "
                          << "layer. " << file.filename().string() << " (layer " << from.layer
                          << ") -> " << other << ".h (layer " << to->layer << ")\n";
                everyIncludePointsDown = false;
            }
        }
    };

    for (const DeclaredUnit& unit : declared)
    {
        checkIncludes(unit, unit.directory / (unit.unit + ".h"), "header");
        checkIncludes(unit, unit.directory / (unit.unit + ".cpp"), "implementation");
    }

    // 각 파일이 선언한 층을 모아 목록으로 출력한다. 목록의 근거는 현재 소스다.
    std::map<int, std::vector<std::string>> byLayer;
    for (const DeclaredUnit& unit : declared)
    {
        byLayer[unit.layer].push_back(unit.unit);
    }
    std::cout << "  the editor's layers, as its own files declare them:\n";
    for (const EditorLayerName& layer : LayerNames)
    {
        const auto found = byLayer.find(layer.layer);
        const std::size_t count = found == byLayer.end() ? 0 : found->second.size();
        std::cout << "    " << layer.layer << " — " << layer.name << " (" << count << "): "
                  << layer.what << '\n';
        if (found == byLayer.end())
        {
            continue;
        }
        std::ranges::sort(found->second);
        for (const std::string& unit : found->second)
        {
            std::cout << "      " << unit << '\n';
        }
    }

    return Expect(
               everyUnitDeclaresItsLayer,
               "every editor file should say which layer it is in, once, with a name that fits") &&
        Expect(
            everyIncludePointsDown,
            "an editor file should include only its own layer or one below");
}

static const TestSupport::Registration gEditorLayerTests{
    "Layering", "editor layer tests should pass", RunEditorLayerTests };
