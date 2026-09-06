#pragma once

#include <memory>
#include <utility>

#include "../Platform/ITextRasterizer.h"
#include "../Rendering/IRenderFrontend.h"
#include "../Assets/TextureData.h"
#include "../Rendering/TextRasterizationCache.h"

namespace GameEngine::SceneRendering
{

/// <summary>
/// 장면을 프레임으로 바꾸는 프론트엔드다. 런타임의 장면을 걸어 렌더러 컴포넌트를 draw로,
/// 카메라를 프레임의 카메라로 옮긴다. 렌더링 코어 위, 런타임 위에 있는 다리라서 두 모듈을
/// 모두 알지만 둘 중 어느 것도 이것을 모른다.
/// </summary>
class SceneRenderPass final : public Rendering::IRenderFrontend
{
public:
    /// <summary>
    /// 이 패스가 글자를 배치할 때 쓸 캐시를 받는다. 소유하지 않고 나눠 쥐는 이유는 같은
    /// 세계의 UI 배치가 <b>같은 캐시로 재야</b> 하기 때문이다: 재는 캐시와 그리는 캐시가
    /// 다르면 한쪽에만 등록된 글꼴이 두 값의 폭을 만들고, 그 어긋남은 로그도 실패도 없이
    /// 라벨이 자기 글자보다 좁거나 넓은 자리를 갖는 것으로만 드러난다.
    /// </summary>
    /// <param name="textCache">
    /// 이 세계의 글자 배치 캐시다. null이면 이 패스는 텍스트를 그리지 않는다.
    /// </param>
    explicit SceneRenderPass(std::shared_ptr<Rendering::TextRasterizationCache> textCache)
        : mTextCache(std::move(textCache))
    {
    }

    void Collect(const Runtime::Game& game, Rendering::RenderFrameBuilder& builder) override;

    /// <summary>
    /// 그림 없는 UI 요소가 쓰는 1x1 흰 픽셀이다. 색은 draw의 틴트가 정하므로 이 하나면
    /// 모든 단색 사각형이 그려진다. 즉시 모드 UI가 쓰는 것과 같은 리소스 id를 쓴다 —
    /// 픽셀이 글자 그대로 같으므로 백엔드가 한 번만 올리고 양쪽이 나눠 쓴다.
    /// </summary>
    /// <returns>1x1 흰 텍스처다. 처음 필요할 때 만들어진다.</returns>
    [[nodiscard]] const std::shared_ptr<const Assets::TextureData>& GetSolidTexture();

    std::shared_ptr<const Assets::TextureData> mSolidTexture;

private:
    /// <summary>이 세계의 글자 배치 캐시다. UI 배치의 잣대와 같은 것을 나눠 쥔다.</summary>
    std::shared_ptr<Rendering::TextRasterizationCache> mTextCache;
};

}
