#include "EditorDragRuleTests.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Rules/DragGesture.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>한 프레임이 손짓에게 보이는 것이다.</summary>
    struct Frame
    {
        float x = 0.0f;
        float y = 0.0f;
        /// <summary>버튼이 눌려 있는지다. <c>UIContext::IsMouseDown</c>이다.</summary>
        bool mouseDown = true;
        /// <summary>이 프레임이 뗀 프레임인지다. <c>WasMouseReleased</c>다.</summary>
        bool mouseReleased = false;
    };

    /// <summary>
    /// 계층 패널과 셸이 손짓을 모는 방식이다. 두 호출자의 코드가 이 순서 그대로다: 뗌 표시 없이
    /// 버튼이 올라와 있으면 취소하고, 그 밖의 프레임만 손짓에 먹인다.
    ///
    /// 이 규칙이 호출자에 있는 이유는 <c>DragGesture</c>가 버튼이 눌려 있는지만 보기 때문이다.
    /// 유지 모드 UI가 포인터를 가져간 프레임은 눌림과 뗌이 함께 지워져서, 손짓만 보면 그것이
    /// 놓기와 구별되지 않는다. 무엇이 지워졌는지 아는 것은 그 둘을 함께 읽는 호출자다.
    /// </summary>
    struct Outcome
    {
        /// <summary>이번 프레임에 놓기가 일어났는지다. 재부모화와 패널 교체가 이때 일어난다.</summary>
        bool dropped = false;
        /// <summary>집은 것이 놓기 없이 비워졌는지다.</summary>
        bool cancelled = false;
    };

    [[nodiscard]] Outcome DriveOneFrame(
        GameEditor::DragGesture& gesture, const Frame& frame, const float threshold)
    {
        Outcome outcome;
        if (!frame.mouseReleased && !frame.mouseDown)
        {
            outcome.cancelled = gesture.IsHeld();
            gesture.Release();
            return outcome;
        }
        const GameEditor::DragGesture::Result result =
            gesture.Update(frame.x, frame.y, frame.mouseDown, threshold);
        outcome.dropped = result.dropped;
        outcome.cancelled = result.cancelled;
        return outcome;
    }

    /// <summary>입력 열 하나를 호출자의 규칙으로 몰고 마지막 프레임의 결과를 돌려준다.</summary>
    [[nodiscard]] Outcome Drive(
        const float pressX, const float pressY, const std::vector<Frame>& frames,
        const float threshold, bool* const draggingAtEnd = nullptr)
    {
        GameEditor::DragGesture gesture;
        gesture.Press(pressX, pressY);
        Outcome outcome;
        for (const Frame& frame : frames)
        {
            outcome = DriveOneFrame(gesture, frame, threshold);
        }
        if (draggingAtEnd)
        {
            *draggingAtEnd = gesture.IsDragging();
        }
        return outcome;
    }
}

