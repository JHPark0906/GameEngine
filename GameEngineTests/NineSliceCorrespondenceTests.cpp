#include "NineSliceCorrespondenceTests.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Assets/TextureData.h"
#include "Math/Matrix.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "BackendPixelSupport.h"
#include "TestSupport.h"

using TestSupport::Describe;
using TestSupport::Expect;
using TestSupport::Quantize;
using TestSupport::ReadPixel;
using TestSupport::Rgba;
using TestSupport::SupportedBackends;

/// <summary>
/// 아홉 조각이 저마다 제자리에 가는지를 실제로 그려진 픽셀로 묻는다.
///
/// 이미 있는 nine-slice 픽셀 검사는 테두리 고리를 한 색으로 칠하고 귀퉁이 한 점과 가운데 한
/// 점만 읽는다. 그러면 귀퉁이 조각이 변 한가운데에 찍혀도 읽히는 색은 여전히 「테두리 색」이라
/// 통과한다. 화면 공간 패널 검사도 가로로는 변하지 않는 그림을 써서 세로 줄만 확인한다.
/// 그래서 여기서는 아홉 칸을 모두 다른 색으로 칠하고 출력의 아홉 자리에서 각각 그 칸의 색이
/// 나오는지를 묻는다 — 조각과 자리의 대응을 직접 묻는 방법이다.
/// </summary>
namespace
{
    /// <summary>그림의 크기다. button-32.png와 같다.</summary>
    constexpr unsigned int TextureExtent = 32;

    /// <summary>
    /// 네 변의 테두리다. 넷을 서로 다르게 둔 것이 요점이다.
    ///
    /// 테두리를 네 변 모두 같은 값으로 두면 순서가 뒤바뀐 코드도 통과한다. 값이 실려 가는 길이
    /// meta의 [left, top, right, bottom] → Assets::Sprite::Border → SpriteBorder → 조각 조립으로
    /// 네 번 옮겨 담기고, 그 사이 어디서 top과 right가 바뀌어도 8,8,8,8은 아무 말도 하지 않는다.
    /// </summary>
    constexpr float BorderLeft = 8.0f;
    constexpr float BorderTop = 4.0f;
    constexpr float BorderRight = 12.0f;
    constexpr float BorderBottom = 6.0f;

    /// <summary>
    /// 툴바 단추의 논리 크기다.
    /// </summary>
    constexpr float LogicalWidth = 165.0f;
    constexpr float LogicalHeight = 48.0f;

    /// <summary>그리는 면이다. 배율 2에서의 330x96과 여백이 들어갈 만큼 크다.</summary>
    constexpr unsigned int ImageWidth = 384;
    constexpr unsigned int ImageHeight = 128;

    /// <summary>사각형의 왼쪽 위 모서리다. 면의 가장자리와 겹치지 않게 띄운다.</summary>
    constexpr float Margin = 16.0f;
    /// <summary>배경색이다. 아홉 칸 어느 것과도 달라서, 조각이 아예 안 그려지면 드러난다.</summary>
    constexpr GameEngine::Math::Color ClearColor{
        64.0f / 255.0f, 96.0f / 255.0f, 32.0f / 255.0f, 1.0f };


    /// <summary>월드 공간에서 한 단위가 몇 픽셀인가. 카메라와 스프라이트가 함께 쓰는 값이다.</summary>
    constexpr float WorldPixelsPerUnit = 32.0f;

    /// <summary>
    /// 그린 사각형이 출력에서 차지하는 자리다. 픽셀이다.
    ///
    /// 화면 공간과 월드 공간이 <b>같은 픽셀</b>을 덮도록 맞춰 둔다. 그래야 같은 단언을 두 공간에
    /// 그대로 세울 수 있고, 한쪽만 틀렸을 때 그 차이가 곧 답이 된다.
    /// </summary>
    struct Placement
    {
        float left = 0.0f;
        float top = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    [[nodiscard]] Placement PlacementFor(const float scale)
    {
        return { Margin, Margin, LogicalWidth * scale, LogicalHeight * scale };
    }

