#include "LayeringTests.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 한 모듈이 층 표에서 갖는 성질이다. 층 번호만으로는 표현할 수 없는 것이 하나 있기 때문에
    /// 있다: 구현 모듈은 자기보다 위에 있는 누구에게나 열려 있는 것이 아니라, 지정된 소비자에게만
    /// 열려 있다.
    /// </summary>
    enum class ModuleKind
    {
        /// <summary>층 규칙만 지키면 누구나 도달할 수 있는 모듈이다.</summary>
        Shared,

        /// <summary>
        /// 한 플랫폼이나 한 그래픽 API에 묶인 구현이다. 아래층이라서 층 규칙으로는 모두가
        /// 내려다볼 수 있지만, 실제로는 <see cref="ImplementationConsumers"/>에 지정된 자리만
        /// 도달할 수 있어야 한다. 이식성이 이 성질에 걸려 있다: 이것이 새는 순간 포팅 비용은
        /// "구현 하나와 분기 하나"에서 "이 모듈을 아는 모든 자리를 찾아 고치기"로 바뀐다.
        /// </summary>
        Implementation,
    };

    /// <summary>층 표의 한 행이다.</summary>
    struct ModuleRule
    {
        /// <summary>엔진 루트 기준 디렉터리 이름이다. 예: "Rendering/D3D11".</summary>
        std::string name;

        /// <summary>낮을수록 아래층이다. 한 모듈은 자기보다 낮은 층만 포함할 수 있다.</summary>
        int layer;

        /// <summary>도달할 수 있는 자리가 층 규칙으로 정해지는지, 명단으로 정해지는지다.</summary>
        ModuleKind kind;
    };

    /// <summary>
    /// 엔진 모듈의 층이다. 낮은 층이 앞이다. 한 모듈은 자기와 같거나 낮은 층만 포함할 수 있다.
    /// 같은 인덱스의 모듈은 서로 독립인 형제다(D3D11과 D3D12).
    ///
    /// 이 표는 PROJECT_STRUCTURE.md의 "Dependency direction"과 같은 것이어야 한다. 문서가
    /// 주장을 적고, 이 테스트가 그것을 증명한다.
    /// </summary>
    const std::vector<ModuleRule> Layers{
        { "Diagnostics", 0, ModuleKind::Shared },
        { "Math", 1, ModuleKind::Shared },
        { "Core", 2, ModuleKind::Shared },
        // Math만 필요하고, 이것을 내려다볼 필요가 있는 자리는 Runtime(9층)뿐이라 그보다 훨씬
        // 아래다. Core와 같은 층인 이유는 D3D11·D3D12처럼 서로 독립된 형제이기 때문이다 —
        // Core도 Animation도 서로를 모른다.
        { "Animation", 2, ModuleKind::Shared },
        { "Platform", 3, ModuleKind::Shared },
        { "UIModel", 3, ModuleKind::Shared },
        { "Platform/Win32", 4, ModuleKind::Implementation },
        // Text는 Platform 인터페이스를 구현하는 4층 구현 모듈이다.
        // 래스터라이저는 PlatformServices 팩토리에서만 생성하며 다른 모듈의 직접 참조를 금지한다.
        { "Text", 4, ModuleKind::Implementation },
        { "Assets", 5, ModuleKind::Shared },
        { "Rendering", 6, ModuleKind::Shared },
        { "Rendering/Direct3D", 7, ModuleKind::Shared },
        { "Rendering/D3D11", 8, ModuleKind::Implementation },
        { "Rendering/D3D12", 8, ModuleKind::Implementation },
        { "Runtime", 9, ModuleKind::Shared },
        { "SceneRendering", 10, ModuleKind::Shared },
        { "Serialization", 11, ModuleKind::Shared },
        { "UI", 12, ModuleKind::Shared },
        { "App", 13, ModuleKind::Shared },
        { "Build", 14, ModuleKind::Shared },
        { "Player", 15, ModuleKind::Shared },
    };

    /// <summary>
    /// 구현 모듈에 도달하도록 지정된 자리들이다. 각 항목은 (소비자, 구현 모듈) 쌍이며, 소비자가
    /// 슬래시로 끝나면 그 디렉터리 아래 전부를, 아니면 그 파일 하나만 가리킨다. 경로는 저장소
    /// 루트가 아니라 엔진 루트 기준이되, 애플리케이션 검사에서는 저장소 루트 기준이다.
    ///
    /// 두 가지를 동시에 정한다. 층을 거슬러 올라가는 것이 허용된 유일한 자리들이고 — 플랫폼
    /// 중립 모듈이 자기 플랫폼 구현을 만들고, 렌더링 코어가 백엔드 목록을 만드는 팩토리 파일들
    /// — 동시에 구현 모듈을 내려다보는 것이 허용된 유일한 자리들이다. 후자가 없으면 층 규칙은
    /// 침묵한다: Platform/Win32는 4층, 두 백엔드는 8층이라 그 위의 모든 모듈에게 "아래"이고,
    /// App이 D3D11GraphicsDevice.h를 포함해도 하향 포함이라 통과했을 것이다.
    ///
    /// 여기에 파일을 더하는 것은 설계 결정이므로 문서와 함께 바뀌어야 한다.
    /// </summary>
    const std::vector<std::pair<std::string, std::string>> ImplementationConsumers{
        // 플랫폼 구현을 이름 부르는 두 팩토리다.
        { "Platform/PlatformServices.cpp", "Platform/Win32" },
        { "Platform/WindowFactory.cpp", "Platform/Win32" },
        // 텍스트 래스터라이저도 이 팩토리에서 만든다. 구현을 선택하는 곳은 하나뿐이어야 한다.
        { "Platform/PlatformServices.cpp", "Text" },
        // 진입점은 운영체제가 부르는 서명을 선언하므로 그 플랫폼의 진단을 함께 쓴다.
        { "Player/Win32PlayerEntry.cpp", "Platform/Win32" },
        // Direct3D 계열은 이미 Windows에 묶여 있다. 포팅은 이들을 통째로 대체하지 방향을 바꾸지
        // 않으므로, Win32::LogHResult를 이름 부르는 것이 포팅 비용을 늘리지 않는다.
        { "Rendering/Direct3D/", "Platform/Win32" },
        { "Rendering/D3D11/", "Platform/Win32" },
        { "Rendering/D3D12/", "Platform/Win32" },
        // 백엔드를 이름 부르는 유일한 자리다.
        { "Rendering/GraphicsBackendRegistry.cpp", "Rendering/D3D11" },
        { "Rendering/GraphicsBackendRegistry.cpp", "Rendering/D3D12" },
    };

    /// <summary>
    /// 엔진 라이브러리 밖에서 엔진 헤더를 포함하는 곳들이다. 저장소 루트 기준 경로다.
    /// </summary>
    const std::vector<std::string> ApplicationRoots{
        "GameEditor/Source",
        "GameBuilder",
        "SampleGame/Source",
    };

    /// <summary>
    /// 플랫폼 중립 헤더에서 넓은 문자 타입을 찾는 패턴이다. 계약에 쓰이면 그 계약은 UTF-16을
    /// 쓰는 플랫폼의 것이 된다.
    /// </summary>
    const std::regex WideCharacterPattern{
        R"raw((^|[^A-Za-z0-9_])(wchar_t|std::wstring[A-Za-z0-9_]*|LPWSTR|LPCWSTR))raw" };

    /// <summary>
    /// 직렬화 호환성을 위한 projectName과 허용 목록의 네 헤더만 와이드 문자 계약을 갖는다.
    /// 새로운 공개 계약은 UTF-8을 사용하며 예외 목록을 임의로 늘리지 않는다.
    /// </summary>
    const std::vector<std::string> WideCharacterContracts{
        "App/ProjectFile.h",     // .gameproject 확장자 리터럴
        "App/ProjectSettings.h", // projectName — .gameproject에 직렬화된다
        "Platform/IWindow.h",    // WindowDescription::title
        "Platform/PlatformServices.h", // FileDialogRequest의 네 필드
    };

    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        // 이 파일은 <repo>/GameEngineTests/LayeringTests.cpp에 있다.
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    [[nodiscard]] std::filesystem::path EngineRoot()
    {
        return RepositoryRoot() / "GameEngine";
    }

    [[nodiscard]] const ModuleRule* FindModule(const std::string& name)
    {
        const auto found = std::ranges::find(Layers, name, &ModuleRule::name);
        return found == Layers.end() ? nullptr : &*found;
    }

    /// <summary>
    /// 표가 하위 모듈을 두고 있는 최상위 디렉터리인지다. Platform과 Rendering이 그렇다. 이런
    /// 디렉터리 바로 아래에는 표에 등재된 이름만 올 수 있다.
    /// </summary>
    [[nodiscard]] bool IsContainerModule(const std::string& name)
    {
        return std::ranges::any_of(Layers, [&](const ModuleRule& rule)
        {
            const std::size_t slash = rule.name.find('/');
            return slash != std::string::npos && rule.name.compare(0, slash, name) == 0;
        });
    }

    /// <summary>
    /// 엔진 루트 기준 경로가 속한 모듈이다. 표에 없는 이름을 돌려줄 수 있으며, 호출자는 그것을
    /// 미등재 모듈로 보고한다.
    ///
    /// Platform과 Rendering 바로 아래의 디렉터리는 표에 없으면 부모로 흡수되지 않는다. 흡수되면
    /// Rendering/Vulkan을 만드는 것만으로 새 백엔드가 6층 "Rendering"이 되어
    /// GraphicsBackendRegistry.cpp와 같은 모듈로 보이고, seam 등재라는 설계 결정을 거치지 않고
    /// 통과했을 것이다.
    /// </summary>
    [[nodiscard]] std::string ModuleOf(const std::filesystem::path& relative)
    {
        // "Rendering/D3D11/x.h" -> "Rendering/D3D11", "Math/Vector.h" -> "Math", "pch.h" -> ""
        std::vector<std::string> parts;
        for (const auto& part : relative.parent_path())
        {
            parts.push_back(part.generic_string());
        }
        if (parts.empty())
        {
            return {};
        }
        if (parts.size() >= 2)
        {
            const std::string nested = parts[0] + "/" + parts[1];
            if (FindModule(nested) != nullptr || IsContainerModule(parts[0]))
            {
                return nested;
            }
        }
        return parts[0];
    }

    [[nodiscard]] int LayerOf(const std::string& module)
    {
        const ModuleRule* const rule = FindModule(module);
        return rule == nullptr ? -1 : rule->layer;
    }

    [[nodiscard]] bool IsImplementationModule(const std::string& module)
    {
        const ModuleRule* const rule = FindModule(module);
        return rule != nullptr && rule->kind == ModuleKind::Implementation;
    }

    /// <summary>
    /// 이 모듈이 이미 한 플랫폼에 묶여 있는지다. 구현 모듈과, PROJECT_STRUCTURE.md가 그것들과
    /// 같은 부류로 묶는 Direct3D 계열 공용 코드다 — 포팅이 방향을 바꾸는 것이 아니라 통째로
    /// 대체하는 코드이므로, 여기서는 그 플랫폼의 문자 타입을 써도 이식 비용이 늘지 않는다.
    /// </summary>
    [[nodiscard]] bool IsPlatformBoundModule(const std::string& module)
    {
        const ModuleRule* const rule = FindModule(module);
        return (rule != nullptr && rule->kind == ModuleKind::Implementation) ||
            module == "Rendering/Direct3D";
    }

    /// <summary>주석 줄인지다. 이 저장소의 주석은 전부 //로 시작한다.</summary>
    [[nodiscard]] bool IsCommentLine(const std::string& line)
    {
        const std::size_t first = line.find_first_not_of(" \t");
        return first != std::string::npos && line.compare(first, 2, "//") == 0;
    }

    /// <summary>이 파일이 이 구현 모듈에 도달하도록 지정됐는지다.</summary>
    [[nodiscard]] bool IsDesignatedConsumer(const std::string& file, const std::string& module)
    {
        return std::ranges::any_of(ImplementationConsumers, [&](const auto& entry)
        {
            if (entry.second != module)
            {
                return false;
            }
            return entry.first.ends_with("/")
                ? file.starts_with(entry.first)
                : file == entry.first;
        });
    }

    /// <summary>
    /// 포함 대상 문자열이 어느 구현 모듈 안을 가리키면 그 모듈 이름이고, 아니면 빈 문자열이다.
    /// </summary>
    [[nodiscard]] std::string ImplementationModuleNamedBy(const std::string& includeTarget)
    {
        for (const ModuleRule& rule : Layers)
        {
            if (rule.kind == ModuleKind::Implementation &&
                includeTarget.find(rule.name + "/") != std::string::npos)
            {
                return rule.name;
            }
        }
        return {};
    }

}

