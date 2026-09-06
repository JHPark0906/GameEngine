#include "GraphicsBackendChoiceTests.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "App/EditorSettings.h"
#include "App/GraphicsBackendChoice.h"
#include "App/ProjectSettings.h"
#include "App/ProjectSettingsLoader.h"
#include "Rendering/GraphicsBackend.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
}

bool RunGraphicsBackendChoiceListTests()
{
    using GameEngine::App::DescribeGraphicsBackendChoice;
    using GameEngine::App::GraphicsBackendChoice;
    using GameEngine::App::ListGraphicsBackendChoices;
    using GameEngine::Rendering::AutomaticGraphicsBackendId;
    using GameEngine::Rendering::GraphicsBackendDescriptor;
    using GameEngine::Rendering::GraphicsBackendRegistry;

    const std::vector<GraphicsBackendChoice> choices = ListGraphicsBackendChoices();
    const std::span<const GraphicsBackendDescriptor> backends =
        GraphicsBackendRegistry::GetBackends();

    const bool oneChoicePerBackend = choices.size() == backends.size() &&
        std::ranges::equal(
            choices, backends,
            [](const GraphicsBackendChoice& choice, const GraphicsBackendDescriptor& backend)
            {
                return choice.id == backend.id;
            });
    const bool everyChoiceIsKnown =
        std::ranges::all_of(choices, [](const GraphicsBackendChoice& choice)
        {
            return GraphicsBackendRegistry::IsKnownId(choice.id) &&
                GraphicsBackendRegistry::Find(choice.id) != nullptr;
        });
    const bool automaticIsNotAChoice =
        std::ranges::none_of(choices, [](const GraphicsBackendChoice& choice)
        {
            return choice.id.empty() || choice.id == AutomaticGraphicsBackendId;
        });

    const GraphicsBackendChoice supported{ "Sample", true };
    const GraphicsBackendChoice unsupported{ "Sample", false };
    const bool labelNamesTheBackend =
        DescribeGraphicsBackendChoice(supported) == "Sample" &&
        DescribeGraphicsBackendChoice(unsupported).starts_with("Sample") &&
        DescribeGraphicsBackendChoice(unsupported) != "Sample";

    return Expect(
               oneChoicePerBackend,
               "the dialog should offer every compiled backend in registry order") &&
        Expect(everyChoiceIsKnown, "every offered choice should resolve in the registry") &&
        Expect(automaticIsNotAChoice, "automatic selection should not be offered as a button") &&
        Expect(
            labelNamesTheBackend,
            "a button label should name the backend and mark one this machine cannot run");
}

