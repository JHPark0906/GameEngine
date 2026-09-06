#include "pch.h"
#include "Win32FileDialog.h"

#include <windows.h>
#include <objbase.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <string>

#include "../../Diagnostics/Debug.h"

namespace GameEngine::Platform::Win32
{

/// <summary>공용 대화상자의 이중 널 종단 필터를 만든다.</summary>
[[nodiscard]] std::wstring MakeDialogFilter(
    const std::wstring& filterName, const std::wstring& extension)
{
    // "Game Project (*.gameproject)\0*.gameproject\0All Files (*.*)\0*.*\0"
    std::wstring filter = filterName + L" (*" + extension + L")";
    filter.push_back(L'\0');
    filter += L"*" + extension;
    filter.push_back(L'\0');
    filter += L"All Files (*.*)";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    return filter;
}

std::optional<std::filesystem::path> ShowFileDialog(
    const PlatformServices::FileDialogRequest& request, const bool save)
{
    std::array<wchar_t, 32768> filePath{};
    std::ranges::copy(
        request.suggestedFileName.begin(),
        request.suggestedFileName.end(),
        filePath.begin());

    const std::wstring filter = MakeDialogFilter(request.filterName, request.extension);
    // 점을 뗀 확장자다. 공용 대화상자의 기본 확장자는 점 없이 받는다.
    const std::wstring defaultExtension =
        request.extension.empty() ? std::wstring{} : request.extension.substr(1);
    const std::wstring initialDirectory = request.initialDirectory.native();

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = request.owner.kind == NativeSurfaceKind::Win32
        ? static_cast<HWND>(request.owner.handle)
        : nullptr;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = filePath.data();
    dialog.nMaxFile = static_cast<DWORD>(filePath.size());
    dialog.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    dialog.lpstrTitle = request.title.empty() ? nullptr : request.title.c_str();
    dialog.lpstrDefExt = defaultExtension.empty() ? nullptr : defaultExtension.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (save ? OFN_NOREADONLYRETURN | OFN_OVERWRITEPROMPT
              : OFN_FILEMUSTEXIST);

    // 고전 공용 대화상자는 호출 스레드가 STA일 것을 요구하는데 이 프로세스는 COM을 MTA로
    // 초기화하므로, 대화상자를 열 때 그 아파트먼트를 로그에 남긴다.
    APTTYPE apartmentType = APTTYPE_CURRENT;
    APTTYPEQUALIFIER apartmentQualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT apartmentResult = CoGetApartmentType(&apartmentType, &apartmentQualifier);
    Diagnostics::Debug::Log(
        "Showing a file dialog. save=", save ? 1 : 0,
        ", apartment=",
        FAILED(apartmentResult) ? "unknown"
            : (apartmentType == APTTYPE_MTA ? "MTA"
                : (apartmentType == APTTYPE_STA ? "STA"
                    : (apartmentType == APTTYPE_MAINSTA ? "main STA" : "other"))));

    const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    Diagnostics::Debug::Log("The file dialog closed. accepted=", accepted ? 1 : 0);
    if (!accepted)
    {
        // 취소는 결과 없음이고, 실패만 오류다. 공용 대화상자는 그 둘을 확장 오류 코드로
        // 구분한다.
        if (const DWORD error = CommDlgExtendedError(); error != 0)
        {
            Diagnostics::Debug::LogError("A file dialog failed. error=", error);
        }
        return std::nullopt;
    }
    return std::filesystem::path(filePath.data());
}

}
