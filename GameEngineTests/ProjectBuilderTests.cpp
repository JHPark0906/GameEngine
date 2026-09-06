#include "ProjectBuilderTests.h"

#include <windows.h>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <ranges>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "Rendering/CachedTextMeasure.h"
#include "App/GameBootstrapRegistry.h"
#include "App/ProjectFile.h"
#include "Assets/AssetDatabase.h"
#include "Build/ProjectBuilder.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/MeshDrawGeometry.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Assets/TextureData.h"
#include "Assets/ResourceId.h"
#include "Assets/AssetImporterRegistry.h"
#include "Platform/IAudioOutput.h"
#include "Platform/IWindow.h"
#include "Runtime/Input.h"
#include "Runtime/AudioSource.h"
#include "Runtime/Camera.h"
#include "Runtime/ComponentType.h"
#include "Runtime/Light.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Rigidbody3D.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TilemapRenderer.h"
#include "Serialization/ComponentSchema.h"
#include "Runtime/GameObject.h"
#include "Runtime/Scene.h"
#include "Runtime/Transform.h"
#include "Runtime/SceneManager.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Game.h"
#include "Runtime/Scene.h"
#include "Runtime/TextRenderer.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/PreservedComponent.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/PlatformServices.h"
#include "Serialization/SceneSerializer.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    bool WriteFile(const std::filesystem::path& path, const std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return static_cast<bool>(stream);
    }

    /// <summary>A format whose files hold one mesh, enough for a scene to reference an asset.</summary>
    class TestMeshImporter final : public GameEngine::Assets::IAssetImporter
    {
    public:
        [[nodiscard]] GameEngine::Assets::AssetType GetAssetType() const override
        {
            return GameEngine::Assets::AssetType::Mesh;
        }

        [[nodiscard]] bool Import(
            const std::filesystem::path& relativePath,
            std::span<const std::byte>,
            const GameEngine::Assets::ImportIdentity& identity,
            const GameEngine::Assets::ImportMode mode,
            GameEngine::Assets::ImportedContents& contents,
            std::string&) const override
        {
            contents.subAssets.push_back(
                { GameEngine::Assets::AssetType::Mesh, relativePath.stem().string() });
            if (mode == GameEngine::Assets::ImportMode::Structure)
            {
                return true;
            }
            auto mesh = std::make_shared<GameEngine::Assets::MeshData>();
            mesh->id = identity.MakeResourceId(GameEngine::Assets::ResourceIdDomain::Mesh, 0);
            mesh->vertices.resize(3);
            mesh->indices = { 0, 1, 2 };
            contents.meshes.push_back(std::move(mesh));
            return true;
        }
    };

    /// <summary>업데이트 안에서 장면 로드를 요청한다. 그것이 로드를 큐에 넣는 조건이다.</summary>
    class SceneLoadingBehaviour final : public GameEngine::Runtime::MonoBehaviour
    {
    public:
        GameEngine::Runtime::Game* game = nullptr;
        unsigned int sceneToLoad = 0;
        GameEngine::Runtime::SceneLoadResult result = GameEngine::Runtime::SceneLoadResult::Failed;
        bool hasRequested = false;
        /// <summary>업데이트 중의 AddScene 시도 결과이다. 거부되어 0이어야 한다.</summary>
        unsigned int addSceneResult = 1;

    protected:
        void Update(float) override
        {
            if (!hasRequested && game)
            {
                hasRequested = true;
                result = game->LoadScene(sceneToLoad);
                // 순회 중인 목록에 장면을 넣는 것은 거부되어야 한다. 실제로 거부되는지는 이
                // 시점 — 장면들이 업데이트 중일 때 — 에만 물을 수 있다.
                addSceneResult = game->AddScene(
                    std::make_unique<GameEngine::Runtime::Scene>(game->GetRuntimeContext()));
            }
        }

    private:
        [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Component> Clone() const override
        {
            return nullptr;
        }
    };

    /// <summary>
    /// 장면 저장은 로더가 읽는 형식 그대로를 만들어야 한다: 저장하고 다시 로드한 장면은 같은
    /// 이름, 같은 계층, 같은 컴포넌트 값을 가져야 하고, 같은 장면을 두 번 저장하면 같은 텍스트가
    /// 나와야 한다 — 장면의 저장 컨테이너가 순서를 약속하지 않기 때문에, 결정성은 공짜가 아니라
    /// 지켜야 하는 성질이다.
    /// </summary>
    bool RunSceneSaveTests()
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);

        Runtime::Scene scene(runtimeContext, "Saved");
        Runtime::GameObject* const root = scene.CreateGameObject("Root");
        Runtime::GameObject* const child = scene.CreateGameObject("Child");
        bool built = root && child;
        if (built)
        {
            root->GetTransform().SetPosition({ 1.5f, -2.0f, 3.25f });
            root->GetTransform().SetRotation({ 0.0f, 45.0f, 90.0f });
            root->GetTransform().SetScale({ 2.0f, 2.0f, 1.0f });

            Runtime::Camera* const camera = root->AddComponent<Runtime::Camera>();
            built = camera != nullptr;
            if (camera)
            {
                camera->SetProjectionMode(Runtime::Camera::ProjectionMode::Orthographic);
                camera->SetOrthographicSize(7.0f);
                camera->SetPriority(3);
                camera->SetClearColor({ 0.25f, 0.5f, 0.75f, 1.0f });
            }

            child->SetActive(false);
            built = built && child->GetTransform().SetParent(&root->GetTransform());
            child->GetTransform().SetPosition({ 0.0f, 1.0f, 0.0f });

            Runtime::SpriteRenderer* const sprite =
                child->AddComponent<Runtime::SpriteRenderer>();
            built = built && sprite != nullptr;
            if (sprite)
            {
                sprite->SetSprite(Assets::AssetReference("Textures/Panel.png"));
                sprite->SetDrawMode(Runtime::SpriteRenderer::DrawMode::Sliced);
                sprite->SetSize({ 3.0f, 2.0f });
                sprite->SetFlipX(true);
                sprite->SetSortingOrder(5);
                sprite->SetColor({ 1.0f, 0.5f, 0.25f, 0.75f });
            }

            Runtime::MeshRenderer* const mesh = child->AddComponent<Runtime::MeshRenderer>();
            built = built && mesh != nullptr;
            if (mesh)
            {
                mesh->SetMesh(Assets::AssetReference("Models/Rock.fbx", 2));
            }
        }

        const std::string savedText = Serialization::SceneSerializer::SaveToText(scene);
        const std::string savedAgain = Serialization::SceneSerializer::SaveToText(scene);
        const bool deterministic = !savedText.empty() && savedText == savedAgain;

        const std::unique_ptr<Runtime::Scene> loaded =
            Serialization::SceneSerializer::LoadFromBytes(
                std::as_bytes(std::span(savedText.data(), savedText.size())),
                "Saved.scene",
                runtimeContext);

        bool roundTripped = loaded != nullptr && loaded->GetName() == "Saved";
        if (roundTripped)
        {
            const Runtime::GameObject* const loadedRoot = loaded->FindGameObject("Root");
            const Runtime::GameObject* const loadedChild = loaded->FindGameObject("Child");
            roundTripped = loadedRoot && loadedChild;
            if (roundTripped)
            {
                const Runtime::Transform& rootTransform = loadedRoot->GetTransform();
                roundTripped =
                    rootTransform.GetPosition().GetX() == 1.5f &&
                    rootTransform.GetPosition().GetZ() == 3.25f &&
                    rootTransform.GetRotation().GetY() == 45.0f &&
                    rootTransform.GetScale().GetX() == 2.0f &&
                    loadedChild->GetTransform().GetParent() == &rootTransform &&
                    !loadedChild->IsActive();

                const Runtime::Camera* const loadedCamera =
                    loadedRoot->GetComponent<Runtime::Camera>();
                roundTripped = roundTripped && loadedCamera &&
                    loadedCamera->GetProjectionMode() ==
                        Runtime::Camera::ProjectionMode::Orthographic &&
                    loadedCamera->GetOrthographicSize() == 7.0f &&
                    loadedCamera->GetPriority() == 3 &&
                    loadedCamera->GetClearColor().b == 0.75f;

                const Runtime::SpriteRenderer* const loadedSprite =
                    loadedChild->GetComponent<Runtime::SpriteRenderer>();
                roundTripped = roundTripped && loadedSprite &&
                    loadedSprite->GetSprite().ToString() == "Textures/Panel.png" &&
                    loadedSprite->GetDrawMode() == Runtime::SpriteRenderer::DrawMode::Sliced &&
                    loadedSprite->GetSize().GetX() == 3.0f &&
                    loadedSprite->IsFlippedX() &&
                    loadedSprite->GetSortingOrder() == 5 &&
                    loadedSprite->GetColor().a == 0.75f;

                const Runtime::MeshRenderer* const loadedMesh =
                    loadedChild->GetComponent<Runtime::MeshRenderer>();
                roundTripped = roundTripped && loadedMesh &&
                    loadedMesh->GetMesh().ToString() == "Models/Rock.fbx#2";
            }
        }

        // 팩토리가 없는 컴포넌트 타입은 로드를 실패시키지도, 잃지도 않아야 한다: 데이터로 보존
        // 되어 있다가 저장 때 속성 그대로 되살아나야 한다. 프로젝트 정의 컴포넌트를 에디터가
        // 열고 저장할 때 매번 밟는 경로다.
        constexpr std::string_view unknownComponentScene = R"({"sceneName":"Unknown",)"
            R"("gameObjects":[{"name":"Holder","components":)"
            R"([{"type":"NotARealComponent","speed":2.5,"path":"Data/Route.json"}]}]})";
        const std::unique_ptr<Runtime::Scene> unknownLoaded =
            Serialization::SceneSerializer::LoadFromBytes(
                std::as_bytes(std::span(unknownComponentScene.data(), unknownComponentScene.size())),
                "Unknown.scene",
                runtimeContext);
        const Runtime::GameObject* const holder =
            unknownLoaded ? unknownLoaded->FindGameObject("Holder") : nullptr;
        const Serialization::PreservedComponent* const preserved =
            holder ? holder->GetComponent<Serialization::PreservedComponent>() : nullptr;
        const bool unknownComponentPreserved = preserved &&
            preserved->GetPreservedTypeName() == "NotARealComponent";

        // 저장하면 보존된 것이 타입과 속성 그대로 돌아온다.
        const std::string unknownSaved = unknownLoaded
            ? Serialization::SceneSerializer::SaveToText(*unknownLoaded)
            : std::string{};
        const bool preservedSurvivesSave =
            unknownSaved.find("\"type\": \"NotARealComponent\"") != std::string::npos &&
            unknownSaved.find("\"speed\": 2.5") != std::string::npos &&
            unknownSaved.find("\"path\": \"Data/Route.json\"") != std::string::npos;

        // 팩토리 없는 프로세스를 반복해서 오가도 보존 컴포넌트의 데이터는 유지되어야 한다.
        // 이 검사는 텍스트 전체 대신 보존된 컴포넌트가 같은 내용으로 다시 저장되는지 확인한다.
        const std::unique_ptr<Runtime::Scene> reloaded =
            Serialization::SceneSerializer::LoadFromBytes(
                std::as_bytes(std::span(unknownSaved.data(), unknownSaved.size())),
                "Unknown.scene",
                runtimeContext);
        const Runtime::GameObject* const reloadedHolder =
            reloaded ? reloaded->FindGameObject("Holder") : nullptr;
        const std::string reloadedSaved = reloaded
            ? Serialization::SceneSerializer::SaveToText(*reloaded)
            : std::string{};
        const bool preservationIsStable = reloadedHolder &&
            reloadedHolder->GetComponent<Serialization::PreservedComponent>() != nullptr &&
            reloadedSaved.find("\"type\": \"NotARealComponent\"") != std::string::npos &&
            reloadedSaved.find("\"speed\": 2.5") != std::string::npos &&
            reloadedSaved.find("\"path\": \"Data/Route.json\"") != std::string::npos;

        return Expect(built, "the scene to save should build") &&
            Expect(deterministic, "saving the same scene twice should produce the same text") &&
            Expect(roundTripped, "a saved scene should load back with the same values") &&
            Expect(
                unknownComponentPreserved,
                "an unknown component should be preserved as data rather than lost") &&
            Expect(
                preservedSurvivesSave,
                "a preserved component should be written back with its properties") &&
            Expect(
                preservationIsStable,
                "preservation should survive any number of load-save round trips");
    }

    /// <summary>속성 값의 동등이다. Color에는 operator==가 없으므로 여기서 채널을 비교한다.</summary>
    bool PropertyValuesEqual(
        const GameEngine::Runtime::PropertyValue& left,
        const GameEngine::Runtime::PropertyValue& right)
    {
        if (left.index() != right.index())
        {
            return false;
        }
        return std::visit(
            [&right](const auto& value) -> bool
            {
                using Value = std::decay_t<decltype(value)>;
                const Value& other = std::get<Value>(right);
                if constexpr (std::is_same_v<Value, GameEngine::Math::Color>)
                {
                    return value.r == other.r && value.g == other.g &&
                        value.b == other.b && value.a == other.a;
                }
                else
                {
                    return value == other;
                }
            },
            left);
    }

    /// <summary>
    /// 왕복 검증을 위해 현재와 다른 값을 만든다. float의 x*0.5+0.25는 엔진 컴포넌트의 어떤
    /// 기본값에서 출발해도 실제 setter의 클램프 범위 안에 남는다.
    /// </summary>
    GameEngine::Runtime::PropertyValue MutatedPropertyValue(
        const GameEngine::Runtime::PropertyDescriptor& descriptor,
        const GameEngine::Runtime::PropertyValue& current)
    {
        using GameEngine::Runtime::PropertyKind;
        using GameEngine::Runtime::PropertyValue;
        const auto squeeze = [](const float value) { return value * 0.5f + 0.25f; };
        switch (descriptor.GetKind())
        {
        case PropertyKind::Bool:
            return PropertyValue{ !std::get<bool>(current) };
        case PropertyKind::Int:
            return PropertyValue{ std::get<int>(current) + 1 };
        case PropertyKind::Float:
            return PropertyValue{ squeeze(std::get<float>(current)) };
        case PropertyKind::String:
            return PropertyValue{ std::get<std::string>(current) + "*" };
        case PropertyKind::Vector2:
        {
            const auto& value = std::get<GameEngine::Math::Vector2>(current);
            return PropertyValue{
                GameEngine::Math::Vector2{ squeeze(value.GetX()), squeeze(value.GetY()) } };
        }
        case PropertyKind::Vector3:
        {
            const auto& value = std::get<GameEngine::Math::Vector3>(current);
            return PropertyValue{ GameEngine::Math::Vector3{
                squeeze(value.GetX()), squeeze(value.GetY()), squeeze(value.GetZ()) } };
        }
        case PropertyKind::Color:
        {
            const auto& value = std::get<GameEngine::Math::Color>(current);
            return PropertyValue{ GameEngine::Math::Color{
                squeeze(value.r), squeeze(value.g), squeeze(value.b), squeeze(value.a) } };
        }
        case PropertyKind::AssetReference:
            return PropertyValue{
                GameEngine::Assets::AssetReference::Parse("TestAssets/RoundTrip.png#1") };
        case PropertyKind::Enum:
            return PropertyValue{ (std::get<int>(current) + 1) %
                static_cast<int>(descriptor.GetEnumNames().size()) };
        }
        return current;
    }

    /// <summary>직렬화되는 모든 속성이 두 컴포넌트에서 같은 값인지 확인한다.</summary>
    bool SerializedPropertiesEqual(
        const GameEngine::Runtime::ComponentType& type,
        const GameEngine::Runtime::Component& left,
        const GameEngine::Runtime::Component& right)
    {
        for (const GameEngine::Runtime::PropertyDescriptor* const descriptor :
             GameEngine::Runtime::CollectProperties(type))
        {
            if (GameEngine::Runtime::HasTrait(
                    descriptor->GetTraits(), GameEngine::Runtime::PropertyTraits::NotSerialized))
            {
                continue;
            }
            if (!PropertyValuesEqual(descriptor->Get(left), descriptor->Get(right)))
            {
                std::cerr << "FAILED: a property came back different. type=" << type.GetName()
                          << ", property=" << descriptor->GetName() << '\n';
                return false;
            }
        }
        return true;
    }

    /// <summary>
    /// 일반화된 직렬화가 컴포넌트별 코드 없이 형식을 지키는지 고정한다: 만들 수 있는 모든
    /// 타입이 기본값으로도, 값을 바꿔도 파일을 왕복하고, 저장은 결정적이며, 형식 오류 —
    /// 이름 표 밖의 enum 문자열, 다른 회전 표기 — 는 로드를 실패시키고, 구 파일의 레거시
    /// 멤버는 로드를 막지 않는다.
    /// </summary>
    bool RunGenericSerializationTests()
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);

        const auto loadText = [&runtimeContext](const std::string_view text)
        {
            return Serialization::SceneSerializer::LoadFromBytes(
                std::as_bytes(std::span(text.data(), text.size())), "Generic.scene",
                runtimeContext);
        };

        const Runtime::ComponentType* const creatableTypes[] = {
            &Runtime::Camera::StaticType(), &Runtime::Light::StaticType(),
            &Runtime::MeshRenderer::StaticType(), &Runtime::SpriteRenderer::StaticType(),
            &Runtime::TextRenderer::StaticType(), &Runtime::AudioSource::StaticType(),
            &Runtime::Rigidbody2D::StaticType(), &Runtime::Rigidbody3D::StaticType(),
        };

        // 만들 수 있는 모든 타입: 기본값 저장→로드→속성 동일, 그리고 모든 속성을 바꾼 뒤에도
        // 왕복. Transform은 호스트 객체의 것을 함께 바꿔, rotation의 상수 주석까지 왕복을 지난다.
        bool roundTrips = true;
        bool deterministic = true;
        for (const Runtime::ComponentType* const type : creatableTypes)
        {
            for (const bool mutate : { false, true })
            {
                Runtime::Scene scene(runtimeContext, "Generic");
                Runtime::GameObject* const host = scene.CreateGameObject("Host");
                Runtime::Component* const component =
                    host ? host->AddComponent(type->CreateInstance()) : nullptr;
                if (!component)
                {
                    roundTrips = false;
                    continue;
                }
                if (mutate)
                {
                    for (const Runtime::PropertyDescriptor* const descriptor :
                         Runtime::CollectProperties(*type))
                    {
                        roundTrips = descriptor->TrySet(
                            *component, MutatedPropertyValue(
                                *descriptor, descriptor->Get(*component))) && roundTrips;
                    }
                    for (const Runtime::PropertyDescriptor* const descriptor :
                         Runtime::CollectProperties(Runtime::Transform::StaticType()))
                    {
                        roundTrips = descriptor->TrySet(
                            host->GetTransform(), MutatedPropertyValue(
                                *descriptor, descriptor->Get(host->GetTransform()))) && roundTrips;
                    }
                }

                const std::string saved = Serialization::SceneSerializer::SaveToText(scene);
                deterministic = deterministic && !saved.empty() &&
                    saved == Serialization::SceneSerializer::SaveToText(scene);

                const std::unique_ptr<Runtime::Scene> loaded = loadText(saved);
                const Runtime::GameObject* const loadedHost =
                    loaded ? loaded->FindGameObject("Host") : nullptr;
                const Runtime::Component* loadedComponent = nullptr;
                if (loadedHost)
                {
                    for (const std::unique_ptr<Runtime::Component>& candidate :
                         loadedHost->GetAllComponents())
                    {
                        if (&candidate->GetComponentType() == type)
                        {
                            loadedComponent = candidate.get();
                        }
                    }
                }
                roundTrips = loadedComponent &&
                    SerializedPropertiesEqual(*type, *component, *loadedComponent) &&
                    SerializedPropertiesEqual(
                        Runtime::Transform::StaticType(), host->GetTransform(),
                        loadedHost->GetTransform()) && roundTrips;
            }
        }

        // 이름 표 밖의 enum 문자열은 그 컴포넌트에 대한 판정이다: 장면은 열리고, 값을 읽을 수
        // 없던 컴포넌트만 보존 데이터로 강등된다 — 그 JSON은 팩토리가 있는 다음 로드에서
        // 그대로 되살아난다. 오타 하나로 편집 중이던 장면 전체를 잃지 않는 자리다. 반면 다른
        // 회전 표기는 장면 전체의 뜻을 바꾸므로 여전히 로드를 무산시킨다. 아래 두 로드는
        // 의도된 오류·경고 로그를 남긴다.
        const std::unique_ptr<Runtime::Scene> badEnumScene = loadText(
            R"({"sceneName":"Bad","gameObjects":[{"name":"O","components":)"
            R"([{"type":"SpriteRenderer","drawMode":"diagonal"}]}]})");
        const Runtime::GameObject* const badEnumObject =
            badEnumScene ? badEnumScene->FindGameObject("O") : nullptr;
        const bool badEnumDegrades = badEnumObject &&
            badEnumObject->GetComponent<Runtime::SpriteRenderer>() == nullptr &&
            badEnumObject->GetComponent<Serialization::PreservedComponent>() != nullptr;
        const bool badRotationUnitFails = loadText(
            R"({"sceneName":"Bad","gameObjects":[{"name":"O","components":)"
            R"([{"type":"Transform","rotation":[0,0,0],"rotationUnit":"radians"}]}]})") == nullptr;

        // 구 파일: 레거시 aspectRatio 멤버는 이제 무시되지만 로드를 막지 않고, 나머지 값은
        // 그대로 읽힌다.
        const std::unique_ptr<Runtime::Scene> legacy = loadText(
            R"({"sceneName":"Legacy","gameObjects":[{"name":"Cam","components":)"
            R"([{"type":"Camera","orthographic":true,"aspectRatio":2.5,"priority":4}]}]})");
        const Runtime::GameObject* const legacyObject =
            legacy ? legacy->FindGameObject("Cam") : nullptr;
        const Runtime::Camera* const legacyCamera =
            legacyObject ? legacyObject->GetComponent<Runtime::Camera>() : nullptr;
        const bool legacyLoads = legacyCamera &&
            legacyCamera->GetProjectionMode() == Runtime::Camera::ProjectionMode::Orthographic &&
            legacyCamera->GetPriority() == 4;

        // 타일맵은 속성으로 기술할 수 없는 상태 — 타일 배열 — 를 스스로 쓴다. 저장은 레이어를
        // 목록으로 적고, 로더는 첫 레이어만 읽는다: 나중에 레이어가 여럿이 되어도 오늘 저장한
        // 장면이 그대로 읽히게 하는 자리다.
        Runtime::Scene tilemapScene(runtimeContext, "Tilemap");
        Runtime::GameObject* const tilemapObject = tilemapScene.CreateGameObject("Map");
        Runtime::TilemapRenderer* const tilemap =
            tilemapObject ? tilemapObject->AddComponent<Runtime::TilemapRenderer>() : nullptr;
        bool tilemapRoundTrips = tilemap != nullptr;
        if (tilemap)
        {
            tilemap->SetColumns(3);
            tilemap->SetRows(2);
            tilemap->SetCellSize({ 0.5f, 0.25f });
            tilemap->SetTileset(Assets::AssetReference::Parse("Textures/Tiles.png"));
            static_cast<void>(tilemap->SetTile(0, 0, 4));
            static_cast<void>(tilemap->SetTile(2, 1, 9));
        }
        const std::string tilemapText = Serialization::SceneSerializer::SaveToText(tilemapScene);
        const bool savesLayerList = tilemapText.find("\"layers\": [") != std::string::npos &&
            tilemapText.find("\"tiles\": [") != std::string::npos;

        const std::unique_ptr<Runtime::Scene> tilemapLoaded = loadText(tilemapText);
        const Runtime::GameObject* const loadedMap =
            tilemapLoaded ? tilemapLoaded->FindGameObject("Map") : nullptr;
        const Runtime::TilemapRenderer* const loadedTilemap =
            loadedMap ? loadedMap->GetComponent<Runtime::TilemapRenderer>() : nullptr;
        tilemapRoundTrips = tilemapRoundTrips && loadedTilemap &&
            loadedTilemap->GetColumns() == 3 && loadedTilemap->GetRows() == 2 &&
            loadedTilemap->GetCellSize().GetY() == 0.25f &&
            loadedTilemap->GetTileset().ToString() == "Textures/Tiles.png" &&
            loadedTilemap->GetTile(0, 0) == 4 && loadedTilemap->GetTile(2, 1) == 9 &&
            loadedTilemap->GetTile(1, 0) == Runtime::TilemapRenderer::EmptyTile;

        // 레이어가 여럿인 장면 — 아직 이 판이 만들지 않는 것 — 은 첫 레이어만 읽힌다.
        const std::unique_ptr<Runtime::Scene> multiLayer = loadText(
            R"({"sceneName":"Multi","gameObjects":[{"name":"Map","components":)"
            R"([{"type":"TilemapRenderer","columns":2,"rows":1,)"
            R"("layers":[{"tiles":[5,6]},{"tiles":[7,8]}]}]}]})");
        const Runtime::GameObject* const multiObject =
            multiLayer ? multiLayer->FindGameObject("Map") : nullptr;
        const Runtime::TilemapRenderer* const multiTilemap =
            multiObject ? multiObject->GetComponent<Runtime::TilemapRenderer>() : nullptr;
        const bool readsFirstLayerOnly = multiTilemap &&
            multiTilemap->GetTile(0, 0) == 5 && multiTilemap->GetTile(1, 0) == 6;

        return Expect(roundTrips, "every creatable type should round-trip the scene file") &&

            Expect(deterministic, "generic saves should stay deterministic") &&
            Expect(
                badEnumDegrades,
                "an enum string outside the name table should preserve that component, not fail "
                "the scene") &&
            Expect(
                badRotationUnitFails,
                "a rotation notation other than the declared constants should fail the load") &&
            Expect(legacyLoads, "a legacy member should be ignored without failing the load") &&
            Expect(savesLayerList, "a tilemap should save its tiles inside a layer list") &&
            Expect(tilemapRoundTrips, "a tilemap should round-trip its grid and tiles") &&
            Expect(readsFirstLayerOnly, "a scene with several layers should load the first one");
    }

    std::span<const GameEngine::Runtime::PropertyDescriptor> ProjectScriptProperties();

    /// <summary>
    /// 프로젝트가 정의하는 컴포넌트의 표본이다. SampleGame의 ScreenBounceBehaviour와 같은
    /// 모양 — MonoBehaviour 파생, 자기 타입·속성·생성 훅 선언 — 이지만, 테스트는 SampleGame을
    /// 링크할 수 없으므로 메커니즘을 이 테스트 로컬 타입으로 고정한다.
    /// </summary>
    class ProjectScriptComponent final : public GameEngine::Runtime::MonoBehaviour
    {
    public:
        [[nodiscard]] static const GameEngine::Runtime::ComponentType& StaticType()
        {
            static const GameEngine::Runtime::ComponentType type{
                "ProjectScriptComponent", &GameEngine::Runtime::MonoBehaviour::StaticType(),
                &ProjectScriptProperties,
                &GameEngine::Runtime::MakeComponentInstance<ProjectScriptComponent> };
            return type;
        }
        [[nodiscard]] const GameEngine::Runtime::ComponentType& GetComponentType() const override
        {
            return StaticType();
        }

        [[nodiscard]] float GetSpeed() const { return mSpeed; }
        void SetSpeed(const float speed) { mSpeed = speed; }
        [[nodiscard]] const GameEngine::Math::Vector2& GetHeading() const { return mHeading; }
        void SetHeading(const GameEngine::Math::Vector2& heading) { mHeading = heading; }

    private:
        [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Component> Clone() const override
        {
            return nullptr;
        }

        float mSpeed = 1.0f;
        GameEngine::Math::Vector2 mHeading{ 1.0f, 0.0f };
    };

    std::span<const GameEngine::Runtime::PropertyDescriptor> ProjectScriptProperties()
    {
        static const GameEngine::Runtime::PropertyDescriptor properties[] = {
            GameEngine::Runtime::MakeProperty<ProjectScriptComponent>(
                "speed", "Speed",
                &ProjectScriptComponent::GetSpeed, &ProjectScriptComponent::SetSpeed),
            GameEngine::Runtime::MakeProperty<ProjectScriptComponent>(
                "heading", "Heading",
                &ProjectScriptComponent::GetHeading, &ProjectScriptComponent::SetHeading),
        };
        return properties;
    }

    /// <summary>
    /// 프로젝트 컴포넌트가 엔진 타입과 같은 길로 직렬화에 참여하는지 고정한다:
    /// RegisterComponentType 한 줄로 등록하면 저장이 속성을 쓰고, 로드가 같은 값으로 되만들며,
    /// 등록을 되돌린 프로세스에서는 보존 경로가 데이터를 지킨다.
    /// </summary>
    bool RunProjectComponentRegistrationTests()
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);

        const bool registered =
            Serialization::RegisterComponentType(ProjectScriptComponent::StaticType());

        Runtime::Scene scene(runtimeContext, "Scripted");
        Runtime::GameObject* const host = scene.CreateGameObject("Host");
        ProjectScriptComponent* const script =
            host ? host->AddComponent<ProjectScriptComponent>() : nullptr;
        if (script)
        {
            script->SetSpeed(3.5f);
            script->SetHeading({ 0.25f, -0.75f });
        }

        const std::string saved = Serialization::SceneSerializer::SaveToText(scene);
        const bool propertiesWritten = script &&
            saved.find("\"type\": \"ProjectScriptComponent\"") != std::string::npos &&
            saved.find("\"speed\": 3.5") != std::string::npos &&
            saved.find("\"heading\": ") != std::string::npos;

        const auto loadSaved = [&saved, &runtimeContext]
        {
            return Serialization::SceneSerializer::LoadFromBytes(
                std::as_bytes(std::span(saved.data(), saved.size())), "Scripted.scene",
                runtimeContext);
        };
        const std::unique_ptr<Runtime::Scene> loaded = loadSaved();
        const Runtime::GameObject* const loadedHost =
            loaded ? loaded->FindGameObject("Host") : nullptr;
        const ProjectScriptComponent* const loadedScript =
            loadedHost ? loadedHost->GetComponent<ProjectScriptComponent>() : nullptr;
        const bool roundTripped = loadedScript &&
            loadedScript->GetSpeed() == 3.5f &&
            loadedScript->GetHeading() == Math::Vector2{ 0.25f, -0.75f } &&
            loadedScript->IsEnabled();

        // 등록을 되돌린다 — 이 표는 프로세스 전역이라, 남기면 다음 테스트가 물려받는다.
        const bool unregistered =
            Serialization::ComponentFactory::Unregister("ProjectScriptComponent") &&
            !Serialization::ComponentFactory::IsRegistered("ProjectScriptComponent");

        // 팩토리가 없는 프로세스 — 에디터가 이 장면을 여는 경우 — 는 보존 경로로 데이터를 지킨다.
        const std::unique_ptr<Runtime::Scene> preservedLoad = loadSaved();
        const Runtime::GameObject* const preservedHost =
            preservedLoad ? preservedLoad->FindGameObject("Host") : nullptr;
        const Serialization::PreservedComponent* const preserved =
            preservedHost ? preservedHost->GetComponent<Serialization::PreservedComponent>()
                          : nullptr;
        const bool preservedWithoutFactory = preserved &&
            preserved->GetPreservedTypeName() == "ProjectScriptComponent";

        return Expect(registered, "a project component type should register in one line") &&
            Expect(
                propertiesWritten,
                "a registered project component should be saved with its properties") &&
            Expect(roundTripped, "a project component should round-trip the scene file") &&
            Expect(unregistered, "a test registration should be undone for isolation") &&
            Expect(
                preservedWithoutFactory,
                "without its factory the component should be preserved as data");
    }

    /// <summary>
    /// 잠긴 카메라는 장면의 카메라 선택에 지지 않아야 한다. 씬 뷰가 이 성질 위에 서 있다:
    /// 장면에 카메라가 있어도, 뷰가 잠근 편집 카메라가 프레임의 카메라로 남아야 같은 장면을
    /// 다른 시점에서 볼 수 있다.
    /// </summary>
    /// <summary>
    /// 오버레이 패스는 세계와 분리된 화면 공간이다. 오버레이를 끈 프레임은 오버레이 draw를
    /// 버리고 세계의 draw는 그대로 받는다 — 씬 뷰가 게임의 HUD 없이 세계만 보는 방식이다.
    /// </summary>
    bool RunOverlayPassTests()
    {
        using namespace GameEngine;

        const auto makeText = [](Rendering::RenderFrameBuilder& builder, const Rendering::TextSpace space)
        {
            auto page = std::make_shared<Rendering::RasterizedTextImage>();
            page->id = Assets::MakeResourceId(Assets::ResourceIdDomain::Text, 0xFFFF);
            page->width = 2;
            page->height = 1;
            page->alphaPixels.assign(2, std::byte{ 0xFF });
            auto glyphs = std::make_shared<std::vector<Rendering::TextGlyphQuad>>();
            Rendering::TextGlyphQuad glyph;
            glyph.width = 2.0f;
            glyph.height = 1.0f;
            glyph.uWidth = 1.0f;
            glyph.vHeight = 1.0f;
            glyphs->push_back(glyph);
            Rendering::TextDraw draw;
            draw.pipeline = builder.AddPipeline({ Rendering::PipelineKind::Text });
            draw.page = std::move(page);
            draw.glyphs = std::move(glyphs);
            draw.space = space;
            return draw;
        };

        Rendering::RenderFrameBuilder withOverlay;
        const bool overlayAccepted = withOverlay.TryAddDraw(
            Rendering::RenderPass::Overlay, makeText(withOverlay, Rendering::TextSpace::Screen));
        const bool worldAccepted = withOverlay.TryAddDraw(
            Rendering::RenderPass::Transparent, makeText(withOverlay, Rendering::TextSpace::World));
        const Rendering::RenderFrame fullFrame = std::move(withOverlay).Build();

        Rendering::RenderFrameBuilder withoutOverlay;
        withoutOverlay.SetOverlayEnabled(false);
        const bool overlayDropped = !withoutOverlay.TryAddDraw(
            Rendering::RenderPass::Overlay, makeText(withoutOverlay, Rendering::TextSpace::Screen));
        const bool worldStillAccepted = withoutOverlay.TryAddDraw(
            Rendering::RenderPass::Transparent, makeText(withoutOverlay, Rendering::TextSpace::World));
        const Rendering::RenderFrame worldFrame = std::move(withoutOverlay).Build();

        return Expect(overlayAccepted && worldAccepted, "a default frame should accept both passes") &&
            Expect(
                fullFrame.GetDrawPackets(Rendering::RenderPass::Overlay).size() == 1,
                "the overlay draw should land in the overlay pass") &&
            Expect(overlayDropped, "a frame without overlay should drop overlay draws") &&
            Expect(worldStillAccepted, "a frame without overlay should still accept world draws") &&
            Expect(
                worldFrame.GetDrawPackets(Rendering::RenderPass::Overlay).empty() &&
                worldFrame.GetDrawPackets(Rendering::RenderPass::Transparent).size() == 1,
                "the built frame should carry only the world draw");
    }

    /// <summary>
    /// 계층 편집의 두 규칙이다. 부모를 지우면 자손도 함께 사라지고 — 루트로 떠오르지 않고 —
    /// 월드를 유지하며 재부모화하면 객체는 그 자리에 머문 채 로컬 값만 새 부모 기준으로 바뀐다.
    /// </summary>
    std::unique_ptr<GameEngine::App::IGameBootstrap> MakeRegistryTestBootstrap()
    {
        class TestBootstrap final : public GameEngine::App::IGameBootstrap
        {
        public:
            bool Initialize(GameEngine::Runtime::Game&, GameEngine::Platform::IWindow&) override
            {
                return true;
            }
        };
        return std::make_unique<TestBootstrap>();
    }

    /// <summary>
    /// 서로 다른 팩토리는 서로 다른 타입의 코드를 생성해 COMDAT 폴딩으로 합쳐지지 않게 한다.
    /// 등록부는 팩토리 포인터로 정체성을 구분하고 같은 팩토리의 중복 등록은 허용한다.
    /// </summary>
    std::unique_ptr<GameEngine::App::IGameBootstrap> MakeOtherRegistryTestBootstrap()
    {
        class OtherTestBootstrap final : public GameEngine::App::IGameBootstrap
        {
        public:
            bool Initialize(GameEngine::Runtime::Game&, GameEngine::Platform::IWindow&) override
            {
                return true;
            }

            /// <summary>
            /// 첫 번째 bootstrap과 다른 선택이다. 두 프로젝트가 엔진에 서로 다른 것을 요구하는
            /// 실제 모습이기도 하고, 두 팩토리의 기계어가 갈리는 이유이기도 하다.
            /// </summary>
            [[nodiscard]] std::optional<std::string> GetGraphicsBackendSetting() const override
            {
                return std::string("Direct3D");
            }
        };
        return std::make_unique<OtherTestBootstrap>();
    }

    /// <summary>
    /// 실행 파일 하나는 프로젝트 하나다: bootstrap 자리는 하나이고, 비어 있으면 보통의 게임이며,
    /// 두 번째 등록은 거절된다.
    /// </summary>
    bool RunGameBootstrapRegistryTests()
    {
        using GameEngine::App::GameBootstrapRegistry;

        // 레지스트리는 함수 포인터 동일성으로 판정하므로, 아래의 "다른 팩토리" 검사는 두 팩토리가
        // 실제로 다른 주소일 때만 레지스트리를 시험한다. 링커가 둘을 접으면 이 검사는 레지스트리가
        // 아니라 링커를 시험하는 것이 되므로, 그 사실이 실패 메시지에 먼저 드러나야 한다. 접힌
        // 채로 "두 번째 팩토리가 거절되지 않았다"고만 말하면 Release에서만 나는 이유를 알 수 없다.
        // 주소를 volatile를 거쳐 읽는다. 그냥 비교하면 컴파일러가 "서로 다른 함수는 서로 다른
        // 주소를 갖는다"는 추상 기계의 규칙으로 참이라고 접어 버리는데, 접는 주체는 컴파일러가
        // 아니라 링커라서 그 참은 거짓이 된다. volatile이 없으면 이 가드는 아무것도 잡지 못한다.
        GameBootstrapRegistry::Factory volatile firstFactory = &MakeRegistryTestBootstrap;
        GameBootstrapRegistry::Factory volatile secondFactory = &MakeOtherRegistryTestBootstrap;
        const bool factoriesAreDistinct = firstFactory != secondFactory;

        GameBootstrapRegistry::Reset();
        const bool emptyIsPlainGame = GameBootstrapRegistry::Create() == nullptr;
        const bool firstRegisters = GameBootstrapRegistry::Register(&MakeRegistryTestBootstrap);
        const bool created = GameBootstrapRegistry::Create() != nullptr;
        const bool sameAgainIsFine = GameBootstrapRegistry::Register(&MakeRegistryTestBootstrap);
        const bool secondRejected = !GameBootstrapRegistry::Register(&MakeOtherRegistryTestBootstrap);
        GameBootstrapRegistry::Reset();

        return Expect(emptyIsPlainGame, "no registration should mean no bootstrap") &&
            Expect(firstRegisters && created, "a registered factory should create the bootstrap") &&
            Expect(sameAgainIsFine, "registering the same factory twice should be accepted") &&
            Expect(factoriesAreDistinct,
                "the two test factories must build different bootstraps, or the linker folds them "
                "into one address and the check below tests nothing") &&
            Expect(secondRejected, "a second, different factory should be rejected");
    }

    class UpdateOrderProbe final : public GameEngine::Runtime::MonoBehaviour
    {
    public:
        std::vector<unsigned int>* log = nullptr;

    protected:
        void Update(float) override
        {
            if (log && GetGameObject())
            {
                log->push_back(GetGameObject()->GetInstanceId());
            }
        }

    private:
        [[nodiscard]] std::unique_ptr<GameEngine::Runtime::Component> Clone() const override
        {
            return nullptr;
        }
    };

    /// <summary>
    /// 객체는 장면에 들어온 순서로 업데이트되고 열거된다. 해시 순서에 기대면 같은 장면이 다른
    /// 프로세스에서 다른 순서로 돌 수 있다. 삭제와 추가를 거쳐도 남은 것들의 상대 순서는 그대로다.
    /// </summary>
    bool RunUpdateOrderTests()
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);
        Runtime::Scene scene(runtimeContext, "Order");

        std::vector<unsigned int> log;
        std::vector<unsigned int> created;
        for (int index = 0; index < 8; ++index)
        {
            Runtime::GameObject* const gameObject = scene.CreateGameObject("O" + std::to_string(index));
            if (!gameObject) return Expect(false, "objects should be created");
            auto probe = std::make_unique<UpdateOrderProbe>();
            probe->log = &log;
            static_cast<void>(gameObject->AddComponent(std::move(probe)));
            created.push_back(gameObject->GetInstanceId());
        }

        scene.Update(0.016f);
        const bool firstPassInOrder = log == created;

        // 가운데 하나를 지우고 하나를 더한다: 나머지는 순서를 지키고 새 것은 끝에 온다.
        static_cast<void>(scene.RemoveGameObject(created[3]));
        Runtime::GameObject* const late = scene.CreateGameObject("Late");
        auto lateProbe = std::make_unique<UpdateOrderProbe>();
        lateProbe->log = &log;
        static_cast<void>(late->AddComponent(std::move(lateProbe)));
        std::vector<unsigned int> expected = created;
        expected.erase(expected.begin() + 3);
        expected.push_back(late->GetInstanceId());

        log.clear();
        scene.Update(0.016f);
        const bool secondPassInOrder = log == expected;

        std::vector<unsigned int> roots;
        for (const Runtime::GameObject* const root : scene.GetRootGameObjects())
        {
            roots.push_back(root->GetInstanceId());
        }
        const bool enumerationInOrder = roots == expected;

        return Expect(firstPassInOrder, "objects should update in creation order") &&
            Expect(secondPassInOrder, "removal and addition should keep the remaining order") &&
            Expect(enumerationInOrder, "root enumeration should follow the same order");
    }

    bool RunHierarchyEditTests()
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);
        Runtime::Scene scene(runtimeContext, "Hierarchy");

        Runtime::GameObject* const parent = scene.CreateGameObject("Parent");
        Runtime::GameObject* const child = scene.CreateGameObject("Child");
        Runtime::GameObject* const grandchild = scene.CreateGameObject("Grandchild");
        Runtime::GameObject* const bystander = scene.CreateGameObject("Bystander");
        if (!parent || !child || !grandchild || !bystander)
        {
            return Expect(false, "the hierarchy test objects should build");
        }

        // 부모를 (2, 0, 0)으로 옮기고 90도 돌린 뒤, 월드 (2, 0, 1)에 있는 자식을 월드 유지로
        // 붙인다. 부모 기준으로는 (-1, 0, 0) — 이 엔진의 요 90도는 로컬 +x를 월드 -z로 보내므로,
        // 월드 +z에 있는 것은 로컬 -x다.
        parent->GetTransform().SetPosition({ 2.0f, 0.0f, 0.0f });
        parent->GetTransform().SetRotation({ 0.0f, 90.0f, 0.0f });
        child->GetTransform().SetPosition({ 2.0f, 0.0f, 1.0f });
        const Math::Vector3 worldBefore = child->GetTransform().GetWorldPosition();
        const bool reparented = child->GetTransform().SetParent(&parent->GetTransform(), true);
        const Math::Vector3 worldAfter = child->GetTransform().GetWorldPosition();
        const Math::Vector3 local = child->GetTransform().GetPosition();
        const auto isNear = [](const float a, const float b) { return std::abs(a - b) < 1e-3f; };
        const bool worldKept = isNear(worldBefore.GetX(), worldAfter.GetX()) &&
            isNear(worldBefore.GetY(), worldAfter.GetY()) &&
            isNear(worldBefore.GetZ(), worldAfter.GetZ());
        const bool localRewritten = isNear(local.GetX(), -1.0f) && isNear(local.GetY(), 0.0f) &&
            isNear(local.GetZ(), 0.0f);
        const bool rotationCancelled = isNear(child->GetTransform().GetRotation().GetY(), -90.0f);

        // 로컬을 유지하는 기본 동작은 그대로다: 로더가 기대는 계약이다.
        static_cast<void>(grandchild->GetTransform().SetParent(&child->GetTransform()));
        const bool localDefaultKept = grandchild->GetTransform().GetPosition().GetX() == 0.0f;

        const unsigned int childId = child->GetInstanceId();
        const unsigned int grandchildId = grandchild->GetInstanceId();
        const unsigned int bystanderId = bystander->GetInstanceId();
        const bool removed = scene.RemoveGameObject(parent->GetInstanceId());
        const bool descendantsGone =
            scene.GetGameObject(childId) == nullptr && scene.GetGameObject(grandchildId) == nullptr;
        const bool bystanderStays = scene.GetGameObject(bystanderId) != nullptr;

        return Expect(reparented, "reparenting under a valid parent should succeed") &&
            Expect(worldKept, "reparenting with worldPositionStays should keep the world position") &&
            Expect(localRewritten, "the local position should be rewritten relative to the parent") &&
            Expect(rotationCancelled, "the local rotation should cancel the parent's rotation") &&
            Expect(localDefaultKept, "reparenting without worldPositionStays should keep local values") &&
            Expect(removed, "removing the parent should succeed") &&
            Expect(descendantsGone, "removing a parent should remove its descendants") &&
            Expect(bystanderStays, "removing a parent should leave unrelated objects alone");
    }

    bool RunLockedCameraTests()
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);

        Runtime::Scene scene(runtimeContext, "Camera");
        Runtime::GameObject* const cameraObject = scene.CreateGameObject("SceneCamera");
        Runtime::Camera* const sceneCamera =
            cameraObject ? cameraObject->AddComponent<Runtime::Camera>() : nullptr;
        if (sceneCamera)
        {
            sceneCamera->SetPriority(100);
        }

        // 잠긴 카메라를 알아볼 표식: 장면 카메라가 결코 만들지 않는 지우기 색이다.
        Rendering::CameraRenderData lockedCamera;
        lockedCamera.view = Math::Matrix4x4::Identity();
        lockedCamera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(2.0f, 2.0f, 0.1f, 10.0f);
        lockedCamera.clearColor = { 0.125f, 0.25f, 0.375f, 1.0f };

        Rendering::RenderFrameBuilder builder;
        builder.LockCamera(lockedCamera);

        // 장면의 카메라가 하듯 SetCamera로 덮으려 해 본다. 잠겨 있으므로 무시되어야 한다.
        Rendering::CameraRenderData sceneProvided;
        sceneProvided.clearColor = { 1.0f, 0.0f, 0.0f, 1.0f };
        builder.SetCamera(sceneProvided);

        const Rendering::RenderFrame frame = std::move(builder).Build();
        const bool lockedCameraSurvives = frame.GetCamera() &&
            frame.GetCamera()->clearColor.r == 0.125f &&
            frame.GetCamera()->clearColor.g == 0.25f;

        return Expect(sceneCamera != nullptr, "the scene camera should build") &&
            Expect(lockedCameraSurvives, "a locked camera should ignore later SetCamera calls");
    }

    /// <summary>
    /// 프레임의 조명 계약을 고정한다: 광원은 상한까지만 실리고, 기술할 수 없는 광원은 거부되며,
    /// 주변광은 합쳐진다. 그리고 Light 컴포넌트가 장면 파일을 왕복한다.
    /// </summary>
    bool RunLightingTests()
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        Rendering::LightRenderData sun;
        sun.kind = Rendering::LightKind::Directional;
        sun.direction = { 0.0f, -1.0f, 0.0f };
        Rendering::LightRenderData lamp;
        lamp.kind = Rendering::LightKind::Point;
        lamp.position = { 1.0f, 2.0f, 3.0f };
        lamp.range = 4.0f;
        Rendering::LightRenderData zeroDirection;
        zeroDirection.direction = { 0.0f, 0.0f, 0.0f };
        Rendering::LightRenderData zeroRange = lamp;
        zeroRange.range = 0.0f;

        const bool rejectsInvalid = !builder.AddLight(zeroDirection) && !builder.AddLight(zeroRange);
        std::size_t accepted = 0;
        for (std::size_t index = 0; index < Rendering::MaxFrameLights + 2; ++index)
        {
            if (builder.AddLight(index % 2 == 0 ? sun : lamp))
            {
                ++accepted;
            }
        }
        builder.AddAmbientLight({ 0.1f, 0.2f, 0.3f, 1.0f });
        builder.AddAmbientLight({ 0.1f, 0.1f, 0.1f, 1.0f });
        const Rendering::RenderFrame frame = std::move(builder).Build();
        Rendering::RenderFrameValidationResult validation;
        const bool frameValid = frame.TryValidate(validation).has_value();
        const bool ambientSummed = std::abs(frame.GetAmbientLight().r - 0.2f) < 1e-5f &&
            std::abs(frame.GetAmbientLight().b - 0.4f) < 1e-5f;

        // 광원이 없는 프레임은 셰이더 상수에서 빛 0개, 주변광 검정으로 저장된다.
        Rendering::MeshShading shading;
        Rendering::MeshConstants constants;
        Rendering::StoreMeshLighting(shading, constants);
        const bool darkByDefault = constants.ambientAndLightCount.w == 0.0f &&
            constants.lights[0].colorAndRange.x == 0.0f;

        // 왕복: Light 컴포넌트는 파일을 지나 같은 값으로 돌아온다.
        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);
        Runtime::Scene scene(runtimeContext, "Lit");
        Runtime::GameObject* const lampObject = scene.CreateGameObject("Lamp");
        Runtime::Light* const lampLight = lampObject ? lampObject->AddComponent<Runtime::Light>() : nullptr;
        if (lampLight)
        {
            lampLight->SetKind(Runtime::Light::Kind::Point);
            lampLight->SetColor({ 0.5f, 0.25f, 0.125f, 1.0f });
            lampLight->SetIntensity(2.0f);
            lampLight->SetRange(7.0f);
        }
        const std::string savedText = Serialization::SceneSerializer::SaveToText(scene);
        const std::unique_ptr<Runtime::Scene> loaded = Serialization::SceneSerializer::LoadFromBytes(
            std::as_bytes(std::span(savedText.data(), savedText.size())), "Lit.scene", runtimeContext);
        bool roundTripped = false;
        if (loaded)
        {
            for (const Runtime::GameObject* object : loaded->GetRootGameObjects())
            {
                if (const Runtime::Light* light = object->GetComponent<Runtime::Light>())
                {
                    roundTripped = light->GetKind() == Runtime::Light::Kind::Point &&
                        light->GetColor().r == 0.5f && light->GetIntensity() == 2.0f &&
                        light->GetRange() == 7.0f;
                }
            }
        }

        return Expect(rejectsInvalid, "lights a shader cannot use should be rejected") &&
            Expect(accepted == Rendering::MaxFrameLights, "a frame should carry at most MaxFrameLights lights") &&
            Expect(frameValid, "a frame with lights should validate") &&
            Expect(ambientSummed, "ambient lights should add up") &&
            Expect(darkByDefault, "a frame without lights should store no light") &&
            Expect(roundTripped, "a Light component should round-trip through the scene file");
    }

    /// <summary>
    /// 투명 draw의 순서를 고정한다: 같은 sortingOrder 안에서는 카메라에서 먼 것이 먼저, 같은 깊이면
    /// instanceId 순이다. 불투명 draw는 깊이를 보지 않는다.
    /// </summary>
    bool RunTransparentDepthSortTests()
    {
        using namespace GameEngine;

        Rendering::RenderFrameBuilder builder;
        Rendering::CameraRenderData camera;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(4.0f, 4.0f, 0.1f, 100.0f);
        builder.SetCamera(camera);

        auto texture = std::make_shared<Assets::TextureData>();
        texture->id = 41;
        texture->width = 1;
        texture->height = 1;
        texture->pixels.resize(4, std::byte{ 255 });
        const Rendering::MaterialHandle material = builder.AddMaterial({ texture });
        const Rendering::PipelineHandle pipeline = builder.AddPipeline({ Rendering::PipelineKind::Sprite });

        const auto addSprite = [&](const float z, const int sortingOrder, const unsigned int instanceId)
        {
            Rendering::SpriteDraw draw;
            draw.pipeline = pipeline;
            draw.material = material;
            draw.localToWorld = Math::Matrix4x4::CreateTranslation({ 0.0f, 0.0f, z });
            static_cast<void>(builder.TryAddDraw(
                Rendering::RenderPass::Transparent, draw, sortingOrder, instanceId));
        };
        addSprite(1.0f, 0, 1);   // 가깝다
        addSprite(9.0f, 0, 2);   // 멀다 → 먼저
        addSprite(5.0f, 0, 3);
        addSprite(5.0f, 0, 0);   // 같은 깊이 → id 순
        addSprite(0.5f, -1, 4);  // 낮은 sortingOrder가 깊이보다 앞선다

        const Rendering::RenderFrame frame = std::move(builder).Build();
        Rendering::RenderFrameValidationResult validation;
        const bool valid = frame.TryValidate(validation).has_value();
        const std::vector<Rendering::DrawPacket>& packets =
            frame.GetDrawPackets(Rendering::RenderPass::Transparent);
        std::vector<unsigned int> order;
        for (const Rendering::DrawPacket& packet : packets)
        {
            order.push_back(packet.instanceId);
        }
        const std::vector<unsigned int> expected{ 4, 2, 0, 3, 1 };

        return Expect(valid, "a depth-sorted frame should validate") &&
            Expect(order == expected, "transparent draws should be ordered far to near within a sorting order");
    }

    /// <summary>
    /// AddScene의 발급 규칙을 고정한다: id는 프로젝트 파일이 쓸 수 없는 예약 범위에서 나오고,
    /// 등록 쪽은 그 범위를 거부하며, null 장면은 실패로 답한다. 이 규칙들이 어긋나면 에디터가
    /// 추가한 장면과 프로젝트의 장면이 같은 id를 두고 다투게 된다.
    /// </summary>
    bool RunAddSceneTests(const std::filesystem::path& root)
    {
        using namespace GameEngine;

        Runtime::ObjectRegistry objects;
        const Runtime::Input input;
        Runtime::RuntimeContext runtimeContext(objects, input);
        Runtime::SceneManager manager(runtimeContext);

        const unsigned int firstId =
            manager.AddScene(std::make_unique<Runtime::Scene>(runtimeContext, "First"));
        const unsigned int secondId =
            manager.AddScene(std::make_unique<Runtime::Scene>(runtimeContext, "Second"));
        const bool issuesDistinctReservedIds =
            firstId >= 0x40000000u && secondId >= 0x40000000u && firstId != secondId;

        const Runtime::Scene* const firstScene = manager.GetScene(firstId);
        const bool addedSceneIsActive = firstScene && firstScene->GetName() == "First" &&
            manager.IsSceneLoaded(firstId);
        const bool addedSceneUnloads =
            manager.UnloadScene(firstId) && !manager.IsSceneLoaded(firstId);

        const bool rejectsNull = manager.AddScene(nullptr) == 0;

        // 예약 범위의 id를 적은 프로젝트는 등록 단계에서 거부된다.
        const std::filesystem::path projectRoot = root / "AddScene";
        const bool wrote = WriteFile(projectRoot / "Scenes" / "Reserved.scene", "{}");
        const GameEngine::Platform::DirectoryContentSource content(projectRoot);
        const std::unordered_map<unsigned int, std::filesystem::path> reservedPaths{
            { 0x40000000u, "Scenes/Reserved.scene" } };
        const std::unordered_map<unsigned int, std::filesystem::path> ordinaryPaths{
            { 3, "Scenes/Reserved.scene" } };
        const bool rejectsReservedIds =
            wrote && !manager.RegisterScenePaths(content, reservedPaths) &&
            manager.RegisterScenePaths(content, ordinaryPaths);

        return Expect(issuesDistinctReservedIds,
                   "added scenes should get distinct ids from the reserved range") &&
            Expect(addedSceneIsActive, "an added scene should be active under its issued id") &&
            Expect(addedSceneUnloads, "an added scene should unload by its issued id") &&
            Expect(rejectsNull, "adding a null scene should fail") &&
            Expect(rejectsReservedIds,
                "registering a scene path with a reserved id should be refused");
    }

    /// <summary>
    /// 업데이트 중 요청한 장면 로드는 큐에 들어가 업데이트 끝에서 완료된다.
    /// Game::LoadScene의 즉시 반환 뒤에 완료되는 장면도 다음 렌더 프레임 전에 에셋을 미리 읽어야
    /// 한다. 렌더 프론트엔드에서 장면 콘텐츠 전체를 동기 임포트하는 지연을 피하는 계약이다.
    /// </summary>
    bool RunDeferredSceneLoadTest(const std::filesystem::path& root)
    {
        static const TestMeshImporter meshImporter;
        const bool registered =
            GameEngine::Assets::AssetImporterRegistry::Register(".testmesh", meshImporter);

        const std::filesystem::path projectRoot = root / "DeferredLoad";
        const bool wrote =
            WriteFile(projectRoot / "DeferredLoad.gameproject", "{}") &&
            WriteFile(
                projectRoot / "Scenes" / "First.scene",
                R"({"sceneName":"First","gameObjects":[]})") &&
            WriteFile(
                projectRoot / "Scenes" / "Second.scene",
                R"({"sceneName":"Second","gameObjects":[{"name":"Rock","components":)"
                R"([{"type":"MeshRenderer","mesh":"Models/Rock.testmesh"}]}]})") &&
            WriteFile(projectRoot / "Models" / "Rock.testmesh", "mesh-bytes");

        const GameEngine::Platform::DirectoryContentSource projectContent(projectRoot);
        GameEngine::Runtime::Game game{ nullptr, nullptr };
        game.SetSceneLoader(GameEngine::Serialization::SceneSerializer::MakeSceneLoader());
        const std::unordered_map<unsigned int, std::filesystem::path> scenePaths{
            { 1, "Scenes/First.scene" }, { 2, "Scenes/Second.scene" } };
        const bool initialized = wrote && game.Initialize(projectContent, scenePaths) &&
            game.LoadScene(1) == GameEngine::Runtime::SceneLoadResult::Loaded;

        SceneLoadingBehaviour* loader = nullptr;
        if (initialized)
        {
            if (GameEngine::Runtime::Scene* const scene = game.GetSceneManager().GetScene(1))
            {
                if (GameEngine::Runtime::GameObject* const host = scene->CreateGameObject("Loader"))
                {
                    loader = host->AddComponent<SceneLoadingBehaviour>();
                }
            }
        }
        if (loader)
        {
            loader->game = &game;
            loader->sceneToLoad = 2;
        }

        // The first scene references nothing, so nothing is resident before the update.
        const bool nothingResidentBefore =
            game.GetAssetDatabase().GetLoadedPayloadCount() == 0;

        game.Update(1.0f / 60.0f);

        const bool wasQueued = loader &&
            loader->result == GameEngine::Runtime::SceneLoadResult::Queued;
        const bool addDuringUpdateRefused = loader && loader->addSceneResult == 0;
        const bool sceneLoaded = game.GetSceneManager().IsSceneLoaded(2);
        const bool assetsPreloaded = game.GetAssetDatabase().GetLoadedPayloadCount() == 1;


        return Expect(registered, "the test mesh format should register") &&
            Expect(initialized, "the deferred-load project should initialize") &&
            Expect(loader != nullptr, "the loading behaviour should attach") &&
            Expect(nothingResidentBefore, "an empty scene should load no assets") &&
            Expect(wasQueued, "a load requested during an update should be queued") &&
            Expect(
                addDuringUpdateRefused,
                "adding a scene while scenes update should be refused") &&
            Expect(sceneLoaded, "the queued scene should be loaded by the end of that update") &&
            Expect(
                assetsPreloaded,
                "a scene loaded from inside an update should have its assets pre-loaded");
    }
}

