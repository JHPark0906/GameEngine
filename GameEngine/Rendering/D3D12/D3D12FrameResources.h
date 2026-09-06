#pragma once

#include <cstddef>
#include <memory>

#include <d3d12.h>

#include "D3D12DescriptorHeapAllocator.h"
#include "../RenderFrame.h"

namespace GameEngine::Rendering::D3D12
{

/// <summary>
/// 정책이 원하는 자리 수와 장치가 줄 수 있는 자리 수를 결합한다. 둘 중 작은 쪽이다: 정책은 장치가
/// 못 하는 것을 요구할 수 없고, 장치가 더 줄 수 있다고 해서 정책이 정한 상주량을 넘길 이유도 없다.
///
/// 결합 규칙만 따로 떼어 둔 이유는 장치 없이 확인할 수 있게 하기 위해서다. 장치를 여는 쪽은
/// <see cref="ComputeShaderVisibleDescriptorCount"/>이다.
/// </summary>
[[nodiscard]] constexpr UINT CombineDescriptorCount(
    const std::size_t requestedEntries, const UINT deviceLimit)
{
    return requestedEntries < static_cast<std::size_t>(deviceLimit)
        ? static_cast<UINT>(requestedEntries)
        : deviceLimit;
}

/// <summary>
/// 이 백엔드가 만드는 shader-visible descriptor 슬롯 수를, 정책이 원하는 수와 장치가 줄 수 있는
/// 수를 결합해서 정한다. 따라서 이것이 한 번에 바인딩할 수 있는 텍스처의 최대 개수이다.
///
/// 원하는 수는 공용 정책의 TextureBindingBudget에서 온다 — 얼마나 상주시킬지는 백엔드가 아니라
/// 엔진이 정할 일이기 때문이다. 줄 수 있는 수는 장치의 resource binding tier가 정하는
/// shader-visible 힙 한계이며, 이쪽은 D3D12의 API 한계라서 공용 정책이 알 바가 아니다.
///
/// 이것이 유일한 정의다. 힙이 이 수로 만들어지고, 업로더는 힙이 실제로 얻은 자리 수를
/// allocator에게 물어 자기 바인딩 캐시를 묶는다. 두 수가 따로 적히면 한쪽만 올렸을 때 슬롯을
/// 낭비하거나, 캐시가 allocator가 줄 수 없는 자리가 있다고 믿게 되어 텍스처가 그냥 나타나지
/// 않는다.
/// </summary>
[[nodiscard]] UINT ComputeShaderVisibleDescriptorCount(ID3D12Device& device);

/// <summary>상수 정렬과 공용 제출 예산을 반영한 프레임의 최대 상수 바이트 수다.</summary>
[[nodiscard]] std::size_t ComputeFrameConstantBytes(const RenderFrame& frame);

/// <summary>
/// 이 백엔드가 CPU를 GPU보다 몇 프레임 앞서 달리게 하는지이며, 따라서 command allocator,
/// 상수 링 세그먼트, swap-chain 버퍼가 몇 개 존재하는지이다.
///
/// GPU가 아직 읽고 있을 수 있는 모든 리소스는 이만큼의 프레임을 살아남아야 한다: GPU 리소스를
/// 쥔 캐시들은 항목을 이만큼 보존하고, 한 프레임의 스테이징 버퍼는 자기 슬롯이 다시 돌아올
/// 때에야 해제된다.
/// </summary>
inline constexpr UINT FramesInFlight = 2;

/// <summary>지연 캡처를 받을 수 있는 채널 수다. 에디터는 게임 뷰와 씬 뷰로 둘을 쓴다.</summary>
inline constexpr UINT MaxDeferredCaptureChannels = 4;

/// <summary>
/// 프레임마다 갈리는 링 — 상수 버퍼 세그먼트, 업로드 버퍼, 명령 할당자 — 의 슬롯 수다. 주
/// 프레임이 앞의 FramesInFlight개를 쓰고, 지연 캡처 채널마다 둘씩 그 뒤를 쓴다. 캡처가 주
/// 프레임과 GPU에서 겹쳐 실행되므로 슬롯을 나눠 갖지 않으면 한쪽이 다른 쪽의 상수를 덮어쓴다.
/// </summary>
inline constexpr UINT FrameSlotCount = FramesInFlight + 2 * MaxDeferredCaptureChannels;

/// <summary>
/// GPU 캐시가 항목을 지키는 캐시 프레임 수다. 창이 있으면 주 프레임이, headless면
/// 캡처가 캐시 프레임을 전진시킨다. 지연 캡처가 쓴 항목은 그 캡처의 펜스가 두 캡처
/// 뒤에야 확인되므로 한 프레임 더 지킨다.
/// </summary>
inline constexpr UINT FrameSlotRetention = FramesInFlight + 1;

/// <summary>
/// 모든 D3D12 렌더 패스가 공유하는 루트 시그니처, shader-visible descriptor 힙, 프레임별 상수
/// 링이다. D3D11은 패스마다 자기 상수 버퍼와 상태 객체를 주지만, D3D12는 command list당 루트
/// 시그니처 하나와 descriptor 힙 하나를 바인딩하므로, 그 상태를 패스마다 복제하는 대신 여기서
/// 소유한다.
/// </summary>
class D3D12FrameResources final
{
public:
    D3D12FrameResources();
    ~D3D12FrameResources();

