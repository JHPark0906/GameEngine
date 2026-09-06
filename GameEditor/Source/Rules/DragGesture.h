#pragma once

// editor-layer: 0 (Rules)

namespace GameEditor
{

/// <summary>
/// 누르고, 끌고, 놓는 한 번의 손짓이다: 누른 자리를 기억하고, 문턱을 넘어야 끌기로 치고, 뗀
/// 자리에서 끝난다.
///
/// 편집기의 패널·에셋·창·계층 드래그가 함께 쓰는 규칙이다. 커서 좌표와 버튼 상태만 받아
/// 문턱을 넘었는지 판단하므로, 위젯이나 화면 없이 시험할 수 있다. 놓기를 취소해야 하는
/// 포인터 소유권과 편집 대상의 상태는 호출자가 판단한다.
///
/// 무엇을 끌고 있는지는 담지 않는다. 그것은 호출자가 아는 것이고, 여기에 담으면 끌 수 있는
/// 것마다 이 타입이 하나씩 생긴다.
/// </summary>
class DragGesture final
{
public:
    /// <summary>손이 떨리는 정도로는 끌기가 시작되지 않아야 하는 거리다. 96 DPI 기준 픽셀이다.</summary>
    static constexpr float DefaultThreshold = 6.0f;

    /// <summary>손짓 한 번이 끝나며 남긴 것이다.</summary>
    struct Result
    {
        /// <summary>이번 갱신에서 끌기가 시작됐는지다. 문턱을 막 넘은 프레임에만 참이다.</summary>
        bool began = false;
        /// <summary>끌던 중에 버튼을 뗐는지다. 놓기가 일어나는 프레임이 이것이다.</summary>
        bool dropped = false;
        /// <summary>문턱을 넘지 못한 채 버튼을 뗐는지다. 끌기가 아니라 클릭이었다는 뜻이다.</summary>
        bool cancelled = false;
    };

    /// <summary>지금 무언가를 집고 있는지다. 문턱을 넘기 전에도 참이다.</summary>
    [[nodiscard]] bool IsHeld() const { return mHeld; }

    /// <summary>문턱을 넘어 실제로 끌고 있는지다. 커서를 따라다니는 표시가 이것을 읽는다.</summary>
    [[nodiscard]] bool IsDragging() const { return mDragging; }

    [[nodiscard]] float GetStartX() const { return mStartX; }
    [[nodiscard]] float GetStartY() const { return mStartY; }

    /// <summary>
    /// 그 자리에서 집는다. 이미 집고 있으면 아무것도 하지 않는다 — 한 손짓이 끝나기 전에 다른
    /// 것을 집으면 어느 쪽을 놓는 것인지 말할 수 없다.
    /// </summary>
    /// <param name="x">누른 자리의 x다.</param>
    /// <param name="y">누른 자리의 y다.</param>
    void Press(float x, float y);

    /// <summary>집은 것을 놓는다. 끌던 중이었는지와 무관하게 상태가 비워진다.</summary>
    void Release();

    /// <summary>
    /// 이번 프레임의 커서와 버튼 상태를 적용한다. 집고 있지 않으면 아무 일도 하지 않는다.
    /// </summary>
    /// <param name="x">지금 커서의 x다.</param>
    /// <param name="y">지금 커서의 y다.</param>
    /// <param name="buttonDown">버튼이 아직 눌려 있는지다.</param>
    /// <param name="threshold">끌기로 치기 시작하는 거리다. 화면 배율이 곱해진 값을 넘긴다.</param>
    /// <returns>이번 프레임에 시작·놓기·취소 중 무엇이 일어났는지다.</returns>
    [[nodiscard]] Result Update(
        float x, float y, bool buttonDown, float threshold = DefaultThreshold);

private:
    bool mHeld = false;
    bool mDragging = false;
    float mStartX = 0.0f;
    float mStartY = 0.0f;
};

}
