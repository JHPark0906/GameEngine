#include "Win32ImeKeyTests.h"

#include <cstddef>
#include <initializer_list>
#include <windows.h>
#include <imm.h>

#include "Platform/Win32/Win32Input.h"
#include "Runtime/Input.h"
#include "TestSupport.h"

namespace
{
    LPARAM KeyData(const unsigned int scan, const bool extended = false, const bool release = false)
    {
        return static_cast<LPARAM>(1ull | (static_cast<unsigned long long>(scan) << 16) |
            (extended ? (1ull << 24) : 0ull) | (release ? (3ull << 30) : 0ull));
    }
}

bool RunWin32ImeKeyTests()
{
    using namespace GameEngine;
    using Platform::Key;
    using TestSupport::Expect;
    Platform::Win32::Win32Input platform;
    Runtime::Input input;
    const auto message = [&platform](const UINT kind, const WPARAM key, const LPARAM data = 0)
    {
        return platform.HandleMessage(nullptr, kind, key, data);
    };
    const auto read = [&platform, &input]()
    {
        Platform::InputState state;
        platform.ReadState(state);
        input.BeginFrameWithState(state);
        return state;
    };
    message(WM_SETFOCUS, 0);
    bool passed = true;
    for (const auto letter : { 'A', 'D' })
    {
        const auto scan = MapVirtualKeyExW(static_cast<UINT>(letter), MAPVK_VK_TO_VSC, GetKeyboardLayout(0));
        if (!Expect(scan != 0, "A/D must have a scan code in the active Windows keyboard layout")) return false;
        const Key key = letter == 'A' ? Key::A : Key::D;
        passed &= Expect(!message(WM_KEYDOWN, VK_PROCESSKEY, KeyData(scan)),
            "IME keyboard messages must remain available to the text composition path");
        const auto pressed = read();
        passed &= Expect(input.GetKey(key) && input.GetKeyDown(key),
            "an IME process-key message must press the physical A/D game key");
        passed &= Expect(pressed.WasKeyHandledByIme(key),
            "UI commands must be able to distinguish IME-consumed keys without hiding them from gameplay");
        message(WM_KEYDOWN, VK_PROCESSKEY, KeyData(scan));
        read();
        passed &= Expect(input.GetKey(key) && !input.GetKeyDown(key),
            "IME auto-repeat must retain held movement without fabricating another press");
        message(WM_KEYUP, static_cast<WPARAM>(letter), KeyData(scan, false, true));
        read();
        passed &= Expect(!input.GetKey(key) && input.GetKeyUp(key),
            "normal key-up after an IME key-down must release the same game key");
    }
    const auto scanA = MapVirtualKeyExW('A', MAPVK_VK_TO_VSC, GetKeyboardLayout(0));
    message(WM_KEYDOWN, 'A', KeyData(scanA));
    read();
    // Release identity comes from the press, so a layout/IME transition cannot strand A held.
    message(WM_KEYUP, 'Q', KeyData(scanA, false, true));
    read();
    passed &= Expect(!input.GetKey(Key::A) && input.GetKeyUp(Key::A) && !input.GetKey(Key::Q),
        "key-up must release the remembered scan-code binding after a virtual-key mapping changes");

    message(WM_KEYDOWN, VK_PROCESSKEY, KeyData(scanA));
    message(WM_IME_STARTCOMPOSITION, 0);
    message(WM_CHAR, 0xd55c); // 한: committed text is still received through WM_CHAR.
    const auto text = read();
    passed &= Expect(text.typedText == "\xed\x95\x9c" && input.GetKey(Key::A),
        "committed Korean text and held game keys must remain independent channels");
    message(WM_IME_ENDCOMPOSITION, 0);
    message(WM_KILLFOCUS, 0);
    auto state = read();
    passed &= Expect(!state.hasFocus && !input.GetKey(Key::A) && state.compositionText.empty(),
        "focus loss must clear held IME keys and unfinished composition");
    message(WM_KEYDOWN, VK_PROCESSKEY, KeyData(scanA));
    state = read();
    passed &= Expect(!input.GetKey(Key::A), "late key-down messages must not restore movement while unfocused");
    message(WM_SETFOCUS, 0);
    read();
    passed &= Expect(!input.GetKey(Key::A), "regaining focus must not revive an old scan-code press");

    // Combined modifier states survive releasing just one physical side.
    const auto leftShift = MapVirtualKeyExW(VK_LSHIFT, MAPVK_VK_TO_VSC, GetKeyboardLayout(0));
    const auto rightShift = MapVirtualKeyExW(VK_RSHIFT, MAPVK_VK_TO_VSC, GetKeyboardLayout(0));
    message(WM_KEYDOWN, VK_SHIFT, KeyData(leftShift));
    message(WM_KEYDOWN, VK_SHIFT, KeyData(rightShift));
    message(WM_KEYUP, VK_SHIFT, KeyData(leftShift, false, true));
    read();
    passed &= Expect(input.GetKey(Key::Shift), "releasing left Shift must not release a held right Shift");
    message(WM_KEYUP, VK_SHIFT, KeyData(rightShift, false, true));
    read();
    passed &= Expect(!input.GetKey(Key::Shift), "the combined modifier must release after both sides are up");
    message(WM_SYSKEYDOWN, VK_MENU, KeyData(0x38, true));
    read();
    passed &= Expect(input.GetKey(Key::Alt), "extended system keys must retain their existing behavior");
    message(WM_SYSKEYUP, VK_PROCESSKEY, KeyData(0x38, true, true));
    read();
    passed &= Expect(!input.GetKey(Key::Alt), "IME-wrapped extended key-up must release its remembered binding");

    const auto scanEnter = MapVirtualKeyExW(VK_RETURN, MAPVK_VK_TO_VSC, GetKeyboardLayout(0));
    message(WM_IME_STARTCOMPOSITION, 0);
    message(WM_KEYDOWN, VK_PROCESSKEY, KeyData(scanEnter));
    message(WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
    message(WM_CHAR, 0xd55c);
    message(WM_IME_ENDCOMPOSITION, 0);
    state = read();
    passed &= Expect(input.GetKeyDown(Key::Enter) && state.imeHandledEnter && state.compositionText.empty() &&
        state.typedText == "\xed\x95\x9c",
        "an IME Enter must remain marked after result/end messages clear the composition in the same read");
    message(WM_KEYUP, VK_RETURN, KeyData(scanEnter, false, true));
    state = read();
    passed &= Expect(!state.imeHandledEnter && !state.WasKeyHandledByIme(Key::Enter),
        "IME command consumption must clear after one ReadState");
    message(WM_KEYDOWN, VK_RETURN, KeyData(scanEnter));
    state = read();
    passed &= Expect(input.GetKeyDown(Key::Enter) && !state.imeHandledEnter,
        "the next ordinary Enter must be available to submit committed text");
    message(WM_KEYUP, VK_RETURN, KeyData(scanEnter, false, true));
    read();
    message(WM_IME_STARTCOMPOSITION, 0);
    message(WM_IME_ENDCOMPOSITION, 0);
    message(WM_KEYDOWN, VK_RETURN, KeyData(scanEnter));
    state = read();
    passed &= Expect(state.imeHandledEnter,
        "IME end arriving before its ordinary Enter message in the same read must not trigger form submission");
    return passed;
}

static const TestSupport::Registration gWin32ImeKeyTests{
    "UIEvent", "Win32 IME physical key tests should pass", RunWin32ImeKeyTests };
