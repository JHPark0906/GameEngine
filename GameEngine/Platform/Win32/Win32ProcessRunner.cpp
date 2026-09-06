#include "pch.h"
#include "Win32ProcessRunner.h"

#include <windows.h>

#include <array>
#include <memory>
#include <system_error>
#include <utility>

#include "Win32ConsoleText.h"

#include "../../Core/TextEncoding.h"
#include "../../Diagnostics/Debug.h"

namespace GameEngine::Platform
{

namespace
{
    struct HandleCloser
    {
        void operator()(const HANDLE handle) const
        {
            if (handle && handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
            }
        }
    };

    using UniqueHandle = std::unique_ptr<void, HandleCloser>;

    /// <summary>
    /// UTF-8 인자를 이 API가 받는 와이드 문자열로 바꾼다.
    ///
    /// 변환 자체는 Core가 한다. Windows의 <c>wchar_t</c>가 UTF-16과 같은 비트라는 것이 이
    /// 파일이 아는 플랫폼 지식이고, 그래서 두 바이트짜리 요소를 그대로 옮겨 담는다.
    ///
    /// UTF-8이 아닌 인자는 빈 문자열이 된다. 부르는 쪽이 만든 인자는 경로와 스위치이므로
    /// 여기 도착하는 것은 이미 UTF-8이고, 아니라면 명령을 짐작해 고치는 것보다 비어서
    /// 실패하는 편이 낫다.
    /// </summary>
    [[nodiscard]] std::wstring Utf8ToWide(const std::string& text)
    {
        const std::optional<std::u16string> utf16 = Core::Utf8ToUtf16(text);
        if (!utf16)
        {
            Diagnostics::Debug::LogError(
                "A process argument is not valid UTF-8 and was dropped.");
            return {};
        }
        return std::wstring(utf16->begin(), utf16->end());
    }

    /// <summary>
    /// 감쌀 필요가 있는 인자인지다. 공백도 따옴표도 없는 인자는 그대로 두어야 한다.
    ///
    /// 전부 감싸면 되리라 생각하기 쉽지만 아니다: <c>cmd.exe</c>는 <c>/c</c>를 따옴표에 싸서
    /// 주면 스위치로 읽지 않고, 그러면 명령이 실행되지 않은 채 종료 코드만 돌아온다. 그런 실패는
    /// 프로세스가 도는 것처럼 보이므로 원인을 짚기 어렵다.
    /// </summary>
    [[nodiscard]] bool NeedsQuoting(const std::wstring& argument)
    {
        return argument.empty() ||
            argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    }

