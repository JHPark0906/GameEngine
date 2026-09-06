#pragma once
#include "../Math/Vector.h"
#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 이 요소가 자기 내용에 맞는 크기를 요구하게 한다.
///
/// 앵커와 오프셋은 "부모의 어디를 차지하는가"를 선언하지만, 그 선언은 안에 무엇이 들어가는지
/// 모른다. 그래서 글자가 자리보다 길면 잘리고, 자리가 남으면 빈다. 이 컴포넌트는 그 반대
/// 방향을 연다: 요소가 <b>필요한 크기를 답하고</b> 배치가 그 답을 존중한다.
///
/// 요구하는 크기는 두 값의 큰 쪽이다 — 내용을 재서 나온 <b>선호 크기</b>와, 여기에 적어 둔
/// <b>최소 크기</b>. 내용을 잴 수 없는 환경에서도 최소 크기는 지켜지므로, 폰트를 열지 못한
/// 화면에서 요소가 사라지지 않는다.
///
/// 크기가 자라도 좌상단 모서리는 움직이지 않는다. 어느 모서리를 붙잡을지는 pivot의 일인데
/// 아직 pivot이 없고, 없는 것을 있는 척 고르는 대신 규칙 하나를 정해 적어 둔다.
///
/// 재는 대상은 같은 오브젝트의 <see cref="TextRenderer"/>다. 오늘 크기를 요구할 만한 내용이
/// 글자뿐이기 때문이며, 다른 내용이 생기면 그때 이 목록이 늘어난다.
/// </summary>
class LayoutElement final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>내용에 맞출 방향이다.</summary>
    enum class Fit : unsigned char
    {
        /// <summary>맞추지 않는다. 최소 크기만 지켜진다.</summary>
        None,
        /// <summary>가로만 내용에 맞춘다. 한 줄 라벨과 버튼이 이것이다.</summary>
        Horizontal,
        /// <summary>세로만 내용에 맞춘다.</summary>
        Vertical,
        /// <summary>양쪽 모두 내용에 맞춘다.</summary>
        Both,
    };

    /// <summary>
    /// 글자가 주어진 폭에 맞춰 접히는지다. 접히면 이 요소는 가로를 요구하지 않고 — 받은 폭을
    /// 그대로 받아들이고 — 그 폭에서 필요한 <b>높이</b>만 요구한다.
    ///
    /// 접기가 이 컴포넌트의 다른 성질과 다른 점이 하나 있다: 요구하는 크기가 더는 내재적이지
    /// 않다. 접힌 높이는 폭의 함수이므로, 재기가 부모에게서 쓸 수 있는 폭을 함께 받아야 답이
    /// 나온다. 배치가 두 단계로 갈라진 이유가 결국 이것이다.
    /// </summary>
    [[nodiscard]] bool IsWrapping() const { return mWrap; }
    void SetWrapping(const bool wrap) { mWrap = wrap; }

    /// <summary>이 요소가 내용에 맞출 방향이다.</summary>
    [[nodiscard]] Fit GetFit() const { return mFit; }
    void SetFit(const Fit fit) { mFit = fit; }

    /// <summary>
    /// 내용 둘레에 두는 여백이다. x는 좌우를 합한 값, y는 상하를 합한 값이며 픽셀 단위다.
    /// 면의 배율이 곱해진다 — 여백은 픽셀이므로 화면을 탄다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetPadding() const { return mPadding; }
    void SetPadding(const Math::Vector2& padding) { mPadding = padding; }

    /// <summary>
    /// 내용과 무관하게 지켜지는 최소 크기다. 픽셀 단위이며 면의 배율이 곱해진다. 내용이 없거나
    /// 잴 수 없을 때 남는 크기가 이것이다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetMinimumSize() const { return mMinimumSize; }
    void SetMinimumSize(const Math::Vector2& minimumSize) { mMinimumSize = minimumSize; }

private:
    Fit mFit = Fit::Horizontal;
    bool mWrap = false;
    Math::Vector2 mPadding{ 0.0f, 0.0f };
    Math::Vector2 mMinimumSize{ 0.0f, 0.0f };
};

}
