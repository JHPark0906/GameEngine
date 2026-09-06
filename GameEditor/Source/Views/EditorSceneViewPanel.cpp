#include "Views/EditorSceneViewPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Document/EditorContext.h"
#include "Rules/EditorSceneTool.h"
#include "Assets/AssetDatabase.h"
#include "Math/MathUtility.h"
#include "Math/Matrix.h"
#include "Math/ViewportProjection.h"
#include "Runtime/Collider2D.h"
#include "Runtime/Collider3D.h"
#include "Runtime/GameObject.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/Scene.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/Transform.h"

namespace GameEditor
{

namespace
{
    using GameEngine::UI::UIRect;

    // 씬 뷰 궤도 조작의 감도다.
    constexpr float OrbitDegreesPerPixel = 0.4f;
    constexpr float MinimumPitchDegrees = -89.0f;
    constexpr float MaximumPitchDegrees = 89.0f;
    constexpr float MinimumDistance = 0.5f;
    constexpr float MaximumDistance = 500.0f;
    /// <summary>휠 한 눈금마다 거리에 곱해지는 비율이다. 곱이라서 가까울수록 섬세해진다.</summary>
    constexpr float DollyStepScale = 0.9f;
    /// <summary>팬 속도는 거리에 비례한다. 멀리서 보고 있을수록 한 픽셀이 많은 월드를 덮는다.</summary>
    constexpr float PanWorldPerPixelPerDistance = 0.002f;

    /// <summary>기즈모 축의 월드 길이를 카메라 거리에 비례시키는 비율이다.</summary>
    constexpr float GizmoWorldLengthPerDistance = 0.18f;
    /// <summary>축 끝 손잡이의 한 변이자 히트 반경이다. 96 DPI 기준 픽셀이다.</summary>
    constexpr float GizmoHandlePixels = 11.0f;
    /// <summary>축이 화면에서 이보다 짧으면 잡을 수 없다. 시선과 나란한 축이 그렇다.</summary>
    constexpr float GizmoMinimumAxisPixels = 8.0f;

    /// <summary>선택되지 않은 콜라이더의 월드 AABB 윤곽선 색이다.</summary>
    constexpr GameEngine::Math::Color ColliderGizmoColor{ 0.20f, 0.90f, 0.90f, 1.0f };
    /// <summary>선택된 오브젝트의 콜라이더를 다른 윤곽선 위에서 구분하는 색이다.</summary>
    constexpr GameEngine::Math::Color SelectedColliderGizmoColor{ 0.55f, 1.0f, 1.0f, 1.0f };
    constexpr float ColliderGizmoThickness = 2.0f;
    constexpr float SelectedColliderGizmoThickness = 3.0f;

    /// <summary>
    /// 광선과 축 정렬 상자의 교차 거리다. 슬랩 검사: 세 축마다 광선이 상자 안에 있는 t 구간을
    /// 구해 교집합이 남으면 맞은 것이다. 맞지 않으면 false다.
    /// </summary>
    [[nodiscard]] bool IntersectRayBox(
        const float origin[3], const float direction[3],
        const float boxMin[3], const float boxMax[3], float& distance)
    {
        float tMin = 0.0f;
        float tMax = (std::numeric_limits<float>::max)();
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) < 1e-6f)
            {
                if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
                {
                    return false;
                }
                continue;
            }
            float t0 = (boxMin[axis] - origin[axis]) / direction[axis];
            float t1 = (boxMax[axis] - origin[axis]) / direction[axis];
            if (t0 > t1)
            {
                std::swap(t0, t1);
            }
            tMin = (std::max)(tMin, t0);
            tMax = (std::min)(tMax, t1);
            if (tMin > tMax)
            {
                return false;
            }
        }
        distance = tMin;
        return true;
    }
}

