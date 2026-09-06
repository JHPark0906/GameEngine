#include <utility>
#include <vector>
#include <algorithm>
#include "EditorDocumentTests.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <memory>
#include <thread>
#include <span>
#include <string>
#include <unordered_set>

#include "Document/UndoStack.h"
#include "Diagnostics/Debug.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/ComponentType.h"
#include "Runtime/Transform.h"
#include "Runtime/RectTransform.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Camera.h"
#include "Runtime/Light.h"
#include "Runtime/Game.h"
#include "Assets/AssetDatabase.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Document/EditorContext.h"
#include "Rules/EditorCloseDecision.h"
#include "Document/EditorCommands.h"
#include "Rules/EditorSceneCommands.h"
#include "Rules/EditorSceneCommands.h"
#include "Views/EditorDockLayout.h"
#include "Rules/EditorFloatingWindowRect.h"
#include "Rules/EditorFonts.h"
#include "Rules/EditorPanelCommon.h"
#include "Views/EditorToolbarView.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Rendering/RenderFrame.h"
#include "Runtime/SpriteRenderer.h"
#include "Rendering/RenderFrameBuilder.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Runtime/Transform.h"
#include "Core/TextFit.h"
#include "Platform/ITextRasterizer.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Platform/PlatformServices.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Rendering/RenderFrame.h"
#include "Runtime/SpriteRenderer.h"
#include "Rendering/RenderFrameBuilder.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UILayoutSystem.h"
#include "UI/PanelSlotLayout.h"

#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using TestSupport::TemporaryDirectory;
    using TestSupport::WriteFile;

    /// <summary>아무것도 하지 않지만 기록될 수는 있는 편집이다. RecordEdit의 경로만 시험한다.</summary>
    class NoOpEditCommand final : public GameEditor::IEditCommand
    {
    public:
        [[nodiscard]] bool Apply() override { return true; }
        [[nodiscard]] bool Revert() override { return true; }
    };

    /// <summary>장면 둘을 가진 최소 프로젝트를 만든다. 경로는 .gameproject 파일이다.</summary>
    [[nodiscard]] std::filesystem::path WriteTestProject(const std::filesystem::path& root)
    {
        const std::filesystem::path projectFile = root / "EditorDocumentTest.gameproject";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "EditorDocumentTest",)"
                R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [)"
                R"( { "id": 0, "path": "Scenes/First.scene" },)"
                R"( { "id": 1, "path": "Scenes/Second.scene" } ]})") &&
            WriteFile(root / "Scenes" / "First.scene",
                R"({"sceneName": "First", "gameObjects": []})") &&
            WriteFile(root / "Scenes" / "Second.scene",
                R"({"sceneName": "Second", "gameObjects": []})");
        return wrote ? projectFile : std::filesystem::path{};
    }
}

/// <summary>
/// 저장되지 않은 편집의 상태 전이를 확인한다.
/// EditorContext는 창 없이 구성하며 HasUnsavedChanges가 저장·편집·장면 전환을 올바르게
/// 구분하는지 검사한다. 확인 창이 실제로 그려지는지는 이 테스트 범위에 포함하지 않는다.
/// </summary>
bool RunEditorUnsavedChangeTests()
{
    TemporaryDirectory temporaryDirectory("editor-document");
    const std::filesystem::path projectFile = WriteTestProject(temporaryDirectory.GetPath());
    if (projectFile.empty())
    {
        return Expect(false, "the editor document test project should be written");
    }

    GameEditor::EditorContext context;
    bool passed = Expect(
        !context.HasUnsavedChanges(), "a context with no project should have nothing to save");

    passed &= Expect(context.OpenProject(projectFile), "the test project should open");
    passed &= Expect(context.HasOpenScene(), "opening a project should open its initial scene");
    passed &= Expect(
        !context.HasUnsavedChanges(),
        "a scene just read from its file should have nothing to save");

    // 기록된 편집이 표시를 세운다. RecordEdit이 편집의 단일 길목이므로 여기가 그 자리다.
    context.RecordEdit(std::make_unique<NoOpEditCommand>());
    passed &= Expect(context.HasUnsavedChanges(), "a recorded edit should mark the scene unsaved");

    passed &= Expect(context.SaveOpenScene(), "the open scene should save");
    passed &= Expect(!context.HasUnsavedChanges(), "saving should clear the mark");

    // 플레이는 이 답을 바꾸지 않는다. 이탈이 되돌리는 스냅숏은 진입할 때의 상태 — 저장되지 않은
    // 편집을 담은 그 상태 — 이므로, 다녀와도 파일과의 차이는 그대로다. 이 전이가 틀리면 편집이
    // 남아 있는데 저장된 것처럼 보이고, 그것이 정확히 잃는 모양이다.
    context.MarkEdited();
    passed &= Expect(context.EnterPlayMode(), "play mode should start");
    passed &= Expect(
        context.HasUnsavedChanges(), "entering play should not clear unsaved changes");
    passed &= Expect(context.ExitPlayMode(), "play mode should stop");
    passed &= Expect(
        context.HasUnsavedChanges(), "leaving play should not clear unsaved changes either");

    // 다른 장면을 열면 새 문서다: 파일에서 막 읽어 왔으므로 저장할 것이 없다. 떠나기 전에 묻는
    // 일은 UI의 몫이고, 여기서는 연 뒤의 상태만 정한다.
    passed &= Expect(context.OpenScene(1), "the second scene should open");
    passed &= Expect(
        !context.HasUnsavedChanges(), "opening another scene should leave nothing to save");

    // Play 중의 편집은 기록되지 않는다(CanRecordEdits). 표시도 서지 않아야 한다 — 그것을 세우면
    // 스크립트가 움직인 상태를 사람의 편집으로 잘못 말하게 된다.
    passed &= Expect(context.EnterPlayMode(), "play mode should start again");
    context.RecordEdit(std::make_unique<NoOpEditCommand>());
    passed &= Expect(
        !context.HasUnsavedChanges(), "an edit refused during play should not mark the scene");
    passed &= Expect(context.ExitPlayMode(), "play mode should stop again");

    // Play를 종료한 뒤의 편집도 미저장 상태로 기록해야 한다.
    // 장면 재생성 이후의 변경을 놓치면 프로젝트 전환 시 저장되지 않은 작업을 잃을 수 있다.
    passed &= Expect(context.SaveOpenScene(), "the scene should save after play");
    passed &= Expect(!context.HasUnsavedChanges(), "saving after play should clear the mark");
    passed &= Expect(context.EnterPlayMode(), "play mode should start once more");
    passed &= Expect(context.ExitPlayMode(), "play mode should stop once more");
    context.RecordEdit(std::make_unique<NoOpEditCommand>());
    passed &= Expect(
        context.HasUnsavedChanges(), "an edit made after leaving play should mark the scene");

    return passed;
}

/// <summary>
/// 도킹 슬롯의 기본 비율이다.
///
/// 값들이 그리기 코드 안의 상수로 흩어져 있으면 "다른 값이었으면 어땠을지"를 재 볼 방법이 없어,
/// 좁다는 보고에 눈대중으로만 답하게 된다. 슬롯 사각형은 창 크기만으로 정해지는 순수한 계산이
/// 므로 여기서 산술로 확인한다 — 특히 각 패널이 몇 줄을 담게 되는지를.
///
/// 확인하지 않는 것: 그렇게 나뉜 화면이 실제로 어떻게 보이는지. 그것은 창이 필요하다.
/// </summary>
namespace
{
    /// <summary>
    /// Play 모드가 프로젝트 컴포넌트를 실제로 갱신하는지 세는 컴포넌트다. 프로젝트가 자기
    /// 컴포넌트를 등록하는 것과 같은 문 — <c>RegisterComponentType</c> — 으로 들어온다.
    /// </summary>
    class PlayTickProbe final : public GameEngine::Runtime::MonoBehaviour
    {
    public:
        [[nodiscard]] static const GameEngine::Runtime::ComponentType& StaticType()
        {
            static const GameEngine::Runtime::ComponentType type{
                "PlayTickProbe", &GameEngine::Runtime::MonoBehaviour::StaticType(),
                &NoProperties, &GameEngine::Runtime::MakeComponentInstance<PlayTickProbe> };
            return type;
        }
        [[nodiscard]] const GameEngine::Runtime::ComponentType& GetComponentType() const override
        {
            return StaticType();
        }

        /// <summary>프로세스 전체에서 Update가 불린 횟수다.</summary>
        static int sUpdateCount;

    protected:
        void Update(float /*deltaTime*/) override { ++sUpdateCount; }

    private:
        static std::span<const GameEngine::Runtime::PropertyDescriptor> NoProperties()
        {
            return {};
        }
    };
    int PlayTickProbe::sUpdateCount = 0;
}

/// <summary>
/// Play를 누르면 프로젝트 컴포넌트의 <c>Update</c>가 실제로 불리는지 확인한다.
///
/// 프로젝트 컴포넌트는 그 프로젝트의 소스가 <c>RegisterComponentType</c>으로 등록해야 에디터
/// 프로세스에 존재한다. 이 시험은 그 등록을 프로젝트가 하는 그대로 한 뒤, 장면에 그 컴포넌트를
/// 실은 프로젝트를 열고 Play 모드로 들어가 런타임을 돌리며 호출 횟수를 센다.
/// </summary>
bool RunEditorPlayModeComponentTests()
{
    // 프로젝트가 자기 팩토리에서 하는 것과 같은 등록이다. 한 프로세스에서 한 번이면 된다.
    static const bool registered =
        GameEngine::Serialization::RegisterComponentType(PlayTickProbe::StaticType());
    if (!Expect(registered, "the probe component should register through the shared door"))
    {
        return false;
    }

    TemporaryDirectory temporaryDirectory("editor-play-component");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = root / "PlayProbe.gameproject";
    const bool wrote = WriteFile(projectFile,
        R"({"projectName": "PlayProbe",)"
        R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
        R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/Main.scene" } ]})") &&
        WriteFile(root / "Scenes" / "Main.scene",
            R"({"sceneName": "Main", "gameObjects": [ { "name": "Probe", "isActive": true,)"
            R"( "components": [)"
            R"( { "type": "Transform", "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },)"
            R"( { "type": "PlayTickProbe" } ] } ]})");
    if (!Expect(wrote, "the play-mode test project should be written"))
    {
        return false;
    }

    PlayTickProbe::sUpdateCount = 0;
    GameEditor::EditorContext context;
    bool passed = Expect(context.OpenProject(projectFile), "the test project should open");
    passed &= Expect(context.HasOpenScene(), "the scene carrying the probe should open");

    // 장면을 읽는 것만으로는 아무것도 갱신되지 않는다.
    passed &= Expect(
        PlayTickProbe::sUpdateCount == 0,
        "loading a scene should not update the components in it");

    // Play에 들어가 런타임을 돌리면 프레임마다 한 번씩 불린다.
    passed &= Expect(context.EnterPlayMode(), "play mode should start");
    GameEngine::Runtime::Game* const projectGame = context.GetProjectGame();
    passed &= Expect(projectGame != nullptr, "play mode should have a runtime to update");
    constexpr int Frames = 5;
    if (projectGame)
    {
        for (int frame = 0; frame < Frames; ++frame)
        {
            projectGame->Update(1.0f / 60.0f);
        }
    }
    passed &= Expect(
        PlayTickProbe::sUpdateCount == Frames,
        "a project component should be updated once per frame while playing");

    passed &= Expect(context.ExitPlayMode(), "play mode should stop");
    return passed;
}

