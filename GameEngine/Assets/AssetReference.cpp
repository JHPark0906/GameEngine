#include "pch.h"
#include "AssetReference.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace GameEngine::Assets
{

AssetReference::AssetReference(std::filesystem::path path, const std::uint32_t localId)
    : mPath(std::move(path)), mLocalId(localId)
{
}
AssetReference::AssetReference(const Core::Guid guid, const std::uint32_t localId)
    : mLocalId(localId), mGuid(guid)
{
}


std::string AssetReference::ToString() const
{
    // 저장 형식이다. 정체성으로 가리키는 참조는 정체성으로, 경로로 가리키는 참조는 경로로 적힌다.
    // 사람에게 보일 글자는 이것이 아니다 — 그것은 DescribeAssetReference의 몫이다.
    std::string text = mGuid.IsValid() ? mGuid.ToString() : mPath.generic_string();
    if (!IsMainAsset())
    {
        text += '#';
        text += std::to_string(mLocalId);
    }
    return text;
}

AssetReference AssetReference::Parse(const std::string_view text)
{
    if (text.empty())
    {
        return {};
    }

    const std::size_t separator = text.rfind('#');
    if (separator == std::string_view::npos || separator + 1 == text.size())
    {
        if (const std::optional<Core::Guid> guid = Core::Guid::Parse(text))
        {
            return AssetReference{ *guid };
        }
        return AssetReference{ std::filesystem::path(text) };
    }

    const std::string_view suffix = text.substr(separator + 1);
    std::uint32_t localId = 0;
    const char* const first = suffix.data();
    const char* const last = first + suffix.size();
    const std::from_chars_result parsed = std::from_chars(first, last, localId);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
    {
        // The '#' belongs to the file name rather than being a sub-asset qualifier.
        return AssetReference{ std::filesystem::path(text) };
    }

    const std::string_view head = text.substr(0, separator);
    if (const std::optional<Core::Guid> guid = Core::Guid::Parse(head))
    {
        return AssetReference{ *guid, localId };
    }
    return AssetReference{ std::filesystem::path(head), localId };
}

}
