#pragma once
#include "../Math/Vector.h"
#include "Component.h"

namespace GameEngine::Runtime
{

class UILayoutSystem;

/// <summary>
/// UI 요소가 부모 사각형 안에서 차지하는 자리다. 화면 UI의 배치는 이것이 정하며, 월드의
/// <see cref="Transform"/>은 건드리지 않는다.
///
/// 나누는 이유는 두 배치가 서로 다른 질문에 답하기 때문이다. Transform은 "이 물체가 세계
/// 어디에 있는가"를 3차원 TRS로 답하고, UI는 "이 요소가 부모의 어디를 어떻게 차지하는가"를
/// 픽셀과 비율로 답한다. 그래서 레이아웃이 계산한 결과를 Transform에 밀어 넣는 대신, UI를
/// 그리는 쪽이 여기서 사각형을 직접 읽는다 — 사용자가 인스펙터에서 만진 Transform이 다음
/// 프레임에 조용히 덮어써지는 일이 없다.
///
/// 계층은 <see cref="Transform"/>의 부모-자식 그대로다. UI 요소를 부모에 붙이는 방법이 다른
/// 게임 오브젝트와 같아야 하므로, 계층을 위한 두 번째 체계를 만들지 않는다.
/// </summary>
class RectTransform final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>화면 픽셀 단위의 사각형이다. 원점은 그리는 면의 좌상단이다.</summary>
    struct Rect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        [[nodiscard]] float GetCenterX() const { return x + width * 0.5f; }
        [[nodiscard]] float GetCenterY() const { return y + height * 0.5f; }
        [[nodiscard]] float GetRight() const { return x + width; }
        [[nodiscard]] float GetBottom() const { return y + height; }

        [[nodiscard]] bool Contains(const float pointX, const float pointY) const
        {
            return pointX >= x && pointX < GetRight() && pointY >= y && pointY < GetBottom();
        }

        /// <summary>넓이가 있는지다. 잘려 없어진 사각형은 비어 있다.</summary>
        [[nodiscard]] bool IsEmpty() const { return width <= 0.0f || height <= 0.0f; }

