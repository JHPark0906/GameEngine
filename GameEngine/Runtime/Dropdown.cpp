#include "pch.h"
#include "Dropdown.h"

#include "GameObject.h"
#include "PropertyDescriptor.h"
#include "RectTransform.h"
#include "TextRenderer.h"

#include <span>
#include <utility>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// Dropdown이 선언하는 속성들이다. 펼침과 강조는 여기 없다: 매 프레임 사람이 정하는 것이라,
    /// 파일에 적으면 다음 로드가 아무도 누르지 않은 목록을 펼친 채로 되살린다.
    /// </summary>
    std::span<const PropertyDescriptor> DropdownProperties()
    {
        static const PropertyDescriptor properties[] = {
            MakeProperty<Dropdown>("value", "Value", &Dropdown::GetValue, &Dropdown::SetValue),
            MakeProperty<Dropdown>(
                "optionHeight", "Option Height", &Dropdown::GetOptionHeight,
                &Dropdown::SetOptionHeight),
        };
        return properties;
    }
}

const ComponentType& Dropdown::StaticType()
{
    static const ComponentType type{
        "Dropdown", &Selectable::StaticType(), &DropdownProperties,
        &MakeComponentInstance<Dropdown> };
    return type;
}

void Dropdown::SetOptions(std::vector<std::string> options)
{
    mOptions = std::move(options);
    mChoice.ClampTo(mOptions.size());
    SynchronizeDisplay();
}

int Dropdown::GetValue() const
{
    const std::size_t selected = mChoice.GetSelected();
    return selected == Core::ChoiceModel::NoSelection ? -1 : static_cast<int>(selected);
}

void Dropdown::SetValue(const int value)
{
    mChoice.SetSelected(
        value < 0 ? Core::ChoiceModel::NoSelection : static_cast<std::size_t>(value),
        mOptions.size());
    SynchronizeDisplay();
}

void Dropdown::OnInteractableLost()
{
    // 펼쳐진 채로 남으면 누구도 접을 수 없다. 커서 위 표시도 함께 지운다.
    mChoice.Close();
    mHovered = false;
}

void Dropdown::Open()
{
    mChoice.Open(mOptions.size());
}

void Dropdown::Close()
{
    mChoice.Close();
}

float Dropdown::ResolveOptionHeight() const
{
    if (mOptionHeight > 0.0f)
    {
        return mOptionHeight;
    }
    const GameObject* const owner = GetGameObject();
    const RectTransform* const rect = owner ? owner->GetComponent<RectTransform>() : nullptr;
    return rect ? rect->GetResolvedRect().height : 0.0f;
}

bool Dropdown::Covers(const float x, const float y) const
{
    const GameObject* const owner = GetGameObject();
    const RectTransform* const rect = owner ? owner->GetComponent<RectTransform>() : nullptr;
    if (!rect)
    {
        return false;
    }
    if (rect->GetVisibleRect().Contains(x, y))
    {
        return true;
    }
    return OptionAt(x, y).has_value();
}

std::optional<std::size_t> Dropdown::OptionAt(const float x, const float y) const
{
    if (!mChoice.IsOpen() || mOptions.empty())
    {
        return std::nullopt;
    }
    const GameObject* const owner = GetGameObject();
    const RectTransform* const rect = owner ? owner->GetComponent<RectTransform>() : nullptr;
    if (!rect)
    {
        return std::nullopt;
    }
    const RectTransform::Rect header = rect->GetResolvedRect();
    const float optionHeight = ResolveOptionHeight();
    if (optionHeight <= 0.0f || x < header.x || x >= header.GetRight() || y < header.GetBottom())
    {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>((y - header.GetBottom()) / optionHeight);
    return index < mOptions.size() ? std::optional<std::size_t>(index) : std::nullopt;
}

void Dropdown::ApplyPointerState(
    const bool hovered, const bool clicked, const float cursorX, const float cursorY,
    const bool pressedElsewhere, const Core::ChoiceModel::Input& keys)
{
    mHovered = hovered;

    Core::ChoiceModel::Input input = keys;
    if (clicked)
    {
        if (const std::optional<std::size_t> option = OptionAt(cursorX, cursorY))
        {
            input.clicked = option;
        }
        else
        {
            input.toggle = true;
        }
    }
    else if (pressedElsewhere)
    {
        input.cancel = true;
    }
    if (hovered)
    {
        input.hovered = OptionAt(cursorX, cursorY);
    }

    const Core::ChoiceModel::Result result = mChoice.Apply(mOptions.size(), input);
    if (result.selectionChanged)
    {
        mValueChanged = true;
        SynchronizeDisplay();
    }
}

void Dropdown::SynchronizeDisplay()
{
    GameObject* const owner = GetGameObject();
    TextRenderer* const renderer = owner ? owner->GetComponent<TextRenderer>() : nullptr;
    if (!renderer)
    {
        return;
    }
    const std::size_t selected = mChoice.GetSelected();
    renderer->SetText(selected < mOptions.size() ? mOptions[selected] : std::string{});
}

void Dropdown::ClearFrameFlags()
{
    mValueChanged = false;
}

}
