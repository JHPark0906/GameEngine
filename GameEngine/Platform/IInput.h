#pragma once

// 이 헤더의 계약: 키와 마우스 버튼은 <b>레벨과 전이 누적을 함께</b> 든다. 레벨은 "지금 눌려
// 있는가"이고 누적은 "마지막 읽기 이후 몇 번 눌리고 떼어졌는가"이며, 전이는 읽기 사이의 것을
// 잃지 않는다 — 한 프레임 안에서 눌렸다 떼어진 클릭과 단축키가 그렇게 살아남는다. 구현이
// 지킬 것은 둘이다: 상태를 바꿀 때 SetKey/SetMouseButton을 쓸 것(배열을 직접 쓰지 말 것),
// 그리고 상태를 넘겨준 직후 TakeAccumulated()를 부를 것.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace GameEngine::Platform
{

/// <summary>
/// 키다. 어느 플랫폼의 스캔 코드나 가상 키 코드가 아니라, 키에 인쇄된 것으로 이름 붙인다.
///
/// 키에 여기서 이름을 붙이면, 에디터도 게임 스크립트도 어느 윈도잉 시스템이 그 이벤트를
/// 배달했는지 신경 쓰지 않는다.
///
/// 이것은 키보드가 반드시 갖는 집합이다. 일부 배열에만 있는 키는 추측하는 대신 뺐다. 프랑스어
/// 키보드에서 다른 물리 키를 뜻하는 이름은 이름이 없는 것보다 나쁘기 때문이다 — 사람이 치는
/// 텍스트는 텍스트로, `InputState::typedText`를 통해 도착하고, IME가 만들 수 있는 것도 그것뿐이다.
/// </summary>
enum class Key : unsigned char
{
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Escape, Space, Enter, Tab, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,
    Left, Right, Up, Down,

    /// <summary>양쪽 shift 키 모두이다. 좌우 구분은 약속할 만큼 이식성이 없다.</summary>
    Shift,
    Control,
    Alt,

    Count,
};

/// <summary>마우스 버튼이다. 사람이 세는 순서대로 번호를 매긴다.</summary>
enum class MouseButton : unsigned char
{
    Left = 0,
    Right,
    Middle,
    Count,
};

/// <summary>커서의 위치이다. 창 클라이언트 영역 좌상단 기준 픽셀 단위다.</summary>
struct CursorPosition
{
    int x = 0;
    int y = 0;

    [[nodiscard]] bool operator==(const CursorPosition&) const = default;
};

/// <summary>
/// 한 순간의 입력 장치 모습과, 마지막으로 읽힌 뒤 타이핑된 것들이다.
///
/// 눌려 있는 상태만 보고한다. 키가 *이번 프레임에* 눌렸는지는 두 순간에 대한 질문이라서, 각
/// 플랫폼이 제 나름으로 에지를 추적하며 프레임이 무엇인지에 대해 서로 어긋나는 대신, 런타임이
/// 연속된 상태를 비교해서 답한다.
/// </summary>
struct InputState
{
    std::array<bool, static_cast<std::size_t>(Key::Count)> keys{};

    /// <summary>
    /// 마지막 읽기 이후 각 키가 눌린 횟수와 떼어진 횟수다. 버튼과 같은 이유로 누적이다:
    /// 프레임이 길어지는 순간에 눌렸다 떼어진 단축키는 표본 두 개로는 보이지 않는다.
    /// </summary>
    std::array<unsigned char, static_cast<std::size_t>(Key::Count)> keyPresses{};
    std::array<unsigned char, static_cast<std::size_t>(Key::Count)> keyReleases{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> mouseButtons{};

    /// <summary>
    /// 마지막 읽기 이후 각 버튼이 눌린 횟수와 떼어진 횟수다. 상태가 아니라 <b>누적</b>인 이유는
    /// 표본 사이에서 시작하고 끝난 클릭 때문이다: 누름과 뗌이 한 프레임 안에서 끝나면 표본은
    /// 두 번 다 "올라와 있음"을 보고, 레벨만 비교하는 쪽에는 그 클릭이 존재한 적이 없게 된다.
    /// 타이핑과 휠이 같은 이유로 이미 누적이다.
    /// </summary>
    std::array<unsigned char, static_cast<std::size_t>(MouseButton::Count)> mousePresses{};
    std::array<unsigned char, static_cast<std::size_t>(MouseButton::Count)> mouseReleases{};
    CursorPosition cursor;

    /// <summary>마지막 읽기 이후의 휠 눈금이다. 양수가 사람에게서 멀어지는 쪽이다.</summary>
    float wheelDelta = 0.0f;

    /// <summary>
    /// 이 창이 입력을 받는 창인지 여부이다. 포커스를 잃은 창은 아무것도 눌려 있지 않다고
    /// 보고하므로, alt-tab으로 떠날 때 물리적으로 눌려 있던 키가 영원히 눌린 채로 남지 않는다.
    /// </summary>
    bool hasFocus = false;

    /// <summary>
    /// 마지막 읽기 이후 조합된 문자들이다. UTF-8이다. 키 상태와 분리된 이유는 다른 질문이기
    /// 때문이다: 어느 키가 눌렸는지는 키보드에 대한 것이고, 사람이 무엇을 쳤는지는 그 사람의
    /// 배열, dead key, IME에 대한 것이다. 텍스트는 키 상태로 재구성할 수 없고, IME가 필요한
    /// 언어에서는 근처에도 못 간다.
    /// </summary>
    std::string typedText;

    /// <summary>
    /// 지금 IME가 조합 중인 문자열이다. UTF-8이며, 조합이 없으면 비어 있다.
    ///
    /// `typedText`와 다른 질문에 답한다: typedText는 이미 확정된 글자이고, 이것은 아직 확정되지
    /// 않은 — 사람이 보고 고치는 중인 — 글자다. 한글을 치는 동안 확정된 글자만 도착하면 화면에는
    /// 아무것도 없다가 완성된 글자가 튀어나오므로, 텍스트 필드는 이것을 캐럿 자리에 함께 보여
    /// 준다. 확정되는 순간 이 문자열은 비고 같은 내용이 typedText로 도착한다.
    ///
    /// 조합의 밑줄이나 절 구분 같은 표시 정보는 싣지 않는다. 이 계층이 약속하는 것은 "무엇이
    /// 조합 중인가"까지이고, 그것을 어떻게 그릴지는 UI의 몫이다.
    /// </summary>
    // 한 번의 읽기에 이전 음절의 typedText와 다음 음절의 compositionText가 함께 올 수 있다.
    std::string compositionText;

    /// <summary>조합 문자열 안의 UTF-8 바이트 캐럿 위치다. npos는 플랫폼이 위치를 제공하지 않았음을 뜻한다.</summary>
    std::size_t compositionCaret = std::string::npos;

    // 조합 세대는 END 뒤 지연된 결과를 새 조합의 결과와 구별한다. 0은 아직 조합이 없다는 뜻이다.
    std::uint64_t compositionId = 0;
    // 마지막 읽기 이후 실제로 확정한 조합과 Enter가 처리한 조합이다. 0이면 해당 이벤트가 없다.
    // UI는 committedCompositionId의 typedText를 적용한 뒤 선택적으로 전송할 수 있다.
    std::uint64_t committedCompositionId = 0;
    std::uint64_t imeEnterCompositionId = 0;

    /// <summary>
    /// 마지막 읽기 이후 IME가 처리한 물리 키 누름이다. 게임의 키 상태는 그대로 유지하면서
    /// 텍스트 위젯이 조합용 Backspace/방향키를 확정된 문자에 다시 적용하지 않도록 구분한다.
    /// </summary>
    std::array<bool, static_cast<std::size_t>(Key::Count)> imeHandledKeys{};
    /// <summary>IME 확정용 Enter는 조합 문자열이 같은 프레임에 비어도 폼 제출이 아니다.</summary>
    bool imeHandledEnter = false;

    void MarkKeyHandledByIme(const Key key)
    {
        if (key == Key::Unknown || key >= Key::Count) return;
        imeHandledKeys[static_cast<std::size_t>(key)] = true;
        if (key == Key::Enter) imeHandledEnter = true;
    }

    [[nodiscard]] bool WasKeyHandledByIme(const Key key) const
    {
        return key != Key::Unknown && key < Key::Count && imeHandledKeys[static_cast<std::size_t>(key)];
    }

    [[nodiscard]] bool IsKeyDown(const Key key) const
    {
        return key != Key::Unknown && key < Key::Count &&
            keys[static_cast<std::size_t>(key)];
    }

    [[nodiscard]] bool IsMouseButtonDown(const MouseButton button) const
    {
        return button < MouseButton::Count && mouseButtons[static_cast<std::size_t>(button)];
    }

    /// <summary>
    /// 키의 눌림 상태를 바꾸고, 바뀌었으면 그 전이를 센다. 버튼의 <see cref="SetMouseButton"/>과
    /// 같은 이유로 레벨과 누적을 한자리에서 유지한다.
    /// </summary>
    /// <param name="key">바뀐 키다.</param>
    /// <param name="isDown">이제 눌려 있으면 true다.</param>
    void SetKey(const Key key, const bool isDown)
    {
        if (key >= Key::Count)
        {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(key);
        if (keys[index] != isDown)
        {
            auto& counter = isDown ? keyPresses[index] : keyReleases[index];
            if (counter < 255)
            {
                ++counter;
            }
        }
        keys[index] = isDown;
    }

    /// <summary>마지막 읽기 이후 이 키가 눌린 적이 있는지다.</summary>
    [[nodiscard]] bool WasKeyPressed(const Key key) const
    {
        return key < Key::Count && keyPresses[static_cast<std::size_t>(key)] > 0;
    }

    /// <summary>마지막 읽기 이후 이 키가 떼어진 적이 있는지다.</summary>
    [[nodiscard]] bool WasKeyReleased(const Key key) const
    {
        return key < Key::Count && keyReleases[static_cast<std::size_t>(key)] > 0;
    }

    /// <summary>
    /// 마지막 읽기 이후 쌓인 것 — 글자, 휠, 버튼 전이 — 을 비운다. 읽는 쪽이 그것을 가져갔다는
    /// 뜻이므로 상태를 넘겨준 직후에 부른다 — 읽는 쪽이 프레임마다 한 번 부른다. 누적 항목이
    /// 늘어도 이 한 곳만 고치면 된다.
    /// </summary>
    void TakeAccumulated()
    {
        // 조합 문자열과 그 캐럿은 누적 이벤트가 아니라 현재 상태라 다음 IME 갱신까지 유지한다.
        typedText.clear();
        wheelDelta = 0.0f;
        mousePresses.fill(0);
        mouseReleases.fill(0);
        keyPresses.fill(0);
        keyReleases.fill(0);
        imeHandledKeys.fill(false);
        imeHandledEnter = false;
        committedCompositionId = 0;
        imeEnterCompositionId = 0;
    }
    /// <summary>
    /// 버튼의 눌림 상태를 바꾸고, 바뀌었으면 그 전이를 센다. 레벨과 누적을 함께 두는 유일한
    /// 자리다 — 따로 쓰면 표본 사이에서 끝난 클릭이 조용히 사라진다. 입력을 만드는 쪽(플랫폼
    /// 구현과 시험의 대역)이 부르는 유일한 변경 자리다.
    /// </summary>
    void SetMouseButton(const MouseButton button, const bool isDown)
    {
        if (button >= MouseButton::Count)
        {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(button);
        if (mouseButtons[index] != isDown)
        {
            auto& counter = isDown ? mousePresses[index] : mouseReleases[index];
            if (counter < 255)
            {
                ++counter;
            }
        }
        mouseButtons[index] = isDown;
    }

    /// <summary>마지막 읽기 이후 이 버튼이 눌린 적이 있는지다.</summary>
    [[nodiscard]] bool WasMouseButtonPressed(const MouseButton button) const
    {
        return button < MouseButton::Count && mousePresses[static_cast<std::size_t>(button)] > 0;
    }

    /// <summary>마지막 읽기 이후 이 버튼이 떼어진 적이 있는지다.</summary>
    [[nodiscard]] bool WasMouseButtonReleased(const MouseButton button) const
    {
        return button < MouseButton::Count && mouseReleases[static_cast<std::size_t>(button)] > 0;
    }
};

/// <summary>
/// 플랫폼이 보는 그대로의 키보드와 마우스이다.
///
/// 누적되는 부분 — 타이핑된 텍스트와 휠 — 은 읽기가 파괴적이다. 상태가 아니라 마지막 읽기
/// 이후의 집계이기 때문이다. 따라서 읽는 자만이 읽어도 된다: 엔진 루프가 프레임당 한 번 읽어
/// 결과를 런타임에 건넨다.
/// </summary>
class IInput
{
public:
    virtual ~IInput() = default;

    IInput(const IInput&) = delete;
    IInput& operator=(const IInput&) = delete;
    IInput(IInput&&) = delete;
    IInput& operator=(IInput&&) = delete;

    /// <summary>현재 상태를 채우고, 이전 호출 이후 누적된 텍스트와 휠 움직임을 가져간다.</summary>
    virtual void ReadState(InputState& state) = 0;

protected:
    IInput() = default;
};

}
