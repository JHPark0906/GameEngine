#pragma once

#include <memory>

namespace GameEngine::Platform
{
class IClipboard;
class ITextMeasure;
}

namespace GameEngine::Runtime
{

class Input;
class SceneManager;

/// <summary>
/// 포인터 하나가 UI 요소들과 맺는 상호작용의 상태 기계다. 요소를 인스턴스 id로만 다루므로 장면도
/// 화면도 없이 시험할 수 있다.
///
/// 이것이 따로 있는 이유는 어려운 부분이 좌표가 아니라 시간이기 때문이다. "커서 아래 무엇이
/// 있는가"는 사각형 검사 한 번이지만, "이 클릭은 누구의 것인가"는 누른 순간부터 뗀 순간까지의
/// 기억이다 — 그 사이에 커서가 요소를 벗어났다가 돌아올 수 있고, 그동안 다른 요소 위를 지나도
/// 그 요소가 눌림을 가로채면 안 된다.
/// </summary>
class UIPointerRouter final
{
public:
    /// <summary>이번 프레임의 입력이다. 어느 요소가 커서 아래인지와 버튼의 가장자리 두 개다.</summary>
    struct Frame
    {
        /// <summary>커서 아래 최상단 요소의 인스턴스 id다. 없으면 0이다.</summary>
        unsigned int topmostId = 0;
        /// <summary>이번 프레임에 버튼이 눌리기 시작했는지다.</summary>
        bool pressStarted = false;
        /// <summary>이번 프레임에 버튼이 떼어졌는지다.</summary>
        bool released = false;
        /// <summary>
        /// 커서 아래 그 요소가 키보드 포커스를 받을 수 있는지다. 텍스트를 받는 요소만 참이다.
        /// </summary>
        bool topmostTakesFocus = false;
    };

    /// <summary>이번 프레임에 각 상태를 받은 요소다. 없으면 0이다.</summary>
    struct Result
    {
        unsigned int hoveredId = 0;
        unsigned int pressedId = 0;
        unsigned int clickedId = 0;

        /// <summary>
        /// 포인터를 <b>쥐고 있는</b> 요소다. 없으면 0이다.
        ///
        /// <see cref="pressedId"/>와 다르다. 눌린 <i>모습</i>은 커서가 그 위에 있는 동안만
        /// 이어지지만 — 누른 채 버튼 밖으로 나가면 눌림이 풀려 보이고, 그것이 사람에게
        /// 「여기서 떼면 취소」를 말한다 — 쥔 것은 커서가 어디로 가든 이어진다.
        ///
        /// 그 차이가 필요한 것은 <b>끌기</b> 때문이다. 끌리는 것은 커서 아래에 머물지 않는다:
        /// 창을 끌면 창이 움직이고, 움직인 한 프레임 동안 커서는 방금 잡은 제목줄 밖에 있다.
        /// 눌림으로 끌기를 읽으면 그 한 프레임에 끌기가 끝났다가 다음 프레임에 새 끌기로
        /// 다시 시작하며, 한 몸짓이 여러 몸짓으로 쪼개진다 — 화면에서는 창이 커서를 따라오지
        /// 않고 토막토막 뛰거나 아예 안 움직이는 것으로 보인다.
        /// </summary>
        unsigned int capturedId = 0;
        /// <summary>지금 키보드 포커스를 쥔 요소다. 없으면 0이다.</summary>
        unsigned int focusedId = 0;
    };

    /// <summary>
    /// 한 프레임을 처리한다.
    ///
    /// 규칙은 셋이다. 눌림이 시작되면 그 요소가 포인터를 <b>잡는다</b>. 잡혀 있는 동안 다른
    /// 요소는 hover도 press도 받지 못한다 — 누른 채 옆 버튼 위로 지나갔다고 그 버튼이 눌린
    /// 것처럼 보이면 안 된다. 그리고 클릭은 <b>뗀 자리가 누른 자리와 같을 때만</b> 완성된다:
    /// 밖에서 떼는 것은 취소이며, 그것이 사람이 실수를 무를 수 있는 유일한 방법이다.
    ///
    /// 잡힌 요소를 벗어난 동안에도 잡음은 풀리지 않으므로, 커서가 돌아오면 눌린 모습도 함께
    /// 돌아온다.
    /// </summary>
    /// <param name="frame">이번 프레임의 입력이다.</param>
    /// <returns>hover·press·click을 받은 요소들이다.</returns>
    [[nodiscard]] Result Update(const Frame& frame);

    /// <summary>지금 포인터를 잡고 있는 요소다. 없으면 0이다.</summary>
    [[nodiscard]] unsigned int GetCapturedId() const { return mCapturedId; }

