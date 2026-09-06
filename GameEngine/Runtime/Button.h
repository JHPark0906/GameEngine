#pragma once

#include "../Math/Color.h"
#include "Selectable.h"

namespace GameEngine::Runtime
{

class UIEventSystem;

/// <summary>
/// 눌리는 UI 요소다. 자기 <see cref="RectTransform"/>이 차지한 사각형 안에서 커서를 받아
/// hover·press·click 상태를 갖고, 같은 오브젝트의 <see cref="SpriteRenderer"/>가 있으면 그
/// 상태에 맞는 색으로 물들인다.
///
/// 상태를 스스로 정하지 않고 <see cref="UIEventSystem"/>에게서 받는 이유는, 어느 요소가 커서를
/// 받는지가 그 요소 혼자로는 답할 수 없는 질문이기 때문이다 — 겹친 요소들 중 누가 위인지, 이미
/// 다른 요소가 눌림을 잡고 있는지는 계층 전체를 봐야 안다. 소리를 상태에 맞추는
/// <see cref="AudioSystem"/>, 사각형을 계산하는 <see cref="UILayoutSystem"/>과 같은 자리다.
/// </summary>
class Button final : public Selectable
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>평상시 색이다. 같은 오브젝트에 SpriteRenderer가 있을 때만 쓰인다.</summary>
    [[nodiscard]] const Math::Color& GetNormalColor() const { return mNormalColor; }
    void SetNormalColor(const Math::Color& color) { mNormalColor = color; }

    /// <summary>커서가 올라와 있을 때의 색이다.</summary>
    [[nodiscard]] const Math::Color& GetHoveredColor() const { return mHoveredColor; }
    void SetHoveredColor(const Math::Color& color) { mHoveredColor = color; }

    /// <summary>눌려 있을 때의 색이다.</summary>
    [[nodiscard]] const Math::Color& GetPressedColor() const { return mPressedColor; }
    void SetPressedColor(const Math::Color& color) { mPressedColor = color; }

    /// <summary>입력을 받지 않을 때의 색이다.</summary>
    [[nodiscard]] const Math::Color& GetDisabledColor() const { return mDisabledColor; }
    void SetDisabledColor(const Math::Color& color) { mDisabledColor = color; }

    /// <summary>커서가 이 버튼 위에 있는지다. 눌림을 다른 버튼이 잡고 있으면 거짓이다.</summary>
    [[nodiscard]] bool IsHovered() const { return mHovered; }

    /// <summary>
    /// 이 버튼이 눌린 채인지다. 누른 채 버튼 <b>밖으로</b> 나가면 거짓이 된다 — 그 모습이
    /// 사람에게 「여기서 떼면 취소」를 말한다.
    /// </summary>
    [[nodiscard]] bool IsPressed() const { return mPressed; }

    /// <summary>
    /// 이 버튼이 포인터를 <b>쥐고 있는지</b>다. 누른 순간부터 뗄 때까지, 커서가 어디로 가든
    /// 참이다.
    ///
    /// 끌기를 읽는 쪽이 보아야 하는 것이 이것이지 <see cref="IsPressed"/>가 아니다. 끌리는
    /// 것은 커서 아래에 머물지 않으며 — 창을 끌면 창이 움직여 커서가 제목줄 밖으로 나간다 —
    /// 눌림으로 읽으면 그 프레임에 끌기가 끝났다가 다음 프레임에 새 끌기로 다시 시작한다.
    /// </summary>
    [[nodiscard]] bool HoldsPointer() const { return mHoldsPointer; }

    /// <summary>
    /// 이번 프레임에 클릭이 완성됐는지다. 누른 자리와 뗀 자리가 모두 이 버튼일 때만 참이며,
    /// 다음 프레임에는 다시 거짓이다.
    ///
    /// 스크립트는 <c>Update</c>에서 이것을 읽는다. 콜백을 등록하는 대신 상태로 두는 이유는
    /// 장면 파일이 담을 수 있는 것이 값이지 함수가 아니기 때문이다 — 콜백을 직렬화하려면
    /// 이름으로 찾는 두 번째 체계가 필요하고, 그것은 이 단위의 몫이 아니다.
    /// </summary>
    [[nodiscard]] bool WasClickedThisFrame() const { return mClicked; }

private:
    // 상태를 쓰는 것은 이벤트 시스템 하나다. 다른 곳이 이것을 고칠 수 있으면 화면이 말하는
    // 상태와 실제로 커서를 받은 요소가 어긋날 수 있다.
    friend class UIEventSystem;

    /// <summary>이번 프레임의 상태를 받고, SpriteRenderer가 있으면 그 색을 맞춘다.</summary>
    void ApplyPointerState(bool hovered, bool pressed, bool clicked, bool holdsPointer);

    /// <summary>받지 않게 된 버튼은 눌린 채로 남지 않는다. 보이는 상태만 여기서 정리한다.</summary>
    void OnInteractableLost() override;

    // 기본값은 밝기 배수가 아닌 색이다. 그림 없는 UI 요소는 1x1 흰 픽셀에 틴트를 곱하므로
    // 틴트가 화면의 색을 결정한다. 같은 화면에 함께 놓이는 즉시 모드 UI와 색을 맞춘다.
    Math::Color mNormalColor{ 0.22f, 0.24f, 0.27f, 1.0f };
    Math::Color mHoveredColor{ 0.28f, 0.31f, 0.35f, 1.0f };
    Math::Color mPressedColor{ 0.16f, 0.17f, 0.19f, 1.0f };
    // 비활성만 아래로 뺀다. 위로는 길이 없다 — 이 값이 구별되어야 하는 면 중 가장 밝은
    // 것(툴바 띠 0.20)과 normal(0.27) 사이의 폭 전체가 17.85뿐이라, 양쪽에서 동시에 12를
    // 넘길 수 없다. 아래로는 가장 어두운 면인 띠(23,26,28)까지 28만큼의 자리가 있고 그중
    // 12를 문턱이 먹으므로 여유의 상한이 16이다. 여유 15.2는 상한의 95%이며 색은
    // 띠의 절반 밝기에 머물러, 검정에 붙어 "구멍"처럼 보이는 쪽으로는 가지 않는다.
    Math::Color mDisabledColor{ 0.040f, 0.045f, 0.050f, 1.0f };

    bool mHovered = false;
    bool mPressed = false;

    /// <summary>포인터를 쥐고 있는지다. 누른 순간부터 뗄 때까지 자리와 무관하게 참이다.</summary>
    bool mHoldsPointer = false;
    bool mClicked = false;
};

}
