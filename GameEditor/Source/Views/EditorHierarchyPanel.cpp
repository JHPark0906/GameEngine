#include "Views/EditorHierarchyPanel.h"
#include <optional>

#include "Rules/EditorConfirmation.h"
#include "Rules/EditorSceneSwitchQuestions.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <ranges>
#include <utility>

#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "Rules/EditorPanelCommon.h"
#include "App/ProjectFile.h"
#include "Diagnostics/Debug.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"

namespace GameEditor
{

namespace
{
    using GameEngine::UI::UIRect;
}

EditorHierarchyPanel::EditorHierarchyPanel(
    IEditorScale& scale, IPropertyEditHost& propertyEdit, EditorContext& context, GameEngine::UI::UIContext& ui,
    ConfirmationQueue& confirmations)
    : mScale(scale), mPropertyEdit(propertyEdit), mContext(context), mUI(ui), mConfirmations(confirmations)
{
}

float EditorHierarchyPanel::S(const float logical) const
{
    return mScale.S(logical);
}

void EditorHierarchyPanel::BuildHierarchyRows(std::vector<HierarchyRow>& rows) const
{
    if (const GameEngine::App::ProjectFileData* project = mContext.GetOpenProject())
    {
        HierarchyRow heading;
        heading.label = "[Project] " + ToUtf8(project->settings.projectName);
        heading.isHeading = true;
        rows.push_back(std::move(heading));
        std::vector<std::pair<unsigned int, std::filesystem::path>> scenes(
            project->settings.scenePaths.begin(), project->settings.scenePaths.end());
        std::ranges::sort(scenes, {}, &std::pair<unsigned int, std::filesystem::path>::first);
        for (const auto& [sceneId, scenePath] : scenes)
        {
            const bool isOpen = mContext.HasOpenScene() &&
                mContext.GetOpenProjectSceneId() == sceneId;
            // 장면 목록은 등록을 따르므로 탐색기에서 파일만 지워도 행이 남는다.
            // 누를 수 없는 이유를 알 수 있도록 파일이 없는 행은 그 사실을 표시한다.
            const bool fileIsMissing = !GameEngine::App::ProjectFile::SceneFileExists(
                *project, sceneId);
            const char* const marker = fileIsMissing
                ? "[Scene?] "
                : (isOpen ? "[Scene*] " : "[Scene] ");
            HierarchyRow row;
            row.label = std::string(marker) + scenePath.generic_string() +
                (fileIsMissing ? "  (file is missing)" : "");
            row.indent = 1.0f;
            row.sceneId = sceneId;
            row.isSceneFile = true;
            rows.push_back(std::move(row));
        }
    }

    GameEngine::Runtime::Game* const projectGame = mContext.GetProjectGame();
    if (!projectGame)
    {
        return;
    }
    // 프로젝트 런타임의 장면들이다. 위의 파일 행이 "열 수 있는 것"이라면 여기는 "열려 있는 것"
    // 이고, 이름이 둘을 이어 준다.
    for (const auto& [sceneId, scene] : projectGame->GetSceneManager().GetActiveScenes())
    {
        HierarchyRow heading;
        heading.label = "[Open Scene] " + scene->GetName();
        heading.scene = scene.get();
        heading.isHeading = true;
        rows.push_back(std::move(heading));
        for (GameEngine::Runtime::GameObject* gameObject : scene->GetRootGameObjects())
        {
            if (gameObject)
            {
                AppendGameObjectRows(rows, *gameObject, 1);
            }
        }
    }
}

void EditorHierarchyPanel::AppendGameObjectRows(
    std::vector<HierarchyRow>& rows,
    GameEngine::Runtime::GameObject& gameObject,
    const int depth) const
{
    HierarchyRow row;
    row.label = gameObject.GetName().empty() ? "(GameObject)" : gameObject.GetName();
    row.indent = static_cast<float>(depth);
    row.instanceId = gameObject.GetInstanceId();
    rows.push_back(std::move(row));

    for (GameEngine::Runtime::Transform* childTransform : gameObject.GetTransform().GetChildren())
    {
        GameEngine::Runtime::GameObject* child =
            childTransform ? childTransform->GetGameObject() : nullptr;
        if (child)
        {
            AppendGameObjectRows(rows, *child, depth + 1);
        }
    }
}

void EditorHierarchyPanel::AskAboutSwitchingScene(
    const unsigned int sceneId, const std::string& question)
{
    ConfirmationRequest request;
    request.title = "Unsaved changes";
    request.question = question;
    request.choices = { "Save and open", "Discard and open" };
    request.cancelLabel = "Stay here";
    request.onAnswered = [this, sceneId](const std::optional<std::size_t> chosen)
    {
        if (!chosen)
        {
            return;
        }
        if (*chosen == 0)
        {
            // 저장이 실패하면 장면을 바꾸지 않는다. 저장한 줄 알고 잃는 것이 이 물음이 막으려는
            // 일이므로, 물음을 닫지 않고 실패를 붙여 다시 세운다.
            if (!mContext.SaveOpenScene())
            {
                AskAboutSwitchingScene(
                    sceneId, MakeSceneSwitchQuestion(SceneSwitchFailure::SaveFailed));
                return;
            }
            if (!mContext.OpenScene(sceneId))
            {
                // 저장은 됐다. 그 사실을 말하지 않으면 사람은 다시 저장을 시도한다.
                AskAboutSwitchingScene(
                    sceneId, MakeSceneSwitchQuestion(SceneSwitchFailure::OpenFailedAfterSave));
                return;
            }
        }
        else if (!mContext.OpenScene(sceneId))
        {
            // 버리기를 눌렀는데 못 열었다. 편집은 아직 살아 있으므로 그것을 말해야 한다.
            AskAboutSwitchingScene(
                sceneId, MakeSceneSwitchQuestion(SceneSwitchFailure::OpenFailedAfterDiscard));
            return;
        }
        // 장면이 실제로 바뀐 프레임이다. 그리기가 이것을 보고 필드 포커스를 정리한다.
        mSceneChanged = true;
    };
    mConfirmations.Ask(std::move(request));
}

void EditorHierarchyPanel::Draw(GameEngine::UI::UIRect content)
{
    // 질문에 답해 장면이 바뀌었으면 필드 포커스는 이전 장면의 것이다.
    if (mSceneChanged)
    {
        mSceneChanged = false;
        mUI.ClearFieldFocus();
    }
    // 물음은 떠 있는 창이 표시하므로 목록 배치는 물음의 높이에 영향받지 않는다.

    // 목록 위의 버튼 줄: 객체 만들기와 지우기.
    const UIRect buttonRow{ content.x, content.y, content.width, S(RowHeight) + 2.0f * S(Padding) };
    if (mUI.DrawButton(
            GameEngine::UI::MakeWidgetId("hierarchy-add"),
            { buttonRow.x + S(Padding), buttonRow.y + S(Padding), S(52.0f), S(RowHeight) }, "Add"))
    {
        CreateGameObject();
    }
    // 지울 것이 없으면 버튼을 비활성으로 표시한다.
    // 할 수 없는 동작과 눌러도 반응 없는 고장이 구별되어야 한다.
    const UIRect deleteRect{
        buttonRow.x + S(52.0f) + 2.0f * S(Padding), buttonRow.y + S(Padding),
        S(64.0f), S(RowHeight) };
    const bool canDelete = dynamic_cast<GameEngine::Runtime::GameObject*>(
        mContext.GetSelectedObject()) != nullptr;
    if (canDelete)
    {
        if (mUI.DrawButton(GameEngine::UI::MakeWidgetId("hierarchy-delete"), deleteRect, "Delete"))
        {
            DeleteSelectedGameObject();
        }
    }
    else
    {
        // 즉시 모드 UI에 비활성 버튼이 따로 없어서, 누를 수 없는 쪽은 버튼 대신 어두운 판으로
        // 보인다 — 툴바가 쓰는 것과 같은 모양이다.
        mUI.DrawPanel(deleteRect, PanelColor);
        mUI.DrawLabel(
            deleteRect, "Delete", DimTextColor, RowFontSize, GameEngine::UI::TextAlign::Center,
            GameEngine::UI::UIFontRole::Title);
    }
    content = { content.x, content.y + buttonRow.height,
        content.width, content.height - buttonRow.height };

    std::vector<HierarchyRow> rows;
    BuildHierarchyRows(rows);

    const float offset = mUI.ApplyScroll(
        GameEngine::UI::MakeWidgetId("hierarchy-scroll"), content,
        static_cast<float>(rows.size()) * S(RowHeight));
    std::vector<std::pair<UIRect, const HierarchyRow*>> visibleRows;
    float y = content.y - offset;
    for (const HierarchyRow& row : rows)
    {
        const UIRect rowRect{ content.x, y, content.width, S(RowHeight) };
        y += S(RowHeight);
        // 즉시 모드 UI에는 클리핑이 없어서, 영역을 벗어난 행은 그리지 않는 것이 클리핑이다.
        if (rowRect.y < content.y || rowRect.GetBottom() > content.GetBottom())
        {
            continue;
        }
        visibleRows.emplace_back(rowRect, &row);
        const UIRect labelRect{
            rowRect.x + S(Padding) + row.indent * S(14.0f), rowRect.y,
            rowRect.width - S(Padding) - row.indent * S(14.0f), rowRect.height };

        if (row.isHeading)
        {
            // 드래그 중인 객체를 장면 제목 위에 들고 있으면, 놓으면 루트가 된다는 뜻으로 밝힌다.
            if (mDrag.IsDragging() && row.scene && rowRect.Contains(mUI.GetMouseX(), mUI.GetMouseY()))
            {
                mUI.DrawPanel(rowRect, DropTargetColor);
            }
            mUI.DrawLabel(labelRect, row.label, DimTextColor, RowFontSize);
            continue;
        }
        if (row.isSceneFile)
        {
            // 한 번 누르면 고르고, 두 번 누르면 연다. 고르는 것이 따로 있는 이유는 이름을
            // 바꾸거나 지울 대상이 열지 않은 장면일 수 있기 때문이다 — 지우려고 먼저 열어야
            // 한다면 지우는 순간 편집 중인 장면이 사라진다.
            const bool isSelected = mContext.GetSelectedSceneId() == row.sceneId;
            const auto result = mUI.DrawSelectable(
                GameEngine::UI::MakeWidgetId("hierarchy-scene", row.sceneId),
                rowRect, "", isSelected);
            mUI.DrawLabel(labelRect, row.label, DimTextColor, RowFontSize);
            if (result.clicked)
            {
                mContext.SelectScene(row.sceneId);
            }
            // 장면 파일 행의 더블클릭이 그 장면을 여는 제스처다. 저장되지 않은 편집이 있으면
            // 곧바로 열지 않고 목록 위의 확인 줄로 묻는다 — 그 교체가 편집을 버리기 때문이다.
            if (result.doubleClicked)
            {
                if (mContext.HasUnsavedChanges())
                {
                    AskAboutSwitchingScene(row.sceneId, MakeSceneSwitchQuestion());
                }
                else if (mContext.OpenScene(row.sceneId))
                {
                    mUI.ClearFieldFocus();
                }
            }
            continue;
        }

        const bool selected = mContext.GetSelectedInstanceId() == row.instanceId;
        const auto result = mUI.DrawSelectable(
            GameEngine::UI::MakeWidgetId("hierarchy-object", row.instanceId),
            rowRect, "", selected);
        if (mDrag.IsDragging() && row.instanceId != mDragInstanceId &&
            rowRect.Contains(mUI.GetMouseX(), mUI.GetMouseY()))
        {
            mUI.DrawPanel(rowRect, DropTargetColor);
        }
        mUI.DrawLabel(labelRect, row.label, TextColor, RowFontSize);
        if (result.clicked)
        {
            mContext.SelectObject(row.instanceId);
            // 선택이 바뀌면 이전 선택의 값을 실은 인스펙터 필드의 포커스는 의미를 잃는다.
            mUI.ClearFieldFocus();
            // 누른 자리를 기억한다. 충분히 끌면 드래그가 되고, 그냥 떼면 선택으로 끝난다.
            // 새로 집는다. 먼저 놓는 이유는 집는 동작이 이미 집고 있을 때 아무 일도 하지 않기
            // 때문이다 — 놓지 않으면 무엇을 끄는지만 바뀌고 어디서 집었는지는 옛것으로 남는다.
            mDragInstanceId = row.instanceId;
            mDrag.Release();
            mDrag.Press(mUI.GetMouseX(), mUI.GetMouseY());
        }
    }

    UpdateHierarchyDrag(visibleRows);
}

void EditorHierarchyPanel::UpdateHierarchyDrag(
    const std::vector<std::pair<GameEngine::UI::UIRect, const HierarchyRow*>>& visibleRows)
{
    if (mDragInstanceId == 0)
    {
        return;
    }
    const float mouseX = mUI.GetMouseX();
    const float mouseY = mUI.GetMouseY();

    // 뗌 표시 없이 버튼이 올라와 있으면 이 손짓은 놓기가 아니라 취소다. 유지 모드 UI가 이번
    // 프레임의 포인터를 가져가면 눌림과 뗌이 함께 지워지는데, 그때 집은 것을 재부모화하면
    // 툴바 버튼 하나를 누른 것이 계층을 바꾸는 일이 된다.
    if (!mUI.WasMouseReleased() && !mUI.IsMouseDown())
    {
        mDrag.Release();
        mDragInstanceId = 0;
        return;
    }

    // 문턱은 손이 떨리는 정도로 재부모화가 일어나지 않게 한다. 화면 배율이 곱해진 값을 넘긴다.
    const GameEditor::DragGesture::Result result =
        mDrag.Update(mouseX, mouseY, mUI.IsMouseDown(), S(6.0f));

    auto* const dragged = dynamic_cast<GameEngine::Runtime::GameObject*>(
        mContext.FindObject(mDragInstanceId));
    // 놓는 프레임까지 이름표가 보인다. 손짓은 그 프레임에 이미 끝나 있으므로 놓기가 일어났다는
    // 사실이 곧 "직전까지 끌고 있었다"이다.
    if ((mDrag.IsDragging() || result.dropped) && dragged)
    {
        // 집은 객체의 이름이 커서를 따라다닌다.
        mUI.DrawLabel(
            { mouseX + S(12.0f), mouseY - S(RowHeight) * 0.5f, S(240.0f), S(RowHeight) },
            dragged->GetName().empty() ? "(GameObject)" : dragged->GetName(), TextColor, RowFontSize);
    }

    // 재부모화하고, 성공하면 앞뒤 상태로 undo 커맨드를 기록한다. 월드 유지 재계산이 로컬 값을
    // 바꾸므로, 커맨드는 다시 계산하는 대신 여기서 읽은 로컬 값을 그대로 되세운다.
    const auto reparentWithUndo = [this, dragged](GameEngine::Runtime::Transform* const newParent)
    {
        GameEngine::Runtime::Transform& transform = dragged->GetTransform();
        if (transform.GetParent() == newParent)
        {
            return;
        }
        const GameEngine::Runtime::Transform* const oldParent = transform.GetParent();
        const unsigned int oldParentId = oldParent && oldParent->GetGameObject()
            ? oldParent->GetGameObject()->GetInstanceId() : 0;
        const ReparentGameObjectCommand::TransformState oldLocal{
            transform.GetPosition(), transform.GetRotation(), transform.GetScale(),
            transform.GetSiblingIndex() };
        // 순환이나 장면 경계는 Transform이 거절하고 로그로 말한다. 월드 위치를 유지한다: 계층을
        // 옮기는 것이지 객체를 옮기는 것이 아니다.
        if (!transform.SetParent(newParent, true))
        {
            return;
        }
        const unsigned int newParentId = newParent && newParent->GetGameObject()
            ? newParent->GetGameObject()->GetInstanceId() : 0;
        const ReparentGameObjectCommand::TransformState newLocal{
            transform.GetPosition(), transform.GetRotation(), transform.GetScale(),
            transform.GetSiblingIndex() };
        mContext.RecordEdit(std::make_unique<ReparentGameObjectCommand>(
            mContext, dragged->GetInstanceId(), oldParentId, oldLocal, newParentId, newLocal));
    };

    if (result.dropped || result.cancelled)
    {
        if (result.dropped && dragged)
        {
            for (const auto& [rowRect, row] : visibleRows)
            {
                if (!rowRect.Contains(mouseX, mouseY))
                {
                    continue;
                }
                if (row->instanceId != 0 && row->instanceId != mDragInstanceId)
                {
                    auto* const target = dynamic_cast<GameEngine::Runtime::GameObject*>(
                        mContext.FindObject(row->instanceId));
                    if (target)
                    {
                        reparentWithUndo(&target->GetTransform());
                    }
                }
                else if (row->scene && row->scene == dragged->GetScene())
                {
                    reparentWithUndo(nullptr);
                }
                break;
            }
        }
        // 손짓은 끝났다. 손짓은 이미 자기를 비웠으므로 집은 것만 놓는다.
        mDragInstanceId = 0;
    }
}

void EditorHierarchyPanel::CreateGameObject()
{
    // 생성은 커맨드의 Apply가 한다 — 만든 것을 선택하는 것까지가 그 편집이라, redo도 선택을
    // 되살린다. 대상은 열린 장면이다 — 에디터는 한 번에 한 장면을 편집하므로 선택된 객체의
    // 장면과 언제나 같다.
    auto command = std::make_unique<CreateGameObjectCommand>(mContext, "GameObject");
    if (!command->Apply())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Cannot create a GameObject without an open scene.");
        return;
    }
    mContext.RecordEdit(std::move(command));
}

void EditorHierarchyPanel::DeleteSelectedGameObject()
{
    auto* gameObject =
        dynamic_cast<GameEngine::Runtime::GameObject*>(mContext.GetSelectedObject());
    if (!gameObject)
    {
        return;
    }
    // 커맨드가 삭제 전에 부분 트리 전체 — 자손, 컴포넌트, 속성 — 를 스냅숏하므로, undo가 그
    // 순서대로 다시 세운다.
    auto command = std::make_unique<DeleteGameObjectCommand>(mContext, *gameObject);
    if (!command->Apply())
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to delete the selected GameObject from the Hierarchy.");
        return;
    }
    mContext.RecordEdit(std::move(command));
    mContext.SelectObject(0);
    // 지워진 객체의 칸 텍스트와 포커스는 이제 아무의 것도 아니다.
    mPropertyEdit.ResetFieldEditingState();
}

}