        /// <summary>
        /// 두 사각형이 함께 덮는 부분이다. 겹치지 않으면 넓이가 0인 사각형이 된다 — 음수 크기를
        /// 흘려보내지 않는 것이 배치 전체의 규칙이다.
        /// </summary>
        /// <param name="other">교차할 상대 사각형이다.</param>
        /// <returns>교차 사각형이며, 겹치지 않으면 비어 있다.</returns>
        [[nodiscard]] Rect IntersectedWith(const Rect& other) const
        {
            const float left = x > other.x ? x : other.x;
            const float top = y > other.y ? y : other.y;
            const float right = GetRight() < other.GetRight() ? GetRight() : other.GetRight();
            const float bottom = GetBottom() < other.GetBottom() ? GetBottom() : other.GetBottom();
            Rect result;
            result.x = left;
            result.y = top;
            result.width = right > left ? right - left : 0.0f;
            result.height = bottom > top ? bottom - top : 0.0f;
            return result;
        }
    };

    /// <summary>
    /// 부모 사각형에서 이 요소의 좌상단 모서리가 붙는 자리다. 0..1의 비율이며 x는 왼쪽에서,
    /// y는 위에서 잰다. 부모와 함께 늘어나는 요소는 min과 max를 다르게 두고, 크기가 고정된
    /// 요소는 둘을 같게 둔다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetAnchorMin() const { return mAnchorMin; }
    void SetAnchorMin(const Math::Vector2& anchorMin) { mAnchorMin = anchorMin; }

    /// <summary>부모 사각형에서 이 요소의 우하단 모서리가 붙는 자리다. 0..1의 비율이다.</summary>
    [[nodiscard]] const Math::Vector2& GetAnchorMax() const { return mAnchorMax; }
    void SetAnchorMax(const Math::Vector2& anchorMax) { mAnchorMax = anchorMax; }

    /// <summary>
    /// 좌상단 앵커에서 이 요소의 좌상단 모서리까지의 거리다. 픽셀 단위이며 오른쪽·아래가
    /// 양수다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetOffsetMin() const { return mOffsetMin; }
    void SetOffsetMin(const Math::Vector2& offsetMin) { mOffsetMin = offsetMin; }

    /// <summary>
    /// 우하단 앵커에서 이 요소의 우하단 모서리까지의 거리다. 픽셀 단위이며 오른쪽·아래가
    /// 양수다. 앵커가 한 점일 때 offsetMax − offsetMin이 곧 이 요소의 크기다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetOffsetMax() const { return mOffsetMax; }
    void SetOffsetMax(const Math::Vector2& offsetMax) { mOffsetMax = offsetMax; }

    /// <summary>
    /// 이번 프레임에 이 요소가 실제로 차지한 사각형이다. <see cref="UILayoutSystem"/>이 매
    /// 프레임 다시 계산하므로 저장되지 않으며, 레이아웃이 아직 돌지 않았으면 비어 있다.
    /// </summary>
    [[nodiscard]] const Rect& GetResolvedRect() const { return mResolvedRect; }

    /// <summary>
    /// 계산된 사각형 중 조상 마스크에 잘려 나가고 남은 부분이다. 마스크가 없으면 계산된 사각형과
    /// 같고, 완전히 가려졌으면 비어 있다.
    ///
    /// 자리와 보이는 부분을 따로 두는 이유는 둘이 서로 다른 질문에 답하기 때문이다. 그리기는
    /// 자리를 알아야 한다 — 절반이 가려진 행은 절반 크기로 줄어드는 것이 아니라 원래 크기로
    /// 놓인 채 잘리는 것이다. 반면 커서가 이 요소를 맞혔는지는 보이는 부분이 답한다: 가려진
    /// 절반을 누른 것은 이 요소를 누른 것이 아니다.
    /// </summary>
    [[nodiscard]] const Rect& GetVisibleRect() const { return mVisibleRect; }

    /// <summary>글자나 자식이 요구하는 크기다. 픽셀 단위이며 자리는 담지 않는다.</summary>
    struct Size
    {
        float width = 0.0f;
        float height = 0.0f;
    };

    /// <summary>
    /// 이 요소의 내용이 요구하는 크기다. 재기 단계가 채우고 놓기 단계가 읽는다.
    ///
    /// 자리와 따로 있는 이유는 둘의 흐름이 반대이기 때문이다. 자리는 부모에서 자식으로
    /// 내려오지만, 내용이 얼마나 필요한지는 자식에서 부모로 올라온다 — 목록의 높이는 그
    /// 안의 행들이 정하고, 행의 높이는 그 안의 글자가 정한다. 한 순회로는 두 방향을 함께
    /// 흘릴 수 없으므로 요구 크기를 여기 적어 두고 다음 순회가 읽는다.
    /// </summary>
    [[nodiscard]] const Size& GetDesiredSize() const { return mDesiredSize; }

private:
    // 계산된 사각형을 쓰는 것은 레이아웃 하나다. 다른 곳이 이것을 고칠 수 있으면 화면에 보이는
    // 자리와 앵커 선언이 어긋날 수 있고, 그 어긋남은 다음 프레임에 조용히 사라져 재현되지 않는다.
    friend class UILayoutSystem;
    void SetResolvedRect(const Rect& rect) { mResolvedRect = rect; }
    void SetVisibleRect(const Rect& rect) { mVisibleRect = rect; }
    void SetDesiredSize(const Size& size) { mDesiredSize = size; }

    Math::Vector2 mAnchorMin{ 0.0f, 0.0f };
    Math::Vector2 mAnchorMax{ 0.0f, 0.0f };
    Math::Vector2 mOffsetMin{ 0.0f, 0.0f };
    Math::Vector2 mOffsetMax{ 100.0f, 100.0f };
    Rect mResolvedRect;
    Rect mVisibleRect;
    Size mDesiredSize;
};

}
