#pragma once

#include <optional>
#include <span>

namespace GameEngine::Platform
{

/// <summary>
/// 화면 좌표의 사각형이다. 오른쪽과 아래는 포함하지 않는다 — 붙어 있는 두 모니터가 겹친 것으로
/// 읽히지 않게 하는 규약이며, 창 시스템들이 쓰는 것과 같다.
/// </summary>
struct ScreenRectangle
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] constexpr int GetWidth() const { return right - left; }
    [[nodiscard]] constexpr int GetHeight() const { return bottom - top; }
    [[nodiscard]] constexpr bool IsValid() const { return GetWidth() > 0 && GetHeight() > 0; }
    [[nodiscard]] constexpr bool operator==(const ScreenRectangle&) const = default;
};

/// <summary>두 사각형이 한 픽셀이라도 겹치는지다.</summary>
[[nodiscard]] constexpr bool RectanglesOverlap(
    const ScreenRectangle& left, const ScreenRectangle& right)
{
    return left.IsValid() && right.IsValid() && left.left < right.right &&
        right.left < left.right && left.top < right.bottom && right.top < left.bottom;
}

/// <summary>
/// 전체화면으로 가기 전의 창 자리를 기억했다가, 돌아올 자리를 답한다.
///
/// 순수한 값 계산이라 창 시스템 없이 시험된다. 실제로 창을 옮기는 일은 플랫폼 구현의 몫이고,
/// 여기서 정하는 것은 "어디로 돌아가야 하는가"뿐이다.
/// </summary>
class WindowPlacementMemory final
{
public:
    /// <summary>전체화면으로 가기 직전의 자리를 기억한다. 이미 기억한 것이 있으면 덮지 않는다.</summary>
    /// <param name="placement">창이 있던 화면 사각형이다.</param>
    void Remember(const ScreenRectangle& placement)
    {
        // 전체화면인 채로 다시 전체화면을 요청해도 기억이 전체화면 사각형으로 덮이면 안 된다.
        // 그러면 돌아갈 자리가 사라진다.
        if (!mRemembered)
        {
            mRemembered = placement;
        }
    }

    [[nodiscard]] bool HasRemembered() const { return mRemembered.has_value(); }

    /// <summary>기억을 지운다. 창으로 돌아온 뒤에 부른다.</summary>
    void Forget() { mRemembered.reset(); }

    /// <summary>
    /// 창으로 돌아갈 사각형이다.
    ///
    /// 기억한 자리가 지금 어느 모니터와도 겹치지 않으면 그 자리는 쓰지 않는다: 모니터를 뽑거나
    /// 해상도를 바꾼 뒤에 돌아가면 창이 보이지 않는 곳에 놓이고, 사람은 창을 잃은 것과 구별할 수
    /// 없다. 그럴 때는 기본 자리로 돌아간다.
    /// </summary>
    /// <param name="monitors">지금 붙어 있는 모니터들의 사각형이다.</param>
    /// <param name="fallback">기억한 자리를 쓸 수 없을 때 갈 자리다.</param>
    /// <returns>돌아갈 사각형이다. 기억한 것이 없으면 기본 자리다.</returns>
    [[nodiscard]] ScreenRectangle ResolveRestore(
        const std::span<const ScreenRectangle> monitors, const ScreenRectangle& fallback) const
    {
        if (!mRemembered || !mRemembered->IsValid())
        {
            return fallback;
        }
        for (const ScreenRectangle& monitor : monitors)
        {
            if (RectanglesOverlap(*mRemembered, monitor))
            {
                return *mRemembered;
            }
        }
        return fallback;
    }

private:
    std::optional<ScreenRectangle> mRemembered;
};

}
