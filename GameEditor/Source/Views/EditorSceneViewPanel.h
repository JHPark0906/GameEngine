#pragma once

// editor-layer: 2 (Views)

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "Assets/TextureData.h"
#include "Math/Aabb2D.h"
#include "Math/Aabb3D.h"
#include "Math/Color.h"
#include "Math/Vector.h"
#include "Rendering/RenderFrame.h"
#include "UI/UIContext.h"
#include "Rules/EditorPanelHosts.h"

namespace GameEngine::Runtime
{
class GameObject;
}

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 씬 뷰 패널이다: 편집 카메라가 본 장면의 캡처 이미지를 보이고, 그 위의 드래그가 궤도·팬·돌리,
/// 거의 움직이지 않은 클릭이 피킹이다. 궤도 카메라 상태와 피킹의 경계 캐시를 자신이 소유하고,
/// 캡처 계약 — 뷰 크기, 편집 카메라, 이미지 받기 — 은 셸이 이 패널에 물어 답한다.
/// </summary>
class EditorSceneViewPanel final
{
public:
    EditorSceneViewPanel(IEditorScale& scale, IPropertyEditHost& propertyEdit, ISceneToolHost& sceneToolHost, EditorContext& context, GameEngine::UI::UIContext& ui);

    /// <summary>패널 내용을 그린다. 제목줄 프레임은 셸이 이미 그렸고, 그 아래 영역을 받는다.</summary>
    void Draw(GameEngine::UI::UIRect content);

    /// <summary>씬 뷰가 이번 프레임에 캡처받을 픽셀 크기이다. 첫 프레임 전에는 무효다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderTargetSize GetViewSize() const
    {
        return mSceneViewSize;
    }

    /// <summary>씬 뷰의 편집 카메라이다. 궤도 상태 — 피벗, 거리, 요, 피치 — 에서 만든다.</summary>
    [[nodiscard]] GameEngine::Rendering::CameraRenderData GetCamera() const;

    /// <summary>캡처된 장면 이미지를 다음 프레임의 뷰가 보일 텍스처로 받는다.</summary>
    void PresentImage(const GameEngine::Rendering::CapturedImage& image);

private:
    /// <summary>객체 로컬 공간의 축 정렬 경계 상자다. 피킹이 이것에 광선을 쏜다.</summary>
    struct LocalBounds
    {
        float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
        float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    };

    /// <summary>96 DPI 기준의 논리 길이를 이 화면의 픽셀로 바꾼다. 셸의 배율을 따른다.</summary>
    [[nodiscard]] float S(float logical) const;

    /// <summary>씬 뷰 픽셀을 지나는 월드 광선이다. 편집 카메라의 위치와 방향에서 만든다.</summary>
    void MakeSceneRay(
        float pixelX, float pixelY,
        GameEngine::Math::Vector3& origin, GameEngine::Math::Vector3& direction) const;
    /// <summary>광선에 가장 가까이 맞는 열린 장면의 객체다. 없으면 0이다.</summary>
    [[nodiscard]] unsigned int PickObject(
        const GameEngine::Math::Vector3& origin, const GameEngine::Math::Vector3& direction);
    /// <summary>객체가 그리는 것의 로컬 경계다. 그리는 것이 없으면 false다.</summary>
    [[nodiscard]] bool GetLocalBounds(
        const GameEngine::Runtime::GameObject& gameObject, LocalBounds& bounds);

    IEditorScale& mScale;
    IPropertyEditHost& mPropertyEdit;
    ISceneToolHost& mSceneToolHost;
    EditorContext& mContext;
    GameEngine::UI::UIContext& mUI;

    /// <summary>
    /// 캡처된 뷰 이미지를 담는 텍스처다. 캡처가 올 때마다 픽셀과 revision만 바뀐다 — 그래서
    /// 백엔드는 프레임마다 새 리소스를 만들지 않는다.
    /// </summary>
    std::shared_ptr<GameEngine::Assets::TextureData> mSceneImage;
    /// <summary>마지막 프레임에 레이아웃이 준 뷰 콘텐츠 크기이다. 다음 캡처가 이 크기로 온다.</summary>
    GameEngine::Rendering::RenderTargetSize mSceneViewSize;

    // 씬 뷰의 궤도 카메라 상태. 장면의 컴포넌트가 아니므로 장면에 저장되지 않는다.
    GameEngine::Math::Vector3 mPivot{ 0.0f, 1.0f, 5.0f };
    float mDistance = 12.0f;
    float mYawDegrees = 0.0f;
    float mPitchDegrees = 15.0f;

