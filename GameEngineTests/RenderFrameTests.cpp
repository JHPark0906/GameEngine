#include "RenderFrameTests.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "Rendering/CachedTextMeasure.h"
#include "Assets/MeshData.h"
#include "Assets/TextureData.h"
#include "Math/Matrix.h"
#include "Platform/IAudioOutput.h"
#include "Platform/PlatformServices.h"
#include "Rendering/ClipSpace.h"
#include "Rendering/IRenderFrontend.h"
#include "Rendering/QuadDrawGeometry.h"
#include "Rendering/RenderBackendInterfaces.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/SpriteRenderPass.h"
#include "Runtime/Game.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Platform/DirectoryContentSource.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UILayoutSystem.h"
#include "Runtime/SpriteRenderer.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>
    /// 프론트엔드가 래스터화한 이미지를 대신한다. 그래서 텍스트 draw가 플랫폼 폰트 스택을
    /// 테스트로 끌어들이지 않고 프레임 계약을 만족할 수 있다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<const GameEngine::Rendering::RasterizedTextImage> MakeTestTextImage(
        const std::uint64_t id, const unsigned int width = 4, const unsigned int height = 2)
    {
        auto image = std::make_shared<GameEngine::Rendering::RasterizedTextImage>();
        image->id = id;
        image->width = width;
        image->height = height;
        image->alphaPixels.resize(static_cast<std::size_t>(width) * height);
        return image;
    }

    /// <summary>블록 전체를 덮는 글리프 하나다. 텍스트 draw가 계약을 만족하는 최소 형태다.</summary>
    [[nodiscard]] std::shared_ptr<const std::vector<GameEngine::Rendering::TextGlyphQuad>>
    MakeTestGlyphs(const float width = 4.0f, const float height = 2.0f)
    {
        auto glyphs = std::make_shared<std::vector<GameEngine::Rendering::TextGlyphQuad>>();
        GameEngine::Rendering::TextGlyphQuad quad;
        quad.width = width;
        quad.height = height;
        quad.uWidth = 1.0f;
        quad.vHeight = 1.0f;
        glyphs->push_back(quad);
        return glyphs;
    }

    /// <summary>프론트엔드가 프레임에 건네는 방식 그대로 디코딩된 이미지를 만든다.</summary>
    [[nodiscard]] std::shared_ptr<const GameEngine::Assets::TextureData> MakeTestTexture(
        const std::uint64_t id)
    {
        auto texture = std::make_shared<GameEngine::Assets::TextureData>();
        texture->id = id;
        texture->width = 2;
        texture->height = 2;
        texture->pixels.assign(texture->GetByteSize(), std::byte{});
        return texture;
    }

    // payload가 어느 종류인지는 DrawTraits 특성화가 답한다. 특성화가 강제라서, DrawPayload에
    // 대안이 하나 더 생기면 GetDrawPacketKind가 그것을 조용히 Tilemap으로 분류하는 대신
    // 컴파일이 멎는다 — variant를 늘리는 사슬에서 여기가 방화벽이다. 아래 대응은 그 특성화들이
    // 서로 어긋나지 않는지를 컴파일 시점에 확인한다.
    static_assert(
        GameEngine::Rendering::DrawTraits<GameEngine::Rendering::MeshDraw>::Kind ==
        GameEngine::Rendering::DrawPacketKind::Mesh);
    static_assert(
        GameEngine::Rendering::DrawTraits<GameEngine::Rendering::SpriteDraw>::Kind ==
        GameEngine::Rendering::DrawPacketKind::Sprite);
    static_assert(
        GameEngine::Rendering::DrawTraits<GameEngine::Rendering::TextDraw>::Kind ==
        GameEngine::Rendering::DrawPacketKind::Text);
    static_assert(
        GameEngine::Rendering::DrawTraits<GameEngine::Rendering::TilemapDraw>::Kind ==
        GameEngine::Rendering::DrawPacketKind::Tilemap);
    static_assert(
        GameEngine::Rendering::DrawTraits<GameEngine::Rendering::SkinnedMeshDraw>::Kind ==
        GameEngine::Rendering::DrawPacketKind::SkinnedMesh);
    static_assert(
        std::variant_size_v<GameEngine::Rendering::DrawPayload> == 5,
        "DrawPayload gained an alternative; give it a DrawTraits specialization and assert it "
        "here too.");
}

