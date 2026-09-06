#include "Views/EditorConfirmationView.h"

#include <algorithm>
#include <ranges>
#include <optional>
#include <vector>
#include <string>
#include <string_view>

#include "Rules/EditorPanelCommon.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/ContentFit.h"
#include "Runtime/GameObject.h"
#include "Runtime/LayoutElement.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Core/TextFit.h"
#include "Diagnostics/Debug.h"
#include "Platform/ITextMeasure.h"
#include "Platform/ITextRasterizer.h"
#include "Runtime/UIWindow.h"

namespace GameEditor
{
namespace
{
    using GameEngine::Math::Vector2;
    using GameEngine::Runtime::Button;
    using GameEngine::Runtime::Canvas;
    using GameEngine::Runtime::ContentFit;
    using GameEngine::Runtime::GameObject;
    using GameEngine::Runtime::LayoutElement;
    using GameEngine::Runtime::RectTransform;
    using GameEngine::Runtime::SpriteRenderer;
    using GameEngine::Runtime::TextRenderer;
    using GameEngine::Runtime::UIWindow;

    /// <summary>
    /// 창이 담을 수 있는 버튼의 최대 수다. 물음 하나에 이보다 많은 선택지를 두면 사람이
    /// 읽지 못하므로, 넘는 것은 만들지 않고 로그로 말한다.
    /// </summary>
    constexpr std::size_t MaxChoices = 6;

    /// <summary>잣대가 없을 때의 폭이다. 첫 프레임과 잣대 없는 자리가 이것으로 선다.</summary>
    constexpr float MinimumWindowWidth = 280.0f;

    /// <summary>
    /// 창이 화면에서 차지할 수 있는 최대 비율이다. 상한이 없으면 긴 라벨 하나가 창을 화면
    /// 폭까지 늘려, 물음이 아니라 벽이 된다.
    /// </summary>
    constexpr float MaximumWindowWidthFraction = 0.8f;

    /// <summary>잘린 글 뒤에 붙는 것이다.</summary>
    constexpr const char* Ellipsis = "...";
    constexpr float TitleFontSize = RowFontSize;
    constexpr float QuestionFontSize = RowFontSize;
    constexpr float ButtonLabelInset = 6.0f;


