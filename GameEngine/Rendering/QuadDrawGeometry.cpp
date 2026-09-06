#include "pch.h"
#include "QuadDrawGeometry.h"
#include "../Diagnostics/Debug.h"
#include "RenderColorPolicy.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace GameEngine::Rendering
{
namespace
{
    /// <summary>단위 quad를 텍스처 크기로 배율한 뒤 draw의 변환으로 배치한다.</summary>
    [[nodiscard]] Math::Matrix4x4 MakeQuadWorldMatrix(
        const QuadTextureSize texture,
        const float scale,
        const Math::Matrix4x4& localToWorld)
    {
        return Math::Matrix4x4::CreateScale({
            static_cast<float>(texture.width) * scale,
            static_cast<float>(texture.height) * scale,
            1.0f }) * localToWorld;
    }

    /// <summary>
    /// 렌더 타깃 픽셀을 좌상단 원점의 클립 공간으로 사상하는 투영이다. 아래 경계로 높이를, 위
    /// 경계로 0을 넘기는 것이 Y축을 뒤집는 방법이다.
    /// </summary>
    [[nodiscard]] Math::Matrix4x4 MakeScreenProjection(const RenderTargetSize renderTargetSize)
    {
        return Math::Matrix4x4::CreateOrthographicOffCenterLeftHanded(
            0.0f, static_cast<float>(renderTargetSize.width),
            static_cast<float>(renderTargetSize.height), 0.0f,
            0.0f, 1.0f);
    }

    /// <summary>
    /// 이미지 안의 사각형 하나를 읽도록 quad에 실을 UV 배율과 오프셋이다.
    ///
    /// 공유 quad는 로컬 +y 정점에 v=0을, 즉 이미지의 위를 놓는다. 월드는 y가 위로 가므로 그대로
    /// 맞다. 그러나 <see cref="MakeScreenProjection"/>은 y를 뒤집어 로컬 +y를 화면 <b>아래</b>로
    /// 보내므로, 화면 공간에서는 v를 거꾸로 읽어야 그림이 바로 선다.
    ///
    /// 화면 공간의 y 방향을 아는 자리는 둘뿐이고 서로 반대여야 한다: 자리를 뒤집는 곳이 위의
    /// 투영이고, 그래서 텍셀을 되돌려 읽는 곳이 여기다. 뒤집기를 세 번째 자리에 또 넣으면 두
    /// 번 뒤집혀 제자리로 돌아온다 — 그러니 이 함수 밖에서 화면 공간이라는 이유로 v를 건드리지
    /// 않는다.
    /// </summary>
    /// <param name="vSpan">이미 <c>flipY</c>가 반영된 세로 범위다. 두 뒤집기는 서로 상쇄한다.</param>
    [[nodiscard]] std::array<float, 4> MakeUvTransform(
        const float uSpan, const float vSpan,
        const float uStart, const float vStart,
        const bool screenSpace)
    {
        return {
            uSpan,
            screenSpace ? -vSpan : vSpan,
            uStart,
            screenSpace ? vStart + vSpan : vStart };
    }
}

std::optional<QuadTransform> TryBuildSpriteQuad(
    const RenderFrame& frame,
    const SpriteDraw& draw,
    const QuadTextureSize texture,
    const char* const backendName)
{
    // 화면 공간 스프라이트는 렌더 타깃 픽셀로 놓이므로 카메라를 쓰지 않는다. 월드 스프라이트는
    // 프론트엔드가 언제나 실어 주는 카메라 상태를 쓰며, 백엔드가 자기 투영을 대신 만들어서는
    // 안 된다. 텍스트와 같은 구분이다.
    const bool screenSpace = draw.space == DrawSpace::Screen;
    const std::optional<CameraRenderData>& camera = frame.GetCamera();
    const RenderTargetSize renderTargetSize = frame.GetRenderTargetSize();
    if (screenSpace && !renderTargetSize.IsValid())
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render a screen-space sprite without a render-target size.");
        return std::nullopt;
    }
    if (!screenSpace && !camera)
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render a sprite without frame camera state.");
        return std::nullopt;
    }

    // 시트의 한 프레임은 이미지의 일부다: quad는 프레임의 픽셀 크기를 갖고, UV는 그 사각형
    // 안에서만 움직인다. 프레임이 곧 이미지 전체인 스프라이트는 이 곱이 1이다.
    const QuadTextureSize frameSize{
        static_cast<unsigned int>(
            std::lround(static_cast<float>(texture.width) * draw.uvRect.width)),
        static_cast<unsigned int>(
            std::lround(static_cast<float>(texture.height) * draw.uvRect.height)) };

    QuadTransform quad;
    quad.worldViewProjection =
        MakeQuadWorldMatrix(
            frameSize, screenSpace ? 1.0f : 1.0f / draw.pixelsPerUnit, draw.localToWorld) *
        (screenSpace ? MakeScreenProjection(renderTargetSize)
                     : camera->view * camera->projection);
    quad.tint = ToLinearColor(draw.tint);
    // 뒤집기는 프레임 안에서 일어난다: 배율의 부호를 뒤집고 오프셋을 반대쪽 끝으로 옮기면,
    // 같은 프레임이 좌우/상하로만 뒤집힌다.
    quad.uvTransform = MakeUvTransform(
        draw.flipX ? -draw.uvRect.width : draw.uvRect.width,
        draw.flipY ? -draw.uvRect.height : draw.uvRect.height,
        draw.flipX ? draw.uvRect.u + draw.uvRect.width : draw.uvRect.u,
        draw.flipY ? draw.uvRect.v + draw.uvRect.height : draw.uvRect.v,
        screenSpace);
    return quad;
}