bool RunRenderFrameTests()
{
    GameEngine::Rendering::RenderFrameBuilder builder;
    GameEngine::Rendering::MeshDraw mesh;
    GameEngine::Rendering::SpriteDraw sprite;
    GameEngine::Rendering::TextDraw text;
    // The frame carries decoded pixels, so a test material is data rather than a path.
    const std::shared_ptr<const GameEngine::Assets::TextureData> texture = MakeTestTexture(1);
    const GameEngine::Rendering::MaterialHandle material = builder.AddMaterial({ texture });
    const GameEngine::Rendering::MaterialHandle duplicateMaterial = builder.AddMaterial({ texture });
    const GameEngine::Rendering::PipelineHandle meshPipeline =
        builder.AddPipeline({ GameEngine::Rendering::PipelineKind::Mesh });
    const GameEngine::Rendering::PipelineHandle duplicateMeshPipeline =
        builder.AddPipeline({ GameEngine::Rendering::PipelineKind::Mesh });
    // The frame carries finished vertices, so a test geometry is data rather than a path.
    auto meshData = std::make_shared<GameEngine::Assets::MeshData>();
    meshData->id = 7;
    meshData->vertices.resize(3);
    meshData->indices = { 0, 1, 2 };
    const GameEngine::Rendering::GeometryHandle geometry = builder.AddGeometry({ meshData });
    const GameEngine::Rendering::GeometryHandle duplicateGeometry = builder.AddGeometry({ meshData });
    mesh.pipeline = meshPipeline;
    mesh.geometry = geometry;
    mesh.material = material;
    sprite.pipeline = builder.AddPipeline({ GameEngine::Rendering::PipelineKind::Sprite });
    sprite.material = material;
    text.pipeline = builder.AddPipeline({ GameEngine::Rendering::PipelineKind::Text });
    text.page = MakeTestTextImage(1);
    text.glyphs = MakeTestGlyphs();

    static_cast<void>(builder.TryAddDraw(GameEngine::Rendering::RenderPass::Transparent, sprite, 10, 2));
    static_cast<void>(builder.TryAddDraw(GameEngine::Rendering::RenderPass::Opaque, mesh));
    static_cast<void>(builder.TryAddDraw(GameEngine::Rendering::RenderPass::Transparent, text, 10, 1));
    GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();

    GameEngine::Rendering::RenderFrameBuilder invalidBuilder;
    GameEngine::Rendering::SpriteDraw invalidSprite;
    invalidSprite.pipeline = invalidBuilder.AddPipeline({ GameEngine::Rendering::PipelineKind::Mesh });
    invalidSprite.material = invalidBuilder.AddMaterial({ MakeTestTexture(2) });
    static_cast<void>(invalidBuilder.TryAddDraw(
        GameEngine::Rendering::RenderPass::Transparent, invalidSprite));
    const GameEngine::Rendering::RenderFrame invalidFrame = std::move(invalidBuilder).Build();

    GameEngine::Rendering::RenderFrameBuilder invalidTextBuilder;
    GameEngine::Rendering::TextDraw invalidText;
    invalidText.pipeline = invalidTextBuilder.AddPipeline({ GameEngine::Rendering::PipelineKind::Text });
    invalidText.page = MakeTestTextImage(2);
    invalidText.glyphs = MakeTestGlyphs();
    invalidText.space = static_cast<GameEngine::Rendering::TextSpace>(255);
    static_cast<void>(invalidTextBuilder.TryAddDraw(
        GameEngine::Rendering::RenderPass::Transparent, invalidText));
    const GameEngine::Rendering::RenderFrame invalidTextFrame = std::move(invalidTextBuilder).Build();

    GameEngine::Rendering::RenderFrameBuilder invalidColorBuilder;
    GameEngine::Rendering::SpriteDraw invalidColorSprite;
    invalidColorSprite.pipeline = invalidColorBuilder.AddPipeline({ GameEngine::Rendering::PipelineKind::Sprite });
    invalidColorSprite.material = invalidColorBuilder.AddMaterial({ MakeTestTexture(3) });
    invalidColorSprite.tint.r = std::numeric_limits<float>::infinity();
    static_cast<void>(invalidColorBuilder.TryAddDraw(
        GameEngine::Rendering::RenderPass::Transparent, invalidColorSprite));
    const GameEngine::Rendering::RenderFrame invalidColorFrame = std::move(invalidColorBuilder).Build();

    GameEngine::Rendering::RenderFrameBuilder invalidTransformBuilder;
    GameEngine::Rendering::TextDraw invalidTransformText;
    invalidTransformText.pipeline = invalidTransformBuilder.AddPipeline({ GameEngine::Rendering::PipelineKind::Text });
    invalidTransformText.page = MakeTestTextImage(3);
    invalidTransformText.glyphs = MakeTestGlyphs();
    invalidTransformText.localToWorld = GameEngine::Math::Matrix4x4::CreateScale(
        { std::numeric_limits<float>::infinity(), 1.0f, 1.0f });
    static_cast<void>(invalidTransformBuilder.TryAddDraw(
        GameEngine::Rendering::RenderPass::Transparent, invalidTransformText));
    const GameEngine::Rendering::RenderFrame invalidTransformFrame = std::move(invalidTransformBuilder).Build();

    GameEngine::Rendering::RenderFrameBuilder invalidCameraBuilder;
    GameEngine::Rendering::CameraRenderData invalidCamera;
    invalidCamera.projection = GameEngine::Math::Matrix4x4::CreateScale(
        { 1.0f, std::numeric_limits<float>::infinity(), 1.0f });
    invalidCameraBuilder.SetCamera(invalidCamera);
    const GameEngine::Rendering::RenderFrame invalidCameraFrame = std::move(invalidCameraBuilder).Build();

    struct CountingCommandEncoder final : GameEngine::Rendering::ICommandEncoder
    {
        void BeginPass(GameEngine::Rendering::RenderPass) override { ++beginPassCount; }
        void EndPass(GameEngine::Rendering::RenderPass) override { ++endPassCount; }

        int beginPassCount = 0;
        int endPassCount = 0;
    } commandEncoder;
    struct CountingPacketEncoder final : GameEngine::Rendering::IDrawPacketEncoder
    {
        void Encode(const GameEngine::Rendering::RenderFrame&, const GameEngine::Rendering::DrawPacket&) override
        {
            ++encodeCount;
        }

        int encodeCount = 0;
    } packetEncoder;
    const GameEngine::Rendering::RenderFrameValidationResult dispatchValidation =
        GameEngine::Rendering::PassDispatcher().Dispatch(invalidFrame, commandEncoder, packetEncoder);

    struct TextFrontend final : GameEngine::Rendering::IRenderFrontend
    {
        TextFrontend(const int sortingOrder, const unsigned int instanceId)
            : mSortingOrder(sortingOrder), mInstanceId(instanceId)
        {
        }

        void Collect(
            const GameEngine::Runtime::Game&, GameEngine::Rendering::RenderFrameBuilder& frameBuilder) override
        {
            GameEngine::Rendering::TextDraw draw;
            draw.pipeline = frameBuilder.AddPipeline({ GameEngine::Rendering::PipelineKind::Text });
            draw.page = MakeTestTextImage(mInstanceId);
            draw.glyphs = MakeTestGlyphs();
            static_cast<void>(frameBuilder.TryAddDraw(
                GameEngine::Rendering::RenderPass::Transparent, std::move(draw), mSortingOrder, mInstanceId));
        }

        int mSortingOrder;
        unsigned int mInstanceId;
    } firstFrontend(10, 2), secondFrontend(10, 1);
    GameEngine::Runtime::Game frontendGame{ nullptr, nullptr };
    GameEngine::Rendering::RenderFrameBuilder frontendBuilder;
    firstFrontend.Collect(frontendGame, frontendBuilder);
    secondFrontend.Collect(frontendGame, frontendBuilder);
    const GameEngine::Rendering::RenderFrame frontendFrame = std::move(frontendBuilder).Build();

    // A resolver states its contract through a concept, so its resolved types are ordinary
    // backend structs with no base class and no vtable. This stand-in returns three unrelated
    // types on purpose: the contract is about nullable non-owning pointers, not a shared base.
    struct TestResolvedGeometry final { int vertices = 0; };
    struct TestResolvedMaterial final { int width = 0; };
    struct TestResolvedText final { int glyphs = 0; };
    struct ResultResolver final
    {
        void BeginFrame()
        {
            ++beginFrameCount;
        }

        [[nodiscard]] const TestResolvedGeometry* ResolveGeometry(
            const GameEngine::Rendering::RenderFrame& sourceFrame,
            const GameEngine::Rendering::GeometryHandle handle)
        {
            return sourceFrame.GetGeometry(handle) ? &geometry : nullptr;
        }

        [[nodiscard]] const TestResolvedGeometry* ResolveSkinnedGeometry(
            const GameEngine::Rendering::RenderFrame& sourceFrame,
            const GameEngine::Rendering::SkinnedGeometryHandle handle)
        {
            return sourceFrame.GetSkinnedGeometry(handle) ? &geometry : nullptr;
        }

        [[nodiscard]] const TestResolvedMaterial* ResolveMaterial(
            const GameEngine::Rendering::RenderFrame& sourceFrame,
            const GameEngine::Rendering::MaterialHandle handle)
        {
            return sourceFrame.GetMaterial(handle) ? &material : nullptr;
        }

        [[nodiscard]] const TestResolvedText* ResolveText(
            const GameEngine::Rendering::TextDraw& draw)
        {
            return draw.page ? &text : nullptr;
        }

        TestResolvedGeometry geometry;
        TestResolvedMaterial material;
        TestResolvedText text;
        unsigned int beginFrameCount = 0;
    } resultResolver;
    static_assert(
        GameEngine::Rendering::RenderResourceResolver<ResultResolver>,
        "the resolver contract should accept a backend that shares no base class");
    static_assert(
        !GameEngine::Rendering::RenderResourceResolver<TestResolvedGeometry>,
        "the resolver contract should reject a type that does not resolve anything");

    resultResolver.BeginFrame();
    const TestResolvedGeometry* resolvedGeometry =
        resultResolver.ResolveGeometry(frame, mesh.geometry);
    const TestResolvedMaterial* resolvedMaterial =
        resultResolver.ResolveMaterial(frame, material);
    const TestResolvedText* resolvedText = resultResolver.ResolveText(text);

    const std::vector<const GameEngine::Rendering::MeshDraw*> meshes =
        frame.GetDraws<GameEngine::Rendering::MeshDraw>(
            GameEngine::Rendering::RenderPass::Opaque);
    const std::vector<const GameEngine::Rendering::SpriteDraw*> sprites =
        frame.GetDraws<GameEngine::Rendering::SpriteDraw>(
            GameEngine::Rendering::RenderPass::Transparent);
    const std::vector<const GameEngine::Rendering::TextDraw*> texts =
        frame.GetDraws<GameEngine::Rendering::TextDraw>(
            GameEngine::Rendering::RenderPass::Transparent);

    return Expect(
               frame.GetPipeline(mesh.pipeline)->kind == GameEngine::Rendering::PipelineKind::Mesh &&
               frame.GetPipeline(sprite.pipeline)->kind == GameEngine::Rendering::PipelineKind::Sprite &&
               frame.GetPipeline(text.pipeline)->kind == GameEngine::Rendering::PipelineKind::Text,
               "draw types should resolve their pipeline handles") &&
        Expect(
            frame.GetGeometry(mesh.geometry)->data->id == 7 &&
            frame.GetGeometry(mesh.geometry)->data->indices.size() == 3 &&
            frame.GetMaterial(sprite.material)->baseColorTexture->id == 1,
            "geometry and a material should carry their finished bytes") &&
        Expect(
            material.index == duplicateMaterial.index &&
                meshPipeline.index == duplicateMeshPipeline.index && geometry.index == duplicateGeometry.index,
            "frame tables should intern equal materials, geometries, and pipelines") &&
        Expect(
            resolvedGeometry == &resultResolver.geometry &&
                resolvedMaterial == &resultResolver.material && resolvedText == &resultResolver.text &&
                resultResolver.beginFrameCount == 1 &&
                resultResolver.ResolveGeometry(frame, { 999 }) == nullptr &&
                resultResolver.ResolveMaterial(frame, { 999 }) == nullptr,
            "resource resolvers should return backend-owned typed results and reject invalid handles") &&
        Expect(
            frame.GetDrawPackets(GameEngine::Rendering::RenderPass::Opaque).size() == 1 &&
            frame.GetDrawPackets(GameEngine::Rendering::RenderPass::Transparent).size() == 2,
            "draw packets should be retained in their render-pass queues") &&
        Expect(meshes.size() == 1, "opaque mesh packets should be queryable by type") &&
        Expect(sprites.size() == 1, "transparent sprite packets should be queryable by type") &&
        Expect(
            texts.size() == 1 && texts.front()->page && texts.front()->page->id == 1,
            "transparent text packets should be queryable by type") &&
        Expect(
            std::holds_alternative<GameEngine::Rendering::MeshDraw>(
                frame.GetDrawPackets(GameEngine::Rendering::RenderPass::Opaque).front().payload),
            "opaque packets should sort before transparent packets") &&
        Expect(
            std::holds_alternative<GameEngine::Rendering::TextDraw>(
                frame.GetDrawPackets(GameEngine::Rendering::RenderPass::Transparent).front().payload),
            "transparent packets should use sorting order then instance ID") &&
        Expect(frame.Validate().IsValid(), "valid draw packets should satisfy the render-frame contract") &&
        Expect(
            invalidFrame.Validate().error ==
                GameEngine::Rendering::RenderFrameValidationError::PipelinePayloadMismatch,
            "packet payload types should match their pipeline kinds") &&
        Expect(
            invalidTextFrame.Validate().error ==
                GameEngine::Rendering::RenderFrameValidationError::InvalidDrawParameters,
            "text packets should use supported coordinate spaces and alignments") &&
        Expect(
            invalidColorFrame.Validate().error ==
                GameEngine::Rendering::RenderFrameValidationError::InvalidDrawColor &&
                invalidTransformFrame.Validate().error ==
                GameEngine::Rendering::RenderFrameValidationError::InvalidDrawTransform &&
                invalidCameraFrame.Validate().error ==
                GameEngine::Rendering::RenderFrameValidationError::InvalidCamera,
            "frames should reject non-finite draw colors, transforms, and camera matrices") &&
        Expect(
            dispatchValidation.error ==
                GameEngine::Rendering::RenderFrameValidationError::PipelinePayloadMismatch &&
                commandEncoder.beginPassCount == 0 && commandEncoder.endPassCount == 0 &&
                packetEncoder.encodeCount == 0,
            "the dispatcher should reject invalid frames before invoking backend encoders") &&
        Expect(
            frontendFrame.Validate().IsValid() &&
                frontendFrame.GetDrawPackets(GameEngine::Rendering::RenderPass::Transparent).size() == 2 &&
                frontendFrame.GetDrawPackets(GameEngine::Rendering::RenderPass::Transparent).front().instanceId == 1,
            "multiple render frontends should contribute to one sorted render frame") &&
        Expect(
            !GameEngine::Rendering::RenderFrameBuilder().TryAddDraw(
                static_cast<GameEngine::Rendering::RenderPass>(255), GameEngine::Rendering::SpriteDraw{}),
            "the frame builder should reject unsupported render passes");
}

