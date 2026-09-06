#pragma once

// editor-layer: 1 (Document)

#include <chrono>
#include <memory>

#include "Platform/IDirectoryWatcher.h"

namespace GameEditor
{

/// <summary>
/// 열린 프로젝트의 파일 변동을 듣고, <b>언제</b> 다시 읽을지를 답한다.
///
/// 답하는 것이 「언제」뿐이라는 것이 이 부품의 경계다. 다시 읽는 일 자체 — 다시 스캔하고,
/// 사이드카를 옮기고, 페이로드를 물려받고, 고아를 훑고, 리비전을 올리는 것 — 은 여기 없다.
/// 그 일은 정돈 부품과 런타임 데이터베이스와 문서의 리비전을 한자리에서 엮으므로, 여기로
/// 들이면 셋이 전부 따라 들어온다.
///
/// 리비전도 여기 없다. 그것을 올리는 곳은 넷인데(프로젝트 열기, 장면 올리기, 다시 읽기,
/// 플레이 이탈) 이 부품과 관계있는 것은 하나뿐이라, 여기로 옮기면 나머지 셋이 감시자를
/// 붙잡고 리비전을 올리게 된다. 리비전은 「이 프로젝트를 보는 것들이 다시 읽어야 하는가」라는
/// 문서 전체의 값이지 감시의 값이 아니다.
///
/// <b>시각을 인자로 받는다.</b> 조용 기간은 시간으로 정해지는 규칙이라, 지금 시각을 안에서
/// 읽으면 그 규칙을 재는 방법이 「실제로 기다리기」밖에 없다. 받으면 시험이 시계를 손으로
/// 밀 수 있고, 편집기는 언제나 진짜 시각을 준다.
/// </summary>
class EditorProjectWatch final
{
public:
    /// <summary>
    /// 프로젝트 디렉터리가 조용해진 뒤 기다리는 시간이다. 파일 복사 한 번은 알림 여럿을 내고,
    /// 그 중간의 절반 쓰인 파일을 스캔하면 임포트가 실패한다.
    /// </summary>
    static constexpr std::chrono::milliseconds QuietPeriod{ 300 };

    /// <summary>
    /// 이 디렉터리의 눈으로 갈아 끼운다. 프로젝트마다 새로 만든다 — 감시는 디렉터리 하나에
    /// 붙고, 이전 프로젝트의 눈은 여기서 놓인다. 기다리던 변동도 함께 잊는다.
    /// </summary>
    void Watch(std::unique_ptr<GameEngine::Platform::IDirectoryWatcher> watcher);

    /// <summary>
    /// 변동을 듣고, 조용해질 만큼 기다렸으면 참이다. 참을 돌려준 뒤에는 다음 변동이 올 때까지
    /// 다시 참이 되지 않는다 — 한 번의 변동은 한 번의 다시 읽기다.
    /// </summary>
    /// <param name="now">지금 시각이다. 편집기는 steady_clock의 지금을 준다.</param>
    [[nodiscard]] bool PollForQuietChange(std::chrono::steady_clock::time_point now);

private:
    /// <summary>열린 프로젝트 루트의 파일 변동을 지켜보는 눈이다. 프로젝트가 없으면 null이다.</summary>
    std::unique_ptr<GameEngine::Platform::IDirectoryWatcher> mWatcher;
    /// <summary>변동을 들었고 아직 다시 읽지 않았는지다.</summary>
    bool mChangePending = false;
    /// <summary>마지막 변동을 들은 시각이다. 여기서 조용한 시간을 잰다.</summary>
    std::chrono::steady_clock::time_point mLastChange{};
};

}
