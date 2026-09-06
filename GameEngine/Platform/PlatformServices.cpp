#include "pch.h"
#include "PlatformServices.h"

// Porting these services means adding an implementation and one branch here.
#include "Win32/Win32AudioOutput.h"
#include "Win32/Win32AudioDecoder.h"
#include "Win32/Win32ChoiceDialog.h"
#include "Win32/Win32Clipboard.h"
#include "Win32/Win32ProcessRunner.h"
#include "Win32/Win32DirectoryWatcher.h"
#include "Win32/Win32FileDialog.h"
#include "Win32/Win32FileMapping.h"
#include "Win32/Win32ImageDecoder.h"
#include "Win32/Win32Utilities.h"
#include "../Diagnostics/Debug.h"
#include "../Text/EngineTextRasterizer.h"


#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
// 공용 파일 대화상자는 Win32의 것이다. 포팅은 이 구현과 분기 하나를 더한다.

#pragma comment(lib, "comdlg32.lib")

namespace GameEngine::Platform
{

std::unique_ptr<IImageDecoder> PlatformServices::CreateImageDecoder()
{
    return std::make_unique<Win32::Win32ImageDecoder>();
}

std::unique_ptr<IAudioDecoder> PlatformServices::CreateAudioDecoder()
{
    return std::make_unique<Win32::Win32AudioDecoder>();
}

std::unique_ptr<ITextRasterizer> PlatformServices::CreateTextRasterizer()
{
    // 엔진 자체 구현이 유일한 텍스트 래스터라이저다. 플랫폼의 폰트 스택을 쓰지 않는다.
    return std::make_unique<Text::EngineTextRasterizer>();
}

std::unique_ptr<IClipboard> PlatformServices::CreateClipboard()
{
    return std::make_unique<Win32::Win32Clipboard>();
}

std::unique_ptr<IProcessRunner> PlatformServices::CreateProcessRunner()
{
    return std::make_unique<Win32ProcessRunner>();
}

std::unique_ptr<IDirectoryWatcher> PlatformServices::CreateDirectoryWatcher(
    const std::filesystem::path& directory)
{
    return std::make_unique<Win32::Win32DirectoryWatcher>(directory);
}

std::unique_ptr<IAudioOutput> PlatformServices::CreateAudioOutput()
{
    return std::make_unique<Win32::Win32AudioOutput>();
}

std::filesystem::path PlatformServices::GetExecutableDirectory()
{
    return Win32::GetExecutableDirectory();
}

std::filesystem::path PlatformServices::GetExecutablePath()
{
    return Win32::GetExecutablePath();
}

std::vector<std::string> PlatformServices::GetCommandLineArguments()
{
    return Win32::GetCommandLineArguments();
}

std::unique_ptr<IFileMapping> PlatformServices::MapFileReadOnly(const std::filesystem::path& path)
{
    auto mapping = std::make_unique<Win32::Win32FileMapping>(path);
    return mapping->GetBytes().empty() ? nullptr : std::move(mapping);
}

std::optional<std::filesystem::path> PlatformServices::ShowOpenFileDialog(
    const FileDialogRequest& request)
{
    return Win32::ShowFileDialog(request, false);
}

std::optional<std::filesystem::path> PlatformServices::ShowSaveFileDialog(
    const FileDialogRequest& request)
{
    return Win32::ShowFileDialog(request, true);
}

std::optional<std::size_t> PlatformServices::ShowChoiceDialog(const ChoiceDialogRequest& request)
{
    return Win32::ShowChoiceDialog(request);
}
}
