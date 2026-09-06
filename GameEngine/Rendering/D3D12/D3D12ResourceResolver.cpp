#include "pch.h"
#include "D3D12ResourceResolver.h"

#include <windows.h>
#include <memory>
#include <wrl/client.h>

#include "D3D12ResourceDescriptions.h"
#include "ID3D12GraphicsDevice.h"
#include "../../Platform/Win32/Win32Diagnostics.h"
#include "../ShaderInterop.h"
#include "../../Assets/MeshData.h"
#include "../../Assets/TextureData.h"
#include "../ResolvedResourceCache.h"
#include "../RenderResourceCachePolicy.h"

namespace GameEngine::Rendering::D3D12
{

namespace
{
    /// <summary>
    /// 프레임이 이미 실어 온 픽셀을 받아 둔다. 이 백엔드는 이미지 파일을 열지 않는다:
    /// 프론트엔드가 디코딩했으므로 모든 백엔드가 같은 픽셀을 업로드한다. 검사하는 것은 자기
    /// 장치의 한계뿐인데, 그것은 프론트엔드가 알 방법이 없는 값이다.
    /// </summary>
    [[nodiscard]] bool LoadTexture(
        const unsigned int maximumTextureDimension,
        const std::shared_ptr<const Assets::TextureData>& texture,
        D3D12ResolvedTexture& resource)
    {
        const Assets::TextureData& image = *texture;
        if (image.width > maximumTextureDimension || image.height > maximumTextureDimension)
        {
            Diagnostics::Debug::LogError(
                "A decoded image exceeds this D3D12 device's texture limit. id=", image.id,
                ", width=", image.width, ", height=", image.height,
                ", limit=", maximumTextureDimension);
            return false;
        }

        // The frontend's id already distinguishes a material from a rasterized text image, so it can
        // be carried through to the texture uploader's shared binding cache unchanged.
        resource.id = image.id;
        resource.revision = image.revision;
        // Aliased, not copied: this points at the asset's pixels and keeps the asset alive.
        resource.pixels = { texture, &texture->pixels };
        // 이미지 픽셀은 sRGB다. 샘플러가 선형으로 풀도록 포맷이 그렇게 말한다.
        resource.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        resource.bytesPerPixel = Assets::TextureData::BytesPerPixel;
        resource.width = image.width;
        resource.height = image.height;
        return true;
    }

    [[nodiscard]] bool LoadTextTexture(
        const unsigned int maximumTextureDimension,
        const std::shared_ptr<const RasterizedTextImage>& textImage,
        D3D12ResolvedTexture& resource)
    {
        const RasterizedTextImage& image = *textImage;
        if (image.width > maximumTextureDimension || image.height > maximumTextureDimension)
        {
            // Reject dimensions beyond the device limit for both material textures and text coverage pages.
            Diagnostics::Debug::LogError(
                "A rasterized text image exceeds this D3D12 device's texture limit. id=", image.id,
                ", width=", image.width, ", height=", image.height,
                ", limit=", maximumTextureDimension);
            return false;
        }

        resource.id = image.id;
        resource.revision = image.revision;
        resource.pixels = { textImage, &textImage->alphaPixels };
        resource.format = DXGI_FORMAT_R8_UNORM;
        resource.bytesPerPixel = 1;
        resource.width = image.width;
        resource.height = image.height;
        return true;
    }
}

struct D3D12ResourceResolver::Implementation
{
    ID3D12Device* device = nullptr;
    GraphicsDeviceCapabilities capabilities;

