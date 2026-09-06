#include "TextEditModelTests.h"

#include <cstddef>
#include <iostream>
#include <string>

#include "UIModel/TextEditModel.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::UIModel::TextEditModel;

    /// <summary>
    /// 한 글자가 세 바이트인 텍스트다. 캐럿이 바이트 단위로 움직이면 여기서 글자가 쪼개진다 —
    /// 이 엔진을 쓰는 사람이 오브젝트 이름을 한글로 적을 때 바로 그 일이 일어난다.
    /// </summary>
    const std::string Korean = "가나다";
}

bool RunTextEditModelTests()
{
    // 세 바이트짜리 글자 사이를 방향키가 지나간다. 오른쪽 두 번이면 두 번째 글자 뒤,
    // 즉 6바이트 자리다 — 2바이트가 아니다.
    bool movesByCharacter = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input toStart;
        toStart.moveToStart = true;
        static_cast<void>(model.Apply(text, toStart));
        TextEditModel::Input right;
        right.moveRight = true;
        static_cast<void>(model.Apply(text, right));
        const std::size_t afterFirst = model.GetCaret();
        static_cast<void>(model.Apply(text, right));
        const std::size_t afterSecond = model.GetCaret();
        TextEditModel::Input left;
        left.moveLeft = true;
        static_cast<void>(model.Apply(text, left));
        movesByCharacter = afterFirst == 3 && afterSecond == 6 && model.GetCaret() == 3;
    }

    // 지우기도 글자 단위다: 백스페이스 한 번이 세 바이트를 지우고, 남는 것은 온전한 글자다.
    bool erasesWholeCharacter = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input backspace;
        backspace.backspace = true;
        const TextEditModel::Result result = model.Apply(text, backspace);
        erasesWholeCharacter = result.textChanged && text == "가나" && model.GetCaret() == 6;
    }

    // 캐럿이 글자 한가운데를 가리키게 된 채로 편집이 시작될 수 있다 — 즉시 모드에서는 문자열이
    // 편집 밖의 경로로 바뀐다. 그때 캐럿은 글자의 시작으로 되돌아간다.
    bool clampsIntoBoundary = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        model.PlaceCaret(4, false); // '나' 한가운데
        model.ClampTo(text);
        const std::size_t snapped = model.GetCaret();
        std::string shorter = "가";
        model.ClampTo(shorter);
        clampsIntoBoundary = snapped == 3 && model.GetCaret() == 3;
    }

    // Shift+방향키는 선택을 늘리고, Shift 없는 방향키는 선택을 그 방향 끝으로 접는다.
    bool selectsWithShift = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input shiftLeft;
        shiftLeft.moveLeft = true;
        shiftLeft.extendSelection = true;
        static_cast<void>(model.Apply(text, shiftLeft));
        const TextEditModel::Selection selected = model.GetSelection();
        const std::string selectedText(model.GetSelectedText(text));
        TextEditModel::Input left;
        left.moveLeft = true;
        static_cast<void>(model.Apply(text, left));
        selectsWithShift = selected.begin == 6 && selected.end == 9 && selectedText == "다" &&
            !model.GetSelection().HasSelection() && model.GetCaret() == 6;
    }

    // 선택을 둔 채 타이핑하면 선택이 대체된다. 전체 선택 뒤의 한 글자가 그 필드의 새 내용이다.
    bool typingReplacesSelection = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input typing;
        typing.selectAll = true;
        typing.typedText = "가";
        const TextEditModel::Result result = model.Apply(text, typing);
        typingReplacesSelection = result.textChanged && text == "가" && model.GetCaret() == 3;
    }

    // 복사·잘라내기는 클립보드에 실을 텍스트를 결과로 돌려준다 — 이 층은 클립보드를 모른다.
    // 붙여넣기는 그 반대로 텍스트를 입력으로 받는다.
    bool clipboardRoundTrips = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input cut;
        cut.selectAll = true;
        cut.cut = true;
        const TextEditModel::Result cutResult = model.Apply(text, cut);
        TextEditModel::Input paste;
        paste.paste = true;
        paste.pastedText = cutResult.clipboardText;
        const TextEditModel::Result pasteResult = model.Apply(text, paste);
        clipboardRoundTrips = cutResult.wroteClipboard && cutResult.clipboardText == Korean &&
            cutResult.textChanged && pasteResult.textChanged && text == Korean &&
            model.GetCaret() == Korean.size();
    }

    // 한 줄 필드이므로 붙여넣은 여러 줄은 한 줄이 된다. 제어 문자는 내용이 아니라 명령이다.
    bool filtersControlCharacters = false;
    {
        std::string text;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input paste;
        paste.paste = true;
        paste.pastedText = "first\r\nsecond\tthird";
        const TextEditModel::Result result = model.Apply(text, paste);
        filtersControlCharacters = result.textChanged && text == "firstsecondthird";
    }

    // 빈 편집은 아무것도 바꾸지 않는다: 값이 그대로인데 "바뀌었다"고 말하면 호출자가 undo를
    // 기록하거나 화면을 다시 그린다.
    bool quietWhenNothingHappens = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        const TextEditModel::Result result = model.Apply(text, TextEditModel::Input{});
        quietWhenNothingHappens =
            !result.textChanged && !result.caretMoved && !result.wroteClipboard && text == Korean;
    }

    // 문자열 끝에서의 오른쪽·Delete와 시작에서의 왼쪽·백스페이스는 자리를 벗어나지 않는다.
    bool staysInsideText = false;
    {
        std::string text = Korean;
        TextEditModel model;
        model.ResetTo(text);
        TextEditModel::Input rightAndDelete;
        rightAndDelete.moveRight = true;
        rightAndDelete.deleteForward = true;
        static_cast<void>(model.Apply(text, rightAndDelete));
        const bool endHeld = model.GetCaret() == Korean.size() && text == Korean;
        // 두 프레임이다: 먼저 시작으로 가고, 그다음 프레임에 왼쪽과 백스페이스를 누른다.
        // 한 프레임에 몰면 백스페이스가 이동 전 캐럿 자리에서 먼저 일어난다 — 지우기가
        // 이동보다 앞서는 것은 사람이 키를 그 순서로 누르기 때문이다.
        TextEditModel::Input toStart;
        toStart.moveToStart = true;
        static_cast<void>(model.Apply(text, toStart));
        TextEditModel::Input leftAndBackspace;
        leftAndBackspace.moveLeft = true;
        leftAndBackspace.backspace = true;
        static_cast<void>(model.Apply(text, leftAndBackspace));
        staysInsideText = endHeld && model.GetCaret() == 0 && text == Korean;
    }

    return Expect(movesByCharacter, "arrow keys should move the caret one character at a time") &&
        Expect(erasesWholeCharacter, "backspace should erase a whole character, not a byte") &&
        Expect(clampsIntoBoundary, "a caret inside a character should snap to its start") &&
        Expect(selectsWithShift, "shift should extend the selection and a bare arrow collapse it") &&
        Expect(typingReplacesSelection, "typing over a selection should replace it") &&
        Expect(clipboardRoundTrips, "cut should hand out the text that paste puts back") &&
        Expect(filtersControlCharacters, "a one-line field should drop pasted control characters") &&
        Expect(quietWhenNothingHappens, "an empty frame of input should report no change") &&
        Expect(staysInsideText, "the caret should not walk past either end of the text");
}

static const TestSupport::Registration gTextEditModelTests{
    "UIModel", "text edit model tests should pass", RunTextEditModelTests };
