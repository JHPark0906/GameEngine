#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// FileDialogRequest가 값으로 담으므로 이것만 완전한 타입이 필요하다. 나머지 서비스는 unique_ptr로
// 돌아가고 unique_ptr 선언에는 전방 선언이면 충분하니, 여기서 그 헤더들을 들이지 않는다:
// GetExecutableDirectory() 하나만 쓰는 소비자가 오디오나 텍스트 인터페이스의 변경에 다시
// 컴파일될 이유가 없다. 반환값을 실제로 쥐는 쪽이 자기가 쓰는 인터페이스를 포함한다.
#include "NativeSurface.h"

namespace GameEngine::Platform
{

class IAudioOutput;
class IAudioDecoder;
class IClipboard;
class IProcessRunner;
class IDirectoryWatcher;
class IFileMapping;
class IImageDecoder;
class ITextRasterizer;

/// <summary>
/// 엔진이 필요로 하지만 스스로 제공할 수 없는 서비스들의 플랫폼 구현을 만든다. WindowFactory와
/// 더불어, 구체 구현의 이름을 부르는 유일한 자리다.
/// </summary>
class PlatformServices final
{
public:
    /// <summary>이 플랫폼의 초기화되지 않은 이미지 디코더를 만든다.</summary>
    [[nodiscard]] static std::unique_ptr<IImageDecoder> CreateImageDecoder();

    /// <summary>메모리의 압축 오디오를 PCM으로 바꾸는 이 플랫폼의 디코더를 만든다.</summary>
    [[nodiscard]] static std::unique_ptr<IAudioDecoder> CreateAudioDecoder();

    /// <summary>이 플랫폼의 초기화되지 않은 텍스트 래스터라이저를 만든다.</summary>
    [[nodiscard]] static std::unique_ptr<ITextRasterizer> CreateTextRasterizer();

    /// <summary>이 플랫폼의 시스템 클립보드를 만든다.</summary>
    [[nodiscard]] static std::unique_ptr<IClipboard> CreateClipboard();

    /// <summary>
    /// 프로그램 하나를 돌리고 그 출력을 읽는 러너다. 한 러너가 한 번에 하나를 맡으므로,
    /// 동시에 둘을 돌릴 자리는 러너를 둘 만든다.
    /// </summary>
    [[nodiscard]] static std::unique_ptr<IProcessRunner> CreateProcessRunner();

    /// <summary>
    /// 이 플랫폼의 디렉터리 감시를 만든다. 그 디렉터리와 아래 전체의 변동을 지켜본다.
    /// </summary>
    /// <param name="directory">지켜볼 디렉터리의 절대 경로다.</param>
    /// <returns>감시다. 디렉터리를 열지 못했으면 IsValid가 거짓인 채로 돌아온다.</returns>
    [[nodiscard]] static std::unique_ptr<IDirectoryWatcher> CreateDirectoryWatcher(
        const std::filesystem::path& directory);

    /// <summary>이 플랫폼의 초기화되지 않은 오디오 출력을 만든다.</summary>
    /// <returns>Initialize를 기다리는 오디오 출력이다.</returns>
    [[nodiscard]] static std::unique_ptr<IAudioOutput> CreateAudioOutput();

    /// <summary>
    /// 실행 중인 실행 파일이 사는 디렉터리이다.
    ///
    /// 에셋 루트, 프로젝트 서술자, 셰이더 경로는 현재 작업 디렉터리가 아니라 이것에 대해
    /// 해석되므로, 배포된 빌드는 어디서 실행됐는지에 의존하지 않는다. 다른 플랫폼 서비스들과
    /// 함께 여기 속한다: Runtime과 Rendering이 Win32 구현을 이름으로 부르면, 포팅이 구현 하나와
    /// 분기 하나를 더하는 일로 끝나지 않는 설비가 이것 하나만 남는다.
    /// </summary>
    [[nodiscard]] static std::filesystem::path GetExecutableDirectory();

    /// <summary>
    /// 실행 중인 실행 파일 그 자체이다. 빌드에 packed된 콘텐츠는 이 파일에서 읽히므로, 윈도잉
    /// 시스템을 이름으로 부르는 대신 플랫폼을 통해 요청한다.
    /// </summary>
    [[nodiscard]] static std::filesystem::path GetExecutablePath();

