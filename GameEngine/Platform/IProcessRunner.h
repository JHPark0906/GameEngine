#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>돌릴 프로그램 하나다.</summary>
struct ProcessRequest
{
    /// <summary>실행 파일이다. 절대 경로여야 한다 — PATH 탐색은 부르는 쪽이 이미 끝냈다.</summary>
    std::filesystem::path executable;
    /// <summary>
    /// 인자들이다. 하나씩 담는다: 명령줄 한 줄로 받으면 공백이 든 경로를 부르는 쪽마다 따로
    /// 따옴표 치게 되고, 그 규칙은 한 번 틀리면 조용히 다른 프로그램을 부른다.
    /// </summary>
    std::vector<std::string> arguments;
    /// <summary>그 프로그램이 시작할 디렉터리다. 비어 있으면 이 프로세스의 것을 물려받는다.</summary>
    std::filesystem::path workingDirectory;
};

/// <summary>
/// 프로그램 하나를 돌리고, 그것이 내는 줄을 읽고, 끝났는지 본다.
///
/// 게임 스레드를 막지 않는 것이 이 인터페이스의 전부다. 빌드는 몇 분이 걸리고 그동안 에디터는
/// 계속 그려져야 하므로, 읽기는 다른 스레드가 하고 프레임은 <see cref="Poll"/>로 모인 줄만
/// 가져간다 — 프레임에서 하는 일은 큐 하나를 비우는 것뿐이다.
///
/// 표준 출력과 표준 오류를 하나로 합쳐 읽는다. 컴파일러는 오류를 뒤쪽으로 내보내는데, 둘을
/// 따로 읽으면 콘솔에서 순서가 뒤섞여 어느 줄이 어느 파일의 오류인지 알 수 없게 된다.
///
/// 빌드 도구는 자식을 더 낳는다 — cmake가 msbuild를, msbuild가 컴파일러를. 그래서 이 인터페이스가
/// 약속하는 것은 <b>프로세스 트리 전체</b>의 수명이다: 취소나 소멸은 손자까지 끝내야 하고,
/// 그러지 않으면 남은 도구가 빌드 트리에 락을 쥔 채 다음 실행을 이유 없이 실패시킨다.
/// </summary>
class IProcessRunner
{
public:
    virtual ~IProcessRunner() = default;

    IProcessRunner(const IProcessRunner&) = delete;
    IProcessRunner& operator=(const IProcessRunner&) = delete;
    IProcessRunner(IProcessRunner&&) = delete;
    IProcessRunner& operator=(IProcessRunner&&) = delete;

    /// <summary>
    /// 프로그램을 시작한다. 이미 하나가 돌고 있으면 시작하지 않고 거짓이다 — 한 러너는 한 번에
    /// 하나만 맡는다. 두 개를 돌리려면 러너를 둘 두는 것이 맞다: 그래야 어느 줄이 어느 실행의
    /// 것인지 섞이지 않는다.
    /// </summary>
    [[nodiscard]] virtual bool Start(const ProcessRequest& request) = 0;

    /// <summary>아직 돌고 있는지다.</summary>
    [[nodiscard]] virtual bool IsRunning() const = 0;

    /// <summary>
    /// 지난 호출 이후 모인 줄들을 넘긴다. 프레임마다 부르라고 있는 함수이며, 줄이 없으면 아무
    /// 일도 하지 않는다.
    ///
    /// 넘어오는 텍스트는 UTF-8이다. 빌드 도구는 콘솔 코드페이지로 쓰므로 이 계층이 그것을
    /// 바꾼다 — 위쪽은 어느 코드페이지였는지 알 필요가 없다.
    /// </summary>
    /// <param name="onLine">줄 하나를 받는 호출 가능 객체다. 줄바꿈은 이미 떼어져 있다.</param>
    virtual void Poll(const std::function<void(std::string_view line)>& onLine) = 0;

    /// <summary>
    /// 끝났으면 그 종료 코드를 한 번 내준다. 아직 돌고 있거나 이미 가져갔으면 비어 있다.
    ///
    /// 한 번만 내주는 이유는 이것이 사건이기 때문이다: 「빌드가 끝났다」에 답하는 자리가 여럿이면
    /// 성공 메시지가 프레임마다 다시 나온다.
    /// </summary>
    [[nodiscard]] virtual std::optional<int> TakeExitCode() = 0;

    /// <summary>
    /// 지금 끝내라고 말한다. 프로세스 트리 전체가 대상이며, 돌고 있지 않으면 아무 일도 없다.
    /// </summary>
    virtual void RequestCancel() = 0;

protected:
    IProcessRunner() = default;
};

}
