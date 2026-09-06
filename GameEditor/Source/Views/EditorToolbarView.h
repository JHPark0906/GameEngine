#pragma once

// editor-layer: 2 (Views)

#include <optional>
#include <filesystem>
#include <string>
#include <vector>

#include "UI/UIContext.h"

#include "Rules/EditorConfirmation.h"
#include "Rules/EditorMenuModel.h"
#include "Views/EditorProjectCommands.h"
#include "Rules/EditorPanelCommon.h"
#include "Rules/EditorPanelHosts.h"
#include "Rules/EditorProjectBuild.h"
#include "Rules/EditorToolbarLayout.h"

namespace GameEngine::Runtime
{
class Button;
class GameObject;
class RectTransform;
class Scene;
class TextRenderer;
}

namespace GameEngine::App
{
struct ProjectFileData;
}

namespace GameEditor
{

class EditorContext;

/// <summary>확인 줄의 글자 크기다. 툴바의 다른 글자와 같다.</summary>
inline constexpr float ToolbarLabelFontSize = RowFontSize;

/// <summary>
/// 상단 툴바다. 에디터의 런타임 장면에 놓인 UI 컴포넌트로 구성된다.
///
/// 오브젝트는 <b>에디터 프로세스 자신의 런타임</b>에 산다. 편집 대상 프로젝트의 런타임이 아니다:
/// 계층 패널이 열거하는 것은 후자뿐이라, 이 구분만으로 "이 오브젝트는 편집 대상이 아니다"라는
/// 표시가 필요 없어진다. 표시를 만들었다면 직렬화·인스펙터·복제가 전부 그것을 알아야 했을
/// 것이다.
///
/// 오브젝트는 에셋이 아니라 코드로 세운다. 장면 파일로 저작하면 사용자가 그 파일을 열어 에디터
/// 자신의 UI를 편집할 수 있게 되는데, 지금 열어 둘 이유가 없는 문이다.
/// </summary>
class EditorToolbarView final
{
public:
    EditorToolbarView(
        EditorContext& context, IPropertyEditHost& propertyEdit, ConfirmationQueue& confirmations,
        IFileDialogs& fileDialogs);

    /// <summary>
    /// 툴바를 이 장면에 세운다. 한 번만 부른다 — 버튼 집합이 고정이라 다시 세울 일이 없다.
    /// </summary>
    /// <param name="scene">에디터 프로세스 자신의 런타임 장면이다.</param>
    void Build(GameEngine::Runtime::Scene& scene);

    /// <summary>
    /// 프레임마다 상태를 맞추고 눌린 버튼의 동작을 수행한다.
    /// 레이블, 활성 여부, 보이는 그룹을 여기서 정한다.
    /// </summary>
    /// <param name="contentScale">창의 콘텐츠 배율이다. 캔버스의 픽셀 배율이 된다.</param>
    void Synchronize(float contentScale);

    /// <summary>
    /// 툴바가 지금 차지하는 높이다. 논리 픽셀이며 배율이 곱해지기 전이다.
    ///
    /// 상수가 아니라 물음인 이유는 접힘 때문이다: 버튼이 한 줄에 다 들지 않으면 띠가
    /// 두 줄이 되고, 도킹이 비워 두어야 할 높이도 그만큼 달라진다. 도킹이 자기 상수를
    /// 들고 있으면 그 순간 패널이 둘째 줄 위에 올라앉는다.
    /// </summary>
    [[nodiscard]] float GetToolbarHeight() const;

    /// <summary>지금 프로젝트 빌드가 돌고 있는지다. 메뉴가 Build와 Cancel Build를 가를 때 쓴다.</summary>
    [[nodiscard]] bool IsBuildRunning() const { return mProjectBuild.IsRunning(); }

    /// <summary>
    /// 툴바 위에 비워 둘 높이다. 메뉴 막대가 그 자리를 쓴다. 논리 픽셀이다.
    ///
    /// 툴바가 자기 위에 무엇이 있는지 아는 대신 <b>얼마나 비워야 하는지</b>만 받는 이유는,
    /// 아는 순간 그 무엇이 없는 날 — 시험이 툴바만 세우는 날이 그렇다 — 툴바가 서지 못하기
    /// 때문이다.
    /// </summary>
    void SetTopInset(const float logicalInset) { mTopInset = logicalInset; }