    // 씬 뷰 클릭 판정: 누른 자리와 그 뒤 움직인 거리. 거의 안 움직였으면 궤도가 아니라 피킹이다.
    //
    // 이것은 <c>GameEditor::DragGesture</c>와 다른 손짓이라 그것을 쓰지 않는다. 저쪽이 묻는 것은
    // "시작점에서 얼마나 멀어졌는가"이고 여기서 묻는 것은 "커서가 얼마나 돌아다녔는가"다:
    // 거리는 프레임마다 누적되므로 한 바퀴 돌아 제자리로 온 드래그도 드래그로 남는다. 회전은
    // 문턱과 무관하게 첫 프레임부터 적용되고, 이 거리는 뗄 때 "그래서 이건 클릭이었나"만
    // 가른다.
    bool mScenePressActive = false;
    float mScenePressX = 0.0f;
    float mScenePressY = 0.0f;
    float mSceneDragDistance = 0.0f;
    /// <summary>메시 id별 로컬 경계 캐시다. 정점을 매 클릭마다 다시 훑지 않는다.</summary>
    std::unordered_map<std::uint64_t, LocalBounds> mMeshBounds;

    // ---- 이동 기즈모
    //
    // 기즈모는 캡처된 프레임 위에 UI로 얹는다. 3D로 그리려면 편집 카메라용 프론트엔드와 그
    // 기하를 따로 두어야 하는데, 축 세 개와 손잡이를 위해 렌더 경로를 하나 더 만드는 값이다.
    // UI 오버레이면 이미 있는 것들 — 편집 카메라, 화면 투영, UIContext의 선과 사각형 — 만으로
    // 끝나고, 피킹도 화면 좌표에서 곧장 판정된다.

    /// <summary>기즈모가 잡을 수 있는 축이다. None은 아무것도 잡지 않은 상태다.</summary>
    enum class GizmoAxis : unsigned char
    {
        None,
        X,
        Y,
        Z,
    };

    /// <summary>이번 프레임의 기즈모 화면 배치다. 그리기와 히트 판정이 같은 값을 본다.</summary>
    struct GizmoLayout
    {
        bool visible = false;
        /// <summary>기즈모 원점 — 선택된 객체의 월드 위치 — 의 화면 좌표다.</summary>
        float originX = 0.0f;
        float originY = 0.0f;
        /// <summary>축마다의 손잡이 끝점 화면 좌표다. X, Y, Z 순이다.</summary>
        float tipX[3] = {};
        float tipY[3] = {};
        /// <summary>축이 화면에서 보이는지다. 시선과 나란한 축은 잡을 수 없다.</summary>
        bool axisVisible[3] = {};
    };

    /// <summary>편집 카메라의 뷰·투영을 곱한 행렬이다. 투영과 드래그 변환이 이것을 쓴다.</summary>
    [[nodiscard]] GameEngine::Math::Matrix4x4 GetViewProjection() const;
    /// <summary>선택된 객체의 기즈모 배치를 이번 프레임의 뷰 크기로 계산한다.</summary>
    [[nodiscard]] GizmoLayout ComputeGizmoLayout(const GameEngine::UI::UIRect& content) const;
    /// <summary>기즈모를 그린다. 잡고 있는 축은 밝게 보인다.</summary>
    void DrawGizmo(const GizmoLayout& layout);

    /// <summary>
    /// 열린 장면의 활성 2D·3D 콜라이더를 물리 판정에 쓰는 월드 AABB로 그린다. 선택된 오브젝트는
    /// 마지막에 더 밝게 그려, 다른 윤곽선에 가려지지 않는다.
    /// </summary>
    void DrawColliderGizmos(const GameEngine::UI::UIRect& content);

    /// <summary>
    /// 월드 공간의 두 점을 잇는 선을 씬 뷰에 그린다. 이동 기즈모와 콜라이더 윤곽선이
    /// 같은 투영을 사용하도록 이 함수에서 처리한다.
    ///
    /// 두 끝점 중 하나라도 카메라 뒤에 있으면 그리지 않는다 — 카메라 뒤의 점을 투영한 화면
    /// 좌표는 뜻이 없고, 그것으로 선을 그으면 화면을 가로지르는 엉뚱한 선이 나온다.
    /// </summary>
    /// <param name="content">투영 좌표가 놓이는 씬 뷰 이미지의 전체 UI 좌표 사각형이다.</param>
    /// <param name="worldStart">선의 시작점, 월드 공간이다.</param>
    /// <param name="worldEnd">선의 끝점, 월드 공간이다.</param>
    /// <param name="thickness">선의 굵기다. 논리 픽셀이며 이 메서드가 <c>S()</c>로 배율을 곱한다.</param>
    /// <param name="color">선의 색이다.</param>
    void DrawWorldSegment(
        const GameEngine::UI::UIRect& content,
        const GameEngine::Math::Vector3& worldStart, const GameEngine::Math::Vector3& worldEnd,
        float thickness, const GameEngine::Math::Color& color);