EditorSceneViewPanel::EditorSceneViewPanel(
    IEditorScale& scale, IPropertyEditHost& propertyEdit, ISceneToolHost& sceneToolHost, EditorContext& context, GameEngine::UI::UIContext& ui)
    : mScale(scale), mPropertyEdit(propertyEdit), mSceneToolHost(sceneToolHost), mContext(context), mUI(ui)
{
    // 저장돼 있던 편집 시점을 되살린다. 이 카메라는 장면의 컴포넌트가 아니라 사용자 상태라서,
    // 장면 파일이 아니라 에디터 설정에서 온다.
    const GameEngine::App::EditorSettingsData& settings = context.GetSettings();
    if (settings.hasSceneCamera)
    {
        mPivot = settings.cameraPivot;
        mDistance = settings.cameraDistance;
        mYawDegrees = settings.cameraYawDegrees;
        mPitchDegrees = settings.cameraPitchDegrees;
    }
}

float EditorSceneViewPanel::S(const float logical) const
{
    return mScale.S(logical);
}

GameEngine::Rendering::CameraRenderData EditorSceneViewPanel::GetCamera() const
{
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    const Matrix4x4 rotation = Matrix4x4::CreateRotationRollPitchYawDegrees(
        { mPitchDegrees, mYawDegrees, 0.0f });
    const Vector3 forward = rotation.TransformDirection(Vector3::Forward);
    const Vector3 position = mPivot - forward * mDistance;

    GameEngine::Rendering::CameraRenderData camera;
    const Matrix4x4 localToWorld = rotation * Matrix4x4::CreateTranslation(position);
    if (!localToWorld.TryInvert(camera.view))
    {
        camera.view = Matrix4x4::Identity();
    }
    camera.projection = Matrix4x4::CreatePerspectiveFieldOfViewLeftHanded(
        60.0f, mSceneViewSize.IsValid() ? mSceneViewSize.GetAspectRatio() : 1.0f, 0.1f, 1000.0f);
    // 게임 뷰와 구분되는 씬 뷰만의 배경이다. 장면의 카메라가 아니므로 장면의 배경색을 따르지
    // 않는다.
    camera.clearColor = { 0.16f, 0.17f, 0.19f, 1.0f };
    return camera;
}

void EditorSceneViewPanel::PresentImage(const GameEngine::Rendering::CapturedImage& image)
{
    mUI.UpdateDynamicTexture(mSceneImage, image.width, image.height, image.pixels);
}

void EditorSceneViewPanel::MakeSceneRay(
    const float pixelX, const float pixelY,
    GameEngine::Math::Vector3& origin, GameEngine::Math::Vector3& direction) const
{
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    const Matrix4x4 rotation = Matrix4x4::CreateRotationRollPitchYawDegrees(
        { mPitchDegrees, mYawDegrees, 0.0f });
    const Vector3 forward = rotation.TransformDirection(Vector3::Forward);
    const Vector3 right = rotation.TransformDirection(Vector3::Right);
    const Vector3 up = rotation.TransformDirection(Vector3::Up);
    origin = mPivot - forward * mDistance;

    // 픽셀을 NDC로, NDC를 카메라 앞 1단위 평면의 점으로. 카메라의 수직 시야각 60도와 뷰의
    // 종횡비가 그 평면의 크기를 정한다 — GetCamera의 투영과 같은 값이다.
    const float width = static_cast<float>((std::max)(mSceneViewSize.width, 1u));
    const float height = static_cast<float>((std::max)(mSceneViewSize.height, 1u));
    const float ndcX = pixelX / width * 2.0f - 1.0f;
    const float ndcY = 1.0f - pixelY / height * 2.0f;
    const float tanHalfFov = std::tan(GameEngine::Math::ToRadians(60.0f) * 0.5f);
    const float aspect = width / height;
    direction = (forward + right * (ndcX * tanHalfFov * aspect) + up * (ndcY * tanHalfFov))
        .Normalized();
}

