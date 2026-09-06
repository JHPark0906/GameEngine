#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace GameEngine::Core
{

/// <summary>
/// 128비트 식별자다. 무엇을 가리키는지도, 어디서 왔는지도 모른다 — 아는 것은 두 개의 64비트
/// 반쪽과 그것을 글자로 옮기는 방법뿐이라 화면도 파일도 없이 시험할 수 있다.
///
/// 32자리 소문자 16진수로 오간다. 구분자가 없으므로 경로와 헷갈리지 않고, 파일 이름으로도
/// 안전하다.
/// </summary>
struct Guid
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    /// <summary>무언가를 가리키는 값인지다. 양쪽이 모두 0인 것은 「없음」이다.</summary>
    [[nodiscard]] constexpr bool IsValid() const { return high != 0 || low != 0; }

    /// <summary>
    /// 64비트 조회 키로 접는다. 어느 절반도 버리지 않는 이유는 이 값이 다시 56비트로 잘려
    /// 리소스 id가 되기 때문이다 — 한쪽만 취하면 버린 절반의 정보가 영영 돌아오지 않고, UUID의
    /// 판본·변형 비트가 상위에 몰려 있어 실제 엔트로피는 절반보다도 줄어든다.
    ///
    /// 한쪽을 돌려서 섞는 이유는 그냥 XOR이 두 반쪽을 맞바꾼 값에 같은 키를 주기 때문이다.
    /// 무작위 guid에서 그런 짝이 나올 일은 없다시피 하지만, 두 정체성이 같은 키를 받는 것은
    /// 확률로 넘길 성질이 아니라 없앨 수 있는 성질이다.
    /// </summary>
    [[nodiscard]] constexpr std::uint64_t Fold() const
    {
        return high ^ ((low << 32) | (low >> 32));
    }

    /// <summary>32자리 소문자 16진수다. 유효하지 않으면 빈 문자열이다.</summary>
    [[nodiscard]] std::string ToString() const;

    /// <summary>
    /// 32자리 16진수를 읽는다. 길이가 다르거나 16진수가 아닌 글자가 있으면 비어 있다 — 경로와
    /// 구별하는 것이 이 엄격함이다.
    /// </summary>
    /// <param name="text">읽을 글자들이다.</param>
    [[nodiscard]] static std::optional<Guid> Parse(std::string_view text);

    /// <summary>글자들이 guid의 모양인지다. 읽어 보지 않고도 형식을 가를 수 있게 한다.</summary>
    [[nodiscard]] static bool LooksLikeGuid(std::string_view text);

    [[nodiscard]] constexpr bool operator==(const Guid&) const = default;
};

/// <summary>
/// 새 guid를 만든다. 버전 4 — 무작위다. 발급은 되돌릴 수 없는 동작이라 부르는 자리가 적어야
/// 한다.
/// </summary>
[[nodiscard]] Guid MakeGuid();

}

template <>
struct std::hash<GameEngine::Core::Guid>
{
    [[nodiscard]] std::size_t operator()(const GameEngine::Core::Guid& guid) const noexcept
    {
        return static_cast<std::size_t>(guid.Fold());
    }
};
