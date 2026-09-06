#include "pch.h"
#include "GameBootstrapRegistry.h"
#include "../Diagnostics/Debug.h"

#include <memory>

namespace GameEngine::App
{

namespace
{
    GameBootstrapRegistry::Factory gFactory = nullptr;
}

bool GameBootstrapRegistry::Register(const Factory factory)
{
    if (!factory)
    {
        return false;
    }
    if (gFactory && gFactory != factory)
    {
        Diagnostics::Debug::LogError(
            "A game bootstrap is already registered; an executable runs exactly one project.");
        return false;
    }
    gFactory = factory;
    return true;
}

std::unique_ptr<IGameBootstrap> GameBootstrapRegistry::Create()
{
    return gFactory ? gFactory() : nullptr;
}

void GameBootstrapRegistry::Reset()
{
    gFactory = nullptr;
}

}