bool EditorSceneViewPanel::GetLocalBounds(
    const GameEngine::Runtime::GameObject& gameObject, LocalBounds& bounds)
{
    const GameEngine::Assets::AssetDatabase* const projectAssets = mContext.GetProjectAssetDatabase();
    if (!projectAssets)
    {
        return false;
    }
    const GameEngine::Assets::AssetDatabase& assetDatabase = *projectAssets;
    for (const GameEngine::Runtime::MeshRenderer* renderer :
         gameObject.GetComponents<GameEngine::Runtime::MeshRenderer>())
    {
        if (!renderer->IsRenderable() || !renderer->GetMesh().IsValid())
        {
            continue;
        }
        const std::shared_ptr<const GameEngine::Assets::MeshData> meshData =
            assetDatabase.LoadMesh(renderer->GetMesh());
        if (!meshData || !meshData->IsValid())
        {
            continue;
        }
        const auto cached = mMeshBounds.find(meshData->id);
        if (cached != mMeshBounds.end())
        {
            bounds = cached->second;
            return true;
        }
        LocalBounds computed;
        bool first = true;
        for (const GameEngine::Core::MeshVertex& vertex : meshData->vertices)
        {
            if (first)
            {
                computed = { vertex.position.x, vertex.position.y, vertex.position.z,
                    vertex.position.x, vertex.position.y, vertex.position.z };
                first = false;
                continue;
            }
            computed.minX = (std::min)(computed.minX, vertex.position.x);
            computed.minY = (std::min)(computed.minY, vertex.position.y);
            computed.minZ = (std::min)(computed.minZ, vertex.position.z);
            computed.maxX = (std::max)(computed.maxX, vertex.position.x);
            computed.maxY = (std::max)(computed.maxY, vertex.position.y);
            computed.maxZ = (std::max)(computed.maxZ, vertex.position.z);
        }
        mMeshBounds.emplace(meshData->id, computed);
        bounds = computed;
        return true;
    }
    for (const GameEngine::Runtime::SpriteRenderer* renderer :
         gameObject.GetComponents<GameEngine::Runtime::SpriteRenderer>())
    {
        if (!renderer->IsRenderable() || !renderer->GetSprite().IsValid())
        {
            continue;
        }
        const GameEngine::Assets::Sprite* sprite =
            assetDatabase.FindAsset<GameEngine::Assets::Sprite>(renderer->GetSprite());
        const std::shared_ptr<const GameEngine::Assets::TextureData> texture =
            assetDatabase.LoadTexture(renderer->GetSprite());
        if (!sprite || !texture || !texture->IsValid())
        {
            continue;
        }
        // 스프라이트는 렌더러가 그리는 그대로의 quad다: 단순 모드는 텍스처 크기/픽셀 배율,
        // 9-slice는 지정된 크기. 두께는 없다시피 하지만 슬랩 검사가 0을 싫어하므로 조금 준다.
        float halfWidth, halfHeight;
        if (renderer->GetDrawMode() == GameEngine::Runtime::SpriteRenderer::DrawMode::Sliced)
        {
            halfWidth = renderer->GetSize().GetX() * 0.5f;
            halfHeight = renderer->GetSize().GetY() * 0.5f;
        }
        else
        {
            const float ppu = (std::max)(sprite->GetPixelsPerUnit(), 0.001f);
            halfWidth = static_cast<float>(texture->width) / ppu * 0.5f;
            halfHeight = static_cast<float>(texture->height) / ppu * 0.5f;
        }
        bounds = { -halfWidth, -halfHeight, -0.01f, halfWidth, halfHeight, 0.01f };
        return true;
    }
    return false;
}

