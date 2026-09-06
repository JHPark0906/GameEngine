#pragma once
#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 이 아래의 UI를 하나의 창으로 묶는다. 창은 자기 사각형 안의 포인터를 자기 것으로 주장한다.
///
/// 요소 단위의 겹침 판정만으로는 답할 수 없는 물음이 둘 있어서 이 표시가 필요하다.
///
/// 하나는 <b>가림</b>이다. 위에 뜬 창의 빈 자리 — 요소가 없는 배경 — 아래에 다른 창의 버튼이
/// 있으면, 요소만 훑는 판정은 그 버튼을 커서 아래 최상단으로 고른다. 사람 눈에는 위 창의
/// 배경이 그 버튼을 덮고 있으므로, 보이지 않는 것이 눌리는 셈이다. 창을 알면 "커서가 더 위
/// 창의 사각형 안에 있으면 아래 창의 요소는 후보가 아니다"로 답할 수 있다.
///
/// 다른 하나는 <b>모달</b>이다. 모달인 동안 받아들일 요소는 그 창에 속한 것뿐이며, 이것은
/// 요소 하나를 잡는 것(capture)과 다른 층의 규칙이다 — 잡음은 한 제스처의 임자를 정하고,
/// 모달은 어느 요소들이 애초에 후보가 되는지를 정한다. 그래서 화면 전체를 덮는 투명한 차단
/// 오브젝트를 놓지 않는다: 막는 것은 물건이 아니라 규칙이고, 물건으로 만들면 그 물건의
/// 자리·순서·잘림이 다시 문제가 된다.
///
/// 창들 사이의 순서는 <b>계층 순서</b>다. 뒤에 오는 형제가 위이며, 그리는 순서도 같다.
/// z 값을 따로 두지 않는 이유는 순서가 둘이 되면 어긋날 수 있고, 그 어긋남이 곧 "보이는 것과
/// 눌리는 것이 다르다"는 증상이기 때문이다. 창을 앞으로 가져오는 것은
/// <see cref="Transform::SetAsLastSibling"/>이다.
///
/// 창의 사각형은 이 오브젝트의 <see cref="RectTransform"/>이 말한다. 창이 더할 말은 "여기부터
/// 하나의 창이다"와 "지금 모달인가"뿐이다.
/// </summary>
class UIWindow final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>
    /// 이 창이 모달인지다. 모달인 동안 다른 창의 요소는 커서를 받지 못한다.
    ///
    /// 모달이 둘 이상이면 계층에서 가장 뒤에 있는 것 — 가장 위 — 이 이긴다. 그것이 마지막에
    /// 열린 창이므로 사람이 지금 답해야 하는 물음이다.
    /// </summary>
    [[nodiscard]] bool IsModal() const { return mModal; }
    void SetModal(const bool modal) { mModal = modal; }

private:
    bool mModal = false;
};

}
