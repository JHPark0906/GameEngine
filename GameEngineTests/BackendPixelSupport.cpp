#include "BackendPixelSupport.h"

#include <cstddef>

#include "Rendering/GraphicsBackend.h"

namespace TestSupport
{

Rgba ReadPixel(
    const GameEngine::Rendering::CapturedImage& image, const unsigned int x, const unsigned int y)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) *
        GameEngine::Rendering::CapturedImage::BytesPerPixel;
    return {
        static_cast<unsigned char>(image.pixels[offset]),
        static_cast<unsigned char>(image.pixels[offset + 1]),
        static_cast<unsigned char>(image.pixels[offset + 2]),
        static_cast<unsigned char>(image.pixels[offset + 3]) };
}

std::string Describe(const Rgba& color)
{
    return "(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
        std::to_string(color.b) + ", " + std::to_string(color.a) + ")";
}

Rgba Quantize(const GameEngine::Math::Color& color)
{
    const auto quantize = [](const float value)
    {
        return static_cast<unsigned char>(value * 255.0f + 0.5f);
    };
    return { quantize(color.r), quantize(color.g), quantize(color.b), quantize(color.a) };
}

std::shared_ptr<const GameEngine::Assets::TextureData> MakeHalvedTexture(
    const std::uint64_t id, const Rgba& upper, const Rgba& lower)
{
    auto texture = std::make_shared<GameEngine::Assets::TextureData>();
    texture->id = id;
    texture->width = 8;
    texture->height = 8;
    texture->pixels.resize(texture->GetByteSize());
    for (unsigned int y = 0; y < texture->height; ++y)
    {
        const Rgba& color = y < texture->height / 2 ? upper : lower;
        for (unsigned int x = 0; x < texture->width; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * texture->width + x) * 4;
            texture->pixels[offset] = static_cast<std::byte>(color.r);
            texture->pixels[offset + 1] = static_cast<std::byte>(color.g);
            texture->pixels[offset + 2] = static_cast<std::byte>(color.b);
            texture->pixels[offset + 3] = static_cast<std::byte>(color.a);
        }
    }
    return texture;
}

std::shared_ptr<const GameEngine::Assets::TextureData> MakeRoundedTexture(
    const std::uint64_t id, const unsigned int extent, const float cornerRadius,
    const Rgba& fill, const Rgba& carved)
{
    auto texture = std::make_shared<GameEngine::Assets::TextureData>();
    texture->id = id;
    texture->width = extent;
    texture->height = extent;
    texture->pixels.resize(texture->GetByteSize());
    const auto span = static_cast<float>(extent);
    for (unsigned int y = 0; y < extent; ++y)
    {
        for (unsigned int x = 0; x < extent; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const bool inCornerSquare = (px < cornerRadius || px > span - cornerRadius) &&
                (py < cornerRadius || py > span - cornerRadius);
            const float centreX = px < cornerRadius ? cornerRadius : span - cornerRadius;
            const float centreY = py < cornerRadius ? cornerRadius : span - cornerRadius;
            const float dx = px - centreX;
            const float dy = py - centreY;
            const bool outside =
                inCornerSquare && dx * dx + dy * dy > cornerRadius * cornerRadius;
            const Rgba& color = outside ? carved : fill;
            const std::size_t offset = (static_cast<std::size_t>(y) * extent + x) * 4;
            texture->pixels[offset] = static_cast<std::byte>(color.r);
            texture->pixels[offset + 1] = static_cast<std::byte>(color.g);
            texture->pixels[offset + 2] = static_cast<std::byte>(color.b);
            texture->pixels[offset + 3] = static_cast<std::byte>(color.a);
        }
    }
    return texture;
}

std::vector<const GameEngine::Rendering::GraphicsBackendDescriptor*> SupportedBackends()
{
    std::vector<const GameEngine::Rendering::GraphicsBackendDescriptor*> supported;
    for (const GameEngine::Rendering::GraphicsBackendDescriptor& backend :
         GameEngine::Rendering::GraphicsBackendRegistry::GetBackends())
    {
        if (backend.IsComplete() && backend.IsSupported())
        {
            supported.push_back(&backend);
        }
    }
    return supported;
}

}
