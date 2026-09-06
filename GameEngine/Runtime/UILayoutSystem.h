#pragma once
#include "RectTransform.h"

namespace GameEngine::Platform
{
class ITextMeasure;
}

namespace GameEngine::Runtime
{

class SceneManager;
class Transform;

/// <summary>
/// UI 계층의 사각형을 매 프레임 다시 계산하는 시스템이다.
///
/// 계산은 두 번 훑는다. 흐름이 둘이기 때문이다: <b>자리</b>는 부모에서 자식으로 내려오고 —
/// 자식은 부모의 사각형 안에서만 자기 자리를 말할 수 있다 — <b>요구하는 크기</b>는 자식에서
/// 부모로 올라온다. 목록의 높이는 그 안의 행들이 정하고, 행의 높이는 그 안의 글자가 정한다.
/// 한 순회로 두 방향을 함께 흘릴 수는 없으므로 순회가 둘이다.
///
/// <b>재기</b>가 먼저 돌며 각 요소가 요구하는 크기를 <see cref="RectTransform"/>에 적어 둔다.
/// <b>놓기</b>가 그다음에 돌며 앵커와 오프셋으로 자리를 정하되, 요구 크기를 존중해야 하는
/// 요소는 적혀 있는 값을 읽는다. 그래서 놓기는 글자를 재지 않는다 — 잣대를 받지 않는 것이
/// 그 사실의 증거다.
///
/// 두 순회 모두 상태를 남기지 않는다. 계층 없이 <see cref="ResolveRect"/> 하나만으로 배치
/// 규칙을 검증할 수 있는 것이 그 덕이며, 그래서 잣대도 멤버가 아니라 매개변수로 흐른다.
/// 소리를 상태에 맞추는 <see cref="AudioSystem"/>과 같은 자리이며, 같은 이유로 프레임의
/// 상태가 확정된 뒤에 한 번 돈다.
/// </summary>
class UILayoutSystem final
{
public:
    /// <summary>
    /// 활성 장면의 모든 UI 면을 이번 프레임의 화면 크기에 맞춘다.
    ///
    /// 크기를 픽셀 두 개로 받는 것은 런타임이 렌더링 계층의 타입을 알지 않기 위해서다. 그리는
    /// 면의 크기는 프레임을 소유한 쪽이 알고 있고, 런타임에게 필요한 것은 그 두 수뿐이다.
    /// </summary>
    /// <param name="sceneManager">활성 장면을 쥔 매니저이다.</param>
    /// <param name="surfaceWidth">그리는 면의 가로 픽셀 수이다.</param>
    /// <param name="surfaceHeight">그리는 면의 세로 픽셀 수이다.</param>
    /// <param name="textMeasure">
    /// 글자 크기를 답하는 잣대다. 소유하지 않으며 null이어도 된다 — 그때는 내용에 맞추는 요소가
    /// 선언된 최소 크기만 지킨다. 재기 단계만 이것을 쓴다.
    /// </param>
    void Synchronize(
        SceneManager& sceneManager,
        float surfaceWidth,
        float surfaceHeight,
        Platform::ITextMeasure* textMeasure = nullptr) const;

    /// <summary>
    /// 앵커·오프셋 선언 하나를 부모 사각형에 적용한다. 자리 계산의 전부가 이 한 식이므로, 계층
    /// 없이 이 함수만으로 배치 규칙을 검증할 수 있다.
    /// </summary>
    /// <param name="rectTransform">적용할 선언이다.</param>
    /// <param name="parentRect">부모가 차지한 사각형이다.</param>
    /// <param name="scaleFactor">이 면의 픽셀 배율이다. 오프셋에만 곱해진다.</param>
    /// <returns>계산된 사각형이다. 뒤집힌 선언의 폭과 높이는 0으로 눌린다.</returns>
    [[nodiscard]] static RectTransform::Rect ResolveRect(
        const RectTransform& rectTransform,
        const RectTransform::Rect& parentRect,
        float scaleFactor);

private:
    // ---- 재기: 자식에서 부모로 요구 크기를 모은다.

    /// <summary>
    /// 계층을 훑으며 각 요소가 요구하는 크기를 채운다. 자식을 먼저 재는 이유는 컨테이너의 요구
    /// 크기가 자식들의 요구 크기에서 나오기 때문이다 — 그 방향이 이 순회가 따로 있는 이유다.
    /// </summary>
    /// <param name="transform">훑기 시작할 노드의 Transform이다.</param>
    /// <param name="scaleFactor">이 면의 픽셀 배율이다.</param>
    /// <param name="textMeasure">글자 크기를 답하는 잣대다. null이어도 된다.</param>
    /// <param name="availableRect">
    /// 부모가 이 노드에게 줄 수 있는 사각형이다. 접히는 요소는 이 폭에서 필요한 높이를
    /// 답하므로, 제약이 아래로 함께 내려가야 답이 나온다.
    /// </param>
    static void MeasureSubtree(
        const Transform& transform,
        const RectTransform::Rect& availableRect,
        float scaleFactor,
        Platform::ITextMeasure* textMeasure);

