#include "EditorSpriteSheetUiEditingTests.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "../GameEditor/Source/Document/EditorContext.h"
#include "../GameEditor/Source/Rules/EditorPanelCommon.h"
#include "../GameEditor/Source/Rules/EditorPanelHosts.h"
#include "../GameEditor/Source/Views/EditorInspectorPanel.h"
#include "Core/Json.h"
#include "Platform/TextFile.h"
#include "Platform/IInput.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Input.h"
#include "TestSupport.h"
#include "UI/UIContext.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    /// <summary>UIContextTests.cpp의 FakeInput과 같다: 플랫폼 입력을 대신한다.</summary>
    class FakeInput final : public GameEngine::Platform::IInput
    {
    public:
        void ReadState(GameEngine::Platform::InputState& state) override
        {
            state = mState;
            state.hasFocus = true;
            mState.TakeAccumulated();
        }

        void SetKey(const GameEngine::Platform::Key key, const bool isDown) { mState.SetKey(key, isDown); }
        void SetMouseButton(const GameEngine::Platform::MouseButton button, const bool isDown)
        {
            mState.SetMouseButton(button, isDown);
        }
        void SetCursor(const int x, const int y) { mState.cursor = { x, y }; }
        void Type(const std::string& text) { mState.typedText += text; }

    private:
        GameEngine::Platform::InputState mState;
    };

    /// <summary>EditorPanelHostTests.cpp의 CountingHost와 같은 자리다: 셸 대신 세우는 가짜다.</summary>
    class NoOpHost final
        : public GameEditor::IEditorScale
        , public GameEditor::IPropertyEditHost
        , public GameEditor::IAssetDragHost
    {
    public:
        [[nodiscard]] float S(const float logical) const override { return logical; }
        void ApplyProperty(
            GameEngine::Runtime::Component&, const GameEngine::Runtime::PropertyDescriptor&,
            const GameEngine::Runtime::PropertyValue&, std::uint64_t) override
        {
        }
        [[nodiscard]] std::uint64_t MakeMergeKey(GameEngine::UI::WidgetId) const override { return 0; }
        void ResetFieldEditingState() override {}
        void PerformUndo() override {}
        void PerformRedo() override {}
        void BeginAssetDrag(GameEngine::Assets::AssetReference) override {}
        [[nodiscard]] bool IsDraggingAsset() const override { return false; }
        [[nodiscard]] const GameEngine::Assets::AssetReference& GetDraggedAsset() const override
        {
            return mNothing;
        }
        [[nodiscard]] GameEngine::Assets::AssetReference TakeDraggedAsset() override { return {}; }

    private:
        GameEngine::Assets::AssetReference mNothing;
    };

    /// <summary>
    /// 한 프레임을 연출한다. UIContextTests.cpp의 RunUIFrame과 같은 모양이되, 여는 것은
    /// EditorInspectorPanel::Draw 실제 호출이다 — 이 시험이 재는 것은 그 호출 안에서 실제로
    /// 그려지는 Columns 칸이지, 시험이 흉내 낸 텍스트 필드가 아니다.
    /// </summary>
    void RunPanelFrame(
        FakeInput& source, GameEngine::Runtime::Input& input, GameEngine::UI::UIContext& ui,
        GameEditor::EditorInspectorPanel& panel)
    {
        input.BeginFrame(source);
        ui.BeginFrame(input, { 800, 600 });
        panel.Draw({ 0.0f, 0.0f, 800.0f, 600.0f });
        GameEngine::Rendering::RenderFrameBuilder builder;
        builder.SetRenderTargetSize({ 800, 600 });
        ui.EndFrame(builder);
    }
}

