#pragma once

/// <summary>
/// 게임 스레드의 일과 렌더 스레드의 제출이 실제로 겹치는지, 그리고 프레임이 제출된 순서대로만
/// 도착하는지 확인한다. 「빨라졌다」가 아니라 「겹쳤다」가 이 단위의 합격 조건이다.
/// </summary>
[[nodiscard]] bool RunRenderThreadOverlapTests();

/// <summary>대기 중인 프레임이 한 번에 하나를 넘지 않는지 — 큐 깊이 1 — 확인한다.</summary>
[[nodiscard]] bool RunRenderThreadQueueDepthTests();

/// <summary>
/// 멈춘 뒤에는 장치를 부르는 것이 아무도 없는지 확인한다. 이 순서가 뒤집히면 이미 사라진 장치를
/// 부르는 종료 크래시가 되고, 그것은 화면으로도 잡히지 않는다.
/// </summary>
[[nodiscard]] bool RunRenderThreadShutdownOrderTests();

/// <summary>렌더 쪽 실패가 게임 스레드로 건너오고, 그 처리가 게임 스레드에 남는지 확인한다.</summary>
[[nodiscard]] bool RunRenderThreadFailureTests();

/// <summary>
/// 게임 스레드가 장치를 직접 부르는 동안 렌더 스레드가 같은 장치를 만지지 않는지 확인한다.
/// 캡처 뷰를 이미지로 받는 길이 그런 경우이고, 겹치면 로그 없는 크래시가 된다.
/// </summary>
[[nodiscard]] bool RunRenderThreadDeviceHandoverTests();

/// <summary>
/// 캡처가 주 프레임보다 먼저 그려지고, 그 픽셀이 채널을 실은 채 한 프레임 뒤에 돌아오는지
/// 확인한다. 순서가 뒤집히면 에디터의 뷰가 두 프레임 늦고, 채널이 없으면 돌아온 픽셀이 어느
/// 뷰의 것인지 말할 수 없다.
/// </summary>
[[nodiscard]] bool RunRenderThreadCaptureTests();

// 캡처 루프가 장치를 넘겨받느라 잃는 겹침의 수치는 여기 없다. 시간을 재는 것은 관측 도구의
// 일이라 `--measure`가 그것을 잰다(Benchmarks.cpp). 여기 남은 시험들이 지키는 것은 시간이
// 아니라 구조다: 장치에 두 스레드가 함께 들어가지 않는 것, 캡처가 없으면 겹침이 남는 것.
