#include "pch.h"
#include "ProcessRun.h"

#include <chrono>
#include <memory>
#include <thread>

#include "PlatformServices.h"

namespace GameEngine::Platform
{

namespace
{
    /// <summary>
    /// 줄을 확인하는 간격이다. 사람이 읽는 출력이라 이보다 촘촘할 이유가 없고, 이보다 성기면
    /// 빌드 도구의 출력이 뭉쳐 나와 흐르는 것처럼 보이지 않는다.
    /// </summary>
    constexpr std::chrono::milliseconds PollInterval{ 10 };
}

std::optional<int> RunProcessToCompletion(
    const ProcessRequest& request, const std::function<void(std::string_view line)>& onLine)
{
    const std::unique_ptr<IProcessRunner> runner = PlatformServices::CreateProcessRunner();
    if (!runner || !runner->Start(request))
    {
        return std::nullopt;
    }
    for (;;)
    {
        runner->Poll(onLine);
        if (const std::optional<int> exitCode = runner->TakeExitCode())
        {
            // 끝난 뒤에도 큐에 남은 줄이 있다. 그것을 버리면 마지막 오류 줄이 사라진다.
            runner->Poll(onLine);
            return exitCode;
        }
        std::this_thread::sleep_for(PollInterval);
    }
}

}
