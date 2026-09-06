#pragma once

#include <array>

namespace GameEngine::Math
{

/// <summary>
/// 정점과 셰이더 상수가 공유하는 평범한 실수 묶음이다. 그래픽스 API의 벡터 타입이 아니라 plain
/// float라서 어느 API 계열의 백엔드도 이것을 그대로 쓰고, 에셋 계층은 렌더링을 모른 채 정점을
/// 담는다.
/// </summary>
struct Float2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Float3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Float4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

/// <summary>행 우선 4x4 행렬이다. 상수 버퍼가 받는 그대로의 형태다.</summary>
struct Float4x4
{
    std::array<float, 16> elements{};
};

}
