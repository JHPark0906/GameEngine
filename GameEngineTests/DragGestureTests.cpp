#include "DragGestureTests.h"

#include <iostream>

#include "Rules/DragGesture.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
}

bool RunDragGestureTests()
{
    using GameEditor::DragGesture;
    constexpr float Threshold = 6.0f;

    DragGesture gesture;
    const bool idleAtRest = !gesture.IsHeld() && !gesture.IsDragging();

    // 집지 않은 손짓은 커서가 아무리 움직여도 아무 일도 아니다.
    const DragGesture::Result withoutPress = gesture.Update(100.0f, 100.0f, true, Threshold);
    const bool ignoresMotionWithoutPress =
        !withoutPress.began && !withoutPress.dropped && !withoutPress.cancelled &&
        !gesture.IsDragging();

    gesture.Press(10.0f, 10.0f);
    const bool heldButNotDragging = gesture.IsHeld() && !gesture.IsDragging() &&
        gesture.GetStartX() == 10.0f && gesture.GetStartY() == 10.0f;

    // 문턱 안에서는 아직 끌기가 아니다. 손이 떨리는 정도로 시작되면 클릭이 사라진다.
    const DragGesture::Result small = gesture.Update(14.0f, 12.0f, true, Threshold);
    const bool belowThresholdIsNotDragging = !small.began && !gesture.IsDragging();

    // 문턱을 넘는 그 프레임에만 began이 참이다.
    const DragGesture::Result crossing = gesture.Update(20.0f, 10.0f, true, Threshold);
    const bool beganOnceAtThreshold = crossing.began && gesture.IsDragging();
    const DragGesture::Result continuing = gesture.Update(40.0f, 30.0f, true, Threshold);
    const bool beganDoesNotRepeat = !continuing.began && gesture.IsDragging();

    // 끌던 중에 떼면 놓기다. 그리고 손짓은 끝난다.
    const DragGesture::Result release = gesture.Update(40.0f, 30.0f, false, Threshold);
    const bool droppedWhenReleased = release.dropped && !release.cancelled &&
        !gesture.IsHeld() && !gesture.IsDragging();

    // 문턱을 못 넘고 떼면 클릭이었다는 뜻이고, 놓기가 아니다.
    gesture.Press(50.0f, 50.0f);
    static_cast<void>(gesture.Update(52.0f, 51.0f, true, Threshold));
    const DragGesture::Result click = gesture.Update(52.0f, 51.0f, false, Threshold);
    const bool cancelledWhenNeverDragged =
        click.cancelled && !click.dropped && !gesture.IsHeld();

    // 집은 채로 다시 집는 것은 무시된다. 두 번 집으면 어느 쪽을 놓는 것인지 말할 수 없다.
    gesture.Press(0.0f, 0.0f);
    gesture.Press(100.0f, 100.0f);
    const bool secondPressIgnored = gesture.GetStartX() == 0.0f && gesture.GetStartY() == 0.0f;

    // 놓기는 언제든 손짓을 끝낸다.
    static_cast<void>(gesture.Update(80.0f, 80.0f, true, Threshold));
    gesture.Release();
    const bool releaseClearsEverything = !gesture.IsHeld() && !gesture.IsDragging();

    return Expect(idleAtRest, "a new gesture should hold nothing") &&
        Expect(ignoresMotionWithoutPress, "motion without a press should do nothing") &&
        Expect(heldButNotDragging, "a press should hold without dragging yet") &&
        Expect(belowThresholdIsNotDragging, "motion within the threshold is not a drag") &&
        Expect(beganOnceAtThreshold, "crossing the threshold should begin the drag") &&
        Expect(beganDoesNotRepeat, "the begin should be reported only once") &&
        Expect(droppedWhenReleased, "releasing while dragging should be a drop") &&
        Expect(cancelledWhenNeverDragged, "releasing without dragging should cancel") &&
        Expect(secondPressIgnored, "a second press while held should be ignored") &&
        Expect(releaseClearsEverything, "releasing should clear the gesture");
}

static const TestSupport::Registration gDragGestureTests{
    "EditorDocument", "drag gesture tests should pass", RunDragGestureTests };
