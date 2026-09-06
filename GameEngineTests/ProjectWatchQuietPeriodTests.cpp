#include <chrono>
#include <iostream>
#include <memory>

#include "../GameEditor/Source/Document/EditorProjectWatch.h"

#include "ProjectWatchQuietPeriodTests.h"
#include "TestSupport.h"

using GameEditor::EditorProjectWatch;
using TestSupport::Expect;

namespace
{

/// <summary>변동을 냈다고 시험이 말해 주는 눈이다. 진짜 디렉터리는 보지 않는다.</summary>
class FakeDirectoryWatcher final : public GameEngine::Platform::IDirectoryWatcher
{
public:
    bool valid = true;
    /// <summary>다음 한 번의 폴링에서 변동을 보고할지다. 보고하면 스스로 내려간다.</summary>
    bool changed = false;
    int polls = 0;

    [[nodiscard]] bool IsValid() const override { return valid; }

    [[nodiscard]] bool PollChanges() override
    {
        ++polls;
        const bool reporting = changed;
        changed = false;
        return reporting;
    }
};

}

bool RunProjectWatchQuietPeriodTests()
{
    std::cout << "running project watch quiet period tests\n";

    using Clock = std::chrono::steady_clock;
    const Clock::time_point start{};
    const auto quiet = EditorProjectWatch::QuietPeriod;

    bool passed = true;

    // 눈이 없으면 아무 일도 없다. 프로젝트가 열리기 전의 상태다.
    {
        EditorProjectWatch watch;
        passed = Expect(
            !watch.PollForQuietChange(start),
            "a watch with no directory should never ask for a reread") && passed;
    }

    // ---- 조용 기간 전에는 다시 읽지 않는다, 지나면 한 번만 읽는다 ----
    {
        EditorProjectWatch watch;
        auto watcher = std::make_unique<FakeDirectoryWatcher>();
        FakeDirectoryWatcher& fake = *watcher;
        watch.Watch(std::move(watcher));

        passed = Expect(
            !watch.PollForQuietChange(start),
            "a quiet directory should not ask for a reread") && passed;

        fake.changed = true;
        passed = Expect(
            !watch.PollForQuietChange(start),
            "the moment a change arrives is too early to reread") && passed;
        passed = Expect(
            !watch.PollForQuietChange(start + quiet - std::chrono::milliseconds(1)),
            "one millisecond before the quiet period is still too early") && passed;

        passed = Expect(
            watch.PollForQuietChange(start + quiet),
            "once the directory has been quiet for the period, it should ask for a reread")
            && passed;
        passed = Expect(
            !watch.PollForQuietChange(start + quiet + std::chrono::seconds(10)),
            "one change should ask for one reread, however long the poll waits afterwards")
            && passed;
    }

    // ---- 그 사이에 또 바뀌면 시계가 다시 시작한다 ----
    //
    // 이것이 규칙의 요점이다. 파일 복사 한 번은 알림 여럿을 내므로, 첫 알림에서부터 세면 아직
    // 절반만 쓰인 파일을 스캔하게 된다.
    {
        EditorProjectWatch watch;
        auto watcher = std::make_unique<FakeDirectoryWatcher>();
        FakeDirectoryWatcher& fake = *watcher;
        watch.Watch(std::move(watcher));

        fake.changed = true;
        static_cast<void>(watch.PollForQuietChange(start));

        // 조용 기간이 거의 다 찼을 때 두 번째 변동이 온다.
        const Clock::time_point second = start + quiet - std::chrono::milliseconds(1);
        fake.changed = true;
        passed = Expect(
            !watch.PollForQuietChange(second),
            "a second change should not itself trigger a reread") && passed;

        // 첫 변동으로부터는 조용 기간이 지났지만, 두 번째 변동으로부터는 아직이다.
        passed = Expect(
            !watch.PollForQuietChange(start + quiet),
            "a later change should restart the clock, so the first one no longer counts")
            && passed;
        passed = Expect(
            watch.PollForQuietChange(second + quiet),
            "the reread should come a quiet period after the last change, not the first")
            && passed;
    }

    // 새 프로젝트의 눈으로 갈아 끼우면 기다리던 변동도 함께 잊는다. 이전 프로젝트에서 들은
    // 변동으로 새 프로젝트를 다시 읽을 이유가 없다.
    {
        EditorProjectWatch watch;
        auto first = std::make_unique<FakeDirectoryWatcher>();
        first->changed = true;
        watch.Watch(std::move(first));
        static_cast<void>(watch.PollForQuietChange(start));

        watch.Watch(std::make_unique<FakeDirectoryWatcher>());
        passed = Expect(
            !watch.PollForQuietChange(start + quiet + std::chrono::seconds(10)),
            "a change heard for the previous project should not reread the new one") && passed;
    }

    return passed;
}

static const TestSupport::Registration gProjectWatchQuietPeriodTests{
    "EditorDocument", "project watch quiet period tests should pass",
    RunProjectWatchQuietPeriodTests };
