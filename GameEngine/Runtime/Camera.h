#pragma once
#include "../Math/Color.h"
#include "../Math/Matrix.h"
#include "Behaviour.h"

#include <memory>

namespace GameEngine::Runtime
{

/// <summary>장면을 렌더링할 시점과 투영 정보를 제공한다.</summary>
class Camera final : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
    static constexpr float DefaultOrthographicSize = 5.0f;

    enum class ProjectionMode
    {
        Perspective,
        Orthographic
    };

    [[nodiscard]] ProjectionMode GetProjectionMode() const { return mProjectionMode; }
    void SetProjectionMode(ProjectionMode projectionMode) { mProjectionMode = projectionMode; }

    [[nodiscard]] float GetFieldOfView() const { return mFieldOfView; }
    void SetFieldOfView(float fieldOfView);
    [[nodiscard]] float GetAspectRatio() const { return mAspectRatio; }
    void SetAspectRatio(float aspectRatio);
    [[nodiscard]] float GetNearClipPlane() const { return mNearClipPlane; }
    void SetNearClipPlane(float nearClipPlane);
    [[nodiscard]] float GetFarClipPlane() const { return mFarClipPlane; }
    void SetFarClipPlane(float farClipPlane);
    [[nodiscard]] float GetOrthographicSize() const { return mOrthographicSize; }
    void SetOrthographicSize(float orthographicSize);

    [[nodiscard]] int GetPriority() const { return mPriority; }
    void SetPriority(int priority) { mPriority = priority; }
    [[nodiscard]] const Math::Color& GetClearColor() const { return mClearColor; }
    void SetClearColor(const Math::Color& clearColor) { mClearColor = clearColor; }

    [[nodiscard]] Math::Matrix4x4 GetViewMatrix() const;
    [[nodiscard]] Math::Matrix4x4 GetProjectionMatrix() const;

private:
    void OnPropertiesRestored() noexcept override;

    ProjectionMode mProjectionMode = ProjectionMode::Perspective;
    Math::Color mClearColor{ 0.1f, 0.1f, 0.1f, 1.0f };
    float mFieldOfView = 60.0f;
    float mAspectRatio = 16.0f / 9.0f;
    float mNearClipPlane = 0.1f;
    float mFarClipPlane = 1000.0f;
    float mOrthographicSize = DefaultOrthographicSize;
    int mPriority = 0;
};

}
