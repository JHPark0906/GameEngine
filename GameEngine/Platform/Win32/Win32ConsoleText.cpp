#include "pch.h"
#include "Win32ConsoleText.h"

#include <windows.h>

namespace GameEngine::Platform
{

std::string ConsoleBytesToUtf8(const std::string_view bytes)
{
    if (bytes.empty())
    {
        return {};
    }
    // 자식의 콘솔 코드페이지다. 이 프로세스의 GetConsoleOutputCP()가 아니다: 그 값은 우리를
    // 띄운 셸이 정한 것이라 자식과는 상관이 없고, 실제로 같은 실행 파일이 셸에 따라 다른
    // 글자를 읽게 만든다. 새로 만들어지는 콘솔은 시스템의 OEM 코드페이지로 시작하므로
    // 자식이 쓸 코드페이지는 그것이다.
    const UINT codePage = GetOEMCP();
    const int wideLength = MultiByteToWideChar(
        codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLength <= 0)
    {
        return std::string(bytes);
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(
        codePage, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), wideLength);

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
    {
        return std::string(bytes);
    }
    std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wideLength, utf8.data(), utf8Length, nullptr, nullptr);
    return utf8;
}

}