bool RunEditorSpriteSheetUiEditingTests()
{
    using namespace GameEngine;

    TemporaryDirectory projectDirectory("sprite-sheet-ui-editing");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "SpriteSheetUiTest.gameproject",
            R"({"projectName": "SpriteSheetUiTest",)"
            R"( "window": {"width": 1280, "height": 720}, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [{"id": 0, "path": "Scenes/Main.scene"}]})") &&
        WriteFile(root / "Sprites" / "Chest.png", "png-data") &&
        WriteFile(root / "Sprites" / "Chest.png.meta",
            R"({"format": "gameengine-meta/1", "guid": "00000000000000000000000000000009",)"
            R"( "pixelsPerUnit": 32,)"
            R"( "sheet": {"columns": 8, "rows": 8, "frameCount": 32, "frameRate": 10}})");
    if (!Expect(wrote, "the sprite sheet UI editing test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(
            context.OpenProject(root / "SpriteSheetUiTest.gameproject"),
            "the test project should open"))
    {
        return false;
    }
    context.SelectAsset(std::filesystem::path("Sprites") / "Chest.png");

    NoOpHost host;
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  sprite sheet UI editing tests skipped: no bundled font on this machine\n";
        return true;
    }
    UI::UIContext ui{ nullptr, std::move(rasterizer) };
    GameEditor::EditorInspectorPanel panel(host, host, host, context, ui);

    FakeInput source;
    Runtime::Input input;

    // Columns 칸의 실제 사각형이다. EditorInspectorPanel이 쓰는 것과 같은 상수(EditorPanelCommon.h)
    // 로 계산한다 — 손으로 다시 잰 숫자가 아니라, 패널이 쓰는 바로 그 값들에서 유도한다.
    // DrawAssetInspector: y = Padding, 제목 줄 다음 y += RowHeight + Padding.
    // DrawSpriteSheetSettings의 첫 행(Columns)은 그 y에서 LayoutPropertyRow가 칸을 낸다.
    constexpr float titleBottom = GameEditor::Padding + GameEditor::RowHeight + GameEditor::Padding;
    constexpr float columnsFieldX = GameEditor::Padding + GameEditor::InspectorLabelWidth;
    constexpr float columnsFieldY = titleBottom;
    const int clickX = static_cast<int>(columnsFieldX) + 20;
    const int clickY = static_cast<int>(columnsFieldY) + 10;

    // 포인터의 임자는 직전 프레임의 선언들로 정해진다: 커서를 얹은 프레임과 누르는 프레임이
    // 다르다.
    source.SetCursor(clickX, clickY);
    RunPanelFrame(source, input, ui, panel);
    source.SetMouseButton(Platform::MouseButton::Left, true);
    RunPanelFrame(source, input, ui, panel);
    source.SetMouseButton(Platform::MouseButton::Left, false);
    RunPanelFrame(source, input, ui, panel);

    // 전체 선택 후 새 값을 친다 — 기존 "8" 뒤에 이어 붙는 대신 통째로 바뀌어야 하므로, 캐럿
    // 위치에 기대지 않는다.
    source.SetKey(Platform::Key::Control, true);
    source.SetKey(Platform::Key::A, true);
    RunPanelFrame(source, input, ui, panel);
    source.SetKey(Platform::Key::A, false);
    source.SetKey(Platform::Key::Control, false);
    RunPanelFrame(source, input, ui, panel);

    source.Type("5");
    RunPanelFrame(source, input, ui, panel);
    // strtof는 "5"를 그 자리에서 이미 온전한 수로 읽으므로 apply가 이 프레임에 불렸어야 한다.
    // 파일 감시는 이 시험에 없으므로, 디스크를 직접 읽어 사이드카가 실제로 바뀌었는지 본다.

    const std::optional<std::string> text =
        Platform::ReadTextFile(root / "Sprites" / "Chest.png.meta");
    bool passed = Expect(text.has_value(), "the sidecar should still be readable");
    if (!text)
    {
        return false;
    }
    const Core::Json json = Core::Json::Parse(*text);
    const Core::Json* const sheet = json.Find("sheet");
    passed &= Expect(
        sheet && sheet->Value("columns", 0) == 5,
        "typing into the Columns field in the real Inspector draw path should write the new "
        "value to the sidecar on disk");
    passed &= Expect(
        json.Value("guid", std::string{}) == "00000000000000000000000000000009",
        "the edit should not disturb the existing guid");
    passed &= Expect(
        json.Value("pixelsPerUnit", 0.0f) == 32.0f,
        "the edit should not disturb pixelsPerUnit");

    // 전체 선택 없이 값을 이어 입력한 뒤 Enter를 누르면 확정한 값이 유지되어야 한다.
    const float rowStride = GameEditor::RowHeight + 2.0f;
    const float frameRateFieldY = columnsFieldY + 3.0f * rowStride;
    const int frameRateClickX = static_cast<int>(columnsFieldX) + 20;
    const int frameRateClickY = static_cast<int>(frameRateFieldY) + 10;

    source.SetCursor(frameRateClickX, frameRateClickY);
    RunPanelFrame(source, input, ui, panel);
    source.SetMouseButton(Platform::MouseButton::Left, true);
    RunPanelFrame(source, input, ui, panel);
    source.SetMouseButton(Platform::MouseButton::Left, false);
    RunPanelFrame(source, input, ui, panel);

    // 캐럿을 끝으로 보내 "이어 타이핑"을 확정적으로 만든다 — 클릭이 어디를 짚었든, 이제부터의
    // 입력은 기존 텍스트 뒤에 붙는다.
    source.SetKey(Platform::Key::End, true);
    RunPanelFrame(source, input, ui, panel);
    source.SetKey(Platform::Key::End, false);
    RunPanelFrame(source, input, ui, panel);

    source.Type("0"); // 지금 보이는 "10"에 이어 붙어 "100"이 된다.
    RunPanelFrame(source, input, ui, panel);
    // Enter: 텍스트는 이 프레임에 바뀌지 않으므로 changed는 거짓이지만, mFocusedField가
    // 걷힌다 — 앞선 프레임에서 apply가 이미 불렸어야 한다.
    source.SetKey(Platform::Key::Enter, true);
    RunPanelFrame(source, input, ui, panel);
    source.SetKey(Platform::Key::Enter, false);
    RunPanelFrame(source, input, ui, panel);

    const std::optional<std::string> textAfterEnter =
        Platform::ReadTextFile(root / "Sprites" / "Chest.png.meta");
    passed &= Expect(textAfterEnter.has_value(), "the sidecar should still be readable after Enter");
    if (textAfterEnter)
    {
        const Core::Json jsonAfterEnter = Core::Json::Parse(*textAfterEnter);
        const Core::Json* const sheetAfterEnter = jsonAfterEnter.Find("sheet");
        passed &= Expect(
            sheetAfterEnter && sheetAfterEnter->Value("frameRate", 0.0f) == 100.0f,
            "typing into Frame Rate without Ctrl+A, then committing with Enter, should write "
            "the appended value to the sidecar");
    }

    // ---- 같은 프레임에서 마지막 글자와 Enter가 함께 오는 경우도 잰다 ----
    // 빠르게 치고 곧장 Enter를 누르면 같은 프레임에 typedText와 Enter가 함께 들어올 수 있다.
    // DrawTextField 안에서 changed는 그 프레임의 typedText 처리로 이미 정해진 뒤에야
    // mFocusedField가 걷히므로(521~536번 줄 근처 순서), 이 경우도 앞의 경우와 같은 결과여야
    // 한다 — 다르면 그것이 진짜 결함이다.
    {
        const float frameCountFieldY = columnsFieldY + 2.0f * rowStride;
        const int frameCountClickX = static_cast<int>(columnsFieldX) + 20;
        const int frameCountClickY = static_cast<int>(frameCountFieldY) + 10;

        source.SetCursor(frameCountClickX, frameCountClickY);
        RunPanelFrame(source, input, ui, panel);
        source.SetMouseButton(Platform::MouseButton::Left, true);
        RunPanelFrame(source, input, ui, panel);
        source.SetMouseButton(Platform::MouseButton::Left, false);
        RunPanelFrame(source, input, ui, panel);

        source.SetKey(Platform::Key::Control, true);
        source.SetKey(Platform::Key::A, true);
        RunPanelFrame(source, input, ui, panel);
        source.SetKey(Platform::Key::A, false);
        source.SetKey(Platform::Key::Control, false);
        RunPanelFrame(source, input, ui, panel);

        // 같은 프레임에 타이핑과 Enter를 함께 싣는다.
        source.Type("7");
        source.SetKey(Platform::Key::Enter, true);
        RunPanelFrame(source, input, ui, panel);
        source.SetKey(Platform::Key::Enter, false);
        RunPanelFrame(source, input, ui, panel);

        const std::optional<std::string> textAfterSameFrameEnter =
            Platform::ReadTextFile(root / "Sprites" / "Chest.png.meta");
        passed &= Expect(
            textAfterSameFrameEnter.has_value(),
            "the sidecar should still be readable after a same-frame type+Enter");
        if (textAfterSameFrameEnter)
        {
            const Core::Json jsonAfterSameFrameEnter = Core::Json::Parse(*textAfterSameFrameEnter);
            const Core::Json* const sheetAfterSameFrameEnter = jsonAfterSameFrameEnter.Find("sheet");
            passed &= Expect(
                sheetAfterSameFrameEnter && sheetAfterSameFrameEnter->Value("frameCount", 0) == 7,
                "typing the last character and pressing Enter in the same frame should still "
                "write the new value -- the edit is read before focus is cleared");
        }
    }

    return passed;
}

static const TestSupport::Registration gEditorSpriteSheetUiEditingTests{
    "EditorDocument", "editor sprite sheet UI editing tests should pass",
    RunEditorSpriteSheetUiEditingTests };
