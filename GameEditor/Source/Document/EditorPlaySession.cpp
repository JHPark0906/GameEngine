#include "Document/EditorPlaySession.h"

#include <utility>

namespace GameEditor
{

bool EditorPlaySession::Begin(std::string sceneSnapshot)
{
    if (mIsPlaying)
    {
        return false;
    }
    mSnapshot = std::move(sceneSnapshot);
    mIsPlaying = true;
    return true;
}

std::string EditorPlaySession::End()
{
    mIsPlaying = false;
    std::string snapshot = std::move(mSnapshot);
    // move가 남긴 상태는 정해져 있지 않으므로 비어 있음을 직접 세운다. 이 뒤로 GetSnapshot이
    // 무엇을 돌려주는지가 사고 사본의 내용이 되기 때문에 짐작으로 둘 자리가 아니다.
    mSnapshot.clear();
    return snapshot;
}

void EditorPlaySession::Clear()
{
    mIsPlaying = false;
    mSnapshot.clear();
}

}