    /// <summary>
    /// 잣대로 한 줄의 폭을 잰다. 잣대가 없으면 0이며, 그때 창은 최소 폭으로 선다.
    /// </summary>
    [[nodiscard]] float MeasureWidth(
        GameEngine::Platform::ITextMeasure* const measure, const std::string& text,
        const float fontSize)
    {
        if (!measure || text.empty())
        {
            return 0.0f;
        }
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = text;
        request.fontSize = fontSize;
        return measure->Measure(request).width;
    }
    /// <summary>부모 아래에 사각형 하나를 만든다. 자리는 앵커와 오프셋이 정한다.</summary>
    [[nodiscard]] GameObject* AddRect(
        GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name,
        const Vector2& anchorMin, const Vector2& anchorMax, const Vector2& offsetMin,
        const Vector2& offsetMax)
    {
        GameObject* const object = scene.CreateGameObject(name);
        if (!object)
        {
            return nullptr;
        }
        static_cast<void>(object->GetTransform().SetParent(&parent.GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        if (!rect)
        {
            return nullptr;
        }
        rect->SetAnchorMin(anchorMin);
        rect->SetAnchorMax(anchorMax);
        rect->SetOffsetMin(offsetMin);
        rect->SetOffsetMax(offsetMax);
        return object;
    }

    /// <summary>창 안에 가로로 가득 차는 한 줄을 만든다. 높이는 쌓이며 정해진다.</summary>
    [[nodiscard]] GameObject* AddStackedRow(
        GameEngine::Runtime::Scene& scene, GameObject& parent, const std::string& name)
    {
        return AddRect(
            scene, parent, name, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { Padding, 0.0f },
            { -Padding, 0.0f });
    }
}

EditorConfirmationView::EditorConfirmationView(ConfirmationQueue& queue) : mQueue(queue)
{
}

void EditorConfirmationView::Build(GameEngine::Runtime::Scene& scene)
{
    mCanvasObject = scene.CreateGameObject("ConfirmationCanvas");
    if (!mCanvasObject)
    {
        return;
    }
    // 캔버스에는 RectTransform을 두지 않는다. 두면 배치가 화면 대신 그 사각형을 면의 루트이자
    // 클립으로 삼고, 기본값 사각형은 그 아래 전부를 잘라낸다.
    static_cast<void>(mCanvasObject->AddComponent<Canvas>());

    // 창은 화면 가운데 위쪽에 선다. 가로는 고정이고 세로는 담긴 줄들이 쌓여 정한다.
    mWindowObject = AddRect(
        scene, *mCanvasObject, "Confirmation", { 0.5f, 0.25f }, { 0.5f, 0.25f },
        { -MinimumWindowWidth * 0.5f, 0.0f }, { MinimumWindowWidth * 0.5f, 0.0f });
    if (!mWindowObject)
    {
        return;
    }
    mWindowRect = mWindowObject->GetComponent<RectTransform>();
    if (ContentFit* const fit = mWindowObject->AddComponent<ContentFit>())
    {
        fit->SetDirection(ContentFit::Direction::Vertical);
        fit->SetSpacing(Padding * 0.5f);
        fit->SetPadding(Padding);
    }
    if (SpriteRenderer* const background = mWindowObject->AddComponent<SpriteRenderer>())
    {
        background->SetSpace(SpriteRenderer::Space::Screen);
        background->SetColor(PanelColor);
    }
    // 모달이다. 답하기 전에는 뒤의 것을 만지지 못한다 — 그것이 이 물음을 팝업으로 세우는
    // 이유의 절반이고, 나머지 절반은 여섯 자리가 같은 모양이 되는 것이다.
    if (UIWindow* const window = mWindowObject->AddComponent<UIWindow>())
    {
        window->SetModal(true);
    }

    if (GameObject* const titleRow = AddStackedRow(scene, *mWindowObject, "ConfirmationTitle"))
    {
        if (LayoutElement* const element = titleRow->AddComponent<LayoutElement>())
        {
            element->SetMinimumSize({ 0.0f, RowHeight });
        }
        mTitle = titleRow->AddComponent<TextRenderer>();
        if (mTitle)
        {
            mTitle->SetSpace(TextRenderer::Space::Screen);
            mTitle->SetColor(TextColor);
        }
    }
    if (GameObject* const questionRow =
            AddStackedRow(scene, *mWindowObject, "ConfirmationQuestion"))
    {
        if (LayoutElement* const element = questionRow->AddComponent<LayoutElement>())
        {
            // 물음은 길 수 있다. 내용에 맞춰 늘어나지 않으면 긴 물음이 잘리고, 잘린 물음은
            // 사람이 무엇에 답하는지 모르게 만든다.
            element->SetFit(LayoutElement::Fit::Vertical);
            element->SetWrapping(true);
            element->SetMinimumSize({ 0.0f, RowHeight });
        }
        mQuestion = questionRow->AddComponent<TextRenderer>();
        if (mQuestion)
        {
            mQuestion->SetSpace(TextRenderer::Space::Screen);
            mQuestion->SetColor(TextColor);
        }
    }

    mButtonRow = AddStackedRow(scene, *mWindowObject, "ConfirmationButtons");
    if (!mButtonRow)
    {
        return;
    }
    if (LayoutElement* const element = mButtonRow->AddComponent<LayoutElement>())
    {
        element->SetMinimumSize({ 0.0f, RowHeight });
    }

    // 버튼은 만들 수 있는 만큼 미리 만들고, 물음마다 필요한 수만 켠다. 프레임 안에서
    // 게임 오브젝트를 만들고 부수는 것보다 켜고 끄는 쪽이 조용하다.
    mButtons.resize(MaxChoices);
    for (std::size_t index = 0; index < MaxChoices; ++index)
    {
        ChoiceButton& entry = mButtons[index];
        entry.object = AddRect(
            scene, *mButtonRow, "ConfirmationChoice" + std::to_string(index), { 0.0f, 0.0f },
            { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f });
        if (!entry.object)
        {
            continue;
        }
        if (SpriteRenderer* const background = entry.object->AddComponent<SpriteRenderer>())
        {
            background->SetSpace(SpriteRenderer::Space::Screen);
        }
        entry.button = entry.object->AddComponent<Button>();
        if (GameObject* const labelObject = AddRect(
                scene, *entry.object, "ConfirmationChoiceLabel" + std::to_string(index),
                { 0.0f, 0.0f }, { 1.0f, 1.0f }, { ButtonLabelInset, 0.0f }, { 0.0f, 0.0f }))
        {
            entry.label = labelObject->AddComponent<TextRenderer>();
            if (entry.label)
            {
                entry.label->SetSpace(TextRenderer::Space::Screen);
                entry.label->SetColor(TextColor);
            }
        }
        entry.object->SetActive(false);
    }
    Hide();
}

void EditorConfirmationView::Hide()
{
    mShowing = false;
    mVisibleButtonCount = 0;
    mLoggedCurrent = false;
    if (mWindowObject)
    {
        mWindowObject->SetActive(false);
    }
}

void EditorConfirmationView::ShowRequest(
    const ConfirmationRequest& request, const float contentScale, const float surfaceWidth,
    GameEngine::Platform::ITextMeasure* const textMeasure)
{
    const float scale = contentScale > 0.0f ? contentScale : 1.0f;

    if (mTitle)
    {
        mTitle->SetText(request.title);
        mTitle->SetFontSize(TitleFontSize);
        if (GameObject* const owner = mTitle->GetGameObject())
        {
            // 제목이 없으면 그 줄을 끈다. 꺼진 줄은 쌓이는 자리에서 빠지므로 창이 그만큼 짧아진다.
            owner->SetActive(!request.title.empty());
        }
    }
    if (mQuestion)
    {
        mQuestion->SetText(request.question);
        mQuestion->SetFontSize(QuestionFontSize);
    }

    // 취소는 맨 뒤에 선다. 진행하는 답들이 앞에 모여 있어야 사람이 고르는 것을 먼저 본다.
    std::vector<std::string> labels = request.choices;
    if (!request.cancelLabel.empty())
    {
        labels.push_back(request.cancelLabel);
    }
    if (labels.size() > MaxChoices)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "A confirmation holds at most ", MaxChoices, " buttons, so the rest are not shown."
            " requested=", labels.size());
        labels.resize(MaxChoices);
    }

