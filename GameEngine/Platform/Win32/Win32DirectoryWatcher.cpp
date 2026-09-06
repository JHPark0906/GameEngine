#include "pch.h"
#include "Win32DirectoryWatcher.h"

#include <windows.h>

#include <array>
#include <cstddef>

#include "../../Diagnostics/Debug.h"

namespace GameEngine::Platform::Win32
{

struct Win32DirectoryWatcher::Implementation
{
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE completion = nullptr;
    OVERLAPPED overlapped{};
    /// <summary>
    /// 커널이 변동 기록을 써 넣는 곳이다. 읽기가 걸려 있는 동안은 옮기거나 풀면 안 되므로,
    /// 소멸자가 읽기를 취소하고 끝나기를 기다린 뒤에야 이 객체가 사라진다.
    /// </summary>
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer{};
    bool readPending = false;
    bool valid = false;

    [[nodiscard]] bool IssueRead()
    {
        ResetEvent(completion);
        const BOOL issued = ReadDirectoryChangesW(
            directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
            nullptr, &overlapped, nullptr);
        if (!issued)
        {
            Diagnostics::Debug::LogError(
                "Watching a directory for changes stopped. error=", GetLastError());
            valid = false;
            return false;
        }
        readPending = true;
        return true;
    }

    void Close()
    {
        if (directory != INVALID_HANDLE_VALUE)
        {
            if (readPending)
            {
                // 걸려 있는 읽기는 버퍼를 쓰고 있을 수 있다. 취소하고 끝나기를 기다린 뒤에 닫는다.
                CancelIoEx(directory, &overlapped);
                DWORD bytes = 0;
                static_cast<void>(GetOverlappedResult(directory, &overlapped, &bytes, TRUE));
                readPending = false;
            }
            CloseHandle(directory);
            directory = INVALID_HANDLE_VALUE;
        }
        if (completion)
        {
            CloseHandle(completion);
            completion = nullptr;
        }
        valid = false;
    }
};

Win32DirectoryWatcher::Win32DirectoryWatcher(const std::filesystem::path& directory)
    : mImplementation(std::make_unique<Implementation>())
{
    Implementation& data = *mImplementation;
    // FILE_LIST_DIRECTORY로 여는 디렉터리 핸들이 변동 알림의 원천이다. 다른 프로세스가 그 안의
    // 파일을 자유롭게 만들고 지울 수 있어야 하므로 공유 모드는 전부 연다.
    data.directory = CreateFileW(
        directory.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (data.directory == INVALID_HANDLE_VALUE)
    {
        Diagnostics::Debug::LogError(
            "Could not open a directory to watch it for changes. path=", directory.string(),
            ", error=", GetLastError());
        return;
    }
    data.completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!data.completion)
    {
        Diagnostics::Debug::LogError(
            "Could not create the completion event for a directory watch. error=", GetLastError());
        data.Close();
        return;
    }
    data.overlapped.hEvent = data.completion;
    data.valid = true;
    if (!data.IssueRead())
    {
        data.Close();
    }
}

Win32DirectoryWatcher::~Win32DirectoryWatcher()
{
    mImplementation->Close();
}

bool Win32DirectoryWatcher::IsValid() const
{
    return mImplementation->valid;
}

bool Win32DirectoryWatcher::PollChanges()
{
    Implementation& data = *mImplementation;
    if (!data.valid)
    {
        return false;
    }

    bool changed = false;
    for (;;)
    {
        if (!data.readPending && !data.IssueRead())
        {
            return changed;
        }
        DWORD bytes = 0;
        if (GetOverlappedResult(data.directory, &data.overlapped, &bytes, FALSE))
        {
            // 완료됐다. 무엇이 바뀌었는지는 읽지 않는다 — 듣는 쪽은 전체를 다시 훑는다. 바이트가
            // 0이면 알림이 넘쳐 기록을 잃은 것이고, 그것도 "바뀌었다"다. 다시 걸어 두고 그 사이에
            // 쌓인 것이 더 있는지 이어서 본다.
            data.readPending = false;
            changed = true;
            continue;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE)
        {
            break;
        }
        Diagnostics::Debug::LogError(
            "Watching a directory for changes stopped. error=", error);
        data.readPending = false;
        data.valid = false;
        break;
    }
    return changed;
}

}
