#include "pch.h"
#include "TextEditModel.h"

#include <algorithm>

namespace GameEngine::Core
{

namespace
{
    /// <summary>UTF-8 이어지는 바이트인지다. 문자 경계는 이 바이트가 아닌 자리다.</summary>
    [[nodiscard]] bool IsUtf8ContinuationByte(const char character)
    {
        return (static_cast<unsigned char>(character) & 0xC0) == 0x80;
    }
}

TextEditModel::Selection TextEditModel::GetSelection() const
{
    return { (std::min)(mCaret, mAnchor), (std::max)(mCaret, mAnchor) };
}

void TextEditModel::PlaceCaret(const std::size_t index, const bool extendSelection)
{
    mCaret = index;
    if (!extendSelection)
    {
        mAnchor = index;
    }
}

void TextEditModel::ResetTo(const std::string& text)
{
    mCaret = text.size();
    mAnchor = mCaret;
}

void TextEditModel::ClampTo(const std::string& text)
{
    mCaret = SnapToCharBoundary(text, (std::min)(mCaret, text.size()));
    mAnchor = SnapToCharBoundary(text, (std::min)(mAnchor, text.size()));
}

std::string_view TextEditModel::GetSelectedText(const std::string& text) const
{
    const Selection selection = GetSelection();
    if (!selection.HasSelection() || selection.end > text.size())
    {
        return {};
    }
    return std::string_view(text).substr(selection.begin, selection.end - selection.begin);
}

TextEditModel::Result TextEditModel::Apply(std::string& text, const Input& input)
{
    Result result;

    if (input.selectAll)
    {
        mAnchor = 0;
        mCaret = text.size();
        result.caretMoved = true;
    }

    if (input.copy || input.cut)
    {
        if (const std::string_view selected = GetSelectedText(text); !selected.empty())
        {
            result.clipboardText = std::string(selected);
            result.wroteClipboard = true;
        }
        if (input.cut && EraseSelection(text))
        {
            result.textChanged = true;
        }
    }
    if (input.paste)
    {
        result.textChanged |= Insert(text, input.pastedText);
    }

    result.textChanged |= Insert(text, input.typedText);

    if (input.backspace)
    {
        if (EraseSelection(text))
        {
            result.textChanged = true;
        }
        else if (mCaret > 0)
        {
            const std::size_t previous = PreviousCharBoundary(text, mCaret);
            text.erase(previous, mCaret - previous);
            mCaret = previous;
            mAnchor = previous;
            result.textChanged = true;
        }
    }
    if (input.deleteForward)
    {
        if (EraseSelection(text))
        {
            result.textChanged = true;
        }
        else if (mCaret < text.size())
        {
            text.erase(mCaret, NextCharBoundary(text, mCaret) - mCaret);
            result.textChanged = true;
        }
    }

    // 방향키. Shift는 앵커를 남겨 선택을 늘리고, 없으면 선택이 그 방향의 끝으로 접힌다.
    if (input.moveLeft)
    {
        const Selection selection = GetSelection();
        mCaret = !input.extendSelection && selection.HasSelection()
            ? selection.begin
            : PreviousCharBoundary(text, mCaret);
        if (!input.extendSelection)
        {
            mAnchor = mCaret;
        }
        result.caretMoved = true;
    }
    if (input.moveRight)
    {
        const Selection selection = GetSelection();
        mCaret = !input.extendSelection && selection.HasSelection()
            ? selection.end
            : NextCharBoundary(text, mCaret);
        if (!input.extendSelection)
        {
            mAnchor = mCaret;
        }
        result.caretMoved = true;
    }
    if (input.moveToStart)
    {
        mCaret = 0;
        if (!input.extendSelection)
        {
            mAnchor = 0;
        }
        result.caretMoved = true;
    }
    if (input.moveToEnd)
    {
        mCaret = text.size();
        if (!input.extendSelection)
        {
            mAnchor = mCaret;
        }
        result.caretMoved = true;
    }

    return result;
}

bool TextEditModel::EraseSelection(std::string& text)
{
    const Selection selection = GetSelection();
    if (!selection.HasSelection())
    {
        return false;
    }
    text.erase(selection.begin, selection.end - selection.begin);
    mCaret = selection.begin;
    mAnchor = selection.begin;
    return true;
}

bool TextEditModel::Insert(std::string& text, const std::string_view insertion)
{
    // 한 줄 필드다: 개행과 탭, 그 밖의 제어 문자는 편집 명령이지 내용이 아니다. 붙여넣기의
    // 여러 줄도 여기서 한 줄이 된다.
    std::string filtered;
    filtered.reserve(insertion.size());
    for (const char character : insertion)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte >= 0x20 && byte != 0x7F)
        {
            filtered.push_back(character);
        }
    }
    if (filtered.empty())
    {
        return false;
    }
    static_cast<void>(EraseSelection(text));
    text.insert(mCaret, filtered);
    mCaret += filtered.size();
    mAnchor = mCaret;
    return true;
}

std::size_t TextEditModel::PreviousCharBoundary(const std::string_view text, std::size_t index)
{
    if (index == 0)
    {
        return 0;
    }
    index = (std::min)(index, text.size());
    --index;
    while (index > 0 && IsUtf8ContinuationByte(text[index]))
    {
        --index;
    }
    return index;
}

std::size_t TextEditModel::NextCharBoundary(const std::string_view text, std::size_t index)
{
    if (index >= text.size())
    {
        return text.size();
    }
    ++index;
    while (index < text.size() && IsUtf8ContinuationByte(text[index]))
    {
        ++index;
    }
    return index;
}

std::size_t TextEditModel::SnapToCharBoundary(const std::string_view text, std::size_t index)
{
    while (index > 0 && index < text.size() && IsUtf8ContinuationByte(text[index]))
    {
        --index;
    }
    return index;
}

}
