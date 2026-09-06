#include "pch.h"
#include "UILayoutSystem.h"

#include <algorithm>

#include "../Platform/ITextMeasure.h"
#include "Canvas.h"
#include "ContentFit.h"
#include "LayoutGroup.h"
#include "GameObject.h"
#include "LayoutElement.h"
#include "RectMask.h"
#include "Scene.h"
#include "SceneManager.h"
#include "ScrollRect.h"
#include "TextRenderer.h"
#include "Transform.h"

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>이 오브젝트의 사각형 선언이다. 없으면 null이다.</summary>
    [[nodiscard]] RectTransform* FindRect(const Transform& transform)
    {
        GameObject* const owner = transform.GetGameObject();
        return owner ? owner->GetComponent<RectTransform>() : nullptr;
    }

    /// <summary>
    /// 자식들이 요구한 크기를 모은다. 쌓이는 방향으로는 더하고, 가로지르는 방향으로는 가장 큰
    /// 것을 고른다 — 그 둘이 <see cref="LayoutGroup"/>이 자기 크기를 구하는 재료다.
    /// </summary>
    /// <param name="container">자식을 가진 노드의 Transform이다.</param>
    /// <param name="horizontal">쌓이는 방향이 가로인지다.</param>
    /// <returns>합, 최댓값, 그리고 센 자식의 수다.</returns>
    struct ChildDemand
    {
        float along = 0.0f;
        float across = 0.0f;
        int count = 0;
    };

    [[nodiscard]] ChildDemand CollectChildDemand(
        const Transform& container, const bool horizontal)
    {
        ChildDemand demand;
        for (const Transform* const child : container.GetChildren())
        {
            if (!child)
            {
                continue;
            }
            // 꺼진 자식은 자리를 차지하지 않는다. ContentFit이 세는 규칙과 같다.
            const GameObject* const childObject = child->GetGameObject();
            if (childObject && (!childObject->IsActiveInHierarchy() || childObject->GetComponent<Canvas>()))
            {
                continue;
            }
            const RectTransform* const childRect = FindRect(*child);
            if (!childRect)
            {
                continue;
            }
            const RectTransform::Size& size = childRect->GetDesiredSize();
            demand.along += horizontal ? size.width : size.height;
            demand.across = (std::max)(demand.across, horizontal ? size.height : size.width);
            ++demand.count;
        }
        return demand;
    }

    /// <summary>
    /// 이 컨테이너가 쌓는 자식들이 요구하는 크기다. 자식마다 크기가 다를 수 있으므로 개수가
    /// 아니라 각자의 요구를 더한다 — 이름 길이가 제각각인 목록에서 개수는 답이 되지 못한다.
    ///
    /// 요구할 것이 없는 자식 — 글자도 없고 자기 자식도 없는 것 — 은 균일한 항목 크기로 센다.
    /// 요구할 것이 있는 자식만 자기 요구 크기로 센다.
    /// </summary>
    /// <param name="container">쌓는 노드의 Transform이다.</param>
    /// <param name="fit">그 노드의 유도 규칙이다.</param>
    /// <param name="scaleFactor">이 면의 픽셀 배율이다.</param>
    /// <returns>쌓이는 방향으로 필요한 픽셀 크기다.</returns>
    [[nodiscard]] float SumChildDemand(
        const Transform& container, const ContentFit& fit, const float scaleFactor)
    {
        const bool vertical = fit.GetDirection() == ContentFit::Direction::Vertical;
        float total = 0.0f;
        int counted = 0;
        for (const Transform* const child : container.GetChildren())
        {
            if (!child)
            {
                continue;
            }
            // 꺼진 자식은 자리를 차지하지 않는다. 쌓는 쪽도 같은 규칙으로 건너뛴다.
            const GameObject* const childObject = child->GetGameObject();
            if (childObject && (!childObject->IsActiveInHierarchy() || childObject->GetComponent<Canvas>()))
            {
                continue;
            }
            const RectTransform* const childRect = FindRect(*child);
            const float extent = childRect
                ? (vertical ? childRect->GetDesiredSize().height
                            : childRect->GetDesiredSize().width)
                : 0.0f;
            total += extent > 0.0f ? extent : fit.GetItemSize() * scaleFactor;
            ++counted;
        }
        if (counted == 0)
        {
            return 2.0f * fit.GetPadding() * scaleFactor;
        }
        // 간격은 자식 사이에만 들어간다: n개면 n-1군데다.
        return 2.0f * fit.GetPadding() * scaleFactor + total +
            static_cast<float>(counted - 1) * fit.GetSpacing() * scaleFactor;
    }
}

