#include "pch.h"
#include "UIEventSystem.h"

#include "../Platform/IClipboard.h"
#include "Button.h"
#include "Dropdown.h"
#include "GameObject.h"
#include "InputField.h"
#include "Input.h"
#include "RectTransform.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Selectable.h"
#include "Transform.h"
#include "UIWindow.h"
#include "UIStackOrder.h"

#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{

namespace
{
    /// <summary>
    /// 커서를 맞힐 후보 하나다. 계층 순서대로 담기므로 뒤에 담긴 것이 위에 있는 것이다.
    ///
    /// 배달 목록과 따로 있는 이유는 둘의 쓰임이 다르기 때문이다. 배달 목록은 판정이 끝난
    /// 뒤 각 요소에게 결과를 나눠 주는 데 쓰이고, 이 목록은 <b>누가 커서를 받는가</b>를 정하는
    /// 데 쓰인다 — 그 물음의 답은 부류가 아니라 계층 순서가 정해야 한다.
    /// </summary>
    struct HitTarget
    {
        Selectable* element = nullptr;
        /// <summary>
        /// 이 요소가 속한 창이다. 창 밖에 놓인 요소는 null이며, 그것은 창들보다 아래에 있는
        /// 바탕이라는 뜻이다.
        /// </summary>
        const UIWindow* window = nullptr;
        const UIWindow* modalAncestor = nullptr;
    };

    /// <summary>
    /// 훑는 동안 만난 창 하나다. 계층 순서대로 담기므로 뒤에 담긴 것이 위에 있는 창이다.
    /// </summary>
    struct WindowRecord
    {
        const UIWindow* window = nullptr;
        const RectTransform* rect = nullptr;
        const UIWindow* modalAncestor = nullptr;
    };

    /// <summary>
    /// 그 요소가 부류 <c>T</c>이면 그것으로, 아니면 null로 좁힌다. 형식 사슬을 걷는 비교라
    /// <c>dynamic_cast</c>가 필요 없다 — 이 프로젝트가 컴포넌트 조회에 쓰는 것과 같은 길이다.
    /// </summary>
    template <typename T>
    [[nodiscard]] T* As(Selectable* const selectable)
    {
        return selectable->GetComponentType().IsDerivedFrom(T::StaticType())
            ? static_cast<T*>(selectable)
            : nullptr;
    }


    /// <summary>
    /// 이번 프레임의 키와 타이핑을 편집 모델의 입력으로 옮긴다. 어느 키가 무엇을 뜻하는지는
    /// 위젯 쪽 지식이고, 그것이 무엇을 바꾸는지는 모델의 지식이다.
    /// </summary>
    [[nodiscard]] UIModel::TextEditModel::Input MakeEditInput(const Input& input)
    {
        const bool control = input.GetKey(Platform::Key::Control);
        // 물리 키는 게임 입력에도 남아 있다. 텍스트 명령만 IME가 이미 처리한 누름을 제외한다.
        const auto pressed = [&input](const Platform::Key key)
        {
            return input.GetKeyDown(key) && !input.GetState().WasKeyHandledByIme(key);
        };
        UIModel::TextEditModel::Input edit;
        edit.typedText = input.GetTypedText();
        edit.deleteForward = pressed(Platform::Key::Delete);
        edit.moveLeft = pressed(Platform::Key::Left);
        edit.moveRight = pressed(Platform::Key::Right);
        edit.moveToStart = pressed(Platform::Key::Home);
        edit.moveToEnd = pressed(Platform::Key::End);
        edit.extendSelection = input.GetKey(Platform::Key::Shift);
        edit.selectAll = control && pressed(Platform::Key::A);
        edit.copy = control && pressed(Platform::Key::C);
        edit.cut = control && pressed(Platform::Key::X);
        edit.paste = control && pressed(Platform::Key::V);
        // Ctrl과 함께 온 글자는 명령이지 내용이 아니다. 그것을 걸러 내지 않으면 Ctrl+A가 전체
        // 선택을 하고 곧바로 그 선택을 'a'로 덮어쓴다.
        if (control)
        {
            edit.typedText = {};
        }
        return edit;
    }
}


