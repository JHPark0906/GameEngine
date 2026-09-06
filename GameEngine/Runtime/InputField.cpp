#include "pch.h"
#include "InputField.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "ScreenTextLayout.h"
#include "TextRenderer.h"
#include "../Platform/ITextMeasure.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// InputField가 선언하는 속성들이다. 포커스와 캐럿은 여기 없다: 매 프레임 사람이 정하는
    /// 것이라, 파일에 적으면 다음 로드가 아무도 클릭하지 않은 필드를 포커스된 채로 되살린다.
    /// </summary>
    std::span<const PropertyDescriptor> InputFieldProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<InputField>(
                "text", "Text", &InputField::GetText, &InputField::SetText),
        };
        return properties;
    }
}

const ComponentType& InputField::StaticType()
{
    static const ComponentType type{
        "InputField", &Selectable::StaticType(), &InputFieldProperties,
        &MakeComponentInstance<InputField> };
    return type;
}

void InputField::SetText(std::string text)
{
    mText = std::move(text);
    // 캐럿은 새 내용의 끝이다. 옛 자리를 지키면 그것이 새 문자열의 글자 한가운데일 수 있다.
    mEdit.ResetTo(mText);
    mCaretClock = 0.0f;
    mTextOffsetX = 0.0f;
    SynchronizeDisplay({});
}

void InputField::RequestFocus()
{
    mFocusRequested = IsActiveAndEnabled() && IsInteractable();
}

void InputField::OnInteractableLost()
{
    // 포커스를 쥔 채로 남으면 보이지 않는 곳으로 타이핑이 계속 들어간다.
    ApplyFocus(false);
    mFocusRequested = false;
}

void InputField::ApplyFocus(const bool focused)
{
    if (mFocused == focused)
    {
        return;
    }
    mFocused = focused;
    mCaretClock = 0.0f;
    if (focused)
    {
        // 프로그램 포커스는 끝에서 시작한다. 포인터 포커스는 이벤트 시스템이 클릭 자리로 옮긴다.
        mEdit.ResetTo(mText);
    }
    else
    {
        // 포커스를 잃으면 선택도 없다. 보이지 않는 선택이 남아 있다가 다음 타이핑에 지워지는
        // 것은 사람이 예상할 수 없는 일이다.
        mEdit.PlaceCaret(mEdit.GetCaret(), false);
        mCaretRect = {};
        mSelectionRects.clear();
        mCompositionRects.clear();
        SynchronizeDisplay({});
    }
}

UIModel::TextEditModel::Result InputField::ApplyEditing(const UIModel::TextEditModel::Input& input)
{
    mEdit.ClampTo(mText);
    const auto previousCaret = mEdit.GetCaret();
    const auto previousSelection = mEdit.GetSelection();
    UIModel::TextEditModel::Result result = mEdit.Apply(mText, input);
    // 한 프레임에 누적 글자 확정과 포인터 이후 편집이 나뉘어 적용될 수 있다.
    mEdited = mEdited || result.textChanged;
    const auto selection = mEdit.GetSelection();
    if (result.textChanged || previousCaret != mEdit.GetCaret() ||
        previousSelection.begin != selection.begin || previousSelection.end != selection.end)
        mCaretClock = 0.0f;
    return result;
}

void InputField::ApplySubmit()
{
    mSubmitted = true;
}

void InputField::SynchronizeDisplay(const std::string_view compositionText, const std::size_t compositionCaret)
{
    const auto selection = mEdit.GetSelection();
    mCompositionBegin = selection.begin;
    const auto caret = UIModel::TextEditModel::SnapToCharBoundary(compositionText,
        (std::min)(compositionCaret, compositionText.size()));
    const auto displayCaret = compositionText.empty() ? mEdit.GetCaret() : mCompositionBegin + caret;
    if (mCompositionText != compositionText || mDisplayCaret != displayCaret) mCaretClock = 0.0f;
    mCompositionText = compositionText;
    mDisplayCaret = displayCaret;
    GameObject* const owner = GetGameObject();
    TextRenderer* const renderer = owner ? owner->GetComponent<TextRenderer>() : nullptr;
    if (!renderer)
    {
        // 글자를 그릴 것이 없으면 값만 남는다. 무엇으로 보일지는 이 컴포넌트가 정하지 않는다 —
        // 버튼이 자기 그림을 만들지 않는 것과 같다.
        return;
    }

    // 조합 중인 글자는 아직 내용이 아니다. 화면에만 캐럿 자리에 끼워 보여 주고, 확정되면
    // 보통의 타이핑으로 도착해 내용이 된다.
    if (compositionText.empty())
    {
        renderer->SetText(mText);
        return;
    }
    std::string display = mText;
    // 조합이 선택을 덮을 때도 확정과 같은 자리를 보여 준다. 취소하면 원래 선택 텍스트가 남는다.
    display.replace(selection.begin, selection.end - selection.begin, compositionText);
    renderer->SetText(std::move(display));
}

