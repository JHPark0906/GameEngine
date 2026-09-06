#pragma once
#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 이 아래의 UI가 이 오브젝트의 사각형 밖으로 나가지 못하게 한다.
///
/// 마스크의 잘림 규칙은 계층에 한 번 붙으며, 모든 자손에 같은 범위가 적용된다.
/// 요소마다 범위를 따로 계산하지 않으므로 내용의 표시와 클릭 판정이 같은 경계를 따른다.
///
/// 속성이 없다. 자를 사각형은 이 오브젝트의 <see cref="RectTransform"/>이 이미 말하고 있고,
/// 마스크가 더할 말은 "여기서 자른다"뿐이다.
///
/// 잘림은 요소 단위다: 완전히 벗어난 요소는 사라지고, 걸친 요소는 보이는 부분만 커서에
/// 맞는다. 픽셀 단위로 반쪽만 그려 내는 것은 프레임에 시저 사각형이 필요한 별개의 일이다.
/// </summary>
class RectMask final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
};

}