    /// <summary>
    /// 프로세스가 받은 명령줄 인수다. 실행 파일 이름은 빼고, UTF-8이다. 프로젝트의 bootstrap이
    /// 열 파일 같은 것을 읽는 길이며, 그래서 프로젝트는 진입점 없이도 인수를 받는다.
    /// </summary>
    [[nodiscard]] static std::vector<std::string> GetCommandLineArguments();

    /// <summary>
    /// 파일을 읽기 전용으로 매핑하거나, 매핑할 수 없으면 null을 반환한다. 호출자는 바이트를 쓰는
    /// 동안 결과를 쥐고 있어야 한다.
    /// </summary>
    [[nodiscard]] static std::unique_ptr<IFileMapping> MapFileReadOnly(
        const std::filesystem::path& path);

    /// <summary>
    /// 파일 대화상자 하나가 필요로 하는 것들이다.
    ///
    /// 이 구조체의 넓은 문자열은 호환성을 위한 예외다. 이 계층을 건너는 새 텍스트 계약은
    /// UTF-8 std::string이며, 플랫폼 인코딩으로 바꾸는 일은 인터페이스 너머 구현의 몫이다.
    /// 예외는 여기의 네 문자열과 WindowDescription::title, ProjectSettings::projectName,
    /// ProjectFile::Extension이다. projectName은 .gameproject에 직렬화되므로 인코딩을 바꾸면
    /// 파일 호환성도 고려해야 한다. 새 계약이 예외를 늘리지 않도록 LayeringTests가 검사한다.
    /// </summary>
    struct FileDialogRequest
    {
        /// <summary>대화상자를 소유할 창이다. 무효하면 소유자 없이 뜬다.</summary>
        NativeSurface owner;
        /// <summary>제목 표시줄 텍스트이다.</summary>
        std::wstring title;
        /// <summary>필터가 보여줄 이름이다. 예: "Game Project".</summary>
        std::wstring filterName;
        /// <summary>점으로 시작하는 확장자이다. 예: ".gameproject". 필터와 기본 확장자가 된다.</summary>
        std::wstring extension;
        /// <summary>처음 보여줄 디렉터리이다. 비어 있으면 시스템이 정한다.</summary>
        std::filesystem::path initialDirectory;
        /// <summary>저장 대화상자가 미리 채울 파일 이름이다.</summary>
        std::wstring suggestedFileName;
    };

    /// <summary>
    /// 열 파일을 고르는 대화상자를 띄운다. 사람이 취소하면 빈 optional이다.
    /// 파일 선택은 플랫폼 설비이므로 에디터 로직은 윈도잉 시스템의 대화상자 API에 의존하지 않는다.
    /// </summary>
    [[nodiscard]] static std::optional<std::filesystem::path> ShowOpenFileDialog(
        const FileDialogRequest& request);

    /// <summary>저장할 파일을 고르는 대화상자를 띄운다. 덮어쓰기 확인을 포함한다.</summary>
    [[nodiscard]] static std::optional<std::filesystem::path> ShowSaveFileDialog(
        const FileDialogRequest& request);

    /// <summary>
    /// 사람이 버튼 하나를 눌러 고르는 대화상자가 필요로 하는 것들이다. 모든 텍스트는 UTF-8이며,
    /// 플랫폼의 인코딩으로 바꾸는 일은 구현의 몫이다.
    /// </summary>
    struct ChoiceDialogRequest
    {
        /// <summary>대화상자를 소유할 창이다. 무효하면 소유자 없이 뜬다.</summary>
        NativeSurface owner;
        /// <summary>제목 표시줄 텍스트이다.</summary>
        std::string title;
        /// <summary>버튼 위에 보이는 설명이다.</summary>
        std::string message;
        /// <summary>버튼 하나당 하나씩, 보이는 순서대로다. 첫 번째가 기본 버튼이다.</summary>
        std::vector<std::string> choices;
        /// <summary>취소 버튼의 라벨이다. 비어 있으면 취소 버튼을 두지 않는다.</summary>
        std::string cancelLabel;
    };

    /// <summary>
    /// 선택지마다 버튼 하나를 가진 모달 대화상자를 띄우고, 눌린 선택지의 인덱스를 답한다.
    /// 취소하거나 창을 닫으면 빈 optional이다.
    /// </summary>
    [[nodiscard]] static std::optional<std::size_t> ShowChoiceDialog(
        const ChoiceDialogRequest& request);
};

}
