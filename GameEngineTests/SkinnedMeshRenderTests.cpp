#include "SkinnedMeshRenderTests.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Assets/SkinnedMeshData.h"
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
/// 스키닝 렌더링의 계약: 손으로 지은 스키닝 메시가 D3D11과 D3D12에서 같은 그림이 되고, 뼈 하나를
/// 돌리면 그 뼈에 매인 부분만 실제로 움직인다.
///
/// 이음매가 원점(모델 공간 x=0)에 있는 두 조각 사각형을 쓴다: 왼쪽은 뼈 0에, 오른쪽은 뼈 1에
/// 전부 매인다. 뼈 0은 두 자세 모두 항등이라 왼쪽은 결코 움직이지 않아야 한다 — 그것이 뼈 인덱스가
/// 실제로 정점을 갈라 영향을 준다는 증거다. 뼈 1만 Z축으로 90도 돌리면 오른쪽 조각은 원래 자리를
/// 비우고 회전이 실어 간 자리에 나타나야 한다 — 그 두 자리를 각각 표본으로 잡아, 어느 쪽으로
/// 도는지 추측하지 않고 같은 회전 행렬로 직접 계산한다.
/// </summary>
namespace
{

    constexpr unsigned int ImageWidth = 128;
    constexpr unsigned int ImageHeight = 128;

    /// <summary>BackendImageTests.cpp와 같은 카메라: 128픽셀에 월드 4단위, 원점은 (64, 64),
    /// 월드 1단위는 화면 32픽셀이다. 화면 y는 월드 y와 반대로 간다(위가 화면 위).</summary>
    constexpr float PixelsPerUnit = 32.0f;
    constexpr float ScreenOriginX = 64.0f;
    constexpr float ScreenOriginY = 64.0f;

    constexpr GameEngine::Math::Color ClearColor{ 64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f };
    constexpr Rgba MeshTextureColor{ 30, 200, 90, 255 };
    constexpr float MeshDepth = 2.0f;

    /// <summary>뼈 1의 회전 각도다. 90도라 오른쪽 조각(모델 공간 x:[0,1])이 원래 자리를 완전히
    /// 비우고 위쪽(모델 공간 y:[0,1])으로 옮겨 가, 두 표본 자리가 겹치지 않는다.</summary>
    constexpr float BoneOneRotationDegrees = 90.0f;

    [[nodiscard]] unsigned int ScreenX(const float worldX)
    {
        return static_cast<unsigned int>(ScreenOriginX + worldX * PixelsPerUnit);
    }

    [[nodiscard]] unsigned int ScreenY(const float worldY)
    {
        return static_cast<unsigned int>(ScreenOriginY - worldY * PixelsPerUnit);
    }

    /// <summary>뼈 0에 매인 왼쪽 조각 안의 표본이다. 이음매(x=0)와 바깥 모서리(x=-1) 둘 다에서
    /// 떨어져 있어, 두 자세 어느 쪽에서도 안전하게 안쪽이다.</summary>
    const unsigned int LeftSampleX = ScreenX(-0.75f);
    const unsigned int LeftSampleY = ScreenY(0.0f);

    /// <summary>뼈 1에 매인 오른쪽 조각의, 회전 전 원래 자리 안의 표본이다. 정지 자세에서는
    /// 메시 색이어야 하고, 회전 자세에서는 그 조각이 위로 옮겨 가 지우기 색이어야 한다.</summary>
    const unsigned int RightOriginalSampleX = ScreenX(0.75f);
    const unsigned int RightOriginalSampleY = ScreenY(0.0f);

    /// <summary>
    /// 오른쪽 조각의 회전 후 도착지 안의 표본이다. 어느 쪽으로 도는지 짐작하지 않도록, 회전 전
    /// 표본 자리를 뼈 1과 정확히 같은 행렬로 직접 옮겨서 구한다.
    /// </summary>
    [[nodiscard]] GameEngine::Math::Vector3 ComputeRotatedDestination()
    {
        using namespace GameEngine;
        const Math::Matrix4x4 boneOneRotation =
            Math::Matrix4x4::CreateRotationZDegrees(BoneOneRotationDegrees);
        return boneOneRotation.TransformPoint({ 0.75f, 0.0f, MeshDepth });
    }