    const std::size_t count = labels.size();

    // 창의 폭은 담긴 글자와 버튼이 정한다. 고정 폭이면 긴 라벨은 잘리고
    // 짧은 물음에는 불필요한 빈 공간이 생긴다.
    //
    // 상한을 두는 이유는 반대쪽 실패다. 라벨 하나가 길면 창이 화면 폭까지 늘어나 물음이 아니라
    // 벽이 된다. 상한에 걸리면 물음은 접히고(내용에 맞춰 늘어나므로 저절로), 버튼 라벨은
    // 말줄임된다 — 어느 쪽도 소리 없이 사라지지 않는다.
    float widestLabel = 0.0f;
    for (const std::string& label : labels)
    {
        widestLabel = (std::max)(
            widestLabel, MeasureWidth(textMeasure, label, RowFontSize));
    }
    const float slots = static_cast<float>(count > 0 ? count : 1);
    const float demandedByButtons = count > 0
        ? widestLabel * slots + Padding * (slots + 1.0f) + ButtonLabelInset * 2.0f * slots
        : 0.0f;
    const float demandedByQuestion =
        MeasureWidth(textMeasure, request.question, QuestionFontSize) + Padding * 2.0f;
    // 여기서 정하는 폭은 오프셋에 들어가므로 논리 단위다. 배율은 사각형을 푸는 쪽이 오프셋에만
    // 곱한다 — 앵커는 비율이라 이미 해상도와 무관하고 픽셀만 화면을 탄다. 여기서 미리 곱하면
    // 그 위에 또 곱해져 배율 2에서 창이 네 배가 된다.
    //
    // 면의 폭은 반대로 물리 픽셀이라, 상한을 견주려면 논리로 되돌려야 한다.
    const float logicalSurfaceWidth = scale > 0.0f ? surfaceWidth / scale : surfaceWidth;
    const float maximum = (std::max)(
        MinimumWindowWidth, logicalSurfaceWidth * MaximumWindowWidthFraction);
    const float width = (std::min)(
        maximum,
        (std::max)({ MinimumWindowWidth, demandedByButtons, demandedByQuestion }));
    if (mWindowRect)
    {
        mWindowRect->SetOffsetMin({ -width * 0.5f, 0.0f });
        mWindowRect->SetOffsetMax({ width * 0.5f, 0.0f });
    }

