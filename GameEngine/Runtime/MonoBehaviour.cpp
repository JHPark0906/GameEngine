#include "pch.h"
#include "MonoBehaviour.h"

namespace GameEngine::Runtime
{

const ComponentType& MonoBehaviour::StaticType()
{
    static const ComponentType type{ "MonoBehaviour", &Behaviour::StaticType() };
    return type;
}
void MonoBehaviour::OnAttached()
{
    Awake();
}

void MonoBehaviour::OnRemoved()
{
    OnDestroy();
}

void MonoBehaviour::UpdateBehaviour(const float deltaTime)
{
    if (!mStarted)
    {
        mStarted = true;
        Start();
    }

    Update(deltaTime);
}

}
