#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "../IImageDecoder.h"

namespace GameEngine::Platform::Win32
{

/// <summary>Windows Imaging Component로 이미지를 디코딩한다.</summary>
class Win32ImageDecoder final : public IImageDecoder
{
public:
    Win32ImageDecoder();
    ~Win32ImageDecoder() override;

    [[nodiscard]] bool Initialize() override;
    [[nodiscard]] bool Decode(
        std::span<const std::byte> encodedBytes,
        std::string_view description,
        const ImageDecodeLimits& limits,
        DecodedImage& image) override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
