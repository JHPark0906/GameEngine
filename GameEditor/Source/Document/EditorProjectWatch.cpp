#include "Document/EditorProjectWatch.h"

#include <utility>

namespace GameEditor
{

void EditorProjectWatch::Watch(
    std::unique_ptr<GameEngine::Platform::IDirectoryWatcher> watcher)
{
    mWatcher = std::move(watcher);
    mChangePending = false;
}

bool EditorProjectWatch::PollForQuietChange(const std::chrono::steady_clock::time_point now)
{
    if (!mWatcher || !mWatcher->IsValid())
    {
        return false;
    }
    if (mWatcher->PollChanges())
    {
        mChangePending = true;
        // 변동이 또 오면 시계가 다시 시작한다. 복사 한 번이 알림 여럿을 내므로, 마지막
        // 알림에서부터 조용한 시간을 세야 절반 쓰인 파일을 스캔하지 않는다.
        mLastChange = now;
    }
    if (!mChangePending || now - mLastChange < QuietPeriod)
    {
        return false;
    }
    mChangePending = false;
    return true;
}

}
