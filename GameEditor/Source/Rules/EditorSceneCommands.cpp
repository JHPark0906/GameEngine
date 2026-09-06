#include "Rules/EditorSceneCommands.h"

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 표 자체다. 순서가 곧 툴바에 놓이는 순서이며, 만들기·이름 바꾸기·지우기 순으로 되돌릴 수
    /// 없는 것이 마지막에 온다.
    /// </summary>
    constexpr SceneCommandInfo Commands[] = {
        // 새 장면은 만든 뒤 그것을 열기 때문에 열려 있던 장면을 떠난다.
        { SceneCommand::New, "New Scene", true, false, false },
        // 이름 바꾸기는 파일을 옮기므로, 열려 있는 장면이 그 파일이면 편집 중인 것의 발밑이
        // 바뀐다. 그래서 이것도 먼저 묻는다.
        { SceneCommand::Rename, "Rename Scene", true, false, true },
        // 지우기는 되돌릴 수 없다. 저장 질문과는 별개로 한 번 더 묻는다.
        { SceneCommand::Delete, "Delete Scene", true, true, true },
    };
}

std::span<const SceneCommandInfo> SceneCommands()
{
    return Commands;
}

const SceneCommandInfo* FindSceneCommand(const SceneCommand command)
{
    for (const SceneCommandInfo& info : Commands)
    {
        if (info.command == command)
        {
            return &info;
        }
    }
    return nullptr;
}

}
