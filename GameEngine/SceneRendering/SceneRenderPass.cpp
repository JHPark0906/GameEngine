#include "pch.h"
#include "SceneRenderPass.h"
#include "ViewCulling.h"

#include "../Rendering/RenderFrame.h"
#include "../Rendering/RenderColorPolicy.h"
#include "../Rendering/RenderFrameBuilder.h"
#include "../Platform/ITextRasterizer.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/PoseSampler.h"
#include "../Animation/Skeleton.h"
#include "../Assets/Asset.h"
#include "../Assets/AssetDatabase.h"
#include "../Assets/SkinnedMeshData.h"
#include "../Math/Aabb2D.h"
#include "../Math/Aabb3D.h"
#include "../Core/Guid.h"
#include "../Assets/ResourceId.h"
#include "../Diagnostics/Debug.h"
#include "../Runtime/Animator.h"
#include "../Runtime/Canvas.h"
#include "../Runtime/Camera.h"
#include "../Runtime/Game.h"
#include "../Runtime/GameObject.h"
#include "../Runtime/InputField.h"
#include "../Runtime/LayoutElement.h"
#include "../Runtime/Light.h"
#include "../Runtime/MeshRenderer.h"
#include "../Runtime/RectMask.h"
#include "../Runtime/RectTransform.h"
#include "../Runtime/Scene.h"
#include "../Runtime/SceneManager.h"
#include "../Runtime/ScreenTextLayout.h"
#include "../Runtime/UIStackOrder.h"
#include "../Runtime/SpriteRenderer.h"
#include "../Runtime/TextRenderer.h"
#include "../Runtime/TilemapRenderer.h"
#include "../Runtime/Transform.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <algorithm>
#include <optional>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameEngine::SceneRendering
{

using namespace Rendering;
namespace
{
    [[nodiscard]] std::optional<Runtime::RectTransform::Rect> AncestorTextClip(
        const Runtime::Transform& transform)
    {
        // The nearest mask's visible rectangle already contains every ancestor mask and canvas
        // intersection. A label's own rectangle is a layout box, not an implicit clipping mask.
        for (const auto* parent = transform.GetParent(); parent; parent = parent->GetParent())
        {
            const auto* object = parent->GetGameObject();
            if (object && object->GetComponent<Runtime::RectMask>())
                if (const auto* rect = object->GetComponent<Runtime::RectTransform>())
                    return rect->GetVisibleRect();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::shared_ptr<const std::vector<TextGlyphQuad>> ClipScreenGlyphs(
        const std::shared_ptr<const std::vector<TextGlyphQuad>>& glyphs,
        const Math::Vector3& origin, const Runtime::RectTransform::Rect& clip)
    {
        if (!glyphs) return {};
        std::shared_ptr<std::vector<TextGlyphQuad>> clipped;
        for (std::size_t index = 0; index < glyphs->size(); ++index)
        {
            const TextGlyphQuad& glyph = (*glyphs)[index];
            const Runtime::RectTransform::Rect bounds{
                origin.GetX() + glyph.centerX - glyph.width * 0.5f,
                origin.GetY() - glyph.centerY - glyph.height * 0.5f,
                glyph.width, glyph.height };
            const auto visible = bounds.IntersectedWith(clip);
            const bool unchanged = visible.x == bounds.x && visible.y == bounds.y &&
                visible.width == bounds.width && visible.height == bounds.height;
            if (!clipped && unchanged) continue;
            if (!clipped)
            {
                clipped = std::make_shared<std::vector<TextGlyphQuad>>();
                clipped->reserve(glyphs->size());
                clipped->insert(clipped->end(), glyphs->begin(), glyphs->begin() + index);
            }
            if (visible.IsEmpty()) continue;
            TextGlyphQuad result = glyph;
            result.centerX = visible.GetCenterX() - origin.GetX();
            result.centerY = origin.GetY() - visible.GetCenterY();
            result.width = visible.width;
            result.height = visible.height;
            // Crop geometry and its corresponding atlas rectangle together: squeezing the whole
            // glyph into the remaining rectangle would distort partially visible letters.
            result.u += glyph.uWidth * (visible.x - bounds.x) / glyph.width;
            result.v += glyph.vHeight * (visible.y - bounds.y) / glyph.height;
            result.uWidth *= visible.width / glyph.width;
            result.vHeight *= visible.height / glyph.height;
            clipped->push_back(result);
        }
        return clipped ? clipped : glyphs;
    }

    /// <summary>
    /// 이 요소를 배치한 캔버스의 픽셀 배율이다. 캔버스가 없으면 1이다.
    ///
    /// 가장 가까운 조상의 것을 쓴다. 배치가 캔버스를 새 면의 시작으로 보고 그 아래를 그 배율로
    /// 재기 때문이며, 여기서 같은 캔버스를 찾아야 테두리가 사각형과 같은 배율로 커진다.
    /// </summary>
    /// <param name="transform">배율을 물을 요소의 Transform이다.</param>
    [[nodiscard]] float CanvasScaleFactor(const Runtime::Transform& transform)
    {
        for (const Runtime::Transform* node = &transform; node; node = node->GetParent())
        {
            const Runtime::GameObject* const owner = node->GetGameObject();
            if (!owner)
            {
                continue;
            }
            if (const Runtime::Canvas* const canvas = owner->GetComponent<Runtime::Canvas>())
            {
                return canvas->GetScaleFactor();
            }
        }
        return 1.0f;
    }
    /// <summary>활성 카메라가 없는 장면의 기본 시야 부피 높이이다. 월드 단위다.</summary>
    constexpr float DefaultCameraOrthographicHeight = 10.0f;
    constexpr float DefaultCameraNearClipPlane = 0.01f;
    constexpr float DefaultCameraFarClipPlane = 1000.0f;

    /// <summary>
    /// 광원이 없는 장면에 적용하는 기본 조명이다.
    /// </summary>
    constexpr Math::Color DefaultAmbientLight{ 0.25f, 0.25f, 0.25f, 1.0f };

    [[nodiscard]] LightRenderData MakeDefaultDirectionalLight()
    {
        LightRenderData light;
        light.kind = LightKind::Directional;
        light.direction = Math::Vector3{ 0.35f, -0.8f, -0.45f }.Normalized();
        light.color = { 0.75f, 0.75f, 0.75f, 1.0f };
        return light;
    }

    /// <summary>
    /// 활성 카메라가 없는 장면에 쓰는 카메라 상태이다. 프론트엔드에서 결정하면 이 대체가 모든
    /// 백엔드에서 동일해진다. 백엔드가 각자 대체물을 지어내면 D3D11과 D3D12가 같은 장면에
    /// 대해 다른 이미지를 만든다.
    /// </summary>
    [[nodiscard]] CameraRenderData MakeDefaultCamera(const float aspectRatio)
    {
        const float safeAspectRatio = std::isfinite(aspectRatio) && aspectRatio > 0.0f ? aspectRatio : 1.0f;
        CameraRenderData camera;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicLeftHanded(
            DefaultCameraOrthographicHeight * safeAspectRatio,
            DefaultCameraOrthographicHeight,
            DefaultCameraNearClipPlane,
            DefaultCameraFarClipPlane);
        // A scene with no camera has expressed no background, so the frame clears to black.
        camera.clearColor = Math::Color::Black;
        return camera;
    }

    /// <summary>
    /// 런타임의 정렬을 플랫폼 래스터라이저의 정렬로 대응시킨다. Runtime이 플랫폼 계층에
    /// 의존하지 않도록 두 열거는 분리된 채로 남는다.
    /// </summary>
    [[nodiscard]] Platform::TextAlignment ToPlatformAlignment(
        const Runtime::TextRenderer::Alignment alignment)
    {
        switch (alignment)
        {
        case Runtime::TextRenderer::Alignment::Center: return Platform::TextAlignment::Center;
        case Runtime::TextRenderer::Alignment::Right: return Platform::TextAlignment::Right;
        default: return Platform::TextAlignment::Left;
        }
    }

    // 프레임 안 에셋 resolve 캐시.
    //
    // 참조가 프레임 안에서 처음 나타날 때 한 번 resolve하고 이후에는 경로·GUID·localId로
    // 캐시를 조회한다. 조회는 뷰로 하므로 캐시 미스일 때만 문자열을 복사한다.

    using PathStringView = std::basic_string_view<std::filesystem::path::value_type>;

    struct ReferenceKey
    {
        std::filesystem::path::string_type path;
        std::uint32_t localId = 0;
        Core::Guid guid;
    };

    struct ReferenceKeyView
    {
        PathStringView path;
        std::uint32_t localId = 0;
        Core::Guid guid;
    };

    struct ReferenceKeyHash
    {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(const ReferenceKey& key) const
        {
            return Hash(key.path, key.localId, key.guid);
        }
        [[nodiscard]] std::size_t operator()(const ReferenceKeyView& key) const
        {
            return Hash(key.path, key.localId, key.guid);
        }

    private:
        [[nodiscard]] static std::size_t Hash(
            const PathStringView path, const std::uint32_t localId, const Core::Guid& guid)
        {
            return std::hash<PathStringView>()(path) ^ std::hash<Core::Guid>()(guid) ^
                (static_cast<std::size_t>(localId) * 0x9E3779B97F4A7C15ull);
        }
    };

    struct ReferenceKeyEqual
    {
        using is_transparent = void;
        template <typename TLeft, typename TRight>
        [[nodiscard]] bool operator()(const TLeft& left, const TRight& right) const
        {
            return left.localId == right.localId && left.guid == right.guid &&
                PathStringView(left.path) == PathStringView(right.path);
        }
    };

    /// <summary>스프라이트류 참조의 프레임 안 resolve 결과다. 실패도 캐시되어 한 번만 실패한다.</summary>
    struct ResolvedSprite
    {
        const Assets::Sprite* sprite = nullptr;
        std::shared_ptr<const Assets::TextureData> texture;
    };

    /// <summary>
    /// Material 참조의 프레임 안 resolve 결과다. <c>material</c>이 null이면 참조가
    /// 풀리지 않거나 Material이 아닌 종류를 가리킨다. FindAsset&lt;Assets::Material&gt;이
    /// 모두 null로 답하므로 여기서 종류를 따로 검사하지 않는다.
    /// </summary>
    struct ResolvedMaterial
    {
        const Assets::Material* material = nullptr;
        std::shared_ptr<const Assets::MaterialData> data;
    };

    struct ResolvedMesh
    {
        const Assets::Mesh* mesh = nullptr;
        std::shared_ptr<const Assets::MeshData> data;
    };

    /// <summary>
    /// 스킨드 메시·골격·클립의 resolve 결과다. <see cref="ResolvedMesh"/>와 달리 파일 자신의
    /// 등록 타입(대개 <c>AssetType::Mesh</c>인 모델 파일)을 얻지 않는다 — 이 셋은 언제나 그
    /// 파일의 서브에셋일 뿐이라, <c>FindAsset&lt;Assets::Skeleton&gt;</c>류의 형식 검사는 파일
    /// 자체가 그 형식이 아닌 한 항상 실패한다. 페이로드가 있는지만으로 형식이 이미 검증된다 —
    /// AssetDatabase::LoadSkeleton 등이 로컬 id 자리의 서브에셋 형식이 다르면 null을 낸다.
    /// </summary>
    struct ResolvedSkinnedMesh
    {
        std::shared_ptr<const Assets::SkinnedMeshData> data;
    };

    struct ResolvedSkeleton
    {
        std::shared_ptr<const Animation::Skeleton> data;
    };

    struct ResolvedAnimationClip
    {
        std::shared_ptr<const Animation::AnimationClip> data;
    };

    /// <summary>
    /// 훑을 칸의 닫힌 구간이다. <c>lastColumn &lt; firstColumn</c>(또는 행 쪽)이면 빈 구간 —
    /// 카메라가 격자를 아예 안 본다는 뜻이고, 바깥 루프가 그러면 한 번도 돌지 않는다.
    /// </summary>
    struct TileRange
    {
        int firstColumn = 0;
        int lastColumn = -1;
        int firstRow = 0;
        int lastRow = -1;
    };

    template <typename TValue>
    using ReferenceCache =
        std::unordered_map<ReferenceKey, TValue, ReferenceKeyHash, ReferenceKeyEqual>;

    template <typename TValue, typename TResolve>
    [[nodiscard]] const TValue& ResolveOnce(
        ReferenceCache<TValue>& cache,
        const Assets::AssetReference& reference,
        TResolve&& resolve)
    {
        const ReferenceKeyView view{
            reference.GetPath().native(), reference.GetLocalId(), reference.GetGuid() };
        if (const auto existing = cache.find(view); existing != cache.end())
        {
            return existing->second;
        }
        return cache.emplace(
            ReferenceKey{
                reference.GetPath().native(), reference.GetLocalId(), reference.GetGuid() },
            resolve()).first->second;
    }
}


const std::shared_ptr<const Assets::TextureData>& SceneRenderPass::GetSolidTexture()
{
    if (!mSolidTexture)
    {
        auto white = std::make_shared<Assets::TextureData>();
        // 즉시 모드 UI의 흰 픽셀과 같은 id다. 픽셀이 글자 그대로 같으므로 백엔드 캐시가 한
        // 항목을 나눠 쓰고, 다르게 두면 같은 그림이 두 번 올라갈 뿐이다.
        white->id = Assets::MakeResourceId(Assets::ResourceIdDomain::Dynamic, 0);
        white->width = 1;
        white->height = 1;
        white->pixels.assign(4, std::byte{ 0xFF });
        mSolidTexture = std::move(white);
    }
    return mSolidTexture;
}

namespace
{
    /// <summary>
    /// 한 프레임을 모으는 동안만 사는 작업 상태다. <b>SceneRenderPass의 상태가 아니다.</b>
    /// Collect 호출이 끝나면 사라져야 다음 프레임의 수집과 광원 선택에 상태가 남지 않는다.
    ///
    /// 부류별 수집 함수는 각자의 지역 상태를 사용하고 프레임 전체에 필요한 값만 이곳에서 공유한다.
    /// </summary>
    struct FrameCollector
    {
        /// <summary>1x1 흰 텍스처를 만들어 두는 자리가 패스라서 그것만 빌려 쓴다.</summary>
        SceneRenderPass& renderPass;
        const Assets::AssetDatabase& assetDatabase;
        RenderFrameBuilder& builder;
        const Runtime::SceneManager& scenes;
        /// <summary>이 세계의 글자 배치 캐시다. null이면 텍스트를 그리지 않는다.</summary>
        Rendering::TextRasterizationCache* textCache = nullptr;

        // 스프라이트 캐시는 메시의 재질과도 공유된다: 둘 다 Sprite 에셋과 그 텍스처로 resolve된다.
        ReferenceCache<ResolvedSprite> spriteResolves;
        ReferenceCache<ResolvedMaterial> materialResolves;
        ReferenceCache<ResolvedMesh> meshResolves;
        ReferenceCache<ResolvedSkinnedMesh> skinnedMeshResolves;
        ReferenceCache<ResolvedSkeleton> skeletonResolves;
        ReferenceCache<ResolvedAnimationClip> clipResolves;
        std::unordered_map<const Runtime::GameObject*, std::uint64_t> overlayOrders;

        bool overlayOrdersReady = false;

        [[nodiscard]] std::uint64_t GetOverlayOrder(
            const Runtime::GameObject* object, const bool screenSpace)
        {
            if (!screenSpace) return 0;
            if (!overlayOrdersReady)
            {
                for (const auto& entry : Runtime::BuildUIStack(scenes).entries)
                {
                    if (entry.overlayOrder != 0) overlayOrders.emplace(entry.object, entry.overlayOrder);
                }
                overlayOrdersReady = true;
            }
            const auto found = overlayOrders.find(object);
            return found != overlayOrders.end() ? found->second : 0;
        }

        int priority = (std::numeric_limits<int>::min)();
        unsigned int activeCameraInstanceId = (std::numeric_limits<unsigned int>::max)();
        // 광원은 instanceId 순으로 모아 프레임의 상한을 넘는 것을 버린다. 장면의 걷기 순서가
        // 아니라 id 순이라야 어느 광원이 잘리는지가 프레임마다 같다.
        std::vector<std::pair<unsigned int, LightRenderData>> lights;
        bool hasAmbientLight = false;

        /// <summary>
        /// 실제 view/projection으로 만든 가시 범위다. 장면 직교 카메라는 네 옆면만,
        /// 별도 뷰 카메라는 깊이를 포함한 여섯 면을 사용한다.
        /// </summary>
        std::optional<ViewCulling> viewCulling;

        /// <summary>
        /// 로컬 상자가 이 뷰의 카메라 안에 보이는지 판정한다. 명시적인 뷰 카메라는 절두체를,
        /// 장면의 직교 카메라는 기존 XY 범위를 쓴다. 둘 다 없으면 거르지 않는다.
        /// </summary>
        [[nodiscard]] bool IsVisible(
            const Math::Aabb3D& localBounds, const Math::Matrix4x4& localToWorld) const
        {
            if (viewCulling)
            {
                return viewCulling->IsVisible(localBounds, localToWorld);
            }
            return true;
        }

        /// <summary>
        /// 이 격자에서 카메라가 실제로 볼 수 있는 칸의 구간이다. 통짜 상자로 거르는 대신 훑을
        /// 범위 자체를 좁히는 이유는 타일맵만 다른 문제이기 때문이다 — 칸 하나하나가 상자를
        /// 하나씩 갖는 것이 아니라, 훑는 순서 자체가 격자 좌표라 카메라의 가시 범위를 격자
        /// 좌표로 바꾸면 순회 범위를 줄일 수 있다.
        ///
        /// 뷰 카메라는 절두체와 타일 평면의 교차 범위를 사용한다. 범위를 계산할 수 없으면
        /// 전체 격자를 후보로 돌려주고, 뷰 카메라의 개별 셀 판정은 수집 단계에서 적용한다.
        /// </summary>
        [[nodiscard]] TileRange ComputeVisibleTileRange(
            const int columns, const int rows, const Math::Vector2& cellSize,
            const Math::Matrix4x4& localToWorld) const
        {
            const TileRange fullRange{ 0, columns - 1, 0, rows - 1 };
            if (viewCulling)
            {
                if (cellSize.GetX() <= 0.0f || cellSize.GetY() <= 0.0f)
                {
                    return fullRange;
                }
                const auto bounds = viewCulling->GetVisiblePlaneBounds(localToWorld);
                if (!bounds)
                {
                    return fullRange;
                }
                if (bounds->IsEmpty())
                {
                    return TileRange{ 0, -1, 0, -1 };
                }
                // 원근 뷰도 타일 평면과 만나는 구간만 순회한다. 모든 칸을 제출하면
                // 카메라와 무관한 앞쪽 칸들이 프레임의 draw 예산을 먼저 소모한다.
                const float firstColumn = std::floor(bounds->min.GetX() / cellSize.GetX());
                const float lastColumn = std::floor(bounds->max.GetX() / cellSize.GetX());
                const float firstRow = std::floor(bounds->min.GetY() / cellSize.GetY());
                const float lastRow = std::floor(bounds->max.GetY() / cellSize.GetY());
                if (!std::isfinite(firstColumn) || !std::isfinite(lastColumn) ||
                    !std::isfinite(firstRow) || !std::isfinite(lastRow))
                {
                    return fullRange;
                }
                if (lastColumn < 0.0f || firstColumn >= static_cast<float>(columns) ||
                    lastRow < 0.0f || firstRow >= static_cast<float>(rows))
                {
                    return TileRange{ 0, -1, 0, -1 };
                }
                // 먼 평면의 큰 좌표도 int로 바꾸기 전에 격자 안으로 제한한다.
                return TileRange{
                    static_cast<int>((std::max)(0.0f, firstColumn)),
                    static_cast<int>((std::min)(static_cast<float>(columns - 1), lastColumn)),
                    static_cast<int>((std::max)(0.0f, firstRow)),
                    static_cast<int>((std::min)(static_cast<float>(rows - 1), lastRow)) };
            }
            return fullRange;
        }

        [[nodiscard]] const ResolvedSprite& ResolveSprite(const Assets::AssetReference& reference)
        {
            return ResolveOnce(spriteResolves, reference, [this, &reference]
            {
                ResolvedSprite resolved;
                resolved.sprite = assetDatabase.FindAsset<Assets::Sprite>(reference);
                if (resolved.sprite)
                {
                    resolved.texture = assetDatabase.LoadTexture(reference);
                }
                return resolved;
            });
        }

        [[nodiscard]] const ResolvedMaterial& ResolveMaterial(const Assets::AssetReference& reference)
        {
            return ResolveOnce(materialResolves, reference, [this, &reference]
            {
                ResolvedMaterial resolved;
                resolved.material = assetDatabase.FindAsset<Assets::Material>(reference);
                if (resolved.material)
                {
                    resolved.data = assetDatabase.LoadMaterial(reference);
                }
                return resolved;
            });
        }

        /// <summary>
        /// 이 머티리얼이 실제로 그릴 텍스처다. 머티리얼에 텍스처가 없으면(비어 있는
        /// <c>AssetReference</c>) 단색 판이다 — <see cref="MaterialData::tint"/>의 주석이
        /// 말하는 그대로, 텍스처가 없을 때는 tint 자체가 표면 색이고, 셰이더는 여전히
        /// <c>albedo * tint</c>를 계산하므로 albedo가 흰색인 텍스처 하나로 같은 결과를 낸다.
        /// 텍스처는 있는데 그 참조가 안 풀리면(지워졌거나 형식이 안 맞으면) null을 낸다 —
        /// 그것은 "칠할 색이 없다"이지 "칠할 게 없으니 흰 판을 쓴다"가 아니다.
        /// </summary>
        [[nodiscard]] std::shared_ptr<const Assets::TextureData> ResolveMaterialTexture(
            const Assets::MaterialData& material)
        {
            if (!material.texture.IsValid())
            {
                return renderPass.GetSolidTexture();
            }
            return ResolveSprite(material.texture).texture;
        }

        /// <summary>
        /// 장면이 올라올 때 이미 읽혔으므로 이것은 조회다. 못 찾는 것은 미리 읽기가 무언가를
        /// 놓쳤다는 뜻이고, 조용히 버리는 것보다 보이는 편이 나아서 여기서 읽는다 — 프레임
        /// 안에서, 곧 미리 읽기가 피하려던 바로 그 자리에서.
        /// </summary>
        [[nodiscard]] const ResolvedMesh& ResolveMesh(const Assets::AssetReference& reference)
        {
            return ResolveOnce(meshResolves, reference, [this, &reference]
            {
                ResolvedMesh resolved;
                resolved.mesh = assetDatabase.FindAsset<Assets::Mesh>(reference);
                if (resolved.mesh)
                {
                    resolved.data = assetDatabase.LoadMesh(reference);
                }
                return resolved;
            });
        }

        [[nodiscard]] const ResolvedSkinnedMesh& ResolveSkinnedMesh(
            const Assets::AssetReference& reference)
        {
            return ResolveOnce(skinnedMeshResolves, reference, [this, &reference]
            {
                return ResolvedSkinnedMesh{ assetDatabase.LoadSkinnedMesh(reference) };
            });
        }

        [[nodiscard]] const ResolvedSkeleton& ResolveSkeletonAsset(
            const Assets::AssetReference& reference)
        {
            return ResolveOnce(skeletonResolves, reference, [this, &reference]
            {
                return ResolvedSkeleton{ assetDatabase.LoadSkeleton(reference) };
            });
        }

        [[nodiscard]] const ResolvedAnimationClip& ResolveAnimationClip(
            const Assets::AssetReference& reference)
        {
            return ResolveOnce(clipResolves, reference, [this, &reference]
            {
                return ResolvedAnimationClip{ assetDatabase.LoadAnimationClip(reference) };
            });
        }

        void CollectLights(const Runtime::GameObject* object);
        void CollectMeshes(const Runtime::GameObject* object);
        void CollectSkinnedMeshes(const Runtime::GameObject* object);
        void CollectSprites(const Runtime::GameObject* object);
        void CollectTilemaps(const Runtime::GameObject* object);
        void CollectTexts(const Runtime::GameObject* object);
        void CollectCameras(const Runtime::GameObject* object);

        /// <summary>모으기가 끝난 뒤의 프레임 전체 규칙이다: 기본 카메라, 광원 정렬과 상한, 기본 조명.</summary>
        void Finish(const Runtime::Game& game);
    };

    void FrameCollector::CollectLights(const Runtime::GameObject* const object)
    {
    for (const Runtime::Light* light : object->GetComponents<Runtime::Light>())
    {
        if (!light->IsActiveAndEnabled())
        {
            continue;
        }
        // 광원 색도 저작된 색이라 sRGB다. 세기는 선형 값에 곱한다.
        const Math::Color color = ToLinearColor(light->GetColor());
        const float intensity = light->GetIntensity();
        const Math::Color scaled{ color.r * intensity, color.g * intensity, color.b * intensity, 1.0f };
        if (light->GetKind() == Runtime::Light::Kind::Ambient)
        {
            builder.AddAmbientLight(scaled);
            hasAmbientLight = true;
            continue;
        }
        LightRenderData data;
        data.color = scaled;
        const Math::Matrix4x4 localToWorld = object->GetTransform().GetLocalToWorldMatrix();
        if (light->GetKind() == Runtime::Light::Kind::Point)
        {
            data.kind = LightKind::Point;
            data.position = localToWorld.GetTranslation();
            data.range = light->GetRange();
        }
        else
        {
            data.kind = LightKind::Directional;
            data.direction = localToWorld.TransformDirection(Math::Vector3::Forward).Normalized();
        }
        lights.emplace_back(light->GetInstanceId(), data);
    }
    }

    void FrameCollector::CollectMeshes(const Runtime::GameObject* const object)
    {
    for (const Runtime::MeshRenderer* renderer : object->GetComponents<Runtime::MeshRenderer>())
    {
        if (!renderer->IsRenderable() || !renderer->GetMesh().IsValid() ||
            !renderer->GetMaterial().IsValid())
        {
            continue;
        }

            const ResolvedMesh& mesh = ResolveMesh(renderer->GetMesh());
        const ResolvedMaterial& material = ResolveMaterial(renderer->GetMaterial());
        if (!mesh.mesh || !mesh.data || !material.data)
        {
            continue;
        }
        const std::shared_ptr<const Assets::TextureData> texture =
            ResolveMaterialTexture(*material.data);
        if (!texture)
        {
            continue;
        }

        const Math::Matrix4x4 localToWorld = object->GetTransform().GetLocalToWorldMatrix();
        if (!IsVisible(mesh.data->bounds, localToWorld))
        {
            continue;
        }

        MeshDraw draw;
        draw.pipeline = builder.AddPipeline({ PipelineKind::Mesh });
        draw.geometry = builder.AddGeometry({ mesh.data });
        draw.material = builder.AddMaterial({ texture });
        draw.localToWorld = localToWorld;
        const Math::Color& materialTint = material.data->tint;
        const Math::Color& color = renderer->GetColor();
        draw.tint = { materialTint.r * color.r, materialTint.g * color.g,
            materialTint.b * color.b, materialTint.a * color.a };
        if (draw.tint.a <= 0.0f)
        {
            continue;
        }
        if (renderer->SortsWithSprites() || draw.UsesAlphaBlending())
        {
            static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, std::move(draw),
                renderer->GetSortingOrder(), renderer->GetInstanceId()));
        }
        else
        {
            static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, std::move(draw)));
        }
    }
    }

    /// <summary>
    /// Animator를 SkinnedMeshDraw로 옮긴다. CollectMeshes와 같은 모양이지만 resolve할 것이
    /// 셋 더 있다: 스킨드 메시·골격·클립 모두 Animator가 쥔 AssetReference에서 이 자리가
    /// 직접 읽는다. 채점(Animation::SamplePose)도 여기서 한다 — 골격과 클립 둘 다가 이
    /// 프레임에서 resolve된 뒤에야 할 수 있는 일이고, Animator는 둘의 이름만 알 뿐 값을 쥐지
    /// 않기 때문이다. 재질은 MeshRenderer와 똑같이 ResolveMaterial을 거친다.
    /// </summary>
    void FrameCollector::CollectSkinnedMeshes(const Runtime::GameObject* const object)
    {
    for (const Runtime::Animator* animator : object->GetComponents<Runtime::Animator>())
    {
        if (!animator->IsRenderable() || !animator->GetMesh().IsValid() ||
            !animator->GetSkeleton().IsValid() || !animator->GetClip().IsValid() ||
            !animator->GetMaterial().IsValid())
        {
            continue;
        }

        const ResolvedSkinnedMesh& mesh = ResolveSkinnedMesh(animator->GetMesh());
        const ResolvedSkeleton& skeleton = ResolveSkeletonAsset(animator->GetSkeleton());
        const ResolvedAnimationClip& clip = ResolveAnimationClip(animator->GetClip());
        const ResolvedMaterial& material = ResolveMaterial(animator->GetMaterial());
        if (!mesh.data || !skeleton.data || !clip.data || !material.data)
        {
            continue;
        }
        const std::shared_ptr<const Assets::TextureData> texture =
            ResolveMaterialTexture(*material.data);
        if (!texture)
        {
            continue;
        }

        const Math::Matrix4x4 localToWorld = object->GetTransform().GetLocalToWorldMatrix();
        if (!IsVisible(mesh.data->bounds, localToWorld))
        {
            continue;
        }

        // 골격·클립 어느 쪽이 유효하지 않은지는 SamplePose가 이미 판정하므로 여기서
        // 되풀이하지 않는다 — 빈 벡터로 돌아오면 아래에서 그대로 건너뛴다.
        auto boneMatrices = std::make_shared<std::vector<Math::Matrix4x4>>(
            Animation::SamplePose(*skeleton.data, *clip.data, animator->GetElapsedSeconds()));
        if (boneMatrices->empty())
        {
            continue;
        }

        SkinnedMeshDraw draw;
        draw.pipeline = builder.AddPipeline({ PipelineKind::SkinnedMesh });
        draw.geometry = builder.AddSkinnedGeometry({ mesh.data });
        draw.material = builder.AddMaterial({ texture });
        draw.localToWorld = localToWorld;
        draw.boneMatrices = std::move(boneMatrices);
        const Math::Color& materialTint = material.data->tint;
        const Math::Color& color = animator->GetColor();
        draw.tint = { materialTint.r * color.r, materialTint.g * color.g,
            materialTint.b * color.b, materialTint.a * color.a };
        if (draw.tint.a <= 0.0f)
        {
            continue;
        }
        if (animator->SortsWithSprites() || draw.UsesAlphaBlending())
        {
            static_cast<void>(builder.TryAddDraw(RenderPass::Transparent, std::move(draw),
                animator->GetSortingOrder(), animator->GetInstanceId()));
        }
        else
        {
            static_cast<void>(builder.TryAddDraw(RenderPass::Opaque, std::move(draw)));
        }
    }
    }

    void FrameCollector::CollectSprites(const Runtime::GameObject* const object)
    {
    for (const Runtime::SpriteRenderer* renderer : object->GetComponents<Runtime::SpriteRenderer>())
    {
        if (!renderer->IsRenderable())
        {
            continue;
        }

        const bool screenSpace =
            renderer->GetSpace() == Runtime::SpriteRenderer::Space::Screen;
        const Runtime::RectTransform* const rectTransform =
            screenSpace ? object->GetComponent<Runtime::RectTransform>() : nullptr;

        // 그림 없는 UI 요소는 단색 사각형이다. 배치가 자리를 정해 준 화면 공간 요소에
        // 한해서만 그렇게 다룬다: 월드에 놓인 렌더러가 에셋을 잃은 것은 고쳐야 할 실수이지
        // 단색으로 대신할 일이 아니고, "그림 없는 판"이 흔한 요구인 곳은 UI다. 버튼의
        // 바탕도 패널도 구분선도 전부 그것이라, 여기서 에셋을 요구하면 UI를 만들 때마다
        // 1픽셀짜리 그림을 하나씩 만들게 된다.
        const bool solidRect = !renderer->GetSprite().IsValid() && rectTransform != nullptr;
        if (!solidRect && !renderer->GetSprite().IsValid())
        {
            continue;
        }

        const Assets::Sprite* sprite = nullptr;
        std::shared_ptr<const Assets::TextureData> texture;
        float pixelsPerUnit = 1.0f;
        if (solidRect)
        {
            texture = renderPass.GetSolidTexture();
        }
        else
        {
            const ResolvedSprite& resolved = ResolveSprite(renderer->GetSprite());
            sprite = resolved.sprite;
            if (!sprite || !resolved.texture)
            {
                continue;
            }
            texture = resolved.texture;
            pixelsPerUnit = sprite->GetPixelsPerUnit();
        }

        // 단색 사각형에는 자를 테두리도 고를 프레임도 없다. 조각내기는 그림의 성질이다.
        const bool sliced = !solidRect &&
            renderer->GetDrawMode() == Runtime::SpriteRenderer::DrawMode::Sliced;
        // 조상 마스크에 완전히 가려진 요소는 그릴 것이 없다. 걸친 요소는 지금은 통째로
        // 그려진다 — 픽셀 단위로 반쪽만 내보내려면 프레임에 시저 사각형이 필요하다.
        if (rectTransform && rectTransform->GetVisibleRect().IsEmpty())
        {
            continue;
        }

        SpriteDraw draw;
        draw.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
        draw.material = builder.AddMaterial({ texture });
        draw.tint = renderer->GetColor();
        draw.pixelsPerUnit = pixelsPerUnit;
        draw.flipX = renderer->IsFlippedX();
        draw.flipY = renderer->IsFlippedY();
        if (sliced)
        {
            // 테두리는 그림이 선언한 대로 텍스처 픽셀로 싣는다. 조각이 그림의 어느 부분을
            // 읽는지가 이 값에서 나오므로, 여기에 배율을 곱하면 UV가 함께 어긋난다.
            const Assets::Sprite::Border& border = sprite->GetBorder();
            draw.sliced = true;
            draw.border = {
                border.left, border.top, border.right, border.bottom };
            // 화면에서 모서리를 얼마나 크게 그릴지는 따로 싣는다. 사각형은 배치가 이미
            // 배율을 곱해 넘겨 주므로, 모서리만 원본 픽셀로 남으면 200%에서 절반으로 보인다.
            draw.borderScale =
                screenSpace ? CanvasScaleFactor(object->GetTransform()) : 1.0f;
            // 화면 공간에서는 늘어날 크기도 픽셀이고, UI 계층에 속해 있으면 그 크기를
            // 정하는 것은 렌더러가 아니라 배치다.
            draw.size = rectTransform
                ? Math::Vector2{
                    rectTransform->GetResolvedRect().width,
                    rectTransform->GetResolvedRect().height }
                : renderer->GetSize();
        }
        else
        {
            // 시트의 어느 프레임을 보여 줄지는 렌더러가 쥐고, 그 프레임이 이미지의 어디인지는
            // 에셋이 안다. nine-slice와는 겹치지 않는다: 늘어나는 조각들은 이미지 전체를
            // 기준으로 잘리므로 sliced draw는 언제나 이미지 전체를 쓴다.
            float u = 0.0f;
            float v = 0.0f;
            float width = 1.0f;
            float height = 1.0f;
            // 단색 사각형에는 고를 프레임이 없다: 그림 전체가 한 픽셀이다.
            if (sprite)
            {
                sprite->GetFrameRect(renderer->ResolveFrame(sprite->GetSheet().GetFrameCount()),
                    u, v, width, height);
            }
            draw.uvRect = { u, v, width, height };
        }
        draw.space = screenSpace ? DrawSpace::Screen : DrawSpace::World;
        draw.localToWorld = object->GetTransform().GetLocalToWorldMatrix();
        if (rectTransform)
        {
            // 화면 공간 스프라이트가 UI 계층에 속해 있으면 자리를 정하는 것은 Transform이
            // 아니라 그 계층이다. 두 그리기 방식이 요구하는 것이 다르다: 조각난 스프라이트는
            // 조각마다 이미 픽셀 치수를 갖고 조립되므로 사각형의 중심으로 옮기기만 하면 되고,
            // 통짜 스프라이트는 quad가 프레임 픽셀 크기로 만들어지므로 그 크기로 나눈 배율을
            // 함께 실어야 사각형에 정확히 맞는다.
            const Runtime::RectTransform::Rect& rect = rectTransform->GetResolvedRect();
            const Math::Matrix4x4 toRectCenter = Math::Matrix4x4::CreateTranslation(
                { rect.GetCenterX(), rect.GetCenterY(), 0.0f });
            if (sliced)
            {
                draw.localToWorld = toRectCenter;
            }
            else
            {
                const float frameWidth =
                    static_cast<float>(texture->width) * draw.uvRect.width;
                const float frameHeight =
                    static_cast<float>(texture->height) * draw.uvRect.height;
                if (frameWidth <= 0.0f || frameHeight <= 0.0f)
                {
                    continue;
                }
                draw.localToWorld =
                    Math::Matrix4x4::CreateScale(
                        { rect.width / frameWidth, rect.height / frameHeight, 1.0f }) *
                    toRectCenter;
            }
        }
        // 화면 공간 스프라이트는 컬링 대상이 아니다 — 카메라가 아니라 창 위에 놓이고, 화면
        // 공간에는 카메라 사각형이라는 개념 자체가 없다. 컬링은 월드 공간만 본다.
        if (!screenSpace)
        {
            // 조각난 스프라이트의 세계 크기는 렌더러가 선언한 크기이고, 통짜 스프라이트의
            // 세계 크기는 프레임 픽셀을 pixelsPerUnit으로 나눈 것이다 — 실제로 그려질 quad를
            // QuadDrawGeometry가 만드는 것과 같은 셈이다. 두 크기가 다른 자리에서 나오므로
            // 여기서도 갈라 잰다.
            const Math::Vector2 worldSize = sliced
                ? draw.size
                : Math::Vector2{
                    static_cast<float>(texture->width) * draw.uvRect.width / pixelsPerUnit,
                    static_cast<float>(texture->height) * draw.uvRect.height / pixelsPerUnit };
            const Math::Aabb3D localBounds{
                { worldSize.GetX() * -0.5f, worldSize.GetY() * -0.5f, 0.0f },
                { worldSize.GetX() * 0.5f, worldSize.GetY() * 0.5f, 0.0f } };
            if (!IsVisible(localBounds, draw.localToWorld))
            {
                continue;
            }
        }

        // 화면 공간은 오버레이이므로 세계만 보는 뷰는 스프라이트와 텍스트 모두 수집하지 않는다.
        const RenderPass spritePass =
            screenSpace ? RenderPass::Overlay : RenderPass::Transparent;
        static_cast<void>(builder.TryAddDraw(
            spritePass,
            std::move(draw),
            renderer->GetSortingOrder(),
            renderer->GetInstanceId(), GetOverlayOrder(object, screenSpace)));
    }
    }

    void FrameCollector::CollectTilemaps(const Runtime::GameObject* const object)
    {
    for (const Runtime::TilemapRenderer* renderer :
         object->GetComponents<Runtime::TilemapRenderer>())
    {
        if (!renderer->IsRenderable() || !renderer->GetTileset().IsValid())
        {
            continue;
        }
        const ResolvedSprite& tileset = ResolveSprite(renderer->GetTileset());
        if (!tileset.sprite || !tileset.texture)
        {
            continue;
        }

        const Math::Matrix4x4 localToWorld = object->GetTransform().GetLocalToWorldMatrix();
        // 먼저 카메라가 타일 평면에서 보는 구간만 순회한다. 편집 카메라의 기울어진
        // 절두체는 그 구간 모서리에 보이지 않는 셀도 품으므로 아래에서 한 번 더 거른다.
        const TileRange visible = ComputeVisibleTileRange(
            renderer->GetColumns(), renderer->GetRows(), renderer->GetCellSize(), localToWorld);

        // 빈 칸은 싣지 않는다: 프레임이 나르는 것은 그릴 것뿐이고, 성긴 타일맵은 그만큼
        // 작은 패킷이 된다. 한 레이어가 패킷 하나이므로 칸이 수천이어도 정렬과 검증은
        // 한 번씩이다.
        auto tiles = std::make_shared<std::vector<TilemapTile>>();
        for (int row = visible.firstRow; row <= visible.lastRow; ++row)
        {
            for (int column = visible.firstColumn; column <= visible.lastColumn; ++column)
            {
                const int tile = renderer->GetTile(column, row);
                if (tile < 0)
                {
                    continue;
                }
                if (viewCulling)
                {
                    const Math::Vector2& cell = renderer->GetCellSize();
                    const Math::Aabb3D tileBounds{
                        { static_cast<float>(column) * cell.GetX(), static_cast<float>(row) * cell.GetY(), 0.0f },
                        { static_cast<float>(column + 1) * cell.GetX(), static_cast<float>(row + 1) * cell.GetY(), 0.0f } };
                    if (!viewCulling->IsVisible(tileBounds, localToWorld))
                    {
                        continue;
                    }
                }
                TilemapTile placed;
                placed.column = column;
                placed.row = row;
                tileset.sprite->GetFrameRect(
                    tile, placed.uv.u, placed.uv.v, placed.uv.width, placed.uv.height);
                tiles->push_back(placed);
            }
        }
        if (tiles->empty())
        {
            continue;
        }

        TilemapDraw draw;
        draw.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
        draw.material = builder.AddMaterial({ tileset.texture });
        draw.tint = renderer->GetColor();
        draw.cellSize = renderer->GetCellSize();
        draw.tiles = std::move(tiles);
        draw.localToWorld = localToWorld;
        static_cast<void>(builder.TryAddDraw(
            RenderPass::Transparent,
            std::move(draw),
            renderer->GetSortingOrder(),
            renderer->GetInstanceId()));
    }
    }

    void FrameCollector::CollectTexts(const Runtime::GameObject* const object)
    {
    for (const Runtime::TextRenderer* renderer : object->GetComponents<Runtime::TextRenderer>())

    {
        const auto* field = object->GetComponent<Runtime::InputField>();
        const bool editing = field && field->IsActiveAndEnabled() && field->IsFocused() &&
            renderer->GetSpace() == Runtime::TextRenderer::Space::Screen;
        if (!renderer->IsRenderable() || (renderer->GetText().empty() && !editing))
        {
            continue;
        }

        // Everything that decides which pixels appear is resolved here, so the frame carries a
        // finished image and a backend never sees a font name.
        Platform::TextRasterizationRequest request;
        request.text = renderer->GetText();
        request.fontFamily = renderer->GetFontFamily();
        // 글자 크기도 테두리와 같은 자리에서 배율을 탄다. 선언은 논리 픽셀이고, 그것을
        // 물리 픽셀로 만드는 일은 그리는 가장자리에서 한 번만 일어난다.
        //
        // 한 값은 한 단위여야 한다. 저장된 값을 재는 쪽은 논리로, 그리는 쪽은 물리로 읽으면
        // 그 값을 정하는 쪽은 둘 중 하나만 만족시킬 수 있고, 나머지 하나는 배율만큼
        // 어긋난다 — 그 어긋남은 화면에서 "글꼴이 큰가"로만 보여 원인을 짚기 어렵다.
        const bool screenSpace = renderer->GetSpace() == Runtime::TextRenderer::Space::Screen;
        auto textClip = screenSpace ? AncestorTextClip(object->GetTransform()) : std::nullopt;
        if (screenSpace && field)
            if (const auto* rect = object->GetComponent<Runtime::RectTransform>())
                textClip = rect->GetVisibleRect();
        if (textClip && textClip->IsEmpty()) continue;
        const float textScale = screenSpace ? CanvasScaleFactor(object->GetTransform()) : 1.0f;
        request.fontSize = renderer->GetFontSize() * textScale;
        request.maxWidth = renderer->GetMaxWidth();
        // 배치가 이 글자를 접기로 했으면 접히는 폭도 배치가 정한 것이어야 한다. 재는 쪽이
        // 자리의 폭에서 접고 그리는 쪽이 렌더러의 선언대로 접으면 — 그 선언은 기본이 0,
        // 즉 접지 않음이다 — 잰 높이와 그린 높이가 달라지고, 요소는 자기 글자가 실제로
        // 차지하는 것보다 크거나 작은 자리를 갖는다.
        //
        // 이때 재는 요청과 그리는 요청은 maxWidth가 같아지므로 캐시 키도 같다. 접기를
        // 쓰지 않는 글자는 양쪽 다 0이라 역시 한 항목이다. 두 경로가 서로 다른 폭을 넣는
        // 조합만이 같은 문자열을 두 항목으로 만들고, 그 조합이 생기지 않는 이유가 이 줄이다.
        if (const Runtime::LayoutElement* const layout =
                object->GetComponent<Runtime::LayoutElement>();
            layout && layout->IsWrapping())
        {
            if (const Runtime::RectTransform* const layoutRect =
                    object->GetComponent<Runtime::RectTransform>())
            {
                const Math::Vector2& padding = layout->GetPadding();
                request.maxWidth = (std::max)(
                    layoutRect->GetResolvedRect().width - padding.GetX() * textScale, 0.0f);
            }
        }
        request.lineSpacing = renderer->GetLineSpacing();
        request.alignment = ToPlatformAlignment(renderer->GetAlignment());
        if (screenSpace) request = Runtime::MakeScreenTextRequest(*object, *renderer);
        const bool emptyField = editing && request.text.empty();
        // 빈 필드도 캐럿을 배치할 줄 높이가 필요하다. 공백으로 글꼴 메트릭만 얻고,
        // 아래의 blockWidth는 0으로 유지해 입력 내용이나 배경 폭에 공백을 더하지 않는다.
        if (emptyField) request.text = " ";

        const std::shared_ptr<const ShapedText> shaped =
            textCache ? textCache->Resolve(request) : nullptr;
        if (!shaped || (!editing && shaped->runs.empty()))
        {
            continue;
        }

        const float blockWidth = emptyField ? 0.0f : static_cast<float>(shaped->width);
        const float blockHeight = static_cast<float>(shaped->height);
        const Math::Vector2& padding = renderer->GetBackgroundPadding();
        const float backgroundWidth = blockWidth + 2.0f * padding.GetX() * textScale;
        const float backgroundHeight = blockHeight + 2.0f * padding.GetY() * textScale;
        const Math::Color& backgroundColor = renderer->GetBackgroundColor();
        const bool hasBackground = backgroundColor.IsFinite() && backgroundColor.a > 0.0f &&
            std::isfinite(backgroundWidth) && backgroundWidth > 0.0f &&
            std::isfinite(backgroundHeight) && backgroundHeight > 0.0f;
        Math::Matrix4x4 textLocalToWorld = object->GetTransform().GetLocalToWorldMatrix();

        // 화면 공간 글자는 컬링 대상이 아니다 — 세계가 아니라 창 위에 놓인다. 월드 공간
        // 글자의 상자는 새로 재지 않는다: ITextMeasure가 이미 잰 블록의 폭·높이가 그것이고,
        // 「폭을 얻는 길은 하나」가 이 프로젝트의 규칙이라 컬링이 두 번째 길이 되면 안 된다.
        // 글리프 quad는 그 블록의 중심을 원점으로 만들어지므로(TextGlyphQuad의 계약이다)
        // 상자도 원점 중심이다.
        if (!screenSpace)
        {
            const float pixelsPerUnit = renderer->GetPixelsPerUnit();
            // 배경의 여백만 시야에 걸쳐도 그려야 하므로 같은 측정 블록을 여백만큼 확장한다.
            const float halfWidth = (hasBackground ? backgroundWidth : blockWidth) * 0.5f / pixelsPerUnit;
            const float halfHeight = (hasBackground ? backgroundHeight : blockHeight) * 0.5f / pixelsPerUnit;
            const Math::Aabb3D localBounds{
                { -halfWidth, -halfHeight, 0.0f }, { halfWidth, halfHeight, 0.0f } };
            if (!IsVisible(localBounds, textLocalToWorld))
            {
                continue;
            }
        }
        else
        {
            // 글자 draw의 이동은 블록이 시작될 자리가 아니라 블록의 <b>중심</b>이 놓일
            // 자리다. 글리프 quad가 블록 중심을 원점으로 만들어지기 때문이며 — 배치
            // 캐시가 폭과 높이의 절반을 빼서 담는다 — 그래서 좌상단을 그대로 이동으로
            // 쓰면 모든 라벨이 자기 폭의 절반, 높이의 절반만큼 왼쪽 위로 밀린다. 밀리는
            // 양이 문자열마다 다르므로 그 어긋남은 잡음처럼 보인다.
            //
            // 즉시 모드 UI가 쓰는 규약도 중심이다. 같은 규약을 여기서도 쓴다.
            if (const Runtime::RectTransform* const rectTransform =
                    object->GetComponent<Runtime::RectTransform>())
            {
                if (rectTransform->GetVisibleRect().IsEmpty())
                {
                    continue;
                }
                const Runtime::RectTransform::Rect& rect = rectTransform->GetResolvedRect();
                // 세로 자리는 배치된 블록의 높이를 알아야 정해진다. 그 높이는 글꼴과
                // 크기와 줄 수가 만든 것이라 여기서만 답할 수 있고, 그래서 위젯이
                // 어림잡는 대신 이 자리에서 가운데를 잡는다.
                const auto origin = Runtime::GetScreenTextOrigin(*renderer, rect, blockWidth, blockHeight);
                // 블록 중심은 정렬된 텍스트와 배경이 함께 쓴다. 여백은 크기만 늘리고,
                // 사각형 안에서 이미 결정된 글자 자리를 움직이지 않는다.
                textLocalToWorld = Math::Matrix4x4::CreateTranslation(
                    { origin.GetX() + blockWidth * 0.5f + (field ? field->GetTextOffsetX() : 0.0f),
                      origin.GetY() + blockHeight * 0.5f, 0.0f });
            }
            else
            {
                // 사각형 없이 준 화면 좌표는 글자가 시작할 자리다. 블록 중심을 원점으로
                // 만든 글리프와 배경을 함께 옮겨 같은 좌상단에 배치한다.
                const Math::Vector3 position = object->GetTransform().GetPosition();
                textLocalToWorld = Math::Matrix4x4::CreateTranslation(
                    { position.GetX() + blockWidth * 0.5f,
                      position.GetY() + blockHeight * 0.5f,
                      position.GetZ() });
            }
        }

        // 화면 공간 텍스트와 그 배경은 함께 오버레이다. 세계만 보는 뷰는 둘 다 받지 않는다.
        const RenderPass pass = screenSpace ? RenderPass::Overlay : RenderPass::Transparent;
        const Math::Vector3 textCenter = textLocalToWorld.GetTranslation();
        Runtime::RectTransform::Rect backgroundRect{
            textCenter.GetX() - backgroundWidth * 0.5f, textCenter.GetY() - backgroundHeight * 0.5f,
            backgroundWidth, backgroundHeight };
        if (textClip) backgroundRect = backgroundRect.IntersectedWith(*textClip);
        if (hasBackground && (!textClip || !backgroundRect.IsEmpty()))
        {
            SpriteDraw background;
            background.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
            background.material = builder.AddMaterial({ renderPass.GetSolidTexture() });
            background.tint = backgroundColor;
            background.pixelsPerUnit = renderer->GetPixelsPerUnit();
            background.space = screenSpace ? DrawSpace::Screen : DrawSpace::World;
            // 공유 흰 텍스처는 1픽셀이다. 기존 shaped 블록만 늘려 쓰며, 월드에서는
            // SpriteDraw의 PPU가, 화면에서는 이미 적용한 Canvas 배율이 단위를 맞춘다.
            background.localToWorld = Math::Matrix4x4::CreateScale(
                { backgroundWidth, backgroundHeight, 1.0f }) * textLocalToWorld;
            if (textClip)
                background.localToWorld = Math::Matrix4x4::CreateScale(
                    { backgroundRect.width, backgroundRect.height, 1.0f }) *
                    Math::Matrix4x4::CreateTranslation({ backgroundRect.GetCenterX(), backgroundRect.GetCenterY(), 0 });
            // 글자와 같은 순서·ID·깊이를 써 stable sort가 이 삽입 순서를 유지한다.
            // sortingOrder를 낮추지 않으므로 다른 오브젝트 사이의 순서는 변하지 않는다.
            static_cast<void>(builder.TryAddDraw(
                pass,
                std::move(background),
                renderer->GetSortingOrder(),
                renderer->GetInstanceId(), GetOverlayOrder(object, screenSpace)));
        }

        const auto addDecoration = [&](Runtime::RectTransform::Rect rect, const Math::Color& color)
        {
            if (textClip) rect = rect.IntersectedWith(*textClip);
            if (rect.IsEmpty()) return;
            SpriteDraw draw;
            draw.pipeline = builder.AddPipeline({ PipelineKind::Sprite });
            draw.material = builder.AddMaterial({ renderPass.GetSolidTexture() });
            draw.tint = color;
            draw.space = DrawSpace::Screen;
            draw.localToWorld = Math::Matrix4x4::CreateScale({ rect.width, rect.height, 1.0f }) *
                Math::Matrix4x4::CreateTranslation({ rect.GetCenterX(), rect.GetCenterY(), 0.0f });
            static_cast<void>(builder.TryAddDraw(RenderPass::Overlay, std::move(draw),
                renderer->GetSortingOrder(), renderer->GetInstanceId(), GetOverlayOrder(object, screenSpace)));
        };
        if (editing)
            for (const auto& rect : field->GetSelectionRects()) addDecoration(rect, { .18f, .42f, .85f, .45f });

        // 배경은 블록당 하나이고, 여러 아틀라스 페이지에 걸친 글자는 페이지마다 draw 하나다.
        for (const ShapedTextRun& textRun : shaped->runs)
        {
            TextDraw draw;
            draw.pipeline = builder.AddPipeline({ PipelineKind::Text });
            draw.page = textRun.page;
            draw.glyphs = textRun.glyphs;
            if (textClip) draw.glyphs = ClipScreenGlyphs(draw.glyphs, textCenter, *textClip);
            if (!draw.glyphs || draw.glyphs->empty()) continue;
            draw.tint = renderer->GetColor();
            draw.pixelsPerUnit = renderer->GetPixelsPerUnit();
            draw.space = screenSpace ? TextSpace::Screen : TextSpace::World;
            draw.localToWorld = textLocalToWorld;
            static_cast<void>(builder.TryAddDraw(
                pass,
                std::move(draw),
                renderer->GetSortingOrder(),
                renderer->GetInstanceId(), GetOverlayOrder(object, screenSpace)));
        }
        if (editing)
        {
            for (const auto& rect : field->GetCompositionRects()) addDecoration(rect, renderer->GetColor());
            if (field->IsCaretVisible()) addDecoration(field->GetCaretRect(), renderer->GetColor());
        }
    }
    }

    void FrameCollector::CollectCameras(const Runtime::GameObject* const object)
    {
        for (const Runtime::Camera* camera : object->GetComponents<Runtime::Camera>())
        if (camera->IsActiveAndEnabled() &&
            (camera->GetPriority() > priority ||
             (camera->GetPriority() == priority &&
             (camera->GetInstanceId() < activeCameraInstanceId))))
        {
            CameraRenderData cameraData;
            cameraData.view = camera->GetViewMatrix();
            cameraData.projection = camera->GetProjectionMatrix();
            cameraData.clearColor = camera->GetClearColor();
            builder.SetCamera(cameraData);
            priority = camera->GetPriority();
            activeCameraInstanceId = camera->GetInstanceId();

            // 직교 카메라만 컬링을 켠다. 원근은 절두체가 필요한데(평면 여섯, 상자 여덟 꼭짓점)
            // 오늘 원근으로 그리는 2D 장면이 없어 그 값을 아직 안 쓴다 — 절두체 없이 거르면
            // 틀린 상자로 거르는 버그가 된다. 카메라가 원근이면 nullopt로 되돌려, 뒤이은 모든
            // 부류가 거르지 않고 전부 그리게 한다.
            if (camera->GetProjectionMode() == Runtime::Camera::ProjectionMode::Orthographic)
            {
                viewCulling.emplace(cameraData, false);
            }
            else
            {
                viewCulling.reset();
            }
        }
    }

    void FrameCollector::Finish(const Runtime::Game& game)
    {
    // Every frame leaves the frontend with camera state so no backend has to invent one.
    if (!builder.HasCamera())
    {
        builder.SetCamera(MakeDefaultCamera(game.GetRenderAspectRatio()));
    }

    // 광원이 없는 장면에는 프론트엔드의 기본 조명을 적용한다.
    // 빛을 하나라도 둔 장면은 자기 조명을 온전히 소유한다.
    std::ranges::sort(lights, {}, &std::pair<unsigned int, LightRenderData>::first);
    for (const auto& light : lights | std::views::values)
    {
        if (!builder.AddLight(light))
        {
            break;
        }
    }
    if (lights.empty() && !hasAmbientLight)
    {
        builder.AddAmbientLight(DefaultAmbientLight);
        static_cast<void>(builder.AddLight(MakeDefaultDirectionalLight()));
    }
    }
}