bool RunGraphicsBackendChoiceApplyTests()
{
    using GameEngine::App::ApplyGraphicsBackendChoice;
    using GameEngine::App::ApplyRequestedGraphicsBackend;
    using GameEngine::App::FindGraphicsBackendArgument;
    using GameEngine::App::GraphicsBackendChoice;
    using GameEngine::App::GraphicsBackendOption;
    using GameEngine::App::ProjectSettings;
    using GameEngine::Rendering::GraphicsBackendRegistry;

    const std::vector<GraphicsBackendChoice> choices{ { "First", true }, { "Second", false } };

    ProjectSettings chosenSecond;
    chosenSecond.graphicsApi = "Auto";
    const bool secondButtonRequestsSecond =
        ApplyGraphicsBackendChoice(chosenSecond, choices, std::size_t{ 1 }) &&
        chosenSecond.graphicsApi == "Second";

    ProjectSettings chosenFirst;
    chosenFirst.graphicsApi = "Auto";
    const bool firstButtonRequestsFirst =
        ApplyGraphicsBackendChoice(chosenFirst, choices, std::size_t{ 0 }) &&
        chosenFirst.graphicsApi == "First";

    ProjectSettings cancelled;
    cancelled.graphicsApi = "Auto";
    const bool cancelDoesNotStart =
        !ApplyGraphicsBackendChoice(cancelled, choices, std::nullopt) &&
        cancelled.graphicsApi == "Auto";

    ProjectSettings outOfRange;
    outOfRange.graphicsApi = "Auto";
    const bool outOfRangeDoesNotStart =
        !ApplyGraphicsBackendChoice(outOfRange, choices, choices.size()) &&
        outOfRange.graphicsApi == "Auto";

    const std::string option(GraphicsBackendOption);
    const std::vector<std::string> withBackend{ "--other", option, "Second" };
    const std::vector<std::string> withoutOption{ "--other", "Second" };
    const std::vector<std::string> trailingOption{ option };
    const std::optional<std::string> found = FindGraphicsBackendArgument(withBackend);
    const std::optional<std::string> trailing = FindGraphicsBackendArgument(trailingOption);
    const bool argumentIsParsed = found && *found == "Second" &&
        !FindGraphicsBackendArgument(withoutOption) && trailing && trailing->empty();

    // 레지스트리의 첫 백엔드는 이 빌드에 실제로 있는 식별자다.
    const std::string knownId(GraphicsBackendRegistry::GetBackends().front().id);
    ProjectSettings requested;
    requested.graphicsApi = "Auto";
    const bool knownRequestIsApplied =
        ApplyRequestedGraphicsBackend(requested, knownId) && requested.graphicsApi == knownId;
    ProjectSettings unknownRequested;
    unknownRequested.graphicsApi = "Auto";
    const bool unknownRequestIsRejected =
        !ApplyRequestedGraphicsBackend(unknownRequested, "NoSuchBackend") &&
        !ApplyRequestedGraphicsBackend(unknownRequested, "") &&
        unknownRequested.graphicsApi == "Auto";

    // 명령줄은 사람이 없는 실행의 길이므로 "묻겠다"는 값을 받지 않는다.
    ProjectSettings selectRequested;
    selectRequested.graphicsApi = "Auto";
    const bool selectIsRejectedOnTheCommandLine =
        !ApplyRequestedGraphicsBackend(
            selectRequested, GameEngine::App::SelectGraphicsBackendId) &&
        selectRequested.graphicsApi == "Auto";

    return Expect(
               selectIsRejectedOnTheCommandLine,
               "the command line should refuse a choice policy where a backend belongs") &&
        Expect(
               secondButtonRequestsSecond && firstButtonRequestsFirst,
               "pressing a button should request that button's backend") &&
        Expect(cancelDoesNotStart, "cancelling should leave the settings alone and not start") &&
        Expect(outOfRangeDoesNotStart, "an index past the last button should not start") &&
        Expect(argumentIsParsed, "the command-line switch should yield the identifier after it") &&
        Expect(knownRequestIsApplied, "a known command-line identifier should be applied") &&
        Expect(
            unknownRequestIsRejected,
            "an unknown or empty command-line identifier should be rejected");
}