UIPointerRouter::Result UIPointerRouter::Update(const Frame& frame)
{
    // 누름과 뗌이 같은 프레임에 오는 경우가 있다. 누름을 먼저 처리해야 그 클릭이 완성된다 —
    // 빠른 클릭 한 번이 사라지는 것이 이 순서를 뒤집었을 때의 증상이다.
    if (mCapturedId == 0 && frame.pressStarted && frame.topmostId != 0)
    {
        mCapturedId = frame.topmostId;
    }

    // 누름이 포커스를 옮긴다. 텍스트를 받지 않는 곳 — 버튼이든 빈 자리든 — 을 누르면 포커스는
    // 거둬진다: 사람이 다른 것을 누른 뒤에도 타이핑이 이전 필드로 계속 들어가면 그 글자는
    // 보이지 않는 곳에 쌓인다.
    if (frame.pressStarted)
    {
        mFocusedId = frame.topmostTakesFocus ? frame.topmostId : 0;
    }

    Result result;
    result.focusedId = mFocusedId;
    if (mCapturedId == 0)
    {
        result.hoveredId = frame.topmostId;
        return result;
    }

    // 잡혀 있는 동안 다른 요소는 아무것도 받지 못한다.
    const bool overCaptured = frame.topmostId == mCapturedId;
    result.hoveredId = overCaptured ? mCapturedId : 0;
    result.pressedId = overCaptured ? mCapturedId : 0;
    if (frame.released)
    {
        result.clickedId = overCaptured ? mCapturedId : 0;
        mCapturedId = 0;
    }
    // 쥔 것은 커서가 어디로 가든 이어지지만 뗀 프레임에 끝난다 — 그래서 놓은 <b>뒤에</b>
    // 답한다. 끌리는 것은 커서 아래에 머물지 않으므로 이것이 눌린 모습과 갈라지고, 몸짓이
    // 끝나는 자리에서는 둘이 다시 만난다.
    result.capturedId = mCapturedId;
    return result;
}

UIEventSystem::UIEventSystem() = default;

UIEventSystem::UIEventSystem(std::unique_ptr<Platform::IClipboard> clipboard)
    : mClipboard(std::move(clipboard))
{
}

UIEventSystem::~UIEventSystem() = default;

unsigned int UIEventSystem::UpdateBackspaceRepeat(
    const Input& input, const unsigned int focusedId, const float deltaTime)
{
    constexpr double initialDelay = 0.4;
    constexpr double repeatInterval = 0.05;
    constexpr unsigned int maxRepeats = 8;
    constexpr double timeTolerance = 0.0000001;
    if (focusedId == 0 || !input.GetState().hasFocus)
    {
        mBackspaceRepeatField = 0;
        mBackspaceRepeatArmed = false;
        return 0;
    }
    if (mBackspaceRepeatField != focusedId)
    {
        mBackspaceRepeatField = focusedId;
        mBackspaceRepeatArmed = false;
    }

    const bool pressed = input.GetKeyDown(Platform::Key::Backspace);
    const bool held = input.GetKey(Platform::Key::Backspace);
    if (pressed)
    {
        mBackspaceRepeatArmed = held;
        mBackspaceRepeatRemaining = initialDelay;
    }
    else if (!held)
    {
        mBackspaceRepeatArmed = false;
    }

    // IME가 조합을 지우는 동안 확정된 본문까지 지우지 않는다. 조합이 끝난 뒤에도 새 지연을
    // 거쳐야 하므로, 마지막 조합 문자가 없어진 프레임에 밀린 반복이 한꺼번에 적용되지 않는다.
    if (!input.GetCompositionText().empty() || input.GetState().WasKeyHandledByIme(Platform::Key::Backspace))
    {
        mBackspaceRepeatRemaining = initialDelay;
        return 0;
    }
    if (pressed) return 1;
    if (!mBackspaceRepeatArmed || !std::isfinite(deltaTime) || deltaTime <= 0.0f) return 0;

    // 물리 키의 눌림 전이는 게임과 공유한다. OS 자동 반복을 새 전이로 바꾸지 않고 UI의
    // 경과 시간으로 반복 수를 계산해, 프레임 속도가 삭제 속도가 되는 것을 막는다.
    mBackspaceRepeatRemaining -= static_cast<double>(deltaTime);
    if (mBackspaceRepeatRemaining > timeTolerance) return 0;
    const double due = std::floor((-mBackspaceRepeatRemaining + timeTolerance) / repeatInterval) + 1.0;
    if (due > maxRepeats)
    {
        // 긴 정지 뒤 본문 전체가 사라지지 않도록 한 번의 따라잡기를 제한하고 남은 빚은 버린다.
        mBackspaceRepeatRemaining = repeatInterval;
        return maxRepeats;
    }
    const auto count = static_cast<unsigned int>(due);
    mBackspaceRepeatRemaining += static_cast<double>(count) * repeatInterval;
    return count;
}