/// <summary>
/// 에디터가 장면을 만들고·이름을 바꾸고·지우는 명령을 실제로 내놓는지 확인한다.
///
/// 이 시험이 있는 이유는 기능이 <b>조용히</b> 사라질 수 있기 때문이다. 어떤 기능을 담은 파일을
/// 지우는 병합은 그 기능도 함께 지우는데, 컴파일도 통과하고 경고도 없고 엔진 쪽 시험도 그대로
/// 통과한다 — 그것을 부르는 곳이 함께 사라졌으니 아무도 불평하지 않는다. 없어지는 것은 사용자가
/// 쓰던 기능뿐이다.
///
/// <b>초록불은 사라진 파일 안에 있던 것에 대해 아무 말도 하지 않는다.</b> 그것을 사람의 주의력에
/// 맡기지 않으려고 이 시험이 있다.
/// </summary>
bool RunSceneCommandTests()
{
    using GameEditor::SceneCommand;
    using GameEditor::SceneCommandInfo;

    const std::span<const SceneCommandInfo> commands = GameEditor::SceneCommands();

    const auto has = [&commands](const SceneCommand command)
    {
        return GameEditor::FindSceneCommand(command) != nullptr;
    };

    // 셋 다 있어야 한다. 하나라도 사라지면 여기가 붉어진다.
    const bool hasAllThree =
        has(SceneCommand::New) && has(SceneCommand::Rename) && has(SceneCommand::Delete);
    const bool listMatchesLookup = commands.size() == 3;

    // 이름이 비어 있으면 버튼에 글자가 없다 — 눌러 볼 수는 있지만 무엇인지 알 수 없다.
    bool everyCommandIsLabelled = true;
    for (const SceneCommandInfo& info : commands)
    {
        everyCommandIsLabelled = everyCommandIsLabelled && !info.label.empty();
    }

    // 셋 다 열려 있는 장면을 떠난다: 만들면 새것을 열고, 이름을 바꾸면 편집 중인 파일이 옮겨
    // 가고, 지우면 그 장면이 없어진다. 그래서 저장되지 않은 편집 앞에서는 먼저 물어야 한다 —
    // 묻지 않으면 사람이 한 일이 사라지는 길이 열린다.
    bool everyCommandAsksBeforeLeaving = true;
    for (const SceneCommandInfo& info : commands)
    {
        everyCommandAsksBeforeLeaving = everyCommandAsksBeforeLeaving && info.leavesTheOpenScene;
    }

    // 지우기만 따로 한 번 더 묻는다. 되돌릴 수 없는 유일한 것이다.
    const SceneCommandInfo* const remove = GameEditor::FindSceneCommand(SceneCommand::Delete);
    const SceneCommandInfo* const create = GameEditor::FindSceneCommand(SceneCommand::New);
    const SceneCommandInfo* const rename = GameEditor::FindSceneCommand(SceneCommand::Rename);
    const bool onlyDeleteConfirms = remove && create && rename &&
        remove->needsConfirmation && !create->needsConfirmation && !rename->needsConfirmation;

    // 그리고 무엇을 지우고 무엇의 이름을 바꿀지는 골라져 있어야 안다. 만들기는 아니다.
    const bool selectionRulesAreRight = remove && create && rename &&
        remove->needsSelectedScene && rename->needsSelectedScene && !create->needsSelectedScene;

    return Expect(hasAllThree, "the editor should offer new, rename and delete for scenes") &&
        Expect(listMatchesLookup, "every scene command should be in the list that is looked up") &&
        Expect(everyCommandIsLabelled, "every scene command should carry a label to show") &&
        Expect(
            everyCommandAsksBeforeLeaving,
            "every scene command should ask before leaving unsaved edits") &&
        Expect(
            onlyDeleteConfirms,
            "deleting should confirm separately, and only deleting should") &&
        Expect(
            selectionRulesAreRight,
            "renaming and deleting should need a chosen scene, and creating should not");
}

bool RunEditorDockLayoutTests()
{
    using GameEditor::ComputeDockSlotRects;
    using GameEditor::DockSlotCount;
    using GameEditor::DockSlotMetrics;
    using GameEngine::UI::UIRect;

    // 에디터 자신의 .gameproject가 요청하는 창이다. 배율 1의 논리 픽셀로 계산한다.
    constexpr float Width = 1440.0f;
    constexpr float Height = 900.0f;
    const DockSlotMetrics metrics;
    const std::array<UIRect, DockSlotCount> slots =
        ComputeDockSlotRects(Width, Height, metrics);

    const auto nearly = [](const float left, const float right)
    {
        return std::abs(left - right) < 0.01f;
    };

    // 컬럼 셋이 창 너비를 빈틈없이 덮는다. 겹치거나 벌어지면 패널 사이에 죽은 띠가 생긴다.
    const UIRect& hierarchy = slots[0];
    const UIRect& inspector = slots[1];
    const UIRect& contentBrowser = slots[2];
    const UIRect& scene = slots[3];
    const UIRect& console = slots[4];
    const UIRect& game = slots[5];
    const bool columnsTile = nearly(hierarchy.x, 0.0f) &&
        nearly(scene.x, hierarchy.width) &&
        nearly(game.x, hierarchy.width + scene.width) &&
        nearly(hierarchy.width + scene.width + game.width, Width);

    // 좌측 세 자리가 본문 높이를 빈틈없이 나눈다.
    const float bodyTop = metrics.toolbarHeight;
    const float bodyHeight = Height - bodyTop;
    const bool leftColumnTiles = nearly(hierarchy.y, bodyTop) &&
        nearly(inspector.y, hierarchy.GetBottom()) &&
        nearly(contentBrowser.y, inspector.GetBottom()) &&
        nearly(contentBrowser.GetBottom(), bodyTop + bodyHeight);
    const bool centerColumnTiles = nearly(scene.y, bodyTop) &&
        nearly(console.y, scene.GetBottom()) &&
        nearly(console.GetBottom(), bodyTop + bodyHeight);

    // 패널이 담는 줄 수. 제목줄(HeaderHeight)을 뺀 것이 내용 높이이고, 계층과 콘텐츠 브라우저는
    // 그 위에 버튼 줄을 하나 더 얹는다.
    constexpr float HeaderHeight = 22.0f;
    constexpr float RowHeight = 20.0f;
    constexpr float ButtonRowHeight = RowHeight + 2.0f * 4.0f;
    const auto listRows = [](const float slotHeight, const float reserved)
    {
        return static_cast<int>((slotHeight - HeaderHeight - reserved) / RowHeight);
    };

    const int hierarchyRows = listRows(hierarchy.height, ButtonRowHeight);

    // 그런데 계층이 좁아져서는 안 된다. SampleGame의 Main.scene은 프로젝트 머리글 하나, 장면
    // 파일 둘, 열린 장면 머리글 하나, 객체 여덟 — 열두 줄이다. 그것이 스크롤 없이 들어가야
    // 한다는 것이 이 값의 바닥이다.
    constexpr int SampleSceneRows = 12;
    const bool hierarchyStillFitsTheSample = hierarchyRows >= SampleSceneRows;

    // 팔레트가 우측 컬럼 아래에 놓이며, 34px 칸으로 최소 여덟 줄을 담을 수 있어야 한다.
    constexpr float PaletteCellSize = 34.0f;
    const UIRect& palette = slots[6];
    const int paletteRows =
        static_cast<int>((palette.height - HeaderHeight - 3.0f * RowHeight) / PaletteCellSize);
    const bool paletteHasRoom = paletteRows >= 8;
    const bool rightColumnTiles = nearly(game.y, bodyTop) &&
        nearly(palette.y, game.GetBottom()) &&
        nearly(palette.GetBottom(), bodyTop + bodyHeight) &&
        nearly(palette.x, game.x) && nearly(palette.width, game.width);

    // 슬롯 수가 맞지 않는 저장 배치는 거절하고 기본 배정을 유지해야 한다.
    // 여섯 자리 배치로 일곱 패널을 복원하면 패널이 중복되거나 누락될 수 있다.
    GameEngine::UI::PanelSlotLayout layout(DockSlotCount);
    const bool staleLayoutRejected = !layout.TrySetAssignment({ 0, 1, 2, 3, 4, 5 });
    const bool defaultSurvives = layout.GetPanelInSlot(6) == 6;
    const bool currentLayoutAccepted = layout.TrySetAssignment({ 6, 0, 1, 2, 3, 4, 5 }) &&
        layout.GetPanelInSlot(0) == 6;

    // 팔레트 높이에 상한이 없다는 것. 상한을 두면 그것이 곧 두 번째 스크롤이 된다 — 자리가
    // 아무리 남아도 그 줄 수까지만 보인다. 팔레트는 남은 자리를 그대로 쓴다.
    using GameEditor::ComputePaletteBoxHeight;
    constexpr int TilesetRows = 29;  // 16x16 타일셋을 아홉 열로 늘어놓은 줄 수.
    const float paletteAvailable = palette.height - HeaderHeight - 3.0f * RowHeight;
    const float paletteBox =
        ComputePaletteBoxHeight(paletteAvailable, TilesetRows, PaletteCellSize);
    const bool paletteUsesItsPanel = nearly(paletteBox, paletteAvailable) &&
        paletteBox > 6.0f * PaletteCellSize;
    // 칸이 적으면 자리를 남긴다 — 빈 상자를 늘려 잡지 않는다.
    const bool paletteShrinksToContent =
        nearly(ComputePaletteBoxHeight(paletteAvailable, 2, PaletteCellSize),
            2.0f * PaletteCellSize);

    // 좁은 창에서는 하한이, 넓은 창에서는 상한이 컬럼을 붙든다.
    const std::array<UIRect, DockSlotCount> narrow =
        ComputeDockSlotRects(900.0f, 600.0f, metrics);
    const std::array<UIRect, DockSlotCount> wide =
        ComputeDockSlotRects(3840.0f, 2160.0f, metrics);

    // 어느 두 슬롯도 겹치지 않는다. 입력은 커서 아래 마지막으로 선언된 위젯에게 가므로, 슬롯이
    // 겹치면 아래 패널의 위젯은 선언 순서에 따라 입력을 잃는다. 지금 배치가 겹치지 않는다는
    // 것이 "그 규칙이 이 배치에서는 아무것도 바꾸지 않는다"의 근거이고, 나중에 누가 슬롯을
    // 겹치게 만들면 이 검사가 그 사실을 알린다 — 그때부터는 규칙이 실제로 물기 시작한다.
    const auto noneOverlap = [](const std::array<UIRect, DockSlotCount>& rects)
    {
        for (std::size_t left = 0; left < rects.size(); ++left)
        {
            for (std::size_t right = left + 1; right < rects.size(); ++right)
            {
                const UIRect& a = rects[left];
                const UIRect& b = rects[right];
                const bool apart = a.x + a.width <= b.x || b.x + b.width <= a.x ||
                    a.y + a.height <= b.y || b.y + b.height <= a.y;
                if (!apart)
                {
                    return false;
                }
            }
        }
        return true;
    };
    const bool slotsNeverOverlap =
        noneOverlap(slots) && noneOverlap(narrow) && noneOverlap(wide);
    const bool clampsHold = nearly(narrow[0].width, metrics.leftMinimum) &&
        nearly(wide[0].width, metrics.leftMaximum) &&
        nearly(wide[5].width, metrics.rightMaximum);

    return Expect(columnsTile, "the three columns should tile the window width") &&
        Expect(leftColumnTiles, "the left column's three slots should tile the body height") &&
        Expect(centerColumnTiles, "the centre column's two slots should tile the body height") &&
        Expect(rightColumnTiles, "the right column's two slots should tile the body height") &&
        Expect(
            paletteHasRoom,
            "the tile palette slot should hold at least eight rows of cells") &&
        Expect(
            hierarchyStillFitsTheSample,
            "the hierarchy should still fit the sample scene's twelve rows without scrolling") &&
        Expect(clampsHold, "column widths should clamp at both ends") &&
        Expect(
            slotsNeverOverlap,
            "no two dock slots should overlap at any window size") &&
        Expect(
            staleLayoutRejected && defaultSurvives,
            "a saved six-slot arrangement should be refused now that there are seven") &&
        Expect(currentLayoutAccepted, "a seven-slot arrangement should still be restored") &&
        Expect(
            paletteUsesItsPanel,
            "the palette should fill its panel rather than stop at the old six-row cap") &&
        Expect(
            paletteShrinksToContent,
            "a palette with few tiles should not stretch to fill the panel");
}

