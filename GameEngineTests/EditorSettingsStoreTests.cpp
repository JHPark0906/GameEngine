#include <chrono>
#include <filesystem>
#include <iostream>

#include "../GameEditor/Source/Document/EditorSettingsStore.h"

#include "EditorSettingsStoreTests.h"
#include "TestSupport.h"

using GameEditor::EditorSettingsStore;
using TestSupport::Expect;
using TestSupport::TemporaryDirectory;

namespace
{

/// <summary>
/// 파일의 수정 시각을 한 시간 뒤로 돌린다. 그 뒤에 시각이 그대로면 다시 쓰지 않은 것이다.
///
/// 지금 시각과 비교하지 않는 이유는 이 플랫폼의 파일 시각이 15밀리초 남짓 단위로만 움직여서,
/// 연달아 두 번 쓴 것과 한 번만 쓴 것이 같은 값으로 보일 수 있기 때문이다. 한 시간은 그
/// 눈금보다 훨씬 커서 어느 쪽인지 헷갈릴 여지가 없다.
/// </summary>
[[nodiscard]] bool BackdateOneHour(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_time_type written =
        std::filesystem::last_write_time(path, error);
    if (error)
    {
        return false;
    }
    std::filesystem::last_write_time(path, written - std::chrono::hours(1), error);
    return !error;
}

[[nodiscard]] bool IsBackdated(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_time_type written =
        std::filesystem::last_write_time(path, error);
    if (error)
    {
        return false;
    }
    return written < std::filesystem::file_time_type::clock::now() - std::chrono::minutes(30);
}

}

bool RunEditorSettingsStoreTests()
{
    std::cout << "running editor settings store tests\n";

    TemporaryDirectory temporaryDirectory("editor-settings-store");
    const std::filesystem::path settingsFile =
        temporaryDirectory.GetPath() / "GameEditor.settings.json";

    bool passed = true;

    // 없는 파일은 오류가 아니라 기본 상태다. 읽기만으로는 아무것도 쓰지 않는다 — 편집기를
    // 열었다 닫기만 해도 파일이 생기면, 그 파일이 언제 사람의 뜻인지 알 수 없게 된다.
    EditorSettingsStore store(settingsFile);
    passed = Expect(
        !std::filesystem::exists(settingsFile),
        "reading a missing settings file should not create one") && passed;

    // 값이 바뀌면 쓴다.
    const bool snapWas = store.Get().gridSnapEnabled;
    store.SetGridSnapEnabled(!snapWas);
    passed = Expect(
        std::filesystem::exists(settingsFile),
        "changing a setting should write the file") && passed;
    passed = Expect(
        store.Get().gridSnapEnabled == !snapWas,
        "the changed value should be the one the store reports") && passed;

    // 같은 값을 다시 넣으면 쓰지 않는다.
    passed = Expect(BackdateOneHour(settingsFile), "the settings file should be backdated")
        && passed;
    store.SetGridSnapEnabled(!snapWas);
    passed = Expect(
        IsBackdated(settingsFile),
        "setting the same value again should not rewrite the file") && passed;

    // 이쪽이 SaveIfChanged 자신의 계약이다. RememberLastProject는 값을 비교하지 않고 언제나
    // 저장을 부르므로, 건너뛰는 판단이 저장 쪽에 있다는 것을 여기서만 잴 수 있다.
    const std::filesystem::path project = temporaryDirectory.GetPath() / "Thing.gameproject";
    store.RememberLastProject(project);
    passed = Expect(
        !IsBackdated(settingsFile),
        "remembering a different project should write the file") && passed;

    passed = Expect(BackdateOneHour(settingsFile), "the settings file should be backdated again")
        && passed;
    store.RememberLastProject(project);
    passed = Expect(
        IsBackdated(settingsFile),
        "remembering the same project twice should write the file only once") && passed;

    // 쓴 것이 실제로 파일에 남았는지는 다시 읽어서 본다. 메모리에만 남았다면 다음 실행이
    // 사람의 배치를 잃는다.
    const EditorSettingsStore reread(settingsFile);
    passed = Expect(
        reread.Get().gridSnapEnabled == !snapWas &&
            reread.Get().lastProjectPath == project,
        "a store built again from the same file should see what was saved") && passed;

    return passed;
}

static const TestSupport::Registration gEditorSettingsStoreTests{
    "EditorDocument", "editor settings store tests should pass", RunEditorSettingsStoreTests };
