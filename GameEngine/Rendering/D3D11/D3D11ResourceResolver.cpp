#include "pch.h"
#include "D3D11ResourceResolver.h"


#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include <wrl/client.h>

#include "ID3D11GraphicsDevice.h"
#include "../ShaderInterop.h"
#include "../ResolvedResourceCache.h"
#include "../RenderResourceCachePolicy.h"
#include "../../Assets/MeshData.h"
#include "../../Assets/SkinnedMeshData.h"
#include "../../Assets/TextureData.h"
#include "../TextPageUpload.h"
#include "../../Diagnostics/Debug.h"

namespace GameEngine::Rendering::D3D11
{

namespace
{
    /// <summary>
    /// 프레임이 이미 실어 온 형상을 업로드한다. 이 백엔드는 모델 파일을 열지 않는다:
    /// 프론트엔드가 임포트했으므로 모든 백엔드가 같은 정점을 업로드한다.
    /// </summary>
    [[nodiscard]] bool LoadMesh(
        ID3D11Device& device,
        const Assets::MeshData& mesh,
        D3D11ResolvedGeometry& resource)
    {
        constexpr std::size_t MaximumBufferSize = (std::numeric_limits<UINT>::max)();
        if (mesh.vertices.size() > MaximumBufferSize / sizeof(MeshVertex) ||
            mesh.indices.size() > MaximumBufferSize / sizeof(std::uint32_t))
        {
            Diagnostics::Debug::LogError(
                "D3D11 mesh buffers exceed the API size limit. id=", mesh.id);
            return false;
        }

        const std::vector<MeshVertex>& vertices = mesh.vertices;
        const std::size_t vertexBufferSize = vertices.size() * sizeof(MeshVertex);
        const std::size_t indexBufferSize = mesh.indices.size() * sizeof(std::uint32_t);

        D3D11_BUFFER_DESC descriptor = {};
        descriptor.ByteWidth = static_cast<UINT>(vertexBufferSize);
        descriptor.Usage = D3D11_USAGE_IMMUTABLE;
        descriptor.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initialData = { vertices.data() };
        HRESULT result = device.CreateBuffer(
            &descriptor, &initialData, resource.vertexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            Diagnostics::Debug::LogError("Failed to create the D3D11 mesh vertex buffer. id=", mesh.id);
            return false;
        }

        descriptor.ByteWidth = static_cast<UINT>(indexBufferSize);
        descriptor.BindFlags = D3D11_BIND_INDEX_BUFFER;
        initialData.pSysMem = mesh.indices.data();
        result = device.CreateBuffer(
            &descriptor, &initialData, resource.indexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            Diagnostics::Debug::LogError("Failed to create the D3D11 mesh index buffer. id=", mesh.id);
            return false;
        }
        resource.indexCount = static_cast<UINT>(mesh.indices.size());
        resource.byteSize = vertexBufferSize + indexBufferSize;
        // 업로드마다 로그하지 않는다: 동적 텍스처와 텍스트는 프레임마다 업로드될 수 있고, 그
        // 로그가 에디터 콘솔에 보이면 콘솔을 그리는 일이 새 로그를 낳는 되먹임이 된다.
        return true;
    }

    /// <summary><see cref="LoadMesh"/>와 같지만 정점 형식이 <c>SkinnedMeshVertex</c>다.</summary>
    [[nodiscard]] bool LoadSkinnedMesh(
        ID3D11Device& device,
        const Assets::SkinnedMeshData& mesh,
        D3D11ResolvedSkinnedGeometry& resource)
    {
        constexpr std::size_t MaximumBufferSize = (std::numeric_limits<UINT>::max)();
        if (mesh.vertices.size() > MaximumBufferSize / sizeof(SkinnedMeshVertex) ||
            mesh.indices.size() > MaximumBufferSize / sizeof(std::uint32_t))
        {
            Diagnostics::Debug::LogError(
                "D3D11 skinned mesh buffers exceed the API size limit. id=", mesh.id);
            return false;
        }

        const std::vector<SkinnedMeshVertex>& vertices = mesh.vertices;
        const std::size_t vertexBufferSize = vertices.size() * sizeof(SkinnedMeshVertex);
        const std::size_t indexBufferSize = mesh.indices.size() * sizeof(std::uint32_t);

        D3D11_BUFFER_DESC descriptor = {};
        descriptor.ByteWidth = static_cast<UINT>(vertexBufferSize);
        descriptor.Usage = D3D11_USAGE_IMMUTABLE;
        descriptor.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initialData = { vertices.data() };
        HRESULT result = device.CreateBuffer(
            &descriptor, &initialData, resource.vertexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            Diagnostics::Debug::LogError(
                "Failed to create the D3D11 skinned mesh vertex buffer. id=", mesh.id);
            return false;
        }

        descriptor.ByteWidth = static_cast<UINT>(indexBufferSize);
        descriptor.BindFlags = D3D11_BIND_INDEX_BUFFER;
        initialData.pSysMem = mesh.indices.data();
        result = device.CreateBuffer(
            &descriptor, &initialData, resource.indexBuffer.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            Diagnostics::Debug::LogError(
                "Failed to create the D3D11 skinned mesh index buffer. id=", mesh.id);
            return false;
        }
        resource.indexCount = static_cast<UINT>(mesh.indices.size());
        resource.byteSize = vertexBufferSize + indexBufferSize;
        return true;
    }

