#pragma once

#include <span>
#include <string_view>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>이미지 디코더가 만든, 빈틈없이 채워진 32비트 RGBA 픽셀이다.</summary>
struct DecodedImage
{
    std::vector<std::byte> pixels;
    unsigned int width = 0;
    unsigned int height = 0;

    static constexpr unsigned int BytesPerPixel = 4;

    [[nodiscard]] unsigned int GetRowPitch() const { return width * BytesPerPixel; }

    [[nodiscard]] bool IsValid() const
    {
        return width > 0 && height > 0 &&
            pixels.size() == static_cast<std::size_t>(width) * height * BytesPerPixel;
    }
};

/// <summary>
/// 호출자가 디코드에 거는 한계이다. 치수 한계는 픽셀을 소비할 쪽의 것이고 — 그래픽 백엔드에는
/// 플랫폼 계층이 알 리 없는 API 텍스처 한계가 있다 — 바이트 예산은 형식상 유효한 이미지가
/// 메모리를 소진하는 것을 막는다.
/// </summary>
struct ImageDecodeLimits
{
    unsigned int maximumDimension = 16384;
    std::size_t maximumBytes = 64ull * 1024ull * 1024ull;
};

/// <summary>플랫폼이 제공하는 것으로 이미지 파일을 RGBA 픽셀로 디코딩한다.</summary>
class IImageDecoder
{
public:
    virtual ~IImageDecoder() = default;

    IImageDecoder(const IImageDecoder&) = delete;
    IImageDecoder& operator=(const IImageDecoder&) = delete;
    IImageDecoder(IImageDecoder&&) = delete;
    IImageDecoder& operator=(IImageDecoder&&) = delete;

    /// <summary>디코더를 준비한다. 디코딩 전에 한 번 호출한다.</summary>
    [[nodiscard]] virtual bool Initialize() = 0;

    /// <summary>
    /// 인코딩된 이미지를 32비트 RGBA로 디코딩한다. 실패는 로그되고 결과는 건드리지 않는다.
    ///
    /// 열 경로가 아니라 바이트가 건네지므로 디코더는 결코 파일시스템을 만지지 않는다: 요청한
    /// 쪽이 이미지가 어디서 왔는지, 그것이 디렉터리인지 실행 파일 안의 블록인지 이미 안다.
    /// `description`은 진단이 인용하는 것일 뿐이라, 실패해도 어느 이미지였는지는 말해 준다.
    /// </summary>
    [[nodiscard]] virtual bool Decode(
        std::span<const std::byte> encodedBytes,
        std::string_view description,
        const ImageDecodeLimits& limits,
        DecodedImage& image) = 0;

protected:
    IImageDecoder() = default;
};

}