    /// <summary>BackendImageTests.cpp의 MakeSolidTexture와 같은 모양이다 — 필터링이 두 백엔드를
    /// 갈라놓을 수 없도록 한 색만 담은 8x8 텍스처다. 그 함수는 그 파일에 사적이라 여기 다시 쓴다.</summary>
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
    /// 이음매가 원점에 있는 두 조각 사각형이다. 왼쪽(모델 x:[-1,0])은 정점 전부가 뼈 0에,
    /// 오른쪽(모델 x:[0,1])은 정점 전부가 뼈 1에 100% 매인다. 각 조각의 네 정점은 왼쪽 사각형의
    /// 것을 그대로 옮긴 것이라, BackendImageTests.cpp의 corner() 감기 순서(시계 방향)를 그대로
    /// 지켜 메시 파이프라인의 backface culling이 둘 다 지운다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<GameEngine::Assets::SkinnedMeshData> BuildHingeMesh()
    {
        using namespace GameEngine;

        const auto vertex = [](const float x, const float y, const std::uint32_t boneIndex)
        {
            Assets::SkinnedMeshVertex result;
            result.position = { x, y, MeshDepth };
            result.normal = { 0.0f, 0.0f, -1.0f };
            result.textureCoordinate = { x, y };
            result.boneIndices = { boneIndex, 0, 0, 0 };
            result.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
            return result;
        };

        auto mesh = std::make_shared<Assets::SkinnedMeshData>();
        mesh->id = 0x1000'0000'0000'0010ull;
        mesh->vertices = {
            vertex(-1.0f, -0.5f, 0), vertex(-1.0f, 0.5f, 0), vertex(0.0f, 0.5f, 0), vertex(0.0f, -0.5f, 0),
            vertex(0.0f, -0.5f, 1), vertex(0.0f, 0.5f, 1), vertex(1.0f, 0.5f, 1), vertex(1.0f, -0.5f, 1)
        };
        mesh->indices = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
        mesh->bounds = Assets::ComputeSkinnedBounds(mesh->vertices);
        return mesh;
    }

    /// <summary>
    /// 스키닝 메시 하나만 그리는 프레임이다. <paramref name="rotateBoneOne"/>이 false면 두 뼈
    /// 모두 항등이라 사각형은 평평한 직사각형으로 남고, true면 뼈 1만 Z축으로 돈다.
    /// </summary>
    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildSkinnedFrame(
        const std::shared_ptr<GameEngine::Assets::SkinnedMeshData>& mesh, const bool rotateBoneOne)
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ ImageWidth, ImageHeight });

        Rendering::CameraRenderData camera;
        camera.clearColor = ClearColor;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        // 메시가 보이려면 프레임이 빛을 실어야 한다 — BackendImageTests.cpp와 같은 조명이다.
        builder.AddAmbientLight({ 0.2f, 0.2f, 0.2f, 1.0f });
        Rendering::LightRenderData sun;
        sun.kind = Rendering::LightKind::Directional;
        sun.direction = { 0.0f, 0.0f, 1.0f };
        sun.color = { 0.8f, 0.8f, 0.8f, 1.0f };
        static_cast<void>(builder.AddLight(sun));

        Rendering::SkinnedMeshDraw draw;
        draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::SkinnedMesh });
        draw.geometry = builder.AddSkinnedGeometry({ mesh });
        draw.material = builder.AddMaterial({ MakeSolidTexture(0x5000'0000'0000'0010ull, MeshTextureColor) });
        draw.localToWorld = Math::Matrix4x4::Identity();

        auto boneMatrices = std::make_shared<std::vector<Math::Matrix4x4>>();
        boneMatrices->push_back(Math::Matrix4x4::Identity());
        boneMatrices->push_back(rotateBoneOne
            ? Math::Matrix4x4::CreateRotationZDegrees(BoneOneRotationDegrees)
            : Math::Matrix4x4::Identity());
        draw.boneMatrices = boneMatrices;

        static_cast<void>(builder.TryAddDraw(Rendering::RenderPass::Opaque, draw));
        return std::move(builder).Build();
    }

    struct SkinnedBackendImages
    {
        std::string id;
        GameEngine::Rendering::CapturedImage staticPose;
        GameEngine::Rendering::CapturedImage rotatedPose;
    };

    /// <summary>BackendImageTests.cpp의 ImagesAgree와 같은 허용치다: 두 API가 가장자리를
    /// 똑같이 래스터화한다고는 약속하지 않으므로 동등이 아니라 한계로 잰다.</summary>
    constexpr int MaximumChannelDifference = 2;
    constexpr double MinimumIdenticalPixelRatio = 0.98;

    [[nodiscard]] bool ImagesAgree(
        const GameEngine::Rendering::CapturedImage& left,
        const GameEngine::Rendering::CapturedImage& right,
        const char* const what)
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
        std::cout << "  " << what << ": identical=" << ratio
            << ", worst channel difference=" << worst << '\n';
        return true;
    }

}