bool RunProjectBuilderTests()
{
    const TestSupport::RegistryScope registries;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("GameEngineProjectBuilderTests-" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const std::filesystem::path contentRoot = root / "Project" / "Content";
    const std::filesystem::path compiledRoot = root / "Compiled";
    const std::filesystem::path outputRoot = root / "Build" / "Editor";
    constexpr std::string_view settings = R"({
  "projectName": "BuildTest",
  "window": { "width": 1280, "height": 720 },
  "targetFrameRate": 60,
  "initialSceneId": 0,
  "scenes": [ { "id": 0, "path": "Scenes/Main.scene" } ],
  "graphicsApi": "Auto"
})";
    bool passed = true;
    // 패키징에는 에셋마다 정체성이 있어야 한다. 그것이 없는 프로젝트는 매니페스트를 쓸 수 없고,
    // 에디터가 열어 주기 전까지 그 상태다 — 여기서는 에디터를 세울 수 없으니 손으로 준다.
    const auto identity = [](const char* const digits)
    {
        return std::string(R"({"guid": ")") + digits + R"("})";
    };
    passed &= Expect(WriteFile(contentRoot / "BuildTest.gameproject", settings), "write settings");
    passed &= Expect(
        WriteFile(contentRoot / "BuildTest.gameproject.meta",
            identity("000000000000000000000000000000b1")),
        "write project identity");
    passed &= Expect(WriteFile(contentRoot / "Scenes/Main.scene", "{}"), "write scene");
    passed &= Expect(
        WriteFile(contentRoot / "Scenes/Main.scene.meta",
            identity("000000000000000000000000000000b2")),
        "write scene identity");
    passed &= Expect(WriteFile(contentRoot / "Scenes/Unused.scene", "{}"), "write unused scene");
    passed &= Expect(
        WriteFile(contentRoot / "Scenes/Unused.scene.meta",
            identity("000000000000000000000000000000b3")),
        "write unused scene identity");
    passed &= Expect(WriteFile(contentRoot / "Textures/Albedo.png", "png"), "write texture");
    passed &= Expect(
        WriteFile(contentRoot / "Textures/Albedo.png.meta",
            R"({"guid":"000000000000000000000000000000b4",)"
            R"("pixelsPerUnit":50,"border":[2,3,4,5],)"
            R"("sheet":{"columns":4,"rows":2,"frameCount":7,"frameRate":24}})"),
        "write sprite metadata");
    passed &= Expect(WriteFile(contentRoot / "Meshes/Model.fbx", "fbx"), "write mesh");
    passed &= Expect(
        WriteFile(contentRoot / "Meshes/Model.fbx.meta",
            identity("000000000000000000000000000000b5")),
        "write mesh identity");
    passed &= Expect(WriteFile(contentRoot / "Fonts/Interface.otf", "font"), "write font");
    passed &= Expect(
        WriteFile(contentRoot / "Fonts/Interface.otf.meta",
            identity("000000000000000000000000000000b6")),
        "write font identity");
    passed &= Expect(WriteFile(contentRoot / "Audio/Click.wav", "audio"), "write audio clip");
    passed &= Expect(
        WriteFile(contentRoot / "Audio/Click.wav.meta",
            identity("000000000000000000000000000000b7")),
        "write audio identity");
    passed &= Expect(WriteFile(contentRoot / "Ignored.txt", "ignored"), "write ignored file");
    passed &= Expect(WriteFile(compiledRoot / "BuildTest.exe", "executable"), "write executable");
    passed &= Expect(WriteFile(compiledRoot / "BuildTest.pdb", "symbols"), "write symbols");
    passed &= Expect(WriteFile(compiledRoot / "Dependency.dll", "dependency"), "write dependency");
    passed &= Expect(
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/Mesh.hlsl", "mesh shader"),
        "write mesh shader");
    passed &= Expect(
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/Sprite.hlsl", "sprite shader"),
        "write sprite shader");
    passed &= Expect(
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/Text.hlsl", "text shader"),
        "write text shader");
    passed &= Expect(
        WriteFile(compiledRoot / "Rendering/Direct3D/Shaders/SkinnedMesh.hlsl", "skinned mesh shader"),
        "write skinned mesh shader");
    if (!passed)
    {
        std::filesystem::remove_all(root, error);
        return false;
    }
    passed &= Expect(
        GameEngine::App::ProjectFile::Load(contentRoot / "BuildTest.gameproject").has_value(),
        "matching .gameproject filename should load");
    passed &= Expect(
        WriteFile(contentRoot / "WrongName.gameproject", settings),
        "write mismatched project filename");
    passed &= Expect(
        !GameEngine::App::ProjectFile::Load(contentRoot / "WrongName.gameproject"),
        "project filename must match projectName");
    std::filesystem::remove(contentRoot / "WrongName.gameproject", error);

    const std::filesystem::path createdRoot = root / "CreatedProject";
    const std::filesystem::path createdProjectPath =
        createdRoot / "CreatedProject.gameproject";
    const std::optional<GameEngine::App::ProjectFileData> createdProject =
        GameEngine::App::ProjectFile::Create(createdProjectPath);
    passed &= Expect(createdProject.has_value(), "create a new project");
    passed &= Expect(
        createdProject && createdProject->settings.projectName == L"CreatedProject",
        "derive the project name from the file name");
    passed &= Expect(
        createdProject && createdProject->settings.initialSceneId == 0 &&
        createdProject->settings.scenePaths.contains(0) &&
        createdProject->settings.scenePaths.at(0) == "Scenes/Main.scene",
        "configure the default scene");
    passed &= Expect(
        createdProject &&
        createdProject->settings.graphicsApi == GameEngine::Rendering::AutomaticGraphicsBackendId,
        "new projects should select the newest supported graphics API automatically");
    passed &= Expect(
        std::filesystem::is_regular_file(createdRoot / "Scenes/Main.scene"),
        "create the default scene file");
    // 등록은 하되 <b>돌아온 값을 묻지 않는다.</b> 그 값은 「이번에 처음 넣었는가」이고, 표는
    // 프로세스 전역이라 앞선 스위트가 이미 넣었으면 거짓이다 — 스위트마다 프로세스를 띄우는
    // CTest에서는 참이고, 전체를 한 프로세스로 돌리면 반드시 거짓이 되어, 실행 방법이 시험의
    // 답을 바꾼다.
    //
    // 이 시험이 필요로 하는 것은 「누가 넣었는가」가 아니라 「지금 표에 있는가」다. 그것은 몇
    // 번을 부르든 참이며, 진짜로 등록이 안 된 날에는 거짓이다.
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());
    for (const GameEngine::Runtime::ComponentType* const type :
         GameEngine::Serialization::EngineComponentTypes())
    {
        passed &= Expect(
            GameEngine::Serialization::ComponentFactory::IsRegistered(type->GetName()),
            "every engine component type should be in the factory table");
    }
    // Deserialization needs a registry to give the objects it creates, not a whole Game.
    GameEngine::Runtime::ObjectRegistry runtimeObjects;
    const GameEngine::Runtime::Input runtimeInput;
    GameEngine::Runtime::RuntimeContext runtimeContext(runtimeObjects, runtimeInput);
    const GameEngine::Platform::DirectoryContentSource createdContent(createdRoot);
    const auto loadScene = [&createdContent, &runtimeContext](
        const std::filesystem::path& relativePath)
    {
        std::vector<std::byte> sceneBytes;
        return createdContent.Read(relativePath, sceneBytes)
            ? GameEngine::Serialization::SceneSerializer::LoadFromBytes(
                  sceneBytes, relativePath, runtimeContext)
            : nullptr;
    };
    const std::unique_ptr<GameEngine::Runtime::Scene> createdScene =
        loadScene("Scenes/Main.scene");
    passed &= Expect(createdScene != nullptr, "load the generated default scene");

    constexpr std::string_view textSceneJson = R"({
  "gameObjects": [{
    "name": "Text",
    "components": [
      { "type": "Transform" },
      {
        "type": "TextRenderer",
        "text": "한글 text",
        "fontFamily": "Segoe UI",
        "fontSize": 28.0,
        "space": "screen",
        "alignment": "center",
        "maxWidth": 320.0
      }
    ]
  }]
})";
    const std::filesystem::path textScenePath = createdRoot / "Scenes/Text.scene";
    passed &= Expect(WriteFile(textScenePath, textSceneJson), "write a text renderer scene");
    const std::unique_ptr<GameEngine::Runtime::Scene> textScene = loadScene("Scenes/Text.scene");
    const GameEngine::Runtime::GameObject* textObject = textScene
        ? textScene->FindGameObject("Text")
        : nullptr;
    const GameEngine::Runtime::TextRenderer* textRenderer = textObject
        ? textObject->GetComponent<GameEngine::Runtime::TextRenderer>()
        : nullptr;
    passed &= Expect(
        textRenderer && textRenderer->GetText() == "한글 text" &&
        textRenderer->GetFontSize() == 28.0f &&
        textRenderer->GetSpace() == GameEngine::Runtime::TextRenderer::Space::Screen &&
        textRenderer->GetAlignment() == GameEngine::Runtime::TextRenderer::Alignment::Center &&
        textRenderer->GetMaxWidth() == 320.0f,
        "deserialize UTF-8 text renderer properties");
    std::filesystem::remove(textScenePath, error);

    GameEngine::Assets::AssetDatabase createdDatabase;
    passed &= Expect(
        createdDatabase.Refresh(createdContent),
        "refresh a newly created project's asset database");
    passed &= Expect(
        createdDatabase.GetAssets().size() == 2,
        "register the new project descriptor and default scene as assets");
    passed &= Expect(
        !GameEngine::App::ProjectFile::Create(createdProjectPath),
        "do not overwrite an existing project");

    const std::filesystem::path occupiedRoot = root / "OccupiedProject";
    passed &= Expect(
        WriteFile(occupiedRoot / "Scenes/Main.scene", "existing scene"),
        "write an existing default scene");
    passed &= Expect(
        !GameEngine::App::ProjectFile::Create(
            occupiedRoot / "OccupiedProject.gameproject"),
        "do not overwrite an existing default scene");
    passed &= Expect(
        !std::filesystem::exists(occupiedRoot / "OccupiedProject.gameproject"),
        "do not leave a partial project descriptor after create failure");

    const std::filesystem::path temporaryOccupiedRoot = root / "TemporaryOccupiedProject";
    const std::filesystem::path temporaryProjectPath =
        temporaryOccupiedRoot / "TemporaryOccupiedProject.gameproject.tmp";
    passed &= Expect(
        WriteFile(temporaryProjectPath, "preserve temporary file"),
        "write an existing temporary project file");
    passed &= Expect(
        !GameEngine::App::ProjectFile::Create(
            temporaryOccupiedRoot / "TemporaryOccupiedProject.gameproject"),
        "do not overwrite an existing temporary file");
    passed &= Expect(
        std::filesystem::file_size(temporaryProjectPath, error) == 23 && !error,
        "preserve an existing temporary file");

    // 새 장면 생성은 파일을 만들고 프로젝트의 장면 등록에도 반영해야 한다.
    {
        const std::filesystem::path newScenePath = createdRoot / "Scenes" / "Level1.scene";
        const std::optional<GameEngine::App::ProjectFileData> afterAdd =
            createdProject
                ? GameEngine::App::ProjectFile::AddScene(*createdProject, newScenePath)
                : std::nullopt;
        passed &= Expect(afterAdd.has_value(), "add a scene to an existing project");
        passed &= Expect(
            std::filesystem::is_regular_file(newScenePath), "write the new scene file");

        // 등록이 이 기능의 본체다. 계층이 읽는 것이 바로 이 표다.
        passed &= Expect(
            afterAdd && afterAdd->settings.scenePaths.contains(1) &&
                afterAdd->settings.scenePaths.at(1) == "Scenes/Level1.scene",
            "register the new scene in the project descriptor");
        // 비어 있던 가장 작은 자리가 배정된다. Main이 0을 쓰고 있으므로 1이다.
        passed &= Expect(
            afterAdd && GameEngine::App::ProjectFile::ChooseSceneId(afterAdd->settings) == 2,
            "the next scene should take the next free id");
        // 시작 장면은 그대로다: 장면을 만들었다고 게임이 다른 곳에서 시작하면 놀란다.
        passed &= Expect(
            afterAdd && afterAdd->settings.initialSceneId == 0,
            "adding a scene should not change which scene the game starts in");

        // 빈 장면을 열면 화면이 비어 보이고, 사람은 그것을 고장으로 읽는다. 그래서 카메라가 있다.
        const std::string newSceneText = TestSupport::ReadFile(newScenePath);
        passed &= Expect(
            newSceneText.find("\"sceneName\": \"Level1\"") != std::string::npos,
            "name the new scene after its file");
        passed &= Expect(
            newSceneText.find("\"type\": \"Camera\"") != std::string::npos,
            "give the new scene a camera so that opening it shows something");

        // 같은 파일을 다시 만들 수는 없다. 덮어쓰면 사용자가 그 장면에 넣어 둔 것이 사라진다.
        passed &= Expect(
            afterAdd &&
                !GameEngine::App::ProjectFile::AddScene(*afterAdd, newScenePath),
            "do not overwrite an existing scene file");

        // 확장자가 다르면 거부한다 — 프로젝트가 그 파일을 장면으로 읽지 못한다.
        passed &= Expect(
            afterAdd &&
                !GameEngine::App::ProjectFile::AddScene(
                    *afterAdd, createdRoot / "Scenes" / "NotAScene.txt"),
            "refuse a path that is not a .scene file");

        // 프로젝트 밖은 거부한다: 경로는 프로젝트 기준 상대 경로로 배포되므로, 밖을 가리키면
        // 빌드된 게임에 그 파일이 없다.
        passed &= Expect(
            afterAdd &&
                !GameEngine::App::ProjectFile::AddScene(
                    *afterAdd, root / "Outside.scene"),
            "refuse a scene outside the project folder");

        // 같은 파일을 다른 ID로 두 번 등록하면 계층에 같은 장면이 두 줄로 선다.
        passed &= Expect(
            afterAdd &&
                !GameEngine::App::ProjectFile::WithScene(
                    afterAdd->settings, "Scenes/Level1.scene", 7),
            "refuse to register the same scene file twice");
        passed &= Expect(
            afterAdd &&
                !GameEngine::App::ProjectFile::WithScene(
                    afterAdd->settings, "Scenes/Other.scene", 0),
            "refuse an id that is already in use");

        // 빈 자리를 메운다: 0과 2가 쓰이고 있으면 다음은 1이다. ID는 이름이 아니라 자리다.
        GameEngine::App::ProjectSettings gapped;
        gapped.scenePaths.emplace(0, "Scenes/Main.scene");
        gapped.scenePaths.emplace(2, "Scenes/Third.scene");
        passed &= Expect(
            GameEngine::App::ProjectFile::ChooseSceneId(gapped) == 1,
            "fill the lowest free id rather than growing without end");

        // 왕복: 다시 읽은 프로젝트가 두 장면을 모두 보여야 한다. 계층이 보게 될 목록이 이것이다.
        const std::optional<GameEngine::App::ProjectFileData> reopened =
            GameEngine::App::ProjectFile::Load(createdProjectPath);
        passed &= Expect(
            reopened && reopened->settings.scenePaths.size() == 2 &&
                reopened->settings.scenePaths.contains(0) &&
                reopened->settings.scenePaths.contains(1),
            "a reopened project should list both scenes");
        passed &= Expect(
            reopened && reopened->settings.projectName == L"CreatedProject" &&
                reopened->settings.initialSceneId == 0 &&
                reopened->settings.windowWidth == 1280,
            "rewriting the descriptor should preserve the rest of the settings");
    }

    // 장면 이름 변경과 삭제는 파일과 프로젝트의 장면 등록을 함께 갱신해야 한다.
    {
        const std::optional<GameEngine::App::ProjectFileData> base =
            GameEngine::App::ProjectFile::Load(createdProjectPath);
        const std::filesystem::path levelPath = createdRoot / "Scenes" / "Level1.scene";
        const std::filesystem::path renamedPath = createdRoot / "Scenes" / "Arena.scene";

        // 등록된 파일이 실제로 있는지 물을 수 있어야, 없는 것을 없다고 보일 수 있다.
        passed &= Expect(
            base && GameEngine::App::ProjectFile::SceneFileExists(*base, 1),
            "a registered scene whose file exists should report that it exists");

        // 이름 바꾸기: 파일과 등록이 함께 옮겨 가고, id는 그대로다.
        const std::optional<GameEngine::App::ProjectFileData> renamed = base
            ? GameEngine::App::ProjectFile::RenameScene(*base, 1, renamedPath)
            : std::nullopt;
        passed &= Expect(renamed.has_value(), "rename a scene");
        passed &= Expect(
            !std::filesystem::exists(levelPath) && std::filesystem::is_regular_file(renamedPath),
            "renaming should move the file");
        passed &= Expect(
            renamed && renamed->settings.scenePaths.contains(1) &&
                renamed->settings.scenePaths.at(1) == "Scenes/Arena.scene",
            "renaming should move the registration with the file");
        passed &= Expect(
            renamed && renamed->settings.initialSceneId == 0,
            "renaming should not move which scene the game starts in");

        // 장면의 이름은 파일 이름이다. 다시 읽으면 파일 안의 옛 이름이 아니라 새 파일명이 온다.
        GameEngine::Runtime::ObjectRegistry renameRegistry;
        GameEngine::Runtime::Input renameInput;
        GameEngine::Runtime::RuntimeContext renameContext{ renameRegistry, renameInput };
        const std::string renamedText = TestSupport::ReadFile(renamedPath);
        const std::vector<std::byte> renamedBytes(
            reinterpret_cast<const std::byte*>(renamedText.data()),
            reinterpret_cast<const std::byte*>(renamedText.data() + renamedText.size()));
        const std::unique_ptr<GameEngine::Runtime::Scene> renamedScene =
            GameEngine::Serialization::SceneSerializer::LoadFromBytes(
                renamedBytes, renamedPath, renameContext);
        passed &= Expect(
            renamedScene && renamedScene->GetName() == "Arena",
            "a scene should take its name from its file, not from the name written inside it");

        // 같은 이름으로 바꾸는 것은 아무 일도 아니며 실패가 아니다.
        passed &= Expect(
            renamed && GameEngine::App::ProjectFile::RenameScene(*renamed, 1, renamedPath),
            "renaming a scene to the name it already has should be accepted as a no-op");

        // 이미 있는 파일 위로는 옮기지 않는다 — 그 파일에 든 것이 사라진다.
        passed &= Expect(
            renamed &&
                !GameEngine::App::ProjectFile::RenameScene(
                    *renamed, 1, createdRoot / "Scenes" / "Main.scene"),
            "renaming onto an existing file should be refused");

        // 지우기: 등록과 파일이 함께 사라진다.
        const std::optional<GameEngine::App::ProjectFileData> removed = renamed
            ? GameEngine::App::ProjectFile::RemoveScene(*renamed, 1)
            : std::nullopt;
        passed &= Expect(removed.has_value(), "remove a scene");
        passed &= Expect(
            removed && !removed->settings.scenePaths.contains(1),
            "removing should take the registration away");
        passed &= Expect(
            !std::filesystem::exists(renamedPath), "removing should delete the file too");

        // 마지막 하나는 지울 수 없다. 장면이 없는 프로젝트는 IsValid가 거부하므로, 지우는 동작이
        // 프로젝트를 못 여는 상태로 만들게 된다.
        passed &= Expect(
            removed && !GameEngine::App::ProjectFile::RemoveScene(*removed, 0),
            "removing the last remaining scene should be refused");

        // 시작 장면을 지우면 그 자리가 남은 것 중 가장 작은 id로 옮겨 간다. 그대로 두면 없는
        // 것을 가리켜 프로젝트가 열리지 않는다.
        GameEngine::App::ProjectSettings twoScenes;
        twoScenes.scenePaths.emplace(0, "Scenes/Main.scene");
        twoScenes.scenePaths.emplace(3, "Scenes/Second.scene");
        twoScenes.initialSceneId = 0;
        const std::optional<GameEngine::App::ProjectSettings> withoutStart =
            GameEngine::App::ProjectFile::WithoutScene(twoScenes, 0);
        passed &= Expect(
            withoutStart && withoutStart->initialSceneId == 3,
            "removing the starting scene should move the start to a scene that still exists");

        // 없는 id는 지울 수 없다.
        passed &= Expect(
            !GameEngine::App::ProjectFile::WithoutScene(twoScenes, 9),
            "removing a scene that is not registered should be refused");

        // 같은 파일을 두 id로 등록하게 만드는 이름 바꾸기도 거부한다.
        passed &= Expect(
            !GameEngine::App::ProjectFile::WithRenamedScene(
                twoScenes, 3, "Scenes/Main.scene"),
            "renaming onto another scene's registered path should be refused");
    }

    const GameEngine::Build::ProjectBuildRequest request{
        contentRoot,
        compiledRoot / "BuildTest.exe",
        compiledRoot,
        outputRoot
    };
    const std::optional<GameEngine::Build::ProjectBuildResult> result =
        GameEngine::Build::ProjectBuilder::Build(request);
    passed &= Expect(result.has_value(), "project build should succeed");
    passed &= Expect(result && result->assetCount == 7, "seven assets should be packaged");
    passed &= Expect(result && result->sceneCount == 1, "one scene should be packaged");
    passed &= Expect(result && result->runtimeFileCount == 4, "four shaders should be packaged");
    passed &= Expect(std::filesystem::is_regular_file(outputRoot / "BuildTest.exe"), "copy executable");
    passed &= Expect(std::filesystem::is_regular_file(outputRoot / "BuildTest.pdb"), "copy symbols");
    passed &= Expect(std::filesystem::is_regular_file(outputRoot / "Dependency.dll"), "copy DLL");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "BuildTest.gameproject"),
        "copy project settings");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Scenes/Main.scene"),
        "copy configured scene");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Scenes/Unused.scene"),
        "copy unconfigured scene asset");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Textures/Albedo.png"),
        "copy registered texture");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Meshes/Model.fbx"),
        "copy registered mesh");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Fonts/Interface.otf"),
        "copy registered font");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Audio/Click.wav"),
        "copy registered audio clip");
    passed &= Expect(
        !std::filesystem::exists(outputRoot / "Ignored.txt"),
        "ignore unsupported content");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "Rendering/Direct3D/Shaders/Mesh.hlsl") &&
        std::filesystem::is_regular_file(outputRoot / "Rendering/Direct3D/Shaders/Sprite.hlsl") &&
        std::filesystem::is_regular_file(outputRoot / "Rendering/Direct3D/Shaders/Text.hlsl") &&
        std::filesystem::is_regular_file(outputRoot / "Rendering/Direct3D/Shaders/SkinnedMesh.hlsl"),
        "copy engine runtime shaders");

    const GameEngine::Platform::DirectoryContentSource packagedContent(outputRoot);
    GameEngine::Assets::AssetDatabase packagedDatabase;
    passed &= Expect(
        packagedDatabase.LoadManifest(packagedContent, "Assets/AssetDatabase.json"),
        "packaged asset manifest should validate");
    const auto* packagedSprite =
        packagedDatabase.FindAsset<GameEngine::Assets::Sprite>("Textures/Albedo.png");
    passed &= Expect(
        packagedSprite && packagedSprite->GetPixelsPerUnit() == 50.0f &&
            packagedSprite->GetBorder().bottom == 5.0f &&
            packagedSprite->GetSheet().columns == 4 && packagedSprite->GetSheet().rows == 2 &&
            packagedSprite->GetSheet().GetFrameCount() == 7 &&
            packagedSprite->GetSheet().frameRate == 24.0f,
        "preserve Sprite metadata without packaging the import sidecar");

    // A D3D12 package must stage every shader the renderer compiles during initialization, including Sprite.hlsl and Mesh.hlsl.
    const std::filesystem::path d3d12OutputRoot = root / "Build" / "D3D12";
    constexpr std::string_view d3d12Settings = R"({
  "projectName": "BuildTest",
  "window": { "width": 1280, "height": 720 },
  "targetFrameRate": 60,
  "initialSceneId": 0,
  "scenes": [ { "id": 0, "path": "Scenes/Main.scene" } ],
  "graphicsApi": "D3D12"
})";
    passed &= Expect(
        WriteFile(contentRoot / "BuildTest.gameproject", d3d12Settings),
        "write D3D12 settings");
    const GameEngine::Build::ProjectBuildRequest d3d12Request{
        contentRoot,
        compiledRoot / "BuildTest.exe",
        compiledRoot,
        d3d12OutputRoot
    };
    const std::optional<GameEngine::Build::ProjectBuildResult> d3d12Result =
        GameEngine::Build::ProjectBuilder::Build(d3d12Request);
    passed &= Expect(d3d12Result.has_value(), "D3D12 project build should succeed");
    passed &= Expect(
        d3d12Result && d3d12Result->runtimeFileCount == 4,
        "four shaders should be packaged for D3D12");
    passed &= Expect(
        std::filesystem::is_regular_file(d3d12OutputRoot / "Rendering/Direct3D/Shaders/Mesh.hlsl") &&
        std::filesystem::is_regular_file(d3d12OutputRoot / "Rendering/Direct3D/Shaders/Sprite.hlsl") &&
        std::filesystem::is_regular_file(d3d12OutputRoot / "Rendering/Direct3D/Shaders/Text.hlsl") &&
        std::filesystem::is_regular_file(d3d12OutputRoot / "Rendering/Direct3D/Shaders/SkinnedMesh.hlsl"),
        "copy every engine runtime shader for D3D12");

    passed &= Expect(WriteFile(outputRoot / "PreviousBuild.marker", "keep"), "write marker");
    constexpr std::string_view invalidSettings = R"({
  "projectName": "BuildTest",
  "window": { "width": 1280, "height": 720 },
  "targetFrameRate": 60,
  "initialSceneId": 0,
  "scenes": [ { "id": 0, "path": "Scenes/Missing.scene" } ],
  "graphicsApi": "D3D11"
})";
    passed &= Expect(
        WriteFile(contentRoot / "BuildTest.gameproject", invalidSettings),
        "write invalid settings");
    passed &= Expect(
        !GameEngine::Build::ProjectBuilder::Build(request),
        "build with a missing configured scene should fail");
    passed &= Expect(
        std::filesystem::is_regular_file(outputRoot / "PreviousBuild.marker"),
        "failed build should preserve previous output");

    passed &= Expect(RunSceneSaveTests(), "scene save tests should pass");
    passed &= Expect(RunGenericSerializationTests(), "generic serialization tests should pass");
    passed &= Expect(
        RunProjectComponentRegistrationTests(),
        "project component registration tests should pass");
    passed &= Expect(RunAddSceneTests(root), "add-scene tests should pass");
    passed &= Expect(RunLockedCameraTests(), "locked camera tests should pass");
    passed &= Expect(RunLightingTests(), "lighting tests should pass");
    passed &= Expect(RunTransparentDepthSortTests(), "transparent depth sort tests should pass");
    passed &= Expect(RunOverlayPassTests(), "overlay pass tests should pass");
    passed &= Expect(RunHierarchyEditTests(), "hierarchy edit tests should pass");
    passed &= Expect(RunUpdateOrderTests(), "update order tests should pass");
    passed &= Expect(RunGameBootstrapRegistryTests(), "game bootstrap registry tests should pass");
    passed &= Expect(RunDeferredSceneLoadTest(root), "deferred scene load tests should pass");

    std::filesystem::remove_all(root, error);
    return passed;
}

