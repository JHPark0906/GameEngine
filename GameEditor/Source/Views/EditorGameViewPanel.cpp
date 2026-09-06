#include "Views/EditorGameViewPanel.h"

#include <algorithm>

#include "Document/EditorContext.h"
#include "Rules/EditorPanelCommon.h"

namespace GameEditor
{

EditorGameViewPanel::EditorGameViewPanel(
    EditorContext& context, GameEngine::UI::UIContext& ui)
    : mContext(context), mUI(ui)
{
}

void EditorGameViewPanel::Draw(const GameEngine::UI::UIRect content)
{
    static_cast<void>(mUI.DrawInteractiveImage(
        GameEngine::UI::MakeWidgetId("game-view"), content, mGameImage));
    // 잡고 있는 동안 테두리를 두른다. 보이는 신호가 없으면 사람은 "키가 안 먹는다"와 "아직
    // 포커스를 안 잡았다"를 가를 수 없고, 그 둘은 고치는 법이 다르다.
    if (mContext.IsPlayInputCaptured())
    {
        const float thickness = PlayFocusBorderThickness * mUI.GetScale();
        const float half = thickness * 0.5f;
        const float left = content.x + half;
        const float top = content.y + half;
        const float right = content.GetRight() - half;
        const float bottom = content.GetBottom() - half;
        mUI.DrawLine(left, top, right, top, thickness, PlayFocusColor);
        mUI.DrawLine(right, top, right, bottom, thickness, PlayFocusColor);
        mUI.DrawLine(right, bottom, left, bottom, thickness, PlayFocusColor);
        mUI.DrawLine(left, bottom, left, top, thickness, PlayFocusColor);
    }
    mViewRect = content;
    mGameViewSize = {
        static_cast<unsigned int>((std::max)(content.width, 0.0f)),
        static_cast<unsigned int>((std::max)(content.height, 0.0f)) };
}

void EditorGameViewPanel::PresentImage(const GameEngine::Rendering::CapturedImage& image)
{
    mUI.UpdateDynamicTexture(mGameImage, image.width, image.height, image.pixels);
}

}