/// <summary>
/// 툴바 앵커의 사각형이 도킹 계산에서 비워 둔 툴바 영역과 같은지 확인한다.
/// 두 영역이 일치해야 툴바와 나머지 패널의 배치가 겹치지 않는다.
/// </summary>
bool RunEditorToolbarStripTests()
{
    using GameEditor::ComputeDockSlotRects;
    using GameEditor::DockSlotCount;
    using GameEditor::DockSlotMetrics;
    using GameEngine::Runtime::RectTransform;

    constexpr float Width = 1440.0f;
    constexpr float Height = 900.0f;
    // 화면 배율 1이 아닌 값을 쓴다: 앵커는 비율이라 배율을 타지 않고 오프셋만 타므로, 배율이
    // 1이면 그 구분이 시험되지 않는다.
    constexpr float Scale = 1.5f;

    GameEngine::Runtime::ObjectRegistry registry;
    const GameEngine::Runtime::Input input;
    GameEngine::Runtime::RuntimeContext context{ registry, input };
    GameEngine::Runtime::SceneManager sceneManager{ context };

    auto scene = std::make_unique<GameEngine::Runtime::Scene>(context, "EditorUI");
    GameEngine::Runtime::GameObject* const canvasObject = scene->CreateGameObject("EditorUI");
    GameEngine::Runtime::Canvas* const canvas =
        canvasObject ? canvasObject->AddComponent<GameEngine::Runtime::Canvas>() : nullptr;
    if (canvas)
    {
        canvas->SetScaleFactor(Scale);
    }

    // 툴바가 선언하는 그대로다: 가로로 꽉 찬 띠, 높이는 공용 상수, 오프셋은 논리 픽셀.
    GameEngine::Runtime::GameObject* const stripObject = scene->CreateGameObject("Toolbar");
    RectTransform* strip = nullptr;
    if (stripObject && canvasObject)
    {
        static_cast<void>(stripObject->GetTransform().SetParent(&canvasObject->GetTransform()));
        strip = stripObject->AddComponent<RectTransform>();
    }
    if (!strip)
    {
        return Expect(false, "the toolbar strip test scene should assemble");
    }
    strip->SetAnchorMin({ 0.0f, 0.0f });
    strip->SetAnchorMax({ 1.0f, 0.0f });
    strip->SetOffsetMin({ 0.0f, 0.0f });
    // 툴바가 쓰는 한 줄의 높이다. 선언이 하나뿐이므로 띠와 도킹이 같은 값을 볼 수밖에 없다.
    strip->SetOffsetMax({ 0.0f, GameEditor::ToolbarLayoutMetrics{}.rowHeight });

    static_cast<void>(sceneManager.AddScene(std::move(scene)));
    const GameEngine::Runtime::UILayoutSystem layout;
    layout.Synchronize(sceneManager, Width, Height);

    // 도킹 계산이 비워 두는 띠. 셸이 하는 것과 같이 배율은 미리 곱해 넘긴다.
    //
    // 툴바 높이를 여기서 이어 붙이지 않는다. 시험이 두 값을 같게 만들어 놓고 재면 "둘이
    // 같다"를 증명하지 못한다 — 도킹이 자기 기본값을 쓰게 두고, 그 기본값이 툴바의 한 줄에서
    // 나오는지를 이 시험이 확인한다.
    DockSlotMetrics metrics;
    metrics.toolbarHeight *= Scale;
    metrics.leftMinimum *= Scale;
    metrics.leftMaximum *= Scale;
    metrics.rightMinimum *= Scale;
    metrics.rightMaximum *= Scale;
    metrics.minimumExtent *= Scale;
    const std::array<GameEngine::UI::UIRect, DockSlotCount> slots =
        ComputeDockSlotRects(Width, Height, metrics);

    const RectTransform::Rect& resolved = strip->GetResolvedRect();
    const auto nearly = [](const float left, const float right)
    {
        return std::abs(left - right) < 0.01f;
    };
    const bool coversTheStrip = nearly(resolved.x, 0.0f) && nearly(resolved.y, 0.0f) &&
        nearly(resolved.width, Width) && nearly(resolved.height, GameEditor::ToolbarLayoutMetrics{}.rowHeight * Scale);
    // 그리고 그 띠 바로 아래에서 첫 슬롯이 시작한다: 툴바가 덮는 자리와 패널이 쓰는 자리가
    // 맞닿아 있고 겹치지 않는다.
    const bool panelsStartBelow = nearly(slots[0].y, resolved.GetBottom());

    return Expect(
            coversTheStrip,
            "the toolbar's anchors should resolve to the full-width strip at the top") &&
        Expect(panelsStartBelow, "the docked panels should start exactly below that strip");
}

/// <summary>
/// 떠 있는 창이 화면 밖으로 사라지지 않는지 확인한다.
///
/// 이 산술이 틀리면 사람이 창을 잃는다 — 제목줄이 화면 밖으로 나가면 다시 잡을 방법이 없고,
/// 그 상태를 되돌릴 길도 없다. 그래서 재는 것은 "창이 다 보이는가"가 아니라 <b>다시 잡을 수
/// 있는가</b>이다.
/// </summary>
bool RunEditorFloatingWindowClampTests()
{
    using GameEditor::ClampFloatingWindow;
    using GameEngine::UI::UIRect;

    constexpr float Width = 1440.0f;
    constexpr float Height = 900.0f;
    constexpr float Visible = GameEditor::MinimumVisibleWindow;
    const UIRect window{ 200.0f, 150.0f, 420.0f, 260.0f };

    const auto nearly = [](const float left, const float right)
    {
        return std::abs(left - right) < 0.01f;
    };

    // 화면 안에 온전히 있는 창은 움직이지 않는다.
    const UIRect inside = ClampFloatingWindow(window, Width, Height, Visible);
    const bool insideUnchanged = nearly(inside.x, window.x) && nearly(inside.y, window.y);

    // 왼쪽으로 멀리 밀면 오른쪽 끝이 화면 안에 남는 자리에서 멈춘다.
    UIRect farLeft = window;
    farLeft.x = -5000.0f;
    const UIRect leftClamped = ClampFloatingWindow(farLeft, Width, Height, Visible);
    const bool stopsAtLeft = nearly(leftClamped.x, Visible - window.width);

    // 오른쪽으로 멀리 밀면 왼쪽 끝이 화면 안에 남는 자리에서 멈춘다.
    UIRect farRight = window;
    farRight.x = 9000.0f;
    const UIRect rightClamped = ClampFloatingWindow(farRight, Width, Height, Visible);
    const bool stopsAtRight = nearly(rightClamped.x, Width - Visible);

    // 위로는 제목줄이 나가지 못한다 — 나가면 잡을 것이 없어진다.
    UIRect above = window;
    above.y = -300.0f;
    const bool stopsAtTop = nearly(ClampFloatingWindow(above, Width, Height, Visible).y, 0.0f);

    // 아래로는 머리만 남아도 잡을 수 있으므로 화면 끝 가까이까지 허용한다.
    UIRect below = window;
    below.y = 5000.0f;
    const bool stopsAtBottom =
        nearly(ClampFloatingWindow(below, Width, Height, Visible).y, Height - Visible);

    // 남겨야 할 길이보다 좁은 창은 통째로 보이는 것이 하한이다 — 자기보다 더 보일 수는 없다.
    UIRect narrow{ -500.0f, 10.0f, 40.0f, 80.0f };
    const UIRect narrowClamped = ClampFloatingWindow(narrow, Width, Height, Visible);
    const bool narrowStaysWhole = nearly(narrowClamped.x, 0.0f);

    return Expect(insideUnchanged, "a window already on screen should not be moved") &&
        Expect(stopsAtLeft, "dragging a window off the left should leave a grabbable strip") &&
        Expect(stopsAtRight, "dragging a window off the right should leave a grabbable strip") &&
        Expect(stopsAtTop, "a window's title bar should not go above the screen") &&
        Expect(stopsAtBottom, "a window should stop before its title bar leaves the bottom") &&
        Expect(narrowStaysWhole, "a window narrower than the visible minimum should stay whole");
}

