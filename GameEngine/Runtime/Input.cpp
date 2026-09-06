#include "pch.h"
#include "Input.h"

namespace GameEngine::Runtime
{

void Input::BeginFrame(Platform::IInput& source)
{
    mPrevious = mCurrent;
    source.ReadState(mCurrent);
}

void Input::BeginFrameWithState(const Platform::InputState& state)
{
    mPrevious = mCurrent;
    mCurrent = state;
}

bool Input::GetKey(const Platform::Key key) const
{
    return mCurrent.IsKeyDown(key);
}

bool Input::GetKeyDown(const Platform::Key key) const
{
    return mCurrent.WasKeyPressed(key);
}

bool Input::GetKeyUp(const Platform::Key key) const
{
    return mCurrent.WasKeyReleased(key);
}

bool Input::GetMouseButton(const Platform::MouseButton button) const
{
    return mCurrent.IsMouseButtonDown(button);
}

bool Input::GetMouseButtonDown(const Platform::MouseButton button) const
{
    // 표본 두 개를 비교하지 않고 플랫폼이 센 전이를 읽는다. 비교로는 표본 사이에서 시작하고
    // 끝난 누름이 보이지 않는다.
    return mCurrent.WasMouseButtonPressed(button);
}

bool Input::GetMouseButtonUp(const Platform::MouseButton button) const
{
    return mCurrent.WasMouseButtonReleased(button);
}

Math::Vector2Int Input::GetMousePosition() const
{
    return { mCurrent.cursor.x, mCurrent.cursor.y };
}

Math::Vector2Int Input::GetMouseDelta() const
{
    // Motion across a gap in focus is not motion this window saw: the cursor may have been moved by
    // someone working in another application, and reporting that as a drag would fling whatever was
    // being dragged when they came back.
    if (!mCurrent.hasFocus || !mPrevious.hasFocus)
    {
        return {};
    }
    return { mCurrent.cursor.x - mPrevious.cursor.x, mCurrent.cursor.y - mPrevious.cursor.y };
}

float Input::GetMouseWheel() const
{
    return mCurrent.wheelDelta;
}

}
