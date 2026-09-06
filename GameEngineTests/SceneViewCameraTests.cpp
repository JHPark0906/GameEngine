#include "SceneViewCameraTests.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "BackendPixelSupport.h"
#include "Document/EditorContext.h"
#include "Math/ViewportProjection.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rules/EditorPanelHosts.h"
#include "Rules/EditorSceneTool.h"
#include "Runtime/Camera.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/Scene.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TilemapRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"
#include "UI/UIContext.h"
#include "Views/EditorSceneViewPanel.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    using namespace GameEngine;
    using TestSupport::Expect;

    constexpr int MapExtent = 256;
    constexpr int TargetColumn = 220;
    constexpr int TargetRow = 220;
    constexpr Math::Vector3 TileCenter{ 220.5f, 220.5f, 0.0f };
    constexpr Math::Vector3 SpriteCenter{ 220.5f, 222.5f, 0.0f };
    constexpr TestSupport::Rgba Green{ 0, 255, 0, 255 };
    constexpr TestSupport::Rgba Red{ 255, 0, 0, 255 };
    constexpr TestSupport::Rgba Blue{ 0, 0, 255, 255 };
    constexpr unsigned char OnePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D, 0x49, 0x48,
        0x44, 0x52, 0, 0, 0, 1, 0, 0, 0, 1, 0x08, 0x06, 0, 0, 0, 0x1F, 0x15, 0xC4,
        0x89, 0, 0, 0, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0x0F,
        0x04, 0, 0x09, 0xFB, 0x03, 0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0, 0, 0, 0, 0x49,
        0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

    class ScaleHost final : public GameEditor::IEditorScale
    {
    public:
        explicit ScaleHost(const float scale) : mScale(scale) {}
        [[nodiscard]] float S(const float logical) const override { return logical * mScale; }
    private:
        float mScale;
    };

    class PropertyEditHost final : public GameEditor::IPropertyEditHost
    {
    public:
        void ApplyProperty(Runtime::Component&, const Runtime::PropertyDescriptor&,
            const Runtime::PropertyValue&, std::uint64_t) override {}
        [[nodiscard]] std::uint64_t MakeMergeKey(UI::WidgetId) const override { return 0; }
        void ResetFieldEditingState() override {}
        void PerformUndo() override {}
        void PerformRedo() override {}
    };

    class EmptySceneTool final : public GameEditor::ISceneTool
    {
    public:
        [[nodiscard]] GameEditor::SceneInputCapture HandleSceneInput(
            const GameEditor::SceneToolInput&) override { return {}; }
    };

    class SceneToolHost final : public GameEditor::ISceneToolHost
    {
    public:
        [[nodiscard]] GameEditor::ISceneTool& GetSceneTool() override { return mTool; }
    private:
        EmptySceneTool mTool;
    };

    struct CameraCase
    {
        const char* name;
        Math::Vector3 pivot;
        float distance;
        float yaw;
        float pitch;
        unsigned int width;
        unsigned int height;
    };

    constexpr std::array CameraCases{
        CameraCase{ "wide", { 220.5f, 221.5f, 0 }, 8, 0, 0, 640, 400 },
        CameraCase{ "pan", { 221.5f, 221.5f, 0 }, 8, 0, 0, 640, 400 },
        CameraCase{ "zoom", { 220.5f, 221.5f, 0 }, 5, 0, 0, 640, 400 },
        CameraCase{ "portrait", { 220.5f, 221.5f, 0 }, 8, 0, 0, 400, 640 },
        CameraCase{ "orbit", { 220.5f, 221.5f, 0 }, 8, 20, 15, 640, 400 } };

    [[nodiscard]] bool WriteProject(const std::filesystem::path& root)
    {
        return TestSupport::WriteFile(root / "Camera.gameproject",
                   R"({"projectName":"Camera","window":{"width":640,"height":400},)"
                   R"("initialSceneId":0,"scenes":[{"id":0,"path":"Scenes/Camera.scene"}]})") &&
            TestSupport::WriteFile(root / "Scenes/Camera.scene",
                R"({"sceneName":"Camera","gameObjects":[]})") &&
            TestSupport::WriteFile(root / "white.png", std::string_view(
                reinterpret_cast<const char*>(OnePixelPng), sizeof(OnePixelPng))) &&
            TestSupport::WriteFile(root / "white.png.meta",
                R"({"format":"gameengine-meta/1","pixelsPerUnit":1.0})");
    }

    Runtime::SpriteRenderer* AddSprite(Runtime::Scene& scene, const char* name,
        const Math::Vector3& position, const Math::Color& color)
    {
        auto* object = scene.CreateGameObject(name);
        object->GetTransform().SetPosition(position);
        auto* sprite = object->AddComponent<Runtime::SpriteRenderer>();
        sprite->SetSprite(Assets::AssetReference::Parse("white.png"));
        sprite->SetDrawMode(Runtime::SpriteRenderer::DrawMode::Sliced);
        sprite->SetSize({ 1, 1 });
        sprite->SetColor(color);
        sprite->SetSortingOrder(20);
        return sprite;
    }

    [[nodiscard]] bool HasPacket(const Rendering::RenderFrame& frame, const unsigned int id)
    {
        const auto& packets = frame.GetDrawPackets(Rendering::RenderPass::Transparent);
        return std::any_of(packets.begin(), packets.end(),
            [id](const auto& packet) { return packet.instanceId == id; });
    }

    [[nodiscard]] const Rendering::TilemapDraw* FindMap(const Rendering::RenderFrame& frame)
    {
        const auto maps = frame.GetDraws<Rendering::TilemapDraw>(Rendering::RenderPass::Transparent);
        return maps.size() == 1 ? maps.front() : nullptr;
    }

    [[nodiscard]] bool HasTargetTile(const Rendering::RenderFrame& frame)
    {
        const auto* map = FindMap(frame);
        return map && map->tiles && std::any_of(map->tiles->begin(), map->tiles->end(),
            [](const auto& tile) { return tile.column == TargetColumn && tile.row == TargetRow; });
    }

    [[nodiscard]] Math::ViewportPoint Project(const Rendering::CapturedImage& image,
        const Rendering::CameraRenderData& camera, const Math::Vector3& position)
    {
        return Math::ProjectToViewport(position, camera.view * camera.projection,
            static_cast<float>(image.width), static_cast<float>(image.height));
    }

    [[nodiscard]] bool CheckPixel(const Rendering::CapturedImage& image,
        const Rendering::CameraRenderData& camera, const Math::Vector3& position,
        const TestSupport::Rgba expected, const char* message, const bool equal = true)
    {
        const auto point = Project(image, camera, position);
        if (!Expect(point.isInFront && point.x >= 1 && point.y >= 1 &&
                        point.x + 1 < image.width && point.y + 1 < image.height,
                "the camera pixel probe must lie inside the actual panel viewport")) return false;
        const auto actual = TestSupport::ReadPixel(image,
            static_cast<unsigned int>(point.x), static_cast<unsigned int>(point.y));
        const bool passed = equal ? actual == expected : actual != expected;
        if (!passed)
            std::cerr << "  at (" << point.x << ',' << point.y << "): "
                      << TestSupport::Describe(actual) << '\n';
        return Expect(passed, message);
    }

    /// <summary>GPU 결과 바이트의 채널 순서만 PNG 인코더 형식으로 바꿔 보존한다.</summary>
    [[nodiscard]] bool SaveCapture(const Rendering::CapturedImage& image,
        const std::filesystem::path& path)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()))) ||
            FAILED(factory->CreateStream(stream.GetAddressOf())) ||
            FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) ||
            FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(image.width, image.height)))
            return false;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA)
            return false;
        std::vector<BYTE> bgra(image.pixels.size());
        for (std::size_t offset = 0; offset < bgra.size(); offset += 4)
        {
            bgra[offset] = std::to_integer<BYTE>(image.pixels[offset + 2]);
            bgra[offset + 1] = std::to_integer<BYTE>(image.pixels[offset + 1]);
            bgra[offset + 2] = std::to_integer<BYTE>(image.pixels[offset]);
            bgra[offset + 3] = std::to_integer<BYTE>(image.pixels[offset + 3]);
        }
        return SUCCEEDED(frame->WritePixels(image.height, image.width * 4,
                   static_cast<UINT>(bgra.size()), bgra.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    }

    [[nodiscard]] bool CheckAtScale(GameEditor::EditorContext& context,
        Runtime::TilemapRenderer& map, const unsigned int nearSpriteId,
        const unsigned int farSpriteId, Rendering::IGraphicsDevice& device,
        const std::filesystem::path& captures, const std::string& backend, const float scale)
    {
        auto& game = *context.GetProjectGame();
        bool passed = true;
        for (const auto& scenario : CameraCases)
        {
            std::cout << "  scene camera: " << backend << ' ' << scenario.name
                      << " UI " << scale << '\n';
            context.UpdateSceneCameraSetting(
                scenario.pivot, scenario.distance, scenario.yaw, scenario.pitch);
            ScaleHost scaleHost(scale);
            PropertyEditHost propertyHost;
            SceneToolHost toolHost;
            UI::UIContext ui{ nullptr, nullptr };
            GameEditor::EditorSceneViewPanel panel{ scaleHost, propertyHost, toolHost, context, ui };
            const Rendering::RenderTargetSize target{
                static_cast<unsigned int>(scenario.width * scale),
                static_cast<unsigned int>(scenario.height * scale) };
            Runtime::Input input;
            ui.BeginFrame(input, target);
            panel.Draw({ 0, 0, static_cast<float>(target.width), static_cast<float>(target.height) });
            Rendering::RenderFrameBuilder uiBuilder;
            uiBuilder.SetRenderTargetSize(target);
            ui.EndFrame(uiBuilder);
            const auto panelSize = panel.GetViewSize();
            const auto editorCamera = panel.GetCamera();
            passed &= Expect(panelSize.width == target.width && panelSize.height == target.height,
                "the scene panel must expose its pixel viewport once at UI scales one and two");
            game.SetRenderSurfaceSize(static_cast<float>(target.width), static_cast<float>(target.height));
            SceneRendering::SceneRenderPass frontend{ nullptr };
            const auto collect = [&](const bool lockBefore, const bool lockAfter = false)
            {
                Rendering::RenderFrameBuilder builder;
                builder.SetRenderTargetSize(panelSize);
                if (lockBefore) builder.LockCamera(editorCamera);
                frontend.Collect(game, builder);
                if (lockAfter) builder.LockCamera(editorCamera);
                return std::move(builder).Build();
            };
            const auto gameFrame = collect(false);
            const auto* gameTiles = FindMap(gameFrame);
            passed &= Expect(gameFrame.Validate().IsValid() && gameTiles && gameTiles->tiles &&
                    gameTiles->tiles->size() < 128 && !HasTargetTile(gameFrame) &&
                    HasPacket(gameFrame, nearSpriteId) && !HasPacket(gameFrame, farSpriteId),
                "the ordinary game camera must still cull the distant tile and sprite");

            const auto frame = collect(true);
            const auto* visibleMap = FindMap(frame);
            // Disabling culling is not a fix: 65,536 cells exceed the sprite quad budget,
            // and row 220 never renders when the backend receives the whole row-major map.
            passed &= Expect(frame.Validate().IsValid() && visibleMap && visibleMap->tiles &&
                    visibleMap->tiles->size() < 1024 && HasTargetTile(frame) &&
                    HasPacket(frame, farSpriteId) && !HasPacket(frame, nearSpriteId),
                "the editor camera must collect its distant visible cells within a bounded range");
            Rendering::CapturedImage image;
            if (!Expect(device.RenderToImage(frame, image) && image.IsValid(),
                    "the scene camera frame must reach the real graphics backend"))
            {
                passed = false;
                continue;
            }
            passed &= CheckPixel(image, editorCamera, TileCenter, Green,
                "a tile outside the game camera must be visible through the editor camera");
            passed &= CheckPixel(image, editorCamera, SpriteCenter, Red,
                "a sprite outside the game camera must be visible through the editor camera");
            const std::string name = backend + "-scale" + std::to_string(static_cast<int>(scale)) +
                '-' + scenario.name;
            passed &= Expect(SaveCapture(image, captures / (name + "-visible.png")),
                "the scene camera GPU capture must be preserved");

            if (std::string_view(scenario.name) == "wide")
            {
                // Control: use a stale draw set while the final GPU camera matrix contains the correct editor matrix.
                const auto lateFrame = collect(false, true);
                Rendering::CapturedImage lateImage;
                passed &= Expect(!HasTargetTile(lateFrame) && !HasPacket(lateFrame, farSpriteId),
                    "changing the camera after collection cannot recover already culled content");
                if (Expect(device.RenderToImage(lateFrame, lateImage) && lateImage.IsValid(),
                        "the stale-culling control frame must render"))
                {
                    passed &= CheckPixel(lateImage, editorCamera, TileCenter, Green,
                        "the stale-culling control must visibly miss the distant tile", false);
                    passed &= CheckPixel(lateImage, editorCamera, SpriteCenter, Red,
                        "the stale-culling control must visibly miss the distant sprite", false);
                    passed &= Expect(SaveCapture(lateImage, captures / (name + "-stale-control.png")),
                        "the stale-culling control capture must be preserved");
                }
                else passed = false;
                Rendering::CapturedImage gameImage;
                if (Expect(gameFrame.GetCamera().has_value() &&
                        device.RenderToImage(gameFrame, gameImage) && gameImage.IsValid(),
                        "the ordinary game view must also render"))
                {
                    passed &= CheckPixel(gameImage, *gameFrame.GetCamera(), { .5f, .5f, 0 }, Blue,
                        "the game view must retain its own nearby sprite and camera");
                    passed &= Expect(SaveCapture(gameImage, captures / (name + "-game.png")),
                        "the unchanged game view capture must be preserved");
                }
                else passed = false;
            }

            passed &= Expect(map.SetTile(TargetColumn, TargetRow, Runtime::TilemapRenderer::EmptyTile),
                "editing must erase the distant target cell");
            const auto erasedFrame = collect(true);
            Rendering::CapturedImage erased;
            passed &= Expect(!HasTargetTile(erasedFrame), "an erased tile must leave the collected frame");
            if (Expect(device.RenderToImage(erasedFrame, erased) && erased.IsValid(),
                    "the edited scene frame must render"))
            {
                passed &= CheckPixel(erased, editorCamera, TileCenter, Green,
                    "erasing the visible distant tile must immediately change its GPU pixel", false);
                passed &= CheckPixel(erased, editorCamera, SpriteCenter, Red,
                    "tile editing must leave the independent sprite visible");
                passed &= Expect(SaveCapture(erased, captures / (name + "-erased.png")),
                    "the edited scene camera capture must be preserved");
            }
            else passed = false;
            passed &= Expect(map.SetTile(TargetColumn, TargetRow, 0),
                "painting must restore the distant target cell");
            const auto restoredFrame = collect(true);
            Rendering::CapturedImage restored;
            passed &= Expect(HasTargetTile(restoredFrame) &&
                    device.RenderToImage(restoredFrame, restored) && restored.IsValid() &&
                    restored.pixels == image.pixels,
                "repainting must restore the exact image under pan, zoom, aspect, and orbit changes");
            const auto gameAfter = collect(false);
            passed &= Expect(!HasTargetTile(gameAfter) && !HasPacket(gameAfter, farSpriteId) &&
                    HasPacket(gameAfter, nearSpriteId),
                "an editor capture must not leak its camera or visibility into the next game frame");
        }
        return passed;
    }
}