    /// <summary>
    /// 지금 키보드 포커스를 쥔 요소다. 없으면 0이다.
    ///
    /// 포커스는 포인터 잡음과 다른 개념이라 따로 산다: 잡음은 버튼을 뗄 때까지의 한 제스처지만,
    /// 포커스는 다음 클릭까지 남아 그동안의 타이핑이 갈 곳을 정한다. 그래도 이 상태 기계가 함께
    /// 쥐는 이유는 포커스를 옮기는 사건이 정확히 같은 사건 — 누름 — 이기 때문이다: 누름이 어느
    /// 요소 위에서 일어났는지 아는 곳이 여기 하나뿐이다.
    /// </summary>
    [[nodiscard]] unsigned int GetFocusedId() const { return mFocusedId; }

    /// <summary>키보드 포커스를 놓는다. 포커스를 쥔 요소가 사라질 때 쓴다.</summary>
    void ClearFocus() { mFocusedId = 0; }

    /// <summary>이벤트 시스템이 검증한 키보드 포커스 요청을 반영한다.</summary>
    void Focus(const unsigned int id) { mFocusedId = id; }

    /// <summary>잡음을 놓는다. 잡고 있던 요소가 사라지거나 장면이 바뀔 때 쓴다.</summary>
    void ReleaseCapture() { mCapturedId = 0; }

private:
    unsigned int mCapturedId = 0;
    unsigned int mFocusedId = 0;
};

/// <summary>
/// 배치된 UI 사각형에 커서를 맞혀 <see cref="Button"/>에게 hover·press·click을 나눠 주는
/// 시스템이다.
///
/// <see cref="UILayoutSystem"/> 바로 뒤에 도는 이유는 순서가 곧 정확성이기 때문이다: 사각형이
/// 계산되기 전의 자리로 커서를 맞히면 한 프레임 늦은 화면을 상대로 클릭을 판정하게 된다.
///
/// 입력은 <see cref="Input"/>에서만 읽는다. 플랫폼을 아는 곳은 이미 한 곳뿐이고, 여기서 창이나
/// 마우스 API를 다시 부르면 그 경계가 무너진다.
/// </summary>
class UIEventSystem final
{
public:
    /// <summary>
    /// 클립보드를 쥐고 만들어진다. 텍스트 필드의 복사·붙여넣기가 그것을 쓰며, 없으면 그 두
    /// 명령만 조용히 아무것도 하지 않는다 — 클립보드가 없는 환경(창 없는 테스트)에서도 나머지
    /// 편집은 그대로 돈다.
    /// </summary>
    UIEventSystem();
    explicit UIEventSystem(std::unique_ptr<Platform::IClipboard> clipboard);
    ~UIEventSystem();

    /// <summary>
    /// 활성 장면의 버튼들에 이번 프레임의 포인터 상태를 나눠 준다.
    ///
    /// 겹친 요소들 중에서는 <b>계층 순서의 뒤가 이긴다</b> — 그리는 순서와 같은 순서라, 위에
    /// 보이는 것이 커서를 받는다. 입력을 받지 않는(interactable이 거짓인) 버튼은 후보에서
    /// 아예 빠지므로, 그 아래 요소가 대신 받는다.
    /// </summary>
    /// <param name="sceneManager">활성 장면을 쥔 매니저다.</param>
    /// <param name="input">이번 프레임의 입력이다.</param>
    /// <param name="deltaTime">초 단위 경과 시간이다. 캐럿 점멸과 누르고 있는 Backspace의 반복 간격에 쓴다.</param>
    /// <returns>UI가 이 포인터를 가져갔으면 true다. 커서 아래 요소가 있거나 눌림을 잡고 있을 때다.</returns>
    bool Synchronize(SceneManager& sceneManager, const Input& input,
        Platform::ITextMeasure* textMeasure = nullptr, float deltaTime = 0.0f);

private:
    [[nodiscard]] unsigned int UpdateBackspaceRepeat(
        const Input& input, unsigned int focusedId, float deltaTime);

    UIPointerRouter mRouter;
    // 반복은 포커스를 쥔 필드에 귀속된다. 다른 필드가 눌린 키의 남은 반복을 물려받지 않는다.
    unsigned int mBackspaceRepeatField = 0;
    double mBackspaceRepeatRemaining = 0.0;
    bool mBackspaceRepeatArmed = false;
    /// <summary>복사·붙여넣기가 오가는 시스템 클립보드다. 없을 수 있다.</summary>
    std::unique_ptr<Platform::IClipboard> mClipboard;
};

}
