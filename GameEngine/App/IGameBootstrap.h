#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../Platform/IWindow.h"
#include "../Rendering/IRenderFrontend.h"
#include "../Rendering/RenderFrame.h"

namespace GameEngine::Rendering
{
class TextRasterizationCache;
}
namespace GameEngine::Runtime
{
class Game;
}
namespace GameEngine::App
{

/// <summary>게임 프로젝트가 공통 Application 수명주기에 런타임 구성을 추가하는 경계이다.</summary>
class IGameBootstrap
{
public:
    virtual ~IGameBootstrap() = default;

    /// <summary>초기 장면이 로드된 뒤 프로젝트 전용 런타임 객체를 구성한다.</summary>
    /// <param name="game">Application이 소유하는 게임 런타임이다.</param>
    /// <param name="window">Application이 생성한 기본 창이다.</param>
    [[nodiscard]] virtual bool Initialize(Runtime::Game& game, Platform::IWindow& window) = 0;

    /// <summary>
    /// 주 프레임을 그릴 프론트엔드들을 만든다. 비어 있으면 엔진 기본 — 장면을 그리는
    /// SceneRenderPass — 이 쓰인다.
    ///
    /// 에디터가 이것을 쓴다: 에디터의 주 프레임은 편집 중인 장면이 아니라 UI다. 그 장면은
    /// 캡처 뷰로 렌더링되어 UI 안의 이미지로 나타나므로, 여기에 그것을 그리는 프론트엔드가
    /// 있으면 같은 장면이 UI 뒤에 한 번 더 그려진다.
    ///
    /// 그러면서도 에디터는 여기에 장면 프론트엔드를 하나 둔다. 그것이 그리는 것은 편집
    /// 대상이 아니라 <b>에디터 자신의 런타임</b>이고, 그 안에 에디터의 UI 계층이 산다 —
    /// 주 프레임의 프론트엔드는 언제나 애플리케이션이 부팅한 런타임을 받으며, 편집 중인
    /// 프로젝트의 런타임은 그것과 별개다.
    /// </summary>
    /// <param name="worldTextCache">
    /// 이 런타임의 글자 배치 캐시다. 장면 프론트엔드를 만든다면 이것을 나눠 쥐어야 재는
    /// 폭과 그리는 폭이 갈라지지 않는다 — 같은 세계에 캐시는 하나다.
    /// </param>
    [[nodiscard]] virtual std::vector<std::unique_ptr<Rendering::IRenderFrontend>>
        CreateRenderFrontends(
            std::shared_ptr<Rendering::TextRasterizationCache> worldTextCache)
    {
        static_cast<void>(worldTextCache);
        return {};
    }

    /// <summary>
    /// worldTextCache가 막 만들어진 뒤, 그것을 나눠 쥐는 프론트엔드가 서기 전에 불린다. 이
    /// 세계의 텍스트가 기댈 최소한의 폰트를 등록할 자리다.
    ///
    /// 기본은 아무것도 하지 않는다 — 텍스트 없이 도는 프로젝트는 폰트 스택을 열 필요가 없고,
    /// 폰트를 자기 콘텐츠에서 읽어 오는 자리가 아직 없는 부트스트랩(플레이어가 그렇다)은
    /// 등록할 것이 없다. 등록하지 않으면 그 세계의 텍스트는 어떤 요청이든 계속 실패한다 —
    /// <c>TextRasterizationCache</c>에 폰트가 하나도 없으면 이름을 묻지 않고 거절하는 것이
    /// 그 계약이기 때문이다.
    /// </summary>
    /// <param name="worldTextCache">이 세계의 글자 배치 캐시다. 폰트를 등록할 대상이다.</param>
    virtual void RegisterSceneFonts(Rendering::TextRasterizationCache& worldTextCache)
    {
        static_cast<void>(worldTextCache);
    }

    /// <summary>
    /// 이 프로젝트가 그래픽 백엔드 설정을 스스로 갖고 있으면 그 값이다. 값이 없으면 프로젝트
    /// 서술자에 적힌 값이 쓰인다.
    ///
    /// 자기 설정 파일을 가진 프로젝트를 위한 자리다. 엔진은 그런 파일이 어디에 있고 무엇이라
    /// 불리는지 모르며, 아는 쪽이 여기서 답한다.
    /// </summary>
    [[nodiscard]] virtual std::optional<std::string> GetGraphicsBackendSetting() const
    {
        return std::nullopt;
    }

    /// <summary>
    /// 프레임마다 엔진의 게임이 업데이트된 직후, 캡처와 렌더링 앞에 불린다. 자기 런타임을 따로
    /// 소유한 bootstrap이 그것을 여기서 진행시킨다. 엔진은 그 런타임을 모르므로 대신 돌려 주지
    /// 않는다.
    /// </summary>
    virtual void Update(float deltaTime) { static_cast<void>(deltaTime); }