    /// <summary>
    /// 카메라와 draw를 주어진 공간에 맞춰 세운다. 두 공간에서 결과가 같은 픽셀에 오도록 한다.
    /// </summary>
    void PlaceIntoSpace(
        GameEngine::Rendering::RenderFrameBuilder& builder,
        GameEngine::Rendering::SpriteDraw& draw,
        const GameEngine::Rendering::DrawSpace space,
        const float scale)
    {
        using namespace GameEngine;

        const Placement place = PlacementFor(scale);
        const float centreX = place.left + place.width * 0.5f;
        const float centreY = place.top + place.height * 0.5f;

        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        draw.space = space;
        if (space == Rendering::DrawSpace::Screen)
        {
            camera.projection = Math::Matrix4x4::Identity();
            draw.pixelsPerUnit = 1.0f;
            draw.size = { place.width, place.height };
            // 화면 공간에서 localToWorld는 사각형의 중심을 픽셀로 옮기기만 한다.
            draw.localToWorld = Math::Matrix4x4::CreateTranslation({ centreX, centreY, 0.0f });
        }
        else
        {
            camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(
                static_cast<float>(ImageWidth) / WorldPixelsPerUnit,
                static_cast<float>(ImageHeight) / WorldPixelsPerUnit,
                0.1f,
                100.0f);
            draw.pixelsPerUnit = WorldPixelsPerUnit;
            draw.size = { place.width / WorldPixelsPerUnit, place.height / WorldPixelsPerUnit };
            // 월드는 y가 위로 가고 원점이 화면 한가운데다. 같은 픽셀에 놓으려고 여기서 돌린다.
            draw.localToWorld = Math::Matrix4x4::CreateTranslation(
                { (centreX - static_cast<float>(ImageWidth) * 0.5f) / WorldPixelsPerUnit,
                  (static_cast<float>(ImageHeight) * 0.5f - centreY) / WorldPixelsPerUnit,
                  1.0f });
        }
        builder.SetCamera(camera);
    }

    [[nodiscard]] const char* NameOf(const GameEngine::Rendering::DrawSpace space)
    {
        return space == GameEngine::Rendering::DrawSpace::Screen ? "screen" : "world";
    }

    /// <summary>
    /// 아홉 칸의 색이다. 어느 둘도 한 채널조차 같지 않아서, 조각이 이웃 자리로 밀려가면 반드시
    /// 다른 색이 읽힌다. 배경색과도 모두 다르다.
    /// </summary>
    constexpr std::array<Rgba, 9> CellColors{
        Rgba{ 255, 0, 0, 255 },   Rgba{ 0, 255, 0, 255 },   Rgba{ 0, 0, 255, 255 },
        Rgba{ 255, 255, 0, 255 }, Rgba{ 255, 0, 255, 255 }, Rgba{ 0, 255, 255, 255 },
        Rgba{ 130, 0, 0, 255 },   Rgba{ 0, 130, 0, 255 },   Rgba{ 0, 0, 130, 255 } };

    constexpr std::array<const char*, 9> CellNames{
        "top-left", "top", "top-right", "left", "middle", "right",
        "bottom-left", "bottom", "bottom-right" };


    /// <summary>텍스처의 어느 칸에 속하는 좌표인지를 그 축의 두 테두리로 가른다.</summary>
    [[nodiscard]] std::size_t CellIndexAt(const unsigned int x, const unsigned int y)
    {
        const auto band =
            [](const unsigned int value, const float near, const float far) -> std::size_t
        {
            if (static_cast<float>(value) < near)
            {
                return 0;
            }
            return static_cast<float>(value) < static_cast<float>(TextureExtent) - far ? 1 : 2;
        };
        return band(y, BorderTop, BorderBottom) * 3 + band(x, BorderLeft, BorderRight);
    }

