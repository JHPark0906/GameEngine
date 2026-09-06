#pragma once

#include <memory>
#include <vector>


namespace GameEngine::Rendering
{
class IRenderFrontend;
class TextRasterizationCache;
}

namespace GameEngine::SceneRendering
{

/// <summary>엔진이 기본으로 세우는 프론트엔드 묶음이다: 장면 렌더 패스 하나.</summary>
class RenderFrontendFactory final
{
public:
    /// <summary>기본 프론트엔드 묶음을 세운다.</summary>
    /// <param name="textCache">
    /// 이 세계의 글자 배치 캐시다. UI 배치의 잣대가 같은 것을 나눠 쥐어야 재는 폭과 그리는
    /// 폭이 갈라지지 않는다. null이면 그 패스는 텍스트를 그리지 않는다.
    /// </param>
    [[nodiscard]] static std::vector<std::unique_ptr<Rendering::IRenderFrontend>> CreateDefault(
        std::shared_ptr<Rendering::TextRasterizationCache> textCache);
};

}
