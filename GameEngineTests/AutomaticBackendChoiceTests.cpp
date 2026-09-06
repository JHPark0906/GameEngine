#include "AutomaticBackendChoiceTests.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Rendering/GraphicsBackend.h"
#include "BackendPixelSupport.h"
#include "Rendering/GraphicsDeviceFactory.h"
#include "TestSupport.h"

using TestSupport::Expect;

/// <summary>
/// 자동 선택이 무엇을 고르는지, 그리고 왜 그것을 골랐는지 묻는다.
///
/// 「D3D12를 만들 수 있으면 D3D12, 아니면 D3D11」은 두 문장이고 시험도 둘이어야 한다. 앞 문장은
/// 이 기계에서 확인할 수 있지만, 뒤 문장은 D3D12가 없는 기계를 구해야 확인된다 — 그래서 고르는
/// 함수가 레지스트리 대신 <b>서술자 목록</b>을 받도록 두었고, 여기서는 가짜 서술자로 「우선하는
/// 것이 안 만들어지는 기계」를 만들어 묻는다.
/// </summary>
namespace
{
    /// <summary>가짜 서술자가 무엇을 답할지 정하는 스위치다. 함수 포인터라 상태를 둘 곳이 없다.</summary>
    bool gFirstIsSupported = false;
    bool gSecondIsSupported = false;

    [[nodiscard]] bool FirstIsSupported() { return gFirstIsSupported; }
    [[nodiscard]] bool SecondIsSupported() { return gSecondIsSupported; }

    [[nodiscard]] std::unique_ptr<GameEngine::Rendering::IGraphicsDevice> MakeNothing()
    {
        return nullptr;
    }

    [[nodiscard]] GameEngine::Rendering::GraphicsBackendDescriptor MakeDescriptor(
        const std::string_view id, bool (*isSupported)())
    {
        GameEngine::Rendering::GraphicsBackendDescriptor descriptor;
        descriptor.id = id;
        descriptor.IsSupported = isSupported;
        descriptor.CreateDevice = &MakeNothing;
        return descriptor;
    }

    /// <summary>레지스트리가 실제로 D3D12를 D3D11보다 앞에 놓는지 본다.</summary>
    [[nodiscard]] bool CheckRegistryPrefersD3D12()
    {
        using namespace GameEngine::Rendering;

        const std::span<const GraphicsBackendDescriptor> backends =
            GraphicsBackendRegistry::GetBackends();
        const auto positionOf = [backends](const std::string_view id) -> std::ptrdiff_t
        {
            const auto found = std::ranges::find_if(
                backends, [id](const GraphicsBackendDescriptor& backend) { return backend.id == id; });
            return found == backends.end() ? -1 : std::ranges::distance(backends.begin(), found);
        };
        const std::ptrdiff_t twelve = positionOf("D3D12");
        const std::ptrdiff_t eleven = positionOf("D3D11");
        if (twelve < 0 || eleven < 0)
        {
            std::cout << "  automatic backend choice: this build has only one Direct3D backend\n";
            return true;
        }

        std::cout << "  registry order: D3D12 at " << twelve << ", D3D11 at " << eleven << "\n";
        if (twelve >= eleven)
        {
            std::cerr << "  the registry lists D3D11 before D3D12, so automatic selection would "
                      << "reach D3D11 first\n";
        }
        return Expect(
            twelve < eleven,
            "automatic selection considers D3D12 before D3D11");
    }

    /// <summary>둘 다 만들 수 있는 기계에서는 우선하는 쪽이 뽑히고, 지나친 것이 없다.</summary>
    [[nodiscard]] bool CheckPrefersTheFirstItCanBuild()
    {
        using namespace GameEngine::Rendering;

        gFirstIsSupported = true;
        gSecondIsSupported = true;
        const std::array descriptors{
            MakeDescriptor("Preferred", &FirstIsSupported),
            MakeDescriptor("Fallback", &SecondIsSupported) };

        const AutomaticBackendChoice choice =
            GraphicsDeviceFactory::ChooseAutomatically(descriptors);
        bool passed = Expect(
            choice.backend != nullptr && choice.backend->id == "Preferred",
            "a machine that can build the preferred backend gets it");
        passed &= Expect(
            choice.passedOver.empty(),
            "and nothing is reported as passed over");
        return passed;
    }

