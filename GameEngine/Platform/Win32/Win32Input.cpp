#include "pch.h"
#include "Win32Input.h"

#include <algorithm>
#include <cstddef>
#include <imm.h>
#include <optional>
#include <string>
#include <windowsx.h>

#include "Win32Utilities.h"

#pragma comment(lib, "imm32.lib")

namespace GameEngine::Platform::Win32
{

namespace
{
    /// <summary>
    /// Windows 가상 키에 대한 엔진의 이름이다. 엔진이 이름 붙이지 않는 키는 `Key::Unknown`이다.
    /// 글자와 숫자는 양쪽 다 연속이라 오프셋으로 대응시키고, 나머지는 전부 나열한다. 모든 키의
    /// 표가 공식보다 검사하기 쉽기 때문이다.
    /// </summary>
    [[nodiscard]] Key ToEngineKey(const WPARAM virtualKey)
    {
        if (virtualKey >= 'A' && virtualKey <= 'Z')
        {
            return static_cast<Key>(
                static_cast<unsigned char>(Key::A) + static_cast<unsigned char>(virtualKey - 'A'));
        }
        if (virtualKey >= '0' && virtualKey <= '9')
        {
            return static_cast<Key>(
                static_cast<unsigned char>(Key::Digit0) +
                static_cast<unsigned char>(virtualKey - '0'));
        }
        if (virtualKey >= VK_F1 && virtualKey <= VK_F12)
        {
            return static_cast<Key>(
                static_cast<unsigned char>(Key::F1) +
                static_cast<unsigned char>(virtualKey - VK_F1));
        }

        switch (virtualKey)
        {
        case VK_ESCAPE: return Key::Escape;
        case VK_SPACE: return Key::Space;
        case VK_RETURN: return Key::Enter;
        case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace;
        case VK_DELETE: return Key::Delete;
        case VK_INSERT: return Key::Insert;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return Key::Shift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return Key::Control;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return Key::Alt;
        default:
            return Key::Unknown;
        }
    }

    void SetMouseButton(InputState& state, const MouseButton button, const bool isDown)
    {
        state.SetMouseButton(button, isDown);
    }

    /// <summary>
    /// 바이트 길이를 확인해 IME 문자열을 읽는다. 빈 조합과 읽기 실패를 구분한다.
    /// </summary>
    [[nodiscard]] std::optional<std::wstring> ReadCompositionString(const HIMC context, const DWORD index)
    {
        const LONG byteSize = ImmGetCompositionStringW(context, index, nullptr, 0);
        if (byteSize < 0 || byteSize % sizeof(wchar_t) != 0)
        {
            return std::nullopt;
        }
        std::wstring wide(static_cast<std::size_t>(byteSize) / sizeof(wchar_t), L'\0');
        if (byteSize == 0) return wide;
        const LONG written = ImmGetCompositionStringW(
            context, index, wide.data(), static_cast<DWORD>(byteSize));
        if (written < 0 || written > byteSize || written % sizeof(wchar_t) != 0) return std::nullopt;
        wide.resize(static_cast<std::size_t>(written) / sizeof(wchar_t));
        return wide;
    }

    [[nodiscard]] std::size_t CompositionCaret(const std::wstring_view composition, const LONG caret)
    {
        if (caret < 0) return std::string::npos;
        std::size_t prefixLength = (std::min)(static_cast<std::size_t>(caret), composition.size());
        // 잘못된 UTF-16 경계가 오더라도 서러게이트 한 쌍의 가운데에 캐럿을 놓지 않는다.
        if (prefixLength > 0 && prefixLength < composition.size() &&
            IS_HIGH_SURROGATE(composition[prefixLength - 1]) && IS_LOW_SURROGATE(composition[prefixLength]))
            --prefixLength;
        return WideToUtf8(composition.substr(0, prefixLength)).size();
    }
}

Win32Input::CompositionSnapshot Win32Input::ReadComposition(const HWND window, const LPARAM flags)
{
    CompositionSnapshot snapshot;
    const HIMC context = ImmGetContext(window);
    if (!context) return snapshot;
    if ((flags & GCS_RESULTSTR) != 0) snapshot.result = ReadCompositionString(context, GCS_RESULTSTR);
    if ((flags & (GCS_COMPSTR | GCS_CURSORPOS)) != 0)
    {
        snapshot.composition = ReadCompositionString(context, GCS_COMPSTR);
        snapshot.caret = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
    }
    ImmReleaseContext(window, context);
    return snapshot;
}

bool Win32Input::CompleteComposition(const HWND window)
{
    if (!window) return false;
    const HIMC context = ImmGetContext(window);
    if (!context) return false;
    const bool completed = ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0) != FALSE;
    ImmReleaseContext(window, context);
    return completed;
}

void Win32Input::BeginComposition()
{
    if (++mState.compositionId == 0) ++mState.compositionId;
    mCompositionActive = true;
}