unsigned int EditorSceneViewPanel::PickObject(
    const GameEngine::Math::Vector3& origin, const GameEngine::Math::Vector3& direction)
{
    GameEngine::Runtime::Scene* const scene = mContext.GetOpenScene();
    if (!scene)
    {
        return 0;
    }
    const float rayOrigin[3] = { origin.GetX(), origin.GetY(), origin.GetZ() };
    const float rayDirection[3] = { direction.GetX(), direction.GetY(), direction.GetZ() };

    unsigned int nearest = 0;
    float nearestDistance = (std::numeric_limits<float>::max)();
    for (const auto& [instanceId, gameObject] : scene->GetGameObjects())
    {
        if (!gameObject || !gameObject->IsActiveInHierarchy())
        {
            continue;
        }
        LocalBounds local;
        if (!GetLocalBounds(*gameObject, local))
        {
            continue;
        }
        // 로컬 상자의 여덟 꼭짓점을 월드로 옮겨 다시 축 정렬 상자로 감싼다. 회전한 객체에는
        // 넉넉한 상자가 되지만, 클릭 판정에는 그 정도면 충분하다.
        const GameEngine::Math::Matrix4x4 localToWorld =
            gameObject->GetTransform().GetLocalToWorldMatrix();
        float worldMin[3] = { (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
        float worldMax[3] = { (std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)() };
        for (int corner = 0; corner < 8; ++corner)
        {
            const GameEngine::Math::Vector3 world = localToWorld.TransformPoint({
                (corner & 1) ? local.maxX : local.minX,
                (corner & 2) ? local.maxY : local.minY,
                (corner & 4) ? local.maxZ : local.minZ });
            const float components[3] = { world.GetX(), world.GetY(), world.GetZ() };
            for (int axis = 0; axis < 3; ++axis)
            {
                worldMin[axis] = (std::min)(worldMin[axis], components[axis]);
                worldMax[axis] = (std::max)(worldMax[axis], components[axis]);
            }
        }
        float distance = 0.0f;
        if (IntersectRayBox(rayOrigin, rayDirection, worldMin, worldMax, distance) &&
            distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = instanceId;
        }
    }
    return nearest;
}


GameEngine::Math::Matrix4x4 EditorSceneViewPanel::GetViewProjection() const
{
    const GameEngine::Rendering::CameraRenderData camera = GetCamera();
    return camera.view * camera.projection;
}

EditorSceneViewPanel::GizmoLayout EditorSceneViewPanel::ComputeGizmoLayout(
    const GameEngine::UI::UIRect& content) const
{
    GizmoLayout layout;
    const auto* const selected =
        dynamic_cast<const GameEngine::Runtime::GameObject*>(mContext.GetSelectedObject());
    if (!selected || !mSceneViewSize.IsValid())
    {
        return layout;
    }

    const GameEngine::Math::Vector3 origin =
        selected->GetTransform().GetLocalToWorldMatrix().TransformPoint({ 0.0f, 0.0f, 0.0f });
    const GameEngine::Math::Matrix4x4 viewProjection = GetViewProjection();
    const float width = static_cast<float>(mSceneViewSize.width);
    const float height = static_cast<float>(mSceneViewSize.height);
    const GameEngine::Math::ViewportPoint projected =
        GameEngine::Math::ProjectToViewport(origin, viewProjection, width, height);
    if (!projected.isInFront)
    {
        return layout;
    }

    // 축 길이는 카메라 거리에 비례한다: 멀어져도 손잡이가 화면에서 비슷한 크기로 남는다.
    const float axisLength = mDistance * GizmoWorldLengthPerDistance;
    const GameEngine::Math::Vector3 directions[3] = {
        GameEngine::Math::Vector3::Right,
        GameEngine::Math::Vector3::Up,
        GameEngine::Math::Vector3::Forward,
    };
    layout.originX = content.x + projected.x;
    layout.originY = content.y + projected.y;
    for (int axis = 0; axis < 3; ++axis)
    {
        const GameEngine::Math::ViewportPoint tip = GameEngine::Math::ProjectToViewport(
            origin + directions[axis] * axisLength, viewProjection, width, height);
        layout.tipX[axis] = content.x + tip.x;
        layout.tipY[axis] = content.y + tip.y;
        // 시선과 나란한 축은 화면에서 점이 된다. 그런 축은 잡아도 방향을 말할 수 없으므로 숨긴다.
        const float dx = layout.tipX[axis] - layout.originX;
        const float dy = layout.tipY[axis] - layout.originY;
        layout.axisVisible[axis] = tip.isInFront &&
            (dx * dx + dy * dy) >= S(GizmoMinimumAxisPixels) * S(GizmoMinimumAxisPixels);
    }
    layout.visible = true;
    return layout;
}

void EditorSceneViewPanel::DrawGizmo(const GizmoLayout& layout)
{
    if (!layout.visible)
    {
        return;
    }
    const GameEngine::Math::Color axisColors[3] = {
        { 0.90f, 0.30f, 0.30f, 1.0f },
        { 0.35f, 0.85f, 0.35f, 1.0f },
        { 0.40f, 0.55f, 0.95f, 1.0f },
    };
    const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    const float handle = S(GizmoHandlePixels);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!layout.axisVisible[axis])
        {
            continue;
        }
        const bool held = mGizmoAxis == axes[axis];
        const GameEngine::Math::Color color = held
            ? GameEngine::Math::Color{ 1.0f, 0.95f, 0.4f, 1.0f }
            : axisColors[axis];
        mUI.DrawLine(
            layout.originX, layout.originY, layout.tipX[axis], layout.tipY[axis],
            S(held ? 3.0f : 2.0f), color);
        // 손잡이는 축 끝의 작은 사각형이다. 잡는 자리를 눈으로 알 수 있고, 히트 판정도 이것이다.
        mUI.DrawPanel(
            { layout.tipX[axis] - handle * 0.5f, layout.tipY[axis] - handle * 0.5f,
              handle, handle },
            color);
    }
}