/// <summary>
/// Camera 컴포넌트가 없는 장면도 프레임에 공용 카메라 상태를 남겨야 한다.
/// 백엔드가 각자 대체 투영을 만들면 같은 장면이 백엔드마다 다르게 렌더링될 수 있다.
/// </summary>
bool RunSceneCameraFallbackTests()
{
    GameEngine::Runtime::Game game{ nullptr, nullptr };
    game.SetRenderAspectRatio(2.0f);

    GameEngine::Rendering::RenderFrameBuilder builder;
    GameEngine::SceneRendering::SceneRenderPass frontend{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
            GameEngine::Platform::PlatformServices::CreateTextRasterizer()) };
    frontend.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();

    const std::optional<GameEngine::Rendering::CameraRenderData>& camera = frame.GetCamera();
    const bool hasFiniteCamera = camera && camera->view.IsFinite() && camera->projection.IsFinite();
    // A scene with no camera has expressed no background, so the fallback clears to black.
    const bool fallbackClearsToBlack = camera &&
        camera->clearColor.r == 0.0f && camera->clearColor.g == 0.0f &&
        camera->clearColor.b == 0.0f && camera->clearColor.a == 1.0f;

    // A 10 world-unit view height at a 2:1 aspect ratio gives a 20-unit width, so the row-vector
    // orthographic projection stores 2/20 and 2/10 on its scale diagonal.
    const bool matchesDefaultProjection = camera &&
        std::abs(camera->projection.GetElement(0, 0) - 0.1f) < 0.0001f &&
        std::abs(camera->projection.GetElement(1, 1) - 0.2f) < 0.0001f;

    GameEngine::Rendering::RenderFrameBuilder providedBuilder;
    GameEngine::Rendering::CameraRenderData providedCamera;
    providedCamera.view = GameEngine::Math::Matrix4x4::Identity();
    providedCamera.projection =
        GameEngine::Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 10.0f);
    providedBuilder.SetCamera(providedCamera);
    frontend.Collect(game, providedBuilder);
    const GameEngine::Rendering::RenderFrame providedFrame = std::move(providedBuilder).Build();
    const bool preservedProvidedCamera = providedFrame.GetCamera() &&
        std::abs(providedFrame.GetCamera()->projection.GetElement(0, 0) - 0.5f) < 0.0001f;

    // The frame describes its own output, so screen-space work never queries the device.
    GameEngine::Rendering::RenderFrameBuilder sizedBuilder;
    sizedBuilder.SetRenderTargetSize({ 1600, 900 });
    frontend.Collect(game, sizedBuilder);
    const GameEngine::Rendering::RenderFrame sizedFrame = std::move(sizedBuilder).Build();
    const bool carriesRenderTargetSize = sizedFrame.GetRenderTargetSize().IsValid() &&
        sizedFrame.GetRenderTargetSize().width == 1600 &&
        std::abs(sizedFrame.GetRenderTargetSize().GetAspectRatio() - 16.0f / 9.0f) < 0.0001f;
    const bool unsetSizeIsInvalid =
        !GameEngine::Rendering::RenderTargetSize{}.IsValid() &&
        GameEngine::Rendering::RenderTargetSize{}.GetAspectRatio() == 1.0f;

    return Expect(
               hasFiniteCamera,
               "a scene without a camera should still produce finite frame camera state") &&
        Expect(
            fallbackClearsToBlack,
            "the fallback camera should clear the frame to black") &&
        Expect(
            carriesRenderTargetSize && unsetSizeIsInvalid,
            "a frame should carry the render-target size it was composed for") &&
        Expect(
            matchesDefaultProjection,
            "the default camera should use the render aspect ratio for its orthographic width") &&
        Expect(
            preservedProvidedCamera,
            "the fallback camera must not overwrite camera state from another frontend") &&
        Expect(
            frame.Validate().IsValid(),
            "a frame carrying only the fallback camera should satisfy the render contract");
}

/// <summary>
/// quad 배치가 각 백엔드의 스프라이트 패스에 살면 두 번 쓰이고 어긋날 자유가 생긴다. 공유되고
/// API 중립이므로, 두 백엔드의 스크린숏을 비교하는 것만이 아니라 여기서 검사할 수 있다.
/// </summary>
bool RunQuadDrawGeometryTests()
{
    using namespace GameEngine::Rendering;
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    constexpr float Tolerance = 0.0001f;

    RenderFrameBuilder builder;
    builder.SetRenderTargetSize({ 800, 600 });
    CameraRenderData camera;
    camera.view = Matrix4x4::CreateTranslation({ -100.0f, -100.0f, 0.0f });
    camera.projection = Matrix4x4::CreateOrthographicLeftHanded(20.0f, 10.0f, 0.1f, 100.0f);
    builder.SetCamera(camera);
    const RenderFrame frame = std::move(builder).Build();

    // Screen-space text uses render-target pixels with the origin at the top-left. The centre
    // of an 800x600 target maps to the centre of clip space, and pixel row 0 maps to the top.
    // A glyph covering its whole block makes the expected quad coordinates explicit.
    TextGlyphQuad blockGlyph;
    blockGlyph.width = 100.0f;
    blockGlyph.height = 50.0f;
    blockGlyph.uWidth = 1.0f;
    blockGlyph.vHeight = 1.0f;

    TextDraw screenText;
    screenText.space = TextSpace::Screen;
    screenText.localToWorld = Matrix4x4::CreateTranslation({ 400.0f, 300.0f, 0.0f });
    const std::optional<TextQuadBasis> centredBasis =
        TryBuildTextQuadBasis(frame, screenText, "Test");
    const Vector3 centre = centredBasis
        ? PlaceTextGlyphQuad(*centredBasis, screenText, blockGlyph)
              .worldViewProjection.TransformPoint({})
        : Vector3(9.0f, 9.0f, 9.0f);

    TextDraw topLeftText = screenText;
    topLeftText.localToWorld = Matrix4x4::CreateTranslation({ 0.0f, 0.0f, 0.0f });
    const std::optional<TextQuadBasis> topLeftBasis =
        TryBuildTextQuadBasis(frame, topLeftText, "Test");
    const Vector3 topLeftCorner = topLeftBasis
        ? PlaceTextGlyphQuad(*topLeftBasis, topLeftText, blockGlyph)
              .worldViewProjection.TransformPoint({})
        : Vector3();

    const bool screenSpaceMapsPixelsToClip =
        std::abs(centre.GetX()) < Tolerance && std::abs(centre.GetY()) < Tolerance &&
        std::abs(topLeftCorner.GetX() + 1.0f) < Tolerance &&
        std::abs(topLeftCorner.GetY() - 1.0f) < Tolerance;

    // The glyph's pixel size scales the quad, so a point one unit along local X lands half the
    // glyph width away: 400 + 100 pixels of the 800-wide target is a quarter of clip width.
    const Vector3 scaledEdge = centredBasis
        ? PlaceTextGlyphQuad(*centredBasis, screenText, blockGlyph)
              .worldViewProjection.TransformPoint({ 1.0f, 0.0f, 0.0f })
        : Vector3();
    const bool textureSizeScalesQuad = std::abs(scaledEdge.GetX() - 0.25f) < Tolerance;

    // A glyph placed above the block centre must land higher on screen, and one placed right
    // must land further right — the sign that turns block-local pixels into screen pixels.
    TextGlyphQuad upperGlyph = blockGlyph;
    upperGlyph.centerY = 10.0f;
    TextGlyphQuad rightGlyph = blockGlyph;
    rightGlyph.centerX = 10.0f;
    const bool glyphOffsetsFollowTheBlock = centredBasis &&
        PlaceTextGlyphQuad(*centredBasis, screenText, upperGlyph)
                .worldViewProjection.TransformPoint({}).GetY() > centre.GetY() &&
        PlaceTextGlyphQuad(*centredBasis, screenText, rightGlyph)
                .worldViewProjection.TransformPoint({}).GetX() > centre.GetX();

    // The glyph's atlas rectangle rides the uv transform: scale first, offset second. The shared
    // quad puts the image's top on its local +y vertex, which the screen projection sends to the
    // bottom of the screen, so screen-space glyphs sample v backwards to stand upright.
    TextGlyphQuad atlasGlyph = blockGlyph;
    atlasGlyph.u = 0.25f;
    atlasGlyph.v = 0.5f;
    atlasGlyph.uWidth = 0.125f;
    atlasGlyph.vHeight = 0.0625f;
    const QuadTransform atlasQuad = centredBasis
        ? PlaceTextGlyphQuad(*centredBasis, screenText, atlasGlyph)
        : QuadTransform{};
    const bool atlasRectangleRidesUvTransform =
        std::abs(atlasQuad.uvTransform[0] - 0.125f) < Tolerance &&
        std::abs(atlasQuad.uvTransform[1] + 0.0625f) < Tolerance &&
        std::abs(atlasQuad.uvTransform[2] - 0.25f) < Tolerance &&
        std::abs(atlasQuad.uvTransform[3] - 0.5625f) < Tolerance;

    // World-space text keeps the forward sampling the shared quad was built for.
    TextDraw worldSpaceText = screenText;
    worldSpaceText.space = TextSpace::World;
    const std::optional<TextQuadBasis> worldBasis =
        TryBuildTextQuadBasis(frame, worldSpaceText, "Test");
    const QuadTransform worldAtlasQuad = worldBasis
        ? PlaceTextGlyphQuad(*worldBasis, worldSpaceText, atlasGlyph)
        : QuadTransform{};
    const bool worldSamplesForward =
        std::abs(worldAtlasQuad.uvTransform[1] - 0.0625f) < Tolerance &&
        std::abs(worldAtlasQuad.uvTransform[3] - 0.5f) < Tolerance;

    // Screen-space text never consults the camera, so changing it must not move the quad.
    RenderFrameBuilder otherCameraBuilder;
    otherCameraBuilder.SetRenderTargetSize({ 800, 600 });
    CameraRenderData otherCamera;
    otherCamera.view = Matrix4x4::CreateTranslation({ 5000.0f, -5000.0f, 0.0f });
    otherCamera.projection = Matrix4x4::CreateOrthographicLeftHanded(3.0f, 3.0f, 0.1f, 10.0f);
    otherCameraBuilder.SetCamera(otherCamera);
    const RenderFrame otherCameraFrame = std::move(otherCameraBuilder).Build();
    const std::optional<TextQuadBasis> movedCameraBasis =
        TryBuildTextQuadBasis(otherCameraFrame, screenText, "Test");
    const Vector3 unmoved = movedCameraBasis
        ? PlaceTextGlyphQuad(*movedCameraBasis, screenText, blockGlyph)
              .worldViewProjection.TransformPoint({})
        : Vector3(9.0f, 9.0f, 9.0f);
    const bool screenSpaceIgnoresCamera =
        std::abs(unmoved.GetX() - centre.GetX()) < Tolerance &&
        std::abs(unmoved.GetY() - centre.GetY()) < Tolerance;

    // A sprite is sized by its texture divided by pixels-per-unit, so a 200x100 texture at 100
    // pixels per unit covers two world units by one.
    SpriteDraw sprite;
    sprite.pixelsPerUnit = 100.0f;
    sprite.localToWorld = Matrix4x4::CreateTranslation({ 100.0f, 100.0f, 0.0f });
    const std::optional<QuadTransform> spriteQuad =
        TryBuildSpriteQuad(frame, sprite, { 200, 100 }, "Test");
    // The view cancels the translation, so local X of one unit lands two world units right of
    // the camera centre, which the 20-unit-wide orthographic projection maps to 0.2 of clip
    // space. A quad sized in pixels rather than world units would land somewhere else entirely.
    const Vector3 spriteEdge = spriteQuad
        ? spriteQuad->worldViewProjection.TransformPoint({ 1.0f, 0.0f, 0.0f })
        : Vector3();
    const bool spriteUsesPixelsPerUnit = std::abs(spriteEdge.GetX() - 0.2f) < Tolerance;

    SpriteDraw flipped = sprite;
    flipped.flipX = true;
    flipped.flipY = true;
    const std::optional<QuadTransform> flippedQuad =
        TryBuildSpriteQuad(frame, flipped, { 200, 100 }, "Test");
    const bool flipsUvs = flippedQuad &&
        flippedQuad->uvTransform[0] == -1.0f && flippedQuad->uvTransform[1] == -1.0f &&
        flippedQuad->uvTransform[2] == 1.0f && flippedQuad->uvTransform[3] == 1.0f &&
        spriteQuad && spriteQuad->uvTransform[0] == 1.0f && spriteQuad->uvTransform[2] == 0.0f;

    // 시트의 한 프레임은 이미지의 일부다: quad는 프레임의 픽셀 크기를 갖고 — 200x100 텍스처의
    // 2x2 격자 한 칸은 100x50 — UV는 그 칸 안에서만 움직인다.
    SpriteDraw framed = sprite;
    framed.uvRect = { 0.5f, 0.5f, 0.5f, 0.5f };
    const std::optional<QuadTransform> framedQuad =
        TryBuildSpriteQuad(frame, framed, { 200, 100 }, "Test");
    const Vector3 framedEdge = framedQuad
        ? framedQuad->worldViewProjection.TransformPoint({ 1.0f, 0.0f, 0.0f })
        : Vector3();
    const bool frameSizesQuad = framedQuad &&
        std::abs(framedEdge.GetX() - 0.1f) < Tolerance &&
        std::abs(framedQuad->uvTransform[0] - 0.5f) < Tolerance &&
        std::abs(framedQuad->uvTransform[1] - 0.5f) < Tolerance &&
        std::abs(framedQuad->uvTransform[2] - 0.5f) < Tolerance &&
        std::abs(framedQuad->uvTransform[3] - 0.5f) < Tolerance;

    // 뒤집기는 프레임 안에서 일어난다: 이웃 칸을 넘겨다보지 않고 같은 칸이 거울처럼 뒤집힌다.
    SpriteDraw framedFlipped = framed;
    framedFlipped.flipX = true;
    const std::optional<QuadTransform> framedFlippedQuad =
        TryBuildSpriteQuad(frame, framedFlipped, { 200, 100 }, "Test");
    const bool flipStaysInsideFrame = framedFlippedQuad &&
        std::abs(framedFlippedQuad->uvTransform[0] + 0.5f) < Tolerance &&
        std::abs(framedFlippedQuad->uvTransform[2] - 1.0f) < Tolerance;

    // The frontend guarantees camera state and a render-target size. A frame that lacks what a
    // draw needs is rejected here rather than papered over with a substitute projection.
    const RenderFrame emptyFrame = RenderFrameBuilder{}.Build();
    const bool rejectsIncompleteFrames =
        !TryBuildSpriteQuad(emptyFrame, sprite, { 200, 100 }, "Test") &&
        !TryBuildTextQuadBasis(emptyFrame, screenText, "Test");

    TextDraw worldText;
    worldText.space = TextSpace::World;
    const bool rejectsWorldTextWithoutCamera =
        !TryBuildTextQuadBasis(emptyFrame, worldText, "Test");

    return Expect(
               screenSpaceMapsPixelsToClip,
               "screen-space text should map render-target pixels from the top-left corner") &&
        Expect(textureSizeScalesQuad, "a quad should be scaled by its texture's pixel size") &&
        Expect(
            glyphOffsetsFollowTheBlock,
            "a glyph above or right of the block centre should land above or right on screen") &&
        Expect(
            atlasRectangleRidesUvTransform,
            "a screen glyph should sample its atlas rectangle with v reversed") &&
        Expect(
            worldSamplesForward,
            "a world glyph should sample its atlas rectangle forward") &&
        Expect(
            screenSpaceIgnoresCamera,
            "screen-space text should not move when the frame's camera changes") &&
        Expect(spriteUsesPixelsPerUnit, "a sprite should be sized by its pixels-per-unit") &&
        Expect(flipsUvs, "flipX and flipY should invert the quad's UV scale and offset") &&
        Expect(frameSizesQuad, "a sheet frame should size the quad and confine its UVs") &&
        Expect(
            flipStaysInsideFrame,
            "flipping a sheet frame should mirror inside that frame") &&
        Expect(
            rejectsIncompleteFrames && rejectsWorldTextWithoutCamera,
            "a frame missing camera state or a render-target size should be rejected");
}

