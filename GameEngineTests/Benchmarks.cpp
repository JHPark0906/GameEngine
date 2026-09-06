#include "Benchmarks.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Rendering/CachedTextMeasure.h"
#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Math/Vector.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/PlatformServices.h"
#include "Platform/NativeSurface.h"
#include "Rendering/GraphicsBackend.h"
#include "App/RenderThread.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Camera.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Light.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Rendering/TextRasterizationCache.h"
#include "SceneRendering/SceneRenderPass.h"
#include "UI/UIContext.h"

#include "TestSupport.h"

/// <summary>
/// 창 없이 도는 성능 측정이다. 장면은 프로그램이 만들고, 프레임 조립은 실제 SceneRenderPass를,
/// 제출은 headless 장치의 RenderToImage를 지난다 — 에디터의 게임 뷰가 서는 바로 그 경로라서,
/// 여기 나온 숫자는 진짜 프레임이 밟는 코드의 숫자다.
/// </summary>
namespace
{
    /// <summary>
    /// 이 측정이 세울 장면 패스의 글자 배치 캐시다. 실제 애플리케이션에서는 UI 배치의
    /// 잣대와 같은 것을 나눠 쥐지만, 여기서는 그리는 쪽만 필요하므로 하나면 된다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<GameEngine::Rendering::TextRasterizationCache>
        MakeSceneTextCache()
    {
        return std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
            TestSupport::CreateTestTextRasterizer());
    }

    using Clock = std::chrono::steady_clock;

    constexpr int WarmupRuns = 3;
    constexpr int SampleRuns = 10;
    constexpr unsigned int FrameWidth = 1280;
    constexpr unsigned int FrameHeight = 720;
    /// <summary>깊이 변형의 사슬 길이다. 평평한 장면과의 차이가 월드 행렬 캐싱 후보의 근거다.</summary>
    constexpr int ChainDepth = 10;

    constexpr std::string_view SpritePath = "Resources/tdw.png";
    constexpr std::string_view MeshPath =
        "Resources/free-low-poly-unicorn/source/FoodLowPolyBigPack_01.fbx";

    struct Measurement
    {
        double medianMilliseconds = 0.0;
        double minimumMilliseconds = 0.0;
    };

    /// <summary>워밍업을 버리고 회당 시간을 모아 중앙값과 최소를 낸다.</summary>
    template <typename TBody>
    [[nodiscard]] Measurement Measure(TBody&& body)
    {
        for (int run = 0; run < WarmupRuns; ++run)
        {
            body();
        }
        std::vector<double> samples;
        samples.reserve(SampleRuns);
        for (int run = 0; run < SampleRuns; ++run)
        {
            const Clock::time_point start = Clock::now();
            body();
            const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
            samples.push_back(elapsed.count());
        }
        std::sort(samples.begin(), samples.end());
        Measurement result;
        result.minimumMilliseconds = samples.front();
        result.medianMilliseconds = samples[samples.size() / 2];
        return result;
    }

    void PrintRow(const std::string_view name, const Measurement& measurement)
    {
        std::cout << "  " << std::left << std::setw(40) << name << std::right << std::fixed
                  << std::setprecision(3) << std::setw(10) << measurement.medianMilliseconds
                  << " ms" << std::setw(10) << measurement.minimumMilliseconds << " ms\n";
    }

    /// <summary>
    /// 업데이트 부하를 내는 벤치 전용 동작이다. 매 프레임 자기 Transform을 흔든다 — 장면의
    /// 모든 오브젝트가 이것을 하나씩 지녀, Game::Update가 MonoBehaviour 디스패치를 포함한다.
    /// </summary>
    class WobbleBehaviour final : public GameEngine::Runtime::MonoBehaviour
    {
    protected:
        void Update(const float deltaTime) override
        {
            mPhase += deltaTime;
            GameEngine::Runtime::Transform* const transform = GetTransform();
            if (!transform)
            {
                return;
            }
            const GameEngine::Math::Vector3 position = transform->GetPosition();
            transform->SetPosition(
                { position.GetX(), position.GetY() + std::sin(mPhase) * 0.001f, position.GetZ() });
        }

    private:
        float mPhase = 0.0f;
    };

    /// <summary>
    /// 측정 장면을 만들어 게임에 넣는다. 오브젝트마다 스프라이트와 동작 하나, 열 개마다 메시
    /// 하나, 그리고 카메라와 방향광·주변광이다. chainDepth가 1보다 크면 연속한 오브젝트들을
    /// 그 깊이의 Transform 사슬로 묶는다.
    /// </summary>
    [[nodiscard]] unsigned int PopulateScene(
        GameEngine::Runtime::Game& game, const int objectCount, const int chainDepth)
    {
        using namespace GameEngine;

        auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Benchmark");

        Runtime::GameObject* const cameraObject = scene->CreateGameObject("Camera");
        Runtime::Camera* const camera =
            cameraObject ? cameraObject->AddComponent<Runtime::Camera>() : nullptr;
        if (!camera)
        {
            return 0;
        }
        cameraObject->GetTransform().SetPosition({ 0.0f, 0.0f, -15.0f });

        Runtime::GameObject* const lightObject = scene->CreateGameObject("Sun");
        Runtime::Light* const sun =
            lightObject ? lightObject->AddComponent<Runtime::Light>() : nullptr;
        if (sun)
        {
            sun->SetKind(Runtime::Light::Kind::Directional);
            lightObject->GetTransform().SetRotation({ 45.0f, 30.0f, 0.0f });
        }
        Runtime::GameObject* const ambientObject = scene->CreateGameObject("Ambient");
        if (Runtime::Light* const ambient =
                ambientObject ? ambientObject->AddComponent<Runtime::Light>() : nullptr)
        {
            ambient->SetKind(Runtime::Light::Kind::Ambient);
            ambient->SetIntensity(0.3f);
        }

        const int side = (std::max)(1, static_cast<int>(std::ceil(std::sqrt(objectCount))));
        const float spacing = 12.0f / static_cast<float>(side);
        Runtime::GameObject* previous = nullptr;
        for (int index = 0; index < objectCount; ++index)
        {
            Runtime::GameObject* const object =
                scene->CreateGameObject("O" + std::to_string(index));
            if (!object)
            {
                return 0;
            }
            const float x = (static_cast<float>(index % side) - side * 0.5f) * spacing;
            const float y = (static_cast<float>(index / side) - side * 0.5f) * spacing;
            object->GetTransform().SetPosition({ x, y, 0.0f });

            if (Runtime::SpriteRenderer* const sprite =
                    object->AddComponent<Runtime::SpriteRenderer>())
            {
                sprite->SetSprite(Assets::AssetReference::Parse(std::string(SpritePath)));
                sprite->SetSize({ spacing, spacing });
            }
            if (index % 10 == 0)
            {
                if (Runtime::MeshRenderer* const mesh =
                        object->AddComponent<Runtime::MeshRenderer>())
                {
                    mesh->SetMesh(Assets::AssetReference::Parse(std::string(MeshPath)));
                }
            }
            static_cast<void>(object->AddComponent<WobbleBehaviour>());

            // 깊이 변형: 사슬 머리마다 새 루트를 시작하고, 나머지는 앞의 오브젝트에 매달린다.
            // GetLocalToWorldMatrix가 매번 부모 사슬을 걸어 올라가는 비용이 여기서 드러난다.
            if (chainDepth > 1 && previous && index % chainDepth != 0)
            {
                static_cast<void>(object->GetTransform().SetParent(&previous->GetTransform()));
            }
            previous = object;
        }

        return game.AddScene(std::move(scene));
    }

    [[nodiscard]] GameEngine::Rendering::RenderFrame BuildSceneFrame(
        const GameEngine::Runtime::Game& game, GameEngine::SceneRendering::SceneRenderPass& pass)
    {
        GameEngine::Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ FrameWidth, FrameHeight });
        pass.Collect(game, builder);
        return std::move(builder).Build();
    }

    /// <summary>
    /// 조립 비용을 컴포넌트 쿼리, 월드 행렬, 에셋 resolve, 정렬 없는 Collect로 나눠 잰다.
    /// 각 행은 누적이 아니라 독립적인 측정이다. "asset resolve, unmemoized"는 렌더러마다
    /// resolve하여 ReferenceCache가 피하는 비용을 측정하므로 실제 Collect 비용이 아니다.
    /// 이 해석은 결과를 읽는 사람이 확인할 수 있도록 도구 출력에도 명시한다.
    /// </summary>
    void MeasureAssemblyBreakdown(const GameEngine::Runtime::Game& game)
    {
        using namespace GameEngine;

        const auto forEachObject = [&game](auto&& body)
        {
            for (const auto& scene :
                 game.GetSceneManager().GetActiveScenes() | std::views::values)
            {
                for (const auto& object : scene->GetGameObjects() | std::views::values)
                {
                    body(*object);
                }
            }
        };

        PrintRow("  breakdown: component queries", Measure([&forEachObject]
        {
            int visible = 0;
            forEachObject([&visible](const Runtime::GameObject& object)
            {
                for (const Runtime::Light* light : object.GetComponents<Runtime::Light>())
                {
                    visible += light->IsActiveAndEnabled() ? 1 : 0;
                }
                for (const Runtime::MeshRenderer* renderer :
                     object.GetComponents<Runtime::MeshRenderer>())
                {
                    visible += renderer->IsRenderable() ? 1 : 0;
                }
                for (const Runtime::SpriteRenderer* renderer :
                     object.GetComponents<Runtime::SpriteRenderer>())
                {
                    visible += renderer->IsRenderable() ? 1 : 0;
                }
                for (const Runtime::TextRenderer* renderer :
                     object.GetComponents<Runtime::TextRenderer>())
                {
                    visible += renderer->IsRenderable() ? 1 : 0;
                }
                for (const Runtime::Camera* camera : object.GetComponents<Runtime::Camera>())
                {
                    visible += camera->IsActiveAndEnabled() ? 1 : 0;
                }
            });
            static_cast<void>(visible);
        }));

        PrintRow("  breakdown: world matrices", Measure([&forEachObject]
        {
            float sum = 0.0f;
            forEachObject([&sum](const Runtime::GameObject& object)
            {
                for (const Runtime::SpriteRenderer* renderer :
                     object.GetComponents<Runtime::SpriteRenderer>())
                {
                    if (renderer->IsRenderable())
                    {
                        sum += object.GetTransform().GetLocalToWorldMatrix().GetTranslation().GetX();
                    }
                }
                for (const Runtime::MeshRenderer* renderer :
                     object.GetComponents<Runtime::MeshRenderer>())
                {
                    if (renderer->IsRenderable())
                    {
                        sum += object.GetTransform().GetLocalToWorldMatrix().GetTranslation().GetX();
                    }
                }
            });
            static_cast<void>(sum);
        }));

        PrintRow("  breakdown: asset resolve, unmemoized", Measure([&forEachObject, &game]
        {
            const Assets::AssetDatabase& assetDatabase = game.GetAssetDatabase();
            int resolved = 0;
            forEachObject([&assetDatabase, &resolved](const Runtime::GameObject& object)
            {
                for (const Runtime::SpriteRenderer* renderer :
                     object.GetComponents<Runtime::SpriteRenderer>())
                {
                    if (!renderer->IsRenderable() || !renderer->GetSprite().IsValid())
                    {
                        continue;
                    }
                    resolved += assetDatabase.FindAsset<Assets::Sprite>(renderer->GetSprite())
                        ? 1 : 0;
                    resolved += assetDatabase.LoadTexture(renderer->GetSprite()) ? 1 : 0;
                }
                for (const Runtime::MeshRenderer* renderer :
                     object.GetComponents<Runtime::MeshRenderer>())
                {
                    if (!renderer->IsRenderable() || !renderer->GetMesh().IsValid())
                    {
                        continue;
                    }
                    resolved += assetDatabase.FindAsset<Assets::Mesh>(renderer->GetMesh()) ? 1 : 0;
                    resolved += assetDatabase.LoadMesh(renderer->GetMesh()) ? 1 : 0;
                }
            });
            static_cast<void>(resolved);
        }));

        // 에디터 주 프레임에 장면 패스를 하나 더 다는 값이다. 그 런타임의 장면에는
        // UI 계층 몇 개밖에 없으므로, "컴포넌트가 거의 없을 때 매 프레임 얼마인가"가
        // 이 행이 답하는 질문이다. 짐작 대신 수로 답해야 건너뛸지 말지 정할 수 있다.
        {
            Runtime::Game emptyGame{ nullptr, nullptr };
            SceneRendering::SceneRenderPass emptyPass{ MakeSceneTextCache() };
            PrintRow("Collect+Build (editor surface, no scene)",
                Measure([&emptyGame, &emptyPass]
            {
                const Rendering::RenderFrame frame = BuildSceneFrame(emptyGame, emptyPass);
                static_cast<void>(frame.GetRenderTargetSize().width);
            }));
        }

        SceneRendering::SceneRenderPass pass{ MakeSceneTextCache() };
        PrintRow("  breakdown: Collect only", Measure([&game, &pass]
        {
            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ FrameWidth, FrameHeight });
            pass.Collect(game, builder);
        }));

        // 각 측정 행이 독립적으로 어떤 비용을 재는지 도구 출력 자체에 명시한다.
        std::cout << "    rows above are standalone, not cumulative; "
                     "\"asset resolve, unmemoized\" re-resolves\n"
                     "    every renderer on purpose, so it measures the cost "
                     "SceneRenderPass's per-frame\n"
                     "    memo already avoids — it is not part of \"Collect only\" "
                     "and is not a bottleneck.\n";
    }

    // ==== 핫스팟 측정 (--measure) 전용 도우미 ====

    /// <summary>
    /// AssetDatabase와 같은 FNV-1a 계산으로 해시 비용을 독립 측정한다.
    /// </summary>
    [[nodiscard]] std::uint64_t HashLikeDatabase(const std::span<const std::byte> bytes)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const std::byte byte : bytes)
        {
            hash ^= static_cast<std::uint64_t>(byte);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    /// <summary>
    /// 합성 프로젝트를 만든다: 샘플의 실제 스프라이트를 count개로 불려 임시 디렉터리에 둔다.
    /// 열 개마다 하나는 큰 아틀라스라, 파일 수만이 아니라 바이트 수도 실제 프로젝트를 닮는다.
    /// 반환값은 이 프로젝트가 담은 총 바이트다 — 해시 비용은 파일 수가 아니라 바이트에 붙는다.
    /// </summary>
    [[nodiscard]] std::uintmax_t PopulateSyntheticProject(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& sampleContent,
        const int count)
    {
        const std::filesystem::path smallSource = sampleContent / "Resources/tdw.png";
        const std::filesystem::path largeSource =
            sampleContent / "Resources/free-low-poly-unicorn/textures/atlas_basecolor.png";
        std::error_code error;
        const std::filesystem::path resources = projectRoot / "Resources";
        std::filesystem::create_directories(resources, error);

        // 스캔은 루트에 .gameproject가 정확히 하나 있기를 요구한다. 없으면 실패 경로를 재게 된다.
        const std::string descriptor =
            "{\n  \"projectName\": \"Hotspot\",\n"
            "  \"initialSceneId\": 0,\n  \"scenes\": [],\n  \"graphicsApi\": \"Auto\"\n}\n";
        if (!TestSupport::WriteFile(projectRoot / "Hotspot.gameproject", descriptor))
        {
            return 0;
        }

        std::uintmax_t totalBytes = 0;
        for (int index = 0; index < count; ++index)
        {
            const bool large = index % 10 == 0;
            const std::filesystem::path& source = large ? largeSource : smallSource;
            const std::filesystem::path destination =
                resources / ("asset_" + std::to_string(index) + ".png");
            std::filesystem::copy_file(
                source, destination, std::filesystem::copy_options::overwrite_existing, error);
            if (error)
            {
                return 0;
            }
            totalBytes += std::filesystem::file_size(destination, error);
        }
        return totalBytes;
    }

    /// <summary>합성 프로젝트의 에셋 경로들이다. 읽기·해시 몫을 재는 순회가 이것을 쓴다.</summary>
    [[nodiscard]] std::vector<std::filesystem::path> CollectRelativeAssetPaths(const int count)
    {
        std::vector<std::filesystem::path> paths;
        paths.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index)
        {
            paths.emplace_back("Resources/asset_" + std::to_string(index) + ".png");
        }
        return paths;
    }

    /// <summary>
    /// 스프라이트 count개를 참조하는 장면이다. AddScene이 이것을 받으면 참조된 에셋을 전량
    /// 임포트하므로, 장면 열기의 임포트 몫이 여기서 드러난다.
    /// </summary>
    [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Scene> MakeSyntheticScene(
        GameEngine::Runtime::Game& game, const int count)
    {
        using namespace GameEngine;
        auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Synthetic");
        for (int index = 0; index < count; ++index)
        {
            Runtime::GameObject* const object =
                scene->CreateGameObject("S" + std::to_string(index));
            if (!object)
            {
                return nullptr;
            }
            if (Runtime::SpriteRenderer* const sprite =
                    object->AddComponent<Runtime::SpriteRenderer>())
            {
                sprite->SetSprite(Assets::AssetReference::Parse(
                    "Resources/asset_" + std::to_string(index) + ".png"));
            }
        }
        return scene;
    }

    /// <summary>
    /// 계층 패널이 매 프레임 하는 일의 엔진 쪽 몫이다: 루트 목록을 얻고, 오브젝트마다 라벨
    /// 문자열을 담은 행을 새로 만든다. 에디터의 실제 Draw는 여기에 위젯 선언을 더한 것이라,
    /// 이 수치는 그 하한이다.
    /// </summary>
    struct MeasuredHierarchyRow
    {
        std::string label;
        float indent = 0.0f;
        unsigned int instanceId = 0;
    };

    void AppendMeasuredRows(
        std::vector<MeasuredHierarchyRow>& rows,
        GameEngine::Runtime::GameObject& gameObject,
        const int depth)
    {
        MeasuredHierarchyRow row;
        row.label = gameObject.GetName().empty() ? "(GameObject)" : gameObject.GetName();
        row.indent = static_cast<float>(depth);
        row.instanceId = gameObject.GetInstanceId();
        rows.push_back(std::move(row));

        for (GameEngine::Runtime::Transform* childTransform :
             gameObject.GetTransform().GetChildren())
        {
            if (GameEngine::Runtime::GameObject* const child =
                    childTransform ? childTransform->GetGameObject() : nullptr)
            {
                AppendMeasuredRows(rows, *child, depth + 1);
            }
        }
    }
}