bool RunEditorDragRuleTests()
{
    constexpr float Threshold = 6.0f;
    bool passed = true;

    // 문턱 안에서 뗀 것은 놓기가 아니다. 계층에서는 이것이 선택으로, 패널에서는 아무 일도 없는
    // 것으로 끝난다.
    {
        const Outcome outcome = Drive(100.0f, 100.0f,
            { { 103.0f, 104.0f, true, false }, { 103.0f, 104.0f, false, true } }, Threshold);
        passed &= Expect(
            !outcome.dropped && outcome.cancelled,
            "a release inside the threshold is a cancel, not a drop");
    }

    // 문턱은 초과일 때만 넘은 것이다. 정확히 문턱만큼 움직인 것은 아직 클릭이다.
    {
        const Outcome outcome = Drive(0.0f, 0.0f,
            { { 6.0f, 0.0f, true, false }, { 6.0f, 0.0f, false, true } }, Threshold);
        passed &= Expect(
            !outcome.dropped, "moving exactly the threshold is still a click");
    }

    // 문턱을 넘은 프레임에 끌기가 시작되고, 그 뒤에 뗀 것은 놓기다.
    {
        bool dragging = false;
        const Outcome crossing = Drive(0.0f, 0.0f,
            { { 4.0f, 0.0f, true, false }, { 7.0f, 0.0f, true, false } }, Threshold, &dragging);
        passed &= Expect(
            !crossing.dropped && dragging, "crossing the threshold begins a drag");

        const Outcome outcome = Drive(0.0f, 0.0f,
            { { 4.0f, 0.0f, true, false }, { 7.0f, 0.0f, true, false },
              { 20.0f, 5.0f, true, false }, { 20.0f, 5.0f, false, true } }, Threshold);
        passed &= Expect(outcome.dropped, "and releasing after that is a drop");
    }

    // 대각선도 직선거리로 잰다. (3, 4)는 5라 아직 아니고, (6, 8)은 10이라 넘는다.
    {
        bool shortOfIt = false;
        static_cast<void>(Drive(0.0f, 0.0f, { { 3.0f, 4.0f, true, false } }, Threshold,
            &shortOfIt));
        bool pastIt = false;
        static_cast<void>(Drive(0.0f, 0.0f, { { 6.0f, 8.0f, true, false } }, Threshold, &pastIt));
        passed &= Expect(
            !shortOfIt && pastIt,
            "the threshold is a straight-line distance from the press");
    }

    // 한 번 시작된 끌기는 커서가 시작점으로 돌아와도 끌기다. 문턱은 시작 조건이지 유지 조건이
    // 아니다.
    {
        const Outcome outcome = Drive(50.0f, 50.0f,
            { { 80.0f, 50.0f, true, false }, { 50.0f, 50.0f, true, false },
              { 50.0f, 50.0f, false, true } }, Threshold);
        passed &= Expect(
            outcome.dropped, "a drag that returns to its start is still a drag");
    }

    // 🔴 호출자가 규칙을 갖는 자리가 여기다.
    //
    // 유지 모드 UI가 이 프레임의 포인터를 가져가면 UIContext는 눌림과 뗌을 <b>둘 다</b> 지운다.
    // 손짓만 보면 그 프레임은 "버튼이 눌려 있지 않다"라서 놓기와 구별되지 않는데, 놓기로 읽으면
    // 툴바 버튼 하나를 누른 것이 계층을 재부모화하거나 패널을 맞바꾸는 일이 된다. 그래서 두
    // 호출자가 그 프레임을 <c>Release()</c>로 보내 취소한다.
    {
        constexpr float OffScreen = -1.0e6f;
        bool dragging = true;
        const Outcome outcome = Drive(0.0f, 0.0f,
            { { 30.0f, 0.0f, true, false }, { OffScreen, OffScreen, false, false } }, Threshold,
            &dragging);
        passed &= Expect(
            !outcome.dropped, "a pointer taken by the retained UI does not drop what was held");
        passed &= Expect(
            outcome.cancelled && !dragging, "it cancels the gesture instead");
    }

    // 취소된 뒤의 프레임은 아무 일도 아니다. 놓기가 뒤늦게 도착하지 않는다.
    {
        constexpr float OffScreen = -1.0e6f;
        const Outcome outcome = Drive(0.0f, 0.0f,
            { { 30.0f, 0.0f, true, false }, { OffScreen, OffScreen, false, false },
              { 40.0f, 0.0f, false, true } }, Threshold);
        passed &= Expect(
            !outcome.dropped && !outcome.cancelled,
            "nothing else happens after the gesture has been cancelled");
    }

    // 화면 배율이 곱해진 문턱에서도 같은 규칙이다. 두 곳 모두 S(6.0f)를 넘긴다.
    {
        bool dragging = false;
        static_cast<void>(Drive(0.0f, 0.0f,
            { { 8.0f, 0.0f, true, false }, { 13.0f, 0.0f, true, false } }, 12.0f, &dragging));
        passed &= Expect(dragging, "the same rule holds at a scaled threshold");
    }

    // 위의 <c>DriveOneFrame</c>은 호출자의 규칙을 옮겨 적은 것이라, 호출자가 그 두 줄을 잃어도
    // 그것만으로는 붉어지지 않는다. 그래서 소스를 읽어 두 곳이 실제로 그 가드를 들고 있는지 본다
    // — 씬 뷰가 <c>DragGesture</c>를 부르지 않는지 보는 것과 같은 방식이다.
    {
        // 이 파일은 <repo>/GameEngineTests/EditorDragRuleTests.cpp에 있다.
        const std::filesystem::path source =
            std::filesystem::path(__FILE__).parent_path().parent_path() / "GameEditor" / "Source";
        const std::array<const char*, 2> callers{
            "Views/EditorHierarchyPanel.cpp", "Shell/EditorShell.cpp" };
        for (const char* const name : callers)
        {
            std::ifstream file(source / name);
            bool guarded = false;
            bool readable = false;
            for (std::string line; std::getline(file, line);)
            {
                readable = true;
                if (line.find("!mUI.WasMouseReleased() && !mUI.IsMouseDown()") != std::string::npos)
                {
                    guarded = true;
                }
            }
            passed &= Expect(readable, "the drag callers should be readable from the test");
            passed &= Expect(
                guarded,
                "each drag caller cancels the gesture when the button is up without a release; "
                "dropping that guard turns a consumed pointer into a reparent or a panel swap");
        }
    }

    return passed;
}

