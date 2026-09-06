#pragma once

// editor-layer: 2 (Views)

#include <string>

#include "UI/UIContext.h"

namespace GameEngine::Runtime
{
class Button;
class GameObject;
class RectTransform;
class Scene;
class TextRenderer;
class UIWindow;
}

namespace GameEditor
{

/// <summary>
/// 도킹 칸을 떠나 화면 위에 뜬 패널의 껍데기다: 바탕 그림, 제목줄, 그리고 창이라는 표시.
///
/// 껍데기만 유지 모드다. 패널의 몸통 — 콘솔의 로그 목록처럼 매 프레임 다시 그려지는 내용 —
/// 은 즉시 모드로 남는다. 로그 200줄을 <c>TextRenderer</c> 200개로 세우는 것은 이 토대가
/// 요구하는 것도 아니고 값도 없다. 그 둘이 만나는 자리는 <see cref="GetContentRect"/> 하나이며,
/// 셸이 그 사각형에 몸통을 그린다.
///
/// 즉시 모드끼리의 순서에는 새 규칙이 필요 없다. 셸이 도킹 패널들을 먼저 선언하고 이 창의
/// 몸통을 나중에 선언하면, "커서 아래 마지막으로 선언된 것이 포인터를 갖는다"가 그대로 이
/// 창에 우선권을 준다.
///
/// 창의 사각형은 이 뷰가 논리 픽셀로 들고, <see cref="Synchronize"/>가 그것을
/// <c>RectTransform</c>의 앵커와 오프셋으로 옮긴다. 사각형을 표현하는 새 규약을 만들지
/// 않으므로, Rect 모델이 바뀌면 고칠 자리는 그 한 함수다.
/// </summary>
class EditorFloatingPanelView final
{
public:
    /// <summary>창을 떠 있는 층 아래에 세운다. 한 번만 부른다.</summary>
    /// <param name="scene">에디터 프로세스 자신의 런타임 장면이다.</param>
    /// <param name="layer">떠 있는 창들이 모이는 층이다. 창끼리의 순서가 이 층의 형제 순서다.</param>
    /// <param name="title">제목줄에 적힐 이름이다.</param>
    void Build(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& layer,
        const std::string& title);

    /// <summary>
    /// 프레임마다 자리와 글꼴을 맞춘다. 배치 전에 부른다 — 여기서 세운 사각형을 배치가 푼다.
    /// </summary>
    /// <param name="contentScale">창의 콘텐츠 배율이다. 논리 픽셀을 화면 픽셀로 옮긴다.</param>
    void Synchronize(float contentScale);

    /// <summary>떠 있는지다. 도킹으로 돌아가면 껍데기는 꺼진다.</summary>
    [[nodiscard]] bool IsVisible() const { return mVisible; }
    void SetVisible(bool visible);

    /// <summary>창의 자리다. 논리 픽셀이며 좌상단 기준이다.</summary>
    [[nodiscard]] GameEngine::UI::UIRect GetRect() const { return mRect; }
    void SetRect(const GameEngine::UI::UIRect& rect) { mRect = rect; }

    /// <summary>
    /// 몸통이 그려질 자리다. 제목줄 아래이며, 셸의 배율이 곱해진 화면 픽셀로 답한다.
    /// </summary>
    /// <param name="contentScale">창의 콘텐츠 배율이다.</param>
    [[nodiscard]] GameEngine::UI::UIRect GetContentRect(float contentScale) const;

    /// <summary>
    /// 제목줄이 눌리기 시작했다는 신호를 가져간다. 한 번 가져가면 사라진다.
    ///
    /// 신호를 세우는 쪽(<see cref="Synchronize"/>)과 읽는 쪽(셸의 끌기)은 프레임의 다른
    /// 단계에 있다. 세우는 쪽이 한 프레임에 두 번 실행되더라도 첫 누름 신호를 지우면 안 된다.
    ///
    /// 그래서 이것은 상태가 아니라 <b>걸쇠</b>다. 세워지면 누가 가져갈 때까지 남고, 가져가는
    /// 순간 사라진다 — 단계의 순서나 횟수가 답을 바꾸지 않는다.
    /// </summary>
    [[nodiscard]] bool ConsumeTitleBarPressStart();

    /// <summary>이번 프레임에 제목줄의 Dock 버튼이 눌렸는지다. 도킹 칸으로 돌아가라는 뜻이다.</summary>
    [[nodiscard]] bool WasDockRequested() const;

    /// <summary>이 창을 형제들 중 맨 뒤로 — 곧 맨 위로 — 옮긴다.</summary>
    void Raise();

    /// <summary>이 창이 모달인지 세운다. 모달인 동안 다른 창의 요소는 커서를 받지 못한다.</summary>
    void SetModal(bool modal);

private:
    GameEngine::Runtime::GameObject* mLayerObject = nullptr;
    GameEngine::Runtime::GameObject* mWindowObject = nullptr;
    GameEngine::Runtime::RectTransform* mWindowRect = nullptr;
    GameEngine::Runtime::UIWindow* mWindow = nullptr;
    GameEngine::Runtime::Button* mTitleBar = nullptr;
    GameEngine::Runtime::TextRenderer* mTitleLabel = nullptr;
    GameEngine::Runtime::Button* mDockButton = nullptr;
    GameEngine::Runtime::TextRenderer* mDockLabel = nullptr;

    /// <summary>
    /// 껍데기 안쪽의 사각형들이다. 자리를 매 프레임 다시 세우려면 붙잡고 있어야 한다.
    ///
    /// Build 때 한 번 박아 두면 안 되는 이유는 배율 때문이다. 이 값들은 96 DPI 기준의 논리
    /// 길이라 화면 배율을 곱해야 하고, 곱하지 않으면 배율 2에서 제목줄이 의도의 절반 높이가
    /// 되고 Dock 버튼은 절반 폭이 되어 그 안의 글자가 통째로 잘린다.
    /// </summary>
    GameEngine::Runtime::RectTransform* mHeaderRect = nullptr;
    GameEngine::Runtime::RectTransform* mDockRect = nullptr;
    GameEngine::Runtime::RectTransform* mTitleLabelRect = nullptr;

    /// <summary>창의 자리다. 논리 픽셀이며, 이 뷰가 유일하게 쥔 상태다.</summary>
    GameEngine::UI::UIRect mRect{ 240.0f, 120.0f, 420.0f, 260.0f };
    bool mVisible = false;
    /// <summary>제목줄 눌림의 올라가는 모서리다. 눌린 상태가 아니라 눌리기 시작한 프레임이다.</summary>
    bool mTitlePressStarted = false;
    bool mTitleWasPressed = false;
};

}