bool RunBenchmarks(const std::span<const std::string_view> arguments)
{
    using namespace GameEngine;

    if (arguments.empty())
    {
        std::cerr << "usage: GameEngineTests --benchmark <project content path> [scale...]\n"
                     "example: GameEngineTests --benchmark C:\\Projects\\MyGame\\Content\n";
        return false;
    }

    const std::filesystem::path contentRoot(arguments[0]);
    std::vector<int> scales;
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        int scale = 0;
        const std::string_view text = arguments[index];
        if (std::from_chars(text.data(), text.data() + text.size(), scale).ec != std::errc{} ||
            scale <= 0)
        {
            std::cerr << "not a scale: " << text << '\n';
            return false;
        }
        scales.push_back(scale);
    }
    if (scales.empty())
    {
        scales = { 100, 1000, 10000 };
    }

#ifdef NDEBUG
    constexpr std::string_view configuration = "Release";
#else
    constexpr std::string_view configuration = "Debug";
#endif
    std::cout << "GameEngine benchmark  (configuration: " << configuration
              << ", warmup " << WarmupRuns << ", samples " << SampleRuns << ")\n"
              << "content: " << contentRoot.string() << '\n';

    // 제출 측정에 쓸 headless 장치들. 장치 초기화 자체는 측정 밖이다 — 프레임마다 일어나는
    // 일이 아니다.
    const Platform::DirectoryContentSource content(contentRoot);
    std::vector<std::pair<std::string, std::unique_ptr<Rendering::IGraphicsDevice>>> devices;
    for (const Rendering::GraphicsBackendDescriptor& backend :
         Rendering::GraphicsBackendRegistry::GetBackends())
    {
        if (!backend.IsComplete() || !backend.IsSupported())
        {
            continue;
        }
        std::unique_ptr<Rendering::IGraphicsDevice> device = backend.CreateDevice();
        if (device && device->Initialize(Platform::NativeSurface{}))
        {
            devices.emplace_back(std::string(backend.id), std::move(device));
        }
    }
    if (devices.empty())
    {
        std::cout << "no supported graphics backend; submission is not measured\n";
    }

    // 스크린 공간 TextRenderer 100개를 측정한다. 문자열이 고정인 프레임과 매 프레임 바뀌는
    // 프레임을 나눠 재어, 캐시 재사용과 갱신되는 카운터·로그 콘솔의 비용을 각각 비교한다.
    {
        constexpr int TextCount = 100;
        Runtime::Game textGame{ nullptr, nullptr };
        std::vector<Runtime::TextRenderer*> textRenderers;
        bool textSceneBuilt = textGame.Initialize(content, {});
        if (textSceneBuilt)
        {
            auto scene = std::make_unique<Runtime::Scene>(textGame.GetRuntimeContext(), "Text");
            Runtime::GameObject* const cameraObject = scene->CreateGameObject("Camera");
            textSceneBuilt = cameraObject && cameraObject->AddComponent<Runtime::Camera>();
            for (int index = 0; textSceneBuilt && index < TextCount; ++index)
            {
                Runtime::GameObject* const object =
                    scene->CreateGameObject("T" + std::to_string(index));
                Runtime::TextRenderer* const text =
                    object ? object->AddComponent<Runtime::TextRenderer>() : nullptr;
                if (!text)
                {
                    textSceneBuilt = false;
                    break;
                }
                text->SetText("Item " + std::to_string(index));
                text->SetFontSize(20.0f);
                text->SetFontFamily("TestFont");
                object->GetTransform().SetPosition({
                    100.0f + static_cast<float>(index % 10) * 120.0f,
                    50.0f + static_cast<float>(index / 10) * 60.0f,
                    0.0f });
                textRenderers.push_back(text);
            }
            textSceneBuilt = textSceneBuilt && textGame.AddScene(std::move(scene)) != 0;
        }
        if (!textSceneBuilt)
        {
            std::cerr << "failed to build the text benchmark scene\n";
            return false;
        }
        textGame.SetRenderAspectRatio(
            static_cast<float>(FrameWidth) / static_cast<float>(FrameHeight));

        std::cout << "\n[text: " << TextCount << " screen-space text renderers]\n"
                  << "  " << std::left << std::setw(40) << "measurement" << std::right
                  << std::setw(10) << "median" << "   " << std::setw(10) << "min" << "\n";

        SceneRendering::SceneRenderPass textPass{ MakeSceneTextCache() };
        const auto baseline = BuildSceneFrame(textGame, textPass);
        std::size_t textDrawCount = 0;
        std::size_t glyphCount = 0;
        for (const Rendering::RenderPass renderPass : {
                 Rendering::RenderPass::Opaque, Rendering::RenderPass::Transparent,
                 Rendering::RenderPass::Overlay })
        {
            for (const auto* draw : baseline.GetDraws<Rendering::TextDraw>(renderPass))
            {
                if (draw->glyphs && !draw->glyphs->empty())
                {
                    ++textDrawCount;
                    glyphCount += draw->glyphs->size();
                }
            }
        }
        if (textDrawCount < TextCount || glyphCount == 0)
        {
            std::cerr << "text benchmark produced no complete glyph workload; check the bundled font\n";
            return false;
        }
        std::cout << "  verified text draws=" << textDrawCount << ", glyphs=" << glyphCount << '\n';
        PrintRow("Collect+Build (static text)", Measure([&textGame, &textPass]
        {
            const Rendering::RenderFrame frame = BuildSceneFrame(textGame, textPass);
            static_cast<void>(frame);
        }));

        int textFrame = 0;
        const auto changeAllText = [&textRenderers, &textFrame]
        {
            ++textFrame;
            for (std::size_t index = 0; index < textRenderers.size(); ++index)
            {
                textRenderers[index]->SetText(
                    "Item " + std::to_string(index) + " #" + std::to_string(textFrame));
            }
        };
        PrintRow("Collect+Build (changing text)", Measure([&textGame, &textPass, &changeAllText]
        {
            changeAllText();
            const Rendering::RenderFrame frame = BuildSceneFrame(textGame, textPass);
            static_cast<void>(frame);
        }));

        for (const auto& [backendId, device] : devices)
        {
            bool captured = true;
            PrintRow(backendId + " changing text frame", Measure(
                [&textGame, &textPass, &changeAllText, &device, &captured]
            {
                changeAllText();
                const Rendering::RenderFrame frame = BuildSceneFrame(textGame, textPass);
                Rendering::CapturedImage image;
                if (!device->RenderToImage(frame, image))
                {
                    captured = false;
                }
            }));
            if (!captured)
            {
                std::cerr << "text benchmark capture failed\n";
                return false;
            }
        }
    }

    for (const int scale : scales)
    {
        std::cout << "\n[scale " << scale << ": " << scale << " sprites, "
                  << (scale + 9) / 10 << " meshes, " << scale << " behaviours]\n"
                  << "  " << std::left << std::setw(40) << "measurement" << std::right
                  << std::setw(10) << "median" << "   " << std::setw(10) << "min" << "\n";

        // 평평한 장면: 업데이트, 프레임 조립, 백엔드 제출.
        Runtime::Game flatGame{ nullptr, nullptr };
        if (!flatGame.Initialize(content, {}) || PopulateScene(flatGame, scale, 1) == 0)
        {
            std::cerr << "failed to build the flat benchmark scene\n";
            return false;
        }
        flatGame.SetRenderAspectRatio(
            static_cast<float>(FrameWidth) / static_cast<float>(FrameHeight));

        PrintRow("Game::Update", Measure([&flatGame] { flatGame.Update(1.0f / 60.0f); }));

        SceneRendering::SceneRenderPass pass{ MakeSceneTextCache() };
        PrintRow("Collect+Build (flat)", Measure([&flatGame, &pass]
        {
            const Rendering::RenderFrame frame = BuildSceneFrame(flatGame, pass);
            static_cast<void>(frame);
        }));
        MeasureAssemblyBreakdown(flatGame);

        const Rendering::RenderFrame frame = BuildSceneFrame(flatGame, pass);
        // 캡처는 두 가지 모드로 측정한다. immediate는 제출 후 GPU를 기다려 이번 프레임의 픽셀을 받고,
        // deferred는 한 프레임 늦은 결과를 허용한다. 현재 RenderThread의 뷰는 immediate를 사용하며,
        // deferred는 비교를 위한 별도 모드다. 두 모드의 비용은 따로 보고한다.
        for (const bool deferred : { false, true })
        {
            for (const auto& [backendId, device] : devices)
            {
                const Rendering::IGraphicsDevice::CaptureRequest request{ 0u, deferred };
                bool anyCapture = false;
                bool capturesValid = true;
                PrintRow(
                    backendId + (deferred ? " RenderToImage (deferred)"
                                          : " RenderToImage (immediate)"),
                    Measure([&frame, &device, &request, &anyCapture, &capturesValid]
                    {
                        Rendering::CapturedImage image;
                        // deferred는 이전 GPU 작업이 아직 끝나지 않았으면 false여도 유효하다.
                        const bool captured = device->RenderToImage(frame, image, request);
                        if (!captured && !request.deferred)
                        {
                            capturesValid = false;
                        }
                        anyCapture |= captured;
                    }));
                if (!capturesValid || !anyCapture)
                {
                    std::cerr << "scene benchmark capture failed: " << backendId << '\n';
                    return false;
                }
            }
        }

        // 깊이 변형: 같은 규모, 깊이 10의 Transform 사슬. 조립 시간의 차이가 월드 행렬 캐싱
        // 후보의 근거다.
        Runtime::Game chainGame{ nullptr, nullptr };
        if (!chainGame.Initialize(content, {}) ||
            PopulateScene(chainGame, scale, ChainDepth) == 0)
        {
            std::cerr << "failed to build the chained benchmark scene\n";
            return false;
        }
        chainGame.SetRenderAspectRatio(
            static_cast<float>(FrameWidth) / static_cast<float>(FrameHeight));
        SceneRendering::SceneRenderPass chainPass{ MakeSceneTextCache() };
        PrintRow("Collect+Build (chain depth 10)", Measure([&chainGame, &chainPass]
        {
            const Rendering::RenderFrame frame = BuildSceneFrame(chainGame, chainPass);
            static_cast<void>(frame);
        }));
    }

    std::cout << "\ndone. record results alongside the machine and configuration; see Docs/VALIDATION.md.\n";
    return true;
}

