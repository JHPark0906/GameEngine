#include "ShaderContractTests.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "Platform/PlatformServices.h"
#include "Rendering/Direct3D/ShaderPaths.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/RenderSamplerPolicy.h"
#include "Rendering/ShaderBindings.h"
#include "Rendering/ShaderInterop.h"
#include "Rendering/ShaderProgram.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
}

/// <summary>
/// 모든 셰이더 프로그램이 아티팩트로 대응되고, 공유 레이아웃은 셰이더가 읽는 크기 그대로
/// 남는다.
///
/// 백엔드는 파일이 아니라 프로그램을 요구하고, 대응은 switch다: switch를 넓히지 않고
/// 프로그램을 추가하면 빈 경로가 나오는데, 그것은 컴파일러가 잡는 무언가가 아니라 초기화에
/// 실패하는 백엔드로 드러났을 것이다.
/// </summary>
bool RunShaderContractTests()
{
    using namespace GameEngine::Rendering;

    std::vector<std::filesystem::path> mapped;
    for (const ShaderProgram program : AllShaderPrograms)
    {
        mapped.push_back(Direct3D::GetShaderRelativePath(program));
    }
    const bool allMapped = std::ranges::none_of(
        mapped, [](const std::filesystem::path& path) { return path.empty(); });

    std::vector<std::filesystem::path> distinct = mapped;
    std::ranges::sort(distinct);
    const bool allDistinct =
        std::ranges::unique(distinct).begin() == distinct.end() &&
        distinct.size() == AllShaderPrograms.size();

    // Deployment stages what the mapping reports, so the two must agree.
    const std::vector<std::filesystem::path> declared = Direct3D::GetShaderRelativePaths();
    const bool deploymentMatches = declared.size() == mapped.size() &&
        std::ranges::equal(declared, mapped);

    return Expect(allMapped, "every shader program should map to an artifact") &&
        Expect(allDistinct, "two shader programs should not share one artifact") &&
        Expect(
            deploymentMatches,
            "the artifacts deployment stages should be exactly the mapped programs") &&
        Expect(
            sizeof(MeshConstants) == 320 && sizeof(SpriteConstants) == 96 &&
                sizeof(TextConstants) == 96 && sizeof(MeshVertex) == 32 &&
                sizeof(SpriteVertex) == 16,
            "the shared shader layouts should keep the sizes the shaders read them at");
}

/// <summary>
/// 두 백엔드가 읽는 샘플러 정책을 고정한다. 여기서의 어긋남은 다른 무엇도 잡을 수 없다:
/// D3D11은 패스마다 샘플러를 만들고 D3D12는 메시·스프라이트·텍스트 파이프라인이 모두 공유하는
/// 정적 샘플러 하나를 선언하므로, 둘은 한쪽에서 글리프 아틀라스를 wrap하고 다른 쪽에서 clamp하는
/// 데까지 어긋날 수 있고, 그때 실행 로그에는 아무 오류도 남지 않는다.
/// </summary>
bool RunSamplerPolicyTests()
{
    using namespace GameEngine::Rendering;

    return Expect(
               GetSamplerAddressMode(PipelineKind::Mesh) == SamplerAddressMode::Wrap,
               "a mesh material should tile") &&
        Expect(
            GetSamplerAddressMode(PipelineKind::Sprite) == SamplerAddressMode::Clamp &&
                GetSamplerAddressMode(PipelineKind::Text) == SamplerAddressMode::Clamp,
            "a sprite and a glyph atlas should clamp so filtering cannot reach the far edge") &&
        Expect(
            GetShaderBindings(PipelineKind::Mesh).sampler !=
                    GetShaderBindings(PipelineKind::Sprite).sampler &&
                GetShaderBindings(PipelineKind::Sprite).sampler ==
                    GetShaderBindings(PipelineKind::Text).sampler,
            "the two address modes should occupy different sampler slots");
}