/// <summary>
/// 타일맵의 프레임 계약을 고정한다: 한 레이어가 패킷 하나이고 — 칸이 수백이어도 — 칸 하나의
/// 자리와 UV는 격자 좌표에서 계산된다.
/// </summary>
bool RunTilemapDrawTests()
{
    using namespace GameEngine::Rendering;
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    constexpr float Tolerance = 0.0001f;

    RenderFrameBuilder builder;
    builder.SetRenderTargetSize({ 800, 600 });
    CameraRenderData camera;
    camera.view = GameEngine::Math::Matrix4x4::Identity();
    camera.projection = Matrix4x4::Identity();
    builder.SetCamera(camera);

    auto texture = std::make_shared<GameEngine::Assets::TextureData>();
    texture->id = 77;
    texture->width = 2;
    texture->height = 2;
    texture->pixels.assign(texture->GetByteSize(), std::byte{});

    // 20x20 격자를 가득 채운다: 400칸이 패킷 하나에 실려야 배칭이라 할 수 있다.
    constexpr int Side = 20;
    auto tiles = std::make_shared<std::vector<TilemapTile>>();
    for (int row = 0; row < Side; ++row)
    {
        for (int column = 0; column < Side; ++column)
        {
            TilemapTile tile;
            tile.column = column;
            tile.row = row;
            tile.uv = { 0.5f, 0.5f, 0.5f, 0.5f };
            tiles->push_back(tile);
        }
    }

    TilemapDraw draw;
    draw.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
    draw.material = builder.AddMaterial({ texture });
    draw.cellSize = { 2.0f, 1.0f };
    draw.tiles = tiles;
    static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, draw, 0, 1));
    const RenderFrame frame = std::move(builder).Build();

    const bool oneBatchedPacket =
        frame.GetDrawPackets(RenderPass::Transparent).size() == 1 &&
        frame.GetDraws<TilemapDraw>(RenderPass::Transparent).size() == 1 &&
        frame.GetDraws<TilemapDraw>(RenderPass::Transparent).front()->tiles->size() ==
            static_cast<std::size_t>(Side) * Side &&
        frame.Validate().IsValid();

    // 칸 (1, 2)의 가운데는 원점에서 (1.5칸, 2.5칸)이다 — 원점이 칸 (0,0)의 왼쪽 아래라는 규약.
    const std::optional<Matrix4x4> basis = TryBuildTilemapBasis(frame, draw, "Test");
    TilemapTile placed;
    placed.column = 1;
    placed.row = 2;
    placed.uv = { 0.25f, 0.5f, 0.25f, 0.5f };
    const QuadTransform quad = basis
        ? PlaceTilemapTileQuad(*basis, draw, placed)
        : QuadTransform{};
    const Vector3 center = quad.worldViewProjection.TransformPoint({});
    const bool cellIsPlaced = basis.has_value() &&
        std::abs(center.GetX() - 3.0f) < Tolerance &&
        std::abs(center.GetY() - 2.5f) < Tolerance &&
        std::abs(quad.uvTransform[0] - 0.25f) < Tolerance &&
        std::abs(quad.uvTransform[1] - 0.5f) < Tolerance &&
        std::abs(quad.uvTransform[2] - 0.25f) < Tolerance &&
        std::abs(quad.uvTransform[3] - 0.5f) < Tolerance;

    // 카메라 없는 프레임은 타일맵을 기술할 수 없다 — 스프라이트와 같은 계약이다.
    const RenderFrame emptyFrame = RenderFrameBuilder{}.Build();
    const bool rejectsWithoutCamera = !TryBuildTilemapBasis(emptyFrame, draw, "Test");

    // 빈 목록이나 크기 없는 칸은 프론트엔드 버그이므로 프레임이 거절한다.
    RenderFrameBuilder invalidBuilder;
    invalidBuilder.SetCamera(camera);
    TilemapDraw invalid = draw;
    invalid.pipeline = invalidBuilder.AddPipeline({ PipelineKind::Sprite });
    invalid.material = invalidBuilder.AddMaterial({ texture });
    invalid.tiles = std::make_shared<std::vector<TilemapTile>>();
    static_cast<void>(invalidBuilder.TryAddDraw(RenderPass::Transparent, invalid, 0, 1));
    const RenderFrame invalidFrame = std::move(invalidBuilder).Build();
    const bool rejectsEmptyLayer =
        invalidFrame.Validate().error == RenderFrameValidationError::InvalidDrawParameters;

    return Expect(oneBatchedPacket, "a tilemap layer should be one packet carrying every tile") &&
        Expect(cellIsPlaced, "a tile should sit at its cell and sample its own frame") &&
        Expect(rejectsWithoutCamera, "a tilemap needs the frame's camera state") &&
        Expect(rejectsEmptyLayer, "a tilemap packet with no tiles should not validate");
}

