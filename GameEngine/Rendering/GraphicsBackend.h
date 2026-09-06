#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IGraphicsDevice.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 하나의 그래픽 백엔드에 대해 엔진이 알아야 하는 모든 것이다. 백엔드는 열거로 이름 불리는 대신
/// 서술자를 제출하므로, API를 추가할 때 손대는 곳은 백엔드 자신의 파일과 레지스트리의 백엔드
/// 목록뿐이고 App, Build, 직렬화의 어느 것도 아니다.
/// </summary>
struct GraphicsBackendDescriptor
{
    /// <summary>프로젝트 파일과 진단에 쓰이는 안정적인 식별자이다. 예: "D3D12".</summary>
    std::string_view id;

    /// <summary>자동 선택은 값이 큰 쪽을 우선한다.</summary>
    int automaticSelectionPriority = 0;

    /// <summary>실행 중인 머신이 이 백엔드의 장치를 실제로 만들 수 있는지 보고한다.</summary>
    bool (*IsSupported)() = nullptr;

    /// <summary>이 백엔드의 초기화되지 않은 장치를 만든다.</summary>
    std::unique_ptr<IGraphicsDevice> (*CreateDevice)() = nullptr;

    /// <summary>
    /// 이 백엔드가 실행 파일 옆에서 찾아야 하는 파일들이다. 런타임 루트 기준 상대 경로다. 배포가
    /// 이것을 스테이징하므로, 다른 백엔드와 다른 아티팩트가 필요한 백엔드를 빠뜨릴 수 없다.
    /// </summary>
    std::vector<std::filesystem::path> (*GetRuntimeArtifacts)() = nullptr;

    /// <summary>
    /// 스테이징된 런타임 루트 안의 이 백엔드 셰이더 소스를 컴파일해, 실행 시 소스 컴파일 없이
    /// 로드되는 아티팩트를 소스 옆에 쓴다. 어떤 언어를 어떤 아티팩트로 컴파일하는지는 백엔드
    /// 계열의 지식이라서, 빌드 시스템이 아니라 서술자가 이것을 가리킨다 — HLSL을 blob으로
    /// 만드는 Direct3D 계열처럼, SPIR-V를 원하는 계열은 자기 것을 가져온다.
    ///
    /// 실패해도 배포는 계속된다: 런타임은 아티팩트가 없으면 소스에서 컴파일하는 예비 경로를
    /// 그대로 갖고 있으므로, 사전 컴파일은 정확성이 아니라 시작 비용의 문제다.
    /// </summary>
    bool (*CompileRuntimeArtifacts)(const std::filesystem::path& runtimeRootPath) = nullptr;

    [[nodiscard]] bool IsComplete() const
    {
        return !id.empty() && IsSupported && CreateDevice && GetRuntimeArtifacts &&
            CompileRuntimeArtifacts;
    }
};

/// <summary>대상 머신에서 엔진이 백엔드를 고르게 하는 프로젝트 파일 값이다.</summary>
inline constexpr std::string_view AutomaticGraphicsBackendId = "Auto";

/// <summary>이 빌드에 컴파일되어 들어간 그래픽 백엔드의 집합이다.</summary>
class GraphicsBackendRegistry final
{
public:
    /// <summary>컴파일된 모든 백엔드이다. 자동 선택 우선순위 내림차순이다.</summary>
    [[nodiscard]] static std::span<const GraphicsBackendDescriptor> GetBackends();

    /// <summary>식별자로 백엔드를 찾는다. 모르는 id나 자동 선택 id에는 null을 반환한다.</summary>
    [[nodiscard]] static const GraphicsBackendDescriptor* Find(std::string_view id);

    /// <summary>id가 컴파일된 백엔드를 가리키거나 자동 선택을 요청하는지 여부이다.</summary>
    [[nodiscard]] static bool IsKnownId(std::string_view id);

    /// <summary>유효한 식별자들의 쉼표 구분 목록이다. 진단에 쓰인다.</summary>
    [[nodiscard]] static std::string DescribeAvailableIds();
};

}
