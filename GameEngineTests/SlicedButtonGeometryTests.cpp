#include "SlicedButtonGeometryTests.h"

#include <array>
#include <cmath>
#include <iostream>

#include "Math/Matrix.h"
#include "Rendering/QuadDrawGeometry.h"
#include "Rendering/RenderFrameBuilder.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>버튼 그림의 테두리다. button-32가 선언하는 값이며 논리 픽셀이다.</summary>
    constexpr float LogicalBorder = 8.0f;

    /// <summary>이 시험이 세우는 버튼의 크기다. 논리 픽셀이며, 두 테두리보다 넉넉히 크다.</summary>
    constexpr float LogicalWidth = 96.0f;
    constexpr float LogicalHeight = 32.0f;

    /// <summary>한 조각이 차지하는 크기다. 단위 quad의 두 모서리를 옮겨 재어 얻는다.</summary>
    struct PieceSize
    {
        float width = 0.0f;
        float height = 0.0f;
    };

    /// <summary>그리는 면의 크기다. 조각의 자리는 클립 공간으로 나오므로 이것으로 픽셀로 돌린다.</summary>
    constexpr float TargetWidth = 1600.0f;
    constexpr float TargetHeight = 900.0f;

    [[nodiscard]] PieceSize MeasurePiece(const GameEngine::Rendering::QuadTransform& quad)
    {
        const GameEngine::Math::Vector3 topLeft =
            quad.worldViewProjection.TransformPoint({ -0.5f, 0.5f, 0.0f });
        const GameEngine::Math::Vector3 bottomRight =
            quad.worldViewProjection.TransformPoint({ 0.5f, -0.5f, 0.0f });
        // 클립 공간은 -1..1이라 폭 하나가 면의 절반에 해당한다.
        return { std::abs(bottomRight.GetX() - topLeft.GetX()) * TargetWidth * 0.5f,
                 std::abs(topLeft.GetY() - bottomRight.GetY()) * TargetHeight * 0.5f };
    }

    [[nodiscard]] bool Near(const float value, const float expected)
    {
        return std::abs(value - expected) < 0.01f;
    }

    /// <summary>
    /// 주어진 배율에서 버튼 하나의 아홉 조각을 재고, 모서리·가장자리·가운데의 크기를 확인한다.
    ///
    /// 화면 공간에서는 테두리도 사각형도 픽셀이다. 그래서 배율이 2면 조각들도 두 배가 되어야
    /// 하며, 한쪽만 배율을 타면 모서리가 절반으로 그려지거나 가운데가 밀려난다 — 화면에서는
    /// 「9-슬라이스가 안 먹은 것 같다」로 보인다.
    /// </summary>
    [[nodiscard]] bool CheckButtonSlicesAtScale(const float scale)
    {
        using namespace GameEngine::Rendering;

        // 카메라를 단위 행렬로 두면 조각의 행렬이 단위 quad를 그대로 픽셀 자리로 옮긴다.
        RenderFrameBuilder builder;
        CameraRenderData camera;
        camera.view = GameEngine::Math::Matrix4x4::Identity();
        camera.projection = GameEngine::Math::Matrix4x4::Identity();
        builder.SetCamera(camera);
        // 화면 공간은 그리는 면의 크기를 요구한다. 조각의 자리가 그 면 위의 픽셀이기 때문이다.
        builder.SetRenderTargetSize(
            { static_cast<int>(TargetWidth), static_cast<int>(TargetHeight) });
        const RenderFrame frame = std::move(builder).Build();

        // SceneRenderPass가 화면 공간 스프라이트에 대해 세우는 것과 같은 값이다: 테두리는
        // 캔버스 배율을 타고, 크기는 배치가 이미 곱해 둔 픽셀이다.
        SpriteDraw sliced;
        sliced.sliced = true;
        sliced.space = DrawSpace::Screen;
        // 테두리는 그림이 선언한 그대로 텍스처 픽셀이고, 화면에서 얼마나 크게 그릴지는
        // 배수로 따로 말한다 — SceneRenderPass가 화면 공간 스프라이트에 세우는 그대로다.
        sliced.border = { LogicalBorder, LogicalBorder, LogicalBorder, LogicalBorder };
        sliced.borderScale = scale;
        const float border = LogicalBorder * scale;
        sliced.size = { LogicalWidth * scale, LogicalHeight * scale };
        sliced.pixelsPerUnit = 1.0f;

        std::array<QuadTransform, 9> quads;
        const std::size_t count = BuildSlicedSpriteQuads(frame, sliced, { 32, 32 }, "Test", quads);
        if (!Expect(count == 9, "a button larger than its borders has all nine pieces"))
        {
            return false;
        }

        bool passed = true;
        const float middleWidth = sliced.size.GetX() - border * 2.0f;
        const float middleHeight = sliced.size.GetY() - border * 2.0f;

        // 조각은 왼쪽 위에서 행 우선으로 나온다.
        const std::array<PieceSize, 9> expected{
            PieceSize{ border, border },       PieceSize{ middleWidth, border },
            PieceSize{ border, border },       PieceSize{ border, middleHeight },
            PieceSize{ middleWidth, middleHeight }, PieceSize{ border, middleHeight },
            PieceSize{ border, border },       PieceSize{ middleWidth, border },
            PieceSize{ border, border } };
        static constexpr std::array<const char*, 9> Names{
            "top-left", "top", "top-right", "left", "middle", "right",
            "bottom-left", "bottom", "bottom-right" };

        for (std::size_t index = 0; index < quads.size(); ++index)
        {
            const PieceSize actual = MeasurePiece(quads[index]);
            const bool correct = Near(actual.width, expected[index].width) &&
                Near(actual.height, expected[index].height);
            if (!correct)
            {
                std::cerr << "  the " << Names[index] << " piece is " << actual.width << "x"
                          << actual.height << "px, not " << expected[index].width << "x"
                          << expected[index].height << "px\n";
            }
            passed &= Expect(
                correct, "each piece is the size the border and the rectangle ask for");
        }

        // 조각이 텍스처의 어느 부분을 읽는지도 함께 본다. 크기가 맞아도 읽는 자리가 어긋나면
        // 모서리에 엉뚱한 픽셀이 들어와 단이 생기고, 그것은 「9-슬라이스가 안 먹었다」와 같은
        // 모양으로 보인다. 32픽셀 그림에 테두리 8이면 모서리는 텍스처의 4분의 1이다.
        const std::array<float, 3> spans{ 0.25f, 0.5f, 0.25f };
        const std::array<float, 3> offsets{ 0.0f, 0.25f, 0.75f };
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                const QuadTransform& quad = quads[row * 3 + column];
                // 화면 공간에서는 공유 quad의 로컬 +y가 화면 아래로 향하므로 v를 거꾸로 읽어야 한다.
                // 세로 배율은 음수이고 오프셋은 해당 띠의 반대쪽 끝을 가리켜야 한다.
                const float expectedVSpan = -spans[row];
                const float expectedVOffset = offsets[row] + spans[row];
                const bool samples = Near(quad.uvTransform[0], spans[column]) &&
                    Near(quad.uvTransform[1], expectedVSpan) &&
                    Near(quad.uvTransform[2], offsets[column]) &&
                    Near(quad.uvTransform[3], expectedVOffset);
                if (!samples)
                {
                    std::cerr << "  the " << Names[row * 3 + column] << " piece samples "
                              << quad.uvTransform[0] << "x" << quad.uvTransform[1] << " at "
                              << quad.uvTransform[2] << "," << quad.uvTransform[3]
                              << " instead of " << spans[column] << "x" << expectedVSpan << " at "
                              << offsets[column] << "," << expectedVOffset << "\n";
                }
                passed &= Expect(samples, "and reads the part of the image it stands for");
            }
        }
        return passed;
    }
}

bool RunSlicedButtonGeometryTests()
{
    return TestSupport::ForEachUiScale(CheckButtonSlicesAtScale);
}

static const TestSupport::Registration gSlicedButtonGeometryTests{
    "RenderFrame", "sliced button geometry tests should pass", RunSlicedButtonGeometryTests };