void EditorSceneViewPanel::DrawWorldSegment(
    const UIRect& content,
    const GameEngine::Math::Vector3& worldStart, const GameEngine::Math::Vector3& worldEnd,
    const float thickness, const GameEngine::Math::Color& color)
{
    if (!mSceneViewSize.IsValid())
    {
        return;
    }
    const GameEngine::Math::Matrix4x4 viewProjection = GetViewProjection();
    const float width = static_cast<float>(mSceneViewSize.width);
    const float height = static_cast<float>(mSceneViewSize.height);
    const GameEngine::Math::ViewportPoint start =
        GameEngine::Math::ProjectToViewport(worldStart, viewProjection, width, height);
    const GameEngine::Math::ViewportPoint end =
        GameEngine::Math::ProjectToViewport(worldEnd, viewProjection, width, height);
    if (!start.isInFront || !end.isInFront)
    {
        return;
    }
    mUI.DrawLine(
        content.x + start.x, content.y + start.y,
        content.x + end.x, content.y + end.y,
        S(thickness), color);
}

void EditorSceneViewPanel::DrawWorldAabb(
    const UIRect& content, const GameEngine::Core::Aabb2D& worldBounds,
    const float z, const float thickness, const GameEngine::Math::Color& color)
{
    const GameEngine::Math::Vector3 corners[4] = {
        { worldBounds.min.GetX(), worldBounds.min.GetY(), z },
        { worldBounds.max.GetX(), worldBounds.min.GetY(), z },
        { worldBounds.max.GetX(), worldBounds.max.GetY(), z },
        { worldBounds.min.GetX(), worldBounds.max.GetY(), z },
    };
    for (int edge = 0; edge < 4; ++edge)
    {
        DrawWorldSegment(content, corners[edge], corners[(edge + 1) % 4], thickness, color);
    }
}

void EditorSceneViewPanel::DrawWorldAabb(
    const UIRect& content, const GameEngine::Core::Aabb3D& worldBounds,
    const float thickness, const GameEngine::Math::Color& color)
{
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
    for (const auto& edge : edges)
    {
        DrawWorldSegment(content, corners[edge[0]], corners[edge[1]], thickness, color);
    }
}

void EditorSceneViewPanel::DrawColliderGizmos(const UIRect& content)
{
    const GameEngine::Runtime::Scene* const scene = mContext.GetOpenScene();
    if (!scene || !mSceneViewSize.IsValid())
    {
        return;
    }

    const auto* const selected =
        dynamic_cast<const GameEngine::Runtime::GameObject*>(mContext.GetSelectedObject());
    const auto drawPass =
        [this, scene, selected, content](
            const bool selectedPass, const GameEngine::Math::Color& color,
            const float thickness)
        {
            for (const auto& objectEntry : scene->GetGameObjects())
            {
                const GameEngine::Runtime::GameObject* const gameObject = objectEntry.second.get();
                if (!gameObject || !gameObject->IsActiveInHierarchy() ||
                    (gameObject == selected) != selectedPass)
                {
                    continue;
                }

                const float z = gameObject->GetTransform().GetWorldPosition().GetZ();
                for (const GameEngine::Runtime::Collider2D* const collider :
                     gameObject->GetComponents<GameEngine::Runtime::Collider2D>())
                {
                    if (!collider || !collider->IsActiveAndEnabled())
                    {
                        continue;
                    }

                    const GameEngine::Core::Aabb2D worldBounds = collider->GetWorldBounds();
                    if (worldBounds.IsEmpty())
                    {
                        continue;
                    }
                    DrawWorldAabb(content, worldBounds, z, thickness, color);
                }

                for (const GameEngine::Runtime::Collider3D* const collider :
                     gameObject->GetComponents<GameEngine::Runtime::Collider3D>())
                {
                    if (!collider || !collider->IsActiveAndEnabled())
                    {
                        continue;
                    }

                    const GameEngine::Core::Aabb3D worldBounds = collider->GetWorldBounds();
                    if (!worldBounds.HasVolume())
                    {
                        continue;
                    }
                    DrawWorldAabb(content, worldBounds, thickness, color);
                }
            }
        };

    drawPass(false, ColliderGizmoColor, ColliderGizmoThickness);
    if (selected)
    {
        drawPass(true, SelectedColliderGizmoColor, SelectedColliderGizmoThickness);
    }
}