bool Win32Input::ApplyComposition(const LPARAM flags, const CompositionSnapshot& snapshot)
{
    constexpr LPARAM compositionFlags = GCS_COMPREADSTR | GCS_COMPREADATTR | GCS_COMPREADCLAUSE |
        GCS_COMPSTR | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART |
        GCS_RESULTREADSTR | GCS_RESULTREADCLAUSE | GCS_RESULTSTR | GCS_RESULTCLAUSE;
    if ((flags & compositionFlags) == 0)
    {
        mCompositionEndedSinceRead = mCompositionEndedSinceRead || mCompositionActive;
        mCompositionActive = false;
        mState.compositionText.clear();
        mState.compositionCaret = std::string::npos;
        return true;
    }

    // 연속 한글 입력은 같은 메시지에 이전 음절의 RESULT와 다음 음절의 COMP를 함께 싣는다.
    // 이전 결과를 먼저 확정해야 뒤이어 시작한 새 조합을 지우지 않는다.
    if ((flags & GCS_RESULTSTR) != 0)
    {
        if (mState.compositionId == 0) BeginComposition();
        mCompositionEndedSinceRead = true;
        if (mState.WasKeyPressed(Key::Enter))
        {
            mState.MarkKeyHandledByIme(Key::Enter);
            if (mState.imeEnterCompositionId == 0) mState.imeEnterCompositionId = mState.compositionId;
        }
        mCompositionActive = false;
        mState.compositionText.clear();
        mState.compositionCaret = std::string::npos;
        mPendingHighSurrogate = 0;
        if (snapshot.result)
        {
            mState.typedText += WideToUtf8(*snapshot.result);
            mState.committedCompositionId = mState.compositionId;
        }
    }
    if ((flags & (GCS_COMPSTR | GCS_CURSORPOS)) != 0)
    {
        if (!mCompositionActive) BeginComposition();
        if (snapshot.composition)
        {
            mState.compositionText = WideToUtf8(*snapshot.composition);
            mState.compositionCaret = CompositionCaret(*snapshot.composition, snapshot.caret);
        }
        else
        {
            if ((flags & GCS_COMPSTR) != 0) mState.compositionText.clear();
            mState.compositionCaret = std::string::npos;
        }
    }
    // 결과를 직접 받았으면 DefWindowProc에 넘기지 않는다. 기본 IME 창이 다시 WM_IME_CHAR /
    // WM_CHAR로 확정 결과를 보내는 경로까지 열면 같은 글자가 두 번 들어간다. 읽기가 실패한
    // 경우에만 기본 처리를 남기며, 일반 WM_CHAR와 legacy WM_IME_CHAR는 원래 경로로 받는다.
    return (flags & GCS_RESULTSTR) == 0 || snapshot.result.has_value();
}

void Win32Input::ReadState(InputState& state)
{
    state = mState;

    // 글자·휠·버튼 전이는 상태가 아니라 마지막 읽기 이후의 누적이므로, 읽는 것이 곧 가져가는
    // 것이다.
    mState.TakeAccumulated();
    mCompositionEndedSinceRead = false;
}

void Win32Input::ReleaseAll()
{
    for (std::size_t index = 0; index < mState.keys.size(); ++index)
        mState.SetKey(static_cast<Key>(index), false);
    for (std::size_t index = 0; index < mState.mouseButtons.size(); ++index)
        mState.SetMouseButton(static_cast<MouseButton>(index), false);
    mPressedScanKeys.fill(Key::Unknown);
}