namespace
{
    using MeasureClock = std::chrono::steady_clock;

    /// <summary>
    /// 주어진 시간이 지날 때까지 계산을 반복한다.
    /// sleep_for의 스케줄링 오차가 지정한 작업 시간을 바꾸지 않도록 바쁜 대기로 모형을 구성한다.
    /// 이 값은 실제 엔진 작업의 처리 시간이 아니라 지정한 모형의 비용이다.
    /// </summary>
    void SpinFor(const MeasureClock::duration duration)
    {
        const MeasureClock::time_point end = MeasureClock::now() + duration;
        while (MeasureClock::now() < end)
        {
        }
    }

    /// <summary>시간만 쓰는 장치다. 이 측정에 필요한 것은 그리는 일이 아니라 걸리는 일이다.</summary>
    class TimeSpendingDevice final : public GameEngine::Rendering::IGraphicsDevice
    {
    public:
        explicit TimeSpendingDevice(const MeasureClock::duration work) : mWork(work) {}

        bool Initialize(const GameEngine::Platform::NativeSurface&) override { return true; }
        bool BeginFrame() override { return true; }
        GameEngine::Rendering::RenderTargetSize GetRenderTargetSize() const override
        {
            return { 640, 480 };
        }
        GameEngine::Rendering::GraphicsDeviceCapabilities GetCapabilities() const override
        {
            return {};
        }
        bool Render(const GameEngine::Rendering::RenderFrame&) override { return true; }
        bool EndFrame() override
        {
            SpinFor(mWork);
            return true;
        }
        bool RenderToImage(
            const GameEngine::Rendering::RenderFrame&,
            GameEngine::Rendering::CapturedImage&,
            const CaptureRequest&) override
        {
            return false;
        }