bool RunSkinnedMeshRenderTests()
{
    using namespace GameEngine;

    const std::vector<const Rendering::GraphicsBackendDescriptor*> supported = SupportedBackends();
    if (supported.empty())
    {
        std::cout << "  skinned mesh render tests skipped: no supported graphics backend\n";
        return true;
    }

    const Rgba expectedClear = Quantize(ClearColor);
    const std::shared_ptr<Assets::SkinnedMeshData> mesh = BuildHingeMesh();
    const Rendering::RenderFrame staticFrame = BuildSkinnedFrame(mesh, false);
    const Rendering::RenderFrame rotatedFrame = BuildSkinnedFrame(mesh, true);

    const Math::Vector3 rotatedDestination = ComputeRotatedDestination();
    const auto movedToSampleX = ScreenX(rotatedDestination.GetX());
    const auto movedToSampleY = ScreenY(rotatedDestination.GetY());

    std::vector<SkinnedBackendImages> results;
    bool passed = true;
    for (const Rendering::GraphicsBackendDescriptor* const backend : supported)
    {
        SkinnedBackendImages result;
        result.id = std::string(backend->id);
        const std::string prefix = result.id + ": ";

        const std::unique_ptr<Rendering::IGraphicsDevice> device = backend->CreateDevice();
        if (!device || !device->Initialize(Platform::NativeSurface{}))
        {
            passed &= Expect(
                false, (prefix + "a supported backend should initialize headless").c_str());
            continue;
        }

        passed &= Expect(
            device->RenderToImage(staticFrame, result.staticPose) && result.staticPose.IsValid(),
            (prefix + "the static pose should render").c_str());
        passed &= Expect(
            device->RenderToImage(rotatedFrame, result.rotatedPose) && result.rotatedPose.IsValid(),
            (prefix + "the rotated pose should render").c_str());
        if (!result.staticPose.IsValid() || !result.rotatedPose.IsValid())
        {
            continue;
        }

        // 뼈 0에 매인 왼쪽 조각: 두 자세에서 정확히 같은 픽셀이어야 한다. 조명·법선·색 무엇 하나
        // 다르지 않으니, 값이 조금이라도 벌어지면 뼈 1의 회전이 왼쪽까지 새어 들어갔다는 뜻이다.
        const Rgba leftStatic = ReadPixel(result.staticPose, LeftSampleX, LeftSampleY);
        const Rgba leftRotated = ReadPixel(result.rotatedPose, LeftSampleX, LeftSampleY);
        passed &= Expect(
            leftStatic != expectedClear,
            (prefix + "the bone-0 half should have drawn in the static pose").c_str());
        passed &= Expect(
            leftRotated == leftStatic,
            (prefix + "the bone-0 half should be untouched by bone 1's rotation").c_str());

        // 뼈 1에 매인 오른쪽 조각: 정지 자세에서는 원래 자리를 덮고, 회전 자세에서는 그 자리를
        // 완전히 비운다 — 뼈 하나를 돌리면 메시가 실제로 움직인다는 증거의 절반이다.
        const Rgba rightOriginalStatic =
            ReadPixel(result.staticPose, RightOriginalSampleX, RightOriginalSampleY);
        const Rgba rightOriginalRotated =
            ReadPixel(result.rotatedPose, RightOriginalSampleX, RightOriginalSampleY);
        passed &= Expect(
            rightOriginalStatic != expectedClear,
            (prefix + "the bone-1 half should cover its original spot in the static pose").c_str());
        passed &= Expect(
            rightOriginalRotated == expectedClear,
            (prefix + "the bone-1 half should vacate its original spot once bone 1 rotates").c_str());

        // 나머지 절반: 그 자리가 비었을 뿐 아니라, 뼈 1과 같은 행렬로 직접 계산한 새 자리에
        // 실제로 나타났는지도 본다 — 그냥 사라진 것이 아니라 회전이 실어 간 곳에 있다는 뜻이다.
        const Rgba movedToStatic = ReadPixel(result.staticPose, movedToSampleX, movedToSampleY);
        const Rgba movedToRotated = ReadPixel(result.rotatedPose, movedToSampleX, movedToSampleY);
        passed &= Expect(
            movedToStatic == expectedClear,
            (prefix + "the rotated destination should be empty in the static pose").c_str());
        passed &= Expect(
            movedToRotated != expectedClear,
            (prefix + "the bone-1 half should appear at the rotated destination").c_str());

        results.push_back(std::move(result));
    }

    // 백엔드마다 따로 옳았다는 것과, 서로 같은 그림을 그렸다는 것은 다른 주장이다 — 후자를 여기서
    // 검사한다.
    for (std::size_t index = 1; index < results.size(); ++index)
    {
        const SkinnedBackendImages& first = results.front();
        const SkinnedBackendImages& other = results[index];
        const std::string what = first.id + " and " + other.id;
        passed &= ImagesAgree(first.staticPose, other.staticPose, (what + " static pose").c_str());
        passed &= ImagesAgree(first.rotatedPose, other.rotatedPose, (what + " rotated pose").c_str());
    }

    if (results.size() < 2)
    {
        std::cout << "  only one backend is supported here, so nothing was compared across backends\n";
    }
    return passed;
}

static const TestSupport::Registration gSkinnedMeshRenderTests{
    "SkinnedMeshRender", "skinned mesh render tests should pass", RunSkinnedMeshRenderTests };
