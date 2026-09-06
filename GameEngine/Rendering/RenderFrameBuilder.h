#pragma once

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>
/// Runtime 상태를 API 독립적 RenderFrame 스냅샷으로 조립한다.
/// 조립이 끝난 뒤에는 Build를 통해 읽기 전용 프레임을 백엔드에 전달한다.
/// </summary>
class RenderFrameBuilder final
{
public:
    void SetCamera(CameraRenderData camera)
    {
        if (!mCameraLocked)
        {
            mFrame.mCamera = std::move(camera);
        }
    }

    /// <summary>
    /// 카메라를 확정하고, 이후의 SetCamera를 무시하게 한다.
    ///
    /// 장면 밖에서 결정된 카메라 — 에디터의 씬 뷰 카메라가 그것이다 — 로 프레임을 만들 때
    /// 쓴다. 프론트엔드는 장면에서 찾은 카메라를 SetCamera로 제공하는데, 잠그지 않으면 장면의
    /// 카메라가 언제나 이겨서 장면을 다른 시점에서 볼 방법이 없다.
    /// </summary>
    void LockCamera(CameraRenderData camera)
    {
        mFrame.mCamera = std::move(camera);
        mCameraLocked = true;
    }

    /// <summary>
    /// 이 뷰가 장면 밖에서 지정한 카메라다. 프론트엔드는 draw를 거르기 전에 이것을 읽어야
    /// 그릴 때의 시점과 컬링할 때의 시점이 일치한다. 장면 카메라 선택을 쓰면 nullptr이다.
    /// </summary>
    [[nodiscard]] const CameraRenderData* GetLockedCamera() const
    {
        return mCameraLocked && mFrame.mCamera ? &*mFrame.mCamera : nullptr;
    }

    /// <summary>같은 builder를 공유하는 다른 frontend가 이미 카메라를 제공했는지 확인한다.</summary>
    [[nodiscard]] bool HasCamera() const { return mFrame.mCamera.has_value(); }

    /// <summary>이 프레임이 대상으로 하는 렌더 타깃의 픽셀 크기를 기록한다.</summary>
    void SetRenderTargetSize(const RenderTargetSize size) { mFrame.mRenderTargetSize = size; }

    [[nodiscard]] RenderTargetSize GetRenderTargetSize() const { return mFrame.mRenderTargetSize; }

    /// <summary>
    /// 광원을 싣는다. 프레임이 이미 MaxFrameLights개를 실었거나 광원이 기술할 수 없는 것이면
    /// false를 돌려주고 싣지 않는다. 어느 광원이 자리를 얻는지는 부르는 순서, 즉 프론트엔드의
    /// 결정이다.
    /// </summary>
    [[nodiscard]] bool AddLight(const LightRenderData& light)
    {
        if (mFrame.mLights.size() >= MaxFrameLights || !light.IsValid())
        {
            return false;
        }
        mFrame.mLights.push_back(light);
        return true;
    }

    /// <summary>주변광을 더한다. 장면에 주변광이 여럿이면 합이 프레임의 주변광이다.</summary>
    void AddAmbientLight(const Math::Color& color)
    {
        mFrame.mAmbientLight = {
            mFrame.mAmbientLight.r + color.r,
            mFrame.mAmbientLight.g + color.g,
            mFrame.mAmbientLight.b + color.b,
            1.0f };
    }

    // 리소스 id는 같은 payload의 중복 등록을 막고, 반환 핸들은 이 builder가 만드는
    // 프레임 안에서만 유효하다. 다음 프레임이나 다른 builder의 핸들과 섞지 않는다.
    [[nodiscard]] GeometryHandle AddGeometry(Geometry geometry)
    {
        const std::uint64_t key = geometry.data ? geometry.data->id : 0;
        if (const auto existing = mGeometryHandles.find(key); existing != mGeometryHandles.end())
        {
            return existing->second;
        }

        const GeometryHandle handle{ static_cast<std::uint32_t>(mFrame.mGeometries.size()) };
        mFrame.mGeometries.push_back(std::move(geometry));
        mGeometryHandles.emplace(key, handle);
        return handle;
    }

    [[nodiscard]] SkinnedGeometryHandle AddSkinnedGeometry(SkinnedGeometry geometry)
    {
        const std::uint64_t key = geometry.data ? geometry.data->id : 0;
        if (const auto existing = mSkinnedGeometryHandles.find(key);
            existing != mSkinnedGeometryHandles.end())
        {
            return existing->second;
        }

        const SkinnedGeometryHandle handle{
            static_cast<std::uint32_t>(mFrame.mSkinnedGeometries.size()) };
        mFrame.mSkinnedGeometries.push_back(std::move(geometry));
        mSkinnedGeometryHandles.emplace(key, handle);
        return handle;
    }