void InputField::SynchronizeGeometry(Platform::ITextMeasure* measure, const float deltaTime)
{
    mCaretRect = {};
    mSelectionRects.clear();
    mCompositionRects.clear();
    if (!mFocused || !measure) return;
    if (std::isfinite(deltaTime) && deltaTime > 0.0f)
        mCaretClock = std::fmod(mCaretClock + deltaTime, 1.0f);
    auto* owner = GetGameObject();
    const auto* text = owner ? owner->GetComponent<TextRenderer>() : nullptr;
    const auto* rect = owner ? owner->GetComponent<RectTransform>() : nullptr;
    if (!text || !rect || text->GetSpace() != TextRenderer::Space::Screen) return;
    // 렌더링과 같은 요청으로 측정해야 줄바꿈과 정렬 뒤의 문자 경계까지 캐럿이 일치한다.
    auto request = MakeScreenTextRequest(*owner, *text);
    const bool empty = request.text.empty();
    if (empty) request.text = " "; // 폰트의 실제 줄 높이는 빈 필드에도 필요하다.
    const auto extent = measure->Measure(request);
    const auto stops = measure->MeasureCarets(request);
    if (stops.empty()) return;
    const float width = empty ? 0.0f : extent.width;
    const auto origin = GetScreenTextOrigin(*text, rect->GetResolvedRect(), width, extent.height);
    const float scale = GetTextCanvasScale(*owner);
    const float caretWidth = (std::max)(1.0f, scale);
    const auto findStop = [&stops](const std::size_t offset) -> const Platform::TextCaretStop&
    {
        const Platform::TextCaretStop* found = &stops.front();
        for (const auto& stop : stops)
            if (stop.byteOffset <= offset) found = &stop;
        return *found;
    };
    const auto& caret = findStop(mDisplayCaret);
    const float caretX = origin.GetX() + (empty ? 0.0f : caret.x);
    const auto& bounds = rect->GetResolvedRect();
    // 잉크가 없는 끝 공백도 편집 가능한 폭이다. 블록의 잉크 폭만 보면 캐럿이 밖으로 샌다.
    float contentWidth = width;
    for (const auto& stop : stops) contentWidth = (std::max)(contentWidth, stop.x);
    if (empty || contentWidth <= bounds.width - caretWidth) mTextOffsetX = 0.0f;
    else
    {
        if (caretX + mTextOffsetX < bounds.x) mTextOffsetX = bounds.x - caretX;
        if (caretX + mTextOffsetX + caretWidth > bounds.GetRight())
            mTextOffsetX = bounds.GetRight() - caretWidth - caretX;
    }
    // 오른쪽 정렬의 마지막 경계는 필드 오른쪽 변 자체다. 1픽셀 선은 그 변 안쪽에 놓는다.
    const float drawX = (std::min)(caretX + mTextOffsetX,
        (std::max)(bounds.x, bounds.GetRight() - caretWidth));
    mCaretRect = { drawX, origin.GetY() + caret.y, caretWidth, caret.height };
    mCaretRect = mCaretRect.IntersectedWith(rect->GetVisibleRect());
    const auto addRange = [&](const std::size_t begin, const std::size_t end,
        std::vector<RectTransform::Rect>& rectangles, const bool underline)
    {
        if (begin == end) return;
        for (std::size_t index = 0; index + 1 < stops.size(); ++index)
        {
            const auto& first = stops[index];
            const auto& last = stops[index + 1];
            if (first.byteOffset < begin || first.byteOffset >= end || first.y != last.y) continue;
            RectTransform::Rect box{ origin.GetX() + first.x + mTextOffsetX,
                origin.GetY() + first.y, (std::max)(0.0f, last.x - first.x), first.height };
            if (underline) { box.y += box.height - caretWidth; box.height = caretWidth; }
            box = box.IntersectedWith(rect->GetVisibleRect());
            if (!box.IsEmpty()) rectangles.push_back(box);
        }
    };
    const auto selection = mEdit.GetSelection();
    if (mCompositionText.empty()) addRange(selection.begin, selection.end, mSelectionRects, false);
    else addRange(mCompositionBegin, mCompositionBegin + mCompositionText.size(), mCompositionRects, true);
}

void InputField::PlaceCaretAt(const float x, const float y, const bool extend, Platform::ITextMeasure* measure)
{
    // 조합의 캐럿은 IME가 소유한다. 확정되기 전 표시 오프셋을 원문 편집 모델에 쓰지 않는다.
    if (!measure || !mCompositionText.empty()) return;
    auto* owner = GetGameObject();
    const auto* text = owner ? owner->GetComponent<TextRenderer>() : nullptr;
    const auto* rect = owner ? owner->GetComponent<RectTransform>() : nullptr;
    if (!text || !rect || text->GetSpace() != TextRenderer::Space::Screen) return;
    const auto request = MakeScreenTextRequest(*owner, *text);
    const auto extent = measure->Measure(request);
    const auto stops = measure->MeasureCarets(request);
    const auto origin = GetScreenTextOrigin(*text, rect->GetResolvedRect(), extent.width, extent.height);
    float nearest = (std::numeric_limits<float>::max)();
    std::size_t offset = 0;
    for (const auto& stop : stops)
    {
        const float dx = x - (origin.GetX() + stop.x + mTextOffsetX);
        const float dy = y - (origin.GetY() + stop.y + stop.height * .5f);
        const float distance = dx * dx + dy * dy;
        if (distance < nearest) { nearest = distance; offset = stop.byteOffset; }
    }
    mEdit.PlaceCaret(UIModel::TextEditModel::SnapToCharBoundary(mText, offset), extend);
    mCaretClock = 0.0f;
}

void InputField::ClearFrameFlags()
{
    mEdited = false;
    mSubmitted = false;
}

}