    // 라벨 하나가 받을 폭이다. 상한에 걸려 라벨이 그보다 길면 말줄임한다.
    const float labelBudget = count > 0
        ? width / slots - Padding - ButtonLabelInset * 2.0f
        : 0.0f;
    mVisibleButtonCount = count;
    for (std::size_t index = 0; index < mButtons.size(); ++index)
    {
        ChoiceButton& entry = mButtons[index];
        if (!entry.object)
        {
            continue;
        }
        const bool used = index < count;
        entry.object->SetActive(used);
        if (entry.button)
        {
            // 막힌 선택지는 보이되 눌리지 않는다. Button이 그 상태의 색까지 쥐고 있으므로
            // 흐리게 보이는 것도 함께 온다.
            const bool blocked = std::ranges::find(request.disabledChoices, index) !=
                request.disabledChoices.end();
            entry.button->SetInteractable(!blocked);
        }
        if (!used)
        {
            continue;
        }
        // 버튼들은 줄의 폭을 똑같이 나눠 갖는다. 고정 폭이면 창이 좁아질 때 마지막 버튼이 줄
            // 밖으로 밀려나고, 자를 마스크가 없으므로 잘리는 대신 사라진다 — 고를 수 없는
        // 선택지가 조용히 없어지는 것이라, 폭이 얼마든 다 남는 쪽을 택한다.
        if (RectTransform* const rect = entry.object->GetComponent<RectTransform>())
        {
            const float begin = static_cast<float>(index) / slots;
            const float end = static_cast<float>(index + 1) / slots;
            const float leftInset = index == 0 ? 0.0f : Padding * 0.5f;
            const float rightInset = index + 1 == count ? 0.0f : -Padding * 0.5f;
            rect->SetAnchorMin({ begin, 0.0f });
            rect->SetAnchorMax({ end, 1.0f });
            rect->SetOffsetMin({ leftInset, 0.0f });
            rect->SetOffsetMax({ rightInset, 0.0f });
        }
        if (entry.label)
        {
            // 상한에 걸려 라벨이 자기 자리보다 길면 말줄임한다. 자르는 자리를 정하는 것은
            // 폰트를 모르는 규칙이고, 재는 것만 잣대가 한다.
            const std::string& label = labels[index];
            const GameEngine::Core::TextFitResult fit = GameEngine::Core::FitTextToWidth(
                label, labelBudget, Ellipsis,
                [textMeasure](const std::string_view piece)
                { return MeasureWidth(textMeasure, std::string(piece), RowFontSize); });
            entry.label->SetText(
                fit.truncated ? label.substr(0, fit.length) + Ellipsis : label);
            entry.label->SetFontSize(RowFontSize);
        }
    }

