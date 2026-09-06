#pragma once

// editor-layer: 2 (Views)

#include <cstddef>
#include <vector>

#include "Rules/EditorConfirmation.h"

namespace GameEngine::Platform
{
class ITextMeasure;
}

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

/// <summary>
/// 물음이 서는 자리다. 떠 있는 창 하나로 서며, 그 안에 제목·물음·버튼들이 쌓인다.
///
/// 물음은 모두 공통 모양으로 그린다. 같은 종류의 물음을 알아보기 쉽고,
/// 표시를 고치는 자리도 하나로 유지된다.
///
/// 무엇을 물을지는 <see cref="ConfirmationQueue"/>가 쥐고 이 뷰는 그것을 그린다. 나누는 이유는
/// 줄 세우기와 답 한 번이라는 계약이 창을 몰라도 성립하는 규칙이고, 그래야 창 없이 잴 수
/// 있기 때문이다.
/// </summary>
class EditorConfirmationView final
{
public:
    explicit EditorConfirmationView(ConfirmationQueue& queue);

    /// <summary>물음이 설 자리를 이 장면에 만든다. 한 번만 부른다.</summary>
    /// <param name="scene">에디터 프로세스 자신의 런타임 장면이다.</param>
    void Build(GameEngine::Runtime::Scene& scene);

    /// <summary>
    /// 프레임마다 줄의 맨 앞을 그리고, 눌린 버튼을 답으로 옮긴다. 서 있는 물음이 없으면
    /// 창을 감춘다.
    /// </summary>
    /// <param name="contentScale">창의 콘텐츠 배율이다.</param>
    /// <param name="surfaceWidth">그리는 면의 가로 픽셀이다.</param>
    /// <param name="surfaceHeight">그리는 면의 세로 픽셀이다.</param>
    /// <param name="textMeasure">
    /// 글자 폭을 답하는 잣대다. 창의 폭이 그 답에서 나오므로, 없으면 창은 최소 폭으로 서고
    /// 긴 라벨은 잘린다 — 첫 프레임과 잣대가 없는 자리가 그렇다.
    /// </param>
    void Synchronize(
        float contentScale, float surfaceWidth, float surfaceHeight,
        GameEngine::Platform::ITextMeasure* textMeasure);

    /// <summary>창이 보이는지다. 시험이 이것으로 물음의 유무를 본다.</summary>
    [[nodiscard]] bool IsShowing() const { return mShowing; }

    /// <summary>이번 프레임에 세워진 버튼의 수다.</summary>
    [[nodiscard]] std::size_t GetVisibleButtonCount() const { return mVisibleButtonCount; }

private:
    struct ChoiceButton
    {
        GameEngine::Runtime::GameObject* object = nullptr;
        GameEngine::Runtime::Button* button = nullptr;
        GameEngine::Runtime::TextRenderer* label = nullptr;
    };

    /// <summary>
    /// 서 있는 물음의 제목과 각 선택지의 라벨·화면 사각형을 로그에 남긴다.
    ///
    /// 버튼은 엔진이 그리는 픽셀이므로 외부 자동화가 창 컨트롤로 찾을 수 없다.
    /// 좌표를 추측한 클릭이 사본 삭제 같은 다른 동작에 닿지 않도록 실제 배치를 기록한다.
    /// </summary>
    void LogWhereTheChoicesAre(const ConfirmationRequest& request);

    void ShowRequest(
        const ConfirmationRequest& request, float contentScale, float surfaceWidth,
        GameEngine::Platform::ITextMeasure* textMeasure);
    void Hide();

    ConfirmationQueue& mQueue;

    GameEngine::Runtime::GameObject* mCanvasObject = nullptr;
    GameEngine::Runtime::GameObject* mWindowObject = nullptr;
    GameEngine::Runtime::RectTransform* mWindowRect = nullptr;
    GameEngine::Runtime::TextRenderer* mTitle = nullptr;
    GameEngine::Runtime::TextRenderer* mQuestion = nullptr;
    GameEngine::Runtime::GameObject* mButtonRow = nullptr;
    std::vector<ChoiceButton> mButtons;

    bool mShowing = false;
    /// <summary>
    /// 서 있는 물음의 자리를 이미 적었는지다. 사각형은 배치가 푼 뒤에야 알 수 있으므로 그
    /// 기록은 물음이 선 다음 프레임에 남는다.
    /// </summary>
    bool mLoggedCurrent = false;
    std::size_t mVisibleButtonCount = 0;
};

}