/// <summary>
/// nine-slice 배치는 프레임에 대한 순수 산술이라, 장치 없이 cell을 정확 값으로 검사할 수
/// 있다: 어느 cell이 존재하고, 각각 어디에 놓이며, 어떤 텍셀을 샘플링하는지.
/// </summary>
bool RunSlicedSpriteQuadTests()
{
    using namespace GameEngine::Rendering;
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    const auto isNear = [](const float value, const float expected)
    {
        return std::abs(value - expected) < 0.0001f;
    };

    // Identity view and projection, so a cell's matrix maps the unit quad straight to world
    // units and every position below can be stated literally.
    RenderFrameBuilder builder;
    CameraRenderData camera;
    camera.view = GameEngine::Math::Matrix4x4::Identity();
    camera.projection = Matrix4x4::Identity();
    builder.SetCamera(camera);
    const RenderFrame frame = std::move(builder).Build();

    // A 32x32 texture with an 8-pixel border, stretched to 4x4 world units at 8 pixels per
    // unit: corners are one unit square, the middle is two, and all nine cells exist.
    SpriteDraw sliced;
    sliced.sliced = true;
    sliced.border = { 8.0f, 8.0f, 8.0f, 8.0f };
    sliced.size = { 4.0f, 4.0f };
    sliced.pixelsPerUnit = 8.0f;

    std::array<QuadTransform, 9> quads;
    const std::size_t fullCount = BuildSlicedSpriteQuads(frame, sliced, { 32, 32 }, "Test", quads);

    // Cells come out row-major from the top-left. The first is the top-left corner: one unit
    // square, its outer corner at the sprite's own corner, sampling the first quarter of the
    // texture.
    const Vector3 cornerPoint = quads[0].worldViewProjection.TransformPoint({ -0.5f, 0.5f, 0.0f });
    const bool cornerPlaced = fullCount == 9 &&
        isNear(cornerPoint.GetX(), -2.0f) && isNear(cornerPoint.GetY(), 2.0f) &&
        isNear(quads[0].uvTransform[0], 0.25f) && isNear(quads[0].uvTransform[1], 0.25f) &&
        isNear(quads[0].uvTransform[2], 0.0f) && isNear(quads[0].uvTransform[3], 0.0f);

    // The middle cell fills what the borders leave: two units square, centred, sampling the
    // middle half of the texture.
    const Vector3 middlePoint = quads[4].worldViewProjection.TransformPoint({ -0.5f, 0.5f, 0.0f });
    const bool middleStretched = fullCount == 9 &&
        isNear(middlePoint.GetX(), -1.0f) && isNear(middlePoint.GetY(), 1.0f) &&
        isNear(quads[4].uvTransform[0], 0.5f) && isNear(quads[4].uvTransform[1], 0.5f) &&
        isNear(quads[4].uvTransform[2], 0.25f) && isNear(quads[4].uvTransform[3], 0.25f);

    // When the target is only as tall as the two borders, the middle row has no room and is
    // not emitted rather than emitted with zero height.
    SpriteDraw shallow = sliced;
    shallow.size = { 4.0f, 2.0f };
    const std::size_t shallowCount =
        BuildSlicedSpriteQuads(frame, shallow, { 32, 32 }, "Test", quads);
    const bool degenerateRowSkipped = shallowCount == 6;

    // No border means one cell stretched across the whole extent, sampling the whole texture —
    // which is what a sliced sprite authored without a border should look like.
    SpriteDraw borderless = sliced;
    borderless.border = {};
    const std::size_t borderlessCount =
        BuildSlicedSpriteQuads(frame, borderless, { 32, 32 }, "Test", quads);
    const Vector3 borderlessCorner =
        quads[0].worldViewProjection.TransformPoint({ -0.5f, 0.5f, 0.0f });
    const bool borderlessIsOneQuad = borderlessCount == 1 &&
        isNear(borderlessCorner.GetX(), -2.0f) && isNear(borderlessCorner.GetY(), 2.0f) &&
        isNear(quads[0].uvTransform[0], 1.0f) && isNear(quads[0].uvTransform[1], 1.0f) &&
        isNear(quads[0].uvTransform[2], 0.0f) && isNear(quads[0].uvTransform[3], 0.0f);

    // A flip mirrors the texture, and the border thicknesses travel with their texels: with a
    // 4-pixel left and 8-pixel right border flipped horizontally, the first column is the
    // mirrored right border — one unit wide at 8 pixels per unit — and it samples the last
    // quarter of the texture reversed into the first.
    SpriteDraw flipped = sliced;
    flipped.border = { 4.0f, 0.0f, 8.0f, 0.0f };
    flipped.flipX = true;
    const std::size_t flippedCount =
        BuildSlicedSpriteQuads(frame, flipped, { 32, 32 }, "Test", quads);
    const Vector3 flippedCorner =
        quads[0].worldViewProjection.TransformPoint({ -0.5f, 0.5f, 0.0f });
    const Vector3 flippedInner =
        quads[0].worldViewProjection.TransformPoint({ 0.5f, 0.5f, 0.0f });
    const bool flipSwapsBorders = flippedCount == 3 &&
        isNear(flippedCorner.GetX(), -2.0f) && isNear(flippedInner.GetX(), -1.0f) &&
        isNear(quads[0].uvTransform[0], 0.25f) && isNear(quads[0].uvTransform[2], 0.0f);

    // A border wider than the target extent shrinks proportionally, like a picture frame that
    // keeps its shape by getting thinner; the middle disappears and the two halves meet.
    SpriteDraw crowded = sliced;
    crowded.size = { 1.0f, 4.0f };
    const std::size_t crowdedCount =
        BuildSlicedSpriteQuads(frame, crowded, { 32, 32 }, "Test", quads);
    const Vector3 crowdedInner =
        quads[0].worldViewProjection.TransformPoint({ 0.5f, 0.5f, 0.0f });
    const bool crowdedBordersMeet = crowdedCount == 6 && isNear(crowdedInner.GetX(), 0.0f);

    const RenderFrame cameralessFrame = RenderFrameBuilder{}.Build();
    const bool rejectsMissingCamera =
        BuildSlicedSpriteQuads(cameralessFrame, sliced, { 32, 32 }, "Test", quads) == 0;

    // The frame contract rejects a sliced draw whose numbers cannot place cells at all.
    RenderFrameBuilder invalidBuilder;
    invalidBuilder.SetCamera(camera);
    SpriteDraw invalidSliced;
    invalidSliced.pipeline = invalidBuilder.AddPipeline({ PipelineKind::Sprite });
    invalidSliced.material = invalidBuilder.AddMaterial({ MakeTestTexture(90) });
    invalidSliced.sliced = true;
    invalidSliced.size = { 0.0f, 1.0f };
    static_cast<void>(invalidBuilder.TryAddDraw(RenderPass::Transparent, invalidSliced));
    const bool rejectsZeroSize = !std::move(invalidBuilder).Build().Validate().IsValid();

    RenderFrameBuilder negativeBuilder;
    negativeBuilder.SetCamera(camera);
    SpriteDraw negativeBorder;
    negativeBorder.pipeline = negativeBuilder.AddPipeline({ PipelineKind::Sprite });
    negativeBorder.material = negativeBuilder.AddMaterial({ MakeTestTexture(91) });
    negativeBorder.sliced = true;
    negativeBorder.border = { -1.0f, 0.0f, 0.0f, 0.0f };
    static_cast<void>(negativeBuilder.TryAddDraw(RenderPass::Transparent, negativeBorder));
    const bool rejectsNegativeBorder = !std::move(negativeBuilder).Build().Validate().IsValid();

    return Expect(cornerPlaced, "a corner cell should keep its size and sample its own texels") &&
        Expect(middleStretched, "the middle cell should fill what the borders leave") &&
        Expect(degenerateRowSkipped, "a row the borders leave no room for should not be emitted") &&
        Expect(borderlessIsOneQuad, "a sliced sprite with no border should be one stretched quad") &&
        Expect(flipSwapsBorders, "a flip should carry border thicknesses with their texels") &&
        Expect(crowdedBordersMeet, "borders wider than the extent should shrink to fit") &&
        Expect(rejectsMissingCamera, "a sliced sprite without camera state should be rejected") &&
        Expect(rejectsZeroSize, "a sliced draw with no extent should fail validation") &&
        Expect(rejectsNegativeBorder, "a sliced draw with a negative border should fail validation");
}

/// <summary>
/// 엔진과 다른 클립 공간 규약에 필요한 보정 변환을 장치 없이 확인한다.
/// Direct3D와 Metal처럼 엔진 규약과 일치하는 경우에는 변환이 없어야 하며,
/// 규약이 다른 경우에는 해당 축과 깊이 범위를 정확히 보정해야 한다.
/// </summary>
bool RunClipSpaceTests()
{
    using namespace GameEngine::Rendering;
    using GameEngine::Math::Matrix4x4;
    using GameEngine::Math::Vector3;

    constexpr float Tolerance = 0.0001f;
    constexpr ClipSpaceConvention YDown{
        ClipSpaceYAxis::Down, ClipSpaceDepthRange::ZeroToOne };
    constexpr ClipSpaceConvention WideDepth{
        ClipSpaceYAxis::Up, ClipSpaceDepthRange::MinusOneToOne };

    // A point already in the engine's clip space: +Y up, depth in [0, 1].
    const Vector3 point{ 0.25f, 0.5f, 0.75f };

    const Vector3 unchanged = MakeClipSpaceCorrection(EngineClipSpace).TransformPoint(point);
    const Vector3 flipped = MakeClipSpaceCorrection(YDown).TransformPoint(point);
    const Vector3 widened = MakeClipSpaceCorrection(WideDepth).TransformPoint(point);

    // A backend that agrees must be left alone, and must pay nothing for it.
    Matrix4x4 untouched = Matrix4x4::CreateTranslation({ 3.0f, 4.0f, 5.0f });
    const Matrix4x4 before = untouched;
    ApplyClipSpaceCorrection(untouched, EngineClipSpace);
    bool applyIsIdentityForEngine = true;
    for (std::size_t row = 0; row < 4 && applyIsIdentityForEngine; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (std::fabs(untouched.GetElement(row, column) -
                    before.GetElement(row, column)) > Tolerance)
            {
                applyIsIdentityForEngine = false;
                break;
            }
        }
    }

    Matrix4x4 corrected = Matrix4x4::Identity();
    ApplyClipSpaceCorrection(corrected, YDown);
    const Vector3 correctedPoint = corrected.TransformPoint(point);

    return Expect(
               std::fabs(unchanged.GetX() - point.GetX()) < Tolerance &&
                   std::fabs(unchanged.GetY() - point.GetY()) < Tolerance &&
                   std::fabs(unchanged.GetZ() - point.GetZ()) < Tolerance,
               "the engine's own clip space should need no correction") &&
        Expect(
            std::fabs(flipped.GetX() - point.GetX()) < Tolerance &&
                std::fabs(flipped.GetY() + point.GetY()) < Tolerance &&
                std::fabs(flipped.GetZ() - point.GetZ()) < Tolerance,
            "a clip space with +Y down should invert y and leave depth alone") &&
        Expect(
            std::fabs(widened.GetX() - point.GetX()) < Tolerance &&
                std::fabs(widened.GetY() - point.GetY()) < Tolerance &&
                std::fabs(widened.GetZ() - 0.5f) < Tolerance,
            "a [-1, 1] depth range should map 0.75 to 0.5 and leave y alone") &&
        Expect(applyIsIdentityForEngine, "applying the engine's own convention should be a no-op") &&
        Expect(
            std::fabs(correctedPoint.GetY() + point.GetY()) < Tolerance,
            "applying a correction should reach the same result as the matrix itself");
}