bool RunGraphicsBackendSettingPolicyTests()
{
    using GameEngine::App::DescribeGraphicsBackendSetting;
    using GameEngine::App::EditorSettings;
    using GameEngine::App::EditorSettingsData;
    using GameEngine::App::ListGraphicsBackendSettingValues;
    using GameEngine::App::ProjectSettings;
    using GameEngine::App::ProjectSettingsLoader;
    using GameEngine::App::RequestsGraphicsBackendDialog;
    using GameEngine::App::SelectGraphicsBackendId;
    using GameEngine::Rendering::AutomaticGraphicsBackendId;
    using GameEngine::Rendering::GraphicsBackendRegistry;

    const std::string knownId(GraphicsBackendRegistry::GetBackends().front().id);

    const bool onlySelectAsks = RequestsGraphicsBackendDialog(SelectGraphicsBackendId) &&
        !RequestsGraphicsBackendDialog(AutomaticGraphicsBackendId) &&
        !RequestsGraphicsBackendDialog("") && !RequestsGraphicsBackendDialog(knownId);

    // 정책 값은 백엔드 식별자가 아니다. 이것이 참인 한 "Select"는 장치 팩토리에 도달할 수 없다.
    const bool selectIsNotABackend =
        !GraphicsBackendRegistry::IsKnownId(SelectGraphicsBackendId) &&
        GraphicsBackendRegistry::Find(SelectGraphicsBackendId) == nullptr;

    // 로그가 "엔진이 골랐다"와 "사람에게 묻는다"를 구별해야 한다.
    const std::string automaticDescription =
        DescribeGraphicsBackendSetting(AutomaticGraphicsBackendId);
    const std::string selectDescription = DescribeGraphicsBackendSetting(SelectGraphicsBackendId);
    const bool descriptionsDiffer = automaticDescription != selectDescription &&
        automaticDescription == DescribeGraphicsBackendSetting("") &&
        DescribeGraphicsBackendSetting(knownId).starts_with(knownId);

    // 에디터도 게임 프로젝트도 기본으로는 묻지 않고 엔진이 고른다.
    // 개발 단계 동안 에디터는 묻는 쪽으로 시작한다. 백엔드 작업이 끝나면 다시 엔진이 고른다.
    const bool editorDefaultsToAsking =
        EditorSettingsData{}.graphicsApi == SelectGraphicsBackendId;
    const EditorSettingsData roundTripped =
        EditorSettings::FromText(EditorSettings::ToText(EditorSettingsData{}));
    // 기본값이 무엇이든 파일을 거쳐 그대로 돌아와야 한다. 그 값이 무엇인지는 위에서 따로 본다.
    const bool editorSettingSurvivesTheFile =
        roundTripped.graphicsApi == EditorSettingsData{}.graphicsApi;
    EditorSettingsData asksEveryTime;
    asksEveryTime.graphicsApi = SelectGraphicsBackendId;
    const bool selectSurvivesTheFile =
        EditorSettings::FromText(EditorSettings::ToText(asksEveryTime)).graphicsApi ==
        SelectGraphicsBackendId;
    EditorSettingsData chosenBackend;
    chosenBackend.graphicsApi = knownId;
    const bool anyValueSurvivesTheFile =
        EditorSettings::FromText(EditorSettings::ToText(chosenBackend)).graphicsApi == knownId;

    // 파일을 연 사람에게 선택지가 보여야 한다: 설정 텍스트가 쓸 수 있는 값을 전부 담는다.
    const std::vector<std::string> settingValues = ListGraphicsBackendSettingValues();
    const std::string settingsText = EditorSettings::ToText(EditorSettingsData{});
    const bool everyValueIsOffered =
        settingValues.size() == GraphicsBackendRegistry::GetBackends().size() + 2 &&
        std::ranges::find(settingValues, std::string(AutomaticGraphicsBackendId)) !=
            settingValues.end() &&
        std::ranges::find(settingValues, std::string(SelectGraphicsBackendId)) !=
            settingValues.end() &&
        std::ranges::all_of(settingValues, [&settingsText](const std::string& value)
        {
            return settingsText.find('"' + value + '"') != std::string::npos;
        });

    const std::string projectText =
        R"({"projectName":"BackendDefault","initialSceneId":0,)"
        R"("window":{"width":640,"height":480},)"
        R"("scenes":[{"id":0,"path":"Scenes/Main.scene"}]})";
    const std::span<const std::byte> projectBytes{
        reinterpret_cast<const std::byte*>(projectText.data()), projectText.size() };
    const std::optional<ProjectSettings> loaded = ProjectSettingsLoader::Load(projectBytes, {});
    const bool projectDefaultsToAutomatic =
        loaded && loaded->graphicsApi == AutomaticGraphicsBackendId;

    return Expect(onlySelectAsks, "only the Select setting should bring up the startup dialog") &&
        Expect(
            selectIsNotABackend,
            "the choice policy should never resolve as a backend identifier") &&
        Expect(
            descriptionsDiffer,
            "the log should tell an engine-picked backend from one a person is asked for") &&
        Expect(
            editorDefaultsToAsking,
            "the editor should start by asking which backend to use while it is in development") &&
        Expect(
            editorSettingSurvivesTheFile && selectSurvivesTheFile && anyValueSurvivesTheFile,
            "the editor's backend setting should survive a write and read") &&
        Expect(
            everyValueIsOffered,
            "the settings file should show every value the backend setting accepts") &&
        Expect(
            projectDefaultsToAutomatic,
            "a project that does not name a backend should let the engine choose");
}

static const TestSupport::Registration gGraphicsBackendChoiceListTests{
    "PlayerStartup", "graphics backend choice list tests should pass", RunGraphicsBackendChoiceListTests };

static const TestSupport::Registration gGraphicsBackendChoiceApplyTests{
    "PlayerStartup", "graphics backend choice apply tests should pass", RunGraphicsBackendChoiceApplyTests };

static const TestSupport::Registration gGraphicsBackendSettingPolicyTests{
    "PlayerStartup", "graphics backend setting policy tests should pass", RunGraphicsBackendSettingPolicyTests };
