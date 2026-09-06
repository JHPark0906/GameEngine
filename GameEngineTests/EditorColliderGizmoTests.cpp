#include "EditorColliderGizmoTests.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "Document/EditorContext.h"
#include "Rules/EditorPanelHosts.h"
#include "Rules/EditorSceneTool.h"
#include "Views/EditorSceneViewPanel.h"
#include "Math/Aabb3D.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/ViewportProjection.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/BoxCollider2D.h"
#include "Runtime/BoxCollider3D.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "TestSupport.h"
#include "UI/UIContext.h"

using TestSupport::Expect;

namespace
{
    constexpr GameEngine::Math::Color ColliderGizmoColor{ 0.20f, 0.90f, 0.90f, 1.0f };
    constexpr GameEngine::Math::Color SelectedColliderGizmoColor{ 0.55f, 1.0f, 1.0f, 1.0f };
    constexpr float ColliderGizmoThickness = 2.0f;
    constexpr float SelectedColliderGizmoThickness = 3.0f;
    constexpr GameEngine::UI::UIRect ViewAtOrigin{ 0.0f, 0.0f, 640.0f, 400.0f };
    constexpr GameEngine::UI::UIRect OffsetView{ 700.0f, 450.0f, 640.0f, 400.0f };
    constexpr GameEngine::Rendering::RenderTargetSize TestRenderTargetSize{ 1600, 1000 };

    /// <summary>셸이 주는 DPI 배율만 재현한다. 패널이 논리 선 두께를 한 번만 키우는지 묻는다.</summary>
    class ScaleHost final : public GameEditor::IEditorScale
    {
    public:
        explicit ScaleHost(const float scale) : mScale(scale) {}

        [[nodiscard]] float S(const float logical) const override { return logical * mScale; }

    private:
        float mScale = 1.0f;
    };

    /// <summary>이 검사는 기즈모의 출력만 본다. 속성 쓰기는 일어나면 안 되므로 빈 호스트면 충분하다.</summary>
    class PropertyEditHost final : public GameEditor::IPropertyEditHost
    {
    public:
        void ApplyProperty(
            GameEngine::Runtime::Component&, const GameEngine::Runtime::PropertyDescriptor&,
            const GameEngine::Runtime::PropertyValue&, std::uint64_t) override
        {
        }

        [[nodiscard]] std::uint64_t MakeMergeKey(GameEngine::UI::WidgetId) const override
        {
            return 0;
        }

        void ResetFieldEditingState() override {}
        void PerformUndo() override {}
        void PerformRedo() override {}
    };

    /// <summary>입력을 가져가지 않는 도구다. 씬 뷰가 기즈모를 포함한 자기 draw를 끝까지 낸다.</summary>
    class EmptySceneTool final : public GameEditor::ISceneTool
    {
    public:
        [[nodiscard]] GameEditor::SceneInputCapture HandleSceneInput(
            const GameEditor::SceneToolInput&) override
        {
            return {};
        }
    };

    class SceneToolHost final : public GameEditor::ISceneToolHost
    {
    public:
        [[nodiscard]] GameEditor::ISceneTool& GetSceneTool() override { return mTool; }

    private:
        EmptySceneTool mTool;
    };

    /// <summary>빈 장면 하나가 든 최소 프로젝트를 만든다. 콜라이더는 열고 난 장면에 직접 붙인다.</summary>
    [[nodiscard]] std::filesystem::path WriteTestProject(const std::filesystem::path& root)
    {
        const std::filesystem::path projectFile = root / "EditorColliderGizmoTest.gameproject";
        const bool wrote =
            TestSupport::WriteFile(projectFile,
                R"({"projectName": "EditorColliderGizmoTest",)"
                R"( "window": { "width": 1600, "height": 1000 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [)"
                R"( { "id": 0, "path": "Scenes/Collider.scene" } ]})") &&
            TestSupport::WriteFile(root / "Scenes" / "Collider.scene",
                R"({"sceneName": "Collider", "gameObjects": []})");
        return wrote ? projectFile : std::filesystem::path{};
    }