    /// <summary>아홉 칸이 모두 다른 색인 그림이다.</summary>
    [[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeNineCellTexture()
    {
        auto texture = std::make_shared<GameEngine::Assets::TextureData>();
        texture->id = 0x5000000000000009ull;
        texture->width = TextureExtent;
        texture->height = TextureExtent;
        texture->pixels.resize(texture->GetByteSize());
        for (unsigned int y = 0; y < TextureExtent; ++y)
        {
            for (unsigned int x = 0; x < TextureExtent; ++x)
            {
                const Rgba& color = CellColors[CellIndexAt(x, y)];
                const std::size_t offset = (static_cast<std::size_t>(y) * TextureExtent + x) * 4;
                texture->pixels[offset] = static_cast<std::byte>(color.r);
                texture->pixels[offset + 1] = static_cast<std::byte>(color.g);
                texture->pixels[offset + 2] = static_cast<std::byte>(color.b);
                texture->pixels[offset + 3] = static_cast<std::byte>(color.a);
            }
        }
        return texture;
    }

    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildFrame(
        const float scale, const GameEngine::Rendering::DrawSpace space)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::SpriteDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        draw.material = builder.AddMaterial({ MakeNineCellTexture() });
        draw.sliced = true;
        // 테두리는 그림이 선언한 텍스처 픽셀 그대로이고, 화면에서 얼마나 굵게 그릴지는 배수가
        // 따로 말한다 — SceneRenderPass가 화면 공간 스프라이트에 세우는 그대로다.
        draw.border = { BorderLeft, BorderTop, BorderRight, BorderBottom };
        draw.borderScale = scale;
        PlaceIntoSpace(builder, draw, space, scale);
        static_cast<void>(builder.TryAddDraw(
            space == Rendering::DrawSpace::Screen ? Rendering::RenderPass::Overlay
                                                  : Rendering::RenderPass::Transparent,
            draw, 0, 0));

        return std::move(builder).Build();
    }

    /// <summary>
    /// 출력에서 아홉 자리의 한가운데를 하나씩 읽고, 그 자리에 와야 할 칸의 색인지 본다.
    ///
    /// 읽는 자리는 언제나 띠의 한가운데라, 조각 경계에서 일어나는 필터링에 닿지 않는다.
    /// </summary>
    [[nodiscard]] bool CheckAtScale(const float scale, const GameEngine::Rendering::DrawSpace space)
    {
        using namespace GameEngine;

        const std::vector<const Rendering::GraphicsBackendDescriptor*> supported =
            SupportedBackends();
        if (supported.empty())
        {
            std::cout
                << "  nine-slice correspondence tests skipped: no supported graphics backend\n";
            return true;
        }

        const float width = LogicalWidth * scale;
        const float height = LogicalHeight * scale;
        // 출력에서 세 열과 세 행이 걸치는 자리다. 가운데를 읽으려고 경계를 먼저 적는다. 네 변의
        // 두께가 서로 다르므로 각 변은 자기 테두리로만 계산된다 — 한 값으로 넷을 다 쓰면 순서가
        // 뒤바뀐 코드도 이 시험을 통과한다.
        const std::array<float, 4> xBounds{
            Margin,
            Margin + BorderLeft * scale,
            Margin + width - BorderRight * scale,
            Margin + width };
        const std::array<float, 4> yBounds{
            Margin,
            Margin + BorderTop * scale,
            Margin + height - BorderBottom * scale,
            Margin + height };

        bool passed = true;
        for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
        {
            const std::string prefix = std::string(backend->id) + " " + NameOf(space) + " at scale " +
                std::to_string(static_cast<int>(scale)) + ": ";
            const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
            if (!device || !device->Initialize(Platform::NativeSurface{}))
            {
                passed &= Expect(
                    false, (prefix + "a supported backend should initialize headless").c_str());
                continue;
            }

            Rendering::CapturedImage image;
            if (!Expect(
                    device->RenderToImage(BuildFrame(scale, space), image) && image.IsValid(),
                    (prefix + "a nine-sliced sprite should produce an image").c_str()))
            {
                passed = false;
                continue;
            }

            // 통과할 때도 아홉 자리에서 무엇을 읽었는지 적는다. 「초록이었다」만으로는 이 검사가
            // 정말 아홉 자리를 본 것인지, 아무것도 못 보고 지나간 것인지 나중에 가릴 수 없다.
            std::cout << "  nine-slice " << prefix << "places read:";
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t column = 0; column < 3; ++column)
                {
                    const auto x =
                        static_cast<unsigned int>((xBounds[column] + xBounds[column + 1]) * 0.5f);
                    const auto y =
                        static_cast<unsigned int>((yBounds[row] + yBounds[row + 1]) * 0.5f);
                    const std::size_t index = row * 3 + column;
                    const Rgba actual = ReadPixel(image, x, y);
                    const bool correct = actual == CellColors[index];
                    std::cout << " " << CellNames[index] << "(" << x << "," << y << ")=";
                    if (correct)
                    {
                        std::cout << "itself";
                    }
                    else
                    {
                        std::cout << Describe(actual);
                    }
                    if (!correct)
                    {
                        std::cerr << "  " << prefix << "the " << CellNames[index] << " place ("
                                  << x << "," << y << ") shows " << Describe(actual)
                                  << " but the " << CellNames[index] << " piece is "
                                  << Describe(CellColors[index]);
                        for (std::size_t other = 0; other < CellColors.size(); ++other)
                        {
                            if (actual == CellColors[other])
                            {
                                std::cerr << " — that is the " << CellNames[other] << " piece";
                            }
                        }
                        std::cerr << "\n";
                    }
                    passed &= Expect(
                        correct, "each of the nine places shows the piece that belongs there");
                }
            }
            std::cout << "\n";
        }
    return passed;
    }

