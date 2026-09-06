#include "AppTimingTests.h"

#include <array>
#include <iostream>
#include <span>

#include "Platform/WindowPlacement.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Platform::RectanglesOverlap;
    using GameEngine::Platform::ScreenRectangle;
    using GameEngine::Platform::WindowPlacementMemory;

}

bool RunWindowPlacementTests()
{
    constexpr ScreenRectangle PrimaryMonitor{ 0, 0, 1920, 1080 };
    constexpr ScreenRectangle SecondMonitor{ 1920, 0, 3840, 1080 };
    constexpr ScreenRectangle Fallback{ 0, 0, 1280, 720 };

    const std::array bothMonitors{ PrimaryMonitor, SecondMonitor };
    const std::array primaryOnly{ PrimaryMonitor };

    // 맞닿은 두 모니터가 겹친 것으로 읽히면, 두 번째 모니터에 있던 창이 첫 번째에 있다고
    // 판정된다. 오른쪽 경계를 포함하지 않는 규약이 그것을 막는다.
    const bool touchingIsNotOverlapping = !RectanglesOverlap(PrimaryMonitor, SecondMonitor);
    const bool overlapIsSymmetric =
        RectanglesOverlap({ 1900, 100, 2000, 200 }, PrimaryMonitor) &&
        RectanglesOverlap(PrimaryMonitor, { 1900, 100, 2000, 200 });
    const bool emptyOverlapsNothing = !RectanglesOverlap({ 10, 10, 10, 10 }, PrimaryMonitor);

    // 기억한 것이 없으면 갈 곳은 기본 자리다.
    WindowPlacementMemory empty;
    const bool emptyMemoryFallsBack =
        !empty.HasRemembered() &&
        empty.ResolveRestore(bothMonitors, Fallback) == Fallback;

    // 평범한 왕복: 기억한 자리가 그대로 돌아온다.
    WindowPlacementMemory remembered;
    constexpr ScreenRectangle Windowed{ 100, 120, 1100, 820 };
    remembered.Remember(Windowed);
    const bool roundTripKeepsThePlacement =
        remembered.HasRemembered() &&
        remembered.ResolveRestore(bothMonitors, Fallback) == Windowed;

    // 전체화면인 채로 다시 전체화면을 요청해도 기억이 덮이면 안 된다. 덮이면 돌아갈 자리가
    // 전체화면 사각형이 되어 창으로 영영 못 돌아온다.
    remembered.Remember({ 0, 0, 1920, 1080 });
    const bool secondRememberIsIgnored =
        remembered.ResolveRestore(bothMonitors, Fallback) == Windowed;

    // 창으로 돌아온 뒤에는 기억을 지운다.
    remembered.Forget();
    const bool forgettingReturnsToFallback =
        !remembered.HasRemembered() &&
        remembered.ResolveRestore(bothMonitors, Fallback) == Fallback;

    // 두 번째 모니터에 있던 창인데 그 모니터가 사라졌다. 그 자리로 돌려보내면 창은 보이지
    // 않는 곳에 놓이고, 사람은 창을 잃은 것과 구별할 수 없다.
    WindowPlacementMemory onLostMonitor;
    constexpr ScreenRectangle OnSecond{ 2000, 100, 3000, 800 };
    onLostMonitor.Remember(OnSecond);
    const bool visibleWhileTheMonitorIsThere =
        onLostMonitor.ResolveRestore(bothMonitors, Fallback) == OnSecond;
    const bool fallsBackWhenTheMonitorIsGone =
        onLostMonitor.ResolveRestore(primaryOnly, Fallback) == Fallback;

    // 해상도가 줄어 창이 화면 밖으로 나간 경우도 같다.
    WindowPlacementMemory offScreen;
    offScreen.Remember({ 2400, 1400, 3000, 1800 });
    const bool fallsBackWhenTheResolutionShrank =
        offScreen.ResolveRestore(primaryOnly, Fallback) == Fallback;

    // 한 귀퉁이만 걸쳐 있어도 사람은 창을 잡아 끌 수 있다. 그것은 잃은 창이 아니다.
    WindowPlacementMemory partlyVisible;
    partlyVisible.Remember({ 1850, 900, 2500, 1400 });
    const bool aPartlyVisibleWindowIsKept =
        partlyVisible.ResolveRestore(primaryOnly, Fallback) != Fallback;

    // 모니터를 하나도 못 읽는 경우에도 답이 있어야 한다.
    const bool noMonitorsFallBack =
        partlyVisible.ResolveRestore(std::span<const ScreenRectangle>{}, Fallback) == Fallback;

    return Expect(
               touchingIsNotOverlapping && overlapIsSymmetric && emptyOverlapsNothing,
               "rectangles that only touch should not count as overlapping") &&
        Expect(emptyMemoryFallsBack, "with nothing remembered the window goes to the fallback") &&
        Expect(
            roundTripKeepsThePlacement,
            "going fullscreen and back should return the window to where it was") &&
        Expect(
            secondRememberIsIgnored,
            "asking for fullscreen twice must not overwrite where the window came from") &&
        Expect(forgettingReturnsToFallback, "a forgotten placement leaves nothing to restore") &&
        Expect(
            visibleWhileTheMonitorIsThere && fallsBackWhenTheMonitorIsGone,
            "a placement on a monitor that is gone should fall back rather than hide the window") &&
        Expect(
            fallsBackWhenTheResolutionShrank,
            "a placement outside every monitor should fall back") &&
        Expect(
            aPartlyVisibleWindowIsKept,
            "a placement that still shows a corner is reachable and should be kept") &&
        Expect(noMonitorsFallBack, "with no monitors readable the fallback is the only answer");
}

static const TestSupport::Registration gWindowPlacementTests{
    "AppTiming", "window placement tests should pass", RunWindowPlacementTests };
