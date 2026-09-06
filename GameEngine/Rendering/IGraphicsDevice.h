#pragma once

#include <span>

#include "../Platform/NativeSurface.h"
#include "GraphicsDeviceCapabilities.h"
#include "RenderFrame.h"

namespace GameEngine::Runtime
{
class Game;
}

namespace GameEngine::Rendering
{
struct RenderFrame;

/// <summary>그래픽 백엔드가 구현해야 하는 장치 및 프레임 출력 인터페이스이다.</summary>
class IGraphicsDevice
{
public:
    virtual ~IGraphicsDevice() = default;

    IGraphicsDevice(const IGraphicsDevice&) = delete;
    IGraphicsDevice& operator=(const IGraphicsDevice&) = delete;
    IGraphicsDevice(IGraphicsDevice&&) = delete;
    IGraphicsDevice& operator=(IGraphicsDevice&&) = delete;

    /// <summary>
    /// 지정한 플랫폼 표면에 그래픽 장치를 연결하고 출력 자원을 생성한다.
    ///
    /// 무효한 표면(<see cref="Platform::NativeSurfaceKind::None"/>)을 넘기면 headless로
    /// 초기화된다: present할 곳이 없으므로 BeginFrame과 EndFrame은 오류이고, 프레임은
    /// RenderToImage로만 나온다. 캡처만 소비하는 곳 — 게임 뷰를 이미지로 받는 에디터, 백엔드를
    /// 비교하는 테스트 — 은 창 없이 장치를 세운다.
    /// </summary>
    /// <param name="surface">출력 대상으로 사용할 네이티브 표면이다. 무효하면 headless다.</param>
    /// <returns>그래픽 장치 초기화에 성공했으면 true이다. 지원하지 않는 표면 종류이면 false이다.</returns>
    [[nodiscard]] virtual bool Initialize(const Platform::NativeSurface& surface) = 0;

    /// <summary>렌더 타깃을 바인딩하고 새 프레임 렌더링을 준비한다.</summary>
    [[nodiscard]] virtual bool BeginFrame() = 0;

    /// <summary>현재 렌더 타깃의 픽셀 크기를 반환한다.</summary>
    [[nodiscard]] virtual RenderTargetSize GetRenderTargetSize() const = 0;

    /// <summary>
    /// 이 장치가 지원하는 것들이다. Initialize가 성공한 뒤부터 유효하다. 리졸버는 모든 백엔드가
    /// 공유한다고 단정된 한계를 가정하는 대신, 그 시점에 이것을 읽어 보관한다.
    /// </summary>
    [[nodiscard]] virtual GraphicsDeviceCapabilities GetCapabilities() const = 0;

    /// <summary>검증된 프레임을 명령으로 기록한다.</summary>
    /// <returns>프레임을 기록했으면 true, 계약을 만족하지 않아 거부했으면 false이다.</returns>
    [[nodiscard]] virtual bool Render(const RenderFrame& frame) = 0;

    /// <summary>완성된 프레임을 화면에 표시한다.</summary>
    /// <returns>프레임 표시에 성공했으면 true이다.</returns>
    [[nodiscard]] virtual bool EndFrame() = 0;

    /// <summary>
    /// 프레임이 요구한 크기의 오프스크린 타깃에 한 프레임을 렌더링하고 그 픽셀을 반환한다.
    /// 아무것도 present하지 않으며 swap chain은 관여하지 않는다.
    ///
    /// 이것은 단계가 아니라 완결된 연산이다: `BeginFrame`과 `EndFrame` 사이에 들어가지 않으며,
    /// 거기서 호출하면 오류다. 반환하기 전에 끝까지 실행되는데, 호출자 — 백엔드를 비교하는 도구,
    /// 패널을 채우는 에디터 — 가 원하는 것이 픽셀이지 픽셀에 대한 약속이 아니기 때문이다.
    /// </summary>
    /// <summary>캡처 한 번의 선택지다.</summary>
    struct CaptureRequest
    {
        /// <summary>
        /// 캡처 채널이다. 같은 채널의 캡처들이 타깃과 리드백 버퍼를 공유하므로, 매 프레임 같은
        /// 뷰를 캡처하는 호출자는 뷰마다 다른 채널을 준다.
        /// </summary>
        unsigned int channel = 0;
        /// <summary>
        /// true면 픽셀을 기다리지 않아도 된다: 장치는 이번 프레임을 그려 두고, 같은 채널의
        /// *이전* 캡처가 끝나 있으면 그 픽셀을 돌려준다. 아직 돌려줄 것이 없으면 false를 반환하고
        /// 이미지는 비어 있다. 한 프레임 늦은 그림을 받아들이는 대신 GPU를 기다리지 않는
        /// 방식이다. 지원하지 않는 백엔드는 동기로 처리해도 된다 —
        /// "이전 캡처"가 곧 이번 것일 뿐, 계약은 같다.
        /// </summary>
        bool deferred = false;
    };

    [[nodiscard]] virtual bool RenderToImage(
        const RenderFrame& frame, CapturedImage& image, const CaptureRequest& request = {}) = 0;

    /// <summary>
    /// 동기 배치 캡처 한 항목이다. 프레임과 출력 이미지는 RenderToImages가 반환할 때까지
    /// 호출자가 소유하며, 장치는 이 참조를 보관하지 않는다.
    /// </summary>
    struct CaptureBatchItem
    {
        const RenderFrame& frame;
        CapturedImage& image;
        unsigned int channel = 0;
        bool succeeded = false;
    };

    /// <summary>
    /// 모든 항목의 현재 프레임을 캡처하고 픽셀 회수를 마친 뒤 반환한다. 각 항목의 succeeded는
    /// RenderToImage의 반환값과 같고, 다른 항목의 실패와 독립적이다. BeginFrame과 EndFrame
    /// 사이에서는 호출할 수 없다. 기본 구현은 입력 순서대로 즉시 캡처한다.
    /// 같은 채널이나 출력 이미지를 반복하면 순차 호출과 같은 결과를 유지한다.
    /// </summary>
    virtual void RenderToImages(std::span<CaptureBatchItem> items);

protected:
    IGraphicsDevice() = default;
};

}