    /// <summary>
    /// 둥근 모서리의 반지름을 테두리 폭과 같게 두어 방향 오류가 충분한 픽셀에서 드러나게 한다.
    /// </summary>
    constexpr float RoundedBorder = 8.0f;
    constexpr float CornerRadius = RoundedBorder;
    constexpr Rgba RoundedFill{ 255, 255, 255, 255 };

    [[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeRoundedTexture()
    {
        auto texture = std::make_shared<GameEngine::Assets::TextureData>();
        texture->id = 0x500000000000000Aull;
        texture->width = TextureExtent;
        texture->height = TextureExtent;
        texture->pixels.resize(texture->GetByteSize());
        const auto extent = static_cast<float>(TextureExtent);
        for (unsigned int y = 0; y < TextureExtent; ++y)
        {
            for (unsigned int x = 0; x < TextureExtent; ++x)
            {
                // 귀퉁이 원의 중심에서 잰 거리로 자른다. 부분 투명을 만들지 않아, 비어 있는
                // 자리는 배경색이 그대로 읽힌다.
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float cx = px < CornerRadius ? CornerRadius : extent - CornerRadius;
                const float cy = py < CornerRadius ? CornerRadius : extent - CornerRadius;
                const bool inCornerSquare = (px < CornerRadius || px > extent - CornerRadius) &&
                    (py < CornerRadius || py > extent - CornerRadius);
                const float dx = px - cx;
                const float dy = py - cy;
                const bool carved =
                    inCornerSquare && dx * dx + dy * dy > CornerRadius * CornerRadius;
                const std::size_t offset = (static_cast<std::size_t>(y) * TextureExtent + x) * 4;
                // 파인 모서리를 불투명한 배경색으로 칠해 패스별 알파 블렌딩이 위치 검사에 영향을 주지 않게 한다.
                const Rgba color = carved ? Quantize(ClearColor) : RoundedFill;
                texture->pixels[offset] = static_cast<std::byte>(color.r);
                texture->pixels[offset + 1] = static_cast<std::byte>(color.g);
                texture->pixels[offset + 2] = static_cast<std::byte>(color.b);
                texture->pixels[offset + 3] = static_cast<std::byte>(color.a);
            }
        }
        return texture;
    }

    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildRoundedFrame(
        const float scale, const GameEngine::Rendering::DrawSpace space)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::SpriteDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        draw.material = builder.AddMaterial({ MakeRoundedTexture() });
        draw.sliced = true;
        draw.border = { RoundedBorder, RoundedBorder, RoundedBorder, RoundedBorder };
        draw.borderScale = scale;
        PlaceIntoSpace(builder, draw, space, scale);
        static_cast<void>(builder.TryAddDraw(
            space == Rendering::DrawSpace::Screen ? Rendering::RenderPass::Overlay
                                                  : Rendering::RenderPass::Transparent,
            draw, 0, 0));

        return std::move(builder).Build();
    }

    /// <summary>
    /// 그려진 사각형 내부를 전수 검사해 배경색 픽셀이 네 귀퉁이 영역에만 있는지 확인한다.
    /// </summary>
    [[nodiscard]] bool CheckRoundingStaysInCornersAtScale(
        const float scale, const GameEngine::Rendering::DrawSpace space)
    {
        using namespace GameEngine;

        const std::vector<const Rendering::GraphicsBackendDescriptor*> supported =
            SupportedBackends();
        if (supported.empty())
        {
            return true;
        }

        const Rgba background = Quantize(ClearColor);
        const float inset = RoundedBorder * scale;
        const float width = LogicalWidth * scale;
        const float height = LogicalHeight * scale;

        bool passed = true;
        for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
        {
            const std::string prefix = std::string(backend->id) + " " + NameOf(space) + " at scale " +
                std::to_string(static_cast<int>(scale)) + ": ";
            const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
            if (!device || !device->Initialize(Platform::NativeSurface{}))
            {
                continue;
            }
            Rendering::CapturedImage image;
            if (!Expect(
                    device->RenderToImage(BuildRoundedFrame(scale, space), image) && image.IsValid(),
                    (prefix + "a rounded nine-sliced sprite should produce an image").c_str()))
            {
                passed = false;
                continue;
            }

            // 바깥 한 픽셀은 건너뛴다. 사각형의 가장자리에서는 필터링이 배경과 섞여, 여기서 묻는
            // 것과 상관없는 값이 나온다.
            const auto left = static_cast<unsigned int>(Margin) + 1;
            const auto top = static_cast<unsigned int>(Margin) + 1;
            const auto right = static_cast<unsigned int>(Margin + width) - 1;
            const auto bottom = static_cast<unsigned int>(Margin + height) - 1;
            const auto cornerRight = static_cast<unsigned int>(Margin + width - inset);
            const auto cornerBottom = static_cast<unsigned int>(Margin + height - inset);
            const auto cornerLeft = static_cast<unsigned int>(Margin + inset);
            const auto cornerTop = static_cast<unsigned int>(Margin + inset);

            unsigned int strays = 0;
            unsigned int firstX = 0;
            unsigned int firstY = 0;
            unsigned int carvedInCorners = 0;
            for (unsigned int y = top; y < bottom; ++y)
            {
                for (unsigned int x = left; x < right; ++x)
                {
                    if (!(ReadPixel(image, x, y) == background))
                    {
                        continue;
                    }
                    const bool inCorner = (x < cornerLeft || x >= cornerRight) &&
                        (y < cornerTop || y >= cornerBottom);
                    if (inCorner)
                    {
                        ++carvedInCorners;
                        continue;
                    }
                    if (strays == 0)
                    {
                        firstX = x;
                        firstY = y;
                    }
                    ++strays;
                }
            }

            if (strays > 0)
            {
                std::cerr << "  " << prefix << strays
                          << " background pixels lie outside the four corners, the first at ("
                          << firstX << "," << firstY
                          << ") — a rounded corner is being drawn along an edge\n";
            }
            passed &= Expect(strays == 0, "the rounding shows only in the four corners");
            // 귀퉁이에 아무것도 파이지 않았다면 이 검사는 아무것도 지키지 못한 것이다.
            passed &= Expect(
                carvedInCorners > 0, "and the corners really are rounded, so the check has teeth");

            // 맨 위 행이 가운데 행보다 좁아야 둥근 모서리의 수직 방향이 올바르다.
            const auto drawnWidthOfRow = [&](const unsigned int y)
            {
                unsigned int drawn = 0;
                for (unsigned int x = left; x < right; ++x)
                {
                    if (!(ReadPixel(image, x, y) == background))
                    {
                        ++drawn;
                    }
                }
                return drawn;
            };
            std::cout << "  rounding " << prefix << "outermost row "
                      << drawnWidthOfRow(top) << "px, middle " << drawnWidthOfRow((top + bottom) / 2)
                      << "px, last " << drawnWidthOfRow(bottom - 1) << "px; carved in corners "
                      << carvedInCorners << ", strays " << strays << "; rect " << left << ","
                      << top << ".." << right << "," << bottom << "; a corner pixel reads "
                      << Describe(ReadPixel(image, left, top)) << " and background is "
                      << Describe(background) << "\n";
            const unsigned int topRow = drawnWidthOfRow(top);
            const unsigned int middleRow = drawnWidthOfRow((top + bottom) / 2);
            const unsigned int bottomRow = drawnWidthOfRow(bottom - 1);
            if (topRow >= middleRow || bottomRow >= middleRow)
            {
                std::cerr << "  " << prefix << "the rectangle's top row is " << topRow
                          << "px wide and its bottom row " << bottomRow
                          << "px, against " << middleRow
                          << "px across the middle — a rounded rectangle's outermost rows must be "
                          << "the narrowest, so the corner pieces are facing inwards\n";
            }
            passed &= Expect(
                topRow < middleRow && bottomRow < middleRow,
                "the outermost rows are the narrowest, so the rounding faces outwards");
        }
    return passed;
    }

    /// <summary>같은 그림을 조각내지 않고 통째로 늘려 그리는 프레임이다.</summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildWholeSpriteFrame(
        const float scale, const GameEngine::Rendering::DrawSpace space)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::SpriteDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        draw.material = builder.AddMaterial({ MakeNineCellTexture() });
        PlaceIntoSpace(builder, draw, space, scale);

