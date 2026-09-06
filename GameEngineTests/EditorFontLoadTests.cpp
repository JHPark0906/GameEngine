#include "EditorFontLoadTests.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Rules/EditorFonts.h"
#include "Diagnostics/Debug.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/PlatformServices.h"
#include "TestSupport.h"
#include "UI/UIContext.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 편집기가 자기 폰트를 찾는 자리다. 편집기는 실행 파일 옆의 자기 콘텐츠에서 읽으므로,
    /// 시험도 <b>staged된 그 자리</b>를 본다 — 저장소의 원본이 아니라. 배포에 실제로 실린
    /// 파일을 묻는 것이 이 시험의 요점이기 때문이다.
    /// </summary>
    [[nodiscard]] std::filesystem::path StagedEditorContent()
    {
        return GameEngine::Platform::PlatformServices::GetExecutableDirectory().parent_path() /
            "GameEditor";
    }

    [[nodiscard]] const char* NameOf(const GameEngine::UI::UIFontRole role)
    {
        switch (role)
        {
        case GameEngine::UI::UIFontRole::Title: return "Title";
        case GameEngine::UI::UIFontRole::Body: return "Body";
        case GameEngine::UI::UIFontRole::Monospace: return "Monospace";
        }
        return "unknown";
    }

    [[nodiscard]] bool LoggedWarningMentioning(const std::string_view fragment)
    {
        const std::vector<GameEngine::Diagnostics::LogEntry> logs =
            GameEngine::Diagnostics::Debug::GetRecentLogs();
        return std::ranges::any_of(
            logs,
            [fragment](const GameEngine::Diagnostics::LogEntry& entry)
            {
                return entry.level == GameEngine::Diagnostics::LogLevel::Warning &&
                    entry.message.find(fragment) != std::string::npos;
            });
    }
}

bool RunEditorFontLoadTests()
{
    namespace UI = GameEngine::UI;

    const std::filesystem::path content = StagedEditorContent();
    std::error_code error;
    if (!Expect(
            std::filesystem::is_directory(content, error) && !error,
            "the editor's content should be staged beside the tests; build GameEditor"))
    {
        return false;
    }

    const GameEngine::Platform::DirectoryContentSource source(content);
    UI::UIContext ui{ nullptr, GameEngine::Platform::PlatformServices::CreateTextRasterizer() };

    // 🔴 편집기가 부르는 그 함수를 그대로 부른다. 목록도 로더도 하나뿐이라, 편집기가 청하는
    // 것과 시험이 확인하는 것이 갈릴 수 없다 — 시험이 루프를 다시 적으면 재는 것은 시험 자신이
    // 된다.
    GameEngine::Diagnostics::Debug::ClearRecentLogs();
    GameEditor::LoadEditorFonts(source, ui);
    const bool nothingWarned = !LoggedWarningMentioning("that role will draw with another loaded font instead");

    // 🔴 배정 실패 0. 하나라도 비면 그 역할의 글자는 조용히 다른 역할의 폰트로 그려지고, 그
    // 다른 역할도 한글을 그릴 수 있는 기계에서는 화면으로 구별되지 않는다.
    int unassignedRoles = 0;
    for (const GameEditor::EditorFont& font : GameEditor::EditorFonts)
    {
        if (!ui.IsFontRoleLoaded(font.role))
        {
            std::cerr << "  this role fell back to another role's font: " << NameOf(font.role)
                      << '\n';
            ++unassignedRoles;
        }
    }
    const bool everyRoleIsAssigned = unassignedRoles == 0;

    // 🔴 실패 경로도 <b>같은 함수로</b> 잰다. 시험이 그 루프를 다시 적으면 재는 것은 시험
    // 자신이 된다 — 폰트 하나만 놓아 둔 자리를 주고, 편집기가 쓰는 그 로더를 그대로 부른다.
    TestSupport::TemporaryDirectory partial("editor-fonts-partial");
    const std::filesystem::path lonelyRoot = partial.GetPath();
    std::filesystem::create_directories(lonelyRoot / "Fonts", error);
    std::filesystem::copy_file(
        content / GameEditor::EditorFonts[1].relativePath,
        lonelyRoot / GameEditor::EditorFonts[1].relativePath, error);
    const bool copiedOne = !error;

    const bool enabledBefore = GameEngine::Diagnostics::Debug::AreMessagesEnabled();
    GameEngine::Diagnostics::Debug::SetMessagesEnabled(true);
    GameEngine::Diagnostics::Debug::ClearRecentLogs();
    const GameEngine::Platform::DirectoryContentSource lonelySource(lonelyRoot);
    UI::UIContext lonely{
        nullptr, GameEngine::Platform::PlatformServices::CreateTextRasterizer() };
    GameEditor::LoadEditorFonts(lonelySource, lonely);
    const bool warnedAboutTheMissingFonts =
        LoggedWarningMentioning("that role will draw with another loaded font instead");
    GameEngine::Diagnostics::Debug::SetMessagesEnabled(enabledBefore);

    const bool onlyThatRoleIsFilled = copiedOne &&
        lonely.IsFontRoleLoaded(UI::UIFontRole::Body) &&
        !lonely.IsFontRoleLoaded(UI::UIFontRole::Title) &&
        !lonely.IsFontRoleLoaded(UI::UIFontRole::Monospace);

    return Expect(
            nothingWarned,
            "loading the editor's fonts from the staged content should warn about nothing") &&
        Expect(
            everyRoleIsAssigned,
            "every editor font role should be assigned; none should fall back to another role") &&
        Expect(
            warnedAboutTheMissingFonts,
            "a font that cannot be read should leave a warning behind") &&
        Expect(
            onlyThatRoleIsFilled,
            "a failed font should empty its own role and leave the others alone");
}

static const TestSupport::Registration gEditorFontLoadTests{
    "EditorDocument", "editor font load tests should pass", RunEditorFontLoadTests };