    /// <summary>
    /// 월드 공간의 축 정렬 사각형을 씬 뷰에 윤곽선으로 그린다. 네 모서리를 만들어
    /// <see cref="DrawWorldSegment"/> 넷으로 두른다 — 콜라이더의 <c>GetWorldBounds()</c>가
    /// 이미 이 모양(XY 평면의 <c>Math::Aabb2D</c>)으로 답하므로, 그 값을 그대로 받는다.
    /// </summary>
    /// <param name="content">투영 좌표가 놓이는 씬 뷰 이미지의 전체 UI 좌표 사각형이다.</param>
    /// <param name="worldBounds">월드 공간의 XY 사각형이다.</param>
    /// <param name="z">사각형이 놓일 월드 Z다. 2D 콜라이더는 자기 Z를 모르므로 호출자가 준다.</param>
    /// <param name="thickness">선의 굵기다. 논리 픽셀이며 이 메서드가 <c>S()</c>로 배율을 곱한다.</param>
    /// <param name="color">선의 색이다.</param>
    void DrawWorldAabb(
        const GameEngine::UI::UIRect& content, const GameEngine::Math::Aabb2D& worldBounds,
        float z, float thickness, const GameEngine::Math::Color& color);

    /// <summary>
    /// 월드 공간의 축 정렬 입체를 씬 뷰에 윤곽선으로 그린다. 여덟 꼭짓점과 열두 변은
    /// <see cref="DrawWorldSegment"/>가 투영한다 — 3D 콜라이더의 <c>GetWorldBounds()</c>가
    /// 이미 물리 판정에 쓰는 <c>Math::Aabb3D</c>이므로, 그 값을 그대로 받는다.
    /// </summary>
    /// <param name="content">투영 좌표가 놓이는 씬 뷰 이미지의 전체 UI 좌표 사각형이다.</param>
    /// <param name="worldBounds">월드 공간의 축 정렬 입체다.</param>
    /// <param name="thickness">선의 굵기다. 논리 픽셀이며 이 메서드가 <c>S()</c>로 배율을 곱한다.</param>
    /// <param name="color">선의 색이다.</param>
    void DrawWorldAabb(
        const GameEngine::UI::UIRect& content, const GameEngine::Math::Aabb3D& worldBounds,
        float thickness, const GameEngine::Math::Color& color);

    /// <summary>화면 좌표가 어느 축의 손잡이 위인지다. 아무 데도 아니면 None이다.</summary>
    [[nodiscard]] GizmoAxis PickGizmoAxis(
        const GizmoLayout& layout, float pixelX, float pixelY) const;
    /// <summary>
    /// 잡고 있는 축을 따라 객체를 옮긴다. 드래그가 시작된 자리의 위치를 기준으로 커서까지의
    /// 총 이동을 계산하므로, 스냅이 켜져 있어도 오차가 프레임마다 쌓이지 않는다.
    /// </summary>
    void DragGizmo(float pixelX, float pixelY);

    /// <summary>
    /// 지금 잡고 있는 축이다. 잡은 동안에는 궤도 회전이 일어나지 않는다.
    ///
    /// 손잡이를 잡는 데에는 문턱이 없다 — 그래서 이것도 <c>GameEditor::DragGesture</c>가 아니다.
    /// 손잡이 위에서 누르는 것 자체가 이미 그 축을 고르겠다는 뜻이고, 움직이기 전에 몇 픽셀을
    /// 요구하면 그만큼의 미세 조정을 할 수 없게 된다.
    /// </summary>
    GizmoAxis mGizmoAxis = GizmoAxis::None;
    /// <summary>드래그가 시작된 화면 좌표다. 총 이동량은 여기서부터 잰다.</summary>
    float mGizmoDragStartX = 0.0f;
    float mGizmoDragStartY = 0.0f;
    /// <summary>드래그가 시작될 때 객체가 있던 로컬 위치다. 매 프레임 이 값에서 다시 더한다.</summary>
    GameEngine::Math::Vector3 mGizmoStartPosition;
    /// <summary>
    /// 이 드래그의 undo 병합 키다. 드래그 하나가 undo 한 단계가 되는 근거이며, 드래그가 끝나면
    /// 다음 드래그는 다른 키를 받는다.
    /// </summary>
    std::uint64_t mGizmoMergeKey = 0;
    /// <summary>다음 드래그가 받을 번호다. 병합 키를 서로 다르게 만든다.</summary>
    std::uint64_t mGizmoDragSequence = 0;
};

}
