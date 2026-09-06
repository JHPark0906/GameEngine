#include "pch.h"
#include "Win32ChoiceDialog.h"

#include <algorithm>
#include <vector>

#include "Platform/ChoiceDialogLayout.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../../Diagnostics/Debug.h"
#include "Win32Utilities.h"

namespace GameEngine::Platform::Win32
{

namespace
{
    /// <summary>
    /// 메모리 안에 DLGTEMPLATE을 조립한다. 대화상자 리소스는 실행 파일의 리소스 섹션이 아니라
    /// 여기서 만들어지므로, 엔진 정적 라이브러리를 링크하는 어떤 프로젝트도 자기 리소스 스크립트
    /// 없이 대화상자를 띄운다. 단위는 모두 대화상자 단위이며, 대화상자 글꼴과 DPI에 따라 시스템이
    /// 픽셀로 바꾼다.
    /// </summary>
    class DialogTemplateBuilder final
    {
    public:
        void BeginDialog(
            const DWORD style,
            const short width,
            const short height,
            const std::wstring& title,
            const WORD itemCount)
        {
            AppendDword(style);
            AppendDword(0); // 확장 스타일
            AppendWord(itemCount);
            AppendWord(0); // x
            AppendWord(0); // y
            AppendWord(static_cast<WORD>(width));
            AppendWord(static_cast<WORD>(height));
            AppendWord(0); // 메뉴 없음
            AppendWord(0); // 기본 대화상자 클래스
            AppendString(title);
            // DS_SETFONT가 요구하는 글꼴 크기와 이름이다.
            AppendWord(9);
            AppendString(L"Segoe UI");
        }

        /// <param name="controlClass">미리 정의된 클래스 원자다. 0x0080은 버튼, 0x0082는 정적 텍스트다.</param>
        void AddItem(
            const DWORD style,
            const short x,
            const short y,
            const short width,
            const short height,
            const WORD id,
            const WORD controlClass,
            const std::wstring& text)
        {
            AlignToDword();
            AppendDword(style);
            AppendDword(0); // 확장 스타일
            AppendWord(static_cast<WORD>(x));
            AppendWord(static_cast<WORD>(y));
            AppendWord(static_cast<WORD>(width));
            AppendWord(static_cast<WORD>(height));
            AppendWord(id);
            AppendWord(0xFFFF);
            AppendWord(controlClass);
            AppendString(text);
            AppendWord(0); // 생성 데이터 없음
        }

        [[nodiscard]] const DLGTEMPLATE* GetTemplate() const
        {
            return reinterpret_cast<const DLGTEMPLATE*>(mWords.data());
        }

    private:
        void AppendWord(const WORD value) { mWords.push_back(value); }

        void AppendDword(const DWORD value)
        {
            AppendWord(static_cast<WORD>(value & 0xFFFF));
            AppendWord(static_cast<WORD>(value >> 16));
        }

        void AppendString(const std::wstring& text)
        {
            for (const wchar_t character : text)
            {
                AppendWord(static_cast<WORD>(character));
            }
            AppendWord(0);
        }

        void AlignToDword()
        {
            if (mWords.size() % 2 != 0)
            {
                AppendWord(0);
            }
        }

        // DLGTEMPLATE은 DWORD 정렬을 요구한다. 벡터의 저장소는 그보다 강하게 정렬된다.
        std::vector<WORD> mWords;
    };

    constexpr WORD ButtonClass = 0x0080;
    constexpr WORD StaticClass = 0x0082;
    constexpr WORD FirstChoiceId = 1000;


    /// <summary>설명 글이 몇 줄인지다. 줄 수가 대화상자 높이를 정한다.</summary>
    [[nodiscard]] std::size_t CountLines(const std::string& text)
    {
        return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
    }

    /// <summary>
    /// 지금 떠 있는 선택 대화상자다. 창을 닫으려는 요청이 그것을 취소로 끝내려면 어느 창인지
    /// 알아야 한다. 대화상자는 한 번에 하나만 서므로 하나면 족하다.
    /// </summary>
    HWND gActiveDialog = nullptr;

    /// <summary>
    /// 사람이 창을 닫으려 했고 그 요청이 아직 응용에 전해지지 않았는지다. 세워져 있는 동안
    /// 새 물음은 서지 않고 곧바로 취소로 답한다 — 시작하며 여러 물음이 이어 서는 자리에서,
    /// 첫 물음만 닫고 다음 물음이 다시 굳는 일을 막는다.
    /// </summary>
    bool gCancelRequested = false;

