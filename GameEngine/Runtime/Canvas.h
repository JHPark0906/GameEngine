#pragma once
#include "Component.h"

namespace GameEngine::Runtime
{

/// <summary>
/// UI 계층의 뿌리다. 이 컴포넌트가 붙은 게임 오브젝트의 아래가 하나의 UI 면이 되고, 그 면의
/// 바깥 사각형은 그리는 대상의 픽셀 크기 전체다.
///
/// 뿌리가 따로 있는 이유는 UI 배치가 절대 좌표가 아니라 언제나 무언가에 대한 비율이기 때문이다.
/// 가장 바깥 사각형을 누군가는 말해 주어야 하고, 그것은 장면이 아니라 그리는 면의 크기다.
/// 그래서 UI 계층은 이 컴포넌트에서 시작하며, 이것이 없는 <see cref="RectTransform"/>은 자기
/// 자리를 알 수 없어 배치되지 않는다.
/// </summary>
class Canvas final : public Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>
    /// 이 면의 모든 픽셀 거리에 곱해지는 배율이다. 앵커는 비율이라 영향을 받지 않고, 오프셋만
    /// 커진다 — 고해상도 화면에서 UI가 같은 크기로 보이게 하는 것이 이 한 값이다.
    /// </summary>
    [[nodiscard]] float GetScaleFactor() const { return mScaleFactor; }

    /// <summary>이 면의 픽셀 배율을 정한다. 0 이하는 1로 되돌린다.</summary>
    /// <param name="scaleFactor">새 배율이다.</param>
    void SetScaleFactor(const float scaleFactor)
    {
        mScaleFactor = scaleFactor > 0.0f ? scaleFactor : 1.0f;
    }

private:
    float mScaleFactor = 1.0f;
};

}
