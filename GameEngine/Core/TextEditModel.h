#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace GameEngine::Core
{

/// <summary>
/// 한 줄 텍스트 편집의 상태 기계다: 캐럿, 선택, 그리고 그 둘을 움직이는 편집 연산.
///
/// 위젯도 렌더링도 플랫폼도 모른다. 아는 것은 UTF-8 문자열과 그 안의 바이트 오프셋뿐이라,
/// 화면 없이 시험할 수 있다 — 한글처럼 한 문자가 여러 바이트인 텍스트에서 캐럿이 바이트가
/// 아니라 문자 단위로 움직이는지가 그 시험으로 확인된다.
///
/// 문자열을 소유하지 않고 연산마다 받는다. 즉시 모드 위젯에서는 호출자가 매 프레임 자기
/// 문자열을 넘기고, 유지 모드 입력 필드는 자기 멤버 문자열을 넘긴다 — 두 모드가 같은 편집
/// 모델을 쓰되 소유권 문제로 갈라지지 않게 하는 것이 이 형태의 이유다.
///
/// 클립보드는 이 층 아래에 없다(플랫폼 설비는 Core보다 위다). 그래서 복사는 "이 텍스트를
/// 클립보드에 실어라"를 결과로 돌려주고, 붙여넣기는 클립보드가 준 텍스트를 입력으로 받는다.
/// IME 조합 문자열도 여기에 없다: 조합 중인 글자는 아직 필드의 내용이 아니라 플랫폼이 만들고
/// 있는 것이고, 확정될 때 비로소 입력 텍스트로 도착한다.
/// </summary>
class TextEditModel final
{
public:
    /// <summary>선택 범위다. UTF-8 바이트 오프셋이며, 선택이 없으면 양끝이 같다.</summary>
    struct Selection
    {
        std::size_t begin = 0;
        std::size_t end = 0;

        [[nodiscard]] bool HasSelection() const { return begin != end; }
    };

    /// <summary>
    /// 이번 프레임에 도착한 편집 입력이다. 키 하나하나가 아니라 "무엇이 요청됐는가"이므로,
    /// 어느 키가 그 요청을 만드는지는 위젯과 플랫폼의 몫이다.
    /// </summary>
    struct Input
    {
        /// <summary>확정되어 들어온 문자들이다. 제어 문자는 이 모델이 걸러 낸다.</summary>
        std::string_view typedText;
        /// <summary>붙여넣기가 요청됐을 때 클립보드가 준 텍스트다.</summary>
        std::string_view pastedText;
        bool backspace = false;
        bool deleteForward = false;
        bool moveLeft = false;
        bool moveRight = false;
        bool moveToStart = false;
        bool moveToEnd = false;
        /// <summary>Shift다: 캐럿이 움직여도 선택의 반대쪽 끝이 남는다.</summary>
        bool extendSelection = false;
        bool selectAll = false;
        bool copy = false;
        bool cut = false;
        bool paste = false;
    };

    /// <summary>편집 한 번이 남긴 것이다. 호출자가 화면과 클립보드를 여기에 맞춘다.</summary>
    struct Result
    {
        /// <summary>문자열이 바뀌었는지다. 위젯의 "값이 바뀌었다"가 이것이다.</summary>
        bool textChanged = false;
        /// <summary>캐럿이나 선택이 움직였는지다. 캐럿 깜박임을 되돌리는 신호다.</summary>
        bool caretMoved = false;
        /// <summary>복사·잘라내기가 클립보드에 실어야 할 텍스트를 남겼는지다.</summary>
        bool wroteClipboard = false;
        /// <summary>클립보드에 실을 텍스트다. wroteClipboard가 거짓이면 비어 있다.</summary>
        std::string clipboardText;
    };

    /// <summary>캐럿의 UTF-8 바이트 오프셋이다.</summary>
    [[nodiscard]] std::size_t GetCaret() const { return mCaret; }

    /// <summary>지금의 선택이다. 앞뒤 순서는 정렬돼 있다.</summary>
    [[nodiscard]] Selection GetSelection() const;

    /// <summary>
    /// 캐럿을 그 자리에 놓는다. 클릭과 드래그가 쓴다 — 오프셋을 화면 좌표에서 구하는 것은
    /// 글자 폭을 아는 위젯의 일이고, 이 모델은 결과 오프셋만 받는다.
    /// </summary>
    /// <param name="index">놓을 UTF-8 바이트 오프셋이다.</param>
    /// <param name="extendSelection">참이면 선택의 반대쪽 끝을 남긴다(드래그, Shift 클릭).</param>
    void PlaceCaret(std::size_t index, bool extendSelection);

    /// <summary>
    /// 이 문자열의 편집을 새로 시작한다: 캐럿은 끝, 선택 없음. 포커스가 필드에 막 왔을 때다.
    /// </summary>
    void ResetTo(const std::string& text);

    /// <summary>
    /// 캐럿과 선택을 이 문자열 안의 문자 경계로 되돌린다. 편집이 아닌 경로로 문자열이 바뀌었을
    /// 수 있으므로 — 즉시 모드에서는 흔한 일이다 — 편집 전에 부른다.
    /// </summary>
    void ClampTo(const std::string& text);

    /// <summary>선택된 부분이다. 선택이 없으면 비어 있다.</summary>
    [[nodiscard]] std::string_view GetSelectedText(const std::string& text) const;

    /// <summary>
    /// 이번 프레임의 입력을 적용한다. 순서는 사람이 기대하는 순서다: 전체 선택, 복사·잘라내기,
    /// 붙여넣기, 타이핑, 지우기, 그리고 캐럿 이동.
    /// </summary>
    /// <param name="text">편집될 문자열이다. 제자리에서 바뀐다.</param>
    /// <param name="input">이번 프레임에 요청된 편집이다.</param>
    [[nodiscard]] Result Apply(std::string& text, const Input& input);

    /// <summary>선택을 지운다. 지울 것이 있었으면 참이다.</summary>
    bool EraseSelection(std::string& text);

    /// <summary>
    /// 캐럿 자리에 텍스트를 넣는다. 선택이 있으면 그것을 대체한다. 한 줄 필드이므로 개행·탭
    /// 같은 제어 문자는 걸러진다 — 여러 줄 붙여넣기도 여기서 한 줄이 된다.
    /// </summary>
    /// <returns>실제로 무언가 들어갔으면 참이다.</returns>
    bool Insert(std::string& text, std::string_view insertion);

    /// <summary>index 바로 앞 문자가 시작하는 바이트 오프셋이다. 이미 0이면 0이다.</summary>
    [[nodiscard]] static std::size_t PreviousCharBoundary(std::string_view text, std::size_t index);

    /// <summary>index에서 시작하는 문자 다음의 바이트 오프셋이다. 이미 끝이면 끝이다.</summary>
    [[nodiscard]] static std::size_t NextCharBoundary(std::string_view text, std::size_t index);

    /// <summary>문자 한가운데를 가리키는 오프셋을 그 문자의 시작으로 되돌린다.</summary>
    [[nodiscard]] static std::size_t SnapToCharBoundary(std::string_view text, std::size_t index);

private:
    /// <summary>캐럿의 UTF-8 바이트 오프셋이다.</summary>
    std::size_t mCaret = 0;
    /// <summary>선택의 고정된 끝이다. 캐럿과 같으면 선택이 없다.</summary>
    std::size_t mAnchor = 0;
};

}