    /// <summary>
    /// 눌린 버튼을 EndDialog의 결과로 돌려준다. 결과 0은 취소이고, 선택지는 1부터 센다 —
    /// DialogBox 계열은 실패를 -1로 답하므로 0과 양수만이 이 프로시저의 것이다.
    /// </summary>
    INT_PTR CALLBACK ChoiceDialogProcedure(
        const HWND dialog, const UINT message, const WPARAM wParam, const LPARAM)
    {
        switch (message)
        {
        case WM_INITDIALOG:
            gActiveDialog = dialog;
            return TRUE;
        case WM_COMMAND:
        {
            const WORD id = LOWORD(wParam);
            if (id == IDCANCEL)
            {
                EndDialog(dialog, 0);
                return TRUE;
            }
            if (id >= FirstChoiceId)
            {
                EndDialog(dialog, static_cast<INT_PTR>(id - FirstChoiceId) + 1);
                return TRUE;
            }
            return FALSE;
        }
        case WM_CLOSE:
            EndDialog(dialog, 0);
            return TRUE;
        default:
            return FALSE;
        }
    }
}

void RequestChoiceDialogCancel()
{
    gCancelRequested = true;
    if (gActiveDialog != nullptr)
    {
        // 대화상자에게 스스로 닫으라고 보낸다. 그 프로시저가 WM_CLOSE를 취소로 끝내므로,
        // 취소의 뜻이 한 자리에만 적혀 있게 된다.
        PostMessageW(gActiveDialog, WM_CLOSE, 0, 0);
    }
}

void ClearChoiceDialogCancel()
{
    gCancelRequested = false;
}

std::optional<std::size_t> ShowChoiceDialog(const PlatformServices::ChoiceDialogRequest& request)
{
    // 사람이 이미 창을 닫으려 했으면 더 묻지 않는다. 그 뜻은 "지금은 답하지 않겠다"이고,
    // 그것이 곧 취소다.
    if (gCancelRequested)
    {
        Diagnostics::Debug::Log("Skipped a choice dialog because the window is closing.");
        return std::nullopt;
    }
    const bool hasCancel = !request.cancelLabel.empty();
    const std::size_t buttonCount = request.choices.size() + (hasCancel ? 1 : 0);
    if (buttonCount == 0)
    {
        Diagnostics::Debug::LogError("A choice dialog needs at least one button.");
        return std::nullopt;
    }
    if (buttonCount > 16)
    {
        Diagnostics::Debug::LogError(
            "A choice dialog holds at most 16 buttons. requested=", buttonCount);
        return std::nullopt;
    }

    // 자리는 창을 모르는 산술이 정한다. 그래야 버튼이 잘리거나 겹치는지를 창 없이 잴 수 있다.
    std::vector<std::string> buttonLabels = request.choices;
    if (hasCancel)
    {
        buttonLabels.push_back(request.cancelLabel);
    }
    const ChoiceDialogLayout layout =
        ComputeChoiceDialogLayout(buttonLabels, CountLines(request.message));

    DialogTemplateBuilder builder;
    builder.BeginDialog(
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT,
        layout.width,
        layout.height,
        Utf8ToWide(request.title),
        static_cast<WORD>(buttonCount + 1));
    builder.AddItem(
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        layout.message.left,
        layout.message.top,
        layout.message.width,
        layout.message.height,
        static_cast<WORD>(-1),
        StaticClass,
        Utf8ToWide(request.message));

    // 첫 선택지가 기본 버튼이라 Enter가 그것을 누른다. 취소 버튼이 있으면 목록의 맨 뒤다.
    for (std::size_t index = 0; index < buttonLabels.size(); ++index)
    {
        const bool isCancel = hasCancel && index + 1 == buttonLabels.size();
        const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            (index == 0 ? DWORD{ BS_DEFPUSHBUTTON | WS_GROUP } : DWORD{ BS_PUSHBUTTON });
        const ChoiceDialogRectangle& place = layout.buttons[index];
        builder.AddItem(
            buttonStyle,
            place.left,
            place.top,
            place.width,
            place.height,
            isCancel ? static_cast<WORD>(IDCANCEL) : static_cast<WORD>(FirstChoiceId + index),
            ButtonClass,
            Utf8ToWide(buttonLabels[index]));
    }

    const HWND owner = request.owner.kind == NativeSurfaceKind::Win32
        ? static_cast<HWND>(request.owner.handle)
        : nullptr;
    Diagnostics::Debug::Log("Showing a choice dialog. choices=", request.choices.size());
    const INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), builder.GetTemplate(), owner, ChoiceDialogProcedure, 0);
    // 이 창은 이제 없다. 다음 닫기 요청이 죽은 창에 메시지를 보내지 않게 지운다.
    gActiveDialog = nullptr;
    if (result == -1)
    {
        Diagnostics::Debug::LogError("A choice dialog could not be shown. error=", GetLastError());
        return std::nullopt;
    }
    if (result == 0)
    {
        Diagnostics::Debug::Log("The choice dialog was cancelled.");
        return std::nullopt;
    }
    const std::size_t chosen = static_cast<std::size_t>(result - 1);
    Diagnostics::Debug::Log("The choice dialog closed. chosen=", chosen);
    return chosen;
}

}