namespace
{
    /// <summary>
    /// 제출 호출만 세는 command list다. 백엔드 없이 "공용 패스가 GPU에 몇 번 말하는가"를 재는
    /// 자리이며, 배칭이 실제로 제출 층까지 내려갔는지는 이 수로만 확인된다 — 프레임이 패킷
    /// 하나로 나른다는 사실은 그 아래에서 몇 번 그리는지 말해 주지 않기 때문이다.
    /// </summary>
    class CountingCommandList final : public GameEngine::Rendering::IRenderCommandList
    {
    public:
        [[nodiscard]] const char* GetBackendName() const override { return "Counting"; }
        [[nodiscard]] GameEngine::Rendering::ClipSpaceConvention GetClipSpace() const override
        {
            return GameEngine::Rendering::EngineClipSpace;
        }
        [[nodiscard]] bool DrawMesh(
            const GameEngine::Rendering::MeshShading&,
            const GameEngine::Rendering::IResolvedGeometry&,
            const GameEngine::Rendering::IResolvedTexture&) override
        {
            ++meshDraws;
            return true;
        }
        [[nodiscard]] bool DrawSkinnedMesh(
            const GameEngine::Rendering::MeshShading&,
            const GameEngine::Rendering::IResolvedGeometry&,
            const GameEngine::Rendering::IResolvedTexture&,
            std::span<const GameEngine::Math::Matrix4x4>) override
        {
            ++skinnedMeshDraws;
            return true;
        }
        [[nodiscard]] bool DrawQuad(
            GameEngine::Rendering::PipelineKind,
            const GameEngine::Rendering::QuadTransform&,
            const GameEngine::Rendering::IResolvedTexture&) override
        {
            ++quadDraws;
            return true;
        }
        [[nodiscard]] bool DrawQuads(
            GameEngine::Rendering::PipelineKind,
            std::span<const GameEngine::Rendering::QuadTransform> transforms,
            const GameEngine::Rendering::IResolvedTexture&) override
        {
            ++quadBatches;
            quadsInBatches += static_cast<int>(transforms.size());
            return true;
        }
        void EndPass(GameEngine::Rendering::RenderPass) override {}

        int meshDraws = 0;
        int skinnedMeshDraws = 0;
        int quadDraws = 0;
        int quadBatches = 0;
        int quadsInBatches = 0;
    };

    /// <summary>세는 command list가 받아들이는 텍스처다. 픽셀 크기만 답하면 된다.</summary>
    class CountingTexture final : public GameEngine::Rendering::IResolvedTexture
    {
    public:
        [[nodiscard]] bool IsDrawable() const override { return true; }
        [[nodiscard]] const char* GetBackendName() const override { return "Counting"; }
        [[nodiscard]] unsigned int GetPixelWidth() const override { return 4; }
        [[nodiscard]] unsigned int GetPixelHeight() const override { return 4; }
    };
}

bool RunTilemapSubmissionTests()
{
    using namespace GameEngine::Rendering;

    RenderFrameBuilder builder;
    builder.SetRenderTargetSize({ 320, 240 });
    CameraRenderData camera;
    camera.view = GameEngine::Math::Matrix4x4::Identity();
    camera.projection =
        GameEngine::Math::Matrix4x4::CreateOrthographicLeftHanded(40.0f, 30.0f, 0.1f, 100.0f);
    builder.SetCamera(camera);

    auto texture = std::make_shared<GameEngine::Assets::TextureData>();
    texture->id = 91;
    texture->width = 4;
    texture->height = 4;
    texture->pixels.assign(texture->GetByteSize(), std::byte{});

    // 20x20을 가득 채운 레이어 하나. 프레임은 이것을 패킷 하나로 나른다.
    constexpr int Side = 20;
    auto tiles = std::make_shared<std::vector<TilemapTile>>();
    for (int row = 0; row < Side; ++row)
    {
        for (int column = 0; column < Side; ++column)
        {
            TilemapTile tile;
            tile.column = column;
            tile.row = row;
            tile.uv = { 0.0f, 0.0f, 1.0f, 1.0f };
            tiles->push_back(tile);
        }
    }

    TilemapDraw draw;
    draw.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
    draw.material = builder.AddMaterial({ texture });
    draw.cellSize = { 1.0f, 1.0f };
    draw.tiles = tiles;
    static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, draw, 0, 1));
    const RenderFrame frame = std::move(builder).Build();

    CountingCommandList commandList;
    CountingTexture tileset;
    const SpriteRenderPass pass;
    pass.DrawTilemap(
        commandList, frame, *frame.GetDraws<TilemapDraw>(RenderPass::Transparent).front(), tileset);

    // 프레임이 패킷 하나로 나른 레이어는 제출에서도 한 번이어야 한다. 타일마다 호출이 흩어지면
    // 그 수가 백엔드의 프레임 상수 예산을 말없이 넘겨 한쪽만 도중에 잘리고, 같은 프레임이 두
    // 백엔드에서 다른 픽셀이 된다.
    return Expect(
        commandList.quadBatches == 1 && commandList.quadDraws == 0,
        "a tilemap layer should reach the backend as one batch") &&
        Expect(
            commandList.quadsInBatches == Side * Side,
            "the batch should carry every tile of the layer");
}

