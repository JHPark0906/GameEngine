#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "RenderFrame.h"
#include "../Platform/ITextRasterizer.h"

namespace GameEngine::Rendering
{

/// <summary>아틀라스 안 글리프 하나의 정체성이다: 어느 face의 어느 크기의 어느 글리프인지.</summary>
struct GlyphKey
{
    std::uint64_t fontKey = 0;
    /// <summary>폰트 크기의 float 비트 패턴이다. 같은 픽셀을 만드는 크기만 슬롯을 공유한다.</summary>
    std::uint32_t fontSizeBits = 0;
    std::uint16_t glyphId = 0;

    [[nodiscard]] bool operator==(const GlyphKey&) const = default;
};

/// <summary>GlyphKey를 해시 컨테이너의 키로 쓰기 위한 해시이다.</summary>
struct GlyphKeyHash
{
    [[nodiscard]] std::size_t operator()(const GlyphKey& key) const
    {
        std::uint64_t hash = key.fontKey;
        hash ^= (static_cast<std::uint64_t>(key.fontSizeBits) << 16) ^ key.glyphId;
        hash *= 0x9E3779B97F4A7C15ull;
        return static_cast<std::size_t>(hash ^ (hash >> 32));
    }
};

/// <summary>
/// 아틀라스가 답하는 글리프 슬롯이다. 잉크 없는 글리프 — 공백 — 는 페이지 없이 hasInk가
/// 거짓이며, 배치는 하되 그릴 것이 없다는 뜻이다.
/// </summary>
struct GlyphAtlasSlot
{
    /// <summary>이 글리프의 픽셀이 사는 페이지다. hasInk가 거짓이면 null이다.</summary>
    std::shared_ptr<const RasterizedTextImage> page;
    /// <summary>페이지 안의 UV 사각형이다.</summary>
    float u = 0.0f;
    float v = 0.0f;
    float uWidth = 0.0f;
    float vHeight = 0.0f;
    /// <summary>비트맵 픽셀 크기다.</summary>
    unsigned int width = 0;
    unsigned int height = 0;
    /// <summary>글리프 배치 원점에서 비트맵 좌상단까지의 픽셀 거리다.</summary>
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool hasInk = false;
};

/// <summary>
/// 글리프 비트맵들을 R8 커버리지 페이지에 모아 두는 캐시다. 문자열이 바뀌어도 같은 글자는
/// 같은 슬롯을 다시 쓰므로, 텍스트가 매 프레임 바뀌는 장면의 비용이 "새 글리프 몇 개"로
/// 제한된다.
///
/// 페이지 그림은 발행되고 나면 바뀌지 않는다. 글리프를 더하는 것은 <b>새 그림을 만드는 일</b>이며,
/// 옛 그림은 만들어진 그대로 얼어붙는다. 프레임이 그것을 shared_ptr로 싣고 가 렌더 스레드에서
/// 읽는 동안 게임 스레드는 다음 프레임을 만들기 때문이다 — 그때 픽셀이 바뀌면 두 스레드가 같은
/// 바이트를 두고 만나고, RenderFrame이 「값과 const shared_ptr만 싣는다」고 적어 둔 계약이 이
/// 한 타입에서 깨진다. 복사 비용은 새 글리프를 만난 때만 치러지므로 예열이 끝나면 0이 된다.
///
/// 페이지는 고정 크기로 만들어지고 가득 차면 다음 페이지가 열린다. 페이지 수 상한에 닿으면
/// 아틀라스는 에포크를 올리며 처음부터 다시 시작하는데, 이때 이전 페이지 객체는 건드리지
/// 않는다: 페이지가 shared_ptr라서 그것을 참조하는 캐시된 배치와 in-flight 프레임은 계속
/// 유효한 픽셀을 본다.
/// </summary>
class GlyphAtlas final
{
public:
    /// <summary>페이지 한 변의 픽셀이다. R8 한 페이지가 1MB다.</summary>
    static constexpr unsigned int PageSize = 1024;
    /// <summary>동시에 살아 있는 페이지 수 상한이다. 넘으면 에포크가 오르며 다시 시작한다.</summary>
    static constexpr std::size_t MaximumPages = 8;

    GlyphAtlas();
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    /// <summary>
    /// 키의 슬롯을 반환하고, 아직 없으면 rasterize가 준 비트맵을 페이지에 넣어 만든다.
    /// rasterize 실패와 페이지보다 큰 글리프는 기록되어 다시 시도되지 않으며 null이 답이다.
    /// 반환된 포인터는 다음 Resolve까지 유효하고, 슬롯이 가리키는 페이지는 shared_ptr라
    /// 복사해 간 뒤에는 아틀라스의 수명과 무관하다.
    /// </summary>
    /// <param name="key">글리프의 정체성이다.</param>
    /// <param name="rasterize">캐시 미스에서 비트맵을 만드는 호출이다.</param>
    [[nodiscard]] const GlyphAtlasSlot* Resolve(
        const GlyphKey& key,
        const std::function<bool(Platform::RasterizedGlyph&)>& rasterize);

    /// <summary>
    /// 지금까지 몇 번 처음부터 다시 시작했는지다. 아틀라스 슬롯을 캐시해 둔 쪽은 에포크가
    /// 바뀌면 자기 배치를 다시 만들어야 새 페이지를 가리킨다 — 이전 배치도 그리기는 유효하지만,
    /// 버려진 페이지를 계속 쥐고 있으면 그 메모리가 살아남는다.
    /// </summary>
    [[nodiscard]] std::uint64_t GetEpoch() const;

    /// <summary>
    /// 발행된 페이지 그림이 바뀔 때마다 오르는 값이다. 배치를 캐시해 둔 쪽은 이 값이 달라지면
    /// 다시 배치해야 한다 — 옛 그림도 그리기에는 여전히 유효하지만, 계속 쥐고 있으면 한 id
    /// 아래 두 그림이 같은 프레임에 실리고 그 메모리도 살아남는다.
    /// </summary>
    [[nodiscard]] std::uint64_t GetGeneration() const;

    /// <summary>
    /// 지금까지 만든 페이지 그림의 수다. 글리프가 더해질 때마다 하나씩 늘고, <b>더할 글리프가
    /// 없으면 늘지 않는다</b>.
    ///
    /// 이 값을 밖에서 볼 수 있게 한 이유는 그것이 이 설계를 고른 근거이기 때문이다: 자랄 때
    /// 페이지를 복사하는 비용은 예열 동안만 치러지고 정상 상태에서는 0이어야 한다. 시험이
    /// 그것을 짐작이 아니라 세어서 확인한다.
    /// </summary>
    [[nodiscard]] std::uint64_t GetSnapshotCount() const;

    /// <summary>살아 있는 페이지 수다. 테스트가 페이지 증설과 에포크 리셋을 관찰하는 데 쓴다.</summary>
    [[nodiscard]] std::size_t GetPageCount() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