    /// <summary>
    /// 불변 텍스처 하나와 그 셰이더 리소스 뷰를 만든다. 머티리얼과 래스터화된 텍스트는 포맷과
    /// pitch만 다르므로, 각자 사본을 갖는 대신 이것을 공유한다.
    /// </summary>
    [[nodiscard]] bool CreateTexture(
        ID3D11Device& device,
        const DXGI_FORMAT format,
        const void* const pixels,
        const UINT rowPitch,
        const UINT width,
        const UINT height,
        const bool updatable,
        D3D11ResolvedTexture& resource)
    {
        D3D11_TEXTURE2D_DESC descriptor = {};
        descriptor.Width = width;
        descriptor.Height = height;
        descriptor.MipLevels = 1;
        descriptor.ArraySize = 1;
        descriptor.Format = format;
        descriptor.SampleDesc.Count = 1;
        // 픽셀이 바뀔 텍스처는 UpdateSubresource를 받을 수 있게 DEFAULT로, 나머지는 불변으로.
        descriptor.Usage = updatable ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
        descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initialData = {};
        initialData.pSysMem = pixels;
        initialData.SysMemPitch = rowPitch;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device.CreateTexture2D(
                &descriptor, &initialData, texture.ReleaseAndGetAddressOf())) ||
            FAILED(device.CreateShaderResourceView(
                texture.Get(), nullptr, resource.shaderResourceView.ReleaseAndGetAddressOf())))
        {
            return false;
        }
        resource.texture = std::move(texture);
        resource.updatable = updatable;
        resource.width = width;
        resource.height = height;
        return true;
    }

    /// <summary>
    /// 프레임이 이미 실어 온 픽셀을 업로드한다. 이 백엔드는 이미지 파일을 열지 않는다:
    /// 프론트엔드가 디코딩했으므로 모든 백엔드가 같은 픽셀을 업로드한다. 검사하는 것은 자기
    /// 장치의 한계뿐인데, 그것은 프론트엔드가 알 방법이 없는 값이다.
    /// </summary>
    [[nodiscard]] bool LoadTexture(
        ID3D11Device& device,
        const unsigned int maximumTextureDimension,
        const Assets::TextureData& image,
        D3D11ResolvedTexture& resource)
    {
        if (image.width > maximumTextureDimension || image.height > maximumTextureDimension)
        {
            Diagnostics::Debug::LogError(
                "A decoded image exceeds this D3D11 device's texture limit. id=", image.id,
                ", width=", image.width, ", height=", image.height,
                ", limit=", maximumTextureDimension);
            return false;
        }

        // revision이 이미 0보다 크면 앞으로도 바뀔 텍스처다: 갱신 가능하게 만든다.
        if (!CreateTexture(
                // 이미지 픽셀은 sRGB다. 샘플러가 선형으로 풀도록 포맷이 그렇게 말한다.
                device, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, image.pixels.data(), image.GetRowPitch(),
                image.width, image.height, image.revision > 0, resource))
        {
            Diagnostics::Debug::LogError("Failed to create the D3D11 texture. id=", image.id);
            return false;
        }
        resource.revision = image.revision;
        resource.byteSize = image.pixels.size();
        return true;
    }

    [[nodiscard]] bool LoadTextTexture(
        ID3D11Device& device,
        const unsigned int maximumTextureDimension,
        const RasterizedTextImage& image,
        D3D11ResolvedTexture& resource)
    {
        if (image.width == 0 || image.height == 0 ||
            image.width > maximumTextureDimension ||
            image.height > maximumTextureDimension ||
            image.alphaPixels.size() != static_cast<std::size_t>(image.width) * image.height)
        {
            Diagnostics::Debug::LogError(
                "A rasterized text image exceeds this D3D11 device's texture limit. id=", image.id,
                ", width=", image.width, ", height=", image.height,
                ", limit=", maximumTextureDimension);
            return false;
        }

        // The text program samples coverage from a single-channel texture, so the rasterizer's alpha
        // pixels are uploaded as-is. Expanding them to RGBA would quadruple the text cache for no
        // benefit and would make D3D11 read a different format than D3D12 from the same shader.
        // 아틀라스 페이지는 글리프가 더해질 때마다 같은 id 아래에서 revision이 오르므로,
        // 갱신 가능한 텍스처로 만들어 제자리에 다시 올린다.
        // 픽셀보다 먼저 revision을 뽑는다. 왜 그 순서여야 하는지는 PlanTextPageUpload가 말한다.
        const std::optional<TextPageUpload> plan = PlanTextPageUpload(image, 0);
        if (!CreateTexture(
                device, DXGI_FORMAT_R8_UNORM, image.alphaPixels.data(), image.width,
                image.width, image.height, true, resource))
        {
            Diagnostics::Debug::LogError("Failed to create D3D11 text texture. id=", image.id);
            return false;
        }
        resource.revision = plan ? plan->revision : 0;
        // Single-channel coverage, so one byte per pixel.
        resource.byteSize = image.alphaPixels.size();
        return true;
    }
}