/// <summary>
/// 컴포넌트 스키마는 CMake에서 선택한 프로젝트의 디스크립터 옆에 생성되어야 한다.
/// 선택한 프로젝트가 없으면 편집기 콘텐츠를 사용하며 특정 출력 대상 이름에 의존하지 않는다.
/// </summary>
bool RunComponentSchemaPlacementTests()
{
    namespace fs = std::filesystem;
    const fs::path selectedProjectSchema = GAMEENGINE_TEST_PROJECT_SCHEMA;
    const fs::path editorSchema = GAMEENGINE_TEST_EDITOR_SCHEMA;
    const fs::path stagedSchema = selectedProjectSchema.empty() ? editorSchema : selectedProjectSchema;
    const fs::path projectSchema = selectedProjectSchema.empty()
        ? fs::path(GAMEENGINE_TEST_EDITOR_SOURCE_SCHEMA) : fs::path(GAMEENGINE_TEST_PROJECT_SOURCE_SCHEMA);
    if (!Expect(!stagedSchema.empty() && !projectSchema.empty(),
        "CMake should identify both the configured target's staged and source component schemas")) return false;

    // 읽을 파일은 이 구성이 실제로 낸 경로다. 원본 스키마의 내용과 배치까지 비교하므로,
    // 다른 구성이나 옛 SampleGame 산출물이 남아 있어도 빠진 스테이징을 대신 통과시키지 못한다.
    const auto stagedText = TestSupport::ReadFileWhenSettled(stagedSchema);
    const auto sourceText = TestSupport::ReadFileWhenSettled(projectSchema);
    const auto editorText = TestSupport::ReadFileWhenSettled(editorSchema);
    if (!Expect(stagedText.has_value(), "the configured target should stage its component schema beside the executable") ||
        !Expect(sourceText.has_value(), "the build should write the schema beside the source project, not only the executable") ||
        !Expect(editorText.has_value(), "the configured editor should have a staged component schema"))
    {
        std::cerr << "  staged schema: " << stagedSchema.string() << "\n"
                  << "  source schema: " << projectSchema.string() << "\n"
                  << "  editor schema: " << editorSchema.string() << "\n";
        return false;
    }

    const auto projectFile = GameEngine::App::ProjectFile::FindInDirectory(projectSchema.parent_path());
    const auto schemas = GameEngine::Serialization::ParseComponentSchemas(*sourceText);
    const auto stagedSchemas = GameEngine::Serialization::ParseComponentSchemas(*stagedText);
    const auto editorSchemas = GameEngine::Serialization::ParseComponentSchemas(*editorText);
    bool passed = Expect(projectFile.has_value(),
        "the source schema must sit beside exactly one project descriptor a person opens");
    passed &= Expect(!schemas.empty() && !stagedSchemas.empty() && !editorSchemas.empty(),
        "source, staged and editor schemas must describe their actual registered components");
    passed &= Expect(*sourceText == *stagedText,
        "the schema beside the source project must preserve every component and property emitted by its executable");
    for (const auto& schema : schemas)
    {
        const bool known = std::ranges::any_of(editorSchemas, [&schema](const auto& editorComponent)
        {
            return editorComponent.typeName == schema.typeName;
        });
        if (!known) std::cerr << "  editor schema is missing project type: " << schema.typeName << "\n";
        passed &= Expect(known, "the configured editor must know every component in the source project's schema");
    }
    return passed;
}

static const TestSupport::Registration gProjectBuilderTests{
    "ProjectBuilder", "project builder tests should pass", RunProjectBuilderTests };

static const TestSupport::Registration gComponentSchemaPlacementTests{
    "ProjectBuilder", "component schema placement tests should pass", RunComponentSchemaPlacementTests };
