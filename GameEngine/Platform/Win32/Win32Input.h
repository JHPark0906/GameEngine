#pragma once

#include <array>
#include <optional>
#include <string>
#include <windows.h>

#include "../IInput.h"

namespace GameEngine::Platform::Win32
{

/// <summary>
/// 창의 메시지에서 조립한 키보드·마우스 상태이다.
///
/// `GetAsyncKeyState`가 아니라 메시지인 이유는, 그것이 어느 창이 앞에 있든 물리 키보드를 읽기
/// 때문이다: 사람이 다른 애플리케이션에 타이핑하는 동안에도 게임이 계속 움직였을 것이다. 여기
/// 도착하는 것은 이 창이 포커스를 가졌기에 도착한다.
/// IME가 VK_PROCESSKEY로 바꾼 키는 메시지의 스캔 코드로 복원한다. 문자는 WM_CHAR와
/// IME 조합 메시지로만 받으며, 물리 키를 놓을 때는 누를 때 기록한 키를 해제한다.
/// </summary>
class Win32Input final : public IInput
{
public:
    void ReadState(InputState& state) override;

    /// <summary>
    /// 입력 메시지라면 창 메시지 하나를 기록한다. 입력 메시지였는지를 반환하므로, 창은 메시지를
    /// 계속 넘겨야 하는지 안다.
    /// </summary>
    /// <param name="window">메시지를 받은 창이다. IME 조합 문자열을 읽는 데 쓴다.</param>
    /// <param name="message">Win32 메시지 코드이다.</param>
    /// <param name="wParam">메시지의 wParam이다.</param>
    /// <param name="lParam">메시지의 lParam이다.</param>
    /// <returns>이 입력이 메시지를 소비했으면 true이다.</returns>
    bool HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// 눌려 있는 모든 것을 잊는다. 창이 포커스를 잃을 때 호출된다: alt-tab으로 떠나는 동안 눌려
    /// 있던 키의 key-up은 옮겨 간 곳으로 배달되므로, 이것이 없으면 그 키는 돌아와 다시 누를
    /// 때까지 눌린 채로 남는다.
    /// </summary>
    void ReleaseAll();

private:
    friend struct Win32InputTestAccess;
    friend struct Win32ImeCommitTestAccess;

    /// <summary>동일 IME 문맥에서 읽은 한 메시지의 값이다. nullopt는 빈 문자열과 다른 읽기 실패다.</summary>
    struct CompositionSnapshot
    {
        std::optional<std::wstring> result;
        std::optional<std::wstring> composition;
        LONG caret = -1;
    };

    [[nodiscard]] static CompositionSnapshot ReadComposition(HWND window, LPARAM flags);
    [[nodiscard]] static bool CompleteComposition(HWND window);
    bool ApplyComposition(LPARAM flags, const CompositionSnapshot& snapshot);
    void BeginComposition();
    void HandleKeyMessage(WPARAM virtualKey, LPARAM keyData, bool isDown);

    InputState mState;
    // Scan code plus the extended-key bit identifies a press even if the IME/layout changes
    // before key-up. No global keyboard polling: focus loss clears the whole table.
    std::array<Key, 512> mPressedScanKeys{};
    bool mCompositionActive = false;
    bool mCompositionEndedSinceRead = false;
    // 창 문맥의 확정 요청만 대체할 수 있어 테스트는 실제 IME나 사용자 창을 필요로 하지 않는다.
    bool (*mCompleteComposition)(HWND) = &CompleteComposition;

    /// <summary>서러게이트 쌍의 앞쪽 절반이다. 짝을 실은 메시지를 기다린다.</summary>
    wchar_t mPendingHighSurrogate = 0;
};

}