/// <summary>
/// 엔진 소스의 #include 그래프가 층 표를 지키는지 확인한다. 순환은 여기서 잡힌다 — 문서가
/// "의존은 한 방향"이라고 말하는 것만으로는 실제 그래프가 그렇게 되지 않는다.
///
/// 두 가지를 함께 본다. 층을 거스르는 포함이 없어야 하고, 구현 모듈 — 한 플랫폼이나 한 그래픽
/// API에 묶인 디렉터리 — 은 지정된 소비자에게만 보여야 한다. 두 번째가 없으면 이식성의 가장
/// 중요한 불변식이 문서의 주장으로만 남는다.
/// </summary>
bool RunLayeringTests()
{
    const std::filesystem::path root = EngineRoot();
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
    {
        std::cout << "  layering tests skipped: engine sources not found at " << root.string() << "\n";
        return true;
    }

    const std::regex includePattern(R"raw(^\s*#\s*include\s*"([^"]+)")raw");
    std::vector<std::string> violations;
    std::set<std::string> unknownModules;
    std::size_t scanned = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        const std::string extension = path.extension().string();
        if (extension != ".h" && extension != ".cpp")
        {
            continue;
        }
        const std::filesystem::path relative = std::filesystem::relative(path, root, error);
        if (relative.generic_string().starts_with("x64/"))
        {
            continue;
        }
        const std::string module = ModuleOf(relative);
        if (module.empty())
        {
            continue; // pch.h and other root files
        }
        const int layer = LayerOf(module);
        if (layer < 0)
        {
            unknownModules.insert(module);
            continue;
        }
        ++scanned;

        std::ifstream stream(path);
        std::string line;
        while (std::getline(stream, line))
        {
            std::smatch match;
            if (!std::regex_search(line, match, includePattern))
            {
                continue;
            }
            const std::string target = match[1].str();
            if (target == "pch.h")
            {
                continue;
            }
            // 포함 경로를 이 파일 기준으로 풀어 엔진 루트 기준 상대 경로로 만든다.
            const std::filesystem::path resolved =
                std::filesystem::weakly_canonical(path.parent_path() / target, error);
            const std::filesystem::path targetRelative = std::filesystem::relative(resolved, root, error);
            if (error || targetRelative.empty() || targetRelative.generic_string().starts_with(".."))
            {
                continue; // 엔진 밖(표준 헤더처럼 따옴표로 포함된 것)은 관심 밖이다.
            }
            const std::string targetModule = ModuleOf(targetRelative);
            if (targetModule.empty() || targetModule == module)
            {
                continue;
            }
            const int targetLayer = LayerOf(targetModule);
            if (targetLayer < 0)
            {
                unknownModules.insert(targetModule);
                continue;
            }
            const bool sibling = targetLayer == layer;
            const bool upward = targetLayer > layer;
            // 구현 모듈은 아래층이어도 아무나 볼 수 없다. 방향과 무관하게 명단으로 판정한다.
            const bool restricted = IsImplementationModule(targetModule);
            if (!sibling && !upward && !restricted)
            {
                continue;
            }
            const std::string relativeText = relative.generic_string();
            if (IsDesignatedConsumer(relativeText, targetModule))
            {
                continue;
            }
            const std::string reason = restricted
                ? " (" + targetModule +
                    " is an implementation module; only its designated consumers may include it)"
                : " (" + module + " may not include " + targetModule + ")";
            violations.push_back(relativeText + " -> " + targetRelative.generic_string() + reason);
        }
    }

    for (const std::string& violation : violations)
    {
        std::cerr << "  layering: " << violation << "\n";
    }
    for (const std::string& module : unknownModules)
    {
        std::cerr << "  layering: module not in the layer table: " << module << "\n";
    }

    return Expect(scanned > 50, "the engine sources should have been scanned") &&
        Expect(unknownModules.empty(), "every engine module should be in the layer table") &&
        Expect(violations.empty(), "no engine file should reach a module the layer table denies it");
}

/// <summary>
/// 엔진 밖의 애플리케이션 — 에디터, 빌드 도구, 샘플 게임 — 이 구현 모듈을 이름 부르지 않는지
/// 확인한다.
///
/// 이들은 엔진 루트를 포함 경로로 가지므로 "Rendering/D3D11/D3D11GraphicsDevice.h"라고 적기만
/// 하면 백엔드에 닿는다. 층 검사는 엔진 소스만 훑어 여기를 보지 못하므로, 이것이 없으면
/// "애플리케이션 진입점은 구체 그래픽 장치를 만들거나 포함하지 않는다"는 PROJECT_STRUCTURE.md의
/// 주장이 증명되지 않은 채로 남는다.
/// </summary>
bool RunApplicationIsolationTests()
{
    const std::filesystem::path repository = RepositoryRoot();
    const std::regex includePattern(R"raw(^\s*#\s*include\s*"([^"]+)")raw");
    std::vector<std::string> violations;
    std::size_t scanned = 0;
    std::error_code error;

    for (const std::string& applicationRoot : ApplicationRoots)
    {
        const std::filesystem::path root = repository / applicationRoot;
        if (!std::filesystem::is_directory(root, error))
        {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            const std::filesystem::path& path = entry.path();
            const std::string extension = path.extension().string();
            if (extension != ".h" && extension != ".cpp")
            {
                continue;
            }
            ++scanned;

            std::ifstream stream(path);
            std::string line;
            while (std::getline(stream, line))
            {
                std::smatch match;
                if (!std::regex_search(line, match, includePattern))
                {
                    continue;
                }
                const std::string named = ImplementationModuleNamedBy(match[1].str());
                if (named.empty())
                {
                    continue;
                }
                const std::string relativeText =
                    std::filesystem::relative(path, repository, error).generic_string();
                if (IsDesignatedConsumer(relativeText, named))
                {
                    continue;
                }
                violations.push_back(relativeText + " -> " + match[1].str() + " (" + named +
                    " is an implementation module; an application may not include it)");
            }
        }
    }

    for (const std::string& violation : violations)
    {
        std::cerr << "  layering: " << violation << "\n";
    }

    if (scanned == 0)
    {
        std::cout << "  application isolation tests skipped: application sources not found\n";
        return true;
    }
    return Expect(violations.empty(), "no application should include an implementation module");
}

/// <summary>
/// 공개 헤더는 허용 목록 네 개를 제외하고 UTF-8 계약을 사용한다.
/// 플랫폼 구현 내부의 지역 std::wstring은 외부 계약에 해당하지 않는다.
/// </summary>
bool RunPlatformTextEncodingTests()
{
    const std::filesystem::path root = EngineRoot();
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
    {
        std::cout << "  text encoding tests skipped: engine sources not found\n";
        return true;
    }

    std::vector<std::string> violations;
    std::size_t scanned = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".h")
        {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        const std::string relativeText =
            std::filesystem::relative(path, root, error).generic_string();
        if (relativeText.starts_with("x64/"))
        {
            continue;
        }
        if (IsPlatformBoundModule(ModuleOf(std::filesystem::relative(path, root, error))))
        {
            continue;
        }
        if (std::ranges::find(WideCharacterContracts, relativeText) !=
            WideCharacterContracts.end())
        {
            continue;
        }
        ++scanned;

        std::ifstream stream(path);
        std::string line;
        unsigned int lineNumber = 0;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            if (IsCommentLine(line) || !std::regex_search(line, WideCharacterPattern))
            {
                continue;
            }
            violations.push_back(relativeText + ":" + std::to_string(lineNumber) +
                " uses a wide character type in a platform-neutral header; text that crosses"
                " this boundary is UTF-8 std::string");
        }
    }

    for (const std::string& violation : violations)
    {
        std::cerr << "  text encoding: " << violation << "\n";
    }

    return Expect(scanned > 50, "the engine headers should have been scanned") &&
        Expect(violations.empty(), "no new platform-neutral contract should use wide characters");
}

static const TestSupport::Registration gLayeringTests{
    "Layering", "layering tests should pass", RunLayeringTests };

static const TestSupport::Registration gApplicationIsolationTests{
    "Layering", "application isolation tests should pass", RunApplicationIsolationTests };

static const TestSupport::Registration gPlatformTextEncodingTests{
    "Layering", "platform text encoding tests should pass", RunPlatformTextEncodingTests };
