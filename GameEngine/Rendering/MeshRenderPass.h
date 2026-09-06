#pragma once

#include <cstddef>

#include "RenderCommandList.h"
#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 프레임의 mesh payload를 그린다. 이것은 그래픽 API마다 하나가 아니라 전체에 하나다: 메시를
/// 배치하고, 프레임이 그것을 기술하는지 확인하고, 프레임의 파이프라인 핸들을 존중하는 것은
/// 렌더링의 결정이며, IRenderCommandList 뒤의 제출만이 API 특유의 것이다.
/// </summary>
class MeshRenderPass final
{
public:
    /// <summary>새 프레임의 제출 예산을 시작한다. RenderPacketEncoder는 프레임마다 새 인스턴스를 만든다.</summary>
    void BeginFrame();

    /// <summary>
    /// 메시 draw 하나를 기록하거나, 프레임이나 resolve된 리소스가 그것을 기술할 수 없으면
    /// 건너뛴다. 건너뛰기는 오류가 아니다: 백엔드가 만들지 못한 리소스는 만들다 실패한 그
    /// 리졸버가 이미 보고했다.
    /// </summary>
    void Draw(
        IRenderCommandList& commandList,
        const RenderFrame& frame,
        const MeshDraw& draw,
        const IResolvedGeometry& geometry,
        const IResolvedTexture& material) const;

    /// <summary>스킨드 메시 draw 하나를 기록하거나, 건너뛴다. <see cref="Draw"/>와 같은 규칙이다.</summary>
    void DrawSkinned(
        IRenderCommandList& commandList,
        const RenderFrame& frame,
        const SkinnedMeshDraw& draw,
        const IResolvedGeometry& geometry,
        const IResolvedTexture& material);

private:
    std::size_t mSkinnedMeshCount = 0;
    bool mSkinnedBudgetReported = false;
};

}
