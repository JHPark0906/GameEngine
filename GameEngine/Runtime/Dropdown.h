#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../Core/ChoiceModel.h"
#include "Selectable.h"

namespace GameEngine::Runtime
{

class UIEventSystem;

/// <summary>
/// 목록에서 하나를 고르는 UI 요소다. 자기 <see cref="RectTransform"/>이 차지한 사각형이 머리
/// 칸이고, 펼쳐지면 그 아래로 항목 한 줄씩이 이어진다. 머리 칸을 누르면 펼쳐지고, 항목을 누르면
/// 그것이 값이 되며 접힌다. 고른 항목의 이름은 같은 오브젝트의 <see cref="TextRenderer"/>가
/// 그린다.
///
/// 고르는 규칙을 스스로 갖지 않고 <see cref="Core::ChoiceModel"/>에게 맡기는 이유는 즉시 모드
/// 에디터 UI의 에셋 선택 칸이 같은 규칙을 쓰기 때문이다 — <see cref="InputField"/>가 편집
/// 규칙을 <see cref="Core::TextEditModel"/>과 나누는 것과 같다.
///
/// 어느 요소가 커서를 받는지는 <see cref="Button"/>과 같은 이유로 <see cref="UIEventSystem"/>이
/// 정한다. 항목 줄은 그리지 않는다 — 버튼이 자기 그림을 만들지 않는 것과 같다.
/// </summary>
class Dropdown final : public Selectable
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>고를 수 있는 항목들이다. 값은 이 목록의 인덱스다.</summary>
    [[nodiscard]] const std::vector<std::string>& GetOptions() const { return mOptions; }

    /// <summary>목록을 통째로 바꾼다. 값이 새 목록의 범위를 벗어나면 값이 없어진다.</summary>
    void SetOptions(std::vector<std::string> options);

    /// <summary>고른 항목의 인덱스다. 고른 것이 없으면 -1이다.</summary>
    [[nodiscard]] int GetValue() const;

    /// <summary>값을 정한다. 범위 밖이면 고른 것이 없는 상태가 된다.</summary>
    void SetValue(int value);

    /// <summary>펼쳐진 항목 한 줄의 높이다. 0이면 머리 칸의 높이를 그대로 쓴다.</summary>
    [[nodiscard]] float GetOptionHeight() const { return mOptionHeight; }
    void SetOptionHeight(const float height) { mOptionHeight = height >= 0.0f ? height : 0.0f; }

    /// <summary>지금 목록이 펼쳐져 있는지다.</summary>
    [[nodiscard]] bool IsOpen() const { return mChoice.IsOpen(); }

    /// <summary>펼친다. 스크립트가 여는 길이며, 사람은 머리 칸을 눌러 연다.</summary>
    void Open();

    /// <summary>고르지 않고 접는다.</summary>
    void Close();

    /// <summary>커서가 이 요소 위에 있는지다. 펼쳐 있으면 항목 줄도 이 요소다.</summary>
    [[nodiscard]] bool IsHovered() const { return mHovered; }

    /// <summary>
    /// 이번 프레임에 값이 바뀌었는지다. 다음 프레임에는 다시 거짓이다 —
    /// <see cref="Button::WasClickedThisFrame"/>과 같은 약속이며, 스크립트가 Update에서 읽는다.
    /// </summary>
    [[nodiscard]] bool WasValueChangedThisFrame() const { return mValueChanged; }

    /// <summary>이 요소가 그 점을 덮는지다. 머리 칸은 늘, 항목 줄들은 펼쳐 있을 때만이다.</summary>
    [[nodiscard]] bool Covers(float x, float y) const override;

    /// <summary>
    /// 그 점에 놓인 항목의 인덱스다. 머리 칸 위이거나, 접혀 있거나, 목록 밖이면 비어 있다.
    /// </summary>
    [[nodiscard]] std::optional<std::size_t> OptionAt(float x, float y) const;

private:
    // 커서와 키를 주는 것은 이벤트 시스템 하나다. 다른 곳이 이것을 넣을 수 있으면 화면이 보이는
    // 상태와 실제로 값을 받는 요소가 어긋난다.
    friend class UIEventSystem;

    /// <summary>
    /// 이번 프레임의 포인터와 키를 적용한다. 클릭이 머리 칸이면 펼침을 뒤집고, 항목이면 그것을
    /// 고른다. 다른 곳에서 누르기 시작하면 접는다.
    /// </summary>
    void ApplyPointerState(
        bool hovered, bool clicked, float cursorX, float cursorY, bool pressedElsewhere,
        const Core::ChoiceModel::Input& keys);

    /// <summary>고른 항목의 이름을 TextRenderer에 싣는다.</summary>
    void SynchronizeDisplay();

    /// <summary>프레임마다의 표시 — 값이 바뀌었음 — 를 지운다. 시스템이 프레임 앞에서 부른다.</summary>
    void ClearFrameFlags();

    /// <summary>항목 한 줄의 실제 높이다. 정해진 값이 없으면 머리 칸 높이다.</summary>
    [[nodiscard]] float ResolveOptionHeight() const;

    /// <summary>받지 않게 된 요소는 펼쳐진 채로 남지 않는다. 그러면 누구도 접을 수 없다.</summary>
    void OnInteractableLost() override;

    std::vector<std::string> mOptions;
    float mOptionHeight = 0.0f;

    Core::ChoiceModel mChoice;
    bool mHovered = false;
    bool mValueChanged = false;
};

}
