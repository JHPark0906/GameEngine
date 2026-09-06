#include "pch.h"
#include "AudioListener.h"

namespace GameEngine::Runtime
{

const ComponentType& AudioListener::StaticType()
{
    static const ComponentType type{
        "AudioListener", &Behaviour::StaticType(), nullptr, &MakeComponentInstance<AudioListener> };
    return type;
}

}