    mShowing = true;
    if (mWindowObject)
    {
        mWindowObject->SetActive(true);
    }
}

void EditorConfirmationView::LogWhereTheChoicesAre(const ConfirmationRequest& request)
{
    const auto place = [](const ChoiceButton& entry)
    {
        std::string where = "(?)";
        if (entry.object)
        {
            if (const RectTransform* const rect = entry.object->GetComponent<RectTransform>())
            {
                const RectTransform::Rect resolved = rect->GetResolvedRect();
                where = "@(" + std::to_string(static_cast<int>(resolved.x)) + "," +
                    std::to_string(static_cast<int>(resolved.y)) + "," +
                    std::to_string(static_cast<int>(resolved.width)) + "," +
                    std::to_string(static_cast<int>(resolved.height)) + ")";
            }
        }
        return where;
    };

    std::string line = "Confirmation shown. title=\"" + request.title + "\" choices=[";
    for (std::size_t index = 0; index < request.choices.size() && index < mButtons.size(); ++index)
    {
        if (index > 0)
        {
            line += ", ";
        }
        line += request.choices[index] + place(mButtons[index]);
    }
    line += "]";
    if (!request.cancelLabel.empty() && request.choices.size() < mButtons.size())
    {
        line += " cancel=\"" + request.cancelLabel + "\"" + place(mButtons[request.choices.size()]);
    }
    // 사각형은 클라이언트 기준 물리 픽셀이다. 배율은 이미 곱해져 있으므로 밖에서 다시 곱하면
    // 빗나간다.
    GameEngine::Diagnostics::Debug::Log(line);
}

void EditorConfirmationView::Synchronize(
    const float contentScale, const float surfaceWidth, const float surfaceHeight,
    GameEngine::Platform::ITextMeasure* const textMeasure)
{
    static_cast<void>(surfaceHeight);

    // 배율은 캔버스가 쥔다. 오프셋과 글자 크기가 모두 논리 단위이므로, 그것을 화면 픽셀로
    // 옮기는 곱은 여기 한 번뿐이다 — 툴바가 자기 캔버스에 하는 일과 같다.
    if (mCanvasObject)
    {
        if (Canvas* const canvas = mCanvasObject->GetComponent<Canvas>())
        {
            canvas->SetScaleFactor(contentScale > 0.0f ? contentScale : 1.0f);
        }
    }

    const ConfirmationRequest* const current = mQueue.GetCurrent();
    if (!current)
    {
        Hide();
        return;
    }

    // 이번 프레임에 눌린 버튼을 먼저 읽는다. 답을 부르면 줄의 맨 앞이 바뀌므로, 바뀐 뒤에
    // 읽으면 방금 선 물음의 버튼을 이 프레임의 클릭으로 누르게 된다.
    std::optional<std::size_t> pressed;
    for (std::size_t index = 0; index < mVisibleButtonCount && index < mButtons.size(); ++index)
    {
        const ChoiceButton& entry = mButtons[index];
        if (entry.button && entry.object && entry.object->IsActiveInHierarchy() &&
            entry.button->IsInteractable() && entry.button->WasClickedThisFrame())
        {
            pressed = index;
        }
    }

    if (pressed)
    {
        // 취소 버튼은 목록의 맨 뒤에 있고, 그것은 "아무것도 하지 않음"이다.
        const std::size_t choiceCount = current->choices.size();
        const std::optional<std::size_t> answer =
            *pressed < choiceCount ? std::optional<std::size_t>(*pressed) : std::nullopt;
        mQueue.Answer(answer);
        // 답이 오면 다음 물음은 새 물음이다. 그 자리도 새로 적어야 한다.
        mLoggedCurrent = false;
        const ConfirmationRequest* const next = mQueue.GetCurrent();
        if (!next)
        {
            Hide();
            return;
        }
        ShowRequest(*next, contentScale, surfaceWidth, textMeasure);
        return;
    }

    ShowRequest(*current, contentScale, surfaceWidth, textMeasure);

    // 자리는 배치가 푼 뒤에야 안다. 그래서 물음이 선 다음 프레임에 한 번 적는다 — 아직 풀리지
    // 않은 사각형을 적으면 밖에서 그 자리를 눌러 엉뚱한 곳에 닿는다.
    if (!mLoggedCurrent && mWindowRect && !mWindowRect->GetResolvedRect().IsEmpty())
    {
        mLoggedCurrent = true;
        LogWhereTheChoicesAre(*current);
    }
}

}
