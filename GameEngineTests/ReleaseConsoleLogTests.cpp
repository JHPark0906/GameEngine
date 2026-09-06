#include "ReleaseConsoleLogTests.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Diagnostics/Debug.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Diagnostics::LogEntry;
    using GameEngine::Diagnostics::LogLevel;

    [[nodiscard]] bool Holds(
        const std::vector<LogEntry>& logs, const LogLevel level, const std::string& text)
    {
        return std::ranges::any_of(
            logs,
            [level, &text](const LogEntry& entry)
            {
                return entry.level == level && entry.message.find(text) != std::string::npos;
            });
    }
}

bool RunReleaseConsoleLogTests()
{
    using GameEngine::Diagnostics::Debug;
    using GameEngine::Diagnostics::LogsMessagesByDefault;

    const bool enabledBefore = Debug::AreMessagesEnabled();

    // 기본값은 구성이 정한다. 이것이 지켜져야 출하된 게임이 프레임마다 문자열을 만들지 않는다 —
    // 편집기가 자기 것을 켜는 것과 이것은 별개의 사실이고, 둘 다 지켜져야 한다.
    const bool defaultFollowsTheBuild = enabledBefore == LogsMessagesByDefault;

    // 명시적으로 로그를 켜면 Debug와 Release 모두 일반 로그를 기록해야 한다.
    Debug::SetMessagesEnabled(true);
    Debug::ClearRecentLogs();
    Debug::Log("release console probe: a plain message");
    Debug::LogWarning("release console probe: a warning");
    Debug::LogError("release console probe: an error");

    const std::vector<LogEntry> enabled = Debug::GetRecentLogs();
    const bool keepsMessagesWhenOn =
        Holds(enabled, LogLevel::Message, "release console probe: a plain message");
    const bool keepsWarnings = Holds(enabled, LogLevel::Warning, "release console probe: a warning");
    const bool keepsErrors = Holds(enabled, LogLevel::Error, "release console probe: an error");

    // 개정 번호도 오른다. 콘솔은 그것이 바뀔 때만 스냅숏을 다시 받으므로, 메시지가 보관되고도
    // 번호가 그대로면 화면에는 나타나지 않는다.
    const std::uint64_t revisionBefore = Debug::GetLogRevision();
    Debug::Log("release console probe: one more");
    const bool revisionMoves = Debug::GetLogRevision() != revisionBefore;

    // 끄면 일반 메시지만 사라지고 경고와 오류는 남는다. 끄는 쪽도 지켜져야 한다 — 그러지
    // 않으면 플레이어가 이 전환으로 얻는 것이 없다.
    Debug::SetMessagesEnabled(false);
    Debug::ClearRecentLogs();
    Debug::Log("release console probe: while off");
    Debug::LogWarning("release console probe: warning while off");

    const std::vector<LogEntry> disabled = Debug::GetRecentLogs();
    const bool dropsMessagesWhenOff =
        !Holds(disabled, LogLevel::Message, "release console probe: while off");
    const bool keepsWarningsWhenOff =
        Holds(disabled, LogLevel::Warning, "release console probe: warning while off");

    // 이 프로세스를 다음 시험에게 넘기기 전에 원래대로 돌려 둔다.
    Debug::SetMessagesEnabled(enabledBefore);
    Debug::ClearRecentLogs();

    return Expect(
               defaultFollowsTheBuild,
               "whether messages are on by default should still be what the build says") &&
        Expect(
            keepsMessagesWhenOn,
            "with messages on, a plain message should reach the console's store in any build") &&
        Expect(keepsWarnings, "a warning should reach the console's store") &&
        Expect(keepsErrors, "an error should reach the console's store") &&
        Expect(revisionMoves, "storing a message should move the revision the console watches") &&
        Expect(dropsMessagesWhenOff, "with messages off, a plain message should be dropped") &&
        Expect(keepsWarningsWhenOff, "a warning should be kept even with messages off");
}

static const TestSupport::Registration gReleaseConsoleLogTests{
    "Diagnostics", "release console log tests should pass", RunReleaseConsoleLogTests };
