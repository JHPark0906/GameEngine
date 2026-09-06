#include "BackendImageTests.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

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
using TestSupport::MakeHalvedTexture;
using TestSupport::Quantize;
using TestSupport::ReadPixel;
using TestSupport::Rgba;
using TestSupport::SupportedBackends;

/// <summary>
/// 백엔드가 오프스크린 타깃에 그린 실제 픽셀을 비교한다.
/// 창의 전경 여부나 데스크톱 합성 결과와 독립적으로 렌더링 결과를 검증한다.
/// 백엔드 레지스트리를 순회하므로 모든 등록된 백엔드에 같은 결과를 요구한다.
/// </summary>
namespace
{

    constexpr unsigned int ImageWidth = 128;
    constexpr unsigned int ImageHeight = 128;

    /// <summary>
    /// 타깃을 지우는 색이다.
    ///
    /// 어느 채널도 서로 같지 않아, 채널 순서가 뒤바뀌면 대칭으로 통과하는 대신 실패한다. 그리고
    /// 각 채널이 온전한 8비트 값 나누기 255라서, 비교는 두 값 사이에 떨어진 수를 하드웨어가
    /// 어떻게 반올림할지에 대한 추측이 아니라 프레임이 요구한 값에 대해 이뤄진다.
    /// </summary>
    constexpr GameEngine::Math::Color ClearColor{ 64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f };

    /// <summary>스프라이트 자신의 색이다. 모든 채널에서 지우기 색과 다르다.</summary>
    constexpr Rgba SpriteColor{ 220, 40, 120, 255 };

    /// <summary>메시의 기본 색이다. 이미지에 무엇이 닿는지는 조명이 정하므로 이 값을 직접
    /// 단언하는 것은 없다. 프레임의 다른 무엇과도 다르기만 하면 된다.</summary>
    constexpr Rgba MeshTextureColor{ 30, 200, 90, 255 };

    /// <summary>텍스트 커버리지에 입히는 틴트 색이다.</summary>
    constexpr GameEngine::Math::Color TextTint{ 1.0f, 1.0f, 0.0f, 1.0f };

    /// <summary>nine-slice 테스트 텍스처이다: 이 색의 테두리 고리가 다른 색의 중앙을 두른다.</summary>
    /// <summary>타일맵 칸의 색이다. 다른 표본과 섞이지 않는 값이어야 한다.</summary>
    /// <summary>오버레이 패널의 색이다. 장면의 어느 표본과도 겹치지 않는 값이어야 한다.</summary>
    constexpr Rgba OverlayPanelColor{ 255, 140, 0, 255 };
    /// <summary>오버레이 패널의 아래 절반 색이다. 위와 달라야 방향을 물을 수 있다.</summary>
    constexpr Rgba OverlayPanelLowerColor{ 0, 90, 190, 255 };
    /// <summary>오버레이 패널 안쪽의 표본 자리다. 화면 좌표 (88,8)~(120,32) 사각형 안이다.</summary>
    constexpr unsigned int OverlaySampleX = 104;
    constexpr unsigned int OverlayUpperSampleY = 13;
    constexpr unsigned int OverlayLowerSampleY = 27;
    constexpr Rgba TilemapColor{ 120, 240, 255, 255 };
    constexpr Rgba SlicedBorderColor{ 10, 60, 200, 255 };
    constexpr Rgba SlicedCenterColor{ 240, 200, 30, 255 };

    /// <summary>월드 단위 깊이이다. 스프라이트가 더 가까워, 둘이 겹치는 곳에서는 스프라이트가 이긴다.</summary>
    constexpr float SpriteDepth = 1.0f;
    constexpr float MeshDepth = 2.0f;

    /// <summary>
    /// 정확히 하나의 draw 안에 떨어지는 점들이다. 그래서 각 파이프라인을 따로 읽을 수 있다.
    ///
    /// 카메라가 128픽셀에 월드 4단위를 보여주므로 원점은 (64, 64)에 놓이고 월드 1단위는
    /// 32픽셀이다. 스크린 공간 텍스트는 픽셀로 직접 배치된다.
    /// </summary>
    constexpr unsigned int SpriteSampleX = 64;
    constexpr unsigned int SpriteSampleY = 64;
    constexpr unsigned int MeshSampleX = 24;
    constexpr unsigned int MeshSampleY = 64;

    /// sliced 스프라이트는 월드 (1, -1) — 화면 중심 (96, 96) — 에 1.5단위, 48픽셀로 늘어나
    /// 있고, 2텍셀 테두리는 슬라이스되면 16픽셀이다. 따라서 테두리 고리는 모서리에서 72..88
    /// 픽셀을 덮고 중앙은 88에서 시작한다.
    /// <summary>타일맵 격자 안쪽의 표본 자리다. 월드 (-1.5, -1.5) 근처가 여기다.</summary>
    constexpr unsigned int TilemapSampleX = 16;
    constexpr unsigned int TilemapSampleY = 112;
    constexpr unsigned int SlicedCornerSampleX = 78;
    constexpr unsigned int SlicedCornerSampleY = 78;
    constexpr unsigned int SlicedCenterSampleX = 96;
    constexpr unsigned int SlicedCenterSampleY = 96;