EditorSceneViewPanel::GizmoAxis EditorSceneViewPanel::PickGizmoAxis(
    const GizmoLayout& layout, const float pixelX, const float pixelY) const
{
    if (!layout.visible)
    {
        return GizmoAxis::None;
    }
    const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    // 손잡이보다 넉넉하게 잡는다: 정확히 몇 픽셀짜리 사각형을 맞히라고 요구할 이유가 없다.
    const float radius = S(GizmoHandlePixels);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!layout.axisVisible[axis])
        {
            continue;
        }
        const float dx = pixelX - layout.tipX[axis];
        const float dy = pixelY - layout.tipY[axis];
        if (dx * dx + dy * dy <= radius * radius)
        {
            return axes[axis];
        }
    }
    return GizmoAxis::None;
}

void EditorSceneViewPanel::DragGizmo(const float pixelX, const float pixelY)
{
    auto* const selected =
        dynamic_cast<GameEngine::Runtime::GameObject*>(mContext.GetSelectedObject());
    if (!selected || mGizmoAxis == GizmoAxis::None || !mSceneViewSize.IsValid())
    {
        return;
    }

    const int axisIndex = mGizmoAxis == GizmoAxis::X ? 0 : (mGizmoAxis == GizmoAxis::Y ? 1 : 2);
    const GameEngine::Math::Vector3 directions[3] = {
        GameEngine::Math::Vector3::Right,
        GameEngine::Math::Vector3::Up,
        GameEngine::Math::Vector3::Forward,
    };

    // 총 이동을 드래그 시작점에서 다시 잰다. 프레임마다의 증분을 누적하면 스냅이 걸릴 때마다
    // 버려진 나머지가 쌓여 커서와 물체가 어긋난다.
    GameEngine::Math::Vector3 startWorld = mGizmoStartPosition;
    if (const GameEngine::Runtime::Transform* const parent = selected->GetTransform().GetParent())
    {
        startWorld = parent->GetLocalToWorldMatrix().TransformPoint(mGizmoStartPosition);
    }
    const float distance = GameEngine::Math::ComputeAxisDragDistance(
        startWorld, directions[axisIndex], GetViewProjection(),
        static_cast<float>(mSceneViewSize.width), static_cast<float>(mSceneViewSize.height),
        pixelX - mGizmoDragStartX, pixelY - mGizmoDragStartY);

    // 축 방향의 이동은 부모가 회전해 있어도 월드 축을 따라야 하므로, 부모 공간으로 되돌린 뒤
    // 더한다. 부모가 없으면 로컬이 곧 월드다.
    GameEngine::Math::Vector3 localAxis = directions[axisIndex];
    if (const GameEngine::Runtime::Transform* const parent = selected->GetTransform().GetParent())
    {
        GameEngine::Math::Matrix4x4 worldToParent;
        if (parent->GetLocalToWorldMatrix().TryInvert(worldToParent))
        {
            localAxis = worldToParent.TransformDirection(directions[axisIndex]).Normalized();
        }
    }

    GameEngine::Math::Vector3 moved = mGizmoStartPosition + localAxis * distance;
    if (mContext.GetSettings().gridSnapEnabled)
    {
        // 움직인 축의 성분만 격자에 맞춘다. 나머지 두 축은 사람이 놓아둔 값 그대로 남는다.
        const float spacing = mContext.GetSettings().gridSnapSpacing;
        const float components[3] = { moved.GetX(), moved.GetY(), moved.GetZ() };
        float snapped[3] = { components[0], components[1], components[2] };
        snapped[axisIndex] = GameEngine::Math::SnapToGrid(components[axisIndex], spacing);
        moved = { snapped[0], snapped[1], snapped[2] };
    }

    // 쓰기는 인스펙터와 같은 길목을 지난다: 같은 병합 키를 쓰는 동안 드래그 전체가 undo 한
    // 단계로 합쳐지고, 값이 그대로면 길목이 기록을 건너뛴다.
    const GameEngine::Runtime::PropertyDescriptor* const descriptor =
        GameEngine::Runtime::FindProperty(
            GameEngine::Runtime::Transform::StaticType(), "position");
    if (descriptor)
    {
        mPropertyEdit.ApplyProperty(
            selected->GetTransform(), *descriptor,
            GameEngine::Runtime::PropertyValue{ moved }, mGizmoMergeKey);
    }
}

