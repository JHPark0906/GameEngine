#include "Rules/DragGesture.h"

#include <cmath>

namespace GameEditor
{

void DragGesture::Press(const float x, const float y)
{
    if (mHeld)
    {
        return;
    }
    mHeld = true;
    mDragging = false;
    mStartX = x;
    mStartY = y;
}

void DragGesture::Release()
{
    mHeld = false;
    mDragging = false;
}

DragGesture::Result DragGesture::Update(
    const float x, const float y, const bool buttonDown, const float threshold)
{
    Result result;
    if (!mHeld)
    {
        return result;
    }

    if (buttonDown)
    {
        if (!mDragging)
        {
            const float dx = x - mStartX;
            const float dy = y - mStartY;
            if (std::sqrt(dx * dx + dy * dy) > threshold)
            {
                mDragging = true;
                result.began = true;
            }
        }
        return result;
    }

    // 버튼을 뗐다. 문턱을 넘었으면 놓기이고, 넘지 못했으면 클릭이었다 — 부르는 쪽이 그 둘을
    // 갈라야 콘텐츠 브라우저의 한 번 클릭이 놓기로 오해되지 않는다.
    result.dropped = mDragging;
    result.cancelled = !mDragging;
    Release();
    return result;
}

}