    private:
        MeasureClock::duration mWork;
    };

    /// <summary>
    /// 캡처를 게임 스레드 또는 렌더 스레드에서 수행할 때 장치 대기와 작업 겹침을 비교한다.
    /// 절대 시간은 지정한 render/update/capture 상수로 구성한 모형의 값이며 실제 에디터 측정이 아니다.
    /// 같은 장치의 캡처와 렌더링은 직렬 실행되므로 이 모형의 최소 프레임 시간은 render + capture다.
    /// 갱신만 그 작업과 겹칠 수 있다. 단일 스레드, 캡처 없는 렌더 스레드, 두 캡처 배치를 함께 재고
    /// 프레임 시간과 게임 스레드의 장치 대기를 구분한다.
    /// </summary>
    void MeasureCaptureHandoverCost()
    {
        using GameEngine::App::RenderThread;

        constexpr auto RenderWork = std::chrono::milliseconds(16); // present가 vsync를 무는 시간
        constexpr auto UpdateWork = std::chrono::milliseconds(8);  // 한 프레임의 갱신
        constexpr auto CaptureWork = std::chrono::milliseconds(6); // 뷰 하나를 이미지로 받는 시간
        constexpr int FrameCount = 12;

        const auto makeFrame = []
        {
            GameEngine::Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ 640, 480 });
            return std::move(builder).Build();
        };

        // 렌더 스레드 없이 모든 작업을 순서대로 수행하는 모형이다.
        TimeSpendingDevice serialDevice(RenderWork);
        const MeasureClock::time_point serialBegin = MeasureClock::now();
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            SpinFor(UpdateWork);
            SpinFor(CaptureWork);
            static_cast<void>(serialDevice.BeginFrame());
            static_cast<void>(serialDevice.Render(makeFrame()));
            static_cast<void>(serialDevice.EndFrame());
        }
        const MeasureClock::duration serialElapsed = MeasureClock::now() - serialBegin;

        // 플레이어의 모양: 캡처 뷰가 없으니 아무것도 넘겨받지 않는다.
        TimeSpendingDevice playerDevice(RenderWork);
        RenderThread playerThread(playerDevice);
        playerThread.Start();
        const MeasureClock::time_point playerBegin = MeasureClock::now();
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            SpinFor(UpdateWork);
            static_cast<void>(playerThread.Submit({ {}, makeFrame() }));
        }
        playerThread.Stop();
        const MeasureClock::duration playerElapsed = MeasureClock::now() - playerBegin;

        // 게임 스레드 캡처 모형: 갱신 → 장치 인계 대기 → 캡처 → 제출.
        TimeSpendingDevice handoverDevice(RenderWork);
        RenderThread handoverThread(handoverDevice);
        handoverThread.Start();
        MeasureClock::duration blocked = MeasureClock::duration::zero();
        const MeasureClock::time_point handoverBegin = MeasureClock::now();
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            SpinFor(UpdateWork);
            const MeasureClock::time_point waitBegin = MeasureClock::now();
            handoverThread.WaitUntilIdle();
            blocked += MeasureClock::now() - waitBegin;
            SpinFor(CaptureWork);
            static_cast<void>(handoverThread.Submit({ {}, makeFrame() }));
        }
        handoverThread.Stop();
        const MeasureClock::duration handoverElapsed = MeasureClock::now() - handoverBegin;

        // 갱신 후 프레임과 캡처를 렌더 스레드에 함께 제출한다.
        // 게임 스레드는 장치를 직접 기다리지 않는다. 같은 장치의 렌더링과 캡처는 직렬 실행되므로
        // 이 모형의 프레임 시간 하한은 render + capture이며 갱신만 그 작업과 겹칠 수 있다.
        TimeSpendingDevice movedDevice(RenderWork + CaptureWork);
        RenderThread movedThread(movedDevice);
        movedThread.Start();
        const MeasureClock::time_point movedBegin = MeasureClock::now();
        for (int frame = 0; frame < FrameCount; ++frame)
        {
            SpinFor(UpdateWork);
            static_cast<void>(movedThread.Submit({ {}, makeFrame() }));
        }
        movedThread.Stop();
        const MeasureClock::duration movedElapsed = MeasureClock::now() - movedBegin;

        const auto perFrame = [](const MeasureClock::duration duration)
        {
            return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(duration)
                       .count() /
                FrameCount;
        };
        const auto printRow = [](const std::string_view name, const double value)
        {
            std::cout << "  " << std::left << std::setw(44) << name << std::right << std::fixed
                      << std::setprecision(3) << std::setw(10) << value << " ms/frame\n";
        };
        std::cout << "\ncapture cost (model: render 16ms, update 8ms, capture 6ms)\n";
        printRow("one thread, before the render thread", perFrame(serialElapsed));
        printRow("render thread, no capture views", perFrame(playerElapsed));
        printRow("capture on the game thread", perFrame(handoverElapsed));
        printRow("  of which blocked on the device", perFrame(blocked));
        printRow("capture on the render thread", perFrame(movedElapsed));
        printRow("  of which blocked on the device", 0.0);
    }

    /// <summary>
    /// 에셋 재스캔이 실제 프로젝트에서 무엇을 얼마나 무는지 잰다.
    ///
    /// 에디터는 파일이 바뀌면 <c>AssetDatabase</c>를 새로 스캔해 통째로 갈아 끼운다. 그 자리에서
    /// 스캔하지 않는 이유는 실패한 스캔이 자기를 비우기 때문이고, 그 선택은 옳다 — 다만 갈아
    /// 끼우는 순간 <b>이미 읽어 둔 페이로드가 새 데이터베이스에는 없다</b>. 그래서 값은 둘로
    /// 나뉜다: 스캔 자체의 비용과, 버려진 페이로드를 다시 읽는 비용.
    ///
    /// 두 번째가 이 측정의 요점이다. 스캔은 파일 이름과 해시를 훑는 일이지만 페이로드는 이미지를
    /// 디코드하고 메시를 파싱한 결과라, 무엇이 상주해 있었느냐에 따라 자릿수가 달라진다.
    ///
    /// <b>이 측정이 재지 않는 것</b>: 재스캔이 얼마나 자주 일어나는지. 그것은 사람이 파일을
    /// 얼마나 자주 건드리느냐에 달렸고 여기서 답할 수 없다. 여기 있는 것은 한 번에 드는 값이다.
    /// </summary>
    /// <param name="contentRoot">실제 프로젝트의 Content 디렉터리다.</param>
    void MeasureAssetRescanCost(const std::filesystem::path& contentRoot)
    {
        using namespace GameEngine;

        const Platform::DirectoryContentSource source(contentRoot);
        if (!source.IsValid())
        {
            std::cerr << "the content root could not be read: " << contentRoot.string() << '\n';
            return;
        }

        Assets::AssetDatabase database;
        if (!database.Refresh(source))
        {
            std::cerr << "the first scan failed; nothing to measure\n";
            return;
        }

        // 이 프로젝트가 실제로 쥔 것을 모두 읽어 상주시킨다. 에디터가 장면을 열어 둔 상태가
        // 이것이고, 재스캔이 버리는 것도 이것이다.
        std::size_t textures = 0;
        std::size_t meshes = 0;
        for (const std::unique_ptr<Assets::Asset>& asset : database.GetAssets())
        {
            if (!asset)
            {
                continue;
            }
            const Assets::AssetReference reference =
                Assets::AssetReference::Parse(asset->GetRelativePath().generic_string());
            if (database.LoadTexture(reference))
            {
                ++textures;
            }
            else if (database.LoadMesh(reference))
            {
                ++meshes;
            }
        }
        const std::size_t residentBefore = database.GetLoadedPayloadCount();

        std::cout << "\nasset rescan cost (content: " << contentRoot.string() << ")\n"
                  << "  assets " << database.GetAssets().size() << ", resident payloads "
                  << residentBefore << " (" << textures << " textures, " << meshes << " meshes)\n"
                  << "  " << std::left << std::setw(40) << "measurement" << std::right
                  << std::setw(10) << "median" << "   " << std::setw(10) << "min" << "\n";

        // 스캔 하나. 에디터가 파일 변동마다 하는 그것이다.
        PrintRow("AssetDatabase::Refresh (rescan)", Measure([&source]
        {
            Assets::AssetDatabase rescanned;
            if (!rescanned.Refresh(source))
            {
                std::cerr << "a rescan failed\n";
            }
        }));

        // 갈아 끼운 뒤 같은 것을 다시 상주시키는 비용. 스캔이 끝난 데이터베이스에서 시작하므로
        // 여기 재는 것은 순수하게 페이로드를 다시 읽는 몫이다.
        PrintRow("  reloading the payloads it dropped", Measure([&source]
        {
            Assets::AssetDatabase rescanned;
            if (!rescanned.Refresh(source))
            {
                return;
            }
            for (const std::unique_ptr<Assets::Asset>& asset : rescanned.GetAssets())
            {
                if (!asset)
                {
                    continue;
                }
                const Assets::AssetReference reference =
                Assets::AssetReference::Parse(asset->GetRelativePath().generic_string());
                if (!rescanned.LoadTexture(reference))
                {
                    static_cast<void>(rescanned.LoadMesh(reference));
                }
            }
        }));

        // 그리고 물려받는 경우다. 아무것도 안 바뀐 재스캔이라 모든 페이로드가 옛 것에게서
        // 넘어오고, 남는 것은 스캔뿐이어야 한다 — 위 줄과의 차이가 이 최적화가 걷어낸 몫이다.
        PrintRow("  same, inheriting what did not change", Measure([&source, &database]
        {
            Assets::AssetDatabase rescanned;
            if (!rescanned.Refresh(source))
            {
                return;
            }
            rescanned.InheritPayloadsFrom(database);
            for (const std::unique_ptr<Assets::Asset>& asset : rescanned.GetAssets())
            {
                if (!asset)
                {
                    continue;
                }
                const Assets::AssetReference reference =
                    Assets::AssetReference::Parse(asset->GetRelativePath().generic_string());
                if (!rescanned.LoadTexture(reference))
                {
                    static_cast<void>(rescanned.LoadMesh(reference));
                }
            }
        }));
    }
}