struct D3D11ResourceResolver::Implementation
{
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    GraphicsDeviceCapabilities capabilities;

    // D3D11 submits through the immediate context, so a resource is finished with when the frame
    // that used it is, and one frame of retention is enough.
    ResolvedResourceCache<D3D11ResolvedGeometry> geometries{ MeshCacheBudget, "D3D11 mesh" };
    ResolvedResourceCache<D3D11ResolvedSkinnedGeometry> skinnedGeometries{
        MeshCacheBudget, "D3D11 skinned mesh" };
    ResolvedResourceCache<D3D11ResolvedTexture> textures{ TextureCacheBudget, "D3D11 texture" };
    ResolvedResourceCache<D3D11ResolvedTexture> texts{ TextCacheBudget, "D3D11 text" };
};

D3D11ResourceResolver::D3D11ResourceResolver()
    : mImplementation(std::make_unique<Implementation>())
{
}

D3D11ResourceResolver::~D3D11ResourceResolver() = default;

bool D3D11ResourceResolver::Initialize(ID3D11GraphicsDevice& graphicsDevice)
{
    ID3D11Device* const device = graphicsDevice.GetDevice();
    if (!device)
    {
        Diagnostics::Debug::LogError("A valid D3D11 device is required by the resource resolver.");
        return false;
    }

    if (mImplementation->device)
    {
        if (mImplementation->device.Get() == device)
        {
            return true;
        }
        Diagnostics::Debug::LogError("The D3D11 resource resolver cannot be reinitialized for another device.");
        return false;
    }

    // Image decoding and text rasterization belong to the frontend. The frame supplies finished
    // pixels, so the resolver does not create a decoder or rasterizer.
    mImplementation->capabilities = graphicsDevice.GetCapabilities();
    if (!mImplementation->capabilities.IsValid())
    {
        Diagnostics::Debug::LogError("The D3D11 device reported no usable capabilities.");
        return false;
    }
    mImplementation->device = device;
    return true;
}

void D3D11ResourceResolver::BeginFrame(const bool advanceCaches)
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

const D3D11ResolvedGeometry* D3D11ResourceResolver::ResolveGeometry(
    const RenderFrame& frame,
    const GeometryHandle handle)
{
    Implementation& data = *mImplementation;
    const Geometry* geometry = frame.GetGeometry(handle);
    if (!geometry || !geometry->data || !geometry->data->IsValid() || !data.device)
    {
        return nullptr;
    }

    // The frontend's mesh id is never reused, which is what makes it safe to cache an upload
    // against without re-examining the vertices behind it.
    const Assets::MeshData& mesh = *geometry->data;
    return data.geometries.Resolve(
        mesh.id,
        mesh.GetByteSize(),
        [&data, &mesh](D3D11ResolvedGeometry& resource)
        {
            return LoadMesh(*data.device.Get(), mesh, resource);
        });
}