RectTransform::Rect UILayoutSystem::ResolveRect(
    const RectTransform& rectTransform,
    const RectTransform::Rect& parentRect,
    const float scaleFactor)
{
    const Math::Vector2& anchorMin = rectTransform.GetAnchorMin();
    const Math::Vector2& anchorMax = rectTransform.GetAnchorMax();
    const Math::Vector2& offsetMin = rectTransform.GetOffsetMin();
    const Math::Vector2& offsetMax = rectTransform.GetOffsetMax();

    // 앵커는 부모 안의 비율이고 오프셋은 그 자리에서 잰 픽셀이다. 배율이 오프셋에만 곱해지는
    // 이유가 여기 있다: 비율은 이미 해상도와 무관하고, 픽셀만 화면을 탄다.
    const float left = parentRect.x + anchorMin.GetX() * parentRect.width
        + offsetMin.GetX() * scaleFactor;
    const float top = parentRect.y + anchorMin.GetY() * parentRect.height
        + offsetMin.GetY() * scaleFactor;
    const float right = parentRect.x + anchorMax.GetX() * parentRect.width
        + offsetMax.GetX() * scaleFactor;
    const float bottom = parentRect.y + anchorMax.GetY() * parentRect.height
        + offsetMax.GetY() * scaleFactor;

    RectTransform::Rect resolved;
    resolved.x = left;
    resolved.y = top;
    // 뒤집힌 선언 — 오른쪽 끝이 왼쪽 끝보다 왼쪽인 것 — 은 사각형이 아니다. 음수 크기를 그대로
    // 흘리면 그것을 받는 쪽마다 다른 방식으로 어긋나므로, 여기서 한 번 0으로 누른다.
    resolved.width = (std::max)(right - left, 0.0f);
    resolved.height = (std::max)(bottom - top, 0.0f);
    return resolved;
}

RectTransform::Size UILayoutSystem::MeasureDesiredSize(
    const Transform& element,
    const RectTransform::Rect& availableRect,
    const float scaleFactor,
    Platform::ITextMeasure* const textMeasure)
{
    const GameObject* const owner = element.GetGameObject();
    if (!owner)
    {
        return {};
    }

    RectTransform::Size desired;

    // 내용에 맞추는 요소는 자기 글자가 요구하는 크기를 말한다. 선언된 최소 크기는 잴 것이
    // 없거나 잴 수 없을 때에도 남으므로, 폰트를 열지 못한 화면에서 요소가 통째로 사라지지 않는다.
    if (const LayoutElement* const layout = owner->GetComponent<LayoutElement>())
    {
        const Math::Vector2& padding = layout->GetPadding();
        const Math::Vector2& minimum = layout->GetMinimumSize();
        desired.width = minimum.GetX() * scaleFactor;
        desired.height = minimum.GetY() * scaleFactor;

        const TextRenderer* const text = owner->GetComponent<TextRenderer>();
        if (textMeasure && text && !text->GetText().empty())
        {
            // 재는 요청을 그리는 쪽과 같은 타입으로 조립한다. 접지 않고 재는 것 — maxWidth를
            // 주지 않는 것 — 이 이 자리의 질문이다: "줄이지 않았을 때 얼마나 필요한가"가 곧
            // 요구하는 크기다.
            Platform::TextRasterizationRequest request;
            request.text = text->GetText();
            request.fontFamily = text->GetFontFamily();
            request.fontSize = text->GetFontSize() * scaleFactor;
            request.lineSpacing = text->GetLineSpacing();
            // 접히는 요소만 폭을 제약으로 넘긴다. 그리는 쪽도 배치된 폭에서 같은 padding을
            // 빼서 요청하므로 측정과 그리기가 같은 접기 폭을 사용한다.
            const bool wraps = layout->IsWrapping();
            if (wraps)
            {
                request.maxWidth =
                    (std::max)(availableRect.width - padding.GetX() * scaleFactor, 0.0f);
            }
            const Platform::TextExtent extent = textMeasure->Measure(request);
            // 접히는 요소는 가로를 요구하지 않는다. 받은 폭을 그대로 받아들이고 그 폭에서
            // 필요한 높이만 말하는 것이 접기의 정의이므로, 여기서 폭을 요구하면 접을 이유가
            // 사라진다.
            if (!wraps)
            {
                desired.width =
                    (std::max)(desired.width, extent.width + padding.GetX() * scaleFactor);
            }
            desired.height =
                (std::max)(desired.height, extent.height + padding.GetY() * scaleFactor);
        }
    }

    // 자식이 저마다 얼마나 필요한지에서 자기 크기를 구하는 컨테이너다. ContentFit이 자식의
    // "수"로 구하는 것과 나란히 놓인다 — 글자 길이가 제각각인 버튼과 메뉴 항목이 이쪽이다.
    if (const LayoutGroup* const group = owner->GetComponent<LayoutGroup>())
    {
        const bool horizontal = group->GetDirection() == LayoutGroup::Direction::Horizontal;
        const ChildDemand demand = CollectChildDemand(element, horizontal);
        const float spacing = group->GetSpacing() * scaleFactor;
        const float padding = group->GetPadding() * scaleFactor;
        const float along =
            LayoutGroup::MeasureAlong(demand.along, demand.count, spacing, padding);
        const float across = LayoutGroup::MeasureAcross(demand.across, padding);
        if (horizontal)
        {
            desired.width = (std::max)(desired.width, along);
            desired.height = (std::max)(desired.height, across);
        }
        else
        {
            desired.height = (std::max)(desired.height, along);
            desired.width = (std::max)(desired.width, across);
        }
    }

    // 컨테이너는 이미 재어 둔 자식들이 답한다. 자식을 먼저 재는 순서가 이 한 줄을 가능하게 한다.
    if (const ContentFit* const fit = owner->GetComponent<ContentFit>())
    {
        const float extent = SumChildDemand(element, *fit, scaleFactor);
        if (fit->GetDirection() == ContentFit::Direction::Vertical)
        {
            desired.height = extent;
        }
        else
        {
            desired.width = extent;
        }
    }
    return desired;
}