    /// <summary>
    /// 캡처할 뷰 하나의 서술이다: 픽셀 크기와, 장면의 카메라 대신 쓸 카메라(선택)이다.
    ///
    /// 카메라가 있으면 프레임의 카메라로 잠기고 장면의 카메라 선택은 무시된다 — 씬 뷰가 그렇게
    /// 같은 장면을 자기 시점에서 본다. 없으면 장면의 카메라가 평소처럼 쓰인다 — 게임 뷰다.
    /// </summary>
    struct CaptureView
    {
        Rendering::RenderTargetSize size;
        /// <summary>
        /// 지정하면 이 뷰의 렌더링과 가시 범위 수집에 함께 쓰인다. 다른 뷰의 장면 카메라로
        /// 먼저 거른 draw 목록을 재사용하지 않는다. 카메라 상태는 프로젝트 씬을 변경하지 않는다.
        /// </summary>
        std::optional<Rendering::CameraRenderData> camera;
        /// <summary>
        /// 이 뷰가 그릴 런타임이다. null이면 엔진이 부팅한 게임이다. 다른 런타임을 소유한
        /// bootstrap — 편집 대상 프로젝트를 자기 Game에 띄우는 에디터 — 가 그것을 가리킨다.
        /// 그 런타임의 업데이트는 bootstrap의 몫이다(<see cref="Update"/>).
        /// </summary>
        Runtime::Game* game = nullptr;
        /// <summary>
        /// 오버레이 패스 — 화면 공간 UI — 를 이 뷰에 그릴지이다. 게임을 보는 뷰는 그리고, 편집
        /// 카메라로 세계를 보는 뷰는 그리지 않는다.
        /// </summary>
        bool includeOverlay = true;
    };

    /// <summary>
    /// 이번 프레임에 캡처할 뷰들을 수집한다. 크기가 무효한 뷰는 렌더링이 생략되고, 뷰가 하나도
    /// 없으면 이번 프레임은 그리지 않는다 — 게임 업데이트는 그대로 돈다.
    ///
    /// 첫 뷰가 주 뷰다: 게임 카메라의 종횡비가 첫 뷰의 크기를 따른다.
    ///
    /// <see cref="CaptureView::game"/>이 가리키는 런타임은 이 호출과 다음 <see cref="Update"/>
    /// 사이에서만 유효하다고 본다. 그래서 루프는 업데이트 뒤에 이 목록을 다시 받는다:
    /// 업데이트가 그 런타임을 없앨 수 있고 — 에디터가 다른 프로젝트를 여는 것이 그렇다 —
    /// 그러면 업데이트 전에 받은 포인터는 해제된 객체를 가리킨다. 프레임마다 두 번 불릴 수
    /// 있으므로 이 함수는 부작용 없이 같은 답을 낼 수 있어야 한다.
    ///
    /// <b>여기서 받은 뷰는 게임 스레드를 떠나지 않는다.</b> 엔진이 이 스레드에서 그것을 읽어
    /// 프레임을 만들고, 렌더 스레드로 건너가는 것은 그 프레임과 뷰의 자리 번호뿐이다 — 그래서
    /// 위의 포인터를 다른 스레드가 보는 일은 없고, 그 수명 규칙은 이 스레드 안에서만 지키면
    /// 된다.
    /// </summary>
    virtual void CollectCaptureViews(std::vector<CaptureView>& views) { static_cast<void>(views); }

    /// <summary>
    /// 캡처된 뷰의 픽셀을 받는다. viewIndex는 CollectCaptureViews가 채운 목록의 자리다.
    ///
    /// 이미지는 이 호출 동안만 유효하므로, 보관하려면 받는 쪽이 복사한다 — 어차피 표시 형식으로의
    /// 변환이 그 복사다.
    ///
    /// <b>언제나 게임 스레드에서 불린다.</b> 그림은 렌더 스레드가 그렸을 수 있지만, 그 픽셀은
    /// 값으로 건너와 이 스레드가 쥔 뒤에 이 호출이 일어난다 — 받는 쪽이 자기 상태를 만져도
    /// 되는 이유가 그것이다. 그리고 그림은 <b>한 프레임 전의 것</b>이다.
    /// </summary>
    virtual void OnViewCaptured(std::size_t viewIndex, const Rendering::CapturedImage& image)
    {
        static_cast<void>(viewIndex);
        static_cast<void>(image);
    }

    /// <summary>
    /// 이번 프레임이 실패해 실행이 곧 끝난다는 것을 알린다. 프로세스가 살아 있는 마지막
    /// 순간이며, 저장하지 않은 작업물을 지킬 유일한 기회다.
    ///
    /// 이것이 필요한 이유는 프레임 실패가 곧 종료이기 때문이다: present 한 번이 실패하면 —
    /// 드라이버 업데이트나 GPU 리셋이면 그렇게 된다 — 루프는 false를 돌려주고 애플리케이션은
    /// 그대로 빠져나간다. 엔진은 무엇이 저장할 만한 것인지 모르므로, 아는 쪽이 여기서 한다.
    ///
    /// 여기서 하는 일은 짧고 실패해도 되는 것이어야 한다. 이미 무너지고 있는 프로세스이고,
    /// 그래픽 장치는 이미 없을 수 있다.
    /// </summary>
    /// <param name="reason">무엇이 실패했는지다. 로그에 실린다.</param>
    virtual void OnUnrecoverableFailure(const char* reason) { static_cast<void>(reason); }

    /// <summary>
    /// 사람이 창을 닫으려 할 때, 닫아도 되는지 답한다. false면 창이 남는다.
    ///
    /// 이 물음이 있는 이유는 저장되지 않은 작업이다. 무엇이 저장되지 않았는지는 프로젝트만
    /// 알고, 창은 모른다. 기본이 true인 것은 저장할 것이 없는 프로젝트에게는 닫기를 막을
    /// 이유가 없기 때문이다.
    ///
    /// 프레임 밖에서, 메시지를 처리한 직후에 불린다. 그래서 여기서 사람에게 묻는 동안 게임
    /// 업데이트나 렌더링이 도중에 끼어들지 않는다.
    /// </summary>
    /// <returns>닫아도 되면 true다.</returns>
    [[nodiscard]] virtual bool ShouldClose() { return true; }
};

}