    /// <summary>
    /// 인자 하나를 명령줄에 실을 수 있게 감싼다. Windows의 명령줄은 문자열 하나라, 공백이 든
    /// 경로는 따옴표로 묶고 그 안의 역슬래시와 따옴표는 규칙대로 이스케이프해야 파서가 원래
    /// 문자열로 되돌린다.
    /// </summary>
    [[nodiscard]] std::wstring QuoteArgument(const std::wstring& argument)
    {
        if (!NeedsQuoting(argument))
        {
            return argument;
        }
        std::wstring quoted;
        quoted.push_back(L'"');
        std::size_t backslashCount = 0;
        for (const wchar_t character : argument)
        {
            if (character == L'\\')
            {
                ++backslashCount;
                continue;
            }
            if (character == L'"')
            {
                quoted.append(backslashCount * 2 + 1, L'\\');
                quoted.push_back(character);
                backslashCount = 0;
                continue;
            }
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
            quoted.push_back(character);
        }
        quoted.append(backslashCount * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    /// <summary>
    /// 실행 파일 경로를 명령줄의 첫 자리에 실을 수 있게 감싼다. 인자와 달리 공백이 없어도
    /// 반드시 따옴표를 두른다: <c>cmd.exe</c>는 자기 명령줄을 스스로 해석해서, 벗겨진
    /// <c>C:/Windows/System32/cmd.exe</c>의 슬래시들을 자기 스위치로 읽고 「구문이 잘못되었습니다」만
    /// 내놓는다. Windows 경로에는 따옴표가 들어갈 수 없으므로 감싸는 것으로 충분하다.
    /// </summary>
    [[nodiscard]] std::wstring QuoteProgramPath(const std::wstring& path)
    {
        return L"\"" + path + L"\"";
    }

}

Win32ProcessRunner::~Win32ProcessRunner()
{
    RequestCancel();
    Close();
}

bool Win32ProcessRunner::Start(const ProcessRequest& request)
{
    if (mRunning.load())
    {
        Diagnostics::Debug::LogError(
            "This process runner is already running something. executable=",
            request.executable.string());
        return false;
    }
    Close();
    mExitCode.reset();
    {
        const std::scoped_lock lock(mLinesMutex);
        mLines.clear();
    }
    mPartial.clear();

    // 자식이 낳는 것까지 한 무리로 묶는다. 이 핸들이 닫히면 무리가 함께 끝난다 — 취소가
    // 손자에게까지 닿는 근거가 이 한 줄이다.
    UniqueHandle job{ CreateJobObjectW(nullptr, nullptr) };
    if (!job)
    {
        Diagnostics::Debug::LogError("Failed to create a job for the child process.");
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        Diagnostics::Debug::LogError("Failed to limit the job for the child process.");
        return false;
    }

    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
    {
        Diagnostics::Debug::LogError("Failed to create a pipe for the child process.");
        return false;
    }
    UniqueHandle readPipe{ readEnd };
    UniqueHandle writePipe{ writeEnd };
    // 읽는 쪽은 자식이 물려받지 않는다. 물려받으면 자식이 끝나도 파이프가 열린 채로 남아
    // 읽기가 영원히 끝나지 않는다.
    if (!SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0))
    {
        Diagnostics::Debug::LogError("Failed to prevent the child from inheriting its read pipe.");
        return false;
    }

    // 표준 입력은 빈 장치로 준다. STARTF_USESTDHANDLES를 쓰면서 하나라도 비워 두면 자식은
    // 쓸 수 없는 핸들을 물려받고, cmd처럼 세 개를 다 여는 프로그램은 시작하자마자 엉뚱한
    // 오류를 내며 끝난다. 빈 장치는 뜻도 맞다: 입력을 기다리는 도구가 몇 분짜리 빌드를
    // 영원히 멈춰 세우는 대신 즉시 EOF를 받는다.
    UniqueHandle standardInput{ CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
        OPEN_EXISTING, 0, nullptr) };
    if (standardInput.get() == INVALID_HANDLE_VALUE)
    {
        Diagnostics::Debug::LogError("Failed to open the null device for the child process.");
        return false;
    }

    // 구분자를 Windows의 것으로 맞춘다. std::filesystem이 다루는 경로에는 슬래시가 흔히
    // 남는데, 대부분의 프로그램은 그대로 받아도 되지만 cmd.exe는 그런 경로로 불리면
    // 시작하지 못한 채 「경로를 찾을 수 없습니다」만 내놓는다.
    std::filesystem::path executable = request.executable;
    executable.make_preferred();
    std::wstring commandLine = QuoteProgramPath(executable.native());
    for (const std::string& argument : request.arguments)
    {
        commandLine.push_back(L' ');
        commandLine += QuoteArgument(Utf8ToWide(argument));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    // 표준 출력과 표준 오류가 같은 파이프로 간다. 둘을 따로 읽으면 콘솔에서 순서가 섞인다.
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    startup.hStdInput = standardInput.get();

    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = request.workingDirectory.native();
    const BOOL started = CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
    const DWORD startError = started ? ERROR_SUCCESS : GetLastError();
    // 쓰는 쪽은 자식이 가져갔다. 이쪽이 쥐고 있으면 자식이 끝나도 파이프가 닫히지 않는다.
    writePipe.reset();
    standardInput.reset();
    if (!started)
    {
        Diagnostics::Debug::LogError(
            "Failed to start a process. executable=", request.executable.string(),
            ", error=", startError);
        return false;
    }
    UniqueHandle child{ process.hProcess };
    UniqueHandle primaryThread{ process.hThread };

    // 멈춘 채로 만들어 job에 넣고 나서 깨운다. 먼저 달리게 하면 그 사이에 낳은 손자가 job
    // 밖에 남아, 취소해도 죽지 않는 프로세스가 생긴다.
    if (!AssignProcessToJobObject(job.get(), child.get()))
    {
        Diagnostics::Debug::LogError("Failed to put the child process into its job.");
        // 아직 실행하지 않은 자식은 job으로 취소할 수 없다. 직접 끝내고 시작 실패로 답한다.
        static_cast<void>(TerminateProcess(child.get(), ERROR_PROCESS_ABORTED));
        return false;
    }
    if (ResumeThread(primaryThread.get()) == static_cast<DWORD>(-1))
    {
        Diagnostics::Debug::LogError("Failed to resume the child process.");
        return false;
    }

    mJob = job.release();
    mProcess = child.release();
    mReadEnd = readPipe.release();
    mRunning.store(true);
    try
    {
        mReader = std::thread(&Win32ProcessRunner::ReadLoop, this);
    }
    catch (const std::system_error& error)
    {
        RequestCancel();
        Close();
        Diagnostics::Debug::LogError("Failed to start the process output reader. error=", error.what());
        return false;
    }
    return true;
}

void Win32ProcessRunner::ReadLoop()
{
    std::array<char, 4096> buffer{};
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(
                static_cast<HANDLE>(mReadEnd), buffer.data(),
                static_cast<DWORD>(buffer.size()), &read, nullptr) ||
            read == 0)
        {
            break;
        }
        std::string_view chunk(buffer.data(), read);
        std::string pending = mPartial;
        pending.append(chunk);
        mPartial.clear();

