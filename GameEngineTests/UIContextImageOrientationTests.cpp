#include "UIContextImageOrientationTests.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "BackendPixelSupport.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Input.h"
#include "TestSupport.h"
#include "UI/UIContext.h"

using TestSupport::Describe;
using TestSupport::Expect;
using TestSupport::MakeHalvedTexture;
using TestSupport::ReadPixel;
using TestSupport::Rgba;
using TestSupport::SupportedBackends;

/// <summary>
/// UIContext가 그리는 이미지의 수직 방향을 실제 픽셀로 확인한다.
/// 프레임을 소유하는 모드는 픽셀 직교 카메라와 월드 공간 draw를 사용하고,
/// 오버레이 모드는 카메라를 유지한 채 화면 공간 draw를 사용한다.
/// 수직 보정의 책임이 다르므로 비대칭 이미지로 두 경로를 모두 검사한다.
/// </summary>
namespace
{
    constexpr unsigned int ImageWidth = 128;
    constexpr unsigned int ImageHeight = 128;

    /// <summary>그림의 위 절반과 아래 절반이다. 서로 다르므로 뒤집히면 두 표본이 맞바뀐다.</summary>
    constexpr Rgba UpperColor{ 240, 60, 20, 255 };
    constexpr Rgba LowerColor{ 20, 80, 220, 255 };

    /// <summary>그림을 놓는 자리다. 두 표본이 각각 자기 절반 한가운데에 떨어진다.</summary>
    constexpr float ImageLeft = 32.0f;
    constexpr float ImageTop = 24.0f;
    constexpr float ImageExtent = 48.0f;
    constexpr unsigned int SampleX = 56;
    constexpr unsigned int UpperSampleY = 36;
    constexpr unsigned int LowerSampleY = 60;

    [[nodiscard]] const char* NameOf(const GameEngine::UI::UISurfaceMode mode)
    {
        return mode == GameEngine::UI::UISurfaceMode::OwnFrame ? "own-frame" : "overlay";
    }

    /// <summary>
    /// 주어진 표면 모드의 UIContext로 이미지를 그려 실제 생성한 프레임을 얻는다.
    /// 수동 조립 프레임으로 대체하면 UIContext가 실은 좌표와 공간 모드를 검증할 수 없다.
    /// </summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildFrame(
        const GameEngine::UI::UISurfaceMode mode)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });
        if (mode == UI::UISurfaceMode::Overlay)
        {
            // 오버레이는 카메라를 건드리지 않으므로 지우기 색을 실어 줄 카메라가 따로 필요하다.
            // 프레임을 소유하는 모드는 자기가 카메라를 잠그니 여기서 싣지 않는다.
            Rendering::CameraRenderData camera;
            camera.view = Math::Matrix4x4::Identity();
            camera.projection = Math::Matrix4x4::Identity();
            builder.SetCamera(camera);
        }

        // 클립보드도 래스터라이저도 주지 않는다. 이 검사는 글자를 그리지 않으므로 없어도 되고,
        // 없는 편이 기계마다 다른 글꼴이 결과에 섞이지 않는다.
        UI::UIContext context{ nullptr, nullptr };
        context.SetSurfaceMode(mode);
        const Runtime::Input input;
        context.BeginFrame(input, { ImageWidth, ImageHeight });
        context.DrawImage(
            UI::UIRect{ ImageLeft, ImageTop, ImageExtent, ImageExtent },
            MakeHalvedTexture(0x5000000000000011ull, UpperColor, LowerColor));
        context.EndFrame(builder);

        return std::move(builder).Build();
    }

    [[nodiscard]] bool CheckMode(const GameEngine::UI::UISurfaceMode mode)
    {
        using namespace GameEngine;

        const std::vector<const Rendering::GraphicsBackendDescriptor*> supported =
            SupportedBackends();
        if (supported.empty())
        {
            std::cout << "  ui context image orientation tests skipped: no supported backend\n";
            return true;
        }

        bool passed = true;
        for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
        {
            const std::string prefix = std::string(backend->id) + " " + NameOf(mode) + ": ";
            const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
            if (!device || !device->Initialize(Platform::NativeSurface{}))
            {
                passed &= Expect(
                    false, (prefix + "a supported backend should initialize headless").c_str());
                continue;
            }

            Rendering::CapturedImage image;
            if (!Expect(
                    device->RenderToImage(BuildFrame(mode), image) && image.IsValid(),
                    (prefix + "UIContext should produce an image").c_str()))
            {
                passed = false;
                continue;
            }

            const Rgba upper = ReadPixel(image, SampleX, UpperSampleY);
            const Rgba lower = ReadPixel(image, SampleX, LowerSampleY);
            const bool rightWayUp = upper == UpperColor && lower == LowerColor;
            if (!rightWayUp)
            {
                std::cerr << "  " << prefix << "the image reads " << Describe(upper)
                          << " above and " << Describe(lower) << " below, against "
                          << Describe(UpperColor) << " and " << Describe(LowerColor);
                if (upper == LowerColor && lower == UpperColor)
                {
                    std::cerr << " and is therefore upside down";
                }
                std::cerr << "\n";
            }
            std::cout << "  ui image " << prefix << "above " << Describe(upper) << ", below "
                      << Describe(lower) << "\n";
            passed &= Expect(
                rightWayUp, "UIContext draws an image with its first row at the top");
        }
        return passed;
    }
}

bool RunUIContextImageOrientationTests()
{
    // 프레임을 소유하는 기본 모드와 오버레이 모드에서 이미지 방향을 각각 확인한다.
    bool passed = CheckMode(GameEngine::UI::UISurfaceMode::OwnFrame);
    passed &= CheckMode(GameEngine::UI::UISurfaceMode::Overlay);
    return passed;
}

static const TestSupport::Registration gUIContextImageOrientationTests{
    "UIContext", "ui context image orientation tests should pass",
    RunUIContextImageOrientationTests };