bool UIEventSystem::Synchronize(SceneManager& sceneManager, const Input& input,
    Platform::ITextMeasure* textMeasure, const float deltaTime)
{
    std::vector<Selectable*> selectables;
    std::vector<HitTarget> targets;
    std::vector<WindowRecord> windows;
    const UIStack<GameObject> stack = BuildUIStack(sceneManager);
    for (const UIStackEntry<GameObject>& entry : stack.entries)
    {
        GameObject& object = *entry.object;
        if (const UIWindow* window = object.GetComponent<UIWindow>())
        {
            windows.push_back({ window, object.GetComponent<RectTransform>(), entry.modalAncestor });
        }
        const bool placed = object.GetComponent<RectTransform>() != nullptr;
        for (Selectable* selectable : object.GetComponents<Selectable>())
        {
            if (placed && selectable->IsActiveAndEnabled() && selectable->IsInteractable())
            {
                selectables.push_back(selectable);
                targets.push_back({ selectable, entry.window, entry.modalAncestor });
            }
        }
    }

    const Math::Vector2Int cursor = input.GetMousePosition();
    const auto cursorX = static_cast<float>(cursor.GetX());
    const auto cursorY = static_cast<float>(cursor.GetY());

    const UIWindow* const modalWindow = stack.activeModal;

    // 커서가 어느 창 안에 있는지는 계층 순서로 답한다: 마지막으로 덮은 창이 그 자리의 임자다.
    const UIWindow* windowUnderCursor = nullptr;
    for (const WindowRecord& record : windows)
    {
        if ((!modalWindow || record.modalAncestor == modalWindow) && record.rect &&
            record.rect->GetVisibleRect().Contains(cursorX, cursorY))
        {
            windowUnderCursor = record.window;
        }
    }

    // 겹친 것들 중 뒤가 이긴다: 앞에서부터 훑되 마지막으로 담긴 것을 남긴다. 후보는 부류와
    // 무관하게 하나의 계층 순서로 늘어서 있으므로, 커서를 받는 것은 그 자리에서 가장 위에
    // 그려지는 요소다 — 버튼이든 드롭다운이든 입력 필드든.
    UIPointerRouter::Frame frame;
    for (const HitTarget& target : targets)
    {
        if (modalWindow && target.modalAncestor != modalWindow)
        {
            continue;
        }
        // 커서 자리를 덮는 창이 이 요소의 창이 아니면 이 요소는 가려져 있다. 위 창의 빈 배경도
        // 덮는 것이므로, 그 아래 버튼이 보이지 않는 채로 눌리는 일이 여기서 막힌다.
        //
        // 활성 모달 가지의 창만 후보이므로, 그 안의 자식 창도 빈 배경으로 아래 요소를 가린다.
        if (windowUnderCursor && target.window != windowUnderCursor)
        {
            continue;
        }
        // 펼쳐진 목록은 자기 사각형 밖까지 덮는다. 사각형 검사가 아니라 요소에게 묻는 이유다.
        if (target.element->Covers(cursorX, cursorY))
        {
            frame.topmostId = target.element->GetInstanceId();
            frame.topmostTakesFocus = target.element->TakesFocus();
        }
    }
    frame.pressStarted = input.GetMouseButtonDown(Platform::MouseButton::Left);
    frame.released = input.GetMouseButtonUp(Platform::MouseButton::Left);

    // 잡고 있던 요소가 사라졌으면 — 파괴되었거나 꺼졌거나 장면이 내려갔으면 — 잡음을 놓는다.
    // 놓지 않으면 그 id는 다시 나타나지 않으므로 포인터가 영영 잡힌 채로 남는다. 포커스도
    // 같다: 사라진 필드가 포커스를 쥔 채면 타이핑이 아무 데도 닿지 않는다.
    const auto stillPresent = [&targets, modalWindow](const unsigned int id)
    {
        for (const HitTarget& target : targets)
        {
            if (target.element->GetInstanceId() == id &&
                (!modalWindow || target.modalAncestor == modalWindow))
            {
                return true;
            }
        }
        return false;
    };
    if (const unsigned int captured = mRouter.GetCapturedId();
        captured != 0 && !stillPresent(captured))
    {
        mRouter.ReleaseCapture();
    }
    if (const unsigned int focused = mRouter.GetFocusedId();
        focused != 0 && !stillPresent(focused))
    {
        mRouter.ClearFocus();
    }

    const unsigned int capturedBeforeUpdate = mRouter.GetCapturedId();
    const unsigned int focusedBeforeUpdate = mRouter.GetFocusedId();
    UIPointerRouter::Result result = mRouter.Update(frame);
    unsigned int requestedFocus = 0;
    for (const HitTarget& target : targets)
    {
        InputField* const field = As<InputField>(target.element);
        if (field && field->ConsumeFocusRequest() &&
            (!modalWindow || target.modalAncestor == modalWindow))
        {
            requestedFocus = field->GetInstanceId();
        }
    }
    if (requestedFocus != 0)
    {
        mRouter.Focus(requestedFocus);
        result.focusedId = requestedFocus;
    }

    // 배달은 부류별로 남는다. 받는 것이 서로 다르기 때문이다 — 버튼은 hover·press·click,
    // 드롭다운은 커서 자리와 다른 곳의 눌림과 키, 입력 필드는 포커스와 편집과 클립보드
    // 왕복이다. 한 함수로 묶으면 아무도 쓰지 않는 인자를 모두가 들고 다니게 된다. 대신 훑는
    // 목록은 하나이므로, 부류가 늘어도 후보를 모으는 자리는 그대로다.
    UIModel::ChoiceModel::Input choiceKeys;
    choiceKeys.moveUp = input.GetKeyDown(Platform::Key::Up);
    choiceKeys.moveDown = input.GetKeyDown(Platform::Key::Down);
    choiceKeys.confirm = input.GetKeyDown(Platform::Key::Enter);
    choiceKeys.cancel = input.GetKeyDown(Platform::Key::Escape);
    const UIModel::TextEditModel::Input editInput = MakeEditInput(input);
    const unsigned int backspaces = UpdateBackspaceRepeat(input, result.focusedId, deltaTime);
    // typedText는 이전 입력 읽기 이후 누적된 글자다. 이번 프레임의 클릭/포커스 요청으로
    // 수신자를 먼저 바꾸면 IME의 마지막 확정 음절이 사라지거나 새 필드에 들어간다.
    // 일반 타이핑도 같은 누적 계약이므로, 이전 필드가 남아 있으면 그 필드에 한 번 배달한다.
    // 같은 필드의 클릭/드래그도 기존 선택에 확정한 뒤 캐럿을 옮겨야 한다.
    const bool pointerMovesPreviousCaret = focusedBeforeUpdate != 0 &&
        ((frame.pressStarted && frame.topmostId == focusedBeforeUpdate) ||
            ((result.capturedId == focusedBeforeUpdate ||
                (frame.released && capturedBeforeUpdate == focusedBeforeUpdate)) &&
                (input.GetMouseButton(Platform::MouseButton::Left) || frame.released)));
    bool textBelongsToPreviousFocus = false;
    if ((focusedBeforeUpdate != result.focusedId || pointerMovesPreviousCaret) && !editInput.typedText.empty())
        for (Selectable* const selectable : selectables)
            if (selectable->GetInstanceId() == focusedBeforeUpdate && As<InputField>(selectable))
                textBelongsToPreviousFocus = true;
    const bool submitted = input.GetKeyDown(Platform::Key::Enter) &&
        !input.GetState().imeHandledEnter && !input.GetState().WasKeyHandledByIme(Platform::Key::Enter);

    for (const HitTarget& target : targets)
    {
        Selectable* const selectable = target.element;
        // 모달은 포인터 후보뿐 아니라 이미 쥔 포커스와 펼쳐진 목록의 키 입력도 가린다.
        // 배경의 프레임 표시는 지워야 이전 클릭/편집 결과가 모달 뒤에서 반복되지 않는다.
        if (modalWindow && target.modalAncestor != modalWindow)
        {
            if (Button* const button = As<Button>(selectable))
                button->ApplyPointerState(false, false, false, false);
            if (Dropdown* const dropdown = As<Dropdown>(selectable))
            {
                dropdown->Close();
                dropdown->ClearFrameFlags();
                dropdown->ApplyPointerState(false, false, cursorX, cursorY, false, {});
            }
            if (InputField* const field = As<InputField>(selectable))
            {
                field->ClearFrameFlags();
                field->ApplyFocus(false);
                field->SynchronizeDisplay({});
            }
            continue;
        }
        const unsigned int id = selectable->GetInstanceId();
        if (Button* const button = As<Button>(selectable))
        {
            button->ApplyPointerState(
                id == result.hoveredId, id == result.pressedId, id == result.clickedId,
                id == result.capturedId);
            continue;
        }

        // 목록의 키는 펼쳐진 요소만 듣는다 — 모델이 접혀 있으면 키를 무시하므로 여기서
        // 가리지 않는다. 다른 곳에서 누르기 시작한 것은 펼쳐진 목록을 접으라는 뜻이다.
        if (Dropdown* const dropdown = As<Dropdown>(selectable))
        {
            dropdown->ClearFrameFlags();
            dropdown->ApplyPointerState(
                id == result.hoveredId, id == result.clickedId, cursorX, cursorY,
                frame.pressStarted && frame.topmostId != id, choiceKeys);
            continue;
        }

        // 타이핑은 한 필드만 받는다. 포커스 전환 중이면 선택을 접기 전에 이전 필드에 먼저
        // 확정하고, 새 필드는 이 글자를 다시 받지 않는다.
        InputField* const field = As<InputField>(selectable);
        if (!field)
        {
            continue;
        }
        field->ClearFrameFlags();
        if (textBelongsToPreviousFocus && id == focusedBeforeUpdate)
        {
            UIModel::TextEditModel::Input queuedText;
            queuedText.typedText = editInput.typedText;
            static_cast<void>(field->ApplyEditing(queuedText));
            // 클릭으로 IME가 확정됐으면 표시도 확정된 문자열로 바꾼 뒤 그 폭으로 클릭을 맞힌다.
            // 지난 프레임 preedit가 남으면 PlaceCaretAt가 조합 중으로 보고 클릭을 거절한다.
            field->SynchronizeDisplay(id == result.focusedId ? std::string_view(input.GetCompositionText()) : std::string_view{},
                input.GetState().compositionCaret);
        }
        const bool focused = id == result.focusedId;
        field->ApplyFocus(focused);
        if (!focused)
        {
            field->SynchronizeDisplay({});
            continue;
        }

        if (frame.pressStarted && frame.topmostId == id)
            field->PlaceCaretAt(cursorX, cursorY, input.GetKey(Platform::Key::Shift), textMeasure);
        else if ((result.capturedId == id || (frame.released && id == capturedBeforeUpdate)) &&
            (input.GetMouseButton(Platform::MouseButton::Left) || frame.released))
            field->PlaceCaretAt(cursorX, cursorY, true, textMeasure);

        UIModel::TextEditModel::Input fieldInput = editInput;
        fieldInput.backspace = backspaces != 0;
        if (textBelongsToPreviousFocus) fieldInput.typedText = {};
        std::string pasted;
        if (fieldInput.paste && mClipboard)
        {
            pasted = mClipboard->GetText();
            fieldInput.pastedText = pasted;
        }
        const UIModel::TextEditModel::Result edit = field->ApplyEditing(fieldInput);
        // 반복 때 타이핑·붙여넣기·선택 명령을 재실행하지 않는다. UTF-8 삭제 규칙만 재사용한다.
        UIModel::TextEditModel::Input repeatedBackspace;
        repeatedBackspace.backspace = true;
        for (unsigned int index = 1; index < backspaces; ++index)
            static_cast<void>(field->ApplyEditing(repeatedBackspace));
        if (edit.wroteClipboard && mClipboard)
        {
            mClipboard->SetText(edit.clipboardText);
        }
        field->SynchronizeDisplay(input.GetCompositionText(), input.GetState().compositionCaret);
        field->SynchronizeGeometry(textMeasure, deltaTime);

        // Enter는 "다 썼다"이다: 표시를 남기고 포커스를 놓는다. 조합 중인 Enter는 그 조합을
        // 확정하는 것이지 입력을 끝내는 것이 아니므로 여기 오지 않는다.
        // Enter로 열린 입력창은 그 Enter를 곧바로 전송으로 해석하지 않는다.
        if (submitted && id != requestedFocus && input.GetCompositionText().empty())
        {
            field->ApplySubmit();
            field->ApplyFocus(false);
            mRouter.ClearFocus();
            mBackspaceRepeatField = 0;
            mBackspaceRepeatArmed = false;
        }
    }

    // 호출자가 이 포인터를 자기 것으로도 쓰려 한다면 알아야 한다. 씬 뷰의 도구가 "내가 가져간
    // 입력"만 선언하고 나머지를 흘려보내는 것과 같은 계약이며, 유지 모드 UI와 즉시 모드 UI가
    // 한 화면에 서게 될 때 같은 클릭을 둘이 처리하는 것을 막는 자리가 여기다.
    return modalWindow != nullptr || frame.topmostId != 0 || mRouter.GetCapturedId() != 0;
}


}
