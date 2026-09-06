#include "Win32ImeSubmitCommitTests.h"

#include <optional>
#include <string>
#include <utility>
#include <windows.h>
#include <imm.h>

#include "Platform/Win32/Win32Input.h"
#include "TestSupport.h"

namespace GameEngine::Platform::Win32
{
struct Win32ImeCommitTestAccess
{
    static bool Result(Win32Input& input, std::optional<std::wstring> result)
    {
        Win32Input::CompositionSnapshot snapshot;
        snapshot.result = std::move(result);
        return input.ApplyComposition(GCS_RESULTSTR, snapshot);
    }
};
}

namespace
{
namespace Platform = GameEngine::Platform;
using Platform::Win32::Win32Input;
using Platform::Win32::Win32ImeCommitTestAccess;
using TestSupport::Expect;

Platform::InputState Read(Win32Input& input)
{
    Platform::InputState state;
    input.ReadState(state);
    return state;
}

void Enter(Win32Input& input)
{
    input.HandleMessage(nullptr, WM_KEYDOWN, VK_RETURN, static_cast<LPARAM>(0x1c) << 16);
    input.HandleMessage(nullptr, WM_KEYUP, VK_RETURN, static_cast<LPARAM>(0x1c) << 16);
}
}

bool RunWin32ImeSubmitCommitTests()
{
    for (const bool resultFirst : { false, true })
    {
        Win32Input input;
        input.HandleMessage(nullptr, WM_SETFOCUS, 0, 0);
        input.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
        if (resultFirst) Win32ImeCommitTestAccess::Result(input, L"한");
        Enter(input);
        if (!resultFirst) Win32ImeCommitTestAccess::Result(input, L"한");
        auto state = Read(input);
        if (!Expect(state.typedText == "한" && state.compositionId != 0 &&
            state.committedCompositionId == state.compositionId &&
            state.imeEnterCompositionId == state.committedCompositionId && state.imeHandledEnter,
            "same-read IME Enter and result must identify the same committed Korean composition in either order")) return false;
        const auto id = state.compositionId;
        state = Read(input);
        if (!Expect(state.compositionId == id && state.committedCompositionId == 0 &&
            state.imeEnterCompositionId == 0 && state.typedText.empty(),
            "reading must drain commit and Enter events without erasing composition identity")) return false;
    }

    Win32Input delayed;
    delayed.HandleMessage(nullptr, WM_SETFOCUS, 0, 0);
    delayed.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
    Enter(delayed);
    const auto pendingId = Read(delayed).imeEnterCompositionId;
    delayed.HandleMessage(nullptr, WM_IME_ENDCOMPOSITION, 0, 0);
    auto state = Read(delayed);
    if (!Expect(pendingId != 0 && state.compositionId == pendingId && state.committedCompositionId == 0,
        "END without RESULT must preserve identity but cannot claim a committed character")) return false;
    if (!Expect(!Win32ImeCommitTestAccess::Result(delayed, std::nullopt) && Read(delayed).committedCompositionId == 0,
        "failed RESULT reads must leave fallback processing open without fabricating a commit")) return false;
    Win32ImeCommitTestAccess::Result(delayed, L"글");
    state = Read(delayed);
    if (!Expect(state.typedText == "글" && state.committedCompositionId == pendingId,
        "a valid delayed result after END must retain the original Enter composition identity")) return false;

    delayed.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
    const auto nextId = Read(delayed).compositionId;
    Win32ImeCommitTestAccess::Result(delayed, L"");
    state = Read(delayed);
    if (!Expect(nextId != pendingId && state.committedCompositionId == nextId && state.typedText.empty(),
        "a new composition must not satisfy a cancelled composition; a valid empty result still has an identity")) return false;
    delayed.HandleMessage(nullptr, WM_IME_STARTCOMPOSITION, 0, 0);
    Enter(delayed);
    state = Read(delayed);
    return Expect(state.imeEnterCompositionId != nextId && state.committedCompositionId == 0,
        "Enter in a new composition cannot be paired with a preceding syllable's result");
}

namespace
{
const TestSupport::Registration gWin32ImeSubmitCommitTests{
    "UIEvent", "Win32 IME commit identity and delayed Enter results", RunWin32ImeSubmitCommitTests };
}
