#pragma once

#include <string>
#include <vector>

#include "../Core/TextEditModel.h"
#include "RectTransform.h"
#include "Selectable.h"

namespace GameEngine::Platform { class ITextMeasure; }

namespace GameEngine::Runtime
{

class UIEventSystem;

/// <summary>
/// 사람이 글자를 치는 UI 요소다. 자기 <see cref="RectTransform"/>이 차지한 사각형에서 클릭으로
/// 키보드 포커스를 얻고, 포커스가 있는 동안의 타이핑·선택·클립보드를
/// <see cref="Core::TextEditModel"/>에게 맡긴다. 보이는 글자는 같은 오브젝트의
/// <see cref="TextRenderer"/>가 그린다.
///
/// 편집 규칙을 스스로 갖지 않는 이유는 즉시 모드 에디터 UI의 텍스트 필드가 이미 같은 규칙을
/// 쓰고 있기 때문이다 — 캐럿이 UTF-8 문자 단위로 움직이는 것, 한 줄 필드가 붙여넣은 개행을
/// 거르는 것은 두 벌이 될 이유가 없다. 그 규칙은 Core에 있고 두 UI가 같은 것을 쓴다.
///
/// 포커스를 스스로 정하지 않는 이유는 <see cref="Button"/>이 눌림을 스스로 정하지 않는 이유와
/// 같다: 어느 요소가 클릭을 받는지는 계층 전체를 봐야 아는 질문이고,
/// <see cref="UIEventSystem"/>이 그 답을 갖고 있다.
///
/// 캐럿과 선택은 글리프 배치가 제공한 문자 경계로 그린다. 조합 중인 문자열은 확정된 내용과
/// 분리해 표시하며, 캐럿은 IME가 알려 준 조합 안의 위치를 따른다.
/// </summary>
class InputField final : public Selectable
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    /// <summary>확정된 내용이다. 조합 중인 글자는 아직 여기 없다.</summary>
    [[nodiscard]] const std::string& GetText() const { return mText; }

    /// <summary>
    /// 내용을 통째로 바꾼다. 인스펙터의 편집과 스크립트가 쓰는 길이며, 캐럿은 끝으로 간다 —
    /// 밖에서 값이 갈렸는데 캐럿이 옛 자리에 남으면 다음 타이핑이 엉뚱한 곳에 들어간다.
    /// </summary>
    void SetText(std::string text);

    /// <summary>글자를 받는 요소이므로 키보드 포커스를 받는다.</summary>
    [[nodiscard]] bool TakesFocus() const override { return true; }

    /// <summary>지금 키보드 포커스를 쥐고 있는지다.</summary>
    [[nodiscard]] bool IsFocused() const { return mFocused; }

    /// <summary>다음 UI 동기화에 키보드 포커스를 요청한다. 모달과 입력 가능 여부는 이벤트 시스템이 판정한다.</summary>
    void RequestFocus();

    /// <summary>
    /// 이번 프레임의 편집으로 내용이 바뀌었는지다. 다음 프레임에는 다시 거짓이다 —
    /// <see cref="Button::WasClickedThisFrame"/>과 같은 약속이며, 스크립트가 Update에서 읽는다.
    /// </summary>
    [[nodiscard]] bool WasEditedThisFrame() const { return mEdited; }

    /// <summary>
    /// 이번 프레임에 Enter로 확정됐는지다. 확정은 포커스를 놓는다 — 즉시 모드 필드가 하는 것과
    /// 같고, 사람이 "다 썼다"고 말하는 방법이 그것뿐이다.
    /// </summary>
    [[nodiscard]] bool WasSubmittedThisFrame() const { return mSubmitted; }

    /// <summary>확정된 문자열 안의 캐럿 UTF-8 바이트 오프셋이다.</summary>
    [[nodiscard]] std::size_t GetCaret() const { return mEdit.GetCaret(); }

    [[nodiscard]] bool IsCaretVisible() const { return mFocused && mCaretClock < 0.5f; }
    // 아래 사각형들은 Canvas 배율과 조상 마스크가 이미 적용된 화면 픽셀 좌표다.
    // 장식의 좌표에 Canvas 배율을 다시 곱하지 않는다.
    [[nodiscard]] const RectTransform::Rect& GetCaretRect() const { return mCaretRect; }
    [[nodiscard]] const std::vector<RectTransform::Rect>& GetSelectionRects() const { return mSelectionRects; }
    [[nodiscard]] const std::vector<RectTransform::Rect>& GetCompositionRects() const { return mCompositionRects; }
    /// <summary>긴 한 줄 입력의 캐럿을 보이게 하는 물리 픽셀 이동량이다.</summary>
    [[nodiscard]] float GetTextOffsetX() const { return mTextOffsetX; }
    // GetCaret과 달리, 선택을 조합 문자열로 대체한 표시 문자열 안의 UTF-8 바이트 위치다.
    [[nodiscard]] std::size_t GetDisplayCaret() const { return mDisplayCaret; }

    /// <summary>지금의 선택 범위다. 선택이 없으면 양끝이 같다.</summary>
    [[nodiscard]] Core::TextEditModel::Selection GetSelection() const
    {
        return mEdit.GetSelection();
    }

private:
    // 포커스와 편집 입력을 주는 것은 이벤트 시스템 하나다. 다른 곳이 이것을 넣을 수 있으면
    // 화면이 보이는 포커스와 실제로 글자를 받는 필드가 어긋난다.
    friend class UIEventSystem;

    [[nodiscard]] bool ConsumeFocusRequest()
    {
        const bool requested = mFocusRequested;
        mFocusRequested = false;
        return requested;
    }

    /// <summary>이번 프레임의 포커스를 받는다. 잃는 프레임에는 선택을 접는다.</summary>
    void ApplyFocus(bool focused);

    /// <summary>
    /// 이번 프레임의 편집을 적용한다. 클립보드를 오가는 텍스트는 결과로 주고받는다 — 이
    /// 컴포넌트도 편집 모델도 클립보드가 무엇인지 모른다.
    /// </summary>
    [[nodiscard]] Core::TextEditModel::Result ApplyEditing(
        const Core::TextEditModel::Input& input);

    /// <summary>Enter가 눌린 프레임에 표시를 남긴다.</summary>
    void ApplySubmit();

    /// <summary>이번 프레임의 표시 문자열을 TextRenderer에 싣는다. 조합 중인 글자를 끼운다.</summary>
    void SynchronizeDisplay(std::string_view compositionText, std::size_t compositionCaret = std::string::npos);

    void SynchronizeGeometry(Platform::ITextMeasure* measure, float deltaTime);
    void PlaceCaretAt(float x, float y, bool extend, Platform::ITextMeasure* measure);

    /// <summary>프레임마다의 표시 — 편집됨, 확정됨 — 를 지운다. 시스템이 프레임 앞에서 부른다.</summary>
    void ClearFrameFlags();

    /// <summary>받지 않게 된 필드는 포커스를 쥘 수 없다. 그러면 타이핑이 갈 곳을 잃는다.</summary>
    void OnInteractableLost() override;

    std::string mText;

    Core::TextEditModel mEdit;
    bool mFocused = false;
    bool mFocusRequested = false;
    bool mEdited = false;
    bool mSubmitted = false;
    std::string mCompositionText;
    std::size_t mCompositionBegin = 0;
    std::size_t mDisplayCaret = 0;
    float mCaretClock = 0.0f;
    float mTextOffsetX = 0.0f;
    RectTransform::Rect mCaretRect;
    std::vector<RectTransform::Rect> mSelectionRects;
    std::vector<RectTransform::Rect> mCompositionRects;
};

}
