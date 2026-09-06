#pragma once

#include "../Platform/ITextRasterizer.h"
#include "RectTransform.h"

namespace GameEngine::Runtime
{

class GameObject;
class TextRenderer;

/// <summary>UI 입력 측정과 장면 렌더링이 공유하는 텍스트 배율·정렬 규칙이다.</summary>
[[nodiscard]] float GetTextCanvasScale(const GameObject& object);

// 폰트 크기와 자동 줄바꿈 폭을 실제 화면 픽셀로 맞춘 요청이다. 측정과 그리기가 공유한다.
[[nodiscard]] Platform::TextRasterizationRequest MakeScreenTextRequest(
    const GameObject& object, const TextRenderer& renderer);

// rect와 측정된 width/height는 모두 화면 픽셀이다. 반환값은 텍스트 블록의 좌상단이다.
[[nodiscard]] Math::Vector2 GetScreenTextOrigin(
    const TextRenderer& renderer, const RectTransform::Rect& rect, float width, float height);

}
