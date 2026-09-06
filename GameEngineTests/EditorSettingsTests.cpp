#include <filesystem>
#include <iostream>
#include <string>

#include "../GameEngine/App/EditorSettings.h"

#include "EditorSettingsTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::App::EditorSettings;
    using GameEngine::App::EditorSettingsData;

    [[nodiscard]] EditorSettingsData MakeFullData()
    {
        EditorSettingsData data;
        data.panelInSlot = { 2, 0, 3, 1, 5, 4 };
        data.lastProjectPath = "D:/Projects/Sample/Sample.gameproject";
        data.lastSceneId = 7;
        data.hasLastScene = true;
        data.hasSceneCamera = true;
        data.cameraPivot = { 1.5f, -2.0f, 0.25f };
        data.cameraDistance = 12.5f;
        data.cameraYawDegrees = -30.0f;
        data.cameraPitchDegrees = 15.0f;
        data.consoleFloating = true;
        data.consoleWindowX = 240.0f;
        data.consoleWindowY = 120.0f;
        data.consoleWindowWidth = 420.0f;
        data.consoleWindowHeight = 260.0f;
        return data;
    }

    /// <summary>
    /// 자리를 모르는 창은 띄우지 않는다.
    ///
    /// "떠 있다"고만 적히고 사각형이 없거나 망가진 파일에서 창을 띄우면 어디에 서는지 아무도
    /// 답할 수 없다. 도킹은 언제나 성립하는 상태이므로, 의심스러우면 도킹이다.
    /// </summary>
    bool RunConsoleWindowFallbackTests()
    {
        const auto floatingFrom = [](const std::string& text)
        {
            return EditorSettings::FromText(text).consoleFloating;
        };

        // 사각형이 아예 없다.
        const bool missingRectDocks = !floatingFrom(
            R"({"version":1,"consoleFloating":true})");
        // 값이 넷이 아니다.
        const bool shortRectDocks = !floatingFrom(
            R"({"version":1,"consoleFloating":true,"consoleWindow":[10,20]})");
        // 폭이 0이면 그릴 수 없는 창이다.
        const bool emptyRectDocks = !floatingFrom(
            R"({"version":1,"consoleFloating":true,"consoleWindow":[10,20,0,260]})");
        // 수가 아닌 값이 적혀 있다.
        const bool wrongTypeDocks = !floatingFrom(
            R"({"version":1,"consoleFloating":true,"consoleWindow":[10,20,"wide",260]})");
        // 온전한 사각형은 그대로 떠 있다.
        const EditorSettingsData good = EditorSettings::FromText(
            R"({"version":1,"consoleFloating":true,"consoleWindow":[10,20,300,180]})");
        const bool wholeRectFloats = good.consoleFloating && good.consoleWindowWidth == 300.0f &&
            good.consoleWindowHeight == 180.0f;

        // 창을 띄우는 것은 도킹 배정을 건드리지 않는다. 떠 있는 패널도 자기 칸을 그대로 쥐고
        // 있어야 돌아올 자리가 있고, 저장된 순열이 그 자리를 기억한다 — 둘이 한 파일에 있으므로
        // 한쪽을 쓰다 다른 쪽을 잃는 것이 이 자리에서 일어날 수 있는 사고다.
        EditorSettingsData docked;
        docked.panelInSlot = { 4, 1, 0, 6, 2, 5, 3 };
        EditorSettingsData floating = docked;
        floating.consoleFloating = true;
        floating.consoleWindowX = 32.0f;
        floating.consoleWindowY = 48.0f;
        floating.consoleWindowWidth = 400.0f;
        floating.consoleWindowHeight = 240.0f;
        const EditorSettingsData floatingBack =
            EditorSettings::FromText(EditorSettings::ToText(floating));
        const EditorSettingsData dockedAgain =
            EditorSettings::FromText(EditorSettings::ToText(docked));
        const bool layoutSurvivesFloating = floatingBack.panelInSlot == docked.panelInSlot &&
            dockedAgain.panelInSlot == docked.panelInSlot;

        return Expect(missingRectDocks, "a floating console with no rectangle should stay docked") &&
            Expect(shortRectDocks, "a rectangle with too few numbers should leave it docked") &&
            Expect(emptyRectDocks, "a rectangle with no area should leave it docked") &&
            Expect(wrongTypeDocks, "a rectangle holding a non-number should leave it docked") &&
            Expect(wholeRectFloats, "a whole rectangle should restore the floating console") &&
            Expect(
                layoutSurvivesFloating,
                "floating a panel should leave the docking arrangement untouched");
    }

    /// <summary>채워진 값도 기본값도 텍스트를 지나 그대로 돌아온다.</summary>
    bool RunRoundTripTests()
    {
        const EditorSettingsData full = MakeFullData();
        const EditorSettingsData fullBack = EditorSettings::FromText(EditorSettings::ToText(full));

        const EditorSettingsData defaults;
        const EditorSettingsData defaultsBack =
            EditorSettings::FromText(EditorSettings::ToText(defaults));

        return Expect(fullBack == full, "a fully populated settings value should round-trip") &&
            Expect(defaultsBack == defaults, "the default settings value should round-trip");
    }

    /// <summary>같은 값은 같은 바이트다 — 결정적 저장의 약속.</summary>
    bool RunDeterminismTests()
    {
        const std::string first = EditorSettings::ToText(MakeFullData());
        const std::string second = EditorSettings::ToText(MakeFullData());
        return Expect(first == second, "the same settings value should serialize identically");
    }

    /// <summary>깨진 파일과 다른 버전의 파일은 오류가 아니라 기본값이다.</summary>
    bool RunCorruptionAndVersionTests()
    {
        const EditorSettingsData defaults;
        const bool notJson = EditorSettings::FromText("this is not json") == defaults;
        const bool notObject = EditorSettings::FromText("[1, 2, 3]") == defaults;
        const bool missingVersion = EditorSettings::FromText("{}") == defaults;
        const bool otherVersion =
            EditorSettings::FromText(R"({"version": 2, "lastSceneId": 7})") == defaults;

        // 배열의 일부만 숫자면 배정은 통째로 기본값이지만, 나머지 키는 여전히 읽힌다.
        const EditorSettingsData partial = EditorSettings::FromText(
            R"({"version": 1, "panelSlots": [0, "x", 2], "lastSceneId": 3})");
        const bool badSlotsIgnored = partial.panelInSlot.empty();
        const bool goodKeysSurvive = partial.hasLastScene && partial.lastSceneId == 3;

        return Expect(notJson, "unparsable text should read as the defaults") &&
            Expect(notObject, "a non-object root should read as the defaults") &&
            Expect(missingVersion, "a file without a version should read as the defaults") &&
            Expect(otherVersion, "a file with another version should read as the defaults") &&
            Expect(badSlotsIgnored, "a half-numeric panel array should fall back whole") &&
            Expect(goodKeysSurvive, "a broken key should not take the healthy keys with it");
    }

    /// <summary>모르는 키는 무시하고 아는 키만 읽는다 — 미래 버전의 파일에 대한 관용.</summary>
    bool RunUnknownKeyTests()
    {
        const EditorSettingsData data = EditorSettings::FromText(
            R"({"version": 1, "futureFeature": {"a": 1}, "lastProjectPath": "C:/p.gameproject",)"
            R"( "extra": [true]})");
        return Expect(
            data.lastProjectPath == std::filesystem::path("C:/p.gameproject") &&
                !data.hasLastScene && !data.hasSceneCamera,
            "unknown keys should be ignored while known keys are read");
    }

    /// <summary>파일 왕복과, 없는 파일의 조용한 기본값.</summary>
    bool RunFileTests()
    {
        const TestSupport::TemporaryDirectory directory("editor-settings");
        const std::filesystem::path filePath = directory.GetPath() / "GameEditor.settings.json";

        const EditorSettingsData written = MakeFullData();
        const bool saved = EditorSettings::Save(written, filePath);
        const bool loadedBack = EditorSettings::Load(filePath) == written;
        const bool missingIsDefaults =
            EditorSettings::Load(directory.GetPath() / "absent.json") == EditorSettingsData{};

        return Expect(saved, "saving settings to a writable path should succeed") &&
            Expect(loadedBack, "loading the saved file should return the written value") &&
            Expect(missingIsDefaults, "loading a missing file should quietly return defaults");
    }
}

/// <summary>
/// 설정 파일이 없을 때 에디터가 어느 백엔드로 시작하는지 고정한다. 개발 단계 동안은 묻는
/// 쪽이므로, 이 값이 조용히 바뀌면 아무도 대화상자를 보지 못한다.
/// </summary>
bool RunDefaultBackendTests()
{
    const GameEngine::App::EditorSettingsData fresh;
    return Expect(
        fresh.graphicsApi == "Select",
        "a settings file that does not exist yet should start by asking which backend to use");
}

bool RunEditorSettingsTests()
{
    return RunRoundTripTests() && RunDeterminismTests() && RunCorruptionAndVersionTests() &&
        RunConsoleWindowFallbackTests() &&
        RunDefaultBackendTests() &&
        RunUnknownKeyTests() && RunFileTests();
}

static const TestSupport::Registration gEditorSettingsTests{
    "EditorSettings", "editor settings tests should pass", RunEditorSettingsTests };
