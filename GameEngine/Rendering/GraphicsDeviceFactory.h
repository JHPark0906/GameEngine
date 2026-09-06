#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "IGraphicsDevice.h"

namespace GameEngine::Rendering
{

struct GraphicsBackendDescriptor;

/// <summary>
/// 자동 선택이 고른 백엔드와, 그보다 우선순위가 높은데 지나친 것들의 이유다.
///
/// 이유를 값으로 들고 나오는 까닭은 로그 한 줄 때문만이 아니다. 「왜 D3D11로 내려갔는가」는
/// 사람이 가장 먼저 묻는 것인데, 고르는 자리에서 적어 두지 않으면 그 답이 남지 않는다.
/// </summary>
struct AutomaticBackendChoice
{
    /// <summary>고른 백엔드다. 쓸 수 있는 것이 하나도 없으면 null이다.</summary>
    const GraphicsBackendDescriptor* backend = nullptr;

    /// <summary>
    /// 더 우선하는 백엔드를 지나친 이유다. 첫 후보가 곧바로 뽑혔으면 비어 있다.
    /// </summary>
    std::string passedOver;
};

/// <summary>프로젝트가 요청한 백엔드 식별자를 구체 그래픽 장치로 바꾼다.</summary>
class GraphicsDeviceFactory final
{
public:
    /// <summary>
    /// 주어진 서술자들 중에서 자동 선택이 고를 것을 정한다. 우선순위 내림차순으로 늘어선
    /// 목록을 받아, 이 머신이 실제로 세울 수 있는 첫 번째를 고른다.
    ///
    /// 레지스트리가 아니라 <b>목록</b>을 받는 것이 요점이다. 그래야 「우선하는 백엔드가 이
    /// 머신에서 안 만들어질 때 다음으로 내려가는가」를, 정말로 그런 기계를 구하지 않고도
    /// 가짜 서술자로 물을 수 있다 — 그 경로는 그러지 않으면 영영 시험되지 않는다.
    /// </summary>
    [[nodiscard]] static AutomaticBackendChoice ChooseAutomatically(
        std::span<const GraphicsBackendDescriptor> backends);

    /// <summary>
    /// 요청된 백엔드 식별자의 장치를 만든다. 빈 id나 "Auto"는 실행 중인 머신이 지원하는 것 중
    /// 우선순위가 가장 높은 백엔드를 고른다.
    /// </summary>
    /// <param name="requestedId">프로젝트 설정에서 온 백엔드 식별자이다.</param>
    /// <param name="selectedId">실제로 만들어진 백엔드의 식별자를 받는다.</param>
    /// <returns>초기화되지 않은 장치이며, 요청에 맞는 지원 백엔드가 없으면 null이다.</returns>
    [[nodiscard]] static std::unique_ptr<IGraphicsDevice> Create(
        std::string_view requestedId,
        std::string& selectedId);

    /// <summary>식별자에 맞는 백엔드가 컴파일되어 있고 여기서 지원되는지 여부이다.</summary>
    [[nodiscard]] static bool IsSupported(std::string_view requestedId);
};

}