void Win32Input::HandleKeyMessage(const WPARAM virtualKey, const LPARAM keyData, const bool isDown)
{
    if (isDown && !mState.hasFocus) return;
    const auto bits = static_cast<unsigned long long>(keyData);
    const unsigned int scan = static_cast<unsigned int>((bits >> 16) & 0xffu);
    const bool extended = (bits & (1ull << 24)) != 0;
    const std::size_t slot = scan | (extended ? 0x100u : 0u);
    // 눌린 뒤 IME나 키보드 배열이 바뀌어도, 뗌은 누를 때 기록한 같은 엔진 키를 해제한다.
    Key key = scan != 0 ? mPressedScanKeys[slot] : Key::Unknown;
    if (key == Key::Unknown)
    {
        WPARAM resolved = virtualKey;
        if (virtualKey == VK_PROCESSKEY && scan != 0)
        {
            // An IME changes wParam, but the hardware scan code remains in lParam. The current
            // layout maps printed key names (A/D etc.) without interpreting any composed text.
            resolved = MapVirtualKeyExW(scan | (extended ? 0xe000u : 0u), MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
        }
        key = ToEngineKey(resolved);
    }
    if (key == Key::Unknown || key >= Key::Count) return;
    if (isDown && (virtualKey == VK_PROCESSKEY ||
        (key == Key::Enter && (mCompositionActive || mCompositionEndedSinceRead))))
    {
        mState.MarkKeyHandledByIme(key);
        if (key == Key::Enter && !mState.IsKeyDown(key)) mState.imeEnterCompositionId = mState.compositionId;
    }
    if (scan != 0) mPressedScanKeys[slot] = isDown ? key : Key::Unknown;
    // Shift/Control/Alt combine both physical sides. Releasing one must leave the other held.
    bool heldElsewhere = false;
    if (!isDown)
    {
        for (const Key held : mPressedScanKeys)
            if (held == key) { heldElsewhere = true; break; }
    }
    mState.SetKey(key, isDown || heldElsewhere);
}

bool Win32Input::HandleMessage(
    const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    switch (message)
    {
    // WM_SYSKEYDOWN carries Alt and the combinations that include it, which WM_KEYDOWN never sees.
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        HandleKeyMessage(wParam, lParam, true);
        return false;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        HandleKeyMessage(wParam, lParam, false);
        return false;

    // IME가 조합 중인 문자열이다. WM_CHAR는 확정된 글자만 주므로, 이것 없이는 한글을 치는
    // 동안 화면에 아무것도 보이지 않다가 완성된 글자가 튀어나온다. 조합 중의 문자열을 읽어
    // 두면 텍스트 필드가 캐럿 자리에 그것을 보여 줄 수 있다.
    case WM_IME_STARTCOMPOSITION:
        BeginComposition();
        mState.compositionText.clear();
        mState.compositionCaret = std::string::npos;
        // 시스템의 조합 창은 띄우지 않는다: 조합 중인 글자는 텍스트 필드가 자기 캐럿 자리에
        // 직접 그리므로, 창이 하나 더 뜨면 같은 글자가 두 곳에 보인다.
        return true;

    case WM_IME_COMPOSITION:
        return ApplyComposition(lParam, ReadComposition(window, lParam));

    case WM_IME_ENDCOMPOSITION:
        mCompositionEndedSinceRead = mCompositionEndedSinceRead || mCompositionActive;
        mCompositionActive = false;
        mState.compositionText.clear();
        mState.compositionCaret = std::string::npos;
        return false;

    case WM_CHAR:
    {
        const auto character = static_cast<wchar_t>(wParam);

        // A character outside the basic multilingual plane arrives as two messages, a high surrogate
        // and then a low one. Neither half converts to anything on its own, so the first is held
        // until its partner arrives and the pair is converted together.
        if (IS_HIGH_SURROGATE(character))
        {
            mPendingHighSurrogate = character;
            return true;
        }
        if (IS_LOW_SURROGATE(character))
        {
            if (mPendingHighSurrogate != 0)
            {
                const wchar_t pair[2]{ mPendingHighSurrogate, character };
                mState.typedText += WideToUtf8(std::wstring_view(pair, 2));
                mPendingHighSurrogate = 0;
            }
            return true;
        }
        mPendingHighSurrogate = 0;

        // Control characters are keystrokes, not text. Tab and the newlines are both.
        if (character >= L' ' || character == L'\t' || character == L'\r' || character == L'\n')
        {
            mState.typedText += WideToUtf8(std::wstring_view(&character, 1));
        }
        return true;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        // 같은 창 안의 입력 필드 전환도 이전 필드의 조합을 먼저 확정한다. 이 호출이 RESULT /
        // END 메시지로 재진입해도 ApplyComposition이 결과를 누적한 뒤 클릭을 기록하게 된다.
        // 성공 여부로 조합을 직접 지우지 않는다. 실패 또는 지연된 결과는 실제 IME 메시지가 정한다.
        if (mCompositionActive) static_cast<void>(mCompleteComposition(window));
        SetMouseButton(mState, MouseButton::Left, true);
        return false;
    case WM_LBUTTONUP:
        SetMouseButton(mState, MouseButton::Left, false);
        return false;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        SetMouseButton(mState, MouseButton::Right, true);
        return false;
    case WM_RBUTTONUP:
        SetMouseButton(mState, MouseButton::Right, false);
        return false;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        SetMouseButton(mState, MouseButton::Middle, true);
        return false;
    case WM_MBUTTONUP:
        SetMouseButton(mState, MouseButton::Middle, false);
        return false;

    case WM_MOUSEMOVE:
        mState.cursor = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        return false;

    case WM_MOUSEWHEEL:
        mState.wheelDelta +=
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
        return false;

    case WM_SETFOCUS:
        mState.hasFocus = true;
        return false;
    case WM_KILLFOCUS:
        mState.hasFocus = false;
        ReleaseAll();
        mState.compositionText.clear();
        mState.compositionCaret = std::string::npos;
        mPendingHighSurrogate = 0;
        mCompositionActive = false;
        mCompositionEndedSinceRead = false;
        return false;

    default:
        return false;
    }
}

}
