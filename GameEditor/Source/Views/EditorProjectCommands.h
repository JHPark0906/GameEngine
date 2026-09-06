#pragma once

// editor-layer: 2 (Views)

#include <filesystem>

#include "Rules/EditorFileDialogs.h"

namespace GameEngine::App
{
struct ProjectFileData;
}

namespace GameEditor
{

class EditorContext;
class ConfirmationQueue;

// ---- 사람에게 경로를 묻는 명령들
//
// 이 다섯 명령은 툴바 상태를 쓰지 않고 문맥, 대화상자, 확인 대기열만 사용한다.
// 자유 함수와 주입 가능한 의존성으로 구성해 툴바와 셸 없이 시험할 수 있다.
//
// 대화상자를 인자로 받는 것도 같은 이유다. 정적 함수를 직접 부르면 사람이 창을 조작해야만
// 그 뒤로 넘어갈 수 있고, 편집기를 띄우는 것은 순번을 받아야 하는 희소 자원이다.

/// <summary>새 프로젝트 파일을 만들고 그것을 연다. 자리는 사람이 고른다.</summary>
void CreateProjectWithDialog(EditorContext& context, IFileDialogs& dialogs);

/// <summary>
/// 프로젝트 파일을 골라 연다. 열고 나서 사고가 남긴 사본이 있으면 묻는다.
/// </summary>
/// <param name="recoveryDirectory">
/// 사본이 사는 자리다. 편집기는 <c>GetRecoveryDirectory()</c>를 주고, 시험은 자기 프로세스의
/// 임시 자리를 준다 — 시험이 사용자의 진짜 사본을 훑지 않게 하는 것이 이 인자의 전부다.
/// </param>
void OpenProjectWithDialog(
    EditorContext& context, IFileDialogs& dialogs, ConfirmationQueue& confirmations,
    const std::filesystem::path& recoveryDirectory);

/// <summary>
/// 열린 프로젝트에 컴포넌트 하나를 만든다. 이름은 파일 대화상자로 받는다 — 이름을 묻는
/// 설비가 그것뿐이고, 어디에 놓이는지도 함께 보인다.
/// </summary>
void CreateScriptWithDialog(EditorContext& context, IFileDialogs& dialogs);

/// <summary>
/// 열린 프로젝트에 최소 머티리얼 파일 하나를 만든다. 자리는 파일 대화상자로 받는다.
///
/// 만드는 것만으로 인스펙터의 선택 목록에 나오지는 않는다 — <c>CreateScriptWithDialog</c>가
/// 컴파일을 기다리는 것과 달리, 다음 <c>AssetDatabase::Refresh</c>를 기다린다. 프로젝트를
/// 다시 여는 것도, 목록에 등록하는 것도 필요 없다.
/// </summary>
void CreateMaterialWithDialog(EditorContext& context, IFileDialogs& dialogs);

/// <summary>
/// 새 장면 파일을 만들어 프로젝트에 등록하고 연다.
///
/// 파일을 만드는 것만으로는 부족하다: 계층이 읽는 목록은 디렉터리가 아니라
/// .gameproject의 scenes 배열이라, 등록하지 않은 장면은 만들어도 나타나지 않는다.
/// </summary>
void CreateSceneWithDialog(EditorContext& context, IFileDialogs& dialogs);

/// <summary>고른 장면의 파일과 등록을 함께 다른 이름으로 옮긴다.</summary>
void RenameSceneWithDialog(EditorContext& context, IFileDialogs& dialogs);

/// <summary>
/// 프로젝트를 다시 열고 그 안의 장면 하나를 연다. 목록이 바뀐 뒤에는 다시 열어야 계층이
/// 새 설정을 쥔다 — 계층이 읽는 것이 디렉터리가 아니라 그 설정이기 때문이다.
/// </summary>
void ReopenProjectAtScene(
    EditorContext& context, const GameEngine::App::ProjectFileData& project,
    const std::filesystem::path& scenePath);

}
