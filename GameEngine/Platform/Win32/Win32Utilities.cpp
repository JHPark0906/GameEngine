#include "pch.h"
#include "Win32Utilities.h"
#include "../../Core/TextEncoding.h"
#include "../../Diagnostics/Debug.h"


#include <windows.h>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <shellapi.h>
#include <string>
#include <vector>

namespace GameEngine::Platform::Win32
{

std::filesystem::path GetExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    constexpr std::size_t maxCapacity = (std::numeric_limits<DWORD>::max)();

    while (buffer.size() <= maxCapacity)
    {
        SetLastError(ERROR_SUCCESS);
        const DWORD capacity = static_cast<DWORD>(buffer.size());
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0)
        {
            Diagnostics::Debug::LogError(
                "GetModuleFileNameW failed. error=", GetLastError());
            return {};
        }
        if (length < capacity)
        {
            return std::filesystem::path(buffer.data(), buffer.data() + length).lexically_normal();
        }

        if (buffer.size() == maxCapacity)
        {
            break;
        }
        const std::size_t nextCapacity = buffer.size() > maxCapacity / 2
            ? maxCapacity
            : buffer.size() * 2;
        buffer.resize(nextCapacity);
    }

    Diagnostics::Debug::LogError("The executable path exceeds the Win32 path buffer limit.");
    return {};
}

std::filesystem::path GetExecutableDirectory()
{
    const std::filesystem::path executablePath = GetExecutablePath();
    return executablePath.empty() ? std::filesystem::path{} : executablePath.parent_path();
}

std::string WideToUtf8(const std::wstring_view text)
{
    // Windows에서 wchar_t는 UTF-16 단위다. 그 사실을 아는 것은 플랫폼을 아는 코드의 몫이고,
    // 변환 규칙 자체는 Core에 하나만 있다.
    return Core::Utf16ToUtf8(std::u16string(text.begin(), text.end()));
}

std::wstring Utf8ToWide(const std::string_view text)
{
    // 이 함수는 읽을 수 없는 입력을 빈 문자열로 처리한다. 실패 이유를 자기 문맥과 함께
    // 보고해야 하는 호출자는 Core 변환 함수를 직접 사용한다.
    const std::optional<std::u16string> utf16 = Core::Utf8ToUtf16(text);
    return utf16 ? std::wstring(utf16->begin(), utf16->end()) : std::wstring{};
}

std::vector<std::string> GetCommandLineArguments()
{
    int argumentCount = 0;
    LPWSTR* const arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
    {
        return {};
    }
    std::vector<std::string> result;
    for (int index = 1; index < argumentCount; ++index)
    {
        result.push_back(WideToUtf8(arguments[index]));
    }
    LocalFree(arguments);
    return result;
}

}