        // 조각내지 않은 스프라이트의 크기는 draw.size가 아니라 <b>텍스처</b>에서 나오므로, 늘리는
        // 일은 변환이 해야 한다. 그리기가 단위 quad에 실어 주는 크기가 공간마다 다르다: 화면은
        // 텍스처 픽셀 그대로이고, 월드는 그것을 pixelsPerUnit으로 나눈 월드 단위다.
        const Placement place = PlacementFor(scale);
        const bool screenSpace = space == Rendering::DrawSpace::Screen;
        const auto texture = static_cast<float>(TextureExtent);
        const float unitWidth = screenSpace ? texture : texture / WorldPixelsPerUnit;
        const float unitHeight = unitWidth;
        const float wantWidth = screenSpace ? place.width : place.width / WorldPixelsPerUnit;
        const float wantHeight = screenSpace ? place.height : place.height / WorldPixelsPerUnit;
        draw.localToWorld =
            Math::Matrix4x4::CreateScale(
                { wantWidth / unitWidth, wantHeight / unitHeight, 1.0f }) *
            draw.localToWorld;
        static_cast<void>(builder.TryAddDraw(
            screenSpace ? Rendering::RenderPass::Overlay : Rendering::RenderPass::Transparent,
            draw, 0, 0));

        return std::move(builder).Build();
    }

    /// <summary>
    /// 9-슬라이스 색 띠와 별도로 비대칭 일반 이미지를 검사해 수직 UV 뒤집힘을 구분한다.
    /// </summary>
    [[nodiscard]] bool CheckWholeSpriteIsNotUpsideDownAtScale(
        const float scale, const GameEngine::Rendering::DrawSpace space)
    {
        using namespace GameEngine;

        const std::vector<const Rendering::GraphicsBackendDescriptor*> supported =
            SupportedBackends();
        if (supported.empty())
        {
            return true;
        }

        const float width = LogicalWidth * scale;
        const float height = LogicalHeight * scale;
        // 조각내지 않으면 그림이 통째로 늘어나므로, 아홉 칸은 텍스처에서 차지하던 비율 그대로
        // 출력에 걸린다.
        const auto extent = static_cast<float>(TextureExtent);
        const std::array<float, 4> uFractions{
            0.0f, BorderLeft / extent, 1.0f - BorderRight / extent, 1.0f };
        const std::array<float, 4> vFractions{
            0.0f, BorderTop / extent, 1.0f - BorderBottom / extent, 1.0f };

        bool passed = true;
        for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
        {
            const std::string prefix = std::string(backend->id) + " " + NameOf(space) + " at scale " +
                std::to_string(static_cast<int>(scale)) + ": ";
            const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
            if (!device || !device->Initialize(Platform::NativeSurface{}))
            {
                continue;
            }
            Rendering::CapturedImage image;
            if (!Expect(
                    device->RenderToImage(BuildWholeSpriteFrame(scale, space), image) && image.IsValid(),
                    (prefix + "a whole sprite should produce an image").c_str()))
            {
                passed = false;
                continue;
            }

            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t column = 0; column < 3; ++column)
                {
                    const auto x = static_cast<unsigned int>(
                        Margin + width * (uFractions[column] + uFractions[column + 1]) * 0.5f);
                    const auto y = static_cast<unsigned int>(
                        Margin + height * (vFractions[row] + vFractions[row + 1]) * 0.5f);
                    const std::size_t index = row * 3 + column;
                    const Rgba actual = ReadPixel(image, x, y);
                    const bool correct = actual == CellColors[index];
                    if (!correct)
                    {
                        std::cerr << "  " << prefix << "stretched whole, the " << CellNames[index]
                                  << " part of the image is at (" << x << "," << y << ") showing "
                                  << Describe(actual) << " instead of "
                                  << Describe(CellColors[index]);
                        for (std::size_t other = 0; other < CellColors.size(); ++other)
                        {
                            if (actual == CellColors[other])
                            {
                                std::cerr << " — that is the " << CellNames[other] << " part";
                            }
                        }
                        std::cerr << "\n";
                    }
                    passed &= Expect(
                        correct, "a whole sprite is drawn the way up it is stored");
                }
            }
        }
    return passed;
    }

    /// <summary>
    /// 양쪽 테두리 합보다 얕은 사각형에서도 9-슬라이스 조각이 제자리에 있어야 한다.
    /// 인셋 합이 대상을 넘으면 비율대로 줄이고 높이가 0인 가운데 칸은 그리지 않아야 한다.
    /// </summary>
    [[nodiscard]] bool CheckShallowRectangleKeepsItsPieces(const float scale)
    {
        using namespace GameEngine;

        const std::vector<const Rendering::GraphicsBackendDescriptor*> supported =
            SupportedBackends();
        if (supported.empty())
        {
            return true;
        }

        // 위아래 테두리를 합치면 이 높이를 넘는다. 배율 2에서 8+6이 두 배가 되어 28인데 사각형은
        // 20밖에 안 된다 — 줄이는 산술이 도는 자리가 정확히 여기다.
        constexpr float ShallowHeight = 10.0f;

        bool passed = true;
        for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
        {
            const std::string prefix =
                std::string(backend->id) + " at scale " + std::to_string(static_cast<int>(scale)) + ": ";
            const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
            if (!device || !device->Initialize(Platform::NativeSurface{}))
            {
                continue;
            }

            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ ImageWidth, ImageHeight });
            Rendering::SpriteDraw draw;
            draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
            draw.material = builder.AddMaterial({ MakeNineCellTexture() });
            draw.sliced = true;
            draw.border = { BorderLeft, BorderTop, BorderRight, BorderBottom };
            draw.borderScale = scale;
            PlaceIntoSpace(builder, draw, Rendering::DrawSpace::Screen, scale);
            draw.size = { LogicalWidth * scale, ShallowHeight * scale };
            draw.localToWorld = Math::Matrix4x4::CreateTranslation(
                { Margin + LogicalWidth * scale * 0.5f,
                  Margin + ShallowHeight * scale * 0.5f,
                  0.0f });
            static_cast<void>(
                builder.TryAddDraw(Rendering::RenderPass::Overlay, draw, 0, 0));

            Rendering::CapturedImage image;
            if (!Expect(
                    device->RenderToImage(std::move(builder).Build(), image) && image.IsValid(),
                    (prefix + "a rectangle shallower than its borders should produce an image")
                        .c_str()))
            {
                passed = false;
                continue;
            }

            // 세로가 눌렸어도 가로는 멀쩡하므로, 왼쪽·가운데·오른쪽이 여전히 자기 열의 색이어야
            // 한다. 귀퉁이가 서로를 넘어 변 한가운데로 갔다면 가운데 열에서 왼쪽 열의 색이 읽힌다.
            const float width = LogicalWidth * scale;
            const auto y = static_cast<unsigned int>(Margin + ShallowHeight * scale * 0.5f);
            const std::array<unsigned int, 3> columns{
                static_cast<unsigned int>(Margin + BorderLeft * scale * 0.5f),
                static_cast<unsigned int>(Margin + width * 0.5f),
                static_cast<unsigned int>(Margin + width - BorderRight * scale * 0.5f) };
            for (std::size_t column = 0; column < columns.size(); ++column)
            {
                // 가운데 행이 사라졌을 수 있으므로 위 행과 아래 행 중 어느 쪽이 와도 받아들이되,
                // <b>열</b>은 반드시 자기 것이어야 한다.
                const Rgba actual = ReadPixel(image, columns[column], y);
                const bool ownColumn = actual == CellColors[column] ||
                    actual == CellColors[3 + column] || actual == CellColors[6 + column];
                if (!ownColumn)
                {
                    std::cerr << "  " << prefix << "shallow rectangle: column " << column
                              << " at (" << columns[column] << "," << y << ") reads "
                              << Describe(actual) << ", which belongs to another column\n";
                }
                passed &= Expect(
                    ownColumn,
                    "a rectangle shallower than its borders keeps each piece in its own column");
            }
        }
    return passed;
    }
}

bool RunNineSliceCorrespondenceTests()
{
    // 배율 1과 2에서 월드 공간과 화면 공간을 모두 확인한다. 배율 1에서는 테두리와 배수가
    // 같은 수라, 잘못된 단위나 배율 적용이 검사에 드러나지 않을 수 있다.
    bool passed = true;
    for (const GameEngine::Rendering::DrawSpace space :
         { GameEngine::Rendering::DrawSpace::World, GameEngine::Rendering::DrawSpace::Screen })
    {
        passed &= TestSupport::ForEachUiScale(
            [space](const float scale) { return CheckAtScale(scale, space); });
        passed &= TestSupport::ForEachUiScale(
            [space](const float scale)
            { return CheckRoundingStaysInCornersAtScale(scale, space); });
        passed &= TestSupport::ForEachUiScale(
            [space](const float scale)
            { return CheckWholeSpriteIsNotUpsideDownAtScale(scale, space); });
    }
    passed &= TestSupport::ForEachUiScale(CheckShallowRectangleKeepsItsPieces);
    return passed;
}

static const TestSupport::Registration gNineSliceCorrespondenceTests{
    "RenderFrame", "nine-slice correspondence tests should pass", RunNineSliceCorrespondenceTests };