namespace
{
    /// <summary>
    /// 씬 뷰 궤도 회전의 입력 규칙이다. 회전은 버튼을 누른 첫 프레임부터 적용한다.
    /// 이동 거리는 프레임마다 누적하며 놓는 시점의 클릭 판정에 사용한다.
    /// </summary>
    class OrbitGesture final
    {
    public:
        /// <summary>회전이 문턱과 무관하게 적용된 총량이다.</summary>
        [[nodiscard]] float GetYawDegrees() const { return mYawDegrees; }
        /// <summary>지금까지 커서가 돌아다닌 거리다. 직선거리가 아니라 누적이다.</summary>
        [[nodiscard]] float GetTravelled() const { return mTravelled; }

        /// <param name="deltaX">이번 프레임의 커서 이동량이다.</param>
        /// <param name="leftDown">왼쪽 버튼이 눌려 있는지다.</param>
        /// <returns>이 프레임에 객체 피킹이 일어나는지다. 즉 클릭으로 판정됐는지다.</returns>
        [[nodiscard]] bool Update(
            const float deltaX, const float deltaY, const bool leftDown, const float threshold)
        {
            if (leftDown)
            {
                if (!mPressActive)
                {
                    mPressActive = true;
                    mTravelled = 0.0f;
                }
                mTravelled += std::abs(deltaX) + std::abs(deltaY);
                // 회전은 여기서 바로 적용된다. 문턱을 기다리지 않는다.
                mYawDegrees += deltaX * OrbitDegreesPerPixel;
                return false;
            }
            if (!mPressActive)
            {
                return false;
            }
            mPressActive = false;
            return mTravelled <= threshold;
        }

    private:
        static constexpr float OrbitDegreesPerPixel = 0.4f;

        bool mPressActive = false;
        float mTravelled = 0.0f;
        float mYawDegrees = 0.0f;
    };

    /// <summary>
    /// 기즈모가 축을 잡는 규칙이다. 손잡이 위에서 버튼이 눌린 첫 프레임에 잡고, 그 프레임부터
    /// 곧바로 옮긴다 — 문턱이 없다.
    /// </summary>
    class GizmoGrab final
    {
    public:
        [[nodiscard]] bool IsGrabbed() const { return mGrabbed; }
        /// <summary>잡은 뒤 객체가 옮겨진 거리다.</summary>
        [[nodiscard]] float GetMoved() const { return mMoved; }

        /// <param name="overHandle">커서가 손잡이 위인지다.</param>
        void Update(const bool leftDown, const bool overHandle, const float cursorX)
        {
            if (leftDown && !mGrabbed && overHandle)
            {
                mGrabbed = true;
                mStartX = cursorX;
            }
            if (!mGrabbed)
            {
                return;
            }
            if (leftDown)
            {
                mMoved = cursorX - mStartX;
                return;
            }
            mGrabbed = false;
        }

    private:
        bool mGrabbed = false;
        float mStartX = 0.0f;
        float mMoved = 0.0f;
    };
}

