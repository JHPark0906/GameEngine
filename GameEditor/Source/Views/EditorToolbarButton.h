#pragma once

// editor-layer: 2 (Views)

#include <string>

#include "Rules/EditorToolbarLayout.h"

namespace GameEngine::Runtime
{
class Button;
class GameObject;
class RectTransform;
class Scene;
class TextRenderer;
}

namespace GameEditor
{

/// <summary>글자 상자를 왼쪽에서 들여 놓는 거리다. 글자가 테두리에 붙지 않게 한다.</summary>
inline constexpr float ToolbarLabelLeftInset = 8.0f;

/// <summary>
/// 글자를 재지 못했을 때 버튼이 갖는 폭이다. 첫 프레임과 글꼴을 열지 못한 화면이 그런
/// 자리이며, 이 값은 「무엇이 들어갈 만큼」이 아니라 「눌러 볼 수 있을 만큼」이다.
/// </summary>
inline constexpr float MinimumToolbarButtonWidth = ToolbarLayoutMetrics{}.minimumButtonWidth;

/// <summary>버튼 하나가 쥐는 조각들이다. 눌림은 Button이, 글자는 TextRenderer가 답한다.</summary>
struct ToolbarButtonParts
{
    GameEngine::Runtime::GameObject* object = nullptr;
    GameEngine::Runtime::RectTransform* rect = nullptr;
    GameEngine::Runtime::Button* button = nullptr;
    GameEngine::Runtime::TextRenderer* label = nullptr;
    /// <summary>글자가 사는 자식 사각형이다. 버튼 사각형에서 왼쪽으로만 들여져 있다.</summary>
    GameEngine::Runtime::RectTransform* labelRect = nullptr;
};

/// <summary>
/// 툴바 버튼 하나를 세운다. 배경·눌림·글자와, 그 글자에서 폭이 나오게 하는 배치 컴포넌트까지
/// 한자리에서 붙인다.
///
/// 시험에서도 이 함수를 불러 실제 버튼 구성을 세운다. 시험이 별도 트리를 조립하면
/// 제품의 버튼 구성 오류를 놓칠 수 있다. 이 함수는 엔진 UI 컴포넌트만 사용해 창 없이 부를 수 있다.
/// </summary>
/// <param name="scene">버튼이 놓일 장면이다.</param>
/// <param name="parent">버튼이 매달릴 부모 오브젝트다.</param>
/// <param name="label">버튼에 적힐 글자이며, 오브젝트의 이름이기도 하다.</param>
/// <param name="fontSize">글자 크기다. 폭이 여기서 나오므로 재는 쪽과 같은 값이어야 한다.</param>
[[nodiscard]] ToolbarButtonParts BuildToolbarButton(
    GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
    const std::string& label, float fontSize);

}