/// <summary>
/// 툴바 형태의 사각형들을 배율 2.0에서 세우고 라벨이 버튼의 세로 가운데에 놓이는지 확인한다.
/// 경계가 화면에서 뚜렷하지 않은 버튼도 배치된 사각형과 글리프 좌표로 검사할 수 있다.
/// </summary>
bool RunToolbarLabelCentringTests()
{
    using namespace GameEngine::Runtime;
    using GameEngine::Math::Vector2;

    // 툴바가 쓰는 값 그대로다.
    constexpr float ToolbarHeight = 32.0f;
    constexpr float ButtonInset = 4.0f;
    constexpr float LabelLeftInset = 8.0f;
    constexpr float LabelFontSize = 13.0f;
    constexpr float ContentScale = 2.0f;

    GameEngine::Runtime::Game game{ nullptr, nullptr };
    game.SetRenderSurfaceSize(2880.0f, 1800.0f);

    auto scene = std::make_unique<Scene>(game.GetRuntimeContext(), "Toolbar");
    GameObject* const canvasObject = scene->CreateGameObject("EditorUI");
    Canvas* const canvas = canvasObject->AddComponent<Canvas>();
    canvas->SetScaleFactor(ContentScale);

    const auto addRect = [&scene](GameObject& parent, const char* const name,
        const Vector2& anchorMin, const Vector2& anchorMax, const Vector2& offsetMin,
        const Vector2& offsetMax)
    {
        GameObject* const object = scene->CreateGameObject(name);
        static_cast<void>(object->GetTransform().SetParent(&parent.GetTransform()));
        RectTransform* const rect = object->AddComponent<RectTransform>();
        rect->SetAnchorMin(anchorMin);
        rect->SetAnchorMax(anchorMax);
        rect->SetOffsetMin(offsetMin);
        rect->SetOffsetMax(offsetMax);
        return object;
    };

    GameObject* const strip = addRect(*canvasObject, "Toolbar", { 0.0f, 0.0f }, { 1.0f, 0.0f },
        { 0.0f, 0.0f }, { 0.0f, ToolbarHeight });
    GameObject* const group = addRect(*strip, "ToolbarButtons", { 0.0f, 0.0f }, { 1.0f, 1.0f },
        { 0.0f, 0.0f }, { 0.0f, 0.0f });
    // "New Project": x=4 폭 96.
    GameObject* const button = addRect(*group, "New Project", { 0.0f, 0.0f }, { 0.0f, 0.0f },
        { ButtonInset, ButtonInset }, { ButtonInset + 96.0f, ToolbarHeight - ButtonInset });
    GameObject* const labelObject = addRect(*button, "New Project Label", { 0.0f, 0.0f },
        { 1.0f, 1.0f }, { LabelLeftInset, 0.0f }, { 0.0f, 0.0f });
    TextRenderer* const label = labelObject->AddComponent<TextRenderer>();
    label->SetText("New Project");
    label->SetSpace(TextRenderer::Space::Screen);
    label->SetAlignment(TextRenderer::Alignment::Left);
    label->SetVerticalAlignment(TextRenderer::VerticalAlignment::Middle);

    const unsigned int sceneId = game.GetSceneManager().AddScene(std::move(scene));
    const UILayoutSystem layout;
    layout.Synchronize(game.GetSceneManager(), 2880.0f, 1800.0f);

    // 글꼴 크기는 논리 단위로 선언하고, 줄바꿈 폭은 배치가 푼 사각형에서 얻는다.
    RectTransform* const labelRect = labelObject->GetComponent<RectTransform>();
    // 글꼴 크기는 논리 단위로 선언한다. 배율은 이 프레임을 모으는 쪽이 곱하므로, 여기서 미리
    // 곱하면 그리는 글자가 두 배가 된다.
    label->SetFontSize(LabelFontSize);
    label->SetMaxWidth(labelRect->GetResolvedRect().width);

    const RectTransform::Rect buttonRect =
        button->GetComponent<RectTransform>()->GetResolvedRect();
    const RectTransform::Rect textRect = labelRect->GetResolvedRect();

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  toolbar label centring tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::Rendering::RenderFrameBuilder builder;
    GameEngine::SceneRendering::SceneRenderPass frontend{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)) };
    frontend.Collect(game, builder);
    const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();
    const std::vector<const GameEngine::Rendering::TextDraw*> draws =
        frame.GetDraws<GameEngine::Rendering::TextDraw>(GameEngine::Rendering::RenderPass::Overlay);
    const GameEngine::Math::Vector3 placed = !draws.empty() && draws.front()
        ? draws.front()->localToWorld.GetTranslation()
        : GameEngine::Math::Vector3{ -1.0f, -1.0f, -1.0f };

    std::cerr << "  toolbar replica: button y=" << buttonRect.y << " h=" << buttonRect.height
        << "  label rect x=" << textRect.x << " w=" << textRect.width
        << "  text drawn at " << placed.GetX() << ',' << placed.GetY() << '\n';

    // 배율 2.0에서 버튼은 y=8 높이 48이고, 라벨 사각형은 x=8+16=24에서 시작한다.
    const bool geometryScaled = std::abs(buttonRect.y - 8.0f) < 0.01f &&
        std::abs(buttonRect.height - 48.0f) < 0.01f &&
        std::abs(textRect.x - 24.0f) < 0.01f;

    // 변환만으로는 글자가 어디 그려지는지 알 수 없다. 글리프 quad는 블록의 <b>중심</b>을 기준으로
    // 만들어지므로(TextRasterizationCache가 halfWidth/halfHeight를 빼고 더한다), 실제 자리는
    // 변환에 그 좌표를 더한 값이다. 사각형과 맞대야 할 것은 그 결과다.
    float inkLeft = 1.0e9f;
    float inkRight = -1.0e9f;
    float inkTop = 1.0e9f;
    float inkBottom = -1.0e9f;
    for (const GameEngine::Rendering::TextDraw* const draw : draws)
    {
        if (!draw || !draw->glyphs)
        {
            continue;
        }
        const GameEngine::Math::Vector3 origin = draw->localToWorld.GetTranslation();
        for (const GameEngine::Rendering::TextGlyphQuad& glyph : *draw->glyphs)
        {
            const float centreX = origin.GetX() + glyph.centerX;
            // quad의 Y는 위가 양수다. 화면 투영은 위가 원점이므로 부호가 뒤집힌다.
            const float centreY = origin.GetY() - glyph.centerY;
            inkLeft = (std::min)(inkLeft, centreX - glyph.width * 0.5f);
            inkRight = (std::max)(inkRight, centreX + glyph.width * 0.5f);
            inkTop = (std::min)(inkTop, centreY - glyph.height * 0.5f);
            inkBottom = (std::max)(inkBottom, centreY + glyph.height * 0.5f);
        }
    }
    std::cerr << "  glyph ink box = " << inkLeft << ',' << inkTop << " .. " << inkRight << ','
        << inkBottom << "   button = " << buttonRect.x << ',' << buttonRect.y << " .. "
        << (buttonRect.x + buttonRect.width) << ',' << (buttonRect.y + buttonRect.height) << '\n';

    // 가로도 잉크로 잰다. 글자가 시작하는 자리는 사각형의 왼쪽 변이며, 글꼴의 좌측 베어링만큼
    // 안쪽에서 시작한다 — 그래서 "같다"가 아니라 "그 변에서 몇 픽셀 안"이 옳은 기대다.
    const bool startsAtRect = inkLeft >= textRect.x - 0.5f && inkLeft <= textRect.x + 8.0f;
    const bool insideButton = placed.GetY() >= buttonRect.y - 0.01f &&
        placed.GetY() <= buttonRect.y + buttonRect.height;

    // 글자는 자기 버튼 안에 있어야 한다. 변환이 옳아도 기준점이 어긋나면 여기서 드러난다.
    const bool inkInsideButton = inkLeft >= buttonRect.x - 0.5f &&
        inkRight <= buttonRect.x + buttonRect.width + 0.5f &&
        inkTop >= buttonRect.y - 0.5f &&
        inkBottom <= buttonRect.y + buttonRect.height + 0.5f;

    return Expect(sceneId != 0, "the toolbar replica should assemble") &&
        Expect(geometryScaled, "the canvas scale should reach the toolbar's rects") &&
        Expect(startsAtRect, "the label should start at its rect's left edge") &&
        Expect(insideButton, "the label should be placed inside its own button") &&
        Expect(inkInsideButton, "the label's glyphs should land inside its own button");
}

/// <summary>
/// 화면 공간 텍스트가 자기 사각형 안 어디에 놓이는지 확인한다: 기본은 좌상단이고, 세로 정렬을
/// 주면 배치된 글자 블록의 높이만큼 내려온다. 프레임에 실린 변환은 창 없이 읽을 수 있으므로
/// 배치에서 draw로 넘어가는 이 한 걸음을 여기서 답한다.
///
/// 세로 정렬은 글꼴이 정하는 블록 높이에 기대므로 자리를 픽셀로 못박지 않는다. 대신 규칙 자체를
/// 잰다: 가운데는 남는 높이의 절반, 아래는 그 전부다. 글꼴이 바뀌어도 이 관계는 남는다.
/// </summary>
bool RunScreenSpaceTextPlacementTests()
{
    using namespace GameEngine::Runtime;
    using GameEngine::Math::Vector2;

    GameEngine::Runtime::Game game{ nullptr, nullptr };
    game.SetRenderSurfaceSize(800.0f, 600.0f);

    // 장면은 이 게임의 런타임 컨텍스트 위에 세운다 — 객체가 등록되는 레지스트리가 게임의
    // 것이어야 게임이 그 장면을 자기 것으로 다룬다.
    auto scene = std::make_unique<Scene>(game.GetRuntimeContext(), "ScreenText");
    GameObject* const canvasObject = scene->CreateGameObject("Canvas");
    static_cast<void>(canvasObject->AddComponent<Canvas>());

    // 툴바의 라벨과 같은 모양이다: 한 점 앵커에 픽셀 오프셋, 화면 공간 텍스트.
    GameObject* const labelObject = scene->CreateGameObject("Label");
    static_cast<void>(labelObject->GetTransform().SetParent(&canvasObject->GetTransform()));
    RectTransform* const rect = labelObject->AddComponent<RectTransform>();
    rect->SetAnchorMin({ 0.0f, 0.0f });
    rect->SetAnchorMax({ 0.0f, 0.0f });
    rect->SetOffsetMin({ 120.0f, 40.0f });
    rect->SetOffsetMax({ 320.0f, 72.0f });
    TextRenderer* const text = labelObject->AddComponent<TextRenderer>();
    text->SetText("Toolbar");
    text->SetSpace(TextRenderer::Space::Screen);
    text->SetFontSize(20.0f);

    const unsigned int sceneId = game.GetSceneManager().AddScene(std::move(scene));
    const UILayoutSystem layout;
    layout.Synchronize(game.GetSceneManager(), 800.0f, 600.0f);
    const RectTransform::Rect resolved = rect->GetResolvedRect();

    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout
            << "  screen-space text placement tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::SceneRendering::SceneRenderPass frontend{
        std::make_shared<GameEngine::Rendering::TextRasterizationCache>(std::move(rasterizer)) };

    // 프레임에 실린 텍스트 draw의 변환이 곧 그 글자가 시작할 자리다.
    const auto placeWith = [&game, &frontend](
        const TextRenderer::VerticalAlignment alignment, TextRenderer& renderer)
    {
        renderer.SetVerticalAlignment(alignment);
        GameEngine::Rendering::RenderFrameBuilder builder;
        frontend.Collect(game, builder);
        const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();
        // 화면 공간 텍스트는 오버레이로 실린다.
        const std::vector<const GameEngine::Rendering::TextDraw*> draws =
            frame.GetDraws<GameEngine::Rendering::TextDraw>(
                GameEngine::Rendering::RenderPass::Overlay);
        if (!draws.empty() && draws.front())
        {
            return draws.front()->localToWorld.GetTranslation();
        }
        return GameEngine::Math::Vector3{ -1.0f, -1.0f, -1.0f };
    };

    const GameEngine::Math::Vector3 placed =
        placeWith(TextRenderer::VerticalAlignment::Top, *text);
    const GameEngine::Math::Vector3 centered =
        placeWith(TextRenderer::VerticalAlignment::Middle, *text);
    const GameEngine::Math::Vector3 bottom =
        placeWith(TextRenderer::VerticalAlignment::Bottom, *text);

    // 변환은 블록의 중심을 가리키므로 그 값을 사각형과 직접 비교하면 안 된다. 비교할 것은
    // 글리프가 실제로 덮는 상자다 — 변환이 사각형의 왼쪽 위와 같기를 요구하면, 그것이 글자를
    // 폭의 절반만큼 밀어내는 계약이 된다.
    const auto inkBoxFor = [&game, &frontend](const TextRenderer::VerticalAlignment alignment,
        TextRenderer& renderer)
    {
        renderer.SetVerticalAlignment(alignment);
        GameEngine::Rendering::RenderFrameBuilder builder;
        frontend.Collect(game, builder);
        const GameEngine::Rendering::RenderFrame frame = std::move(builder).Build();
        float left = 1.0e9f;
        float top = 1.0e9f;
        for (const GameEngine::Rendering::TextDraw* const draw :
             frame.GetDraws<GameEngine::Rendering::TextDraw>(
                 GameEngine::Rendering::RenderPass::Overlay))
        {
            if (!draw || !draw->glyphs)
            {
                continue;
            }
            const GameEngine::Math::Vector3 origin = draw->localToWorld.GetTranslation();
            for (const GameEngine::Rendering::TextGlyphQuad& glyph : *draw->glyphs)
            {
                left = (std::min)(left, origin.GetX() + glyph.centerX - glyph.width * 0.5f);
                top = (std::min)(top, origin.GetY() - glyph.centerY - glyph.height * 0.5f);
            }
        }
        return GameEngine::Math::Vector2{ left, top };
    };
    const GameEngine::Math::Vector2 topInk =
        inkBoxFor(TextRenderer::VerticalAlignment::Top, *text);
    std::cerr << "  screen-space label rect [" << resolved.x << ", " << resolved.y << "] to ["
              << (resolved.x + resolved.width) << ", " << (resolved.y + resolved.height)
              << "], top ink at (" << topInk.GetX() << ", " << topInk.GetY() << ")\n";

    const bool sceneAssembled = sceneId != 0 && resolved.width > 0.0f;
    // Top 정렬이면 글자 블록의 왼쪽 위가 사각형의 왼쪽 위다. 잉크는 그 안쪽에서 시작한다 —
    // 글꼴의 여백만큼이며, 블록 밖으로 나가지는 않는다. 여유를 1.5px 두는 이유는 우리
    // 래스터라이저가 힌팅을 하지 않기 때문이다: 윤곽선이 픽셀 격자의 어느 쪽에 걸리느냐에 따라
    // 반올림이 잉크를 진행 상자보다 1px 앞으로 내보낼 수 있다. 그 밖의 어긋남은 여기서 잡으려는
    // 결함(가운데 정렬처럼 절반 폭이 밀리는 것)에 비하면 훨씬 작다.
    constexpr float UnhintedJitter = 1.5f;
    const bool placedAtRect = topInk.GetX() >= resolved.x - UnhintedJitter &&
        topInk.GetX() <= resolved.x + resolved.width &&
        topInk.GetY() >= resolved.y - UnhintedJitter &&
        topInk.GetY() <= resolved.y + resolved.height;

    // 세로 정렬은 가로 자리를 건드리지 않는다. 비교 대상은 사각형의 변이 아니라 서로다 —
    // 변환은 블록의 중심을 가리키므로 사각형의 왼쪽 변과 같을 이유가 없다.
    const bool keptColumn = std::abs(centered.GetX() - placed.GetX()) < 0.01f &&
        std::abs(bottom.GetX() - placed.GetX()) < 0.01f;

    // 가운데는 남는 높이의 절반, 아래는 그 전부다 — 그 관계가 곧 규칙이다. 기준은 사각형의
    // 위쪽 변이 아니라 Top 정렬이 놓은 자리다: 변환은 블록의 중심을 가리키므로 세 정렬 모두
    // 블록 높이의 절반을 공통으로 갖고 있고, 그 공통항을 빼야 남는 높이만 남는다.
    const float centeredDrop = centered.GetY() - placed.GetY();
    const float bottomDrop = bottom.GetY() - placed.GetY();
    const bool centeredHalfway = centeredDrop > 0.0f &&
        centeredDrop < resolved.height * 0.5f &&
        std::abs(bottomDrop - centeredDrop * 2.0f) < 0.01f;

    return Expect(sceneAssembled, "the screen-space text scene should assemble and lay out") &&
        Expect(
            placedAtRect,
            "screen-space text should be placed within the rect it belongs to") &&
        Expect(keptColumn, "vertical alignment should not move the text sideways") &&
        Expect(
            centeredHalfway,
            "centered text should drop half of what bottom-aligned text drops");
}


