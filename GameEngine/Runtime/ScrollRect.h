#pragma once

#include "../Math/Vector.h"
#include "Component.h"

namespace GameEngine::Runtime
{

class UILayoutSystem;

/// <summary>
/// 자기 사각형(viewport)보다 큰 내용을 그 안에서 밀어 보여 주는 컴포넌트다. 내용은 첫 번째
/// 자식이며, 스크롤은 그 자식의 오프셋을 움직이는 것이다.
///
/// 잘라내는 일은 이 컴포넌트가 하지 않는다. viewport에 함께 붙는 마스크가 "이 서브트리의
/// 사각형은 마스크와 교차되고, 완전히 벗어난 요소는 그려지지도 클릭을 받지도 않는다"를 맡고,
/// 여기서는 <b>무엇이 얼마나 밀렸는지</b>만 정한다. 둘을 한 컴포넌트에 넣지 않는 이유는 잘라내기가
/// 스크롤에만 필요한 일이 아니기 때문이다.
///
/// 스크롤 가능한 범위는 선언되지 않고 <see cref="ContentFit"/>이 유도한 내용 크기에서 나온다.
/// 그래서 "몇 줄만 보인다" 같은 상수가 이 구조에는 없다 — 있을 자리가 없다.
/// </summary>
class ScrollRect final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>세로로 밀 수 있는지다.</summary>
    [[nodiscard]] bool IsVertical() const { return mVertical; }
    void SetVertical(const bool vertical) { mVertical = vertical; }

    /// <summary>가로로 밀 수 있는지다.</summary>
    [[nodiscard]] bool IsHorizontal() const { return mHorizontal; }
    void SetHorizontal(const bool horizontal) { mHorizontal = horizontal; }

    /// <summary>
    /// 내용이 밀려 있는 거리다. 픽셀이며 양수가 "내용이 위로/왼쪽으로 올라간" 방향이다. 매
    /// 프레임 범위 안으로 눌리므로, 여기에 넣은 값이 그대로 남지 않을 수 있다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetScrollOffset() const { return mScrollOffset; }
    void SetScrollOffset(const Math::Vector2& offset) { mScrollOffset = offset; }

    /// <summary>
    /// 한 축의 스크롤 거리를 범위 안으로 누른다.
    ///
    /// 범위는 "내용이 viewport보다 넘치는 만큼"이고, 넘치지 않으면 0이다 — <b>viewport보다 작은
    /// 내용은 밀리지 않는다</b>. 이 규칙이 없으면 짧은 목록이 화면 밖으로 밀려 사라질 수 있고,
    /// 그 상태에서는 되돌릴 방법이 보이지 않는다.
    ///
    /// 계층 없이 이 한 식만으로 검증할 수 있도록 정적이다.
    /// </summary>
    /// <param name="offset">누르기 전의 거리다.</param>
    /// <param name="viewportExtent">viewport가 그 축으로 차지하는 크기다.</param>
    /// <param name="contentExtent">내용이 그 축으로 차지하는 크기다.</param>
    /// <returns>0과 넘치는 양 사이로 눌린 거리다.</returns>
    [[nodiscard]] static float ClampAxis(
        float offset, float viewportExtent, float contentExtent);

private:
    bool mVertical = true;
    bool mHorizontal = false;
    Math::Vector2 mScrollOffset{ 0.0f, 0.0f };
};

}