    // A resolved entry here holds CPU pixels or a shared_ptr to the frame's mesh, not a GPU
    // resource, so one frame of retention is enough. What the GPU reads lives in the uploaders,
    // which retain for the frames in flight.
    ResolvedResourceCache<D3D12ResolvedGeometry> geometries{ MeshCacheBudget, "D3D12 mesh" };
    ResolvedResourceCache<D3D12ResolvedSkinnedGeometry> skinnedGeometries{
        MeshCacheBudget, "D3D12 skinned mesh" };
    ResolvedResourceCache<D3D12ResolvedTexture> textures{ TextureCacheBudget, "D3D12 texture" };
    ResolvedResourceCache<D3D12ResolvedTexture> texts{ TextCacheBudget, "D3D12 text" };
};

D3D12ResourceResolver::D3D12ResourceResolver()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D12ResourceResolver::~D3D12ResourceResolver() = default;

bool D3D12ResourceResolver::Initialize(ID3D12GraphicsDevice& graphicsDevice)
{
    ID3D12Device* const device = graphicsDevice.GetDevice();
    if (!device)
    {
        Diagnostics::Debug::LogError("A valid D3D12 device is required by the resource resolver.");
        return false;
    }

    if (mImplementation->device)
    {
        if (mImplementation->device == device)
        {
            return true;
        }
        Diagnostics::Debug::LogError(
            "The D3D12 resource resolver cannot be reinitialized for another device.");
        return false;
    }

    // Image decoding and text rasterization belong to the frontend. The frame supplies finished
    // pixels, so the resolver does not create a decoder or rasterizer.
    mImplementation->capabilities = graphicsDevice.GetCapabilities();
    if (!mImplementation->capabilities.IsValid())
    {
        Diagnostics::Debug::LogError("The D3D12 device reported no usable capabilities.");
        return false;
    }
    mImplementation->device = device;
    return true;
}

void D3D12ResourceResolver::BeginFrame(const bool advanceCaches)
{
    if (!advanceCaches)
    {
        return;
    }
    mImplementation->geometries.BeginFrame();
    mImplementation->skinnedGeometries.BeginFrame();
    mImplementation->textures.BeginFrame();
    mImplementation->texts.BeginFrame();
}

const D3D12ResolvedGeometry* D3D12ResourceResolver::ResolveGeometry(
    const RenderFrame& frame,
    const GeometryHandle handle)
{
    Implementation& data = *mImplementation;
    const Geometry* geometry = frame.GetGeometry(handle);
    if (!geometry || !geometry->data || !geometry->data->IsValid() || !data.device)
    {
        return nullptr;
    }

    // No GPU work happens here. Filling a default-heap buffer needs a command list, and a resolver
    // must not require one, so this only hands the frame's mesh on to D3D12MeshUploader. The entry
    // is charged no bytes because the vertices belong to the frame; the entry count is what bounds
    // this cache, and the uploader's own budget bounds the GPU memory.
    //
    // The frontend's mesh id is never reused, which is what makes it safe to key on.
    return data.geometries.Resolve(
        geometry->data->id,
        0,
        [geometry](D3D12ResolvedGeometry& resource)
        {
            resource.mesh = geometry->data;
            return true;
        });
}

const D3D12ResolvedSkinnedGeometry* D3D12ResourceResolver::ResolveSkinnedGeometry(
    const RenderFrame& frame,
    const SkinnedGeometryHandle handle)
{
    Implementation& data = *mImplementation;
    const SkinnedGeometry* geometry = frame.GetSkinnedGeometry(handle);
    if (!geometry || !geometry->data || !geometry->data->IsValid() || !data.device)
    {
        return nullptr;
    }

    // D3D12ResolvedGeometry와 같은 이유로 여기서는 GPU 작업을 하지 않는다 — 프레임의 스킨드
    // 메시를 D3D12SkinnedMeshUploader로 그대로 건넬 뿐이다.
    return data.skinnedGeometries.Resolve(
        geometry->data->id,
        0,
        [geometry](D3D12ResolvedSkinnedGeometry& resource)
        {
            resource.mesh = geometry->data;
            return true;
        });
}

const D3D12ResolvedTexture* D3D12ResourceResolver::ResolveMaterial(
    const RenderFrame& frame,
    const MaterialHandle handle)
{
    Implementation& data = *mImplementation;
    const Material* material = frame.GetMaterial(handle);
    if (!material || !material->baseColorTexture || !material->baseColorTexture->IsValid())
    {
        return nullptr;
    }

    // The frontend's image id is never reused, which is what makes it safe to cache an upload
    // against without re-examining the pixels behind it.
    const std::shared_ptr<const Assets::TextureData>& texture = material->baseColorTexture;

    // 같은 id도 새 객체를 실을 수 있으므로 revision과 픽셀 소유자를 각각 확인한다. 주소와
    // 제어 블록이 모두 같을 때만 alias 재대입을 생략해야 새 픽셀의 수명이 보장된다.
    if (D3D12ResolvedTexture* const cached = data.textures.FindMutable(texture->id))
    {
        cached->revision = texture->revision;
        if (cached->pixels.get() != &texture->pixels ||
            cached->pixels.owner_before(texture) || texture.owner_before(cached->pixels))
        {
            cached->pixels = { texture, &texture->pixels };
        }
        return cached;
    }
    return data.textures.Resolve(
        texture->id,
        texture->pixels.size(),
        [&data, &texture](D3D12ResolvedTexture& resource)
        {
            return LoadTexture(data.capabilities.maximumTextureDimension, texture, resource);
        });
}

const D3D12ResolvedTexture* D3D12ResourceResolver::ResolveText(const TextDraw& draw)
{
    Implementation& data = *mImplementation;
    // The frontend filled these atlas pixels, so this backend only uploads them. The page id is
    // stable and never reused, which is what makes it safe to cache an upload against.
    if (!draw.page || !draw.page->IsValid())
    {
        return nullptr;
    }

    const std::shared_ptr<const RasterizedTextImage>& textImage = draw.page;
    // 페이지가 자라면 id는 같아도 객체는 새것이다. 발행된 그림은 그것을 실은 프레임이
    // 끝까지 보고, 새 글리프를 담은 그림은 별도 객체로 다음 revision을 받는다.
    // revision과 함께 픽셀 출처도 다시 연결해야 업로더가 그 revision의 실제 픽셀을 읽는다.
    if (D3D12ResolvedTexture* const cached = data.texts.FindMutable(textImage->id))
    {
        cached->revision = textImage->revision;
        if (cached->pixels.get() != &textImage->alphaPixels ||
            cached->pixels.owner_before(textImage) || textImage.owner_before(cached->pixels))
        {
            cached->pixels = { textImage, &textImage->alphaPixels };
        }
        return cached;
    }
    // Single-channel coverage, so one byte per pixel.
    return data.texts.Resolve(
        textImage->id,
        textImage->alphaPixels.size(),
        [&data, &textImage](D3D12ResolvedTexture& resource)
        {
            return LoadTextTexture(data.capabilities.maximumTextureDimension, textImage, resource);
        });
}

}
