#include "pch.h"
#include "Object.h"

#include "ObjectRegistry.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace GameEngine::Runtime
{

Object::Object(ObjectRegistry& objectRegistry, std::string name)
    : mName(std::move(name))
{
    RegisterWith(objectRegistry);
}

Object::Object(std::string name)
    : mName(std::move(name))
{
}

Object::~Object()
{
    if (mObjectRegistry != nullptr)
    {
        mObjectRegistry->UnregisterObject(mInstanceId);
    }
}

void Object::RegisterWith(ObjectRegistry& objectRegistry)
{
    if (mObjectRegistry)
    {
        throw std::logic_error("An Object cannot be registered twice.");
    }

    mObjectRegistry = &objectRegistry;
    mInstanceId = mObjectRegistry->RegisterObject(this);
}

ObjectRegistry& Object::GetObjectRegistry() const
{
    if (!mObjectRegistry)
    {
        throw std::logic_error("This Object has not been registered with a registry.");
    }

    return *mObjectRegistry;
}

}
