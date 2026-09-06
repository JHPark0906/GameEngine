#pragma once

/// <summary>
/// 검증 오류의 이름이 값마다 하나씩, 서로 다르게 붙어 있는지 고정한다.
///
/// 열거자가 열넷이고 switch의 각 가지가 서로 닮았으므로, 한 가지를 베껴 쓰다 옆 값의 이름을
/// 그대로 두는 것이 이 표에서 가장 일어나기 쉬운 잘못이다. 컴파일러는 그것을 잡아 주지 않는다
/// — 빠진 값은 잡아 주지만, 두 값이 같은 이름을 돌려주는 것은 잡지 못한다.
/// </summary>
[[nodiscard]] bool RunRenderFrameValidationNameTests();