    /// <summary>
    /// 메뉴에서 고른 명령을 수행한다.
    ///
    /// 메뉴 항목과 툴바 버튼은 같은 동작 함수를 호출한다.
    /// 동작 구현이 두 벌이 되면 같은 이름의 명령이 서로 다르게 동작할 수 있다.
    ///
    /// 이 switch에는 <c>default:</c>가 없고, 빠진 항목의 경고가 그 자리에서만 <b>오류로</b>
    /// 올라가 있다. 표에 명령을 더하고 여기 붙이는 것을 잊으면 빌드가 서며, 그것이 「눌러도
    /// 아무 일이 없는 항목」을 사람보다 먼저 잡는 유일한 자리다 — 화면에서 그것은 고장과
    /// 구별되지 않는다.
    /// </summary>
    void RunMenuCommand(EditorMenuCommand command);

private:
    /// <summary>버튼 하나가 쥐는 조각들이다. 눌림은 Button이, 글자는 TextRenderer가 답한다.</summary>
    struct ToolbarButton
    {
        GameEngine::Runtime::GameObject* object = nullptr;
        GameEngine::Runtime::RectTransform* rect = nullptr;
        GameEngine::Runtime::Button* button = nullptr;
        GameEngine::Runtime::TextRenderer* label = nullptr;
        /// <summary>글자가 사는 자식 사각형이다. 버튼 사각형에서 왼쪽으로만 들여져 있다.</summary>
        GameEngine::Runtime::RectTransform* labelRect = nullptr;
    };

    /// <summary>이 오브젝트 아래에 사각형 하나를 만든다. 배경도 글자도 없는 자리다.</summary>
    [[nodiscard]] GameEngine::Runtime::GameObject* AddRect(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
        const std::string& name, const GameEngine::Math::Vector2& anchorMin,
        const GameEngine::Math::Vector2& anchorMax, const GameEngine::Math::Vector2& offsetMin,
        const GameEngine::Math::Vector2& offsetMax);

    /// <summary>
    /// 오른쪽 끝에 붙는 버튼 하나를 만든다. 확인 줄이 이 배치를 쓰는 이유는 질문이 남는 폭을
    /// 전부 갖게 하기 위해서다 — 창이 좁아도 잘리는 것이 질문이어서는 안 된다.
    /// </summary>
    [[nodiscard]] ToolbarButton AddRightButton(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
        const std::string& name, float right, float width);

    /// <summary>왼쪽 끝에서 오른쪽 앵커까지 늘어나는 글자다. 창을 넓히면 함께 넓어진다.</summary>
    [[nodiscard]] GameEngine::Runtime::TextRenderer* AddStretchedLabel(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
        const std::string& name, float left, float right);

    /// <summary>
    /// 버튼 하나를 만든다. 폭도 자리도 여기서 정하지 않는다 — 둘 다 글자를 재고 나서
    /// 정해지므로 <see cref="LayoutToolbarRow"/>가 매 프레임 답한다.
    /// </summary>
    [[nodiscard]] ToolbarButton AddButton(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
        const std::string& name);

    /// <summary>
    /// 이번 프레임의 버튼 자리를 정한다. 각 버튼이 요구한 폭을 모아 접힘 계산에 넘기고,
    /// 나온 사각형을 오프셋으로 적는다.
    /// </summary>
    void LayoutToolbarRow(float contentScale);

    /// <summary>오른쪽 남는 자리를 차지하는 글자 하나를 만든다. 제목 표시줄이 이것이다.</summary>
    [[nodiscard]] GameEngine::Runtime::TextRenderer* AddLabel(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
        const std::string& name, float x, float width, bool centered);

    /// <summary>확인을 기다리며 붙잡아 둔, 장면을 떠나는 동작이다.</summary>
    enum class PendingAction
    {
        None,
        NewProject,
        OpenProject,
        NewScene,
        RenameScene,
        DeleteScene,
    };

    /// <summary>붙잡아 둔 동작을 실제로 수행한다. 확인 줄의 두 진행 버튼이 부른다.</summary>
    void RunPendingAction();

    /// <summary>
    /// 열린 프로젝트를 빌드한다. 이미 돌고 있으면 그만두게 한다 — 버튼 하나가 두 가지 뜻을
    /// 갖는 것은 그 사이에 다른 버튼이 끼어들 자리가 없기 때문이다.
    /// </summary>
    void ToggleProjectBuild();

    /// <summary>고른 장면을 지울지 묻는다. 실제 삭제는 확인 줄이 한다.</summary>
    void AskToDeleteSelectedScene();
    /// <summary>확인을 받은 뒤의 실제 삭제다. 되돌릴 수 없다.</summary>
    void DeleteConfirmedScene();

    /// <summary>
    /// 글꼴 크기는 논리 단위로, 줄바꿈 폭은 배치된 사각형의 폭으로 설정한다.
    /// 정렬이 각 라벨의 사각형 안에서 이루어지도록 폭을 전달한다.
    /// </summary>
    void SynchronizeTextMetrics(float contentScale);

