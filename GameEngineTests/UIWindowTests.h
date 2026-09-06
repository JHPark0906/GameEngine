#pragma once

/// <summary>
/// 떠 있는 창이 포인터를 어떻게 가르는지 고정한다.
///
/// 요소 단위의 겹침 판정만으로는 답이 나오지 않는 물음 둘을 잰다: 위 창의 <b>빈 배경</b>이
/// 아래 창의 버튼을 가리는가, 그리고 모달이 선 동안 다른 창의 요소가 후보에서 빠지는가.
/// </summary>
[[nodiscard]] bool RunUIWindowOcclusionTests();

/// <summary>
/// 창을 앞으로 가져오는 것이 형제 순서를 바꾸는 것인지 확인한다. 순서가 하나여야 보이는 것과
/// 눌리는 것이 갈라지지 않는다.
/// </summary>
[[nodiscard]] bool RunUIWindowRaiseTests();
