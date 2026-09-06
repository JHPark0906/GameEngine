#pragma once

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace GameEngine::Diagnostics
{

/// <summary>
/// 이 빌드가 일반 로그를 <b>기본값으로</b> 남기는지다. 경고와 오류는 언제나 남는다.
///
/// 일반 메시지는 출하된 게임의 문자열 처리 비용을 줄이기 위해 Debug에서만 기본으로 켜진다.
/// 편집기와 플레이어는 같은 엔진 정적 라이브러리를 공유하므로, 실제 활성 여부는 실행 중에
/// <see cref="Debug::SetMessagesEnabled"/>로 정한다.
/// </summary>
inline constexpr bool LogsMessagesByDefault =
#if defined(DEBUG) || defined(_DEBUG)
    true;
#else
    false;
#endif

enum class LogLevel
{
    Message,
    Warning,
    Error,
};

/// <summary>보관된 로그 한 줄이다. 에디터 콘솔 같은 곳이 최근 로그를 다시 그릴 때 읽는다.</summary>
struct LogEntry
{
    LogLevel level = LogLevel::Message;
    std::string message;
};

/// <summary>
/// 디버거와 표준 로그 스트림에 진단 메시지를 기록하는 정적 로거이다.
/// </summary>
class Debug
{
public:
    using LogListenerId = std::uint64_t;
    using LogListener = std::function<void(const LogEntry&)>;

    /// <summary>
    /// 일반 메시지를 기록한다. 일반 메시지 출력이 꺼져 있으면 아무 일도 하지 않는다.
    /// </summary>
    /// <param name="message">기록할 문자열이다.</param>
    static void Log(const char* message);

    /// <summary>경고 메시지를 기록한다.</summary>
    /// <param name="message">기록할 문자열이다.</param>
    static void LogWarning(const char* message);

    /// <summary>오류 메시지를 기록한다.</summary>
    /// <param name="message">기록할 문자열이다.</param>
    static void LogError(const char* message);

    /// <summary>새 로그가 기록될 때 호출할 수신기를 등록한다.</summary>
    /// <returns>수신기를 해제할 때 사용할 식별자이다.</returns>
    // 콜백은 로그를 쓴 스레드에서 잠금 없이 실행된다. UI 등 다른 스레드 소유 상태는 직접 수정하지 않는다.
    [[nodiscard]] static LogListenerId AddLogListener(LogListener listener);

    /// <summary>이전에 등록한 로그 수신기를 해제한다.</summary>
    // 이미 복사된 호출 목록은 남을 수 있으므로, 해제가 진행 중인 콜백의 완료까지 기다리지는 않는다.
    static void RemoveLogListener(LogListenerId listenerId);

    /// <summary>에디터 콘솔과 진단 화면에서 사용할 최근 로그 스냅샷을 반환한다.</summary>
    [[nodiscard]] static std::vector<LogEntry> GetRecentLogs();

    /// <summary>
    /// 최근 로그가 바뀔 때마다 오르는 번호다. 매 프레임 로그를 그리는 콘솔은 이것이 바뀌었을
    /// 때만 스냅샷을 다시 받으면 되므로, 프레임마다 목록을 복사하지 않는다.
    /// </summary>
    [[nodiscard]] static std::uint64_t GetLogRevision();

    /// <summary>보관 중인 최근 로그를 모두 제거한다.</summary>
    static void ClearRecentLogs();

    /// <summary>
    /// 일반 메시지를 남길지 정한다. 기본값은
    /// <see cref="LogsMessagesByDefault"/>이다.
    ///
    /// 편집기가 시작하면서 켜는 자리다. 엔진이 정적 라이브러리 하나라 편집기와
    /// 플레이어가 같은 바이너리를 공유하므로, 「누가 쓰는가」는 컴파일 시간에 답할 수
    /// 없고 실행 중에만 답할 수 있다.
    /// </summary>
    static void SetMessagesEnabled(bool enabled);

    /// <summary>지금 일반 메시지를 남기는지다.</summary>
    [[nodiscard]] static bool AreMessagesEnabled();

    /// <summary>
    /// 여러 값을 연결해 일반 메시지로 기록한다. 메시지가 꺼져 있으면 아무 일도 하지 않는다.
    ///
    /// 메시지가 꺼져 있으면 문자열 연결을 건너뛴다. 호출부의 인자 식은 이 검사 전에 평가된다.
    /// </summary>
    /// <typeparam name="Ts">출력 스트림에 삽입할 수 있는 값들의 형식이다.</typeparam>
    /// <param name="args">순서대로 연결할 값들이다.</param>
    template <typename... Ts>
    static void Log(Ts&&... args)
    {
        if (AreMessagesEnabled())
        {
            LogImpl(LogLevel::Message, std::forward<Ts>(args)...);
        }
    }

    /// <summary>여러 값을 연결해 경고 메시지로 기록한다.</summary>
    /// <typeparam name="Ts">출력 스트림에 삽입할 수 있는 값들의 형식이다.</typeparam>
    /// <param name="args">순서대로 연결할 값들이다.</param>
    template <typename... Ts>
    static void LogWarning(Ts&&... args)
    {
        LogImpl(LogLevel::Warning, std::forward<Ts>(args)...);
    }

    /// <summary>여러 값을 연결해 오류 메시지로 기록한다.</summary>
    /// <typeparam name="Ts">출력 스트림에 삽입할 수 있는 값들의 형식이다.</typeparam>
    /// <param name="args">순서대로 연결할 값들이다.</param>
    template <typename... Ts>
    static void LogError(Ts&&... args)
    {
        LogImpl(LogLevel::Error, std::forward<Ts>(args)...);
    }

private:
    template <typename... Ts>
    static void LogImpl(const LogLevel level, Ts&&... args)
    {
        std::ostringstream stream;
        (stream << ... << std::forward<Ts>(args));

        Write(level, stream.str());
    }

    static void Write(LogLevel level, const std::string& message);

public:
    /// <summary>레벨의 표시 이름이다. 로그를 직접 형식화하는 리스너 — 디버거 미러 — 가 쓴다.</summary>
    [[nodiscard]] static const char* GetLogLevelName(LogLevel level);
};

}
