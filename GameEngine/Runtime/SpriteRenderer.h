#pragma once
#include "../Math/Color.h"
#include "../Math/Vector.h"
#include "Renderer.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{

/// <summary>2차원 스프라이트를 장면에 출력하는 렌더러 컴포넌트이다.</summary>
class SpriteRenderer final : public Renderer
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
    enum class DrawMode : unsigned char
    {
        Simple,
        Sliced,
    };

    /// <summary>
    /// 이 스프라이트가 놓이는 공간이다. 화면에 고정된 그림은 카메라가 움직여도
    /// 따라 움직이지 않아야 하므로 월드 공간과 화면 공간을 구분한다.
    /// </summary>
    enum class Space : unsigned char
    {
        /// <summary>장면 안의 물체다. 카메라가 자리를 정한다.</summary>
        World,
        /// <summary>화면 픽셀에 고정된 그림이다. 오버레이로 그려지며 카메라를 쓰지 않는다.</summary>
        Screen,
    };

    [[nodiscard]] const Assets::AssetReference& GetSprite() const { return mSprite; }

    void CollectAssetReferences(std::vector<Assets::AssetReference>& references) const override
    {
        Renderer::CollectAssetReferences(references);
        if (mSprite.IsValid())
        {
            references.push_back(mSprite);
        }
    }
    void SetSprite(Assets::AssetReference sprite) { mSprite = std::move(sprite); }

    [[nodiscard]] const Math::Color& GetColor() const { return mColor; }
    void SetColor(const Math::Color& color) { mColor = color; }

    [[nodiscard]] DrawMode GetDrawMode() const { return mDrawMode; }
    void SetDrawMode(DrawMode drawMode) { mDrawMode = drawMode; }

    [[nodiscard]] const Math::Vector2& GetSize() const { return mSize; }
    void SetSize(const Math::Vector2& size);

    /// <summary>
    /// 스프라이트 시트에서 보여 줄 프레임 번호다. 시트가 아닌 이미지는 프레임이 하나뿐이라
    /// 어떤 값이든 같은 그림이다. 전체 시트 왕복의 표본이 있으면 이 값은 시작 번호이며,
    /// 실제 표시 번호는 에셋의 프레임 수를 넘긴 ResolveFrame으로 얻는다.
    /// </summary>
    [[nodiscard]] int GetFrame() const { return mFrame; }
    void SetFrame(const int frame)
    {
        mFrame = frame;
        mSheetPingPong.reset();
    }

    /// <summary>시트 크기를 아직 모르는 왕복 재생의 시간 표본이다. 그리기 단계에서 에셋의 프레임 수로 해석한다.</summary>
    void SetSheetPingPongFrame(float elapsedSeconds, int firstFrame, float frameRate, bool loop);
    /// <summary>현재 에셋의 프레임 수로 표시 번호를 확정한다. 수동 SetFrame은 재생 표본을 해제한다.</summary>
    [[nodiscard]] int ResolveFrame(int sheetFrameCount) const;

    /// <summary>이 스프라이트가 장면 안에 있는지 화면에 고정돼 있는지다.</summary>
    [[nodiscard]] Space GetSpace() const { return mSpace; }
    void SetSpace(const Space space) { mSpace = space; }

    [[nodiscard]] bool IsFlippedX() const { return mFlipX; }
    void SetFlipX(bool flipX) { mFlipX = flipX; }
    [[nodiscard]] bool IsFlippedY() const { return mFlipY; }
    void SetFlipY(bool flipY) { mFlipY = flipY; }

private:
    Assets::AssetReference mSprite;
    Math::Color mColor = Math::Color::White;
    Math::Vector2 mSize = {1.0f, 1.0f};
    DrawMode mDrawMode = DrawMode::Simple;
    Space mSpace = Space::World;
    int mFrame = 0;
    struct SheetPingPongSample
    {
        float elapsedSeconds;
        int firstFrame;
        float frameRate;
        bool loop;
    };
    // 마지막 업데이트의 값만 보관하므로 Animator를 비활성화해도 표시가 유지된다.
    std::optional<SheetPingPongSample> mSheetPingPong;
    bool mFlipX = false;
    bool mFlipY = false;
};

}