bool RunSceneViewCameraTests()
{
    const TestSupport::RegistryScope registries;
    const TestSupport::TemporaryDirectory temporary("scene-view-camera");
    if (!Expect(WriteProject(temporary.GetPath()), "the scene camera project and PNG must be written"))
        return false;
    const auto settingsPath = temporary.GetPath() / "Editor.settings.json";
    GameEditor::EditorContext context{ temporary.GetPath(), settingsPath };
    if (!Expect(context.OpenProject(temporary.GetPath() / "Camera.gameproject") &&
                    context.GetProjectGame() && context.GetOpenScene(),
            "the editor must open the real scene camera fixture project")) return false;
    if (!Expect(context.GetEditorAssetDatabase() &&
                    context.GetEditorAssetDatabase()->GetProjectRootPath() == temporary.GetPath() &&
                    std::filesystem::is_regular_file(settingsPath),
            "editor assets and persisted settings must use the isolated fixture paths")) return false;
    auto& scene = *context.GetOpenScene();
    auto* cameraObject = scene.CreateGameObject("Game camera");
    cameraObject->GetTransform().SetPosition({ .5f, .5f, -5 });
    auto* camera = cameraObject->AddComponent<Runtime::Camera>();
    camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
    camera->SetOrthographicSize(2);
    camera->SetClearColor(Math::Color::Black);
    auto* map = scene.CreateGameObject("Large tilemap")->AddComponent<Runtime::TilemapRenderer>();
    map->SetTileset(Assets::AssetReference::Parse("white.png"));
    map->SetColumns(MapExtent);
    map->SetRows(MapExtent);
    map->SetTiles(std::vector<int>(static_cast<std::size_t>(MapExtent) * MapExtent, 0));
    map->SetColor({ 0, 1, 0, 1 });
    const auto* nearSprite = AddSprite(scene, "Game view sprite", { .5f, .5f, 0 }, { 0, 0, 1, 1 });
    const auto* farSprite = AddSprite(scene, "Scene view sprite", SpriteCenter, { 1, 0, 0, 1 });
    // All three renderers share this imported PNG. Preload it through the public
    // database API before collecting; scene loading remains a Game-owned operation.
    if (!Expect(context.GetProjectGame()->GetAssetDatabase().LoadTexture(map->GetTileset()) != nullptr,
            "the scene camera texture must load before the first collected frame")) return false;

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope
    {
        bool owns;
        ~ComScope() { if (owns) CoUninitialize(); }
    } com{ SUCCEEDED(initialized) };
    if (!Expect(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE,
            "PNG encoding must initialize COM")) return false;
    const auto captures = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "build/scene-camera-captures" /
        (std::to_string(GetCurrentProcessId()) + '-' +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(captures);
    std::cout << "  scene camera captures: " << captures.string() << '\n';
    const auto backends = TestSupport::SupportedBackends();
    bool passed = Expect(!backends.empty(), "scene camera pixel checks require a graphics backend");
    for (const auto* backend : backends)
    {
        auto device = backend->CreateDevice();
        if (!Expect(device && device->Initialize(Platform::NativeSurface{}),
                "the scene camera graphics device must initialize"))
        {
            passed = false;
            continue;
        }
        passed &= TestSupport::ForEachUiScale([&](const float scale)
        {
            return CheckAtScale(context, *map, nearSprite->GetInstanceId(), farSprite->GetInstanceId(),
                *device, captures, std::string(backend->id), scale);
        });
    }
    return passed;
}

static const TestSupport::Registration gSceneViewCameraTests{
    "BackendImage", "scene view cameras must drive world collection and GPU visibility", RunSceneViewCameraTests };
