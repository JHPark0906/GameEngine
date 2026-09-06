#include "pch.h"
#include "Win32Clipboard.h"

#include <windows.h>

#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "../../Diagnostics/Debug.h"
#include "Win32Utilities.h"

namespace GameEngine::Platform::Win32
{

namespace
{
    /// <summary>
    /// 열린 클립보드다. 클립보드는 프로세스 간 공유 자원이라 열었으면 반드시 닫아야 하고, 다른
    /// 프로세스가 쥐고 있으면 열기가 실패할 수 있다 — 실패는 조용히 빈손으로 처리한다.
    /// </summary>
    class OpenedClipboard final
    {
    public:
        OpenedClipboard() : mOpen(OpenClipboard(nullptr) != FALSE) {}
        ~OpenedClipboard()
        {
            if (mOpen)
            {
                CloseClipboard();
            }
        }
        OpenedClipboard(const OpenedClipboard&) = delete;
        OpenedClipboard& operator=(const OpenedClipboard&) = delete;

        [[nodiscard]] bool IsOpen() const { return mOpen; }

    private:
        bool mOpen;
    };
}

std::string Win32Clipboard::GetText()
{
    const OpenedClipboard clipboard;
    if (!clipboard.IsOpen())
    {
        return {};
    }
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data)
    {
        return {};
    }
    const auto* const text = static_cast<const wchar_t*>(GlobalLock(data));
    if (!text)
    {
        return {};
    }
    std::string result = WideToUtf8(text);
    GlobalUnlock(data);
    return result;
}

void Win32Clipboard::SetText(const std::string_view text)
{
    const std::wstring wide = Utf8ToWide(text);

    const OpenedClipboard clipboard;
    if (!clipboard.IsOpen())
    {
        return;
    }
    if (!EmptyClipboard())
    {
        return;
    }

    // 클립보드에 놓인 메모리는 시스템 소유가 된다: SetClipboardData가 성공하면 해제 책임이
    // 시스템으로 넘어가고, 실패했을 때만 이쪽이 되돌려 받는다.
    const std::size_t byteCount = (wide.size() + 1) * sizeof(wchar_t);
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (!memory)
    {
        return;
    }
    void* const destination = GlobalLock(memory);
    if (!destination)
    {
        GlobalFree(memory);
        return;
    }
    std::memcpy(destination, wide.c_str(), byteCount);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory))
    {
        GlobalFree(memory);
    }
}

}
