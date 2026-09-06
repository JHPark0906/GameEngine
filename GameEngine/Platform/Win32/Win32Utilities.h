#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace GameEngine::Platform::Win32
{

/// <summary>현재 프로세스 실행 파일의 정규화된 절대 경로를 반환한다.</summary>
[[nodiscard]] std::filesystem::path GetExecutablePath();

/// <summary>현재 프로세스 실행 파일이 위치한 디렉터리를 반환한다.</summary>
[[nodiscard]] std::filesystem::path GetExecutableDirectory();

/// <summary>
/// UTF-16 텍스트를 UTF-8로 변환한다. Windows는 애플리케이션에 텍스트를 UTF-16으로 건네고, 이
/// 계층 위의 모든 것은 UTF-8이므로, 그것이 끝나는 곳이 여기다.
/// </summary>
[[nodiscard]] std::string WideToUtf8(std::wstring_view text);

/// <summary>
/// UTF-8 텍스트를 UTF-16으로 변환한다. 이 계층 위에서 내려온 텍스트가 Windows API에 건네지기
/// 직전에 거치는 자리다. 변환할 수 없는 텍스트는 빈 문자열이 되고 로그로 말한다.
/// </summary>
[[nodiscard]] std::wstring Utf8ToWide(std::string_view text);

/// <summary>프로세스 명령줄을 인수로 나눈다. 실행 파일 이름은 빼고, UTF-8이다.</summary>
[[nodiscard]] std::vector<std::string> GetCommandLineArguments();

}
