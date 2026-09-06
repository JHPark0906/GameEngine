#pragma once

#include "Component.h"

namespace GameEngine::Runtime
{

class UILayoutSystem;

/// <summary>
/// 자기 <see cref="RectTransform"/>의 크기를 자식의 수에서 유도하는 컴포넌트다. 스크롤되는
/// 목록의 "내용"이 이것을 달고, 그 유도된 크기가 곧 스크롤할 수 있는 범위가 된다.
///
/// 크기를 선언하지 않고 유도하는 것이 이 컴포넌트의 전부이며, 그것이 요점이다. "여섯 줄만
/// 보인다" 같은 상수는 내용이 실제로 몇 줄인지와 아무 관계가 없다. 크기가 자식에서 나오면 그
/// 상수를 <b>적을 자리 자체가 없다</b>.
/// </summary>
class ContentFit final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>자식이 쌓이는 방향이다.</summary>
    enum class Direction
    {
        Vertical,
        Horizontal,
    };

    [[nodiscard]] Direction GetDirection() const { return mDirection; }
    void SetDirection(const Direction direction) { mDirection = direction; }

    /// <summary>자식 하나가 쌓이는 방향으로 차지하는 크기다. 픽셀이다.</summary>
    [[nodiscard]] float GetItemSize() const { return mItemSize; }
    void SetItemSize(float itemSize);

    /// <summary>이웃한 두 자식 사이의 간격이다. 픽셀이다.</summary>
    [[nodiscard]] float GetSpacing() const { return mSpacing; }
    void SetSpacing(float spacing);

    /// <summary>앞뒤 여백이다. 쌓이는 방향의 양 끝에 한 번씩 더해진다.</summary>
    [[nodiscard]] float GetPadding() const { return mPadding; }
    void SetPadding(float padding);

    /// <summary>
    /// 자식 <paramref name="childCount"/>개가 필요로 하는 크기다. 자식이 없으면 여백뿐이다.
    ///
    /// 계층 없이 이 한 식만으로 검증할 수 있도록 정적이다 — 유도 규칙이 곧 이 함수다.
    /// </summary>
    /// <param name="childCount">쌓이는 자식의 수다.</param>
    /// <returns>쌓이는 방향으로 필요한 픽셀 크기다.</returns>
    [[nodiscard]] float MeasureExtent(int childCount) const;

private:
    Direction mDirection = Direction::Vertical;
    float mItemSize = 24.0f;
    float mSpacing = 0.0f;
    float mPadding = 0.0f;
};

}
