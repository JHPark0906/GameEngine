#include "pch.h"
#include "ObjectRegistry.h"

#include <limits>
#include <stdexcept>

namespace GameEngine::Runtime
{

unsigned int ObjectRegistry::RegisterObject(Object* object)
{
    if (!object)
    {
        throw std::invalid_argument("Cannot register a null Object.");
    }

    if (mNextInstanceId == (std::numeric_limits<unsigned int>::max)())
    {
        throw std::overflow_error("Object instance ID space is exhausted.");
    }

    const unsigned int instanceId = ++mNextInstanceId;
    mObjects.emplace(instanceId, object);
    return instanceId;
}

void ObjectRegistry::UnregisterObject(const unsigned int instanceId)
{
    mObjects.erase(instanceId);
}

Object* ObjectRegistry::FindObject(const unsigned int instanceId) const
{
    const auto iterator = mObjects.find(instanceId);
    return iterator == mObjects.end() ? nullptr : iterator->second;
}

}