    /// <summary>평상시 버튼들의 이번 프레임 상태와 클릭을 처리한다.</summary>
    void SynchronizeButtons();

    /// <summary>
    /// 장면을 떠나는 동작을 붙잡아 두고, 저장되지 않은 편집을 어떻게 할지 묻는다.
    /// </summary>
    /// <param name="action">답을 받은 뒤 수행할 동작이다.</param>
    void AskBeforeLeavingScene(PendingAction action);

    /// <summary>
    /// 저장되지 않은 편집을 두고 물어본다. 저장이 실패하면 실패를 한 줄 붙여 다시 묻는다 —
    /// 물음이 사라지면 사람은 진행된 줄 알기 때문이다.
    /// </summary>
    /// <param name="failure">붙일 실패 문구다. 비어 있으면 처음 묻는 것이다.</param>
    void AskAboutUnsavedChanges(const std::string& failure);

    /// <summary>고른 장면을 지울지 묻는다. 실제 삭제는 답이 온 뒤다.</summary>
    /// <param name="sceneId">지울 장면의 프로젝트 안 id다.</param>
    void AskAboutDeletingScene(unsigned int sceneId);

    /// <summary>
    /// 이관 계획이 있으면 한 번 묻는다. 띄울지는 이 뷰가 정하지 않는다 — EditorContext가
    /// 프로젝트를 열며 세운 계획이 있으면 있는 것이고, 뷰는 그것을 물음으로 옮길 뿐이다.
    /// </summary>
    void AskAboutMigrationIfNeeded();

    /// <summary>
    /// 정리 계획이 있으면 한 번 묻는다. 계획이 이번에 묻는 한 단계만 보이고, 진행 버튼의
    /// 글자도 그 단계의 것이다 — 지우는 것과 치우는 것이 같은 말을 해서는 안 된다.
    /// </summary>
    void AskAboutCleanupIfNeeded();

    EditorContext& mContext;
    IPropertyEditHost& mPropertyEdit;
    /// <summary>사람에게 묻는 것들이 서는 줄이다. 툴바는 여기에 올리기만 한다.</summary>
    ConfirmationQueue& mConfirmations;
    /// <summary>사람에게 경로를 묻는 길이다. 편집기는 플랫폼 구현을, 시험은 가짜를 준다.</summary>
    IFileDialogs& mFileDialogs;
    /// <summary>이관 계획을 이미 물었는지다. 계획 하나에 한 번만 묻는다.</summary>
    bool mAskedAboutMigration = false;
    /// <summary>정리 계획을 이미 물었는지다. 단계가 바뀌면 다시 묻는다.</summary>
    bool mAskedAboutCleanup = false;

    GameEngine::Runtime::GameObject* mCanvasObject = nullptr;
    /// <summary>띠 자신의 사각형이다. 접히면 줄 수만큼 높아진다.</summary>
    GameEngine::Runtime::RectTransform* mStripRect = nullptr;
    GameEngine::Runtime::GameObject* mButtonGroup = nullptr;
    ToolbarButton mUndo;
    ToolbarButton mRedo;
    ToolbarButton mSnap;
    ToolbarButton mPlay;
    GameEngine::Runtime::TextRenderer* mTitle = nullptr;
    GameEngine::Runtime::RectTransform* mTitleRect = nullptr;

    /// <summary>지우기 확인을 기다리는 중이면 그 장면의 id다.</summary>
    std::optional<unsigned int> mSceneAwaitingDelete;

    /// <summary>배치가 읽는 버튼들이다. 선언 순서가 곧 화면의 순서다.</summary>
    std::vector<ToolbarButton*> mRowButtons;

    /// <summary>
    /// 이번 프레임의 띠 높이다. 접히면 줄 수만큼 자란다. 셸이 이 값을 받아 도킹에 넘기므로,
    /// 패널들이 비워 두는 높이가 곧 이 값이다.
    /// </summary>
    float mRowHeight = ToolbarLayoutMetrics{}.rowHeight;

    /// <summary>툴바 위에 비워 둔 높이다. 메뉴 막대가 그 자리에 선다. 논리 픽셀이다.</summary>
    float mTopInset = 0.0f;

    /// <summary>확인 줄이 떠 있는 동안의 동작이다. 취소하면 None으로 돌아간다.</summary>
    PendingAction mPendingAction = PendingAction::None;

    /// <summary>
    /// 돌고 있는 빌드다. 툴바가 쥐는 이유는 이 버튼 하나가 그것을 시작하고 그만두게 하는
    /// 전부이기 때문이다 — 다른 패널이 볼 일이 생기면 그때 컨텍스트로 옮긴다.
    /// </summary>
    ProjectBuild mProjectBuild;
};

}
