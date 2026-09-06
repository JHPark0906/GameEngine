#pragma once

// editor-layer: 2 (Views)

#include <string>
#include <utility>
#include <vector>

#include "Rules/DragGesture.h"
#include "UI/UIContext.h"
#include "Rules/EditorConfirmation.h"
#include "Rules/EditorPanelHosts.h"

namespace GameEngine::Runtime
{
class GameObject;
class Scene;
}

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 계층 패널이다: 프로젝트의 장면 파일들과 열린 장면의 객체 트리를 한 목록으로 보인다. 클릭이
/// 선택, 장면 파일 행의 더블클릭이 장면 열기, 행 드래그 앤 드롭이 재부모화이고, 목록 위의
/// Add/Delete 버튼이 객체 생성/삭제다. 드래그 상태는 이 패널이 소유한다.
/// </summary>
class EditorHierarchyPanel final
{
public:
    EditorHierarchyPanel(
        IEditorScale& scale, IPropertyEditHost& propertyEdit, EditorContext& context, GameEngine::UI::UIContext& ui,
        ConfirmationQueue& confirmations);

    /// <summary>패널 내용을 그린다. 제목줄 프레임은 셸이 이미 그렸고, 그 아래 영역을 받는다.</summary>
    void Draw(GameEngine::UI::UIRect content);

    /// <summary>
    /// 저장되지 않은 편집을 두고 다른 장면을 열지 묻는다. 실패하면 물음을 닫지 않고 실패를
    /// 한 줄 붙여 다시 묻는다 — 물음이 사라지는 것을 사람은 "됐다"로 읽는다.
    /// </summary>
    /// <param name="sceneId">열려는 장면의 프로젝트 안 id다.</param>
    /// <param name="question">물을 글이다. 다시 물을 때는 실패가 붙어 있다.</param>
    void AskAboutSwitchingScene(unsigned int sceneId, const std::string& question);

private:
    /// <summary>계층 목록의 행 하나이다. 장면 파일 행이거나 런타임 객체 행이다.</summary>
    struct HierarchyRow
    {
        std::string label;
        float indent = 0.0f;
        /// <summary>더블클릭으로 열 프로젝트 장면 id이다. 장면 파일 행이 아니면 무의미하다.</summary>
        unsigned int sceneId = 0;
        /// <summary>클릭으로 선택할 인스턴스 id이다. 객체 행이 아니면 0이다.</summary>
        unsigned int instanceId = 0;
        /// <summary>런타임 장면의 제목 행이면 그 장면이다. 여기에 떨어뜨리면 루트가 된다.</summary>
        GameEngine::Runtime::Scene* scene = nullptr;
        bool isSceneFile = false;
        bool isHeading = false;
    };

    /// <summary>96 DPI 기준의 논리 길이를 이 화면의 픽셀로 바꾼다. 셸의 배율을 따른다.</summary>
    [[nodiscard]] float S(float logical) const;

    void BuildHierarchyRows(std::vector<HierarchyRow>& rows) const;
    void AppendGameObjectRows(
        std::vector<HierarchyRow>& rows,
        GameEngine::Runtime::GameObject& gameObject,
        int depth) const;
    void CreateGameObject();
    void DeleteSelectedGameObject();
    void UpdateHierarchyDrag(
        const std::vector<std::pair<GameEngine::UI::UIRect, const HierarchyRow*>>& visibleRows);

    IEditorScale& mScale;
    IPropertyEditHost& mPropertyEdit;
    EditorContext& mContext;
    GameEngine::UI::UIContext& mUI;
    /// <summary>물음이 서는 줄이다. 계층은 여기에 올리기만 하고 그리지 않는다.</summary>
    ConfirmationQueue& mConfirmations;

    /// <summary>
    /// 물음의 답이 장면을 바꾼 프레임인지다. 그리기가 이것을 소비해 필드 포커스를 정리한다 —
    /// 이전 장면의 필드에 커서가 남아 있으면 다음 타이핑이 없는 것을 고친다.
    /// </summary>
    bool mSceneChanged = false;

    /// <summary>집고 있는 객체다. 0이면 아무것도 집지 않았다.</summary>
    unsigned int mDragInstanceId = 0;
    /// <summary>그 객체를 끄는 손짓이다. 문턱과 클릭·놓기의 구분이 여기 산다.</summary>
    GameEditor::DragGesture mDrag;
};

}
