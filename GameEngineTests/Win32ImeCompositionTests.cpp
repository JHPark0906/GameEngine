#include "Win32ImeCompositionTests.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <windows.h>
#include <imm.h>

#include "Platform/Win32/Win32Input.h"
#include "TestSupport.h"

namespace GameEngine::Platform::Win32
{
    // IMM 데이터 취득 다음의 실제 메시지 처리를 시험한다. 설치된 IME나 데스크톱 포커스,
    // 사용자 키보드 설정을 바꾸지 않고 동시 RESULT/COMP 메시지의 순서를 재현한다.
    struct Win32InputTestAccess
    {
        static bool Composition(Win32Input& input, const LPARAM flags,
            std::optional<std::wstring> result, std::optional<std::wstring> composition, const LONG caret = -1)
        {
            Win32Input::CompositionSnapshot snapshot;
            snapshot.result = std::move(result);
            snapshot.composition = std::move(composition);
            snapshot.caret = caret;
            return input.ApplyComposition(flags, snapshot);
        }

        static void SetCompleter(Win32Input& input, bool (*complete)(HWND))
        {
            input.mCompleteComposition = complete ? complete : &Win32Input::CompleteComposition;
        }

        static const InputState& PeekState(const Win32Input& input) { return input.mState; }
    };
}

namespace
{
    namespace Platform = GameEngine::Platform;
    using Platform::Win32::Win32Input;
    using Platform::Win32::Win32InputTestAccess;
    using TestSupport::Expect;

    Platform::InputState Read(Win32Input& input)
    {
        Platform::InputState state;
        input.ReadState(state);
        return state;
    }

