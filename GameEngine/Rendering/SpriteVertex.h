#pragma once

#include "../Math/Float.h"

namespace GameEngine::Rendering
{

/// <summary>sprite와 text 프로그램의 VS_INPUT과 일치한다.</summary>
struct SpriteVertex
{
    Math::Float2 position;
    Math::Float2 textureCoordinate;
};

}