/// <summary>
/// 글자 크기와 줄 높이의 관계를 고정한다.
///
/// 목록의 한 줄은 <c>RowHeight</c>만큼의 자리를 받고, 그 안에 본문 한 줄이 들어가야 한다. 두
/// 값은 따로 적혀 있으므로 한쪽만 움직이면 조용히 어긋난다 — 글자를 키우면 줄에서 넘치고,
/// 줄을 줄이면 아래위가 잘린다. 화면에서는 "글자가 조금 답답하다" 정도로만 보여서, 눈으로는
/// 어느 쪽이 원인인지 짚기 어렵다.
///
/// 보조 글자도 함께 잰다. 그것의 정의는 값이 아니라 본문과의 <b>간격</b>이므로, 간격이 유지되는
/// 한 본문이 들어가면 보조도 들어간다는 것이 여기서 확인된다.
/// </summary>
bool RunEditorRowFontFitTests()
{
    const std::unique_ptr<GameEngine::Platform::ITextRasterizer> rasterizer =
        TestSupport::CreateTestTextRasterizer();
    if (!rasterizer || !rasterizer->Initialize())
    {
        std::cout << "  row font fit tests skipped: no text rasterizer on this machine\n";
        return true;
    }

    // 위아래로 가장 멀리 뻗는 글자들이다: 대문자와 올림자가 위를, 내림자가 아래를 정한다.
    constexpr std::string_view Tall = "Ag|QJjpqy";
    const auto measuredHeight = [&rasterizer](const float fontSize)
    {
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = std::string(Tall);
        request.fontSize = fontSize;
        GameEngine::Platform::TextGlyphLayout layout;
        if (!rasterizer->LayoutText(request, layout))
        {
            return 0.0f;
        }
        return static_cast<float>(layout.height);
    };

    const float bodyHeight = measuredHeight(GameEditor::RowFontSize);
    const float secondaryHeight = measuredHeight(GameEditor::SecondaryFontSize);
    const bool measured = bodyHeight > 0.0f && secondaryHeight > 0.0f;
    const bool bodyFitsTheRow = bodyHeight <= GameEditor::RowHeight;
    const bool secondaryFitsTheRow = secondaryHeight <= GameEditor::RowHeight;
    // 보조는 본문보다 작아야 두 층으로 읽힌다. 간격이 0이 되면 두 계열이 하나로 보인다.
    const bool secondaryStaysSmaller = GameEditor::SecondaryFontSize < GameEditor::RowFontSize;

    if (!measured)
    {
        return Expect(false, "the rasterizer should report a height for the editor's text");
    }
    if (!bodyFitsTheRow)
    {
        std::cerr << "  body text needs " << bodyHeight << "px but a row is "
                  << GameEditor::RowHeight << "px\n";
    }

    // 인스펙터의 이름 칸은 폭이 고정이고, 들어가지 않는 이름은 말줄임으로 잘린다. 넘치는 것이
    // 아니라 잘리는 것이라 화면에서는 조용하다 — "Order In Layer"가 "Order In…"이 되어도
    // 눈에 띄지 않지만, 이름이 정체성인 칸에서 뒤가 사라지는 것은 값을 못 읽는 것과 같다.
    // 이름은 지어내지 않고 컴포넌트의 속성 서술에서 그대로 가져온다.
    //
    // 이 칸은 Body 역할로 그려지므로(UIContext::DrawLabel의 기본값), 재는 것도 그 폰트여야
    // 한다 — 위의 rasterizer는 D2Coding 하나만 등록돼 있고, 그것은 고정폭이라 실제 에디터가
    // 쓰는 NanumSquareNeo보다 훨씬 넓게 잰다. 실제 폰트를 못 구하면 이 칸만 건너뛴다.
    const std::filesystem::path stagedContent =
        GameEngine::Platform::PlatformServices::GetExecutableDirectory().parent_path() /
        "GameEditor";
    std::error_code stagedError;
    if (!std::filesystem::is_directory(stagedContent, stagedError) || stagedError)
    {
        std::cout << "  inspector label width check skipped: GameEditor content is not staged "
                     "beside the tests\n";
        return Expect(bodyFitsTheRow, "a line of body text should fit inside one list row") &&
            Expect(
                secondaryFitsTheRow, "a line of secondary text should fit inside one list row") &&
            Expect(
                secondaryStaysSmaller, "secondary text should stay a step below the body size");
    }
    const GameEngine::Platform::DirectoryContentSource stagedSource(stagedContent);
    std::vector<std::byte> bodyFontBytes;
    constexpr std::string_view BodyFontAlias = "InspectorBody";
    const bool bodyFontReady =
        stagedSource.Read(GameEditor::EditorFonts[1].relativePath, bodyFontBytes) &&
        rasterizer->RegisterFont(BodyFontAlias, bodyFontBytes);
    constexpr float LabelColumnWidth = GameEditor::InspectorLabelWidth;
    const auto measureWidth = [&rasterizer](const std::string_view text)
    {
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = std::string(text);
        request.fontFamily = BodyFontAlias;
        request.fontSize = GameEditor::SecondaryFontSize;
        GameEngine::Platform::TextGlyphLayout layout;
        if (!rasterizer->LayoutText(request, layout))
        {
            return 0.0f;
        }
        return static_cast<float>(layout.width);
    };
    if (!bodyFontReady)
    {
        return Expect(false, "the editor's Body font should load from staged content");
    }
    const std::array<const GameEngine::Runtime::ComponentType*, 6> InspectedTypes = {
        &GameEngine::Runtime::Transform::StaticType(),
        &GameEngine::Runtime::RectTransform::StaticType(),
        &GameEngine::Runtime::SpriteRenderer::StaticType(),
        &GameEngine::Runtime::TextRenderer::StaticType(),
        &GameEngine::Runtime::Camera::StaticType(),
        &GameEngine::Runtime::Light::StaticType(),
    };
    bool everyNameFits = true;
    for (const GameEngine::Runtime::ComponentType* const type : InspectedTypes)
    {
        if (type == nullptr)
        {
            continue;
        }
        for (const GameEngine::Runtime::PropertyDescriptor* const property :
             GameEngine::Runtime::CollectProperties(*type))
        {
            if (property == nullptr)
            {
                continue;
            }
            // 그리는 쪽이 왼쪽에서 들여쓰는 만큼은 이름이 쓸 수 없다.
            const float available = LabelColumnWidth - 6.0f;
            const float needed = measureWidth(property->GetDisplayName());
            if (needed > available)
            {
                std::cerr << "  inspector property name is cut: \"" << property->GetDisplayName()
                          << "\" needs " << needed << "px but the column gives " << available
                          << "px\n";
                everyNameFits = false;
            }
        }
    }

    return Expect(bodyFitsTheRow, "a line of body text should fit inside one list row") &&
        Expect(secondaryFitsTheRow, "a line of secondary text should fit inside one list row") &&
        Expect(secondaryStaysSmaller, "secondary text should stay a step below the body size") &&
        Expect(
            everyNameFits,
            "every inspector property name should fit its label column without being cut");
}

/// <summary>
/// 툴바 글자가 각 버튼 안에 들어가는지 확인한다.
/// 같은 글자와 크기도 사용하는 폰트에 따라 폭이 달라지므로 실제 측정 결과로 검사한다.
/// </summary>

/// <summary>
/// 시작 장면을 열지 못해도 프로젝트는 열린 채로 남고 오류와 문제 경로를 사용자에게 전달해야 한다.
/// 등록된 장면 파일이 없는 프로젝트는 편집기로 복구할 수 있도록 열기를 허용한다.
/// 프로젝트 밖을 가리키는 장면 등록은 거절해야 하며 파일 누락 허용이 경로 탈출 허용으로 이어지면 안 된다.
/// </summary>
bool RunEditorMissingSceneFileTests()
{
    bool passed = true;

    // ─ 파일이 없다: 프로젝트는 열리고, 그 장면은 등록에 남는다 ─
    {
        TemporaryDirectory temporaryDirectory("editor-missing-scene-file");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile = root / "GoneFile.gameproject";
        // 시작 장면은 있고, 두 번째 장면은 등록만 있고 파일이 없다 — 사람이 탐색기에서
        // 지웠을 때의 모습이다.
        const bool wrote = WriteFile(projectFile,
            R"({"projectName": "GoneFile",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [)"
            R"( { "id": 0, "path": "Scenes/Main.scene" },)"
            R"( { "id": 1, "path": "Scenes/Gone.scene" } ]})") &&
            WriteFile(root / "Scenes" / "Main.scene",
                R"({ "sceneName": "Main", "gameObjects": [] })");
        if (!Expect(wrote, "the missing-file test project should be written"))
        {
            return false;
        }

        GameEngine::Diagnostics::Debug::ClearRecentLogs();
        GameEditor::EditorContext context;
        passed &= Expect(
            context.OpenProject(projectFile),
            "a project should open when one of its scene files is missing");

        // 계층이 그 행을 그리려면 등록이 남아 있어야 한다. 없는 것으로 지워 버리면 사람이
        // 그것을 보지도, 에디터 안에서 지우지도 못한다.
        const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
        passed &= Expect(
            project && project->settings.scenePaths.contains(1),
            "the scene with no file should stay registered so it can be shown and repaired");
        passed &= Expect(
            project && !GameEngine::App::ProjectFile::SceneFileExists(*project, 1),
            "that scene should report that its file is missing");

        // 그리고 그 사실이 사람이 읽는 곳에 남는다. 경로가 있어야 무엇을 고칠지 안다.
        bool reported = false;
        for (const GameEngine::Diagnostics::LogEntry& entry :
             GameEngine::Diagnostics::Debug::GetRecentLogs())
        {
            if (entry.message.find("Gone.scene") != std::string::npos &&
                entry.message.find("missing") != std::string::npos)
            {
                reported = true;
            }
        }
        passed &= Expect(
            reported,
            "opening a project with a missing scene file should say so and name the path");
    }

    // ─ 프로젝트 밖을 가리킨다: 여전히 거부한다 ─
    {
        TemporaryDirectory temporaryDirectory("editor-escaping-scene");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile = root / "Escaping.gameproject";
        // 등록이 프로젝트 밖을 가리킨다. 그 파일을 실제로 만들어 둔다 — 존재 여부가 아니라
        // 경로가 거부 사유라는 것을 재기 위해서다.
        const bool wrote = WriteFile(projectFile,
            R"({"projectName": "Escaping",)"
            R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [)"
            R"( { "id": 0, "path": "Scenes/Main.scene" },)"
            R"( { "id": 1, "path": "../Outside.scene" } ]})") &&
            WriteFile(root / "Scenes" / "Main.scene",
                R"({ "sceneName": "Main", "gameObjects": [] })") &&
            WriteFile(root.parent_path() / "Outside.scene",
                R"({ "sceneName": "Outside", "gameObjects": [] })");
        if (!Expect(wrote, "the escaping-path test project should be written"))
        {
            return false;
        }

        GameEditor::EditorContext context;
        passed &= Expect(
            !context.OpenProject(projectFile),
            "a project whose scene path leaves the project folder should still be refused");
    }

    return passed;
}

