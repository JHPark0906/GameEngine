#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <vector>

#include "RenderFrame.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 백엔드를 대신해 프레임을 검증하고, 거부됐다면 이유를 보고한다. 모든 백엔드가 잘못된 프레임을
/// 같은 방식과 같은 진단으로, 그리고 GPU 상태를 하나라도 준비하기 전에 거부하므로, 거부된
/// 프레임은 백엔드 상태를 남기지 않는다.
/// </summary>
/// <param name="frame">백엔드가 렌더링하려는 프레임이다.</param>
/// <param name="backendName">거부를 보고하는 백엔드 이름이다. 예: "D3D11".</param>
/// <returns>dispatch 능력 객체이며, 프레임이 계약을 만족하지 않으면 nullopt이다.</returns>
[[nodiscard]] inline std::optional<ValidatedRenderFrame> TryValidateForBackend(
    const RenderFrame& frame, const char* backendName)
{
    RenderFrameValidationResult validation;
    std::optional<ValidatedRenderFrame> validatedFrame = frame.TryValidate(validation);
    if (!validatedFrame)
    {
        // 한 패킷의 검증이 실패하면 프레임 전체가 거부되므로 로그도 그 범위를 명시한다.
        Diagnostics::Debug::LogError(
            backendName, " dropped a whole frame: nothing was drawn this frame. ",
            GetRenderFrameValidationErrorName(validation.error),
            " at pass ", static_cast<unsigned int>(validation.pass),
            ", packet ", validation.packetIndex);
    }
    return validatedFrame;
}

/// <summary>
/// 프레임의 값을 백엔드 소유의 GPU 캐시 항목으로 resolve한다. 반환되는 포인터는 소유권이 없고
/// 다음 BeginFrame 호출까지 유효하며, 핸들이 잘못됐거나 리졸버가 준비되지 않았거나 백엔드가
/// 요청된 리소스를 만들 수 없으면 null이다.
///
/// 기반 클래스가 아니라 concept이다. 렌더러는 언제나 자기 백엔드의 리졸버를 구체 타입으로
/// 쥐므로 추상 기반을 통해 dispatch되는 일이 없다: 가상 함수는 다형성을 사 주지 못하고, 값으로
/// 캐시되는 모든 리소스에 vtable 포인터 비용만 얹는다. 여기서 계약을 검사하면 리졸버가 정의된
/// 곳에서 계약이 강제되고, 공변 가상 함수로는 다룰 수 없는 반환 타입까지 검사된다.
/// </summary>
template <typename TResolver>
concept RenderResourceResolver = requires(
    TResolver& resolver,
    const RenderFrame& frame,
    const GeometryHandle geometry,
    const SkinnedGeometryHandle skinnedGeometry,
    const MaterialHandle material,
    const TextDraw& text)
{
    // 리소스 사용 수명을 시작하고, 더 오래된 프레임의 항목이 퇴거될 수 있게 한다.
    { resolver.BeginFrame() } -> std::same_as<void>;
    // 각 resolve는 백엔드가 소유한 것을 가리키는, null일 수 있는 비소유 포인터를 돌려준다.
    { resolver.ResolveGeometry(frame, geometry) == nullptr } -> std::same_as<bool>;
    { resolver.ResolveSkinnedGeometry(frame, skinnedGeometry) == nullptr } -> std::same_as<bool>;
    { resolver.ResolveMaterial(frame, material) == nullptr } -> std::same_as<bool>;
    { resolver.ResolveText(text) == nullptr } -> std::same_as<bool>;
};

/// <summary>API별 명령 버퍼에 기록할 최소 Draw 명령 경계이다.</summary>
class ICommandEncoder
{
public:
    virtual ~ICommandEncoder() = default;
    virtual void BeginPass(RenderPass pass) = 0;
    virtual void EndPass(RenderPass pass) = 0;
};

/// <summary>순서가 보장된 DrawPacket을 실제 백엔드 명령으로 변환한다.</summary>
class IDrawPacketEncoder
{
public:
    virtual ~IDrawPacketEncoder() = default;
    virtual void Encode(const RenderFrame& frame, const DrawPacket& packet) = 0;
};

/// <summary>패스 큐를 재정렬하거나 타입별로 분리하지 않고 순서대로 dispatch한다.</summary>
class PassDispatcher final
{
public:
    /// <summary>인코더를 부르기 전에 프레임을 검증하고 그 결과를 반환한다.</summary>
    [[nodiscard]] RenderFrameValidationResult Dispatch(
        const RenderFrame& frame, ICommandEncoder& commandEncoder, IDrawPacketEncoder& packetEncoder) const
    {
        RenderFrameValidationResult validation;
        const std::optional<ValidatedRenderFrame> validatedFrame = frame.TryValidate(validation);
        if (!validatedFrame) return validation;

        Dispatch(*validatedFrame, commandEncoder, packetEncoder);
        return {};
    }

    /// <summary>백엔드 상태를 준비하기 전에 검증을 마친 프레임을 dispatch한다.</summary>
    void Dispatch(
        const ValidatedRenderFrame& validatedFrame,
        ICommandEncoder& commandEncoder,
        IDrawPacketEncoder& packetEncoder) const
    {
        const RenderFrame& frame = validatedFrame.GetFrame();

        for (std::size_t index = 0; index < static_cast<std::size_t>(RenderPass::Count); ++index)
        {
            const RenderPass pass = static_cast<RenderPass>(index);
            const std::vector<DrawPacket>& packets = frame.GetDrawPackets(pass);
            if (packets.empty()) continue;
            commandEncoder.BeginPass(pass);
            for (const DrawPacket& packet : packets)
            {
                packetEncoder.Encode(frame, packet);
            }
            commandEncoder.EndPass(pass);
        }
    }
};

}