    /// <summary>
    /// 화면 공간 nine-slice 패널의 띠 색들이다. 위와 아래가 다른 색인 것이 요점이다: 조각이
    /// 세로로 거꾸로 조립되면 두 표본이 서로 바뀌므로, 대칭인 테두리로는 잡히지 않는 오류가
    /// 여기서 잡힌다.
    /// </summary>
    constexpr Rgba PanelTopColor{ 20, 200, 120, 255 };
    constexpr Rgba PanelBottomColor{ 200, 20, 120, 255 };
    constexpr Rgba PanelCenterColor{ 250, 250, 60, 255 };
    /// <summary>패널은 화면 좌표 (40,8)~(80,40)에 놓인다. 위 띠는 2px, 아래 띠는 4px이다.</summary>
    constexpr unsigned int PanelSampleX = 60;
    constexpr unsigned int PanelTopSampleY = 9;
    constexpr unsigned int PanelCenterSampleY = 24;
    constexpr unsigned int PanelBottomSampleY = 37;
    /// 텍스트 블록은 (8, 8) 중심의 16x8이라 화면 (0,4)-(16,12)를 덮는다. 페이지의 위 절반만
    /// 잉크이므로, 위 샘플은 틴트고 아래 샘플은 지우기 색이다 — 글리프가 상하로 뒤집히면 두
    /// 단언이 맞바뀌며 실패한다.
    constexpr unsigned int TextSampleX = 12;
    constexpr unsigned int TextSampleY = 5;
    constexpr unsigned int TextLowerSampleX = 12;
    constexpr unsigned int TextLowerSampleY = 10;
    /// <summary>
    /// 자라는 텍스트 페이지 시험이 쓰는 표본 자리다. 그 시험은 자기 프레임을 새로 짓고 그
    /// 프레임에는 이 draw 하나뿐이므로, 위 텍스트 블록과 자리가 겹쳐도 서로 간섭하지 않는다.
    /// </summary>
    constexpr unsigned int TextPageSampleX = 16;
    constexpr unsigned int TextPageSampleY = 16;

    /// <summary>
    /// 바이트로 나타낸 지우기 색이다. 채널이 온전한 8비트 값이라, 이것은 하드웨어가 쓰는 값이지
    /// 반올림 방식에 대한 추측이 아니다.
    /// </summary>
    [[nodiscard]] Rgba ExpectedClearColor() { return Quantize(ClearColor); }

    /// <summary>바이트로 나타낸 텍스트 틴트이다: 완전히 덮인 텍스트는 정확히 자기 틴트로 블렌딩된다.</summary>
    [[nodiscard]] Rgba ExpectedTextColor() { return Quantize(TextTint); }