    D3D12FrameResources(const D3D12FrameResources&) = delete;
    D3D12FrameResources& operator=(const D3D12FrameResources&) = delete;

    [[nodiscard]] bool Initialize(ID3D12Device& device);

    /// <summary>
    /// 상수 링이 이 프레임 자신의 세그먼트를 가리키게 한다.
    ///
    /// 프레임이 in flight이므로 GPU가 이전 프레임의 상수를 아직 읽고 있을 수 있다. 그래서 링을
    /// 매 프레임 처음부터 다시 쓰지 않고, 각 프레임 슬롯은 자기만의 세그먼트에 쓴다.
    /// </summary>
    void BeginFrame(UINT frameIndex);

    /// <summary>현재 GPU 슬롯의 상수 저장소를 이 프레임에 맞게 확장한다. 다른 슬롯은 보존된다.</summary>
    [[nodiscard]] bool PrepareFrame(const RenderFrame& frame);

    /// <summary>그리기에 사용하는 descriptor 힙을 바인딩하고 텍스처 바인딩 기록을 비운다.</summary>
    void BindFrameState(ID3D12GraphicsCommandList& commandList) const;

    /// <summary>
    /// command list가 리셋되거나 루트 시그니처·descriptor 힙을 바꾸면 호출한다.
    /// 다음 draw가 같은 텍스처여도 새 루트 상태에 SRV 테이블을 다시 바인딩하게 한다.
    /// </summary>
    void InvalidateTextureBinding() const;

    [[nodiscard]] ID3D12RootSignature* GetRootSignature() const;
    [[nodiscard]] D3D12DescriptorHeapAllocator& GetDescriptorAllocator() const;

    /// <summary>
    /// 프레임 상수 링에 정렬된 슬롯 하나를 예약하고, draw의 상수를 거기 쓰고, 상수 뷰와 draw의
    /// 텍스처 descriptor 테이블을 모두 바인딩한다. 프레임의 상수 링이 소진되면 false를 반환하며,
    /// 그 경우 아무것도 바인딩되지 않고 draw는 건너뛴다.
    /// </summary>
    template <typename TConstants>
    [[nodiscard]] bool BindDrawConstants(
        ID3D12GraphicsCommandList& commandList,
        const TConstants& constants,
        const UINT textureDescriptorIndex)
    {
        return BindDrawConstantData(
            commandList, &constants, sizeof(constants), textureDescriptorIndex);
    }

private:
    [[nodiscard]] bool BindDrawConstantData(
        ID3D12GraphicsCommandList& commandList,
        const void* constants,
        std::size_t byteSize,
        UINT textureDescriptorIndex);

    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
