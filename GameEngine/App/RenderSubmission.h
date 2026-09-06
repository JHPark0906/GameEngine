#pragma once

#include <vector>

#include "../Rendering/RenderFrame.h"

namespace GameEngine::App
{

/// <summary>
/// 이 파일의 계약: 여기 있는 타입은 스레드를 건너므로, 어떤 멤버도 자기가 소유하지 않은 것을
/// 생 포인터로 가리키지 않는다. 값이거나 <c>shared_ptr&lt;const T&gt;</c>다.
///
/// <c>RenderFrame.h</c>가 자기 자리에 적어 둔 것과 같은 규칙이고 이유도 같다: 게임 스레드는
/// 넘긴 직후 장면을 계속 바꾸므로, 넘어간 것이 무언가를 가리키고 있으면 렌더 쪽은 이미 사라진
/// 것을 읽는다. 그 실패는 로그도 예외도 없다. `GameEngineTests`의 프레임 포인터 계약 시험이
/// 이 파일도 함께 훑는다.
/// </summary>

/// <summary>
/// 렌더 스레드가 이미지로 그려 줄 뷰 하나다.
///
/// 프레임은 게임 스레드가 <c>Collect</c>까지 마쳐 넘긴 것이라, 렌더 스레드는 장면을 보지 않고
/// 이 값만 본다. 채널은 뷰의 자리 번호이며 <b>이미지를 되돌려줄 때 그대로 실려 돌아온다</b> —
/// 숨겨진 뷰가 자리를 비워도 뒤 뷰의 번호가 흔들리지 않는 것이 이 규약의 요점이고, 그것이
/// 없으면 돌아온 픽셀이 어느 뷰의 것인지 말할 수 없다.
/// </summary>
struct CaptureJob
{
    Rendering::RenderFrame frame;
    unsigned int channel = 0;
};

/// <summary>
/// 렌더 스레드가 그려 돌려준 뷰 하나의 픽셀이다.
///
/// 픽셀을 값으로 쥔다: 이 이미지가 게임 스레드로 건너갈 때 렌더 스레드는 이미 다음 일을 하고
/// 있으므로, 빌려주는 것으로는 살아남지 못한다.
/// </summary>
struct CapturedView
{
    unsigned int channel = 0;
    Rendering::CapturedImage image;
};

/// <summary>
/// 한 프레임에 렌더 스레드가 할 일 전부다.
///
/// 캡처가 주 프레임보다 <b>먼저</b> 처리된다는 것이 이 타입이 하나인 이유다. 둘을 따로 넘기면
/// 순서가 우연이 되고, 캡처가 다음 패스로 밀리는 순간 에디터의 뷰가 두 프레임 늦는다. 하나로
/// 묶여 있으면 그 순서는 이 구조의 성질이지 타이밍의 성질이 아니다.
/// </summary>
struct FrameSubmission
{
    /// <summary>이미지로 그릴 뷰들이다. 주 프레임보다 먼저 처리된다.</summary>
    std::vector<CaptureJob> captures;
    /// <summary>창에 present할 프레임이다.</summary>
    Rendering::RenderFrame frame;
};

}