bool RunEditorMissingSceneReportTests()
{
    TemporaryDirectory temporaryDirectory("editor-missing-scene");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    // 파일은 있는데 읽어 들일 수 없는 장면이다. 파일이 아예 없는 경우도 이제 프로젝트는
    // 열리고 그 장면만 못 열며, 그쪽은 RunEditorMissingSceneFileTests가 본다 — 여기서 보는
    // 것은 파일이 있는데 내용이 장면이 아닌 경우다.
    const std::filesystem::path projectFile = root / "MissingScene.gameproject";
    const bool wrote = WriteFile(projectFile,
        R"({"projectName": "MissingScene",)"
        R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
        R"( "initialSceneId": 0, "scenes": [)"
        R"( { "id": 0, "path": "Scenes/Gone.scene" } ]})") &&
        // 확장자는 장면이고 파일도 있지만, 내용이 장면이 아니다.
        WriteFile(root / "Scenes" / "Gone.scene", "{ this is not a scene");
    if (!Expect(wrote, "the missing-scene test project should be written"))
    {
        return false;
    }

    GameEngine::Diagnostics::Debug::ClearRecentLogs();

    GameEditor::EditorContext context;
    bool passed = Expect(
        context.OpenProject(projectFile),
        "the project should still open when its initial scene cannot be read");
    passed &= Expect(
        !context.HasOpenScene(),
        "no scene should be open when the initial scene could not be read");

    // 사람이 읽는 곳에 남았는가. 그리고 무엇을 고쳐야 할지 알 수 있게 경로가 있는가.
    bool reported = false;
    bool namesThePath = false;
    for (const GameEngine::Diagnostics::LogEntry& entry :
         GameEngine::Diagnostics::Debug::GetRecentLogs())
    {
        if (entry.level != GameEngine::Diagnostics::LogLevel::Error)
        {
            continue;
        }
        if (entry.message.find("no scene is open") != std::string::npos)
        {
            reported = true;
            namesThePath = entry.message.find("Gone.scene") != std::string::npos;
        }
    }
    passed &= Expect(
        reported,
        "opening a project whose initial scene is missing should say that no scene is open");
    passed &= Expect(
        namesThePath,
        "that message should name the scene path, so the person knows what to repair");

    return passed;
}