std::size_t BuildSlicedSpriteQuads(
    const RenderFrame& frame,
    const SpriteDraw& draw,
    const QuadTextureSize texture,
    const char* const backendName,
    std::array<QuadTransform, 9>& quads)
{
    // 조각난 스프라이트도 통짜 스프라이트와 같은 두 공간에 놓인다. 다른 것은 조각의 치수를
    // 어느 단위로 재느냐 하나다: 월드에서는 테두리를 pixelsPerUnit으로 나눠 월드 단위로 만들고,
    // 화면에서는 텍스처 픽셀이 곧 화면 픽셀이라 그대로 쓴다.
    const bool screenSpace = draw.space == DrawSpace::Screen;
    const std::optional<CameraRenderData>& camera = frame.GetCamera();
    const RenderTargetSize renderTargetSize = frame.GetRenderTargetSize();
    if (screenSpace && !renderTargetSize.IsValid())
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render a screen-space sliced sprite without a render-target size.");
        return 0;
    }
    if (!screenSpace && !camera)
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render a sliced sprite without frame camera state.");
        return 0;
    }
    if (texture.width == 0 || texture.height == 0)
    {
        return 0;
    }

    // The border in texture pixels, shrunk proportionally when opposite borders overlap — the same
    // give a picture frame has: a frame wider than its picture keeps its shape by getting thinner.
    float left = draw.border.left;
    float right = draw.border.right;
    float top = draw.border.top;
    float bottom = draw.border.bottom;
    const auto width = static_cast<float>(texture.width);
    const auto height = static_cast<float>(texture.height);
    if (left + right > width && left + right > 0.0f)
    {
        const float fit = width / (left + right);
        left *= fit;
        right *= fit;
    }
    if (top + bottom > height && top + bottom > 0.0f)
    {
        const float fit = height / (top + bottom);
        top *= fit;
        bottom *= fit;
    }

    // The corner cells keep the size the border names in world units, shrunk again when the target
    // extent leaves no room for both sides at full size.
    const float targetWidth = draw.size.GetX();
    const float targetHeight = draw.size.GetY();
    // 조각의 크기에만 배율이 곱해진다. UV는 위에서 텍스처 픽셀 그대로 계산됐고, 그 둘이
    // 서로의 값을 건드리지 않는 것이 border와 borderScale을 나눠 실은 이유다.
    const float insetScale =
        (screenSpace ? 1.0f : 1.0f / draw.pixelsPerUnit) * draw.borderScale;
    float leftInset = left * insetScale;
    float rightInset = right * insetScale;
    float topInset = top * insetScale;
    float bottomInset = bottom * insetScale;
    if (leftInset + rightInset > targetWidth && leftInset + rightInset > 0.0f)
    {
        const float fit = targetWidth / (leftInset + rightInset);
        leftInset *= fit;
        rightInset *= fit;
    }
    if (topInset + bottomInset > targetHeight && topInset + bottomInset > 0.0f)
    {
        const float fit = targetHeight / (topInset + bottomInset);
        topInset *= fit;
        bottomInset *= fit;
    }

    // A flip mirrors the texture, so the border thicknesses travel with their texels: the column
    // that shows the right border must be as wide as the right border says.
    if (draw.flipX)
    {
        std::swap(leftInset, rightInset);
    }
    if (draw.flipY)
    {
        std::swap(topInset, bottomInset);
    }

    // Cell boundaries: positions left to right and top to bottom, texture coordinates in the same
    // order. Texture v runs top-down while world y runs up, which is why the y boundaries descend.
    const std::array<float, 4> xBounds{
        -targetWidth * 0.5f,
        -targetWidth * 0.5f + leftInset,
        targetWidth * 0.5f - rightInset,
        targetWidth * 0.5f,
    };
    // 월드는 y가 위로, 화면은 아래로 간다. 경계의 부호를 통째로 뒤집으면 위 조각이 위에 남는다 —
    // 뒤집지 않으면 nine-slice가 세로로 거꾸로 조립된다.
    const float yDirection = screenSpace ? -1.0f : 1.0f;
    const std::array<float, 4> yBounds{
        yDirection * (targetHeight * 0.5f),
        yDirection * (targetHeight * 0.5f - topInset),
        yDirection * (-targetHeight * 0.5f + bottomInset),
        yDirection * (-targetHeight * 0.5f),
    };
    std::array<float, 4> uBounds{ 0.0f, left / width, 1.0f - right / width, 1.0f };
    std::array<float, 4> vBounds{ 0.0f, top / height, 1.0f - bottom / height, 1.0f };
    if (draw.flipX)
    {
        uBounds = { 1.0f - uBounds[3], 1.0f - uBounds[2], 1.0f - uBounds[1], 1.0f - uBounds[0] };
    }
    if (draw.flipY)
    {
        vBounds = { 1.0f - vBounds[3], 1.0f - vBounds[2], 1.0f - vBounds[1], 1.0f - vBounds[0] };
    }

    const Math::Matrix4x4 viewProjection = screenSpace
        ? MakeScreenProjection(renderTargetSize)
        : camera->view * camera->projection;
    std::size_t count = 0;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            const float cellWidth = xBounds[column + 1] - xBounds[column];
            // 경계가 어느 방향으로 늘어서든 조각의 높이는 그 사이의 거리다.
            const float cellHeight = std::fabs(yBounds[row + 1] - yBounds[row]);
            if (cellWidth <= 0.0f || cellHeight <= 0.0f)
            {
                continue;
            }

            QuadTransform& quad = quads[count++];
            quad.worldViewProjection =
                Math::Matrix4x4::CreateScale({ cellWidth, cellHeight, 1.0f }) *
                Math::Matrix4x4::CreateTranslation({
                    (xBounds[column] + xBounds[column + 1]) * 0.5f,
                    (yBounds[row] + yBounds[row + 1]) * 0.5f,
                    0.0f }) *
                draw.localToWorld * viewProjection;
            quad.tint = ToLinearColor(draw.tint);
            quad.uvTransform = MakeUvTransform(
                uBounds[column + 1] - uBounds[column],
                vBounds[row + 1] - vBounds[row],
                uBounds[column],
                vBounds[row],
                screenSpace);
        }
    }
    return count;
}