void EditorSceneViewPanel::Draw(const GameEngine::UI::UIRect content)
{
    const GameEngine::UI::ImageInteraction interaction = mUI.DrawInteractiveImage(
        GameEngine::UI::MakeWidgetId("scene-view"), content, mSceneImage);
    mSceneViewSize = {
        static_cast<unsigned int>((std::max)(content.width, 0.0f)),
        static_cast<unsigned int>((std::max)(content.height, 0.0f)) };

    // 도구에게 이번 프레임의 입력을 보여 주고, 그가 가져간 것을 뺀 나머지로 계속한다. 도구가
    // "내가 처리했다"고 할 때 여기서 반환해 버리면 도구가 쓰지도 않는 입력까지 함께 죽는다:
    // 타일 페인팅을 켜는 순간 팬도 돌리도 기즈모도 멈추고, 기즈모는 그려지지도 않으며, 카메라
    // 상태도 저장되지 않는다.
    SceneInputCapture captured;
    {
        SceneToolInput toolInput;
        MakeSceneRay(
            mUI.GetMouseX() - content.x, mUI.GetMouseY() - content.y,
            toolInput.rayOrigin, toolInput.rayDirection);
        toolInput.leftDragging = interaction.leftDragging;
        toolInput.middleDragging = interaction.middleDragging;
        toolInput.wheel = interaction.wheel;
        captured = mSceneToolHost.GetSceneTool().HandleSceneInput(toolInput);
    }
    const GameEngine::UI::ImageInteraction remaining =
        RemainingAfterCapture(interaction, captured);
    const bool leftDragging = remaining.leftDragging;
    const bool middleDragging = remaining.middleDragging;
    const float wheel = remaining.wheel;

    if (captured.leftButton)
    {
        // 도구가 왼쪽 버튼을 가져갔다. 진행 중이던 누름과 기즈모 잡기는 뗌이 아니라 취소로
        // 끝나야 한다 — 그러지 않으면 붓질을 시작한 프레임이 "거의 안 움직인 클릭"으로 읽혀
        // 객체 선택이 바뀌고, 잡고 있던 기즈모 축은 손을 놓은 것처럼 풀린다.
        mScenePressActive = false;
        mSceneDragDistance = 0.0f;
        mGizmoAxis = GizmoAxis::None;
        mGizmoMergeKey = 0;
    }

    // 왼쪽 드래그가 피벗 둘레 궤도 회전, 가운데 드래그가 팬, 휠이 피벗까지의 거리다.
    // 기즈모가 먼저다: 손잡이 위에서 시작한 드래그는 궤도가 아니라 이동이다.
    const GizmoLayout gizmo = ComputeGizmoLayout(content);
    const float mouseX = mUI.GetMouseX();
    const float mouseY = mUI.GetMouseY();
    if (leftDragging && mGizmoAxis == GizmoAxis::None && !mScenePressActive)
    {
        if (const GizmoAxis axis = PickGizmoAxis(gizmo, mouseX, mouseY); axis != GizmoAxis::None)
        {
            if (auto* const selected = dynamic_cast<GameEngine::Runtime::GameObject*>(
                    mContext.GetSelectedObject()))
            {
                mGizmoAxis = axis;
                mGizmoDragStartX = mouseX;
                mGizmoDragStartY = mouseY;
                mGizmoStartPosition = selected->GetTransform().GetPosition();
                // 이 드래그만의 병합 키다. 드래그 중의 모든 쓰기가 이 키를 쓰므로 undo 스택에는
                // 한 단계만 남고, 다음 드래그는 다음 번호를 받아 따로 되돌려진다.
                mGizmoMergeKey = GameEngine::UI::MakeWidgetId("scene-gizmo", ++mGizmoDragSequence);
            }
        }
    }
    if (mGizmoAxis != GizmoAxis::None)
    {
        if (leftDragging)
        {
            DragGizmo(mouseX, mouseY);
        }
        else
        {
            // 손을 놓았다. 다음 드래그는 새 undo 단계다.
            mGizmoAxis = GizmoAxis::None;
            mGizmoMergeKey = 0;
        }
    }
    else if (leftDragging)
    {
        if (!mScenePressActive)
        {
            // 누른 자리를 기억한다. 거의 움직이지 않고 떼면 궤도가 아니라 클릭 — 피킹이다.
            mScenePressActive = true;
            mScenePressX = mUI.GetMouseX() - content.x;
            mScenePressY = mUI.GetMouseY() - content.y;
            mSceneDragDistance = 0.0f;
        }
        mSceneDragDistance += std::abs(interaction.dragDeltaX) + std::abs(interaction.dragDeltaY);
        mYawDegrees += interaction.dragDeltaX * OrbitDegreesPerPixel;
        mPitchDegrees = std::clamp(
            mPitchDegrees + interaction.dragDeltaY * OrbitDegreesPerPixel,
            MinimumPitchDegrees, MaximumPitchDegrees);
    }
    else if (mScenePressActive)
    {
        if (mSceneDragDistance <= S(4.0f))
        {
            GameEngine::Math::Vector3 origin;
            GameEngine::Math::Vector3 direction;
            MakeSceneRay(mScenePressX, mScenePressY, origin, direction);
            mContext.SelectObject(PickObject(origin, direction));
            mUI.ClearFieldFocus();
        }
        mScenePressActive = false;
    }
    if (middleDragging)
    {
        // 화면에서 커서가 미는 방향으로 세상이 따라오도록, 피벗은 반대로 움직인다.
        const GameEngine::Math::Matrix4x4 rotation =
            GameEngine::Math::Matrix4x4::CreateRotationRollPitchYawDegrees(
                { mPitchDegrees, mYawDegrees, 0.0f });
        const GameEngine::Math::Vector3 right = rotation.TransformDirection(GameEngine::Math::Vector3::Right);
        const GameEngine::Math::Vector3 up = rotation.TransformDirection(GameEngine::Math::Vector3::Up);
        const float scale = mDistance * PanWorldPerPixelPerDistance;
        mPivot += (up * interaction.dragDeltaY - right * interaction.dragDeltaX) * scale;
    }
    if (wheel != 0.0f)
    {
        mDistance = std::clamp(
            mDistance * std::pow(DollyStepScale, wheel),
            MinimumDistance, MaximumDistance);
    }

    // 카메라 저장은 제스처가 끝난 프레임에 이뤄진다: 조작 중에는 값이 매 프레임 바뀌므로 쓰지
    // 않고, 조작이 없는 프레임의 이 호출은 값이 같으면 컨텍스트가 저장을 건너뛴다 — 그래서
    // 궤도·팬·돌리 하나가 끝날 때마다 정확히 한 번 디스크에 닿는다.
    DrawColliderGizmos(content);
    DrawGizmo(gizmo);

    if (!leftDragging && !middleDragging && wheel == 0.0f)
    {
        mContext.UpdateSceneCameraSetting(mPivot, mDistance, mYawDegrees, mPitchDegrees);
    }
}

}
