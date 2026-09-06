#pragma once
#include "../Math/Color.h"
#include "Behaviour.h"

#include <memory>

namespace GameEngine::Runtime
{

/// <summary>
/// 장면을 비추는 광원이다.
///
/// 방향광은 자기 Transform의 앞(+Z)을 따라 비추고 위치는 무시한다. 점광은 자기 위치에서 range
/// 안쪽을 비춘다. 주변광은 방향도 위치도 없이 모든 면에 같은 빛을 더한다 — Unity가 환경 설정에
/// 두는 것을 이 엔진은 장면 안의 컴포넌트로 둔다. 장면이 자기 조명을 완전히 기술하면 프론트엔드가
/// 프레임에 그대로 옮기면 되고, 백엔드는 어떤 조명도 지어내지 않는다.
/// </summary>
class Light final : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    enum class Kind
    {
        Directional,
        Point,
        Ambient,
    };

    [[nodiscard]] Kind GetKind() const { return mKind; }
    void SetKind(const Kind kind) { mKind = kind; }

    [[nodiscard]] const Math::Color& GetColor() const { return mColor; }
    void SetColor(const Math::Color& color) { mColor = color; }

    /// <summary>색에 곱해지는 세기다. 음수는 0이 된다.</summary>
    [[nodiscard]] float GetIntensity() const { return mIntensity; }
    void SetIntensity(float intensity);

    /// <summary>점광이 닿는 거리다. 월드 단위이며 방향광과 주변광은 무시한다.</summary>
    [[nodiscard]] float GetRange() const { return mRange; }
    void SetRange(float range);

private:
    Kind mKind = Kind::Directional;
    Math::Color mColor = Math::Color::White;
    float mIntensity = 1.0f;
    float mRange = 10.0f;
};

}