bool RunEditorAssetWatchTests()
{
    TemporaryDirectory temporaryDirectory("editor-asset-watch");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteTestProject(root);
    if (projectFile.empty())
    {
        return Expect(false, "the asset watch test project should be written");
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the test project should open"))
    {
        return false;
    }
    const GameEngine::Assets::AssetDatabase* const database = context.GetProjectAssetDatabase();
    if (!Expect(database != nullptr, "an open project should have an asset database"))
    {
        return false;
    }
    const std::size_t assetsAtOpen = database->GetAssets().size();
    const unsigned int revisionAtOpen = context.GetProjectRevision();

    // 조용한 프레임은 아무것도 하지 않는다. 폴링이 매 프레임 다시 스캔하면 개정이 계속 오른다.
    for (int frame = 0; frame < 5; ++frame)
    {
        context.PollProjectAssetChanges();
    }
    const bool quietChangesNothing = context.GetProjectRevision() == revisionAtOpen &&
        database->GetAssets().size() == assetsAtOpen;

    // 프레임을 흉내 내며 개정이 오를 때까지 기다린다. 알림은 비동기이고 그 뒤에 조용한 시간이
    // 더 필요하므로, 폴링과 짧은 잠을 번갈아 돈다.
    const auto pumpUntilRevisionChanges = [&context](const unsigned int previous)
    {
        for (int attempt = 0; attempt < 300; ++attempt)
        {
            context.PollProjectAssetChanges();
            if (context.GetProjectRevision() != previous)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };

    // 새 파일이 데이터베이스에 나타난다.
    const bool wroteTexture = WriteFile(root / "Textures" / "Added.png", "png-bytes");
    const bool sawAddition = wroteTexture && pumpUntilRevisionChanges(revisionAtOpen);
    const GameEngine::Assets::AssetDatabase* const afterAddition =
        context.GetProjectAssetDatabase();
    const bool registeredAddition = sawAddition && afterAddition != nullptr &&
        afterAddition->FindAsset("Textures/Added.png") != nullptr &&
        afterAddition->GetAssets().size() == assetsAtOpen + 1;

    // 지운 파일은 데이터베이스에서도 사라진다.
    const unsigned int revisionAfterAddition = context.GetProjectRevision();
    std::error_code removeError;
    const bool removed = std::filesystem::remove(root / "Textures" / "Added.png", removeError);
    const bool sawRemoval = removed && pumpUntilRevisionChanges(revisionAfterAddition);
    const GameEngine::Assets::AssetDatabase* const afterRemoval = context.GetProjectAssetDatabase();
    const bool unregisteredRemoval = sawRemoval && afterRemoval != nullptr &&
        afterRemoval->FindAsset("Textures/Added.png") == nullptr &&
        afterRemoval->GetAssets().size() == assetsAtOpen;

    // 스캔이 실패해도 열려 있던 데이터베이스는 살아남는다. 두 번째 .gameproject가 그 조건이며,
    // 실패한 스캔을 옮겼다면 여기서 에셋이 0개가 된다. 아래 줄은 의도된 오류 로그를 남긴다.
    const std::size_t assetsBeforeFailure = afterRemoval ? afterRemoval->GetAssets().size() : 0;
    const unsigned int revisionBeforeFailure = context.GetProjectRevision();
    const bool wroteSecondProject = WriteFile(root / "Second.gameproject", "{}");
    bool survivedFailure = wroteSecondProject;
    if (survivedFailure)
    {
        survivedFailure = !context.RefreshProjectAssets();
        const GameEngine::Assets::AssetDatabase* const afterFailure =
            context.GetProjectAssetDatabase();
        survivedFailure = survivedFailure && afterFailure != nullptr &&
            afterFailure->GetAssets().size() == assetsBeforeFailure &&
            context.GetProjectRevision() == revisionBeforeFailure;
    }

    return Expect(quietChangesNothing, "a quiet frame should not rescan the project") &&
        Expect(registeredAddition, "a file added under the root should reach the database") &&
        Expect(unregisteredRemoval, "a deleted file should leave the database") &&
        Expect(
            survivedFailure,
            "a failed rescan should leave the open database and revision untouched");
}

bool RunSidecarWriterTests()
{
    TemporaryDirectory temporaryDirectory("sidecar-writer");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteTestProject(root);
    // 넷을 둔다: 사이드카가 없는 것, 정체성을 이미 가진 것, 설정은 있지만 정체성이 없는 것,
    // 옛 이름의 사이드카를 가진 것.
    const std::string keptGuid = "aaaabbbbccccddddeeeeffff00001111";
    const std::string kept = R"({"guid":")" + keptGuid + R"(","pixelsPerUnit":7.0})";
    if (projectFile.empty() || !WriteFile(root / "Bare.png", "png-data") ||
        !WriteFile(root / "Kept.png", "png-data") ||
        !WriteFile(root / "Kept.png.meta", kept) ||
        !WriteFile(root / "Settings.png", "png-data") ||
        !WriteFile(root / "Settings.png.meta", R"({"pixelsPerUnit": 33.0})") ||
        !WriteFile(root / "Old.png", "png-data") ||
        !WriteFile(root / "Old.png.sprite.json", R"({"pixelsPerUnit": 9.0})"))
    {
        return Expect(false, "the sidecar writer test project should be written");
    }

    GameEditor::EditorContext context;
    const bool opened = context.OpenProject(projectFile);
    if (!Expect(opened, "the sidecar writer test project should open"))
    {
        return false;
    }

    const auto readFile = [](const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    };
    const auto countMetaFiles = [&root]()
    {
        std::size_t count = 0;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(root, error);
             !error && iterator != std::filesystem::recursive_directory_iterator();
             iterator.increment(error))
        {
            if (iterator->path().extension() == ".meta")
            {
                ++count;
            }
        }
        return count;
    };

    // 없던 것에는 사이드카가 놓인다. 판본과 정체성을 담고, 형식의 기본값도 함께.
    const bool wroteMissing = std::filesystem::exists(root / "Bare.png.meta");
    const std::string bare = wroteMissing ? readFile(root / "Bare.png.meta") : std::string{};
    const bool bareCarriesBoth = bare.find("gameengine-meta/1") != std::string::npos &&
        bare.find("guid") != std::string::npos &&
        bare.find("pixelsPerUnit") != std::string::npos;

    // 🔴 이미 정체성이 있는 것은 바이트가 그대로다. 다시 발급하면 그것을 가리키던 참조가 전부
    // 아무것도 가리키지 않게 된다.
    const bool keptUntouched = readFile(root / "Kept.png.meta") == kept;

    // 🔴 설정만 있고 정체성이 없던 것은 그 파일에 정체성이 더해지고, 적어 둔 값은 남는다. 새
    // 파일을 만들면 한 에셋에 설정이 둘이 되고 사람이 적은 값은 새 파일에 없다.
    const std::string settings = readFile(root / "Settings.png.meta");
    const bool gainedIdentityKeepingSettings = settings.find("guid") != std::string::npos &&
        settings.find("33") != std::string::npos;

    // 🔴 옛 이름의 사이드카도 그 파일에 정체성을 받는다. 새 이름의 파일이 생기지는 않는다.
    const std::string old = readFile(root / "Old.png.sprite.json");
    const bool legacyGainedIdentityInPlace = old.find("guid") != std::string::npos &&
        old.find("9") != std::string::npos &&
        !std::filesystem::exists(root / "Old.png.meta");

    // 발급된 정체성은 서로 다르다. 같은 값을 둘에게 주면 참조가 엉뚱한 것으로 풀린다.
    const GameEngine::Assets::AssetDatabase* const database = context.GetProjectAssetDatabase();
    std::unordered_set<std::string> identities;
    bool everyAssetIdentified = database != nullptr;
    if (database)
    {
        for (const auto& asset : database->GetAssets())
        {
            everyAssetIdentified = everyAssetIdentified && asset->GetGuid().IsValid() &&
                identities.insert(asset->GetGuid().ToString()).second;
        }
    }

    // 두 번째 스캔은 아무것도 쓰지 않는다. 감시자가 방금 만든 파일을 보고 다시 부르므로, 여기서
    // 멈추지 않으면 스캔과 쓰기가 서로를 부른다.
    //
    // 재는 것은 파일이지 로그가 아니다. 이 프로세스는 메시지를 켜지 않아 Release에서는 일반 로그는
    // 그 구성에서 컴파일되어 사라지고, 로그를 세는 시험은 거기서 아무것도 재지 못한다.
    const std::size_t metaFilesAfterOpen = countMetaFiles();
    const std::string bareBeforeRescan = readFile(root / "Bare.png.meta");
    const bool rescanned = context.RefreshProjectAssets();
    const bool secondWroteNothing = rescanned && countMetaFiles() == metaFilesAfterOpen &&
        readFile(root / "Bare.png.meta") == bareBeforeRescan &&
        readFile(root / "Settings.png.meta") == settings;

    return Expect(wroteMissing && bareCarriesBoth, "an asset with no sidecar should get one") &&
        Expect(keptUntouched, "an identity already recorded should be left byte for byte") &&
        Expect(
            gainedIdentityKeepingSettings,
            "a sidecar holding settings but no identity should gain one and keep them") &&
        Expect(
            legacyGainedIdentityInPlace,
            "a legacy sidecar should gain its identity in place, not in a second file") &&
        Expect(everyAssetIdentified, "every asset should end up with a distinct identity") &&
        Expect(secondWroteNothing, "a second scan should write nothing");
}

bool RunAssetMoveTests()
{
    using GameEngine::Core::Guid;

    TemporaryDirectory temporaryDirectory("asset-move");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const std::filesystem::path projectFile = WriteTestProject(root);
    const auto sidecar = [](const std::string& guid)
    {
        return R"({"format":"gameengine-meta/1","guid":")" + guid + R"("})";
    };
    // 일곱을 둔다: 옮겨질 것, 이름만 바뀔 것, 사이드카까지 함께 옮겨질 것, 내용이 같은 쌍둥이
    // 둘, 받을 자리가 이미 차 있을 것, 이름도 폴더도 달라질 것. 내용은 무리마다 다르게 둔다 —
    // 짝짓기가 내용으로 이루어지므로 우연히 같으면 시험이 다른 것을 재게 된다.
    const std::string movedGuid = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";
    const std::string renamedGuid = "1a2b3c4d5e6f708192a3b4c5d6e7f809";
    const std::string togetherGuid = "2b3c4d5e6f708192a3b4c5d6e7f8091a";
    const std::string twinAGuid = "3c4d5e6f708192a3b4c5d6e7f8091a2b";
    const std::string twinBGuid = "4d5e6f708192a3b4c5d6e7f8091a2b3c";
    const std::string occupiedGuid = "5e6f708192a3b4c5d6e7f8091a2b3c4d";
    const std::string aloneGuid = "6f708192a3b4c5d6e7f8091a2b3c4d5e";
    if (projectFile.empty() || !WriteFile(root / "Moved.png", "moved-bytes") ||
        !WriteFile(root / "Moved.png.meta", sidecar(movedGuid)) ||
        !WriteFile(root / "Renamed.png", "renamed-bytes") ||
        !WriteFile(root / "Renamed.png.meta", sidecar(renamedGuid)) ||
        !WriteFile(root / "Together.png", "together-bytes") ||
        !WriteFile(root / "Together.png.meta", sidecar(togetherGuid)) ||
        !WriteFile(root / "TwinA.png", "twin-bytes") ||
        !WriteFile(root / "TwinA.png.meta", sidecar(twinAGuid)) ||
        !WriteFile(root / "TwinB.png", "twin-bytes") ||
        !WriteFile(root / "TwinB.png.meta", sidecar(twinBGuid)) ||
        !WriteFile(root / "Occupied.png", "occupied-bytes") ||
        !WriteFile(root / "Occupied.png.meta", sidecar(occupiedGuid)) ||
        !WriteFile(root / "Old" / "Alone.png", "alone-bytes") ||
        !WriteFile(root / "Old" / "Alone.png.meta", sidecar(aloneGuid)))
    {
        return Expect(false, "the asset move test project should be written");
    }

    GameEditor::EditorContext context;
    if (!Expect(context.OpenProject(projectFile), "the asset move test project should open"))
    {
        return false;
    }
    const GameEngine::Assets::AssetDatabase* database = context.GetProjectAssetDatabase();
    if (!Expect(database != nullptr, "an open project should have an asset database"))
    {
        return false;
    }
    const GameEngine::Assets::Asset* const beforeMove = database->FindAsset("Moved.png");
    const GameEngine::Assets::AssetKey idBeforeMove = beforeMove ? beforeMove->GetId() : 0;

    // 사람이 탐색기에서 할 법한 일을 한 번에 벌인다. 감시자를 기다리는 대신 직접 다시 읽는 것은,
    // 재는 것이 알림의 타이밍이 아니라 다시 읽을 때의 판정이기 때문이다.
    std::error_code error;
    std::filesystem::create_directories(root / "Sub", error);
    std::filesystem::create_directories(root / "New", error);
    std::filesystem::rename(root / "Moved.png", root / "Sub" / "Moved.png", error);
    std::filesystem::rename(root / "Renamed.png", root / "Renamed2.png", error);
    std::filesystem::rename(root / "Together.png", root / "Sub" / "Together.png", error);
    std::filesystem::rename(
        root / "Together.png.meta", root / "Sub" / "Together.png.meta", error);
    std::filesystem::rename(root / "TwinA.png", root / "Sub" / "TwinA.png", error);
    std::filesystem::rename(root / "TwinB.png", root / "Sub" / "TwinB.png", error);
    std::filesystem::rename(root / "Occupied.png", root / "Sub" / "Occupied.png", error);
    const bool wroteOccupant =
        WriteFile(root / "Sub" / "Occupied.png.meta", R"({"pixelsPerUnit": 5.0})");
    std::filesystem::rename(root / "Old" / "Alone.png", root / "New" / "Different.png", error);
    if (!Expect(!error && wroteOccupant, "the asset move test files should be rearranged"))
    {
        return false;
    }

    const bool rescanned = context.RefreshProjectAssets();
    database = context.GetProjectAssetDatabase();
    if (!Expect(rescanned && database != nullptr, "the rearranged project should be re-read"))
    {
        return false;
    }
    const auto identityAt = [database](const std::filesystem::path& path)
    {
        const GameEngine::Assets::Asset* const asset = database->FindAsset(path);
        return asset ? asset->GetGuid().ToString() : std::string{};
    };

    // 🔴 파일만 옮기면 사이드카가 따라가고, 정체성도 조회 키도 그대로다. 이것이 이 일감 전부의
    // 목적이다 — 따라가지 않으면 새 자리가 새 정체성을 받고 그것을 가리키던 참조가 전부 끊긴다.
    const GameEngine::Assets::Asset* const afterMove = database->FindAsset("Sub/Moved.png");
    const bool sidecarFollowedTheFile = afterMove && afterMove->GetGuid().ToString() == movedGuid &&
        afterMove->GetId() == idBeforeMove && std::filesystem::exists(root / "Sub" / "Moved.png.meta") &&
        !std::filesystem::exists(root / "Moved.png.meta");

    // 이름만 바꾼 것도 같은 판정이다. 폴더가 같으므로 이동으로 본다.
    const bool sidecarFollowedTheName = identityAt("Renamed2.png") == renamedGuid &&
        std::filesystem::exists(root / "Renamed2.png.meta") &&
        !std::filesystem::exists(root / "Renamed.png.meta");

    // 사이드카까지 함께 옮긴 것은 애초에 옮길 것이 없다. 아무 일도 일어나지 않아야 한다.
    const bool movedTogetherIsUntouched = identityAt("Sub/Together.png") == togetherGuid &&
        std::filesystem::exists(root / "Sub" / "Together.png.meta");

    // 🔴 내용이 같은 둘은 어느 것이 어느 것이 되었는지 말할 수 없다. 찍어서 옮기면 참조가 조용히
    // 다른 그림을 가리키게 되므로, 옮기지 않고 옛 자리에 둔 채 새 정체성을 발급한다.
    const std::string twinA = identityAt("Sub/TwinA.png");
    const std::string twinB = identityAt("Sub/TwinB.png");
    const bool twinsWereLeftAlone = !twinA.empty() && !twinB.empty() && twinA != twinB &&
        twinA != twinAGuid && twinA != twinBGuid && twinB != twinAGuid && twinB != twinBGuid &&
        std::filesystem::exists(root / "TwinA.png.meta") &&
        std::filesystem::exists(root / "TwinB.png.meta");

    // 받을 자리가 이미 자기 설정을 갖고 있으면 덮지 않는다. 사람이 적어 둔 값이 그 안에 있다.
    const std::string occupant = identityAt("Sub/Occupied.png");
    std::ifstream occupantStream(root / "Sub" / "Occupied.png.meta", std::ios::binary);
    const std::string occupantText{
        std::istreambuf_iterator<char>(occupantStream), std::istreambuf_iterator<char>() };
    const bool occupiedDestinationRefused = !occupant.empty() && occupant != occupiedGuid &&
        occupantText.find('5') != std::string::npos &&
        std::filesystem::exists(root / "Occupied.png.meta");

    // 🔴 ⑤의 조건: 이름도 폴더도 둘 다 달라지면 옮긴 것이라 볼 근거가 없다. 지우고 다른 것을
    // 넣은 경우와 구별할 방법이 없으므로 옮기지 않는다.
    const bool unrelatedLocationRefused = identityAt("New/Different.png") != aloneGuid &&
        !identityAt("New/Different.png").empty() &&
        std::filesystem::exists(root / "Old" / "Alone.png.meta");

    return Expect(
               sidecarFollowedTheFile,
               "moving a file alone should carry its metadata, identity and lookup key with it") &&
        Expect(sidecarFollowedTheName, "renaming a file in place should carry its metadata") &&
        Expect(movedTogetherIsUntouched, "a file moved with its metadata should keep its identity") &&
        Expect(
            twinsWereLeftAlone,
            "two files with the same contents should not have their metadata guessed at") &&
        Expect(
            occupiedDestinationRefused,
            "a destination that already carries metadata should keep its own") &&
        Expect(
            unrelatedLocationRefused,
            "a file whose name and folder both changed should not be treated as a move");
}

namespace
{
    /// <summary>
    /// 스프라이트 하나를 경로로 가리키는 장면 하나짜리 프로젝트다. 이관이 재는 것이 바로 그
    /// 참조이므로, 픽스처가 가진 것도 그것 하나다.
    /// </summary>
    [[nodiscard]] std::filesystem::path WriteMigrationProject(
        const std::filesystem::path& root, const std::string& spriteGuid,
        const std::string& spriteReference, const std::string& extraComponent = {})
    {
        const std::filesystem::path projectFile = root / "MigrationTest.gameproject";
        const std::string scene =
            R"({"sceneName": "First", "gameObjects": [)"
            R"( {"id": 1, "name": "Sprite", "isActive": true, "components": [)"
            R"( {"type": "Transform"},)"
            R"( {"type": "SpriteRenderer", "sprite": ")" + spriteReference + R"("})" +
            extraComponent + R"( ]} ]})";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "MigrationTest",)"
                R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [ { "id": 0, "path": "Scenes/First.scene" } ]})") &&
            WriteFile(root / "Scenes" / "First.scene", scene) &&
            WriteFile(root / "Textures" / "Albedo.png", "albedo-bytes") &&
            WriteFile(root / "Textures" / "Albedo.png.meta",
                R"({"format":"gameengine-meta/1","guid":")" + spriteGuid + R"("})");
        return wrote ? projectFile : std::filesystem::path{};
    }

    /// <summary>이 디렉터리 아래에 이관 백업이 몇 개 있는지다.</summary>
    [[nodiscard]] std::size_t CountBackups(const std::filesystem::path& root)
    {
        std::size_t count = 0;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(root, error);
             !error && iterator != std::filesystem::recursive_directory_iterator();
             iterator.increment(error))
        {
            if (iterator->path().extension().string().starts_with(".bak-"))
            {
                ++count;
            }
        }
        return count;
    }
}

