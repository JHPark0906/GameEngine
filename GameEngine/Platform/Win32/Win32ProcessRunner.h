#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "../IProcessRunner.h"

namespace GameEngine::Platform
{

/// <summary>
/// Win32에서 프로그램 하나를 돌린다.
///
/// 자식은 Job Object 안에서 시작한다. cmake는 msbuild를, msbuild는 컴파일러와 링커를 낳으므로
/// 직접 자식만 끝내면 손자가 살아남아 빌드 트리에 락을 쥐고, 다음 빌드가 이유를 알 수 없게
/// 실패한다. Job은 그 무리 전체를 하나로 묶어, 핸들이 닫히면 함께 끝나게 한다.
///
/// 읽기는 전용 스레드가 한다. 파이프에서 읽는 것은 막히는 일이라 프레임 안에서 할 수 없고,
/// 읽은 줄은 뮤텍스가 지키는 큐에 쌓였다가 <see cref="Poll"/>에서 한꺼번에 넘어간다.
/// </summary>
class Win32ProcessRunner final : public IProcessRunner
{
public:
    Win32ProcessRunner() = default;
    ~Win32ProcessRunner() override;

    [[nodiscard]] bool Start(const ProcessRequest& request) override;
    [[nodiscard]] bool IsRunning() const override;
    void Poll(const std::function<void(std::string_view line)>& onLine) override;
    [[nodiscard]] std::optional<int> TakeExitCode() override;
    void RequestCancel() override;

private:
    /// <summary>파이프와 핸들을 닫고 읽기 스레드를 거둔다. 시작 실패와 소멸이 함께 쓴다.</summary>
    void Close();

    /// <summary>읽기 스레드의 본체다. 파이프가 닫힐 때까지 읽어 줄로 잘라 큐에 넣는다.</summary>
    void ReadLoop();

    /// <summary>자식 프로세스와 그 무리를 담은 핸들들이다. <c>void*</c>인 것은 windows.h를
    /// 이 헤더로 끌어들이지 않기 위해서다 — 그 헤더는 매크로로 남의 이름을 바꾼다.</summary>
    void* mJob = nullptr;
    void* mProcess = nullptr;
    void* mReadEnd = nullptr;

    std::thread mReader;
    std::mutex mLinesMutex;
    std::deque<std::string> mLines;

    /// <summary>읽다 남은 조각이다. 파이프는 줄 경계에서 끊기지 않는다.</summary>
    std::string mPartial;

    std::atomic<bool> mRunning{ false };
    std::optional<int> mExitCode;
};

}