    /// <summary>한 색으로만 된 텍스처이다. 그래서 필터링이 내부에서 두 백엔드를 다르게 만들 수 없다.</summary>
    [[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeSolidTexture(
        const std::uint64_t id, const Rgba& color)
    {
        auto texture = std::make_shared<GameEngine::Assets::TextureData>();
        texture->id = id;
        texture->width = 8;
        texture->height = 8;
        texture->pixels.resize(texture->GetByteSize());
        for (std::size_t pixel = 0; pixel < texture->pixels.size(); pixel += 4)
        {
            texture->pixels[pixel] = static_cast<std::byte>(color.r);
            texture->pixels[pixel + 1] = static_cast<std::byte>(color.g);
            texture->pixels[pixel + 2] = static_cast<std::byte>(color.b);
            texture->pixels[pixel + 3] = static_cast<std::byte>(color.a);
        }
        return texture;
    }

    /// <summary>
    /// 엔진이 가진 모든 종류의 draw를 하나씩 담은 프레임이다.
    ///
    /// 셋 다인 이유는, 백엔드가 파이프라인 하나는 맞고 다른 하나는 틀릴 수 있기 때문이고, draw
    /// 들이 깊이에서 겹치기 때문이다: 스프라이트가 메시 앞에 앉아 있어서, 이미지는 두 백엔드가
    /// 패스를 같은 순서로 같은 블렌딩으로 처리한다는 사실도 기록한다.
    ///
    /// 형상은 아래의 각 샘플 점이 정확히 하나의 draw 안에 떨어지도록 배치된다.
    /// </summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildTestFrame(const bool includeDraws)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        // Four world units across, so an eight-texel sprite at four pixels per unit covers the
        // middle quarter of the image and both the centre and the corners are decided.
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        if (!includeDraws)
        {
            return std::move(builder).Build();
        }

        // A quad to the left, behind the sprite, wound clockwise on screen so the back-face culling
        // the mesh pipeline uses keeps it.
        auto mesh = std::make_shared<Assets::MeshData>();
        mesh->id = 0x1000'0000'0000'0001ull;
        const auto corner = [](const float x, const float y)
        {
            Core::MeshVertex vertex;
            vertex.position = { x, y, MeshDepth };
            vertex.normal = { 0.0f, 0.0f, -1.0f };
            vertex.textureCoordinate = { x, y };
            return vertex;
        };
        mesh->vertices = { corner(-1.5f, -0.5f), corner(-1.5f, 0.5f), corner(-0.5f, 0.5f), corner(-0.5f, -0.5f) };
        mesh->indices = { 0, 1, 2, 0, 2, 3 };

        Rendering::MeshDraw meshDraw;
        meshDraw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Mesh });
        meshDraw.geometry = builder.AddGeometry({ mesh });
        meshDraw.material = builder.AddMaterial({ MakeSolidTexture(0x5000'0000'0000'0002ull, MeshTextureColor) });
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Opaque, meshDraw));
        // 프레임은 빛을 지어내지 않으므로 메시가 보이려면 프레임이 빛을 실어야 한다. 주변광 하나와
        // 카메라를 향해 비추는 방향광 하나: 두 백엔드가 같은 조명 산술을 하는지도 여기서 비교된다.
        builder.AddAmbientLight({ 0.2f, 0.2f, 0.2f, 1.0f });
        Rendering::LightRenderData sun;
        sun.kind = Rendering::LightKind::Directional;
        sun.direction = { 0.0f, 0.0f, 1.0f };
        sun.color = { 0.8f, 0.8f, 0.8f, 1.0f };
        static_cast<void>(builder.AddLight(sun));

        Rendering::SpriteDraw sprite;
        sprite.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        sprite.material = builder.AddMaterial({ MakeSolidTexture(0x5000'0000'0000'0001ull, SpriteColor) });
        sprite.localToWorld = Math::Matrix4x4::CreateTranslation({ 0.0f, 0.0f, SpriteDepth });
        sprite.pixelsPerUnit = 4.0f;
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, sprite, 0, 1));

        // Coverage the frontend would have shaped, made here rather than through the platform's
        // font stack: what is being compared is what the backends do with the pixels, and a font
        // that differs between machines would compare something else. One full-coverage glyph
        // covering its whole block stands in for an atlas page and its placement.
        auto textPage = std::make_shared<Rendering::RasterizedTextImage>();
        textPage->id = 0x3000'0000'0000'0001ull;
        textPage->width = 16;
        textPage->height = 8;
        // 위 절반만 잉크다: 균일한 커버리지는 상하 반전을 숨기므로, 방향까지 비교에 넣는다.
        textPage->alphaPixels.assign(
            static_cast<std::size_t>(textPage->width) * textPage->height, std::byte{ 0 });
        for (std::size_t pixel = 0;
             pixel < static_cast<std::size_t>(textPage->width) * (textPage->height / 2); ++pixel)
        {
            textPage->alphaPixels[pixel] = std::byte{ 255 };
        }
        auto textGlyphs = std::make_shared<std::vector<Rendering::TextGlyphQuad>>();
        Rendering::TextGlyphQuad textGlyph;
        textGlyph.width = 16.0f;
        textGlyph.height = 8.0f;
        textGlyph.uWidth = 1.0f;
        textGlyph.vHeight = 1.0f;
        textGlyphs->push_back(textGlyph);

        Rendering::TextDraw text;
        text.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Text });
        text.page = textPage;
        text.glyphs = textGlyphs;
        text.space = Rendering::TextSpace::Screen;
        text.tint = TextTint;
        text.localToWorld = Math::Matrix4x4::CreateTranslation({ 8.0f, 8.0f, 0.0f });
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, text, 0, 2));

        // A nine-sliced sprite, so the comparison also holds the multi-quad path to the same image.
        // The texture is a border ring around a distinct centre: if slicing samples the wrong
        // texels, the wrong colour appears where the assertions look.
        auto slicedTexture = std::make_shared<Assets::TextureData>();
        slicedTexture->id = 0x5000'0000'0000'0003ull;
        slicedTexture->width = 8;
        slicedTexture->height = 8;
        slicedTexture->pixels.resize(slicedTexture->GetByteSize());
        for (unsigned int y = 0; y < slicedTexture->height; ++y)
        {
            for (unsigned int x = 0; x < slicedTexture->width; ++x)
            {
                const bool isBorder = x < 2 || y < 2 ||
                    x >= slicedTexture->width - 2 || y >= slicedTexture->height - 2;
                const Rgba& color = isBorder ? SlicedBorderColor : SlicedCenterColor;
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * slicedTexture->width + x) * 4;
                slicedTexture->pixels[offset] = static_cast<std::byte>(color.r);
                slicedTexture->pixels[offset + 1] = static_cast<std::byte>(color.g);
                slicedTexture->pixels[offset + 2] = static_cast<std::byte>(color.b);
                slicedTexture->pixels[offset + 3] = static_cast<std::byte>(color.a);
            }
        }


        // 백엔드별 상한이 다르면 한쪽만 잘렸을 크기의 타일맵 레이어다. 70x70 = 4900칸은 D3D12의
        // 프레임 상수 링이 감당하는 4096을 넘고, 그런 상한이 백엔드마다 다르면 같은 프레임이 두
        // 백엔드에서 다른 픽셀이 된다. 이 비교가 통과한다는 것은 두 백엔드가 같은 지점까지
        // 그린다는 뜻이다.
        constexpr int TilemapSide = 70;
        auto tilemapTiles = std::make_shared<std::vector<Rendering::TilemapTile>>();
        tilemapTiles->reserve(static_cast<std::size_t>(TilemapSide) * TilemapSide);
        for (int row = 0; row < TilemapSide; ++row)
        {
            for (int column = 0; column < TilemapSide; ++column)
            {
                Rendering::TilemapTile tile;
                tile.column = column;
                tile.row = row;
                tile.uv = { 0.0f, 0.0f, 1.0f, 1.0f };
                tilemapTiles->push_back(tile);
            }
        }

        Rendering::TilemapDraw tilemap;
        tilemap.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        tilemap.material =
            builder.AddMaterial({ MakeSolidTexture(0x5000'0000'0000'0004ull, TilemapColor) });
        // 화면 왼쪽 아래 구석에 작게 깐다: 기존 표본 자리를 덮지 않으면서도 모든 칸이 실제로
        // 제출된다.
        tilemap.localToWorld = Math::Matrix4x4::CreateTranslation({ -2.0f, -2.0f, SpriteDepth });
        tilemap.cellSize = { 0.02f, 0.02f };
        tilemap.tiles = tilemapTiles;
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, tilemap, 0, 4));

        Rendering::SpriteDraw slicedSprite;
        slicedSprite.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        slicedSprite.material = builder.AddMaterial({ slicedTexture });
        slicedSprite.localToWorld = Math::Matrix4x4::CreateTranslation({ 1.0f, -1.0f, SpriteDepth });
        slicedSprite.pixelsPerUnit = 4.0f;
        slicedSprite.sliced = true;
        slicedSprite.border = { 2.0f, 2.0f, 2.0f, 2.0f };
        slicedSprite.size = { 1.5f, 1.5f };
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, slicedSprite, 0, 3));


        // 화면 공간 오버레이 패널은 카메라 없이 렌더 타깃 픽셀에 배치된다.
        // 패널 표본과 기존 장면 표본을 함께 검사해 장면과 UI가 같은 프레임에 공존하는지 확인한다.
        Rendering::SpriteDraw overlayPanel;
        overlayPanel.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        // 위아래 절반을 서로 다른 색으로 칠해 단색 이미지로 구분할 수 없는 수직 뒤집힘을 검사한다.
        overlayPanel.material =
            builder.AddMaterial({ MakeHalvedTexture(
                0x5000'0000'0000'0005ull, OverlayPanelColor, OverlayPanelLowerColor) });
        overlayPanel.space = Rendering::DrawSpace::Screen;
        // 배치가 텍스처 크기(8x8)를 이미 곱하므로, 원하는 픽셀 크기를 그것으로 나눠 싣는다 —
        // UIContext가 자기 사각형을 실을 때 하는 것과 같은 산술이다: 좌상단 (88, 8)에서 32x24.
        overlayPanel.localToWorld =
            Math::Matrix4x4::CreateScale({ 32.0f / 8.0f, 24.0f / 8.0f, 1.0f }) *
            Math::Matrix4x4::CreateTranslation({ 88.0f + 16.0f, 8.0f + 12.0f, 0.0f });
        // flipY를 켜지 않는다. 화면 공간이라고 말한 draw는 그리기가 알아서 텍셀을 되돌려 읽으므로
        // (QuadDrawGeometry의 MakeUvTransform), 여기서 또 켜면 두 번 뒤집혀 거꾸로 선다.
        // UIContext도 화면 공간 갈래에서 같은 이유로 켜지 않는다.
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Overlay, overlayPanel, 0, 1));

        // 화면 공간 nine-slice 패널이다. 테두리 있는 UI 패널이 실제로 원하는 그림이며, 월드
        // nine-slice와 다른 점은 조각의 치수를 재는 단위 하나다: 월드는 pixelsPerUnit으로 나눈
        // 월드 단위, 화면은 텍스처 픽셀이 곧 화면 픽셀이다. 위 띠 2px, 아래 띠 4px로 세로가
        // 비대칭이라, 조립이 뒤집히면 위·아래 표본이 서로 바뀌어 잡힌다.
        auto panelTexture = std::make_shared<Assets::TextureData>();
        panelTexture->id = 0x5000'0000'0000'0006ull;
        panelTexture->width = 8;
        panelTexture->height = 8;
        panelTexture->pixels.resize(panelTexture->GetByteSize());
        for (unsigned int y = 0; y < panelTexture->height; ++y)
        {
            const Rgba band = y < 2 ? PanelTopColor
                : y < 4 ? PanelCenterColor
                        : PanelBottomColor;
            for (unsigned int x = 0; x < panelTexture->width; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * panelTexture->width + x) * 4;
                panelTexture->pixels[offset] = static_cast<std::byte>(band.r);
                panelTexture->pixels[offset + 1] = static_cast<std::byte>(band.g);
                panelTexture->pixels[offset + 2] = static_cast<std::byte>(band.b);
                panelTexture->pixels[offset + 3] = static_cast<std::byte>(band.a);
            }
        }

        Rendering::SpriteDraw uiPanel;
        uiPanel.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        uiPanel.material = builder.AddMaterial({ panelTexture });
        uiPanel.space = Rendering::DrawSpace::Screen;
        uiPanel.sliced = true;
        uiPanel.border = { 2.0f, 2.0f, 2.0f, 4.0f };
        // 화면 공간에서 size는 픽셀이고, localToWorld는 그 사각형의 중심으로만 옮긴다 —
        // 조각들이 이미 픽셀 치수를 갖고 조립되므로 배율을 함께 실을 자리가 없다.
        uiPanel.size = { 40.0f, 32.0f };
        uiPanel.localToWorld = Math::Matrix4x4::CreateTranslation({ 60.0f, 24.0f, 0.0f });
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Overlay, uiPanel, 0, 6));

        return std::move(builder).Build();
    }

    /// <summary>
    /// 한 텍스처만 그리는 프레임이다. 텍스처는 호출자의 것이라, 같은 id 아래 픽셀을 바꾸고
    /// revision을 올린 뒤 다시 프레임을 만들면 백엔드가 리소스를 제자리에서 갱신하는지 보인다.
    /// </summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildDynamicTextureFrame(
        const std::shared_ptr<const GameEngine::Assets::TextureData>& texture)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });
        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        Rendering::SpriteDraw sprite;
        sprite.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });
        sprite.material = builder.AddMaterial({ texture });
        sprite.localToWorld = Math::Matrix4x4::CreateTranslation({ 0.0f, 0.0f, SpriteDepth });
        sprite.pixelsPerUnit = 4.0f;
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, sprite, 0, 1));
        return std::move(builder).Build();
    }

    /// <summary>
    /// 같은 id의 텍스처가 revision과 함께 새 픽셀을 받으면, 백엔드는 id로 캐시한 리소스를
    /// 버리지 않고 픽셀만 다시 올려야 한다. 에디터의 뷰 이미지가 매 프레임 이렇게 도착한다.
    /// </summary>
    [[nodiscard]] bool RunDynamicTextureTest(
        GameEngine::Rendering::IGraphicsDevice& device, const std::string& prefix)
    {
        using namespace GameEngine;

        constexpr Rgba First{ 200, 40, 40, 255 };
        constexpr Rgba Second{ 40, 200, 40, 255 };
        auto texture = std::make_shared<Assets::TextureData>();
        texture->id = 0x4000'0000'0000'0077ull;
        texture->revision = 1;
        texture->width = 8;
        texture->height = 8;
        texture->pixels.resize(texture->GetByteSize());
        const auto fill = [&texture](const Rgba& color)
        {
            for (std::size_t pixel = 0; pixel < texture->pixels.size(); pixel += 4)
            {
                texture->pixels[pixel] = static_cast<std::byte>(color.r);
                texture->pixels[pixel + 1] = static_cast<std::byte>(color.g);
                texture->pixels[pixel + 2] = static_cast<std::byte>(color.b);
                texture->pixels[pixel + 3] = static_cast<std::byte>(color.a);
            }
        };

        fill(First);
        Rendering::CapturedImage before;
        const bool renderedFirst =
            device.RenderToImage(BuildDynamicTextureFrame(texture), before) && before.IsValid();

        fill(Second);
        ++texture->revision;
        Rendering::CapturedImage after;
        const bool renderedSecond =
            device.RenderToImage(BuildDynamicTextureFrame(texture), after) && after.IsValid();

        return Expect(renderedFirst && renderedSecond, (prefix + "dynamic texture frames should render").c_str()) &&
            Expect(
                ReadPixel(before, SpriteSampleX, SpriteSampleY) == First,
                (prefix + "the first revision should show its pixels").c_str()) &&
            Expect(
                ReadPixel(after, SpriteSampleX, SpriteSampleY) == Second,
                (prefix + "a new revision under the same id should show the new pixels").c_str());
    }

    /// <summary>
    /// 한 텍스트 페이지만 그리는 프레임이다. 페이지는 호출자의 것이라, 같은 id 아래 다른
    /// 그림으로 다시 프레임을 만들면 백엔드가 그 새 그림을 실제로 올리는지 보인다.
    /// </summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildTextPageFrame(
        const std::shared_ptr<const GameEngine::Rendering::RasterizedTextImage>& page)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });
        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        auto glyphs = std::make_shared<std::vector<Rendering::TextGlyphQuad>>();
        Rendering::TextGlyphQuad glyph;
        glyph.width = static_cast<float>(page->width);
        glyph.height = static_cast<float>(page->height);
        glyph.uWidth = 1.0f;
        glyph.vHeight = 1.0f;
        glyphs->push_back(glyph);

        Rendering::TextDraw text;
        text.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Text });
        text.page = page;
        text.glyphs = glyphs;
        text.space = Rendering::TextSpace::Screen;
        text.tint = TextTint;
        text.localToWorld = Math::Matrix4x4::CreateTranslation(
            { static_cast<float>(TextPageSampleX), static_cast<float>(TextPageSampleY), 0.0f });
        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Transparent, text, 0, 1));
        return std::move(builder).Build();
    }

    /// <summary>
    /// 같은 id의 텍스트 페이지가 <b>다른 객체</b>로 다음 revision을 받으면 — 아틀라스 페이지의
    /// 복사-성장이 정확히 이 모양이다 — 백엔드는 그 새 그림을 올려야 한다.
    ///
    /// 위 동적 텍스처 시험과 짝이지만 결정적으로 다르다: 그 시험은 같은 벡터를 제자리에서
    /// 고쳐 쓰지만, 여기서는 옛 그림을 건드리지 않고 완전히 다른 벡터를 가진 새 객체를 세운다.
    /// 캐시 적중 때 revision만 따라가고 픽셀 출처를 다시 잇지 않는 백엔드는, 옛 그림 — 잉크가
    /// 하나도 없는 그림 — 을 계속 올리므로 이 시험에서만 걸린다.
    /// </summary>
    [[nodiscard]] bool RunGrowingTextPageTest(
        GameEngine::Rendering::IGraphicsDevice& device, const std::string& prefix)
    {
        using namespace GameEngine;

        constexpr std::uint64_t PageId = 0x3000'0000'0000'0099ull;
        constexpr unsigned int PageSize = 8;

        auto empty = std::make_shared<Rendering::RasterizedTextImage>();
        empty->id = PageId;
        empty->revision = 1;
        empty->width = PageSize;
        empty->height = PageSize;
        empty->alphaPixels.assign(static_cast<std::size_t>(PageSize) * PageSize, std::byte{ 0 });

        Rendering::CapturedImage before;
        const bool renderedFirst =
            device.RenderToImage(BuildTextPageFrame(empty), before) && before.IsValid();

        // 옛 객체는 건드리지 않는다 — 그것이 복사-성장의 계약이다. 그것을 복사해 잉크를 채운
        // 새 객체를 같은 id, 다음 revision으로 세운다.
        auto grown = std::make_shared<Rendering::RasterizedTextImage>(*empty);
        grown->alphaPixels.assign(static_cast<std::size_t>(PageSize) * PageSize, std::byte{ 255 });
        ++grown->revision;

        Rendering::CapturedImage after;
        const bool renderedSecond =
            device.RenderToImage(BuildTextPageFrame(grown), after) && after.IsValid();

        return Expect(
                renderedFirst && renderedSecond,
                (prefix + "growing text page frames should render").c_str()) &&
            Expect(
                ReadPixel(before, TextPageSampleX, TextPageSampleY) == ExpectedClearColor(),
                (prefix + "a page with no ink yet should show the clear colour").c_str()) &&
            Expect(
                ReadPixel(after, TextPageSampleX, TextPageSampleY) == ExpectedTextColor(),
                (prefix + "a grown page under the same id should show its new ink").c_str());
    }

    struct BackendImage
    {
        std::string id;
        GameEngine::Rendering::CapturedImage clearOnly;
        GameEngine::Rendering::CapturedImage withDraws;
    };

    /// <summary>
    /// 두 백엔드의 픽셀이 얼마나 벌어져도 되는지이다.
    ///
    /// 현재 Direct3D 11과 12는 여기서 바이트 단위로 동일한 이미지를 만들고, 실행이 그것을
    /// 출력하므로 이 한계 쪽으로의 표류가 눈에 보인다. 동등이 아니라 한계인 이유는, 0을 요구하는
    /// 것은 두 API가 가장자리를 동일하게 래스터화·필터링한다고 단언하는 셈인데 어느 쪽도 그것을
    /// 약속하지 않기 때문이다. 내부와 배경은 백엔드 하나씩, 정확 값으로 단언된다.
    /// </summary>
    constexpr int MaximumChannelDifference = 2;
    constexpr double MinimumIdenticalPixelRatio = 0.98;

    [[nodiscard]] bool ImagesAgree(
        const GameEngine::Rendering::CapturedImage& left,
        const GameEngine::Rendering::CapturedImage& right,
        const char* what)
    {
        if (left.width != right.width || left.height != right.height)
        {
            std::cerr << "FAILED: " << what << " differ in size\n";
            return false;
        }

        std::size_t identical = 0;
        int worst = 0;
        for (unsigned int y = 0; y < left.height; ++y)
        {
            for (unsigned int x = 0; x < left.width; ++x)
            {
                const Rgba a = ReadPixel(left, x, y);
                const Rgba b = ReadPixel(right, x, y);
                if (a == b)
                {
                    ++identical;
                    continue;
                }
                const std::array<int, 4> differences{
                    std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)),
                    std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)),
                    std::abs(static_cast<int>(a.b) - static_cast<int>(b.b)),
                    std::abs(static_cast<int>(a.a) - static_cast<int>(b.a))
                };
                worst = (std::max)(worst, *std::ranges::max_element(differences));
            }
        }

        const auto total = static_cast<double>(left.width) * left.height;
        const double ratio = static_cast<double>(identical) / total;
        if (worst > MaximumChannelDifference || ratio < MinimumIdenticalPixelRatio)
        {
            std::cerr << "FAILED: " << what << " disagree. identical=" << ratio
                << ", worst channel difference=" << worst << '\n';
            return false;
        }

        // Printed when it passes too: how close two backends are is the number this whole file
        // exists to produce, and a bare "passed" would hide it drifting towards the limit.
        std::cout << "  " << what << ": identical=" << ratio
            << ", worst channel difference=" << worst << '\n';
        return true;
    }
}

