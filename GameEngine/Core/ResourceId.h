#pragma once

#include <cstdint>

namespace GameEngine::Core
{

/// <summary>
/// id가 가리키는 완성 데이터의 종류이다.
///
/// 프론트엔드 캐시는 독립적으로 id 값을 발급하지만 백엔드 바인딩 캐시는 여러 종류의
/// 리소스를 함께 다룰 수 있다. 종류를 id 안에 태깅해 서로 다른 종류의 같은 값이 충돌하지
/// 않게 하므로, 공유 캐시도 id를 그대로 키로 사용할 수 있다.
/// </summary>
enum class ResourceIdDomain : std::uint64_t
{
    Mesh = 1,
    Texture = 2,
    Text = 3,
    /// <summary>
    /// 프레임마다 픽셀이 바뀌는 텍스처 — 에디터 뷰가 표시하는 캡처 이미지 같은 것 — 의
    /// 도메인이다. id는 결코 재사용되지 않으므로 매 프레임 새 id가 발급되고, 백엔드 캐시는
    /// 이것을 여느 텍스처처럼 다루며 예산이 낡은 것들을 밀어낸다.
    /// </summary>
    Dynamic = 4,
    /// <summary>디코딩된 오디오 클립 — PCM 샘플 버퍼 — 의 도메인이다.</summary>
    Audio = 5,
    /// <summary>
    /// 뼈 영향을 가진 메시 — <c>Assets::SkinnedMeshData</c> — 의 도메인이다. <c>Mesh</c>와 다른
    /// 도메인인 이유는 한 모델 파일이 정적 메시와 스킨드 메시를 함께 담을 수 있어서다: 같은
    /// 도메인을 쓰면 둘이 같은 로컬 id를 가질 때 id가 충돌한다.
    /// </summary>
    SkinnedMesh = 6,
};

/// <summary>
/// 종류와 그 종류의 값으로 id를 만든다. 값이 하위 56비트를 가지고 종류가 상위 8비트를 차지한다.
///
/// <b>그 56비트에 들어오는 것이 두 가지다.</b> 하나는 계수기다 — <c>Dynamic</c> 텍스처가 프레임마다
/// 하나씩 발급받으며, 60fps로 3,800만 년을 써야 소진되므로 「어떤 실행도 소진하지 못할 크기」라는
/// 말이 그쪽에는 그대로 맞다. 다른 하나는 해시다 — <see cref="Assets::ImportIdentity"/>가
/// <c>assetId ^ contentHash ^ localId</c>를 넣는다.
///
/// 둘은 같은 자리를 쓰지만 요구하는 것이 다르다. 계수기에 필요한 것은 <b>범위</b>이고 해시에
/// 필요한 것은 <b>충돌 저항</b>이다. 비트 수를 바꿀 때는 두 요구를 함께 고려해야 한다.
///
/// 해시 쪽의 값: 한 종류 안에 리소스가 R개일 때 충돌 확률은 대략 R²/2⁵⁷이다 — 만 개에서 7e-10,
/// 백만 개에서 7e-6. 충돌이
/// 나면 조용하다: 백엔드가 이 id로 캐시를 조회하므로 <b>다른 에셋의 픽셀이나 정점이 그려지고</b>,
/// id가 내용에서 나오므로 <b>실행마다 같은 자리에서 재현된다</b> — 가끔 이상한 것이 아니라 그
/// 에셋이 잘못 임포트된 것처럼 보인다.
///
/// 그래서 이 56이라는 수를 줄이거나 여기에 무언가를 더 실으려면 두 용도를 함께 보아야 한다.
/// 계수기만 보고 정하면 해시 쪽의 충돌 확률이 조용히 올라간다.
/// </summary>
[[nodiscard]] constexpr std::uint64_t MakeResourceId(
    const ResourceIdDomain domain, const std::uint64_t index)
{
    return (static_cast<std::uint64_t>(domain) << 56) | (index & 0x00FFFFFFFFFFFFFFull);
}

/// <summary>id가 만들어질 때의 종류이다. 진단에 쓰인다.</summary>
[[nodiscard]] constexpr ResourceIdDomain GetResourceIdDomain(const std::uint64_t id)
{
    return static_cast<ResourceIdDomain>(id >> 56);
}

}
