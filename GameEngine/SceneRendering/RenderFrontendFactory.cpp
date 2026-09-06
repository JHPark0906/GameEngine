#include "pch.h"
#include "RenderFrontendFactory.h"

#include <memory>
#include <utility>
#include <vector>

#include "../Rendering/TextRasterizationCache.h"
#include "../Rendering/IRenderFrontend.h"
#include "SceneRenderPass.h"

namespace GameEngine::SceneRendering
{

std::vector<std::unique_ptr<Rendering::IRenderFrontend>> RenderFrontendFactory::CreateDefault(
    std::shared_ptr<Rendering::TextRasterizationCache> textCache)
{
    std::vector<std::unique_ptr<Rendering::IRenderFrontend>> frontends;
    frontends.push_back(std::make_unique<SceneRenderPass>(std::move(textCache)));
    return frontends;
}

}
