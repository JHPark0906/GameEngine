#include "SharedFileWriteTests.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "Platform/TextFile.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;

namespace
{
    /// <summary>
    /// 한 번의 쓰기가 충분히 오래 걸리도록 크게 잡는다. 재는 것이 경합인 이상 이 크기가 곧
    /// 「읽는 쪽이 그 순간을 만날 여지」이고, 스키마만 한 크기로는 그 순간이 너무 짧다.
    /// </summary>
    [[nodiscard]] std::string Contents(const char fill)
    {
        return std::string(256 * 1024, fill);
    }

    /// <summary>
    /// 읽기 실패와 불완전한 내용 읽기를 구분해 기록한다.
    /// 파일 열기 실패 횟수는 동시 작업과 스케줄링에 따라 달라지므로 구현 사이의 우열을 단언하지 않는다.
    /// 제자리 쓰기에서 불완전한 읽기를 재현하고 원자적 교체에서는 그러한 읽기가 없는지 검사한다.
    /// </summary>
    struct ReadOutcomes
    {
        int couldNotOpen = 0;
        int openedButTorn = 0;
        int whole = 0;
    };

    /// <summary>
    /// 한 쪽이 파일을 계속 다시 쓰는 동안 다른 쪽이 계속 읽는다.
    ///
    /// <paramref name="stopWhenTornIsSeen"/>이면 쓰는 쪽은 <b>읽는 쪽이 찢어진 것을 볼 때까지</b>
    /// 돈다(상한 있음). 횟수를 고정하면 「그 사이에 한 번도 못 마주쳤다」가 실패로 보이는데,
    /// 그것은 고쳐야 할 결함이 아니라 그날 그 순간에 못 만났다는 뜻이다.
    /// </summary>
    [[nodiscard]] ReadOutcomes ReadsWhileRewriting(
        const std::filesystem::path& path,
        GameEngine::Platform::FileWriteResult (*write)(const std::filesystem::path&, std::string_view),
        const bool stopWhenTornIsSeen)
    {
        const std::string first = Contents('a');
        const std::string second = Contents('b');
        static_cast<void>(write(path, first));

        // 읽는 쪽이 몇 번 읽었는지가 이 시험의 눈금이다. 쓰는 횟수를 눈금으로 삼으면 기계가
        // 빠른 날에는 읽기 몇 번 만에 쓰기가 끝나 「찾아봤다」가 거의 아무것도 아니게 된다.
        constexpr int DesiredReads = 300;
        constexpr int MaximumRounds = 20000;
        std::atomic<bool> enough{ false };
        std::atomic<bool> writing{ true };
        std::thread writer([&]
        {
            for (int round = 0; round < MaximumRounds && !enough; ++round)
            {
                static_cast<void>(write(path, round % 2 == 0 ? second : first));
            }
            writing = false;
        });

        ReadOutcomes outcomes;
        int reads = 0;
        while (writing)
        {
            const std::optional<std::string> read = GameEngine::Platform::ReadTextFile(path);
            ++reads;
            if (!read)
            {
                ++outcomes.couldNotOpen;
            }
            else if (*read == first || *read == second)
            {
                ++outcomes.whole;
            }
            else
            {
                ++outcomes.openedButTorn;
            }
            if ((stopWhenTornIsSeen && outcomes.openedButTorn > 0) || reads >= DesiredReads)
            {
                enough = true;
            }
        }
        writer.join();
        return outcomes;
    }

    void Report(const char* const what, const ReadOutcomes& outcomes)
    {
        std::cout << "  " << what << ": whole=" << outcomes.whole
                  << ", opened but torn=" << outcomes.openedButTorn
                  << ", could not open=" << outcomes.couldNotOpen << '\n';
    }
}

bool RunSharedFileWriteTests()
{
    namespace Platform = GameEngine::Platform;

    TemporaryDirectory temporaryDirectory("shared-file-write");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    // 파일을 제자리에서 잘라 쓰는 동안 읽으면 잘린 내용을 가져올 수 있다.
    // 이 대조군은 잘린 읽기를 하나라도 발견하면 멈춘다. 발생 횟수 대신 가능 여부를 검사한다.
    const ReadOutcomes inPlace =
        ReadsWhileRewriting(root / "in-place.json", &Platform::WriteTextFile, true);
    Report("rewritten in place", inPlace);

    // 임시 파일에 쓰고 이름을 바꾸면 그 순간이 없다. 옛 내용 전체이거나 새 내용 전체다.
    // 이쪽은 끝까지 돌린다 — 없다는 것을 보이려면 찾을 수 있는 만큼 찾아봐야 한다.
    const ReadOutcomes atomic =
        ReadsWhileRewriting(root / "atomic.json", &Platform::WriteTextFileAtomically, false);
    Report("replaced by rename", atomic);

    return Expect(
            inPlace.openedButTorn > 0,
            "rewriting a file in place should hand a reader a file that opened but is not whole") &&
        Expect(
            atomic.openedButTorn == 0,
            "replacing a file by rename should never hand a reader half of either version");
}

static const TestSupport::Registration gSharedFileWriteTests{
    "ContentSource", "shared file write tests should pass", RunSharedFileWriteTests };
