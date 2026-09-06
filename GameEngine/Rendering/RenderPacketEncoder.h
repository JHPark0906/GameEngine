#pragma once

#include <type_traits>
#include <variant>

#include "MeshRenderPass.h"
#include "RenderBackendInterfaces.h"
#include "RenderCommandList.h"
#include "RenderFrame.h"
#include "SpriteRenderPass.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 검증된 프레임의 draw 패킷을 공용 렌더 패스 호출로 바꾼다.
///
/// payload가 어느 패스에 속하는지는 백엔드 규칙이 아니라 프론트엔드의 결정이므로, 이것은 검증된
/// 큐가 건네는 것을 그대로 기록한다. 또 하나의 인터페이스가 아니라 리졸버에 대한 템플릿인
/// 이유는, 렌더러가 언제나 자기 백엔드의 리졸버를 구체 타입으로 쥐기 때문이다.
/// RenderResourceResolver concept이 그것을 타입 없는 결합이 되지 않게 지킨다.
/// </summary>
template <RenderResourceResolver TResolver>
class RenderPacketEncoder final : public ICommandEncoder, public IDrawPacketEncoder
{
public:
    RenderPacketEncoder(IRenderCommandList& commandList, TResolver& resourceResolver)
        : mCommandList(commandList), mResourceResolver(resourceResolver)
    {
        mMeshPass.BeginFrame();
        mSpritePass.BeginFrame();
    }

    void BeginPass(const RenderPass pass) override
    {
        mActivePass = pass;
    }

    void EndPass(const RenderPass pass) override
    {
        mCommandList.EndPass(pass);
    }

    void Encode(const RenderFrame& frame, const DrawPacket& packet) override
    {
        if (packet.pass != mActivePass)
        {
            return;
        }

        std::visit([&](const auto& draw)
        {
            using DrawType = std::decay_t<decltype(draw)>;
            if constexpr (std::is_same_v<DrawType, MeshDraw>)
            {
                const auto* const geometry = mResourceResolver.ResolveGeometry(frame, draw.geometry);
                const auto* const material = mResourceResolver.ResolveMaterial(frame, draw.material);
                if (geometry && material)
                {
                    mMeshPass.Draw(mCommandList, frame, draw, *geometry, *material);
                }
            }
            else if constexpr (std::is_same_v<DrawType, SpriteDraw>)
            {
                const auto* const material = mResourceResolver.ResolveMaterial(frame, draw.material);
                if (material)
                {
                    mSpritePass.DrawSprite(mCommandList, frame, draw, *material);
                }
            }
            else if constexpr (std::is_same_v<DrawType, TextDraw>)
            {
                const auto* const text = mResourceResolver.ResolveText(draw);
                if (text)
                {
                    mSpritePass.DrawTextQuad(mCommandList, frame, draw, *text);
                }
            }
            else if constexpr (std::is_same_v<DrawType, TilemapDraw>)
            {
                // 타일셋은 여느 텍스처와 같으므로 재질 리졸버가 그대로 답한다.
                const auto* const tileset = mResourceResolver.ResolveMaterial(frame, draw.material);
                if (tileset)
                {
                    mSpritePass.DrawTilemap(mCommandList, frame, draw, *tileset);
                }
            }
            else if constexpr (std::is_same_v<DrawType, SkinnedMeshDraw>)
            {
                const auto* const geometry =
                    mResourceResolver.ResolveSkinnedGeometry(frame, draw.geometry);
                const auto* const material = mResourceResolver.ResolveMaterial(frame, draw.material);
                if (geometry && material)
                {
                    mMeshPass.DrawSkinned(mCommandList, frame, draw, *geometry, *material);
                }
            }
            else
            {
                // payload에 종류가 늘고 이 분기를 추가하지 않으면 빌드가 실패한다.
                // 조건이 타입에 의존해야 해당 템플릿의 인스턴스화 시점에 검사된다.
                // false를 직접 쓰면 이 분기를 사용하지 않아도 템플릿을 읽는 순간 실패한다.
                static_assert(
                    sizeof(DrawType) == 0, "every draw payload needs its own branch here");
            }
        }, packet.payload);
    }

private:
    IRenderCommandList& mCommandList;
    TResolver& mResourceResolver;
    MeshRenderPass mMeshPass;
    SpriteRenderPass mSpritePass;
    RenderPass mActivePass = RenderPass::Opaque;
};

}