    /// <summary>
    /// 우선하는 것이 안 만들어지는 기계에서는 다음으로 내려가고, 그 이유가 남는다.
    ///
    /// 이것이 「D3D12를 지원하는 GPU면 D3D12」의 나머지 절반이다. 지원하지 않는 기계에서도
    /// 편집기가 떠야 하고, 뜬 뒤에 왜 D3D11인지 물을 수 있어야 한다.
    /// </summary>
    [[nodiscard]] bool CheckFallsToTheNextWhenTheFirstCannotBeBuilt()
    {
        using namespace GameEngine::Rendering;

        gFirstIsSupported = false;
        gSecondIsSupported = true;
        const std::array descriptors{
            MakeDescriptor("Preferred", &FirstIsSupported),
            MakeDescriptor("Fallback", &SecondIsSupported) };

        const AutomaticBackendChoice choice =
            GraphicsDeviceFactory::ChooseAutomatically(descriptors);
        bool passed = Expect(
            choice.backend != nullptr && choice.backend->id == "Fallback",
            "a machine that cannot build the preferred backend falls to the next");
        if (choice.passedOver.find("Preferred") == std::string::npos)
        {
            std::cerr << "  the reason given was \"" << choice.passedOver
                      << "\", which does not name the backend that was passed over\n";
        }
        passed &= Expect(
            choice.passedOver.find("Preferred") != std::string::npos,
            "and says which backend it passed over");
        return passed;
    }

    /// <summary>하나도 만들 수 없으면 아무것도 고르지 않는다.</summary>
    [[nodiscard]] bool CheckChoosesNothingWhenNothingCanBeBuilt()
    {
        using namespace GameEngine::Rendering;

        gFirstIsSupported = false;
        gSecondIsSupported = false;
        const std::array descriptors{
            MakeDescriptor("Preferred", &FirstIsSupported),
            MakeDescriptor("Fallback", &SecondIsSupported) };

        const AutomaticBackendChoice choice =
            GraphicsDeviceFactory::ChooseAutomatically(descriptors);
        return Expect(
            choice.backend == nullptr,
            "a machine that can build neither backend chooses none");
    }

    /// <summary>
    /// 백엔드를 이름으로 지정하면 자동 선택 우선순위와 무관하게 해당 백엔드를 선택해야 한다.
    /// </summary>
    [[nodiscard]] bool CheckNamingABackendIgnoresPriority()
    {
        using namespace GameEngine::Rendering;

        bool passed = true;
        for (const std::string_view id : { std::string_view("D3D11"), std::string_view("D3D12") })
        {
            const GraphicsBackendDescriptor* const found = GraphicsBackendRegistry::Find(id);
            if (!found)
            {
                continue;
            }
            passed &= Expect(
                found->id == id, "naming a backend finds that backend, whatever the priorities are");
        }
        return passed;
    }

    /// <summary>
    /// 픽셀 검사들이 자동 선택 결과에 한정되지 않고 컴파일된 백엔드 전부를 열거하는지 확인한다.
    /// 자동 선택과 검사 대상 열거는 별개여야 한다. 선택된 하나만 검사하면 백엔드 간 비교가
    /// 실행되지 않으면서 통과할 수 있다.
    /// </summary>
    [[nodiscard]] bool CheckPixelTestsCoverEveryCompiledBackend()
    {
        using namespace GameEngine::Rendering;

        const std::vector<const GraphicsBackendDescriptor*> enumerated =
            TestSupport::SupportedBackends();

        std::vector<std::string_view> expected;
        for (const GraphicsBackendDescriptor& backend : GraphicsBackendRegistry::GetBackends())
        {
            if (backend.IsComplete() && backend.IsSupported())
            {
                expected.push_back(backend.id);
            }
        }

        std::string names;
        for (const GraphicsBackendDescriptor* const backend : enumerated)
        {
            names += names.empty() ? "" : ", ";
            names += std::string(backend->id);
        }
        std::cout << "  pixel tests run on " << enumerated.size() << " backend(s): " << names
                  << "\n";

        bool passed = Expect(
            enumerated.size() == expected.size(),
            "the pixel tests enumerate every compiled backend this machine supports");
        for (const std::string_view id : expected)
        {
            const bool present = std::ranges::any_of(
                enumerated,
                [id](const GraphicsBackendDescriptor* const backend) { return backend->id == id; });
            if (!present)
            {
                std::cerr << "  " << id
                          << " is supported here but the pixel tests would not run on it\n";
            }
            passed &= Expect(present, "and none of them is skipped because of what Auto prefers");
        }
        return passed;
    }
}

bool RunAutomaticBackendChoiceTests()
{
    bool passed = CheckRegistryPrefersD3D12();
    passed &= CheckPixelTestsCoverEveryCompiledBackend();
    passed &= CheckPrefersTheFirstItCanBuild();
    passed &= CheckFallsToTheNextWhenTheFirstCannotBeBuilt();
    passed &= CheckChoosesNothingWhenNothingCanBeBuilt();
    passed &= CheckNamingABackendIgnoresPriority();
    return passed;
}

static const TestSupport::Registration gAutomaticBackendChoiceTests{
    "PlayerStartup", "automatic backend choice tests should pass", RunAutomaticBackendChoiceTests };