std::optional<TextQuadBasis> TryBuildTextQuadBasis(
    const RenderFrame& frame,
    const TextDraw& draw,
    const char* const backendName)
{
    // Screen-space text is defined in render-target pixels and never uses the camera. World-space
    // text uses the frame's camera state, which the frontend always provides.
    const std::optional<CameraRenderData>& camera = frame.GetCamera();
    const RenderTargetSize renderTargetSize = frame.GetRenderTargetSize();
    const bool screenSpace = draw.space == TextSpace::Screen;
    if (screenSpace && !renderTargetSize.IsValid())
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render screen-space text without a frame render-target size.");
        return std::nullopt;
    }
    if (!screenSpace && !camera)
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render world-space text without frame camera state.");
        return std::nullopt;
    }

    TextQuadBasis basis;
    // Screen-space text is already measured in pixels, so only world-space text divides by its
    // pixels-per-unit to reach world units.
    basis.scale = screenSpace ? 1.0f : 1.0f / draw.pixelsPerUnit;
    basis.viewProjection = screenSpace
        ? MakeScreenProjection(renderTargetSize)
        : camera->view * camera->projection;
    basis.screenSpace = screenSpace;
    return basis;
}

QuadTransform PlaceTextGlyphQuad(
    const TextQuadBasis& basis,
    const TextDraw& draw,
    const TextGlyphQuad& glyph)
{
    // 글리프의 로컬 y는 블록 위쪽이 +다. draw의 로컬 공간은 스크린이면 픽셀 공간(아래가 +)
    // 이고 월드면 월드 공간(위가 +)이므로, 스크린에서만 부호가 뒤집힌다.
    const float offsetY = basis.screenSpace ? -glyph.centerY : glyph.centerY;
    QuadTransform quad;
    quad.worldViewProjection =
        Math::Matrix4x4::CreateScale(
            { glyph.width * basis.scale, glyph.height * basis.scale, 1.0f }) *
        Math::Matrix4x4::CreateTranslation(
            { glyph.centerX * basis.scale, offsetY * basis.scale, 0.0f }) *
        draw.localToWorld * basis.viewProjection;
    quad.tint = ToLinearColor(draw.tint);
    quad.uvTransform = MakeUvTransform(
        glyph.uWidth, glyph.vHeight, glyph.u, glyph.v, basis.screenSpace);
    return quad;
}