    bool UpdatesAndConsecutiveHangul()
    {
        Win32Input input;
        input.HandleMessage(nullptr, WM_SETFOCUS, 0, 0);
        input.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
        if (!Expect(Read(input).compositionCaret == std::string::npos,
            "starting a new composition should forget the preceding composition caret")) return false;
        bool consumed = Win32InputTestAccess::Composition(input, GCS_COMPSTR | GCS_CURSORPOS,
            std::nullopt, L"\xD55C", 1);
        auto state = Read(input);
        if (!Expect(consumed && state.typedText.empty() && state.compositionText == "\xED\x95\x9C" &&
            state.compositionCaret == 3, "a Korean composition update should supply UTF-8 text and caret bytes")) return false;
        state = Read(input);
        if (!Expect(state.compositionText == "\xED\x95\x9C" && state.compositionCaret == 3,
            "ReadState should preserve composition and its caret between messages")) return false;

        consumed = Win32InputTestAccess::Composition(input, GCS_RESULTSTR | GCS_COMPSTR | GCS_CURSORPOS,
            L"\xD55C", L"\x3131", 1);
        state = Read(input);
        if (!Expect(consumed && state.typedText == "\xED\x95\x9C" &&
            state.compositionText == "\xE3\x84\xB1" && state.compositionCaret == 3,
            "a simultaneous result and next composition must commit the first syllable and retain the new one")) return false;
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, L"\xAE00", 1);
        consumed = Win32InputTestAccess::Composition(input, GCS_RESULTSTR, L"\xAE00", std::nullopt);
        input.HandleMessage(nullptr, WM_IME_ENDCOMPOSITION, 0, 0);
        state = Read(input);
        if (!Expect(consumed && state.typedText == "\xEA\xB8\x80" && state.compositionText.empty() &&
            state.compositionCaret == std::string::npos,
            "the next syllable should commit exactly once without waiting for a WM_CHAR fallback")) return false;
        return Expect(Read(input).typedText.empty(), "reading committed IME results must drain them once");
    }

    bool CursorOnlyAndSurrogates()
    {
        Win32Input input;
        const std::wstring composition = L"A\xD55C\xD83D\xDE00";
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, composition, 4);
        auto state = Read(input);
        if (!Expect(state.compositionText == "A\xED\x95\x9C\xF0\x9F\x98\x80" && state.compositionCaret == 8,
            "UTF-16 surrogate pairs must count as four UTF-8 caret bytes")) return false;
        Win32InputTestAccess::Composition(input, GCS_CURSORPOS, std::nullopt, composition, 2);
        state = Read(input);
        if (!Expect(state.compositionCaret == 4 && state.compositionText.size() == 8,
            "a cursor-only message should move the caret without losing composition text")) return false;
        Win32InputTestAccess::Composition(input, GCS_CURSORPOS, std::nullopt, composition, 3);
        if (!Expect(Read(input).compositionCaret == 4,
            "a caret in the middle of a surrogate pair must clamp to a scalar boundary")) return false;
        Win32InputTestAccess::Composition(input, GCS_CURSORPOS, std::nullopt, composition, 999);
        if (!Expect(Read(input).compositionCaret == 8, "an oversized IME caret should clamp to the end")) return false;
        Win32InputTestAccess::Composition(input, GCS_CURSORPOS, std::nullopt, composition, IMM_ERROR_NODATA);
        if (!Expect(Read(input).compositionCaret == std::string::npos,
            "an unavailable cursor should use the platform-independent end sentinel")) return false;

        input.HandleMessage(nullptr, WM_CHAR, 0xD83D, 0);
        if (!Expect(Read(input).typedText.empty(), "a lone high surrogate should wait across frames")) return false;
        input.HandleMessage(nullptr, WM_CHAR, 0xDE00, 0);
        input.HandleMessage(nullptr, WM_CHAR, L'x', 0);
        if (!Expect(Read(input).typedText == "\xF0\x9F\x98\x80x",
            "ordinary WM_CHAR surrogate pairs and subsequent ASCII must retain their existing path")) return false;
        Win32InputTestAccess::Composition(input, GCS_RESULTSTR, L"\xD55C\xD83D\xDE00", std::nullopt);
        input.HandleMessage(nullptr, WM_CHAR, 0xD55C, 0);
        return Expect(Read(input).typedText == "\xED\x95\x9C\xF0\x9F\x98\x80\xED\x95\x9C",
            "a separate ordinary character matching an IME result must not be heuristically discarded");
    }

    bool EnterAndFallback()
    {
        Win32Input input;
        input.HandleMessage(nullptr, WM_SETFOCUS, 0, 0);
        input.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
        input.HandleMessage(nullptr, WM_KEYDOWN, VK_RETURN, 1);
        const bool consumed = Win32InputTestAccess::Composition(input, GCS_RESULTSTR,
            L"\xD55C", std::nullopt);
        input.HandleMessage(nullptr, WM_IME_ENDCOMPOSITION, 0, 0);
        auto state = Read(input);
        if (!Expect(consumed && state.imeHandledEnter && state.WasKeyHandledByIme(Platform::Key::Enter) &&
            state.typedText == "\xED\x95\x9C", "IME Enter should commit text without submitting the form")) return false;
        input.HandleMessage(nullptr, WM_KEYUP, VK_RETURN, 1);
        Read(input);
        input.HandleMessage(nullptr, WM_KEYDOWN, VK_RETURN, 1);
        if (!Expect(!Read(input).imeHandledEnter, "a later ordinary Enter must remain available for submission")) return false;

        // 실패한 IMM 읽기를 소비하면 글자가 유실된다. 그때만 창 기본 처리의 WM_CHAR 경로를 남긴다.
        const bool failedReadConsumed = Win32InputTestAccess::Composition(input, GCS_RESULTSTR,
            std::nullopt, std::nullopt);
        input.HandleMessage(nullptr, WM_CHAR, L'z', 0);
        state = Read(input);
        return Expect(!failedReadConsumed && state.typedText == "z",
            "failed direct result reads must retain the default WM_CHAR fallback without duplicating text");
    }

    bool CancellationAndFocus()
    {
        Win32Input input;
        input.HandleMessage(nullptr, WM_SETFOCUS, 0, 0);
        input.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, L"\xD55C", 1);
        input.HandleMessage(nullptr, WM_IME_COMPOSITION, 0, 0);
        auto state = Read(input);
        if (!Expect(state.compositionText.empty() && state.compositionCaret == std::string::npos && state.typedText.empty(),
            "an IME cancellation with no GCS flags must discard uncommitted text and caret")) return false;
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, L"\xD55C", 1);
        input.HandleMessage(nullptr, WM_CHAR, 0xD83D, 0);
        input.HandleMessage(nullptr, WM_KILLFOCUS, 0, 0);
        state = Read(input);
        if (!Expect(!state.hasFocus && state.compositionText.empty() && state.compositionCaret == std::string::npos,
            "focus loss must clear the entire composition state")) return false;
        input.HandleMessage(nullptr, WM_SETFOCUS, 0, 0);
        input.HandleMessage(nullptr, WM_CHAR, 0xDE00, 0);
        if (!Expect(Read(input).typedText.empty(), "focus loss must not join a later low surrogate to an old high surrogate")) return false;
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, L"\xD55C", 1);
        input.HandleMessage(nullptr, WM_IME_ENDCOMPOSITION, 0, 0);
        state = Read(input);
        return Expect(state.compositionText.empty() && state.compositionCaret == std::string::npos,
            "ending a composition must reset its caret even without a result");
    }

    struct CompletionProbe
    {
        explicit CompletionProbe(Win32Input& input) : mInput(input)
        {
            mActiveProbe = this;
            Win32InputTestAccess::SetCompleter(input, &Request);
        }
        ~CompletionProbe()
        {
            Win32InputTestAccess::SetCompleter(mInput, nullptr);
            mActiveProbe = nullptr;
        }

        static bool Request(const HWND window)
        {
            auto& probe = *mActiveProbe;
            ++probe.mRequests;
            const auto& before = Win32InputTestAccess::PeekState(probe.mInput);
            probe.mRequestBeforeClick = probe.mRequestBeforeClick &&
                !before.WasMouseButtonPressed(Platform::MouseButton::Left) && !before.compositionText.empty();
            probe.mCorrectWindow = probe.mCorrectWindow && window == probe.mWindow;
            if (!probe.mSucceed) return false;
            Win32InputTestAccess::Composition(probe.mInput, GCS_RESULTSTR, probe.mResult, std::nullopt);
            probe.mInput.HandleMessage(window, WM_IME_ENDCOMPOSITION, 0, 0);
            return true;
        }

        inline static thread_local CompletionProbe* mActiveProbe = nullptr;
        Win32Input& mInput;
        // 비교용 토큰일 뿐 OS API에 전달하지 않는다.
        HWND mWindow = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x1234));
        std::wstring mResult = L"\xD55C";
        int mRequests = 0;
        bool mSucceed = true;
        bool mRequestBeforeClick = true;
        bool mCorrectWindow = true;
    };

    bool ClickCompletesPreviousComposition()
    {
        Win32Input input;
        CompletionProbe probe(input);
        input.HandleMessage(probe.mWindow, WM_SETFOCUS, 0, 0);
        const bool ordinaryConsumed = input.HandleMessage(probe.mWindow, WM_LBUTTONDOWN, 0, 0);
        auto state = Read(input);
        if (!Expect(!ordinaryConsumed && probe.mRequests == 0 && state.WasMouseButtonPressed(Platform::MouseButton::Left),
            "an ordinary click without composition must retain its original behavior")) return false;
        input.HandleMessage(probe.mWindow, WM_LBUTTONUP, 0, 0);
        Read(input);

        input.HandleMessage(probe.mWindow, WM_IME_STARTCOMPOSITION, 0, 0);
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, L"\xD55C", 1);
        Read(input);
        input.HandleMessage(probe.mWindow, WM_LBUTTONDOWN, 0, 0);
        state = Read(input);
        if (!Expect(probe.mRequests == 1 && probe.mRequestBeforeClick && probe.mCorrectWindow &&
            state.typedText == "\xED\x95\x9C" && state.compositionText.empty() &&
            state.compositionCaret == std::string::npos && state.WasMouseButtonPressed(Platform::MouseButton::Left),
            "a click must request completion on its own window before recording the click and retain a reentrant result once")) return false;
        if (!Expect(Read(input).typedText.empty(), "a reentrant click completion must not duplicate committed text")) return false;
        input.HandleMessage(probe.mWindow, WM_LBUTTONUP, 0, 0);
        Read(input);

        input.HandleMessage(probe.mWindow, WM_IME_STARTCOMPOSITION, 0, 0);
        Win32InputTestAccess::Composition(input, GCS_COMPSTR, std::nullopt, L"\xAE00", 1);
        Read(input);
        probe.mResult = L"\xAE00";
        probe.mSucceed = false;
        input.HandleMessage(probe.mWindow, WM_LBUTTONDBLCLK, 0, 0);
        state = Read(input);
        if (!Expect(probe.mRequests == 2 && probe.mRequestBeforeClick && state.typedText.empty() &&
            state.compositionText == "\xEA\xB8\x80" && state.compositionCaret == 3 &&
            state.WasMouseButtonPressed(Platform::MouseButton::Left),
            "a failed double-click completion request must preserve composition text and caret without fabricating a commit")) return false;
        input.HandleMessage(probe.mWindow, WM_LBUTTONUP, 0, 0);
        Read(input);
        probe.mSucceed = true;
        input.HandleMessage(probe.mWindow, WM_LBUTTONDOWN, 0, 0);
        state = Read(input);
        return Expect(probe.mRequests == 3 && state.typedText == "\xEA\xB8\x80" && state.compositionText.empty(),
            "a later click must still be able to complete composition after an earlier request failed");
    }
}

bool RunWin32ImeCompositionTests()
{
    return UpdatesAndConsecutiveHangul() && CursorOnlyAndSurrogates() && EnterAndFallback() &&
        CancellationAndFocus() && ClickCompletesPreviousComposition();
}

static const TestSupport::Registration gWin32ImeCompositionTests{
    "UIEvent", "Win32 IME composition and caret tests should pass", RunWin32ImeCompositionTests };