bool RunHotspotMeasurements(const std::span<const std::string_view> arguments)
{
    using namespace GameEngine;

    if (arguments.empty())
    {
        std::cerr << "usage: GameEngineTests --measure <project content path> [assetCount...]\n";
        return false;
    }

    const std::filesystem::path contentRoot(arguments[0]);
    std::vector<int> assetCounts;
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        int count = 0;
        const std::string_view text = arguments[index];
        if (std::from_chars(text.data(), text.data() + text.size(), count).ec != std::errc{} ||
            count <= 0)
        {
            std::cerr << "not an asset count: " << text << '\n';
            return false;
        }
        assetCounts.push_back(count);
    }
    if (assetCounts.empty())
    {
        assetCounts = { 50, 200, 500 };
    }

#ifdef NDEBUG
    constexpr std::string_view configuration = "Release";
#else
    constexpr std::string_view configuration = "Debug";
#endif
    std::cout << "GameEngine hotspot measurement  (configuration: " << configuration
              << ", warmup " << WarmupRuns << ", samples " << SampleRuns << ")\n"
              << "content: " << contentRoot.string() << '\n';

    // ==== (1) 프로젝트/장면 열기: 스캔의 해시 몫과 장면의 임포트 몫 ====
    std::cout << "\n=== (1) opening a project and a scene ===\n";
    for (const int count : assetCounts)
    {
        const TestSupport::TemporaryDirectory projectDirectory("hotspot-project");
        const std::uintmax_t totalBytes =
            PopulateSyntheticProject(projectDirectory.GetPath(), contentRoot, count);
        if (totalBytes == 0)
        {
            std::cerr << "failed to build the synthetic project\n";
            return false;
        }

        std::cout << "\n[" << count << " sprite assets, "
                  << (totalBytes / 1024) << " KiB total]\n"
                  << "  " << std::left << std::setw(40) << "measurement" << std::right
                  << std::setw(10) << "median" << "   " << std::setw(10) << "min" << "\n";

        const Platform::DirectoryContentSource source(projectDirectory.GetPath());
        const std::vector<std::filesystem::path> assetPaths = CollectRelativeAssetPaths(count);

        // 스캔 전체. 매번 빈 데이터베이스로 시작해야 등록 비용이 다시 든다.
        PrintRow("AssetDatabase::Refresh (scan)", Measure([&source]
        {
            Assets::AssetDatabase database;
            if (!database.Refresh(source))
            {
                std::cerr << "Refresh failed\n";
            }
        }));

        // 그 안의 파일 읽기 몫과, 읽기에 해시를 더한 몫. 둘의 차이가 해시가 가져가는 시간이다.
        PrintRow("  of which: read every file", Measure([&source, &assetPaths]
        {
            std::vector<std::byte> bytes;
            for (const std::filesystem::path& path : assetPaths)
            {
                static_cast<void>(source.Read(path, bytes));
            }
        }));
        PrintRow("  of which: read + FNV hash", Measure([&source, &assetPaths]
        {
            std::vector<std::byte> bytes;
            std::uint64_t hash = 0;
            for (const std::filesystem::path& path : assetPaths)
            {
                if (source.Read(path, bytes))
                {
                    hash ^= HashLikeDatabase(bytes);
                }
            }
            static_cast<void>(hash);
        }));

        // 해시만. 파일 I/O를 빼고 같은 바이트를 메모리에서 해시한다 — 등록에서 해시를 걷어내면
        // 정확히 이만큼이 사라진다.
        {
            std::vector<std::vector<std::byte>> loaded;
            loaded.reserve(assetPaths.size());
            for (const std::filesystem::path& path : assetPaths)
            {
                std::vector<std::byte> bytes;
                if (source.Read(path, bytes))
                {
                    loaded.push_back(std::move(bytes));
                }
            }
            static volatile std::uint64_t hashSink = 0; // 최적화가 해시 루프를 통째로 지우지 못하게 붙잡아 두는 싱크다.
            PrintRow("  of which: FNV hash only (RAM)", Measure([&loaded]
            {
                std::uint64_t hash = 0;
                for (const std::vector<std::byte>& bytes : loaded)
                {
                    hash ^= HashLikeDatabase(bytes);
                }
                hashSink = hash;
            }));
        }

        // 등록이 파일마다 부르는 경로 해석과, 없는 사이드카를 읽어 보는 시도. 스캔 시간이
        // 바이트가 아니라 파일 수에 붙는다면 범인은 이 둘이다.
        PrintRow("  of which: ResolveFilePath x N", Measure([&source, &assetPaths]
        {
            std::size_t resolved = 0;
            for (const std::filesystem::path& path : assetPaths)
            {
                resolved += source.ResolveFilePath(path).empty() ? 0u : 1u;
            }
            static_cast<void>(resolved);
        }));
        PrintRow("  of which: absent sidecar read", Measure([&source, &assetPaths]
        {
            std::vector<std::byte> bytes;
            for (const std::filesystem::path& path : assetPaths)
            {
                std::filesystem::path sidecar = path;
                sidecar += L".meta";
                static_cast<void>(source.Read(sidecar, bytes));
            }
        }));
        // 프로젝트 열기 전체와, 거기에 장면 열기를 더한 것. 차이가 장면 에셋 임포트다.
        PrintRow("Game::Initialize (project open)", Measure([&source]
        {
            // 오디오는 이 측정의 대상이 아니다: 출력 없이 세운다.
            Runtime::Game game{ nullptr, nullptr };
            if (!game.Initialize(source, {}))
            {
                std::cerr << "Game::Initialize failed\n";
            }
        }));
        PrintRow("Game::Initialize + AddScene", Measure([&source, count]
        {
            // 오디오는 이 측정의 대상이 아니다: 출력 없이 세운다.
            Runtime::Game game{ nullptr, nullptr };
            if (!game.Initialize(source, {}))
            {
                std::cerr << "Game::Initialize failed\n";
                return;
            }
            std::unique_ptr<Runtime::Scene> scene = MakeSyntheticScene(game, count);
            if (!scene || game.AddScene(std::move(scene)) == 0)
            {
                std::cerr << "AddScene failed\n";
            }
        }));
    }

    // ==== (2) 계층 패널이 매 프레임 다시 만드는 행 ====
    std::cout << "\n=== (2) rebuilding hierarchy rows every frame ===\n";
    {
        const Platform::DirectoryContentSource content(contentRoot);
        for (const int objectCount : { 100, 10000 })
        {
            // 오디오는 이 측정의 대상이 아니다: 출력 없이 세운다.
            Runtime::Game game{ nullptr, nullptr };
            if (!game.Initialize(content, {}))
            {
                std::cerr << "failed to initialize the hierarchy measurement game\n";
                return false;
            }
            auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Hierarchy");
            for (int index = 0; index < objectCount; ++index)
            {
                if (!scene->CreateGameObject("GameObject " + std::to_string(index)))
                {
                    std::cerr << "failed to create a hierarchy measurement object\n";
                    return false;
                }
            }
            Runtime::Scene* const scenePointer = scene.get();
            if (game.AddScene(std::move(scene)) == 0)
            {
                std::cerr << "failed to add the hierarchy measurement scene\n";
                return false;
            }

            std::cout << "\n[" << objectCount << " objects, flat]\n"
                      << "  " << std::left << std::setw(40) << "measurement" << std::right
                      << std::setw(10) << "median" << "   " << std::setw(10) << "min" << "\n";

            PrintRow("Scene::GetRootGameObjects", Measure([scenePointer]
            {
                const std::vector<Runtime::GameObject*> roots = scenePointer->GetRootGameObjects();
                static_cast<void>(roots.size());
            }));
            PrintRow("BuildHierarchyRows (engine part)", Measure([scenePointer]
            {
                std::vector<MeasuredHierarchyRow> rows;
                for (Runtime::GameObject* const root : scenePointer->GetRootGameObjects())
                {
                    if (root)
                    {
                        AppendMeasuredRows(rows, *root, 1);
                    }
                }
                static_cast<void>(rows.size());
            }));
        }
    }

    // ==== (3) 에디터 뷰 파이프라인의 프레임당 고정비 ====
    std::cout << "\n=== (3) the per-frame view cost in the editor ===\n";
    {
        const Platform::DirectoryContentSource content(contentRoot);
        Runtime::Game game{ nullptr, nullptr };
        if (!game.Initialize(content, {}) || PopulateScene(game, 100, 1) == 0)
        {
            std::cerr << "failed to build the view measurement scene\n";
            return false;
        }
        game.SetRenderAspectRatio(
            static_cast<float>(FrameWidth) / static_cast<float>(FrameHeight));
        SceneRendering::SceneRenderPass pass{ MakeSceneTextCache() };
        const Rendering::RenderFrame frame = BuildSceneFrame(game, pass);

        std::vector<std::pair<std::string, std::unique_ptr<Rendering::IGraphicsDevice>>> devices;
        for (const Rendering::GraphicsBackendDescriptor& backend :
             Rendering::GraphicsBackendRegistry::GetBackends())
        {
            if (!backend.IsComplete() || !backend.IsSupported())
            {
                continue;
            }
            std::unique_ptr<Rendering::IGraphicsDevice> device = backend.CreateDevice();
            if (device && device->Initialize(Platform::NativeSurface{}))
            {
                devices.emplace_back(std::string(backend.id), std::move(device));
            }
        }

        std::cout << "\n[one " << FrameWidth << "x" << FrameHeight
                  << " view, 100-object scene]\n"
                  << "  " << std::left << std::setw(40) << "measurement" << std::right
                  << std::setw(10) << "median" << "   " << std::setw(10) << "min" << "\n";

        // 뷰 하나가 프레임마다 다시 조립되는 비용이다. 캡처와 나눠 봐야 어느 쪽이 무거운지 안다.
        PrintRow("Collect+Build (one view)", Measure([&game, &pass]
        {
            const Rendering::RenderFrame viewFrame = BuildSceneFrame(game, pass);
            static_cast<void>(viewFrame);
        }));

        for (const auto& [backendId, device] : devices)
        {
            // 즉시 캡처는 현재 에디터의 경로이고, 지연 캡처는 비교용이다.
            for (const bool deferred : { false, true })
            {
                const Rendering::IGraphicsDevice::CaptureRequest request{ 0u, deferred };
                bool anyCapture = false;
                bool capturesValid = true;
                const Measurement measurement = Measure(
                    [&frame, &device, &request, &anyCapture, &capturesValid]
                {
                    Rendering::CapturedImage image;
                    // 지연 캡처는 이전 GPU 작업이 아직 끝나지 않았으면 false일 수 있다.
                    const bool captured = device->RenderToImage(frame, image, request);
                    if (!captured && !request.deferred)
                    {
                        capturesValid = false;
                    }
                    anyCapture |= captured;
                });
                if (!capturesValid || !anyCapture)
                {
                    std::cerr << "view benchmark capture failed: " << backendId
                              << (deferred ? " (deferred)\n" : " (immediate)\n");
                    return false;
                }
                PrintRow(backendId + (deferred ? " capture (deferred)" : " capture (immediate)"),
                    measurement);
            }
        }

        // 캡처된 픽셀을 UI 텍스처로 올리는 비용. 뷰마다 프레임마다 한 번씩 일어난다.
        {
            // 재는 것은 동적 텍스처 업로드뿐이라 클립보드도 래스터라이저도 필요 없다.
            UI::UIContext ui{ nullptr, nullptr };
            std::shared_ptr<Assets::TextureData> texture;
            const std::vector<std::byte> pixels(
                static_cast<std::size_t>(FrameWidth) * FrameHeight * 4, std::byte{ 0x80 });
            PrintRow("UIContext::UpdateDynamicTexture", Measure([&ui, &texture, &pixels]
            {
                ui.UpdateDynamicTexture(texture, FrameWidth, FrameHeight, pixels);
            }));
        }
    }

    MeasureCaptureHandoverCost();
    MeasureAssetRescanCost(contentRoot);

    std::cout << "\ndone. report the numbers and measurement environment; see Docs/VALIDATION.md.\n";
    return true;
}