std::optional<Math::Matrix4x4> TryBuildTilemapBasis(
    const RenderFrame& frame,
    const TilemapDraw& draw,
    const char* const backendName)
{
    const std::optional<CameraRenderData>& camera = frame.GetCamera();
    if (!camera)
    {
        Diagnostics::Debug::LogError(
            backendName, " cannot render a tilemap without frame camera state.");
        return std::nullopt;
    }
    return draw.localToWorld * camera->view * camera->projection;
}

QuadTransform PlaceTilemapTileQuad(
    const Math::Matrix4x4& basis,
    const TilemapDraw& draw,
    const TilemapTile& tile)
{
    // 칸의 가운데는 원점에서 (열 + 0.5, 행 + 0.5)칸만큼이다: 원점이 칸 (0, 0)의 왼쪽 아래
    // 모서리라는 타일맵의 규약을 여기서 산술로 옮긴다.
    const float width = draw.cellSize.GetX();
    const float height = draw.cellSize.GetY();
    QuadTransform quad;
    quad.worldViewProjection =
        Math::Matrix4x4::CreateScale({ width, height, 1.0f }) *
        Math::Matrix4x4::CreateTranslation({
            (static_cast<float>(tile.column) + 0.5f) * width,
            (static_cast<float>(tile.row) + 0.5f) * height,
            0.0f }) *
        basis;
    quad.tint = ToLinearColor(draw.tint);
    quad.uvTransform = { tile.uv.width, tile.uv.height, tile.uv.u, tile.uv.v };
    return quad;
}

}
