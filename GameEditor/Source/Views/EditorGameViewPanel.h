#pragma once

// editor-layer: 2 (Views)

#include <memory>

#include "Assets/TextureData.h"
#include "Rendering/RenderFrame.h"
#include "UI/UIContext.h"

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 게임 뷰 패널이다: 프로젝트 런타임이 자기 카메라로 그린 캡처 이미지를 보인다. 조작은 없다 —
/// 이미지 위의 상호작용은 소비만 하고 아무 데도 쓰지 않는다.
/// </summary>
class EditorGameViewPanel final
{
public:
    EditorGameViewPanel(EditorContext& context, GameEngine::UI::UIContext& ui);

    /// <summary>패널 내용을 그린다. 제목줄 프레임은 셸이 이미 그렸고, 그 아래 영역을 받는다.</summary>
    void Draw(GameEngine::UI::UIRect content);

    /// <summary>
    /// 게임 뷰가 이번 프레임에 그려진 자리다. 물리 픽셀이며 원점은 그리는 면의 좌상단이다 —
    /// 창 입력의 커서와 같은 단위라 그대로 비교할 수 있다. 첫 프레임 전에는 넓이가 0이다.
    ///
    /// 자리를 기억하는 이유는 Play 중 입력을 누가 받는지가 이 사각형으로 갈리기 때문이다:
    /// 여기를 클릭하면 게임이 입력을 쥐고, 커서 좌표도 이 원점 기준으로 옮겨진다.
    /// </summary>
    [[nodiscard]] GameEngine::UI::UIRect GetViewRect() const { return mViewRect; }

    /// <summary>게임 뷰가 이번 프레임에 캡처받을 픽셀 크기이다. 첫 프레임 전에는 무효다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderTargetSize GetViewSize() const
    {
        return mGameViewSize;
    }

    /// <summary>캡처된 게임 이미지를 다음 프레임의 뷰가 보일 텍스처로 받는다.</summary>
    void PresentImage(const GameEngine::Rendering::CapturedImage& image);

private:
    EditorContext& mContext;
    GameEngine::UI::UIContext& mUI;

    GameEngine::UI::UIRect mViewRect;

    /// <summary>
    /// 캡처된 뷰 이미지를 담는 텍스처다. 캡처가 올 때마다 픽셀과 revision만 바뀐다 — 그래서
    /// 백엔드는 프레임마다 새 리소스를 만들지 않는다.
    /// </summary>
    std::shared_ptr<GameEngine::Assets::TextureData> mGameImage;
    /// <summary>마지막 프레임에 레이아웃이 준 뷰 콘텐츠 크기이다. 다음 캡처가 이 크기로 온다.</summary>
    GameEngine::Rendering::RenderTargetSize mGameViewSize;
};

}
