#include "pch.h"
#include "Camera.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "Transform.h"
#include "../Diagnostics/Debug.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Camera가 선언하는 속성들이다. 이름은 장면 파일 형식이므로 기존 파일이 읽히는 그대로다.
    /// aspectRatio는 없다: 매 프레임 렌더 타깃 비율로 덮어써지는 런타임 소유 상태라서, 속성으로
    /// 내놓으면 저장하는 순간의 창 크기가 파일에 구워질 뿐이다.
    /// </summary>
    std::span<const PropertyDescriptor> CameraProperties()
    {
        static const PropertyDescriptor properties[] = {
            // 파일의 표현은 bool이고 런타임 표현은 ProjectionMode다. 이름이 파일을 따르고
            // 접근자가 변환하는, 서술자 주석이 말하는 바로 그 경우다.
            MakeProperty<Camera>(
                "orthographic", "Orthographic",
                [](const Camera& camera)
                {
                    return camera.GetProjectionMode() == Camera::ProjectionMode::Orthographic;
                },
                [](Camera& camera, const bool orthographic)
                {
                    camera.SetProjectionMode(orthographic
                        ? Camera::ProjectionMode::Orthographic
                        : Camera::ProjectionMode::Perspective);
                }),
            MakeProperty<Camera>(
                "fieldOfView", "Field Of View", &Camera::GetFieldOfView, &Camera::SetFieldOfView),
            MakeProperty<Camera>(
                "nearClipPlane", "Near Clip Plane",
                &Camera::GetNearClipPlane, &Camera::SetNearClipPlane),
            MakeProperty<Camera>(
                "farClipPlane", "Far Clip Plane",
                &Camera::GetFarClipPlane, &Camera::SetFarClipPlane),
            MakeProperty<Camera>(
                "orthographicSize", "Orthographic Size",
                &Camera::GetOrthographicSize, &Camera::SetOrthographicSize),
            MakeProperty<Camera>("priority", "Priority", &Camera::GetPriority, &Camera::SetPriority),
            MakeProperty<Camera>(
                "clearColor", "Clear Color", &Camera::GetClearColor, &Camera::SetClearColor),
        };
        return properties;
    }
}

const ComponentType& Camera::StaticType()
{
    static const ComponentType type{
        "Camera", &Behaviour::StaticType(), &CameraProperties, &MakeComponentInstance<Camera> };
    return type;
}
void Camera::SetFieldOfView(const float fieldOfView)
{
    mFieldOfView = std::clamp(fieldOfView, 1.0f, 179.0f);
}

void Camera::SetAspectRatio(const float aspectRatio)
{
    mAspectRatio = (std::max)(aspectRatio, 0.001f);
}

void Camera::SetNearClipPlane(const float nearClipPlane)
{
    const float nearPlane = std::isfinite(nearClipPlane) ? (std::max)(nearClipPlane, 0.001f) : 0.001f;
    mNearClipPlane = IsRestoringProperties()
        ? nearPlane : (std::min)(nearPlane, mFarClipPlane - 0.001f);
}

void Camera::SetFarClipPlane(const float farClipPlane)
{
    const float farPlane = std::isfinite(farClipPlane) ? (std::max)(farClipPlane, 0.002f) : 0.002f;
    mFarClipPlane = IsRestoringProperties()
        ? farPlane : (std::max)(farPlane, mNearClipPlane + 0.001f);
}

void Camera::OnPropertiesRestored() noexcept
{
    mFarClipPlane = (std::max)(mFarClipPlane, mNearClipPlane + 0.001f);
}

void Camera::SetOrthographicSize(const float orthographicSize)
{
    mOrthographicSize = (std::max)(orthographicSize, 0.001f);
}

Math::Matrix4x4 Camera::GetViewMatrix() const
{
    if (!GetGameObject())
    {
        return Math::Matrix4x4::Identity();
    }

    Math::Matrix4x4 view;
    if (!GetGameObject()->GetTransform().GetLocalToWorldMatrix().TryInvert(view))
    {
        Diagnostics::Debug::LogError("Cannot create a camera view matrix from a non-invertible Transform.");
        return Math::Matrix4x4::Identity();
    }
    return view;
}

Math::Matrix4x4 Camera::GetProjectionMatrix() const
{
    if (mProjectionMode == ProjectionMode::Orthographic)
    {
        return Math::Matrix4x4::CreateOrthographicLeftHanded(
            mOrthographicSize * 2.0f * mAspectRatio,
            mOrthographicSize * 2.0f,
            mNearClipPlane,
            mFarClipPlane);
    }

    return Math::Matrix4x4::CreatePerspectiveFieldOfViewLeftHanded(
        mFieldOfView,
        mAspectRatio,
        mNearClipPlane,
        mFarClipPlane);
}

}
