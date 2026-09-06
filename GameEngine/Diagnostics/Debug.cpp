#include "pch.h"
#include "Debug.h"

#include <atomic>

#include <cstdint>
#include <iostream>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameEngine::Diagnostics
{

namespace
{
    /// <summary>
    /// 일반 메시지를 남기는지다. 로그는 렌더 스레드에서도 불리므로 atomic이고,
    /// 순서를 지킬 일은 없어 relaxed다 — 켜고 끔는 순간에 한 줄이 더 나거나 덜 나오는 것은
    /// 이 값이 답하려는 질문이 아니다.
    /// </summary>
    std::atomic<bool> gMessagesEnabled{ LogsMessagesByDefault };
}

namespace
{
    std::mutex gLogMutex;
    std::unordered_map<Debug::LogListenerId, Debug::LogListener> gLogListeners;
    std::vector<LogEntry> gRecentLogs;
    std::uint64_t gLogRevision = 0;
    Debug::LogListenerId gNextLogListenerId = 1;
    constexpr size_t MaxRecentLogCount = 256;

}

const char* Debug::GetLogLevelName(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Message:
        return "LOG";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    }

    return "LOG";
}

void Debug::Log(const char* const message)
{
    if (AreMessagesEnabled())
    {
        Write(LogLevel::Message, message);
    }
}

void Debug::SetMessagesEnabled(const bool enabled)
{
    gMessagesEnabled.store(enabled, std::memory_order_relaxed);
}

bool Debug::AreMessagesEnabled()
{
    return gMessagesEnabled.load(std::memory_order_relaxed);
}

void Debug::LogWarning(const char* message)
{
    Write(LogLevel::Warning, message);
}

void Debug::LogError(const char* message)
{
    Write(LogLevel::Error, message);
}

Debug::LogListenerId Debug::AddLogListener(LogListener listener)
{
    if (!listener)
    {
        return 0;
    }

    std::lock_guard lock(gLogMutex);
    const LogListenerId listenerId = gNextLogListenerId++;
    gLogListeners.emplace(listenerId, std::move(listener));
    return listenerId;
}

void Debug::RemoveLogListener(const LogListenerId listenerId)
{
    std::lock_guard lock(gLogMutex);
    gLogListeners.erase(listenerId);
}

std::vector<LogEntry> Debug::GetRecentLogs()
{
    std::lock_guard lock(gLogMutex);
    return gRecentLogs;
}

void Debug::ClearRecentLogs()
{
    std::lock_guard lock(gLogMutex);
    gRecentLogs.clear();
    ++gLogRevision;
}

std::uint64_t Debug::GetLogRevision()
{
    std::lock_guard lock(gLogMutex);
    return gLogRevision;
}

void Debug::Write(const LogLevel level, const std::string& message)
{
    const LogEntry entry{ level, message };
    const std::string formattedMessage =
        "[" + std::string(GetLogLevelName(level)) + "] " + message + '\n';
    std::vector<LogListener> listeners;

    {
        std::lock_guard lock(gLogMutex);
        std::clog << formattedMessage;

        if (gRecentLogs.size() == MaxRecentLogCount)
        {
            gRecentLogs.erase(gRecentLogs.begin());
        }
        gRecentLogs.push_back(entry);
        ++gLogRevision;

        listeners.reserve(gLogListeners.size());
        for (const auto& listener : gLogListeners | std::views::values)
        {
            listeners.push_back(listener);
        }
    }

    for (const LogListener& listener : listeners)
    {
        try
        {
            listener(entry);
        }
        catch (...)
        {
            // Logging must never fail because an observer is shutting down.
        }
    }
}

}
