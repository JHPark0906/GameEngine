#pragma once

namespace GameEngine::Platform
{

/// <summary>
/// 디렉터리 하나와 그 아래 전체에서 파일이 생기고, 바뀌고, 이름이 바뀌고, 지워지는 것을 지켜보는
/// 눈이다. 무엇이 바뀌었는지는 말하지 않고 "바뀐 것이 있었다"만 답한다 — 듣는 쪽이 어차피
/// 전체를 다시 훑기 때문이고, 그래서 이 계층이 파일 이름 인코딩이나 이벤트 병합 규칙을 알
/// 필요가 없다.
///
/// 파일 감시는 창처럼 플랫폼 설비라서 인터페이스로 건넌다: 에디터가 프로젝트 디렉터리의 변동에
/// 따라 에셋 데이터베이스를 다시 읽되 어느 운영체제의 알림 API인지 몰라야 한다.
/// </summary>
class IDirectoryWatcher
{
public:
    virtual ~IDirectoryWatcher() = default;

    IDirectoryWatcher(const IDirectoryWatcher&) = delete;
    IDirectoryWatcher& operator=(const IDirectoryWatcher&) = delete;
    IDirectoryWatcher(IDirectoryWatcher&&) = delete;
    IDirectoryWatcher& operator=(IDirectoryWatcher&&) = delete;

    /// <summary>감시가 서 있는지다. 디렉터리를 열지 못했거나 감시가 끊어졌으면 거짓이다.</summary>
    [[nodiscard]] virtual bool IsValid() const = 0;

    /// <summary>
    /// 지난 호출 이후 변동이 있었는지 묻고 그 기록을 비운다. 기다리지 않는다 — 프레임마다 한 번
    /// 부르는 자리다. 알림이 넘쳐 무엇이 바뀌었는지 잃었을 때도 참이다: 모르는 것은 "바뀌었다"로
    /// 답하는 편이 안전하다.
    /// </summary>
    /// <returns>변동이 기록되어 있었으면 true다.</returns>
    [[nodiscard]] virtual bool PollChanges() = 0;

protected:
    IDirectoryWatcher() = default;
};

}
