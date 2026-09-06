#pragma once

#include <string>

#include "../Math/Vector.h"
#include "../Platform/IInput.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 키보드와 마우스가 하고 있는 일이다. 게임이 묻는 모양 그대로다.
///
/// 플랫폼은 눌려 있는 상태만 보고한다. 무언가가 *이번 프레임에* 일어났는지는 두 순간에 대한
/// 질문이라서, 이전 프레임의 상태를 현재 상태 옆에 두는 것으로 여기서 답한다 — 플랫폼마다가
/// 아니라 한 번만. 플랫폼마다였다면 프레임이 언제 시작하는지에 대한 각자의 생각과 포커스 상실
/// 주변의 각자의 예외들이 남았을 것이다.
/// </summary>
class Input final
{
public:
    /// <summary>
    /// 플랫폼에서 이번 프레임의 상태를 가져오고 이전 것을 비교 대상으로 만든다. 엔진 루프가
    /// 무엇이 업데이트되기 전, 프레임당 한 번 호출한다.
    /// </summary>
    void BeginFrame(Platform::IInput& source);

    /// <summary>
    /// 이번 프레임의 원시 상태다. 한 입력을 두 런타임이 나눠 받아야 하는 자리가 있어서 낸다 —
    /// 플랫폼에서 읽는 것은 프레임당 한 번뿐이므로(누적된 타이핑과 휠을 가져가며 비운다)
    /// 두 번째 런타임은 읽을 수 없고, 읽은 것을 건네받아야 한다.
    /// </summary>
    [[nodiscard]] const Platform::InputState& GetState() const { return mCurrent; }

    /// <summary>
    /// 플랫폼 대신 주어진 상태로 이번 프레임을 연다. 에디터가 게임에게 입력을 건네줄 때 쓴다.
    /// 빈 상태를 주면 "아무것도 눌리지 않음"이 되며, 그것은 아무것도 주지 않는 것과 다르다 —
    /// 주지 않으면 지난 프레임의 눌림이 그대로 남는다.
    /// </summary>
    /// <param name="state">이번 프레임의 상태다.</param>
    void BeginFrameWithState(const Platform::InputState& state);

    /// <summary>키가 지금 눌려 있는지 여부이다.</summary>
    [[nodiscard]] bool GetKey(Platform::Key key) const;

    /// <summary>키가 이번 프레임에 눌렸는지 여부이다.</summary>
    [[nodiscard]] bool GetKeyDown(Platform::Key key) const;

    /// <summary>키가 이번 프레임에 떼였는지 여부이다.</summary>
    [[nodiscard]] bool GetKeyUp(Platform::Key key) const;

    [[nodiscard]] bool GetMouseButton(Platform::MouseButton button) const;
    [[nodiscard]] bool GetMouseButtonDown(Platform::MouseButton button) const;
    [[nodiscard]] bool GetMouseButtonUp(Platform::MouseButton button) const;

    /// <summary>창 클라이언트 영역 좌상단 기준 픽셀 단위의 커서 위치이다.</summary>
    [[nodiscard]] Math::Vector2Int GetMousePosition() const;

    /// <summary>
    /// 이번 프레임에 커서가 움직인 거리이다. 픽셀 단위다. 창이 포커스를 되찾은 첫 프레임에는
    /// 0인데, 다른 창이 포커스를 가진 동안 커서가 움직였을 수 있고 그 거리는 이 창이 본 움직임이
    /// 아니기 때문이다.
    /// </summary>
    [[nodiscard]] Math::Vector2Int GetMouseDelta() const;

    /// <summary>이번 프레임의 휠 눈금이다. 양수가 사람에게서 멀어지는 쪽이다.</summary>
    [[nodiscard]] float GetMouseWheel() const;

    /// <summary>
    /// 이번 프레임에 타이핑된 텍스트이다. UTF-8이다. 텍스트 필드가 덧붙이는 것이 이것이다. 키
    /// 상태에서 문자를 재구성하는 대신 — 이것은 그 사람의 배열, 수정 키, 입력기(IME)를 이미
    /// 반영하고 있다.
    /// </summary>
    [[nodiscard]] const std::string& GetTypedText() const { return mCurrent.typedText; }

    /// <summary>
    /// IME가 지금 조합 중인 문자열이다. UTF-8이며, 조합이 없으면 비어 있다. 확정된 글자는
    /// `GetTypedText`로 오고, 이것은 아직 확정되지 않아 사람이 고치는 중인 글자다 — 한국어
    /// 입력에서 치는 동안 화면에 무엇이 보이는지가 이것으로 정해진다.
    /// </summary>
    /// <returns>조합 중인 UTF-8 문자열이다.</returns>
    [[nodiscard]] const std::string& GetCompositionText() const
    {
        return mCurrent.compositionText;
    }

    /// <summary>이 창이 입력을 받는 창인지 여부이다.</summary>
    [[nodiscard]] bool HasFocus() const { return mCurrent.hasFocus; }

private:
    Platform::InputState mCurrent;
    Platform::InputState mPrevious;
};

}