void UILayoutSystem::MeasureSubtree(
    const Transform& transform,
    const RectTransform::Rect& availableRect,
    const float scaleFactor,
    Platform::ITextMeasure* const textMeasure)
{
    for (const Transform* const child : transform.GetChildren())
    {
        if (!child)
        {
            continue;
        }
        // 중첩 Canvas는 자기 화면과 배율로 별도 배치된다. 바깥 면이 그 결과를 덮지 않는다.
        if (const GameObject* const owner = child->GetGameObject();
            owner && owner->GetComponent<Canvas>())
        {
            continue;
        }
        RectTransform* const childRect = FindRect(*child);
        // 쓸 수 있는 폭은 앵커가 주는 것이다. 요구 크기로 자란 뒤의 폭이 아니라 자라기 전의
        // 폭인데, 접히는 요소는 애초에 가로를 요구하지 않으므로 둘이 같다 — 접기와 가로 맞춤을
        // 함께 쓰면 그 등식이 깨지고, 그래서 접힌 요소는 가로를 요구하지 않는다.
        const RectTransform::Rect childAvailable = childRect
            ? ResolveRect(*childRect, availableRect, scaleFactor)
            : availableRect;

        // 아래를 먼저 재고 나서 자기를 잰다. 컨테이너의 요구 크기가 자식들의 답이기 때문이다.
        MeasureSubtree(*child, childAvailable, scaleFactor, textMeasure);
        if (childRect)
        {
            childRect->SetDesiredSize(
                MeasureDesiredSize(*child, childAvailable, scaleFactor, textMeasure));
        }
    }
}

RectTransform::Rect UILayoutSystem::ApplyDesiredSize(
    const Transform& element, const RectTransform::Rect& anchored)
{
    const GameObject* const owner = element.GetGameObject();
    const RectTransform* const rect = owner ? owner->GetComponent<RectTransform>() : nullptr;
    if (!rect)
    {
        return anchored;
    }
    const RectTransform::Size& desired = rect->GetDesiredSize();

    RectTransform::Rect applied = anchored;

    // 자식을 가진 것은 컨테이너다. 그 축은 선언이 아니라 내용이 정하므로 대체한다.
    const ContentFit* const contentFit = owner->GetComponent<ContentFit>();
    const bool contentDrivesWidth =
        contentFit && contentFit->GetDirection() == ContentFit::Direction::Horizontal;
    const bool contentDrivesHeight =
        contentFit && contentFit->GetDirection() == ContentFit::Direction::Vertical;
    if (contentDrivesWidth)
    {
        applied.width = desired.width;
    }
    if (contentDrivesHeight)
    {
        applied.height = desired.height;
    }

    // 내용에 맞추는 요소는 자라는 방향으로만 손댄다. 이미 넉넉한 자리를 요구 크기까지 줄이면
    // "내용에 맞춘다"가 "내용에 가둔다"가 되고, 그것은 이 컴포넌트가 말한 것이 아니다.
    if (const LayoutElement* const layout = owner->GetComponent<LayoutElement>())
    {
        const LayoutElement::Fit fit = layout->GetFit();
        const bool fitsWidth =
            fit == LayoutElement::Fit::Horizontal || fit == LayoutElement::Fit::Both;
        const bool fitsHeight =
            fit == LayoutElement::Fit::Vertical || fit == LayoutElement::Fit::Both;
        if (fitsWidth && !contentDrivesWidth)
        {
            applied.width = (std::max)(applied.width, desired.width);
        }
        if (fitsHeight && !contentDrivesHeight)
        {
            applied.height = (std::max)(applied.height, desired.height);
        }
    }
    return applied;
}

