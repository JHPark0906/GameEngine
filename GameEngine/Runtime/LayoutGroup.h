#pragma once

#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 자기 크기를 <b>자식들이 요구한 크기</b>에서 유도하는 컴포넌트다.
///
/// <b>유니티의 LayoutGroup과 다른 점이 하나 있다: 자식의 자리는 정하지 않는다.</b> 유니티에서는
/// 같은 이름의 컴포넌트가 크기와 자리를 함께 정하지만, 여기서 자식의 자리는 그 자식의 앵커와
/// 오프셋이 이미 말하고 있고, 툴바처럼 놓는 규칙(넘치면 접기)을 따로 가진 부모도 있다. 자리를
/// 여기서도 정하면 한 사각형을 두 규칙이 각자 쓰게 되고, 그때 화면에 나타나는 값은 둘 중 나중에
/// 쓴 쪽이다 — 어느 쪽이 이길지가 호출 순서로 정해지는 배치는 고칠 수가 없다.
///
/// <see cref="ContentFit"/>과 나란히 두고 읽어야 뜻이 분명해진다. ContentFit은 자식이 <b>몇
/// 개인지</b>로 크기를 구한다 — 줄 높이가 같은 목록이 그것이고, 거기서는 자식 하나하나가
/// 얼마나 필요한지가 답에 들어가지 않는다. 이쪽은 반대로 자식이 <b>저마다 얼마나 필요한지</b>를
/// 더한다. 글자 길이가 제각각인 버튼과 메뉴 항목이 그런 자리다.
///
/// <see cref="LayoutElement"/>와도 갈라진다. LayoutElement는 <b>같은 오브젝트의</b> 글자를 재고,
/// 이쪽은 <b>자식들의 답</b>을 받는다. 글자를 자식 사각형에 두는 버튼은 그래서 둘을 함께 쓴다:
/// 글자를 가진 자식이 LayoutElement로 자기 폭을 말하고, 부모가 이 컴포넌트로 그것을 받는다.
/// </summary>
class LayoutGroup final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>자식들이 놓이는 방향이다. 그 방향으로 더하고, 가로지르는 쪽은 가장 큰 것을 쓴다.</summary>
    enum class Direction
    {
        Horizontal,
        Vertical,
    };

    [[nodiscard]] Direction GetDirection() const { return mDirection; }
    void SetDirection(const Direction direction) { mDirection = direction; }

    /// <summary>이웃한 두 자식 사이의 간격이다. 자식이 하나면 쓰이지 않는다.</summary>
    [[nodiscard]] float GetSpacing() const { return mSpacing; }
    void SetSpacing(float spacing);

    /// <summary>안쪽 여백이다. 두 방향 모두 양 끝에 한 번씩 더해진다.</summary>
    [[nodiscard]] float GetPadding() const { return mPadding; }
    void SetPadding(float padding);

    /// <summary>
    /// 자식들의 요구 크기에서 이 요소의 요구 크기를 구한다.
    ///
    /// 계층 없이 이 한 식만으로 규칙을 확인할 수 있도록 정적이다 — 유도 규칙이 곧 이 함수다.
    /// </summary>
    /// <param name="alongAxis">쌓이는 방향으로 각 자식이 요구한 크기들의 합이다.</param>
    /// <param name="acrossAxis">가로지르는 방향으로 가장 큰 자식이 요구한 크기다.</param>
    /// <param name="childCount">자식의 수다. 간격이 몇 군데 들어가는지가 여기서 나온다.</param>
    /// <returns>쌓이는 방향과 가로지르는 방향의 요구 크기다.</returns>
    [[nodiscard]] static float MeasureAlong(
        float alongAxis, int childCount, float spacing, float padding);

    /// <summary>가로지르는 방향의 요구 크기다. 가장 큰 자식에 여백을 더한 값이다.</summary>
    [[nodiscard]] static float MeasureAcross(float acrossAxis, float padding);

private:
    Direction mDirection = Direction::Horizontal;
    float mSpacing = 0.0f;
    float mPadding = 0.0f;
};

}
