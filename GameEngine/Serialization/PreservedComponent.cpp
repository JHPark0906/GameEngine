#include "pch.h"
#include "PreservedComponent.h"

#include <memory>
#include <string>
#include <utility>

namespace GameEngine::Serialization
{

const Runtime::ComponentType& PreservedComponent::StaticType()
{
    static const Runtime::ComponentType type{
        "PreservedComponent", &Runtime::Component::StaticType() };
    return type;
}

void PreservedComponent::SetData(Core::Json data)
{
    mTypeName = data.Value("type", std::string{});
    mData = std::move(data);
}

std::unique_ptr<Runtime::Component> PreservedComponent::Clone() const
{
    auto clone = std::make_unique<PreservedComponent>();
    clone->SetData(mData);
    return clone;
}

}