    [[nodiscard]] GameEngine::Rendering::RenderFrame DrawFrame(
        GameEditor::EditorSceneViewPanel& panel, GameEngine::UI::UIContext& ui,
        GameEngine::Runtime::Input& input, const GameEngine::UI::UIRect content)
    {
        ui.BeginFrame(input, TestRenderTargetSize);
        panel.Draw(content);
        GameEngine::Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize(TestRenderTargetSize);
        ui.EndFrame(builder);
        return std::move(builder).Build();
    }

    [[nodiscard]] std::vector<const GameEngine::Rendering::SpriteDraw*> FindTintedDraws(
        const GameEngine::Rendering::RenderFrame& frame, const GameEngine::Math::Color& color)
    {
        std::vector<const GameEngine::Rendering::SpriteDraw*> found;
        for (const GameEngine::Rendering::SpriteDraw* const draw :
             frame.GetDraws<GameEngine::Rendering::SpriteDraw>(
                 GameEngine::Rendering::RenderPass::Transparent))
        {
            if (draw && draw->tint == color)
            {
                found.push_back(draw);
            }
        }
        return found;
    }

    /// <summary>회전된 선 quad의 두 화면 축 중 짧은 쪽은 UI에 준 선 두께다.</summary>
    [[nodiscard]] float GetLineThickness(const GameEngine::Rendering::SpriteDraw& draw)
    {
        const GameEngine::Math::Matrix4x4& transform = draw.localToWorld;
        const float firstAxis = std::hypot(
            transform.GetElement(0, 0), transform.GetElement(0, 1));
        const float secondAxis = std::hypot(
            transform.GetElement(1, 0), transform.GetElement(1, 1));
        return (std::min)(firstAxis, secondAxis);
    }

    [[nodiscard]] bool IsNearly(const float left, const float right)
    {
        return std::abs(left - right) <= 0.001f;
    }

