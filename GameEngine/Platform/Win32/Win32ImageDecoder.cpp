#include "pch.h"
#include "Win32ImageDecoder.h"


#include <windows.h>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

#include "Win32ComApartment.h"
#include "Win32Diagnostics.h"

#pragma comment(lib, "windowscodecs.lib")

namespace GameEngine::Platform::Win32
{

struct Win32ImageDecoder::Implementation
{
    ComApartment comApartment;
    Microsoft::WRL::ComPtr<IWICImagingFactory> imagingFactory;
};

Win32ImageDecoder::Win32ImageDecoder()
    : mImplementation(std::make_unique<Implementation>())
{
}

Win32ImageDecoder::~Win32ImageDecoder() = default;

bool Win32ImageDecoder::Initialize()
{
    if (mImplementation->imagingFactory)
    {
        return true;
    }
    if (!mImplementation->comApartment.Initialize())
    {
        Diagnostics::Debug::LogError("Failed to initialize COM for image decoding.");
        return false;
    }
    const HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(mImplementation->imagingFactory.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        LogHResult("Creating the WIC imaging factory", result);
        return false;
    }
    return true;
}

bool Win32ImageDecoder::Decode(
    const std::span<const std::byte> encodedBytes,
    const std::string_view description,
    const ImageDecodeLimits& limits,
    DecodedImage& image)
{
    if (!mImplementation->imagingFactory)
    {
        Diagnostics::Debug::LogError("The image decoder was used before it was initialized.");
        return false;
    }
    if (encodedBytes.empty())
    {
        Diagnostics::Debug::LogError("An image has no bytes to decode. image=", description);
        return false;
    }
    if (encodedBytes.size() > (std::numeric_limits<DWORD>::max)())
    {
        Diagnostics::Debug::LogError("An image is too large to decode. image=", description);
        return false;
    }
    IWICImagingFactory& imagingFactory = *mImplementation->imagingFactory.Get();

    // WIC reads from a stream over the caller's bytes rather than opening the file itself. The
    // stream does not copy, so `encodedBytes` has to outlive the decoding below — it does, because
    // the caller holds it for the whole call.
    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT result = imagingFactory.CreateStream(stream.ReleaseAndGetAddressOf());
    if (SUCCEEDED(result))
    {
        result = stream->InitializeFromMemory(
            reinterpret_cast<BYTE*>(const_cast<std::byte*>(encodedBytes.data())),
            static_cast<DWORD>(encodedBytes.size()));
    }
    if (FAILED(result))
    {
        Diagnostics::Debug::LogError("Failed to read an image. image=", description);
        LogHResult("Image stream creation", result);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    result = imagingFactory.CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        Diagnostics::Debug::LogError("Failed to decode an image. image=", description);
        LogHResult("Image decoder creation", result);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    UINT width = 0;
    UINT height = 0;
    result = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
    if (FAILED(result) || FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0)
    {
        Diagnostics::Debug::LogError("Failed to read image metadata. image=", description);
        return false;
    }
    if (width > limits.maximumDimension || height > limits.maximumDimension)
    {
        Diagnostics::Debug::LogError(
            "An image exceeds the caller's dimension limit. image=", description,
            ", width=", width, ", height=", height);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    result = imagingFactory.CreateFormatConverter(converter.ReleaseAndGetAddressOf());
    if (FAILED(result) || FAILED(converter->Initialize(
            frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        Diagnostics::Debug::LogError("Failed to convert an image to RGBA. image=", description);
        return false;
    }

    const UINT rowPitch = width * DecodedImage::BytesPerPixel;
    const std::size_t pixelBufferSize = static_cast<std::size_t>(rowPitch) * height;
    if (pixelBufferSize > limits.maximumBytes ||
        pixelBufferSize > (std::numeric_limits<UINT>::max)())
    {
        Diagnostics::Debug::LogError(
            "An image exceeds the caller's decoded-pixel budget. image=", description);
        return false;
    }

    std::vector<std::byte> pixels;
    try
    {
        pixels.resize(pixelBufferSize);
    }
    catch (const std::bad_alloc&)
    {
        Diagnostics::Debug::LogError("Insufficient memory to decode an image. image=", description);
        return false;
    }
    result = converter->CopyPixels(
        nullptr, rowPitch, static_cast<UINT>(pixels.size()), reinterpret_cast<BYTE*>(pixels.data()));
    if (FAILED(result))
    {
        Diagnostics::Debug::LogError("Failed to decode image pixels. image=", description);
        LogHResult("Image pixel copy", result);
        return false;
    }

    image.pixels = std::move(pixels);
    image.width = width;
    image.height = height;
    return true;
}

}
