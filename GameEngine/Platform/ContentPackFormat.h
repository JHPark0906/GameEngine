#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GameEngine::Platform::ContentPackFormat
{

/// <summary>
/// 프로젝트가 자기를 실행하는 실행 파일 안에 저장되는 방식이다.
///
/// 팩은 완성된 실행 파일 뒤에 덧붙인다. 콘텐츠를 C++ 바이트 배열로 컴파일하지 않으므로
/// 콘텐츠만 바꾸면 팩을 다시 쓰면 되고 실행 파일을 다시 링크할 필요가 없다.
///
/// 레이아웃은 실행 파일 자체 이미지의 끝에서부터 이렇다:
///
///     [ 파일 데이터, blob이 하나씩 이어짐 ]
///     [ 인덱스: { pathLength, path, offset, size } 레코드 entryCount개 ]
///     [ footer: entryCount, indexOffset, packSize, magic ]
///
/// footer가 맨 끝의 고정 크기라는 것이 팩을 찾을 수 있게 하는 방법이다: 리더가 끝으로 가서
/// footer만큼 되짚어 나머지가 어디 있는지 알아낸다. 오프셋은 파일이 아니라 팩의 시작 기준이라서,
/// 덧붙이기는 실행 파일이 얼마나 컸는지에 의존하지 않는다.
///
/// 모든 숫자는 리틀 엔디언이고 메모리에서 복사되는 대신 한 바이트씩 쓰이므로, 한 머신에서
/// 만들어진 팩은 어느 쪽의 네이티브 방식이 무엇이든 다른 머신에서 읽힌다.
/// </summary>
inline constexpr std::string_view Magic = "GEPACK01";

/// <summary>entryCount (4) + indexOffset (8) + packSize (8) + magic (8)이다.</summary>
inline constexpr std::size_t FooterSize = 4 + 8 + 8 + Magic.size();

/// <summary>
/// 팩이 담았다고 주장할 수 있는 것의 상한이다. 손상되거나 악의적인 footer가, 숫자가 엉터리임을
/// 알아채기 전에 리더에게 수십억 항목짜리 인덱스를 할당시키는 일이 없게 한다.
/// </summary>
inline constexpr std::uint32_t MaximumEntryCount = 1u << 20;

/// <summary>팩 항목이 가질 수 있는 가장 긴 경로이다. 같은 이유에서다.</summary>
inline constexpr std::uint32_t MaximumPathLength = 4096;

[[nodiscard]] constexpr std::uint32_t ReadUInt32(const std::byte* const bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] constexpr std::uint64_t ReadUInt64(const std::byte* const bytes)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

[[nodiscard]] constexpr std::array<std::byte, 4> WriteUInt32(const std::uint32_t value)
{
    return {
        static_cast<std::byte>(value & 0xff),
        static_cast<std::byte>((value >> 8) & 0xff),
        static_cast<std::byte>((value >> 16) & 0xff),
        static_cast<std::byte>((value >> 24) & 0xff),
    };
}

[[nodiscard]] constexpr std::array<std::byte, 8> WriteUInt64(const std::uint64_t value)
{
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < 8; ++index)
    {
        bytes[index] = static_cast<std::byte>((value >> (index * 8)) & 0xff);
    }
    return bytes;
}

}