    [[nodiscard]] MaterialHandle AddMaterial(Material material)
    {
        const std::uint64_t key = material.baseColorTexture ? material.baseColorTexture->id : 0;
        if (const auto existing = mMaterialHandles.find(key); existing != mMaterialHandles.end())
        {
            return existing->second;
        }

        const MaterialHandle handle{ static_cast<std::uint32_t>(mFrame.mMaterials.size()) };
        mFrame.mMaterials.push_back(std::move(material));
        mMaterialHandles.emplace(key, handle);
        return handle;
    }

    [[nodiscard]] PipelineHandle AddPipeline(Pipeline pipeline)
    {
        const auto key = static_cast<unsigned char>(pipeline.kind);
        if (const auto existing = mPipelineHandles.find(key); existing != mPipelineHandles.end())
        {
            return existing->second;
        }

        const PipelineHandle handle{ static_cast<std::uint32_t>(mFrame.mPipelines.size()) };
        mFrame.mPipelines.push_back(std::move(pipeline));
        mPipelineHandles.emplace(key, handle);
        return handle;
    }

    /// <summary>
    /// 이 프레임이 오버레이 패스를 받을지이다. 기본은 받는다. 편집 카메라로 세계를 보는 씬 뷰가
    /// 이것을 끄고, 그러면 오버레이 draw는 추가되는 대신 조용히 버려진다.
    /// </summary>
    void SetOverlayEnabled(const bool enabled) { mOverlayEnabled = enabled; }

    template <typename T>
    [[nodiscard]] bool TryAddDraw(
        const RenderPass pass,
        T draw,
        const int sortingOrder = 0,
        const unsigned int instanceId = 0,
        const std::uint64_t overlayStackOrder = 0)
    {
        static_assert(
            std::is_same_v<std::remove_cvref_t<T>, MeshDraw> ||
            std::is_same_v<std::remove_cvref_t<T>, SpriteDraw> ||
            std::is_same_v<std::remove_cvref_t<T>, TextDraw> ||
            std::is_same_v<std::remove_cvref_t<T>, TilemapDraw> ||
            std::is_same_v<std::remove_cvref_t<T>, SkinnedMeshDraw>,
            "DrawPacket payload must be a supported draw type.");
        if (pass >= RenderPass::Count)
        {
            return false;
        }
        // 오버레이를 끈 프레임은 오버레이 draw를 받지 않는다. 프론트엔드가 뷰마다 다르게 수집하는
        // 대신, 뷰가 프레임에 무엇을 원하는지 말하고 프론트엔드는 언제나 같은 것을 낸다.
        if (pass == RenderPass::Overlay && !mOverlayEnabled)
        {
            return false;
        }
        mFrame.mDrawPacketQueues[static_cast<std::size_t>(pass)].push_back(
            { pass, sortingOrder, instanceId, 0.0f, std::move(draw), overlayStackOrder });
        return true;
    }

    [[nodiscard]] RenderFrame Build() &&
    {
        // 투명 draw의 깊이는 프레임의 카메라로 여기서 잰다. 프론트엔드가 재면 카메라를 아직 못
        // 만난 draw — 같은 걷기에서 카메라보다 먼저 나온 것 — 가 틀린 값을 갖는다.
        if (mFrame.mCamera)
        {
            for (DrawPacket& packet :
                mFrame.mDrawPacketQueues[static_cast<std::size_t>(RenderPass::Transparent)])
            {
                const Math::Vector3 origin = std::visit([](const auto& draw)
                {
                    return draw.localToWorld.GetTranslation();
                }, packet.payload);
                packet.viewDepth = mFrame.mCamera->view.TransformPoint(origin).GetZ();
            }
        }
        for (std::vector<DrawPacket>& queue : mFrame.mDrawPacketQueues)
        {
            std::ranges::stable_sort(queue, DrawPacketPrecedes);
        }
        return std::move(mFrame);
    }

private:
    RenderFrame mFrame;
    bool mCameraLocked = false;
    std::unordered_map<std::uint64_t, GeometryHandle> mGeometryHandles;
    std::unordered_map<std::uint64_t, SkinnedGeometryHandle> mSkinnedGeometryHandles;
    std::unordered_map<std::uint64_t, MaterialHandle> mMaterialHandles;
    std::unordered_map<unsigned char, PipelineHandle> mPipelineHandles;
    bool mOverlayEnabled = true;
};

}
