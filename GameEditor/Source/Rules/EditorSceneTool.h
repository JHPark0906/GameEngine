#pragma once

// editor-layer: 0 (Rules)

#include "Math/Vector.h"
#include "UI/UIContext.h"

namespace GameEditor
{

/// <summary>
/// 씬 뷰의 한 프레임 입력 중 도구가 가져간 것들이다.
///
/// 도구는 가져간 입력만 표시하고, 씬 뷰는 나머지를 카메라 조작과 피킹에 사용한다.
/// 왼쪽 버튼, 가운데 버튼, 휠을 각각 표현해 한 입력의 소비가 다른 입력을 막지 않는다.
/// </summary>
struct SceneInputCapture
{
    /// <summary>왼쪽 드래그와 그 뗌이다. 붓질, 기즈모, 궤도 회전, 클릭 피킹이 다투는 자리다.</summary>
    bool leftButton = false;
    /// <summary>가운데 드래그다. 씬 뷰에서는 팬이다.</summary>
    bool middleButton = false;
    /// <summary>휠 눈금이다. 씬 뷰에서는 돌리다.</summary>
    bool wheel = false;
};

/// <summary>씬 뷰가 한 프레임에 도구에게 건네는 입력이다.</summary>
struct SceneToolInput
{
    /// <summary>커서를 지나는 월드 광선의 시작점이다.</summary>
    GameEngine::Math::Vector3 rayOrigin;
    /// <summary>그 광선의 방향이다.</summary>
    GameEngine::Math::Vector3 rayDirection;
    /// <summary>왼쪽 버튼이 눌린 채인지다.</summary>
    bool leftDragging = false;
    /// <summary>가운데 버튼이 눌린 채인지다.</summary>
    bool middleDragging = false;
    /// <summary>이번 프레임의 휠 눈금이다.</summary>
    float wheel = 0.0f;
};

/// <summary>
/// 씬 뷰의 입력을 나눠 갖는 도구다.
///
/// 씬 뷰가 도구를 구체 타입으로 알지 않게 하는 것이 이 인터페이스의 값이다. 그러지 않으면
/// 캡처는 이름만 바뀐 bool이고, 씬 뷰는 여전히 타일맵을 안다.
/// </summary>
class ISceneTool
{
public:
    virtual ~ISceneTool() = default;

    ISceneTool(const ISceneTool&) = delete;
    ISceneTool& operator=(const ISceneTool&) = delete;
    ISceneTool(ISceneTool&&) = delete;
    ISceneTool& operator=(ISceneTool&&) = delete;

    /// <summary>
    /// 이번 프레임의 씬 뷰 입력을 본다. 쓰지 않는 입력은 가져가지 않는다 — 가져가지 않은 것은
    /// 씬 뷰의 카메라 조작과 피킹이 평소대로 쓴다.
    /// </summary>
    /// <param name="input">이번 프레임의 커서 광선과 버튼 상태다.</param>
    /// <returns>이 도구가 소비한 입력들이다.</returns>
    [[nodiscard]] virtual SceneInputCapture HandleSceneInput(const SceneToolInput& input) = 0;

protected:
    ISceneTool() = default;
};

/// <summary>
/// 도구가 가져간 것을 뺀, 씬 뷰가 이번 프레임에 쓸 수 있는 입력이다.
///
/// 이 함수가 규칙 그 자체다: 가져간 것만 사라지고 나머지는 그대로다. 도구가 "내가 처리했다"고
/// 할 때 씬 뷰가 통째로 반환해 버리면, 붓이 왼쪽 버튼 하나를 원하는 동안 팬과 돌리와 기즈모가
/// 함께 멎는다.
///
/// 드래그 이동량은 걸러지지 않는다. 커서의 이동은 어느 버튼의 것도 아니고, 왼쪽이 도구에게
/// 갔더라도 가운데 드래그의 팬은 같은 이동량을 그대로 써야 한다.
/// </summary>
/// <param name="interaction">씬 뷰 위젯이 이번 프레임에 관찰한 입력이다.</param>
/// <param name="captured">도구가 소비한 입력들이다.</param>
/// <returns>씬 뷰의 카메라 조작과 피킹이 쓸 나머지 입력이다.</returns>
[[nodiscard]] inline GameEngine::UI::ImageInteraction RemainingAfterCapture(
    const GameEngine::UI::ImageInteraction& interaction, const SceneInputCapture& captured)
{
    GameEngine::UI::ImageInteraction remaining = interaction;
    if (captured.leftButton)
    {
        remaining.leftDragging = false;
    }
    if (captured.middleButton)
    {
        remaining.middleDragging = false;
    }
    if (captured.wheel)
    {
        remaining.wheel = 0.0f;
    }
    return remaining;
}

}