bool RunSceneReferenceMigrationTests()
{
    namespace Assets = GameEngine::Assets;
    using GameEngine::Core::Guid;

    // 엔진 컴포넌트의 팩토리를 먼저 붙인다. 등록되지 않은 프로세스에서는 Transform조차
    // 보존 컴포넌트로 실려 오고, 그러면 속성을 볼 수 없어 참조도 보이지 않는다 — 이관이
    // 아무것도 찾지 못하는 것이 기능의 답이 아니라 이 프로세스의 상태임을 여기서 가른다.
    // 또 불러도 무해하고, 이미 등록되어 있으면 false다 — 재는 것은 대답이 아니라 끝난 뒤의
    // 상태이므로, 한 타입이 실제로 등록되어 있는지를 묻는다.
    static_cast<void>(GameEngine::Serialization::RegisterRuntimeComponentFactories());
    if (!Expect(
            GameEngine::Serialization::ComponentFactory::IsRegistered("SpriteRenderer"),
            "the engine component factories should be registered"))
    {
        return false;
    }

    const std::string spriteGuid = "5a4b3c2d1e0f9a8b7c6d5e4f3a2b1c0d";
    const Assets::AssetReference migrated = Assets::AssetReference::Parse(spriteGuid);

    // ---- 거부하면 아무것도 바뀌지 않는다.
    {
        TemporaryDirectory temporaryDirectory("scene-migration-refuse");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile =
            WriteMigrationProject(root, spriteGuid, "Textures/Albedo.png");
        if (projectFile.empty())
        {
            return Expect(false, "the migration test project should be written");
        }
        const std::string before = TestSupport::ReadFile(root / "Scenes" / "First.scene");

        GameEditor::EditorContext context;
        if (!Expect(context.OpenProject(projectFile), "the migration test project should open"))
        {
            return false;
        }
        const GameEditor::SceneMigrationPlan* const plan = context.GetSceneMigrationPlan();
        const bool asks = plan && plan->convertible == 1 && !plan->IsBlocked() &&
            plan->scenes.size() == 1 &&
            plan->scenes.front().scenePath == std::filesystem::path("Scenes/First.scene");
        const bool notYetMigrated = !context.AreSceneReferencesMigrated();
        // 질문 줄은 몇 개가 어느 장면에서 바뀌는지 말한다. 사람이 읽는 것이 이 한 줄뿐이다.
        const bool describesTheWork = plan && plan->Describe().find("1") != std::string::npos &&
            plan->Describe().find("First.scene") != std::string::npos;

        context.DismissSceneMigration();
        const bool nothingChanged = TestSupport::ReadFile(root / "Scenes" / "First.scene") == before &&
            CountBackups(root) == 0 && context.GetSceneMigrationPlan() == nullptr;

        if (!Expect(asks, "a project pointing at assets by path should ask to migrate") ||
            !Expect(describesTheWork, "the question should name the scene and the count") ||
            !Expect(notYetMigrated, "a project that has not migrated should say so") ||
            !Expect(nothingChanged, "refusing should leave every scene file byte for byte"))
        {
            return false;
        }
    }

    // ---- 승인하면 참조가 정체성이 되고, 같은 에셋으로 풀린다.
    {
        TemporaryDirectory temporaryDirectory("scene-migration-apply");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile =
            WriteMigrationProject(root, spriteGuid, "Textures/Albedo.png");
        GameEditor::EditorContext context;
        if (projectFile.empty() || !Expect(context.OpenProject(projectFile), "it should open"))
        {
            return false;
        }
        const Assets::AssetDatabase* database = context.GetProjectAssetDatabase();
        const Assets::Asset* const before =
            database ? database->FindAsset("Textures/Albedo.png") : nullptr;
        const Assets::AssetKey idBefore = before ? before->GetId() : 0;

        const bool applied = context.ApplySceneMigration();
        const std::string text = TestSupport::ReadFile(root / "Scenes" / "First.scene");
        // 🔴 파일이 정체성을 담고 경로는 사라진다. 이것이 이관의 정의다.
        const bool fileHoldsTheIdentity = text.find(spriteGuid) != std::string::npos &&
            text.find("Textures/Albedo.png") == std::string::npos;
        // 🔴 그리고 그 참조는 여전히 같은 에셋으로 풀린다. 형식을 바꾸는 것이 가리키는 대상을
        // 바꾸면 이관은 손실이지 이관이 아니다.
        database = context.GetProjectAssetDatabase();
        const Assets::Asset* const after = database ? database->FindAsset(migrated) : nullptr;
        const bool resolvesToTheSameAsset = after && before && after->GetId() == idBefore;
        const bool backedUp = CountBackups(root) == 1;
        const bool nowMigrated = context.AreSceneReferencesMigrated() &&
            context.GetSceneMigrationPlan() == nullptr;

        // 🔴 이관된 프로젝트에서 새로 고르는 참조는 정체성으로 적힌다. 안 그러면 방금 정리한
        // 장면에 다음 클릭이 경로를 하나 섞어 넣는다.
        const std::vector<Assets::AssetChoice> identityChoices = database
            ? Assets::CollectAssetChoices(
                  *database, Assets::AssetType::Sprite, Assets::AssetReferenceForm::Identity)
            : std::vector<Assets::AssetChoice>{};
        const std::vector<Assets::AssetChoice> pathChoices = database
            ? Assets::CollectAssetChoices(
                  *database, Assets::AssetType::Sprite, Assets::AssetReferenceForm::Path)
            : std::vector<Assets::AssetChoice>{};
        const bool formFollowsTheProject = !identityChoices.empty() && !pathChoices.empty() &&
            identityChoices.front().reference.IsGuidReference() &&
            !pathChoices.front().reference.IsGuidReference() &&
            // 목록에 보이는 이름은 두 형식에서 같다. 사람이 읽는 것은 저장 형식이 아니다.
            identityChoices.front().label == pathChoices.front().label;

        // GUID 참조로 이관한 뒤에는 에셋을 옮겨도 참조가 같은 에셋으로 풀린다.
        std::error_code error;
        std::filesystem::create_directories(root / "Textures" / "Deep", error);
        std::filesystem::rename(
            root / "Textures" / "Albedo.png", root / "Textures" / "Deep" / "Albedo.png", error);
        const bool rescanned = !error && context.RefreshProjectAssets();
        database = context.GetProjectAssetDatabase();
        const Assets::Asset* const afterMove =
            rescanned && database ? database->FindAsset(migrated) : nullptr;
        const bool survivesAMove = afterMove &&
            afterMove->GetRelativePath() == std::filesystem::path("Textures/Deep/Albedo.png");

        if (!Expect(
                applied && fileHoldsTheIdentity,
                "approving should rewrite the reference as an identity") ||
            !Expect(
                resolvesToTheSameAsset,
                "the migrated reference should resolve to the same asset") ||
            !Expect(backedUp, "each rewritten scene should leave one backup beside it") ||
            !Expect(nowMigrated, "a migrated project should say so and stop asking") ||
            !Expect(
                formFollowsTheProject,
                "a new reference should be written in the project's form") ||
            !Expect(survivesAMove, "after migrating, moving the asset should keep the reference"))
        {
            return false;
        }
    }

    // ---- 가리키는 것이 없는 참조는 막지 않고 경로로 남는다.
    {
        TemporaryDirectory temporaryDirectory("scene-migration-unresolved");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile =
            WriteMigrationProject(root, spriteGuid, "Textures/Missing.png");
        GameEditor::EditorContext context;
        if (projectFile.empty() || !Expect(context.OpenProject(projectFile), "it should open"))
        {
            return false;
        }
        // 바꿀 것이 없으므로 묻지 않는다. 「경로 참조가 남아 있다」와 「바꿀 것이 남아 있다」는
        // 다른 사실이고, 사람에게 물을 것은 뒤엣것뿐이다.
        const bool doesNotAsk = context.GetSceneMigrationPlan() == nullptr;
        const bool countsAsMigrated = context.AreSceneReferencesMigrated();
        const std::string text = TestSupport::ReadFile(root / "Scenes" / "First.scene");
        const bool stillAPath = text.find("Textures/Missing.png") != std::string::npos;
        if (!Expect(doesNotAsk, "a reference pointing at nothing should not raise the question") ||
            !Expect(
                countsAsMigrated,
                "a project with nothing left to convert counts as migrated") ||
            !Expect(stillAPath, "a reference pointing at nothing should stay a path"))
        {
            return false;
        }
    }

    // ---- 스키마가 있는 게임 컴포넌트는 그 안의 참조까지 이관된다.
    //
    // 편집 모드에서 게임 컴포넌트는 스키마가 있든 없든 보존 JSON으로 실려 온다 — 그 타입의
    // 팩토리는 게임 실행 파일 안에만 있기 때문이다. 그래서 「어느 속성이 참조인가」를 답하는
    // 것은 스키마뿐이고, 이관은 그것을 눈으로 삼는다.
    {
        TemporaryDirectory temporaryDirectory("scene-migration-schema");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile = WriteMigrationProject(
            root, spriteGuid, "Textures/Albedo.png",
            R"(, {"type": "IconBehaviour", "icon": "Textures/Albedo.png"})");
        const bool wroteSchema = !projectFile.empty() &&
            WriteFile(root / "Components.schema.json",
                R"({"schemaVersion": 1, "components": [ { "type": "IconBehaviour",)"
                R"( "creatable": true, "properties": [)"
                R"( {"name": "icon", "displayName": "Icon", "kind": "asset",)"
                R"( "assetType": "Sprite"} ] } ]})");
        if (!Expect(wroteSchema, "the schema test project should be written"))
        {
            return false;
        }

        GameEditor::EditorContext context;
        if (!Expect(context.OpenProject(projectFile), "it should open"))
        {
            return false;
        }
        // 🔴 스키마가 답해 주므로 막지 않고, 그 안의 참조도 세어 둘이 된다.
        const GameEditor::SceneMigrationPlan* const plan = context.GetSceneMigrationPlan();
        const bool seesInside = plan && !plan->IsBlocked() && plan->opaqueComponents == 0 &&
            plan->convertible == 2;
        const bool applied = seesInside && context.ApplySceneMigration();
        const std::string text = TestSupport::ReadFile(root / "Scenes" / "First.scene");
        // 🔴 그리고 보존된 컴포넌트가 쥐고 있던 그 열쇠도 정체성으로 바뀐다. 서술된 컴포넌트만
        // 바뀌면 그 장면은 「완료」로 표시된 채 게임 컴포넌트의 참조만 조용히 남는다.
        const std::size_t identities = [&text, &spriteGuid]
        {
            std::size_t count = 0;
            for (std::size_t at = text.find(spriteGuid); at != std::string::npos;
                 at = text.find(spriteGuid, at + 1))
            {
                ++count;
            }
            return count;
        }();
        const bool bothRewritten = applied && identities == 2 &&
            text.find("Textures/Albedo.png") == std::string::npos &&
            // 스키마가 참조라 하지 않은 값은 손대지 않는다.
            text.find(R"("type": "IconBehaviour")") != std::string::npos;

        if (!Expect(
                seesInside,
                "a schema should let the migration see inside a preserved component") ||
            !Expect(
                bothRewritten,
                "a reference inside a preserved component should migrate with the rest"))
        {
            return false;
        }
    }

    // ---- 들여다볼 수 없는 컴포넌트가 있으면 막힌다.
    //
    // 그 안의 경로 참조는 남는 것이 아니라 보이지 않는 것이라, 막지 않으면 이관은 끝났다고
    // 말하고 그 참조들은 나중에 그 에셋을 옮기는 날 조용히 끊긴다.
    {
        TemporaryDirectory temporaryDirectory("scene-migration-opaque");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile = WriteMigrationProject(
            root, spriteGuid, "Textures/Albedo.png",
            R"(, {"type": "NoSchemaBehaviour", "velocity": [1, 1]})");
        if (projectFile.empty())
        {
            return Expect(false, "the opaque component project should be written");
        }
        const std::string before = TestSupport::ReadFile(root / "Scenes" / "First.scene");

        GameEditor::EditorContext context;
        if (!Expect(context.OpenProject(projectFile), "it should open"))
        {
            return false;
        }
        const GameEditor::SceneMigrationPlan* const plan = context.GetSceneMigrationPlan();
        const bool blocks = plan && plan->IsBlocked() && plan->opaqueComponents == 1;
        // 사람이 무엇을 해야 하는지가 줄에 있다: 어느 타입이고, 무엇을 하면 보이게 되는지.
        const bool namesTheTypeAndTheFix = plan &&
            plan->Describe().find("NoSchemaBehaviour") != std::string::npos &&
            plan->Describe().find("Build the game project") != std::string::npos;
        const bool refuses = !context.ApplySceneMigration() &&
            TestSupport::ReadFile(root / "Scenes" / "First.scene") == before && CountBackups(root) == 0;
        // 볼 수 없는 것을 보았다고 치지 않는다 — 새 참조는 계속 경로로 적힌다.
        const bool notCalledMigrated = !context.AreSceneReferencesMigrated();
        if (!Expect(blocks, "a component with no schema should block the migration") ||
            !Expect(namesTheTypeAndTheFix, "the blocked question should name the type and the fix") ||
            !Expect(refuses, "a blocked migration should refuse and write nothing") ||
            !Expect(notCalledMigrated, "a project that cannot be fully seen is not migrated"))
        {
            return false;
        }
    }

    // ---- 정체성 없는 에셋을 가리키면 막힌다.
    //
    // 열기 경로에서는 이 상태가 서지 않는다 — 여는 순간 발급이 먼저 돌아 모든 에셋이 정체성을
    // 받는다. 그래서 막는 판정 자체를, 그 판정이 사는 자리에서 잰다.
    {
        TemporaryDirectory temporaryDirectory("scene-migration-blocked");
        const std::filesystem::path root = temporaryDirectory.GetPath();
        const std::filesystem::path projectFile =
            WriteMigrationProject(root, spriteGuid, "Textures/Albedo.png");
        std::error_code error;
        const bool removed = !projectFile.empty() &&
            std::filesystem::remove(root / "Textures" / "Albedo.png.meta", error);

        const GameEngine::Platform::DirectoryContentSource content(root);
        Assets::AssetDatabase database;
        GameEditor::SceneMigrationPlan plan;
        if (removed && database.Refresh(content))
        {
            const Assets::Asset* const nameless = database.FindAsset("Textures/Albedo.png");
            if (nameless && !nameless->GetGuid().IsValid())
            {
                plan.assetsWithoutIdentity.push_back(nameless->GetRelativePath());
            }
        }
        const bool blocks = plan.IsBlocked() && plan.HasSomethingToAsk() && plan.convertible == 0;
        const bool namesTheFile =
            plan.Describe().find("Textures/Albedo.png") != std::string::npos;
        if (!Expect(blocks, "an asset with no identity should block the migration") ||
            !Expect(namesTheFile, "the blocked question should name the file to fix"))
        {
            return false;
        }
    }

    return true;
}

