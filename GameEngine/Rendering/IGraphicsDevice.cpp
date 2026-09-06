#include "pch.h"
#include "IGraphicsDevice.h"

#include <span>

namespace GameEngine::Rendering
{

void IGraphicsDevice::RenderToImages(const std::span<CaptureBatchItem> items)
{
    for (CaptureBatchItem& item : items)
    {
        item.succeeded = RenderToImage(item.frame, item.image, { item.channel, false });
    }
}

}