RectTransform::Rect UILayoutSystem::ApplyScrolling(
    const Transform& viewport, const RectTransform::Rect& viewportRect)
{
    GameObject* const viewportObject = viewport.GetGameObject();
    ScrollRect* const scrollRect =
        viewportObject ? viewportObject->GetComponent<ScrollRect>() : nullptr;
    if (!scrollRect)
    {
        return viewportRect;
    }

    // 내용은 사각형을 가진 첫 번째 자식이다. 컴포넌트 속성이 담는 것은 값이지 다른 오브젝트를
    // 가리키는 참조가 아니라서, 어느 자식이 내용인지를 파일에 적을 방법이 지금은 없다.
    const RectTransform* contentRect = nullptr;
    for (const Transform* const child : viewport.GetChildren())
    {
        if (!child)
        {
            continue;
        }
        if (const RectTransform* const found = FindRect(*child))
        {
            contentRect = found;
            break;
        }
    }
    if (!contentRect)
    {
        return viewportRect;
    }

    // 밀 수 있는 범위는 내용이 viewport를 넘치는 만큼이고, 그 크기는 재기 단계가 이미 답했다.
    const RectTransform::Size& contentDemand = contentRect->GetDesiredSize();

    const Math::Vector2& offset = scrollRect->GetScrollOffset();
    const float clampedY = scrollRect->IsVertical()
        ? ScrollRect::ClampAxis(offset.GetY(), viewportRect.height, contentDemand.height)
        : 0.0f;
    const float clampedX = scrollRect->IsHorizontal()
        ? ScrollRect::ClampAxis(offset.GetX(), viewportRect.width, contentDemand.width)
        : 0.0f;
    scrollRect->SetScrollOffset({ clampedX, clampedY });

    // 미는 것은 선언이 아니라 내용이 계산되는 자리다. 선언된 오프셋을 매 프레임 고쳐 쓰면 밀린
    // 거리가 프레임마다 쌓이고, 그렇게 쌓인 값이 장면 파일에 그대로 저장된다.
    RectTransform::Rect scrolled = viewportRect;
    scrolled.x -= clampedX;
    scrolled.y -= clampedY;
    return scrolled;
}