bool RunSceneViewGestureTests()
{
    using GameEditor::DragGesture;
    constexpr float PickThreshold = 4.0f;
    bool passed = true;

    // 한 바퀴 돌아 제자리로 온 드래그다. 누적 이동량은 60이라 클릭이 아니고, 시작점에서의
    // 직선거리는 0이라 DragGesture라면 클릭이라고 답한다. 이 둘이 갈리는 것이 씬 뷰를 합치지
    // 않은 이유이며, 합쳤다면 궤도를 한 바퀴 돌린 것만으로 선택이 바뀐다.
    {
        OrbitGesture orbit;
        (void)orbit.Update(15.0f, 0.0f, true, PickThreshold);
        (void)orbit.Update(15.0f, 0.0f, true, PickThreshold);
        (void)orbit.Update(-15.0f, 0.0f, true, PickThreshold);
        (void)orbit.Update(-15.0f, 0.0f, true, PickThreshold);
        const bool picked = orbit.Update(0.0f, 0.0f, false, PickThreshold);

        passed &= Expect(
            orbit.GetTravelled() == 60.0f,
            "the orbit measures how far the cursor travelled, not where it ended");
        passed &= Expect(
            !picked, "a drag that returns to its start is not a click for the orbit");

        // 같은 손짓을 DragGesture로 재면 반대 답이 나온다.
        DragGesture gesture;
        gesture.Press(0.0f, 0.0f);
        (void)gesture.Update(15.0f, 0.0f, true, PickThreshold);
        (void)gesture.Update(30.0f, 0.0f, true, PickThreshold);
        (void)gesture.Update(15.0f, 0.0f, true, PickThreshold);
        (void)gesture.Update(0.0f, 0.0f, true, PickThreshold);
        const DragGesture::Result released = gesture.Update(0.0f, 0.0f, false, PickThreshold);
        passed &= Expect(
            released.dropped && !released.cancelled,
            "DragGesture keeps the drag once begun, so it cannot answer the orbit's question");
    }

    // 회전은 문턱을 기다리지 않는다. 1픽셀만 움직여도 그 프레임에 이미 돌아간다.
    {
        OrbitGesture orbit;
        (void)orbit.Update(1.0f, 0.0f, true, PickThreshold);
        passed &= Expect(
            orbit.GetYawDegrees() != 0.0f,
            "the orbit turns on the first frame, before any threshold");
        passed &= Expect(
            orbit.GetTravelled() < PickThreshold,
            "and it turns while still inside the click threshold");

        DragGesture gesture;
        gesture.Press(0.0f, 0.0f);
        const DragGesture::Result first = gesture.Update(1.0f, 0.0f, true, PickThreshold);
        passed &= Expect(
            !first.began && !gesture.IsDragging(),
            "DragGesture would still be waiting at that point, which is the difference");
    }

    // 문턱 안에서 뗀 것은 피킹이다. 궤도에서 문턱이 하는 일은 이것뿐이다.
    {
        OrbitGesture orbit;
        (void)orbit.Update(1.0f, 1.0f, true, PickThreshold);
        const bool picked = orbit.Update(0.0f, 0.0f, false, PickThreshold);
        passed &= Expect(picked, "a release inside the travel threshold picks an object");
    }

    // 기즈모는 문턱이 없다. 손잡이 위에서 누른 첫 프레임에 잡고 곧바로 옮긴다.
    {
        GizmoGrab gizmo;
        gizmo.Update(true, true, 100.0f);
        passed &= Expect(gizmo.IsGrabbed(), "the gizmo grabs on the frame the handle is pressed");
        gizmo.Update(true, true, 101.0f);
        passed &= Expect(
            gizmo.GetMoved() == 1.0f,
            "and one pixel of movement already moves the object");

        DragGesture gesture;
        gesture.Press(100.0f, 0.0f);
        const DragGesture::Result oneP = gesture.Update(101.0f, 0.0f, true, 6.0f);
        passed &= Expect(
            !oneP.began,
            "DragGesture would swallow that pixel, and with it the fine adjustment");
    }

    // 손잡이 밖에서 누른 것은 잡지 않는다. 그 드래그는 궤도의 것이다.
    {
        GizmoGrab gizmo;
        gizmo.Update(true, false, 100.0f);
        passed &= Expect(!gizmo.IsGrabbed(), "pressing away from a handle grabs no axis");
    }

    // 위의 모형들은 씬 뷰의 규칙을 옮겨 적은 것이라, 씬 뷰 자신이 바뀌어도 그것만으로는 붉어지지
    // 않는다. 그래서 소스를 한 번 읽는다: 씬 뷰가 DragGesture를 부르기 시작하면 여기서 걸린다.
    //
    // 층 검사가 포함 관계를 소스에서 읽어 확인하는 것과 같은 방식이다. 규칙을 값으로 잴 수 없는
    // 자리에서는 "그 타입을 쓰지 않는다"는 사실 자체가 잴 수 있는 것이다.
    {
        // 이 파일은 <repo>/GameEngineTests/EditorDragRuleTests.cpp에 있다.
        const std::filesystem::path repository =
            std::filesystem::path(__FILE__).parent_path().parent_path();
        const std::filesystem::path sceneView = repository / "GameEditor" / "Source";
        const std::array<const char*, 2> files{
            "Views/EditorSceneViewPanel.h", "Views/EditorSceneViewPanel.cpp" };

        bool scanned = false;
        bool mentionsDragGesture = false;
        for (const char* const name : files)
        {
            std::ifstream file(sceneView / name);
            if (!file)
            {
                continue;
            }
            scanned = true;
            for (std::string line; std::getline(file, line);)
            {
                // 이 시험이 왜 있는지 설명하는 주석은 그 이름을 부를 수밖에 없으므로 세지 않는다.
                if (line.find("//") != std::string::npos)
                {
                    continue;
                }
                if (line.find("DragGesture") != std::string::npos)
                {
                    mentionsDragGesture = true;
                }
            }
        }
        passed &= Expect(scanned, "the scene view sources should be readable from the test");
        passed &= Expect(
            !mentionsDragGesture,
            "the scene view keeps its own gestures; folding it into DragGesture changes what a "
            "returning drag and a one-pixel gizmo nudge do");
    }

    return passed;
}

static const TestSupport::Registration gEditorDragRuleTests{
    "EditorDocument", "editor drag rule tests should pass", RunEditorDragRuleTests };

static const TestSupport::Registration gSceneViewGestureTests{
    "EditorDocument", "scene view gesture tests should pass", RunSceneViewGestureTests };
