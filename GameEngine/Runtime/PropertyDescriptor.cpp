#include "pch.h"
#include "PropertyDescriptor.h"

#include "ComponentType.h"

#include <string_view>
#include <vector>

namespace GameEngine::Runtime
{

PropertyRestoreScope::PropertyRestoreScope(Component& component)
    : mComponent(component), mWasRestoring(component.mRestoringProperties)
{
    mComponent.mRestoringProperties = true;
}

PropertyRestoreScope::~PropertyRestoreScope()
{
    mComponent.mRestoringProperties = mWasRestoring;
    if (!mWasRestoring)
    {
        mComponent.OnPropertiesRestored();
    }
}

std::vector<const PropertyDescriptor*> CollectProperties(const ComponentType& type)
{
    // 사슬을 뿌리까지 걷어 모은 뒤 거꾸로 붙인다: 기반의 속성이 먼저 온다.
    std::vector<const ComponentType*> chain;
    for (const ComponentType* current = &type; current != nullptr; current = current->GetBase())
    {
        chain.push_back(current);
    }

    std::vector<const PropertyDescriptor*> properties;
    for (auto link = chain.rbegin(); link != chain.rend(); ++link)
    {
        for (const PropertyDescriptor& descriptor : (*link)->GetOwnProperties())
        {
            properties.push_back(&descriptor);
        }
    }
    return properties;
}

const PropertyDescriptor* FindProperty(const ComponentType& type, const std::string_view name)
{
    for (const ComponentType* current = &type; current != nullptr; current = current->GetBase())
    {
        for (const PropertyDescriptor& descriptor : current->GetOwnProperties())
        {
            if (descriptor.GetName() == name)
            {
                return &descriptor;
            }
        }
    }
    return nullptr;
}

}