static const TestSupport::Registration gEditorUnsavedChangeTests{
    "EditorDocument", "editor unsaved-change tests should pass", RunEditorUnsavedChangeTests };

static const TestSupport::Registration gEditorPlayModeComponentTests{
    "EditorDocument", "editor play-mode component tests should pass", RunEditorPlayModeComponentTests };

static const TestSupport::Registration gEditorDockLayoutTests{
    "EditorDocument", "editor dock layout tests should pass", RunEditorDockLayoutTests };

static const TestSupport::Registration gEditorMissingSceneReportTests{
    "EditorDocument", "editor missing-scene report tests should pass", RunEditorMissingSceneReportTests };

static const TestSupport::Registration gEditorMissingSceneFileTests{
    "EditorDocument", "editor missing scene file tests should pass", RunEditorMissingSceneFileTests };

static const TestSupport::Registration gSceneCommandTests{
    "EditorDocument", "scene command tests should pass", RunSceneCommandTests };

static const TestSupport::Registration gEditorAssetWatchTests{
    "EditorDocument", "editor asset watch tests should pass", RunEditorAssetWatchTests };

static const TestSupport::Registration gSidecarWriterTests{
    "EditorDocument", "sidecar writer tests should pass", RunSidecarWriterTests };

static const TestSupport::Registration gAssetMoveTests{
    "EditorDocument", "asset move tests should pass", RunAssetMoveTests };

static const TestSupport::Registration gSceneReferenceMigrationTests{
    "EditorDocument", "scene reference migration tests should pass", RunSceneReferenceMigrationTests };

static const TestSupport::Registration gEditorToolbarStripTests{
    "EditorDocument", "editor toolbar strip tests should pass", RunEditorToolbarStripTests };


static const TestSupport::Registration gEditorRowFontFitTests{
    "EditorDocument", "editor row font fit tests should pass", RunEditorRowFontFitTests };

static const TestSupport::Registration gEditorFloatingWindowClampTests{
    "EditorDocument", "editor floating window clamp tests should pass", RunEditorFloatingWindowClampTests };