    /// <summary>
    /// 3D 상자의 열두 모서리가 Collider3D가 답한 월드 AABB 자리·길이·방향으로 투영됐는지
    /// 확인한다. 기울어진 카메라에서는 로컬 offset에 따른 화면 이동량이 모서리 깊이마다 달라지므로,
    /// 이전 프레임과의 같은 픽셀 차이를 묻지 않고 이 프레임의 실제 투영값을 바로 비교한다.
    /// </summary>
    [[nodiscard]] bool DrawsFollowWorldAabb(
        const std::vector<const GameEngine::Rendering::SpriteDraw*>& draws,
        const GameEngine::Math::Aabb3D& worldBounds,
        const GameEditor::EditorSceneViewPanel& panel, const GameEngine::UI::UIRect& content)
    {
        const GameEngine::Rendering::RenderTargetSize viewSize = panel.GetViewSize();
        if (draws.size() != 12 || !viewSize.IsValid())
        {
            return false;
        }

        const GameEngine::Rendering::CameraRenderData camera = panel.GetCamera();
        const GameEngine::Math::Matrix4x4 viewProjection = camera.view * camera.projection;
        const GameEngine::Math::Vector3 corners[8] = {
            { worldBounds.min.GetX(), worldBounds.min.GetY(), worldBounds.min.GetZ() },
            { worldBounds.max.GetX(), worldBounds.min.GetY(), worldBounds.min.GetZ() },
            { worldBounds.max.GetX(), worldBounds.max.GetY(), worldBounds.min.GetZ() },
            { worldBounds.min.GetX(), worldBounds.max.GetY(), worldBounds.min.GetZ() },
            { worldBounds.min.GetX(), worldBounds.min.GetY(), worldBounds.max.GetZ() },
            { worldBounds.max.GetX(), worldBounds.min.GetY(), worldBounds.max.GetZ() },
            { worldBounds.max.GetX(), worldBounds.max.GetY(), worldBounds.max.GetZ() },
            { worldBounds.min.GetX(), worldBounds.max.GetY(), worldBounds.max.GetZ() },
        };
        constexpr int edges[12][2] = {
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
        };
        for (std::size_t index = 0; index < draws.size(); ++index)
        {
            const GameEngine::Math::ViewportPoint start = GameEngine::Math::ProjectToViewport(
                corners[edges[index][0]], viewProjection,
                static_cast<float>(viewSize.width), static_cast<float>(viewSize.height));
            const GameEngine::Math::ViewportPoint end = GameEngine::Math::ProjectToViewport(
                corners[edges[index][1]], viewProjection,
                static_cast<float>(viewSize.width), static_cast<float>(viewSize.height));
            if (!draws[index] || !start.isInFront || !end.isInFront)
            {
                return false;
            }

            const GameEngine::Math::Vector3 position = draws[index]->localToWorld.GetTranslation();
            const GameEngine::Math::Vector3 direction =
                draws[index]->localToWorld.TransformDirection({ 1.0f, 0.0f, 0.0f });
            if (!IsNearly(position.GetX(), content.x + (start.x + end.x) * 0.5f) ||
                !IsNearly(position.GetY(), content.y + (start.y + end.y) * 0.5f) ||
                !IsNearly(direction.GetX(), end.x - start.x) ||
                !IsNearly(direction.GetY(), end.y - start.y))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool CheckColliderGizmosAtScale(const float scale)
    {
        TestSupport::TemporaryDirectory temporaryDirectory("editor-collider-gizmo");
        const std::filesystem::path projectFile = WriteTestProject(temporaryDirectory.GetPath());
        if (!Expect(!projectFile.empty(), "the collider gizmo test project should be written"))
        {
            return false;
        }

        GameEditor::EditorContext context;
        if (!Expect(context.OpenProject(projectFile), "the collider gizmo test project should open"))
        {
            return false;
        }

        // 실행 파일 옆에 남아 있던 사용자 카메라 상태가 이 장면의 투영을 바꾸지 않게, 패널을
        // 세우기 전에 같은 카메라를 명시한다. +Z를 향해 보므로 XY 콜라이더 평면이 정면에 선다.
        context.UpdateSceneCameraSetting({ 0.0f, 0.0f, 0.0f }, 12.0f, 0.0f, 0.0f);
        GameEngine::Runtime::Scene* const scene = context.GetOpenScene();
        if (!Expect(scene != nullptr, "the collider gizmo project should have an open scene"))
        {
            return false;
        }

        GameEngine::Runtime::GameObject* const ordinaryObject =
            scene->CreateGameObject("OrdinaryCollider");
        GameEngine::Runtime::GameObject* const triggerObject =
            scene->CreateGameObject("TriggerCollider");
        GameEngine::Runtime::GameObject* const selectedObject =
            scene->CreateGameObject("SelectedCollider");
        GameEngine::Runtime::GameObject* const disabledObject =
            scene->CreateGameObject("DisabledCollider");
        GameEngine::Runtime::GameObject* const inactiveObject =
            scene->CreateGameObject("InactiveCollider");
        GameEngine::Runtime::GameObject* const emptyObject =
            scene->CreateGameObject("EmptyCollider");
        if (!Expect(
                ordinaryObject && triggerObject && selectedObject && disabledObject && inactiveObject &&
                    emptyObject,
                "the collider gizmo scene should assemble"))
        {
            return false;
        }

        ordinaryObject->GetTransform().SetPosition({ -3.0f, 0.0f, 0.0f });
        triggerObject->GetTransform().SetPosition({ 3.0f, 0.0f, 0.0f });
        disabledObject->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
        inactiveObject->GetTransform().SetPosition({ 5.0f, 0.0f, 0.0f });

        GameEngine::Runtime::BoxCollider2D* const ordinary =
            ordinaryObject->AddComponent<GameEngine::Runtime::BoxCollider2D>();
        GameEngine::Runtime::BoxCollider2D* const trigger =
            triggerObject->AddComponent<GameEngine::Runtime::BoxCollider2D>();
        GameEngine::Runtime::BoxCollider2D* const selected =
            selectedObject->AddComponent<GameEngine::Runtime::BoxCollider2D>();
        GameEngine::Runtime::BoxCollider2D* const disabled =
            disabledObject->AddComponent<GameEngine::Runtime::BoxCollider2D>();
        GameEngine::Runtime::BoxCollider2D* const inactive =
            inactiveObject->AddComponent<GameEngine::Runtime::BoxCollider2D>();
        GameEngine::Runtime::BoxCollider2D* const empty =
            emptyObject->AddComponent<GameEngine::Runtime::BoxCollider2D>();
        if (!Expect(
                ordinary && trigger && selected && disabled && inactive && empty,
                "the collider gizmo components should assemble"))
        {
            return false;
        }

        ordinary->SetSize({ 2.0f, 2.0f });
        trigger->SetSize({ 2.0f, 2.0f });
        trigger->SetTrigger(true);
        selected->SetOffset({ 0.5f, -0.25f });
        selected->SetSize({ 2.0f, 2.0f });
        disabled->SetSize({ 2.0f, 2.0f });
        disabled->SetEnabled(false);
        inactive->SetSize({ 2.0f, 2.0f });
        inactiveObject->SetActive(false);
        empty->SetSize({ 0.0f, 2.0f });
        context.SelectObject(selectedObject->GetInstanceId());

        ScaleHost scaleHost{ scale };
        PropertyEditHost propertyEdit;
        SceneToolHost sceneToolHost;
        GameEngine::UI::UIContext ui{ nullptr, nullptr };
        GameEditor::EditorSceneViewPanel panel{
            scaleHost, propertyEdit, sceneToolHost, context, ui };
        GameEngine::Runtime::Input input;

        const GameEngine::Rendering::RenderFrame baseline =
            DrawFrame(panel, ui, input, ViewAtOrigin);
        selected->SetOffset({ 0.0f, 0.0f });
        const GameEngine::Rendering::RenderFrame withoutLocalOffset =
            DrawFrame(panel, ui, input, ViewAtOrigin);
        selected->SetOffset({ 0.5f, -0.25f });
        const GameEngine::Rendering::RenderFrame offset =
            DrawFrame(panel, ui, input, OffsetView);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> normal =
            FindTintedDraws(baseline, ColliderGizmoColor);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> selectedDraws =
            FindTintedDraws(baseline, SelectedColliderGizmoColor);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> unoffsetSelectedDraws =
            FindTintedDraws(withoutLocalOffset, SelectedColliderGizmoColor);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> offsetSelectedDraws =
            FindTintedDraws(offset, SelectedColliderGizmoColor);

        // 일반과 trigger는 같은 cyan으로 네 변씩 나오고, disabled/inactive/empty는 한 변도
        // 더하지 않는다. 선택된 것은 별도의 밝은 cyan 네 변으로 마지막 패스에 나온다.
        bool selectedAfterOrdinary = !normal.empty() && !selectedDraws.empty();
        std::size_t lastNormal = 0;
        std::size_t firstSelected =
            baseline.GetDraws<GameEngine::Rendering::SpriteDraw>(
                GameEngine::Rendering::RenderPass::Transparent).size();
        const std::vector<const GameEngine::Rendering::SpriteDraw*> allDraws =
            baseline.GetDraws<GameEngine::Rendering::SpriteDraw>(
                GameEngine::Rendering::RenderPass::Transparent);
        for (std::size_t index = 0; index < allDraws.size(); ++index)
        {
            const GameEngine::Rendering::SpriteDraw* const draw = allDraws[index];
            if (draw && draw->tint == ColliderGizmoColor)
            {
                lastNormal = index;
            }
            if (draw && draw->tint == SelectedColliderGizmoColor)
            {
                firstSelected = (std::min)(firstSelected, index);
            }
        }
        selectedAfterOrdinary &= lastNormal < firstSelected;

        const bool normalThickness = std::all_of(
            normal.begin(), normal.end(), [scale](const GameEngine::Rendering::SpriteDraw* const draw)
            {
                return draw && IsNearly(
                    GetLineThickness(*draw), ColliderGizmoThickness * scale);
            });
        const bool selectedThickness = std::all_of(
            selectedDraws.begin(), selectedDraws.end(),
            [scale](const GameEngine::Rendering::SpriteDraw* const draw)
            {
                return draw && IsNearly(
                    GetLineThickness(*draw), SelectedColliderGizmoThickness * scale);
            });

        // 선택된 상자는 Transform 원점이 아니라 Collider2D가 답한 월드 AABB를 그린다. 로컬
        // offset을 없앤 프레임과 비교해 네 변이 모두 오른쪽·아래로 같은 양만큼 옮겨야 한다.
        bool followsWorldBounds = selectedDraws.size() == unoffsetSelectedDraws.size() &&
            !selectedDraws.empty();
        float expectedOffsetX = 0.0f;
        float expectedOffsetY = 0.0f;
        for (std::size_t index = 0; index < selectedDraws.size() && followsWorldBounds; ++index)
        {
            const GameEngine::Math::Vector3 withOffset =
                selectedDraws[index]->localToWorld.GetTranslation();
            const GameEngine::Math::Vector3 withoutOffset =
                unoffsetSelectedDraws[index]->localToWorld.GetTranslation();
            const float deltaX = withOffset.GetX() - withoutOffset.GetX();
            const float deltaY = withOffset.GetY() - withoutOffset.GetY();
            if (index == 0)
            {
                expectedOffsetX = deltaX;
                expectedOffsetY = deltaY;
                followsWorldBounds = deltaX > 0.001f && deltaY > 0.001f;
            }
            else
            {
                followsWorldBounds &= IsNearly(deltaX, expectedOffsetX) &&
                    IsNearly(deltaY, expectedOffsetY);
            }
        }

        // 투영 좌표는 두 프레임에서 같고, 달라지는 것은 content의 UI 원점뿐이다. 네 변 모두가
        // 그 차이만큼 정확히 옮겨야 패널이 창 좌상단이 아닌 곳에서도 outline을 붙여 그린다.
        bool followsContentOffset = selectedDraws.size() == offsetSelectedDraws.size();
        for (std::size_t index = 0; index < selectedDraws.size() && followsContentOffset; ++index)
        {
            const GameEngine::Math::Vector3 baselinePosition =
                selectedDraws[index]->localToWorld.GetTranslation();
            const GameEngine::Math::Vector3 offsetPosition =
                offsetSelectedDraws[index]->localToWorld.GetTranslation();
            followsContentOffset =
                IsNearly(offsetPosition.GetX() - baselinePosition.GetX(), OffsetView.x) &&
                IsNearly(offsetPosition.GetY() - baselinePosition.GetY(), OffsetView.y);
        }

        return Expect(
                   normal.size() == 8,
                   "active and trigger 2D colliders should each draw four ordinary cyan edges") &&
            Expect(
                selectedDraws.size() == 4,
                "a selected 2D collider should draw four brighter cyan edges") &&
            Expect(
                selectedAfterOrdinary,
                "selected collider edges should be emitted after ordinary collider edges") &&
            Expect(
                normalThickness,
                "ordinary collider gizmo edges should use the scaled two-pixel thickness") &&
            Expect(
                selectedThickness,
                "selected collider gizmo edges should use the scaled three-pixel thickness") &&
            Expect(
                followsWorldBounds,
                "collider gizmo edges should follow a collider's world bounds rather than its transform origin") &&
            Expect(
                followsContentOffset,
                "collider gizmo edges should follow the scene view content offset");
    }

    [[nodiscard]] bool CheckCollider3DGizmosAtScale(const float scale)
    {
        TestSupport::TemporaryDirectory temporaryDirectory("editor-collider-3d-gizmo");
        const std::filesystem::path projectFile = WriteTestProject(temporaryDirectory.GetPath());
        if (!Expect(!projectFile.empty(), "the 3D collider gizmo test project should be written"))
        {
            return false;
        }

        GameEditor::EditorContext context;
        if (!Expect(context.OpenProject(projectFile), "the 3D collider gizmo test project should open"))
        {
            return false;
        }

        // 앞·뒤 모서리까지 서로 다른 화면 자리를 가져야 열두 선 모두가 실제 3D outline으로
        // 검증된다. 모든 상자는 피벗 근처에 두어 어느 꼭짓점도 카메라 뒤로 가지 않는다.
        context.UpdateSceneCameraSetting({ 0.0f, 0.0f, 0.0f }, 12.0f, 30.0f, -20.0f);
        GameEngine::Runtime::Scene* const scene = context.GetOpenScene();
        if (!Expect(scene != nullptr, "the 3D collider gizmo project should have an open scene"))
        {
            return false;
        }

        GameEngine::Runtime::GameObject* const ordinaryObject =
            scene->CreateGameObject("OrdinaryCollider3D");
        GameEngine::Runtime::GameObject* const triggerObject =
            scene->CreateGameObject("TriggerCollider3D");
        GameEngine::Runtime::GameObject* const selectedObject =
            scene->CreateGameObject("SelectedCollider3D");
        GameEngine::Runtime::GameObject* const disabledObject =
            scene->CreateGameObject("DisabledCollider3D");
        GameEngine::Runtime::GameObject* const inactiveObject =
            scene->CreateGameObject("InactiveCollider3D");
        GameEngine::Runtime::GameObject* const emptyObject =
            scene->CreateGameObject("EmptyCollider3D");
        GameEngine::Runtime::GameObject* const flatObject =
            scene->CreateGameObject("FlatCollider3D");
        if (!Expect(
                ordinaryObject && triggerObject && selectedObject && disabledObject && inactiveObject &&
                    emptyObject && flatObject,
                "the 3D collider gizmo scene should assemble"))
        {
            return false;
        }

        ordinaryObject->GetTransform().SetPosition({ -3.0f, 0.0f, 0.0f });
        triggerObject->GetTransform().SetPosition({ 3.0f, 0.0f, 0.0f });
        disabledObject->GetTransform().SetPosition({ -5.0f, 0.0f, 0.0f });
        inactiveObject->GetTransform().SetPosition({ 5.0f, 0.0f, 0.0f });
        emptyObject->GetTransform().SetPosition({ 0.0f, 3.0f, 0.0f });
        flatObject->GetTransform().SetPosition({ 0.0f, -3.0f, 0.0f });

        GameEngine::Runtime::BoxCollider3D* const ordinary =
            ordinaryObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        GameEngine::Runtime::BoxCollider3D* const trigger =
            triggerObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        GameEngine::Runtime::BoxCollider3D* const selected =
            selectedObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        GameEngine::Runtime::BoxCollider3D* const disabled =
            disabledObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        GameEngine::Runtime::BoxCollider3D* const inactive =
            inactiveObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        GameEngine::Runtime::BoxCollider3D* const empty =
            emptyObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        GameEngine::Runtime::BoxCollider3D* const flat =
            flatObject->AddComponent<GameEngine::Runtime::BoxCollider3D>();
        if (!Expect(
                ordinary && trigger && selected && disabled && inactive && empty && flat,
                "the 3D collider gizmo components should assemble"))
        {
            return false;
        }

        ordinary->SetSize({ 2.0f, 2.0f, 2.0f });
        trigger->SetSize({ 2.0f, 2.0f, 2.0f });
        trigger->SetTrigger(true);
        selected->SetOffset({ 0.5f, -0.25f, 0.75f });
        selected->SetSize({ 2.0f, 2.0f, 2.0f });
        disabled->SetSize({ 2.0f, 2.0f, 2.0f });
        disabled->SetEnabled(false);
        inactive->SetSize({ 2.0f, 2.0f, 2.0f });
        inactiveObject->SetActive(false);
        empty->SetSize({ 0.0f, 2.0f, 2.0f });
        flat->SetSize({ 2.0f, 2.0f, 2.0f });
        flatObject->GetTransform().SetScale({ 1.0f, 1.0f, 0.0f });
        context.SelectObject(selectedObject->GetInstanceId());

        ScaleHost scaleHost{ scale };
        PropertyEditHost propertyEdit;
        SceneToolHost sceneToolHost;
        GameEngine::UI::UIContext ui{ nullptr, nullptr };
        GameEditor::EditorSceneViewPanel panel{
            scaleHost, propertyEdit, sceneToolHost, context, ui };
        GameEngine::Runtime::Input input;

        const GameEngine::Rendering::RenderFrame baseline =
            DrawFrame(panel, ui, input, ViewAtOrigin);
        const GameEngine::Rendering::RenderFrame offset =
            DrawFrame(panel, ui, input, OffsetView);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> normal =
            FindTintedDraws(baseline, ColliderGizmoColor);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> selectedDraws =
            FindTintedDraws(baseline, SelectedColliderGizmoColor);
        const std::vector<const GameEngine::Rendering::SpriteDraw*> offsetSelectedDraws =
            FindTintedDraws(offset, SelectedColliderGizmoColor);

        bool selectedAfterOrdinary = !normal.empty() && !selectedDraws.empty();
        std::size_t lastNormal = 0;
        std::size_t firstSelected =
            baseline.GetDraws<GameEngine::Rendering::SpriteDraw>(
                GameEngine::Rendering::RenderPass::Transparent).size();
        const std::vector<const GameEngine::Rendering::SpriteDraw*> allDraws =
            baseline.GetDraws<GameEngine::Rendering::SpriteDraw>(
                GameEngine::Rendering::RenderPass::Transparent);
        for (std::size_t index = 0; index < allDraws.size(); ++index)
        {
            const GameEngine::Rendering::SpriteDraw* const draw = allDraws[index];
            if (draw && draw->tint == ColliderGizmoColor)
            {
                lastNormal = index;
            }
            if (draw && draw->tint == SelectedColliderGizmoColor)
            {
                firstSelected = (std::min)(firstSelected, index);
            }
        }
        selectedAfterOrdinary &= lastNormal < firstSelected;

        const bool normalThickness = std::all_of(
            normal.begin(), normal.end(), [scale](const GameEngine::Rendering::SpriteDraw* const draw)
            {
                return draw && IsNearly(
                    GetLineThickness(*draw), ColliderGizmoThickness * scale);
            });
        const bool selectedThickness = std::all_of(
            selectedDraws.begin(), selectedDraws.end(),
            [scale](const GameEngine::Rendering::SpriteDraw* const draw)
            {
                return draw && IsNearly(
                    GetLineThickness(*draw), SelectedColliderGizmoThickness * scale);
            });
        const bool followsWorldBounds =
            DrawsFollowWorldAabb(selectedDraws, selected->GetWorldBounds(), panel, ViewAtOrigin);
        const bool followsContentOffset =
            DrawsFollowWorldAabb(offsetSelectedDraws, selected->GetWorldBounds(), panel, OffsetView);

        return Expect(
                   normal.size() == 24,
                   "active and trigger 3D colliders should each draw twelve ordinary cyan edges") &&
            Expect(
                selectedDraws.size() == 12,
                "a selected 3D collider should draw twelve brighter cyan edges") &&
            Expect(
                selectedAfterOrdinary,
                "selected 3D collider edges should be emitted after ordinary collider edges") &&
            Expect(
                normalThickness,
                "ordinary 3D collider gizmo edges should use the scaled two-pixel thickness") &&
            Expect(
                selectedThickness,
                "selected 3D collider gizmo edges should use the scaled three-pixel thickness") &&
            Expect(
                followsWorldBounds,
                "3D collider gizmo edges should follow a collider's world bounds") &&
            Expect(
                followsContentOffset,
                "3D collider gizmo edges should follow the scene view content offset");
    }
}

bool RunEditorColliderGizmoTests()
{
    return TestSupport::ForEachUiScale(CheckColliderGizmosAtScale) &&
        TestSupport::ForEachUiScale(CheckCollider3DGizmosAtScale);
}

static const TestSupport::Registration gEditorColliderGizmoTests{
    "EditorDocument", "editor collider gizmo tests should pass", RunEditorColliderGizmoTests };