void SceneRenderPass::Collect(const Runtime::Game& game, RenderFrameBuilder& builder)
{
    if (mTextCache)
    {
        mTextCache->BeginFrame();
    }

    FrameCollector collector{
        *this, game.GetAssetDatabase(), builder, game.GetSceneManager(), mTextCache.get() };
    if (const CameraRenderData* const viewCamera = builder.GetLockedCamera())
    {
        // 카메라만 바꿔 그려도 이미 버린 타일은 돌아오지 않는다.
        // draw를 수집하기 전에 그 뷰 전용 카메라로 가시 범위도 결정한다.
        collector.viewCulling.emplace(*viewCamera);
    }
    // 카메라를 먼저 정해야 컬링이 이 프레임의 가시 범위를 알 수 있다. 선택은 우선순위가
    // 높은 카메라를 택하고 동률이면 id가 낮은 것을 택하므로 방문 순서에 의존하지 않는다.
    //
    // 카메라 선택과 draw 수집에 오브젝트를 두 번 순회한다. 지난 프레임의 카메라를 재사용하면
    // 카메라가 움직일 때 컬링이 한 프레임 늦어지므로 현재 카메라를 먼저 구한다.
    if (!collector.viewCulling)
    {
        for (const auto& scene : game.GetSceneManager().GetActiveScenes() | std::views::values)
        for (const auto& object : scene->GetGameObjects() | std::views::values)
        {
            collector.CollectCameras(object.get());
        }
    }

    for (const auto& scene : game.GetSceneManager().GetActiveScenes() | std::views::values)
    for (const auto& object : scene->GetGameObjects() | std::views::values)
    {
        // 부류의 순서가 곧 같은 오브젝트 안에서의 그리기 순서다. 바꾸면 겹친 것의 위아래가
        // 바뀐다.
        collector.CollectLights(object.get());
        collector.CollectMeshes(object.get());
        collector.CollectSkinnedMeshes(object.get());
        collector.CollectSprites(object.get());
        collector.CollectTilemaps(object.get());
        collector.CollectTexts(object.get());
    }

    collector.Finish(game);
}
}
