#pragma once

// editor-layer: 0 (Rules)

#include <span>
#include <string_view>

namespace GameEditor
{

/// <summary>
/// 장면 파일을 다루는 에디터 명령이다.
///
/// 세 명령은 파일과 등록을 함께 다룬다. 계층이 읽는 목록은 디렉터리가 아니라
/// <c>.gameproject</c>의 scenes 배열이므로, 파일만 만들거나 지우면 목록에 반영되지 않는다.
/// </summary>
enum class SceneCommand
{
    New,
    Rename,
    Delete,
};

/// <summary>
/// 명령 하나가 화면과 계약에 대해 알려 주는 것이다.
///
/// 표로 만든 이유는 이 목록이 <b>사라지면 눈에 띄어야</b> 하기 때문이다. 명령이 버튼 안에만
/// 있으면, 그 버튼이 든 파일을 지우는 병합이 기능을 조용히 지운다 — 컴파일도 시험도 통과한다.
/// 초록불은 사라진 파일 안에 있던 것에 대해 아무 말도 하지 않는다. 표가 있으면 그것을 보는
/// 시험이 붉어진다.
/// </summary>
struct SceneCommandInfo
{
    SceneCommand command = SceneCommand::New;
    /// <summary>버튼에 적히는 글자다.</summary>
    std::string_view label;
    /// <summary>
    /// 열려 있는 장면을 떠나므로, 저장되지 않은 편집이 있으면 먼저 물어야 하는가.
    /// </summary>
    bool leavesTheOpenScene = false;
    /// <summary>
    /// 되돌릴 수 없어서 "정말 할까"를 따로 물어야 하는가. 저장 질문과는 다른 질문이다 —
    /// 저것은 "저장할까"이고 이것은 "지울까"다.
    /// </summary>
    bool needsConfirmation = false;
    /// <summary>계층에서 장면 하나가 골라져 있어야 하는가.</summary>
    bool needsSelectedScene = false;
};

/// <summary>
/// 에디터가 내놓는 장면 명령 전부다. 툴바가 이 표로 버튼을 세우고, 시험이 이 표를 본다.
/// </summary>
[[nodiscard]] std::span<const SceneCommandInfo> SceneCommands();

/// <summary>이 명령의 설명이다. 목록에 없으면 nullptr이다.</summary>
[[nodiscard]] const SceneCommandInfo* FindSceneCommand(SceneCommand command);

}
