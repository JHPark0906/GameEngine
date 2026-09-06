#pragma once

/// <summary>
/// 물음들을 한 줄로 세우는 규칙이다. 한 번에 하나만 서고, 답은 정확히 한 번 오며, 답이 없는
/// 것은 "아무것도 하지 않음"이다 — 이 셋이 틀리면 사람이 고르지 않은 동작이 일어나거나 같은
/// 동작이 두 번 일어난다.
/// </summary>
[[nodiscard]] bool RunConfirmationQueueTests();

/// <summary>
/// 물음이 실제로 세워졌을 때의 자리다. 버튼들이 겹치지 않고 창 안에 들어가는지, 물음이
/// 길어지면 창이 그만큼 높아지는지, 모달로 서는지를 창 없이 사각형으로 잰다.
/// </summary>
[[nodiscard]] bool RunConfirmationViewTests();

/// <summary>
/// 창의 폭이 담긴 것을 따르는지 확인한다. 긴 라벨은 창을 넓히고, 상한에 걸리면 거기서 멈추고
/// 라벨이 말줄임된다 — 어느 쪽도 소리 없이 사라지지 않는다.
/// </summary>
[[nodiscard]] bool RunConfirmationWidthTests();

/// <summary>
/// 지금은 고를 수 없는 선택지가 보이되 눌리지 않는지 확인한다. 감추면 사람이 그 길이 있다는
/// 것 자체를 모르고, 누를 수 있게 두면 눌러도 아무 일이 없는 버튼이 된다.
/// </summary>
[[nodiscard]] bool RunConfirmationDisabledChoiceTests();

/// <summary>
/// 창의 폭에 콘텐츠 배율을 한 번만 적용하는지 확인한다.
/// 논리 단위 오프셋은 사각형을 풀 때 변환하므로 호출자가 미리 배율을 곱하면 안 된다.
/// 배율 1은 중복 적용을 구분할 수 없어 배율 2에서도 검사한다.
/// </summary>
[[nodiscard]] bool RunConfirmationScaleTests();

/// <summary>
/// 확인 창의 제목과 각 선택지의 라벨·화면 사각형이 로그에 남는지 확인한다.
/// 버튼은 별도 OS 창이 아니므로 자동화는 추측한 좌표 대신 이 정보를 사용해야 한다.
/// </summary>
[[nodiscard]] bool RunConfirmationLogTests();