        std::size_t start = 0;
        for (;;)
        {
            const std::size_t end = pending.find('\n', start);
            if (end == std::string::npos)
            {
                mPartial = pending.substr(start);
                break;
            }
            std::string line = pending.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            {
                const std::scoped_lock lock(mLinesMutex);
                mLines.push_back(ConsoleBytesToUtf8(line));
            }
            start = end + 1;
        }
    }

    // 줄바꿈 없이 끝난 마지막 줄도 사람이 읽을 값이다.
    if (!mPartial.empty())
    {
        const std::scoped_lock lock(mLinesMutex);
        mLines.push_back(ConsoleBytesToUtf8(mPartial));
        mPartial.clear();
    }
    mRunning.store(false);
}

bool Win32ProcessRunner::IsRunning() const
{
    return mRunning.load();
}

void Win32ProcessRunner::Poll(const std::function<void(std::string_view line)>& onLine)
{
    // 잠근 채로 콜백을 부르지 않는다. 콜백은 콘솔 패널에 줄을 넣는 일이고, 그동안 읽기 스레드가
    // 막히면 파이프가 차서 자식이 멈춘다.
    std::deque<std::string> taken;
    {
        const std::scoped_lock lock(mLinesMutex);
        taken.swap(mLines);
    }
    for (const std::string& line : taken)
    {
        onLine(line);
    }
}

std::optional<int> Win32ProcessRunner::TakeExitCode()
{
    if (mRunning.load() || !mProcess)
    {
        return std::nullopt;
    }
    if (!mExitCode)
    {
        DWORD code = 0;
        if (GetExitCodeProcess(static_cast<HANDLE>(mProcess), &code) && code != STILL_ACTIVE)
        {
            mExitCode = static_cast<int>(code);
        }
        else
        {
            return std::nullopt;
        }
    }
    const std::optional<int> code = mExitCode;
    mExitCode.reset();
    Close();
    return code;
}

void Win32ProcessRunner::RequestCancel()
{
    if (!mJob)
    {
        return;
    }
    // job을 닫으면 무리가 함께 끝난다. 자식 하나를 죽이는 것으로는 손자가 남는다.
    CloseHandle(static_cast<HANDLE>(mJob));
    mJob = nullptr;
}

void Win32ProcessRunner::Close()
{
    if (mReader.joinable())
    {
        // 파이프의 쓰는 쪽은 자식들이 쥐고 있다. job이 닫혀 그들이 끝나면 읽기가 0을 받고
        // 스레드가 스스로 빠져나온다.
        mReader.join();
    }
    if (mReadEnd)
    {
        CloseHandle(static_cast<HANDLE>(mReadEnd));
        mReadEnd = nullptr;
    }
    if (mProcess)
    {
        CloseHandle(static_cast<HANDLE>(mProcess));
        mProcess = nullptr;
    }
    if (mJob)
    {
        CloseHandle(static_cast<HANDLE>(mJob));
        mJob = nullptr;
    }
    mRunning.store(false);
}

}
