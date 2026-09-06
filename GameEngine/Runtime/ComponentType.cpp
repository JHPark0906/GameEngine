#include "pch.h"
#include "ComponentType.h"

#include <algorithm>

#include "Component.h"
#include "PropertyDescriptor.h"

#include <memory>
#include <span>

namespace GameEngine::Runtime
{

std::span<const PropertyDescriptor> ComponentType::GetOwnProperties() const
{
    return mProperties != nullptr ? mProperties() : std::span<const PropertyDescriptor>{};
}

std::span<const ComponentType* const> ComponentType::GetOwnRequiredComponents() const
{
    return mRequirements != nullptr ? mRequirements()
                                    : std::span<const ComponentType* const>{};
}

std::vector<const ComponentType*> ComponentType::CollectRequiredComponents() const
{
    // 사슬을 걷는다. 기반이 요구한 것은 파생도 요구하는 것이므로, 파생 하나만 보고 답할 수
    // 없다. 같은 것을 두 번 담지 않는 이유는 부르는 쪽이 그 목록대로 붙이기 때문이다.
    std::vector<const ComponentType*> required;
    for (const ComponentType* type = this; type != nullptr; type = type->mBase)
    {
        for (const ComponentType* const one : type->GetOwnRequiredComponents())
        {
            if (one != nullptr &&
                std::find(required.begin(), required.end(), one) == required.end())
            {
                required.push_back(one);
            }
        }
    }
    return required;
}

std::unique_ptr<Component> ComponentType::CreateInstance() const
{
    return mCreateInstance != nullptr ? mCreateInstance() : nullptr;
}

}