const D3D11ResolvedSkinnedGeometry* D3D11ResourceResolver::ResolveSkinnedGeometry(
    const RenderFrame& frame,
    const SkinnedGeometryHandle handle)
{
    Implementation& data = *mImplementation;
    const SkinnedGeometry* geometry = frame.GetSkinnedGeometry(handle);
    if (!geometry || !geometry->data || !geometry->data->IsValid() || !data.device)
    {
        return nullptr;
    }

    const Assets::SkinnedMeshData& mesh = *geometry->data;
    return data.skinnedGeometries.Resolve(
        mesh.id,
        mesh.GetByteSize(),
        [&data, &mesh](D3D11ResolvedSkinnedGeometry& resource)
        {
            return LoadSkinnedMesh(*data.device.Get(), mesh, resource);
        });
}

const D3D11ResolvedTexture* D3D11ResourceResolver::ResolveMaterial(
    const RenderFrame& frame,
    const MaterialHandle handle)
{
    Implementation& data = *mImplementation;
    const Material* material = frame.GetMaterial(handle);
    if (!material || !material->baseColorTexture || !material->baseColorTexture->IsValid() ||
        !data.device)
    {
        return nullptr;
    }

    // The frontend's image id is never reused, which is what makes it safe to cache an upload
    // against without re-examining the pixels behind it.
    const Assets::TextureData& image = *material->baseColorTexture;

    // 같은 id의 픽셀이 바뀌었으면 — 매 프레임 새 그림을 받는 뷰 텍스처 — 리소스를 다시 만들지
    // 않고 제자리에 다시 쓴다. 크기가 같다는 것은 계약이다: 크기가 바뀌면 새 id다.
    if (D3D11ResolvedTexture* const cached = data.textures.FindMutable(image.id))
    {
        if (cached->revision != image.revision && cached->updatable && cached->texture &&
            cached->width == image.width && cached->height == image.height)
        {
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
            data.device->GetImmediateContext(context.ReleaseAndGetAddressOf());
            context->UpdateSubresource(
                cached->texture.Get(), 0, nullptr, image.pixels.data(), image.GetRowPitch(), 0);
            cached->revision = image.revision;
        }
        return cached;
    }
    return data.textures.Resolve(
        image.id,
        image.pixels.size(),
        [&data, &image](D3D11ResolvedTexture& resource)
        {
            return LoadTexture(
                *data.device.Get(), data.capabilities.maximumTextureDimension, image, resource);
        });
}

const D3D11ResolvedTexture* D3D11ResourceResolver::ResolveText(const TextDraw& draw)
{
    Implementation& data = *mImplementation;
    // The frontend filled these atlas pixels, so this backend only uploads them. The page id is
    // stable and never reused, which is what makes it safe to cache an upload against.
    if (!data.device || !draw.page || !draw.page->IsValid())
    {
        return nullptr;
    }

    const RasterizedTextImage& image = *draw.page;
    // 같은 id의 페이지에 글리프가 더해졌으면 — revision이 올랐으면 — 리소스를 다시 만들지
    // 않고 제자리에 다시 쓴다. 이미 쓰인 슬롯의 픽셀은 바뀌지 않으므로 전체 재업로드는 언제나
    // 같은 그림 위에 새 글리프를 더한 그림이다.
    if (D3D11ResolvedTexture* const cached = data.texts.FindMutable(image.id))
    {
        const std::optional<TextPageUpload> plan =
            PlanTextPageUpload(image, cached->revision);
        if (plan && cached->updatable && cached->texture &&
            cached->width == image.width && cached->height == image.height)
        {
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
            data.device->GetImmediateContext(context.ReleaseAndGetAddressOf());
            context->UpdateSubresource(
                cached->texture.Get(), 0, nullptr, image.alphaPixels.data(), image.width, 0);
            cached->revision = plan->revision;
        }
        return cached;
    }
    // Single-channel coverage, so one byte per pixel.
    return data.texts.Resolve(
        image.id,
        image.alphaPixels.size(),
        [&data, &image](D3D11ResolvedTexture& resource)
        {
            return LoadTextTexture(
                *data.device.Get(), data.capabilities.maximumTextureDimension, image, resource);
        });
}

}