void UILayoutSystem::ArrangeSubtree(
    const Transform& transform,
    const RectTransform::Rect& parentRect,
    const RectTransform::Rect& clipRect,
    const float scaleFactor)
{
    // 이 노드가 스크롤 viewport라면 자식들은 밀린 자리에서 계산된다. 자기 사각형은 그대로다 —
    // 밀리는 것은 내용이지 창이 아니다.
    const RectTransform::Rect childParentRect = ApplyScrolling(transform, parentRect);

    // 이 노드가 ContentFit이면 자식들은 선언한 자리가 아니라 쌓이는 순서대로 놓인다. 쌓이는
    // 축의 자리와 크기는 여기서 정하고, 다른 축은 자식의 앵커가 정한다. 크기는 자식이 요구한
    // 것이고, 요구하지 않은 자식은 항목 크기를 받는다 — 재기 단계가 합산한 것과 같은 규칙이다.
    const GameObject* const owner = transform.GetGameObject();
    const ContentFit* const stack = owner ? owner->GetComponent<ContentFit>() : nullptr;
    const bool stackVertical = stack && stack->GetDirection() == ContentFit::Direction::Vertical;
    float cursor = stack
        ? (stackVertical ? childParentRect.y : childParentRect.x) + stack->GetPadding() * scaleFactor
        : 0.0f;

    for (Transform* const child : transform.GetChildren())
    {
        if (!child)
        {
            continue;
        }
        GameObject* const childObject = child->GetGameObject();
        if (!childObject)
        {
            continue;
        }
        if (childObject->GetComponent<Canvas>())
        {
            continue;
        }
        // 꺼진 자식은 자리를 차지하지 않는다. 쌓이는 줄에서 빠지고 그 아래는 올라온다.
        if (stack && !childObject->IsActiveInHierarchy())
        {
            continue;
        }

        // RectTransform이 없는 오브젝트는 자리를 주장하지 않는다. 그래도 계층은 끊기지 않는다:
        // 자식들은 이 오브젝트의 부모 사각형을 그대로 물려받아 계속 배치된다 — 순수한 묶음용
        // 오브젝트가 UI 계층 한가운데 있어도 아래가 사라지지 않도록.
        RectTransform* const childRect = childObject->GetComponent<RectTransform>();
        RectTransform::Rect resolved = childRect
            ? ApplyDesiredSize(*child, ResolveRect(*childRect, childParentRect, scaleFactor))
            : childParentRect;
        if (stack && childRect)
        {
            const RectTransform::Size& demand = childRect->GetDesiredSize();
            const float declared = stackVertical ? demand.height : demand.width;
            const float extent = declared > 0.0f ? declared : stack->GetItemSize() * scaleFactor;
            if (stackVertical)
            {
                resolved.y = cursor;
                resolved.height = extent;
            }
            else
            {
                resolved.x = cursor;
                resolved.width = extent;
            }
            cursor += extent + stack->GetSpacing() * scaleFactor;
        }
        const RectTransform::Rect visible = resolved.IntersectedWith(clipRect);
        if (childRect)
        {
            childRect->SetResolvedRect(resolved);
            childRect->SetVisibleRect(visible);
        }

        // 마스크는 자기 사각형까지만 보이게 한다. 자를 범위는 조상들이 남긴 것과의 교차이므로
        // 겹친 마스크는 좁은 쪽이 이긴다 — 스크롤 창 안의 스크롤 창이 바깥 창을 넘어 보이지
        // 않는다. 자를 때 쓰는 것은 잘린 사각형이 아니라 잘리기 전 자리다: 마스크 자신이 반쯤
        // 가려졌다면 그 가려진 만큼은 이미 조상의 범위에서 빠져 있다.
        const RectTransform::Rect childClipRect =
            childObject->GetComponent<RectMask>() ? visible : clipRect;
        ArrangeSubtree(*child, resolved, childClipRect, scaleFactor);
    }
}

void UILayoutSystem::Synchronize(
    SceneManager& sceneManager,
    const float surfaceWidth,
    const float surfaceHeight,
    Platform::ITextMeasure* const textMeasure) const
{
    RectTransform::Rect surfaceRect;
    surfaceRect.width = (std::max)(surfaceWidth, 0.0f);
    surfaceRect.height = (std::max)(surfaceHeight, 0.0f);

    for (const auto& [sceneId, scene] : sceneManager.GetActiveScenes())
    {
        if (!scene)
        {
            continue;
        }
        for (const auto& [objectId, gameObject] : scene->GetGameObjects())
        {
            if (!gameObject)
            {
                continue;
            }
            const Canvas* const canvas = gameObject->GetComponent<Canvas>();
            if (!canvas)
            {
                continue;
            }

            // 면마다 바깥 사각형은 화면 전체다. 캔버스가 다른 캔버스 아래 있어도 마찬가지다 —
            // 캔버스는 계층의 한 마디가 아니라 새 면의 시작이고, 그것이 캔버스를 두는 이유다.
            const float scaleFactor = canvas->GetScaleFactor();
            const Transform& root = gameObject->GetTransform();


            RectTransform* const canvasRect = gameObject->GetComponent<RectTransform>();
            const RectTransform::Rect rootRect =
                canvasRect ? ResolveRect(*canvasRect, surfaceRect, scaleFactor) : surfaceRect;
            if (canvasRect)
            {
                canvasRect->SetResolvedRect(rootRect);
                canvasRect->SetVisibleRect(rootRect);
            }
            // 재기가 먼저다. 놓기가 읽을 요구 크기를 이 순회가 채운다. 쓸 수 있는
            // 사각형을 함께 내려보내는 것은 접히는 요소 때문이다 — 그 요소의 요구
            // 높이는 받게 될 폭의 함수라서, 제약 없이는 답이 나오지 않는다.
            MeasureSubtree(root, rootRect, scaleFactor, textMeasure);

            // 면 자체가 가장 바깥 경계다. 캔버스 밖으로 나간 요소는 그릴 곳도 누를 곳도 없다.
            ArrangeSubtree(root, rootRect, rootRect, scaleFactor);
        }
    }
}

}