/// <summary>
/// 셰이더 프로그램이 바인딩에 쓰는 슬롯이 곧 셰이더 인터페이스이고, 그 양쪽이 일치해야 한다:
/// 리소스를 바인딩하는 백엔드와 그것을 읽는 셰이더. 두 언어에 리터럴을 따로 적으면 그 둘을
/// 붙들어 주는 것은 주석뿐이다. 숫자는 한 번 선언되고 컴파일러가 HLSL에 정의로 건네므로, 엔진이
/// 정의하지 않은 슬롯을 부르는 셰이더는 잘못된 슬롯을 샘플링하는 대신 컴파일에 실패한다.
///
/// 여기서 검사할 수 있는 것은 표의 모양이다. 셰이더가 실제로 그것을 받는다는 사실은 셰이더가
/// 컴파일된다는 것 자체가 검사하며, 모든 백엔드가 초기화 중에 그렇게 한다.
/// </summary>
bool RunShaderBindingTests()
{
    using namespace GameEngine::Rendering;

    // A pipeline kind and the program that implements it must stay in correspondence, because a
    // backend holds the first and asks about the second.
    const bool programsMatchPipelines =
        GetShaderProgram(PipelineKind::Mesh) == ShaderProgram::Mesh &&
        GetShaderProgram(PipelineKind::Sprite) == ShaderProgram::Sprite &&
        GetShaderProgram(PipelineKind::Text) == ShaderProgram::Text;

    // Every program binds one constant buffer and one texture, so a backend can build one root
    // signature that all of them share. If that ever stops being true, the D3D12 root signature
    // has to stop being shared, and this is where it would show.
    bool sharesConstantAndTextureSlots = true;
    for (const ShaderProgram program : AllShaderPrograms)
    {
        const ShaderBindingSlots slots = GetShaderBindings(program);
        sharesConstantAndTextureSlots = sharesConstantAndTextureSlots &&
            slots.constantBuffer == GetShaderBindings(ShaderProgram::Mesh).constantBuffer &&
            slots.texture == GetShaderBindings(ShaderProgram::Mesh).texture;
    }

    return Expect(
               programsMatchPipelines,
               "a pipeline kind should name the program that implements it") &&
        Expect(
            sharesConstantAndTextureSlots,
            "every program should bind its constants and texture through the same slots");
}

/// <summary>
/// 레지스트리는 구체 그래픽 백엔드의 이름을 부르는 유일한 자리다. 이 검사는 새 백엔드가
/// 만족해야 하는 계약을 고정해서, 백엔드 추가가 팩토리나 빌드 시스템이 잘못 다룰 서술자를
/// 조용히 만들어 낼 수 없게 한다.
/// </summary>
bool RunGraphicsBackendRegistryTests()
{
    using GameEngine::Rendering::AutomaticGraphicsBackendId;
    using GameEngine::Rendering::GraphicsBackendDescriptor;
    using GameEngine::Rendering::GraphicsBackendRegistry;

    const std::span<const GraphicsBackendDescriptor> backends =
        GraphicsBackendRegistry::GetBackends();

    bool everyDescriptorIsComplete = !backends.empty();
    bool identifiersAreUniqueAndResolvable = true;
    bool sortedByDescendingPriority = true;
    int previousPriority = (std::numeric_limits<int>::max)();
    std::unordered_set<std::string> seenIds;
    for (const GraphicsBackendDescriptor& backend : backends)
    {
        everyDescriptorIsComplete = everyDescriptorIsComplete && backend.IsComplete();
        sortedByDescendingPriority =
            sortedByDescendingPriority && backend.automaticSelectionPriority <= previousPriority;
        previousPriority = backend.automaticSelectionPriority;

        const auto [_, inserted] = seenIds.emplace(backend.id);
        identifiersAreUniqueAndResolvable = identifiersAreUniqueAndResolvable && inserted &&
            GraphicsBackendRegistry::Find(backend.id) == &backend &&
            GraphicsBackendRegistry::IsKnownId(backend.id);
    }

    // An automatic request is not a backend, but it is a valid project-file value.
    const bool automaticRequestIsKnown =
        GraphicsBackendRegistry::IsKnownId(AutomaticGraphicsBackendId) &&
        GraphicsBackendRegistry::IsKnownId("") &&
        GraphicsBackendRegistry::Find(AutomaticGraphicsBackendId) == nullptr;
    const bool unknownRequestIsRejected =
        !GraphicsBackendRegistry::IsKnownId("NoSuchBackend") &&
        GraphicsBackendRegistry::Find("NoSuchBackend") == nullptr;
    const std::string availableIds = GraphicsBackendRegistry::DescribeAvailableIds();
    const bool diagnosticListsEveryChoice =
        availableIds.find(AutomaticGraphicsBackendId) != std::string::npos &&
        std::ranges::all_of(backends, [&availableIds](const GraphicsBackendDescriptor& backend)
        {
            return availableIds.find(backend.id) != std::string::npos;
        });

    return Expect(
               everyDescriptorIsComplete,
               "every registered graphics backend should supply a complete descriptor") &&
        Expect(
            identifiersAreUniqueAndResolvable,
            "each backend identifier should be unique and resolve back to its descriptor") &&
        Expect(
            sortedByDescendingPriority,
            "the registry should order backends by descending automatic-selection priority") &&
        Expect(
            automaticRequestIsKnown,
            "an automatic request should be a known id without naming a backend") &&
        Expect(unknownRequestIsRejected, "an unknown backend identifier should be rejected") &&
        Expect(
            diagnosticListsEveryChoice,
            "the diagnostic id list should name every valid project-file choice");
}