    /// <summary>
    /// 요소 하나가 요구하는 크기다. 글자를 가진 요소는 그 글자가, 컨테이너는 이미 재어 둔
    /// 자식들이 답한다. 요구할 것이 없는 요소는 0이다.
    /// </summary>
    /// <param name="element">잴 노드의 Transform이다.</param>
    /// <param name="scaleFactor">이 면의 픽셀 배율이다.</param>
    /// <param name="textMeasure">글자 크기를 답하는 잣대다. null이어도 된다.</param>
    /// <param name="availableRect">이 요소에게 주어질 사각형이다. 접힐 폭이 여기서 온다.</param>
    /// <returns>이 요소가 요구하는 픽셀 크기다.</returns>
    [[nodiscard]] static RectTransform::Size MeasureDesiredSize(
        const Transform& element,
        const RectTransform::Rect& availableRect,
        float scaleFactor,
        Platform::ITextMeasure* textMeasure);

    // ---- 놓기: 부모에서 자식으로 자리를 흘린다.

    /// <summary>
    /// 한 UI 면의 계층을 위에서 아래로 훑으며 사각형을 채운다.
    ///
    /// 부모 사각형과 자를 사각형을 따로 들고 내려간다. 배치는 잘리지 않은 자리에서 계속돼야
    /// 하기 때문이다 — 마스크에 반쯤 걸친 행이 반 크기로 줄어들면 그 아래 행들이 위로 딸려
    /// 올라온다. 잘림은 결과에만 적용된다.
    ///
    /// 잣대를 받지 않는다. 필요한 크기는 재기 단계가 이미 적어 두었으므로, 이 순회에서 글자를
    /// 다시 재는 일은 없다.
    /// </summary>
    /// <param name="transform">훑기 시작할 노드의 Transform이다.</param>
    /// <param name="parentRect">자식들이 자리를 계산할 기준 사각형이다.</param>
    /// <param name="clipRect">조상 마스크들이 남긴, 보일 수 있는 범위다.</param>
    /// <param name="scaleFactor">이 면의 픽셀 배율이다.</param>
    static void ArrangeSubtree(
        const Transform& transform,
        const RectTransform::Rect& parentRect,
        const RectTransform::Rect& clipRect,
        float scaleFactor);

    /// <summary>
    /// 앵커가 준 사각형에 요구 크기를 반영한다. 무엇을 반영할지는 붙어 있는 컴포넌트가 정한다:
    /// 컨테이너(<c>ContentFit</c>)는 그 축을 요구 크기로 <b>대체</b>하고, 내용에 맞추는
    /// 요소(<c>LayoutElement</c>)는 요구 크기까지 <b>늘린다</b>.
    ///
    /// 둘이 한 오브젝트에 함께 붙으면 같은 필드를 두고 다투므로, 자식을 가진 쪽이 이긴다.
    /// 승자를 우연한 호출 순서가 정하지 않도록 규칙을 여기 적어 둔다.
    /// </summary>
    /// <param name="element">적용할 노드의 Transform이다.</param>
    /// <param name="anchored">앵커와 오프셋만으로 계산된 사각형이다.</param>
    /// <returns>요구 크기가 반영된 사각형이다. 좌상단 모서리는 움직이지 않는다.</returns>
    [[nodiscard]] static RectTransform::Rect ApplyDesiredSize(
        const Transform& element, const RectTransform::Rect& anchored);

    /// <summary>
    /// 이 노드가 스크롤 viewport라면, 그 자식들이 계산될 자리를 밀어서 돌려준다. 밀린 거리는
    /// 여기서 범위 안으로 눌린다 — 넘치지 않는 내용은 밀리지 않는다.
    ///
    /// 놓기 단계에 속한다. 스크롤은 내용이 <b>얼마나 큰가</b>가 아니라 <b>어디에 놓이는가</b>의
    /// 문제이며, 그 크기는 재기 단계가 이미 답해 두었다 — 그래서 내용을 다시 재지 않고 적혀
    /// 있는 값을 읽는다.
    /// </summary>
    /// <param name="viewport">검사할 노드의 Transform이다.</param>
    /// <param name="viewportRect">그 노드가 차지한 사각형이다.</param>
    /// <returns>자식들이 부모로 삼을 사각형이다. 스크롤이 없으면 받은 그대로다.</returns>
    [[nodiscard]] static RectTransform::Rect ApplyScrolling(
        const Transform& viewport, const RectTransform::Rect& viewportRect);
};

}