/// <summary>
/// 화면 공간 9-슬라이스의 모서리가 화면 배율을 탄다.
///
/// 사각형은 배치가 배율을 곱해 넘겨 주므로, 모서리만 원본 텍스처 픽셀로 남으면 배율이 오를수록
/// 테두리가 상대적으로 얇아진다 — 200%에서 모서리가 절반으로 보이는 것이 그것이다. 프레임에
/// 실리는 테두리가 사각형과 같은 단위라야 그림이 배율에 따라 같은 모양으로 커진다.
/// </summary>
bool RunSlicedBorderScaleTests()
{
    using namespace GameEngine;

    // 1×1 RGBA PNG다. 이 시험이 재는 것은 테두리 수치뿐이라 픽셀이 무엇인지는 상관없지만,
    // 스프라이트가 텍스처를 얻지 못하면 draw 자체가 실리지 않으므로 진짜 이미지여야 한다.
    static constexpr unsigned char OnePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
    constexpr float AuthoredBorder = 8.0f;
    constexpr float ContentScale = 2.0f;

    TestSupport::TemporaryDirectory projectDirectory("sliced-border-scale");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        TestSupport::WriteFile(root / "Sliced.gameproject", "{}") &&
        TestSupport::WriteFile(root / "Sprites" / "panel.png",
            std::string_view(
                reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
        TestSupport::WriteFile(root / "Sprites" / "panel.png.meta",
            R"({"format": "gameengine-meta/1", "pixelsPerUnit": 16.0,)"
            R"( "border": [8.0, 8.0, 8.0, 8.0]})");
    if (!Expect(wrote, "the sliced-border test project should be written"))
    {
        return false;
    }

    // 테두리와 그 배수를 함께 꺼낸다. 둘이 한 draw의 두 필드이므로 따로 읽으면 서로 다른
    // 프레임의 값을 볼 수 있다.
    struct SlicedFacts
    {
        Rendering::SpriteBorder border{ -1.0f, -1.0f, -1.0f, -1.0f };
        float scale = -1.0f;
    };
    const auto slicedFactsAtScale = [&root](const float scale) -> SlicedFacts
    {
        const Platform::DirectoryContentSource content(root);
        Runtime::Game game{ nullptr, nullptr };
        if (!game.Initialize(content, {}))
        {
            return SlicedFacts{};
        }
        game.SetRenderSurfaceSize(800.0f, 600.0f);

        auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Sliced");
        Runtime::GameObject* const canvasObject = scene->CreateGameObject("Canvas");
        Runtime::Canvas* const canvas = canvasObject->AddComponent<Runtime::Canvas>();
        canvas->SetScaleFactor(scale);

        Runtime::GameObject* const panel = scene->CreateGameObject("Panel");
        static_cast<void>(panel->GetTransform().SetParent(&canvasObject->GetTransform()));
        Runtime::RectTransform* const rect = panel->AddComponent<Runtime::RectTransform>();
        rect->SetAnchorMin({ 0.0f, 0.0f });
        rect->SetAnchorMax({ 0.0f, 0.0f });
        rect->SetOffsetMin({ 0.0f, 0.0f });
        rect->SetOffsetMax({ 120.0f, 60.0f });
        Runtime::SpriteRenderer* const renderer = panel->AddComponent<Runtime::SpriteRenderer>();
        renderer->SetSprite(Assets::AssetReference::Parse("Sprites/panel.png"));
        renderer->SetDrawMode(Runtime::SpriteRenderer::DrawMode::Sliced);
        renderer->SetSpace(Runtime::SpriteRenderer::Space::Screen);

        static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
        const Runtime::UILayoutSystem layout;
        layout.Synchronize(game.GetSceneManager(), 800.0f, 600.0f);

        Rendering::RenderFrameBuilder builder;
        SceneRendering::SceneRenderPass frontend{
            std::make_shared<Rendering::TextRasterizationCache>(
                Platform::PlatformServices::CreateTextRasterizer()) };
        frontend.Collect(game, builder);
        const Rendering::RenderFrame frame = std::move(builder).Build();
        for (const Rendering::SpriteDraw* const draw :
             frame.GetDraws<Rendering::SpriteDraw>(Rendering::RenderPass::Overlay))
        {
            if (draw && draw->sliced)
            {
                return SlicedFacts{ draw->border, draw->borderScale };
            }
        }
        return SlicedFacts{};
    };

    const SlicedFacts atOne = slicedFactsAtScale(1.0f);
    const SlicedFacts atTwo = slicedFactsAtScale(ContentScale);

    bool passed = Expect(
        atOne.border.left == AuthoredBorder && atOne.border.top == AuthoredBorder &&
            atOne.border.right == AuthoredBorder && atOne.border.bottom == AuthoredBorder,
        "at 100% the frame carries the border the sprite declares");
    // 200%에서는 테두리 값 자체가 아니라 그 배수가 두 배가 된다. 프레임이 나르는 border는
    // 텍스처 픽셀이라 그림이 선언한 값 그대로여야 하고 — 조각이 그림의 어느 부분을 읽는지가
    // 거기서 나온다 — 화면에서 두 배로 그리라는 말은 borderScale이 한다.
    passed &= Expect(
        atTwo.border.left == AuthoredBorder && atTwo.border.top == AuthoredBorder &&
            atTwo.border.right == AuthoredBorder && atTwo.border.bottom == AuthoredBorder,
        "at 200% the border stays the texture pixels the sprite declares");
    passed &= Expect(
        atOne.scale == 1.0f && atTwo.scale == ContentScale,
        "and the scale to draw those pixels at rides beside it");

    std::cerr << "  sliced border: scale 1 -> " << atOne.border.left << ", scale 2 -> " << atTwo.border.left
              << " (authored " << AuthoredBorder << ")\n";
    return passed;
}

static const TestSupport::Registration gRenderFrameTests{
    "RenderFrame", "draw packet tests should pass", RunRenderFrameTests };

static const TestSupport::Registration gSceneCameraFallbackTests{
    "RenderFrame", "scene camera fallback tests should pass", RunSceneCameraFallbackTests };

static const TestSupport::Registration gScreenSpaceTextPlacementTests{
    "RenderFrame", "screen-space text placement tests should pass", RunScreenSpaceTextPlacementTests };

static const TestSupport::Registration gToolbarLabelCentringTests{
    "RenderFrame", "toolbar label centring tests should pass", RunToolbarLabelCentringTests };

static const TestSupport::Registration gQuadDrawGeometryTests{
    "RenderFrame", "quad draw geometry tests should pass", RunQuadDrawGeometryTests };

static const TestSupport::Registration gSlicedSpriteQuadTests{
    "RenderFrame", "sliced sprite quad tests should pass", RunSlicedSpriteQuadTests };

static const TestSupport::Registration gSlicedBorderScaleTests{
    "RenderFrame", "sliced border scale tests should pass", RunSlicedBorderScaleTests };

static const TestSupport::Registration gTilemapDrawTests{
    "RenderFrame", "tilemap draw tests should pass", RunTilemapDrawTests };

static const TestSupport::Registration gTilemapSubmissionTests{
    "RenderFrame", "tilemap submission tests should pass", RunTilemapSubmissionTests };

static const TestSupport::Registration gClipSpaceTests{
    "RenderFrame", "clip space tests should pass", RunClipSpaceTests };