/// <summary>
/// 백엔드가 실행 시점에 필요하다고 말하는 것과 빌드가 실제로 실행 파일 옆에 스테이징하는 것
/// 사이의 틈을 메운다.
///
/// 두 목록을 따로 관리하면, 패키징된 빌드가 D3D12의 초기화 때 컴파일하는 셰이더 없이 배포되어
/// 개발 빌드는 멀쩡한데 시작에 실패한다. 이 프로젝트는 애플리케이션처럼
/// 같은 스테이징 규칙(gameengine_stage_runtime_files)으로 배포되므로, 검사는 자기 출력
/// 디렉터리에 대해 돌고 다른 프로젝트의 빌드에 의존하지 않는다.
/// </summary>
bool RunRuntimeArtifactTests()
{
    using GameEngine::Rendering::GraphicsBackendDescriptor;
    using GameEngine::Rendering::GraphicsBackendRegistry;

    const std::filesystem::path runtimeRoot = GameEngine::Platform::PlatformServices::GetExecutableDirectory();

    bool everyArtifactIsStaged = true;
    bool everyBackendDeclaresArtifacts = true;
    std::size_t checkedArtifacts = 0;
    for (const GraphicsBackendDescriptor& backend : GraphicsBackendRegistry::GetBackends())
    {
        const std::vector<std::filesystem::path> artifacts = backend.GetRuntimeArtifacts();
        everyBackendDeclaresArtifacts = everyBackendDeclaresArtifacts && !artifacts.empty();
        for (const std::filesystem::path& artifact : artifacts)
        {
            ++checkedArtifacts;
            if (!std::filesystem::exists(runtimeRoot / artifact))
            {
                std::cerr << "  missing runtime artifact for backend " << backend.id << ": "
                          << (runtimeRoot / artifact).string() << '\n';
                everyArtifactIsStaged = false;
            }
        }
    }

    return Expect(
               everyBackendDeclaresArtifacts,
               "every backend should declare the files it needs beside the executable") &&
        Expect(checkedArtifacts > 0, "the runtime artifact check should have something to verify") &&
        Expect(
            everyArtifactIsStaged,
            "the build should stage every runtime artifact the registry declares");
}

/// <summary>
/// 백엔드 서술자의 아티팩트 사전 컴파일을 실제 배포 셰이더로 돌린다.
///
/// 아티팩트는 이 테스트 실행 파일의 배포 디렉터리에 쓰이므로, 뒤에 도는 backend image
/// 비교의 장치 초기화가 소스 대신 이것을 로드한다 — 사전 컴파일된 셰이더가 소스 컴파일과
/// 같은 이미지를 만드는지까지 그 비교가 함께 증명한다.
/// </summary>
bool RunShaderArtifactTests()
{
    using GameEngine::Rendering::GraphicsBackendRegistry;

    const GameEngine::Rendering::GraphicsBackendDescriptor* descriptor = nullptr;
    for (const auto& backend : GraphicsBackendRegistry::GetBackends())
    {
        if (backend.IsComplete())
        {
            descriptor = &backend;
            break;
        }
    }
    if (!descriptor)
    {
        std::cout << "  shader artifact tests skipped: no complete backend\n";
        return true;
    }

    const std::filesystem::path runtimeRoot =
        GameEngine::Platform::PlatformServices::GetExecutableDirectory();
    const bool compiled = descriptor->CompileRuntimeArtifacts(runtimeRoot);

    bool artifactsExist = true;
    for (const GameEngine::Rendering::ShaderProgram program :
         GameEngine::Rendering::AllShaderPrograms)
    {
        for (const char* const entryPoint : { "VS", "PS" })
        {
            const std::filesystem::path artifactPath = runtimeRoot /
                GameEngine::Rendering::Direct3D::GetCompiledShaderRelativePath(
                    program, entryPoint);
            std::error_code error;
            if (!std::filesystem::is_regular_file(artifactPath, error) || error ||
                std::filesystem::file_size(artifactPath, error) == 0 || error)
            {
                std::cerr << "  missing artifact: " << artifactPath.string() << '\n';
                artifactsExist = false;
            }
        }
    }

    return Expect(compiled, "precompiling the deployed shaders should succeed") &&
        Expect(artifactsExist, "every program and entry point should produce an artifact");
}

static const TestSupport::Registration gShaderContractTests{
    "ShaderContract", "shader contract tests should pass", RunShaderContractTests };

static const TestSupport::Registration gSamplerPolicyTests{
    "ShaderContract", "sampler policy tests should pass", RunSamplerPolicyTests };

static const TestSupport::Registration gShaderBindingTests{
    "ShaderContract", "shader binding tests should pass", RunShaderBindingTests };

static const TestSupport::Registration gGraphicsBackendRegistryTests{
    "ShaderContract", "graphics backend registry tests should pass", RunGraphicsBackendRegistryTests };

static const TestSupport::Registration gRuntimeArtifactTests{
    "ShaderContract", "runtime artifact staging tests should pass", RunRuntimeArtifactTests };

static const TestSupport::Registration gShaderArtifactTests{
    "ShaderContract", "shader artifact tests should pass", RunShaderArtifactTests };