bool RunBackendImageTests()
{
    using namespace GameEngine;

    const std::vector<const Rendering::GraphicsBackendDescriptor*> supported = SupportedBackends();
    if (supported.empty())
    {
        // A machine with no supported backend cannot answer this question, and saying nothing is
        // better than an assertion that only ever passes because it never ran.
        std::cout << "  backend image tests skipped: no supported graphics backend\n";
        return true;
    }

    const Rgba expectedClear = ExpectedClearColor();
    std::vector<BackendImage> results;
    bool passed = true;
    for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
    {
        BackendImage result;
        result.id = std::string(backend->id);
        const std::string prefix = result.id + ": ";

        // headless로 세운다: 캡처를 비교하는 데 창은 필요 없고, 창 없이 서는 것 자체가 이
        // 테스트가 함께 증명하는 계약이다 — 에디터의 게임 뷰가 정확히 이 길로 장치를 세운다.
        const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
        if (!device || !device->Initialize(Platform::NativeSurface{}))
        {
            passed &= Expect(
                false, (prefix + "a supported backend should initialize headless").c_str());
            continue;
        }

        passed &= Expect(
            device->RenderToImage(BuildTestFrame(false), result.clearOnly) && result.clearOnly.IsValid(),
            (prefix + "a frame with no draws should produce an image").c_str());
        passed &= Expect(
            device->RenderToImage(BuildTestFrame(true), result.withDraws) && result.withDraws.IsValid(),
            (prefix + "a frame with draws should produce an image").c_str());
        if (!result.clearOnly.IsValid() || !result.withDraws.IsValid())
        {
            continue;
        }

        passed &= Expect(
            result.clearOnly.width == ImageWidth && result.clearOnly.height == ImageHeight,
            (prefix + "a capture should be the size the frame asked for, not the window's").c_str());

        // Every pixel, not a sample: a target left partly untouched is exactly the failure this is
        // here to catch.
        bool clearedEverywhere = true;
        for (unsigned int y = 0; y < result.clearOnly.height && clearedEverywhere; ++y)
        {
            for (unsigned int x = 0; x < result.clearOnly.width; ++x)
            {
                const Rgba pixel = ReadPixel(result.clearOnly, x, y);
                if (pixel != expectedClear)
                {
                    std::cerr << "  " << result.id << " cleared to " << Describe(pixel)
                        << " at (" << x << ", " << y << "), expected " << Describe(expectedClear)
                        << '\n';
                    clearedEverywhere = false;
                    break;
                }
            }
        }
        passed &= Expect(
            clearedEverywhere,
            (prefix + "an empty frame should be the camera's clear colour everywhere").c_str());


        // 타일맵은 공용 상한이 없다면 D3D12가 도중에 잘랐을 크기다. 칸 하나를 표본으로 잡아, 배치가
        // 실제로 그려졌는지 — 그리고 두 백엔드가 같은 결과를 냈는지 — 를 이 자리에서 고정한다.

        // 오버레이 패널이 화면 좌표 그대로 놓였고, 그 아래 장면 표본들이 카메라 배치를 그대로
        // 지켰다는 것을 함께 본다: 둘 중 하나라도 어긋나면 장면과 UI는 한 프레임에 공존하지
        // 못한다는 뜻이다.
        // 위 절반과 아래 절반을 따로 읽는다. 한 점만 읽으면 패널이 뒤집혀도 통과하고, UIContext가
        // 오버레이를 싣는 자리가 바로 세로 방향을 다루는 자리다.
        const Rgba overlayUpper =
            ReadPixel(result.withDraws, OverlaySampleX, OverlayUpperSampleY);
        const Rgba overlayLower =
            ReadPixel(result.withDraws, OverlaySampleX, OverlayLowerSampleY);
        if (overlayUpper != OverlayPanelColor || overlayLower != OverlayPanelLowerColor)
        {
            std::cerr << "  " << prefix << "the overlay panel reads " << Describe(overlayUpper)
                      << " above and " << Describe(overlayLower) << " below, against "
                      << Describe(OverlayPanelColor) << " and "
                      << Describe(OverlayPanelLowerColor) << "\n";
        }
        passed &= Expect(
            overlayUpper == OverlayPanelColor,
            (prefix + "a screen-space overlay panel should be its own colour").c_str());
        passed &= Expect(
            overlayLower == OverlayPanelLowerColor,
            (prefix + "and should be drawn the way up its image is stored").c_str());

        // 화면 공간 nine-slice: 세 표본이 위 띠·중앙·아래 띠를 순서대로 짚는다. 조립이 세로로
        // 뒤집히면 위와 아래가 서로 바뀌고, 조각의 치수가 여전히 월드 단위면 띠가 화면을 덮거나
        // 사라진다 — 어느 쪽이든 여기서 걸린다.
        passed &= Expect(
            ReadPixel(result.withDraws, PanelSampleX, PanelTopSampleY) == PanelTopColor,
            (prefix + "a screen-space sliced panel should show its top band at the top").c_str());
        passed &= Expect(
            ReadPixel(result.withDraws, PanelSampleX, PanelCenterSampleY) == PanelCenterColor,
            (prefix + "a screen-space sliced panel should stretch its centre").c_str());
        passed &= Expect(
            ReadPixel(result.withDraws, PanelSampleX, PanelBottomSampleY) == PanelBottomColor,
            (prefix + "a screen-space sliced panel should show its bottom band at the bottom")
                .c_str());

        const Rgba tilemapPixel = ReadPixel(result.withDraws, TilemapSampleX, TilemapSampleY);
        passed &= Expect(
            tilemapPixel == TilemapColor,
            (prefix + "a tile inside a large tilemap layer should be the tileset's colour").c_str());

        const Rgba sprite = ReadPixel(result.withDraws, SpriteSampleX, SpriteSampleY);
        passed &= Expect(
            sprite == SpriteColor,
            (prefix + "the middle of an opaque sprite should be the sprite's own colour").c_str());

        // Fully covered text is its tint and nothing else, so this is stated exactly. The lower
        // half of the glyph carries no ink, so it stays the clear colour — together the two
        // samples also pin the glyph's vertical orientation.
        const Rgba text = ReadPixel(result.withDraws, TextSampleX, TextSampleY);
        passed &= Expect(
            text == ExpectedTextColor(),
            (prefix + "text at full coverage should be its tint").c_str());
        passed &= Expect(
            ReadPixel(result.withDraws, TextLowerSampleX, TextLowerSampleY) == expectedClear,
            (prefix + "the inkless half of a glyph should keep the clear colour").c_str());

        // The mesh goes through lighting, so what it should produce is not a value this test can
        // state without restating the shader. What it can state is that it drew something, and the
        // cross-backend comparison below then requires both backends to have drawn the same thing.
        const Rgba mesh = ReadPixel(result.withDraws, MeshSampleX, MeshSampleY);
        passed &= Expect(
            mesh != expectedClear,
            (prefix + "the mesh should have covered its part of the image").c_str());
        std::cout << "  " << result.id << " drew sprite=" << Describe(sprite)
            << " mesh=" << Describe(mesh) << " text=" << Describe(text) << '\n';

        // The sliced sprite is asserted exactly on both sides of its border: sampling well inside
        // a cell of same-coloured texels defeats filtering, so the corner must be pure border
        // colour and the middle pure centre colour.
        passed &= Expect(
            ReadPixel(result.withDraws, SlicedCornerSampleX, SlicedCornerSampleY) == SlicedBorderColor,
            (prefix + "a sliced sprite corner should keep the border texels").c_str());
        passed &= Expect(
            ReadPixel(result.withDraws, SlicedCenterSampleX, SlicedCenterSampleY) == SlicedCenterColor,
            (prefix + "a sliced sprite middle should stretch the centre texels").c_str());

        passed &= Expect(
            ReadPixel(result.withDraws, 0, 0) == expectedClear,
            (prefix + "a corner outside every draw should keep the clear colour").c_str());

        passed &= RunDynamicTextureTest(*device, prefix);
        passed &= RunGrowingTextPageTest(*device, prefix);

        // 지연 캡처: 두 번째 호출까지는 반드시 이미지가 나오고, 그것은 즉시 캡처와 같아야 한다.
        {
            Rendering::CapturedImage deferred;
            const Rendering::IGraphicsDevice::CaptureRequest request{ 1, true };
            const bool first = device->RenderToImage(BuildTestFrame(true), deferred, request);
            if (!first || !deferred.IsValid())
            {
                static_cast<void>(device->RenderToImage(BuildTestFrame(true), deferred, request));
            }
            passed &= Expect(
                deferred.IsValid(),
                (prefix + "a deferred capture should produce an image by its second call").c_str());
            passed &= deferred.IsValid() &&
                ImagesAgree(result.withDraws, deferred, (prefix + "immediate and deferred captures").c_str());
        }

        results.push_back(std::move(result));
    }

    for (std::size_t index = 1; index < results.size(); ++index)
    {
        const BackendImage& first = results.front();
        const BackendImage& other = results[index];
        const std::string what = first.id + " and " + other.id;
        passed &= ImagesAgree(first.clearOnly, other.clearOnly, (what + " empty frames").c_str());
        passed &= ImagesAgree(first.withDraws, other.withDraws, (what + " content frames").c_str());
    }

    if (results.size() < 2)
    {
        std::cout << "  only one backend is supported here, so nothing was compared across backends\n";
    }
    return passed;
}

static const TestSupport::Registration gBackendImageTests{
    "BackendImage", "backend image tests should pass", RunBackendImageTests };
