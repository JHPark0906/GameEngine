#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

/// <summary>
/// 에셋 모듈 안에서만 쓰는 것들이다. <b>GameEngine/Assets 밖에서 include하지 않는다.</b>
///
/// 에셋 표, 페이로드 캐시, 매니페스트가 공유하는 계산을 한 곳에 둔다. 내용 해시처럼 파일에
/// 적히는 값은 쓰는 쪽과 읽는 쪽이 같은 규칙을 사용해야 패키지를 일관되게 해석한다.
/// </summary>
namespace GameEngine::Assets::Internal
{

/// <summary>FNV-1a의 오프셋 기준값이다.</summary>
inline constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ull;
/// <summary>FNV-1a의 소수다.</summary>
inline constexpr std::uint64_t FnvPrime = 1099511628211ull;

/// <summary>
/// 파일 내용의 해시다. 매니페스트에 적히고 다시 읽히는 값이므로 <b>한 벌이어야 한다</b>:
/// 쓰는 쪽과 읽는 쪽이 다른 계산을 하면 모든 에셋이 "바뀌었다"로 보인다.
/// </summary>
[[nodiscard]] inline std::uint64_t HashBytes(const std::span<const std::byte> bytes)
{
    std::uint64_t hash = FnvOffsetBasis;
    for (const std::byte value : bytes)
    {
        hash ^= static_cast<unsigned char>(value);
        hash *= FnvPrime;
    }
    return hash;
}

/// <summary>경로를 UTF-8로 적는다. 슬래시 형식이라 기계에 따라 갈리지 않는다.</summary>
[[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.generic_u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        reinterpret_cast<const char*>(value.data() + value.size())
    };
}

/// <summary>UTF-8로 적힌 경로를 되읽는다.</summary>
[[nodiscard]] inline std::filesystem::path Utf8ToPath(const std::string& value)
{
    const std::u8string utf8Value(
        reinterpret_cast<const char8_t*>(value.data()), value.size());
    return std::filesystem::path(utf8Value);
}

}
