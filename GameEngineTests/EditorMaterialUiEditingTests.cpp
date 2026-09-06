#include "EditorMaterialUiEditingTests.h"

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
    /// <summary>EditorSpriteSheetUiEditingTests.cpp의 FakeInput과 같다.</summary>
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

    /// <summary>
    /// EditorSpriteSheetUiEditingTests.cpp의 NoOpHost와 같되, 끌어 놓기 상태만은 시험이
    /// 채워 넣을 수 있다 — 드롭다운 목록을 여닫는 대신, 실제 콘텐츠 브라우저 끌어 놓기가 그렇듯
    /// "이미 끌고 있는 에셋이 있다"에서 시작해 칸 위에서 떼는 것만으로 텍스처를 정한다. 그
    /// 경로가 목록을 펼치고 접히는 줄을 좌표로 다시 재는 것보다 창 없는 시험에서 훨씬 덜
    /// 부서지기 쉽다 — 칸 하나의 사각형만 맞으면 된다.
    /// </summary>
    class DragHost final
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
        void BeginAssetDrag(GameEngine::Assets::AssetReference reference) override
        {
            mDragged = std::move(reference);
        }
        [[nodiscard]] bool IsDraggingAsset() const override { return mDragged.IsValid(); }
        [[nodiscard]] const GameEngine::Assets::AssetReference& GetDraggedAsset() const override
        {
            return mDragged;
        }
        [[nodiscard]] GameEngine::Assets::AssetReference TakeDraggedAsset() override
        {
            GameEngine::Assets::AssetReference taken = mDragged;
            mDragged = {};
            return taken;
        }

        /// <summary>시험이 콘텐츠 브라우저를 대신해 끌기를 시작한다.</summary>
        void SetDraggedAsset(GameEngine::Assets::AssetReference reference)
        {
            mDragged = std::move(reference);
        }

    private:
        GameEngine::Assets::AssetReference mDragged;
    };

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

    /// <summary>SetCursor, 눌렀다 떼기를 세 프레임으로 나눠 한 번의 클릭을 낸다.</summary>
    void Click(
        FakeInput& source, GameEngine::Runtime::Input& input, GameEngine::UI::UIContext& ui,
        GameEditor::EditorInspectorPanel& panel, const int x, const int y)
    {
        source.SetCursor(x, y);
        RunPanelFrame(source, input, ui, panel);
        source.SetMouseButton(GameEngine::Platform::MouseButton::Left, true);
        RunPanelFrame(source, input, ui, panel);
        source.SetMouseButton(GameEngine::Platform::MouseButton::Left, false);
        RunPanelFrame(source, input, ui, panel);
    }
}

bool RunEditorMaterialUiEditingTests()
{
    using namespace GameEngine;

    TemporaryDirectory projectDirectory("material-ui-editing");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "MaterialUiTest.gameproject",
            R"({"projectName": "MaterialUiTest",)"
            R"( "window": {"width": 1280, "height": 720}, "targetFrameRate": 60,)"
            R"( "initialSceneId": 0, "scenes": [{"id": 0, "path": "Scenes/Main.scene"}]})") &&
        WriteFile(root / "Sprites" / "Circle.png", "png-data") &&
        WriteFile(root / "Sprites" / "Circle.png.meta",
            R"({"format": "gameengine-meta/1", "guid": "00000000000000000000000000000005"})") &&
        WriteFile(root / "Materials" / "Test.material",
            R"({"texture": "", "tint": [1.0, 1.0, 1.0, 1.0]})") &&
        WriteFile(root / "Scenes" / "Main.scene", R"({"sceneName": "Main", "gameObjects": []})");
    if (!Expect(wrote, "the material UI editing test project should be written"))
    {
        return false;
    }

    GameEditor::EditorContext context;
    if (!Expect(
            context.OpenProject(root / "MaterialUiTest.gameproject"), "the test project should open"))
    {
        return false;
    }
    context.SelectAsset(std::filesystem::path("Materials") / "Test.material");

    DragHost host;
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  material UI editing tests skipped: no bundled font on this machine\n";
        return true;
    }
    UI::UIContext ui{ nullptr, std::move(rasterizer) };
    GameEditor::EditorInspectorPanel panel(host, host, host, context, ui);

    FakeInput source;
    Runtime::Input input;

    // 같은 상수(EditorPanelCommon.h)에서 자리를 유도한다 — EditorSpriteSheetUiEditingTests.cpp와
    // 같은 방식이다. DrawAssetInspector: 제목 줄 다음 y = Padding + RowHeight + Padding. 그 y가
    // DrawMaterialSettings의 첫 행(Texture)이다.
    constexpr float titleBottom = GameEditor::Padding + GameEditor::RowHeight + GameEditor::Padding;
    constexpr float rowStride = GameEditor::RowHeight + 2.0f;
    constexpr float textureFieldY = titleBottom;
    const int fieldClickX = static_cast<int>(GameEditor::Padding + GameEditor::InspectorLabelWidth) + 20;
    const int textureClickY = static_cast<int>(textureFieldY) + 10;

    // 아무것도 편집하기 전에 한 프레임 그린다 — 창 없이도 그 자체가 죽지 않는지가 이 시험의
    // 첫 번째 확인이다: 값이 없는 텍스처, 흰 tint를 실제 Draw 호출로 그려서 크래시가 없는지.
    RunPanelFrame(source, input, ui, panel);

    bool passed = true;

    // ---- 텍스처: 콘텐츠 브라우저에서 스프라이트를 끌어다 그 칸에 놓는다 ----
    host.SetDraggedAsset(Assets::AssetReference(std::filesystem::path("Sprites/Circle.png")));
    Click(source, input, ui, panel, fieldClickX, textureClickY);
    // 놓는 클릭은 그 칸의 머리 버튼 위에서도 일어난다 — 같은 눌림-뗌이 놓기와 버튼의 클릭을
    // 함께 만족시켜, 놓자마자 선택 목록도 펼쳐진다. 다시 한번 눌러 접어 둔다: 이번에는 끌던
    // 것이 없으니(TakeDraggedAsset이 이미 비웠다) 버튼의 여닫기만 일어난다. 접지 않으면 펼친
    // 목록이 그 아래 Tint 행의 y를 밀어내려, 다음 클릭이 엉뚱한 자리를 짚는다.
    Click(source, input, ui, panel, fieldClickX, textureClickY);

    {
        const std::optional<std::string> text =
            Platform::ReadTextFile(root / "Materials" / "Test.material");
        passed &= Expect(text.has_value(), "the material file should still be readable");
        if (text)
        {
            const Core::Json json = Core::Json::Parse(*text);
            passed &= Expect(
                json.Value("texture", std::string{}) == "Sprites/Circle.png",
                "choosing the sprite from the real Texture dropdown should write its path to "
                "the material file");
        }
    }

    // ---- Tint: R 칸을 눌러 전체 선택하고 새 값을 친다 ----
    // Texture 행 다음이 Tint 행이다. 네 칸(r,g,b,a) 중 첫 번째가 R이다.
    constexpr float tintFieldY = textureFieldY + rowStride;
    const int tintClickX = fieldClickX;
    const int tintClickY = static_cast<int>(tintFieldY) + 10;
    Click(source, input, ui, panel, tintClickX, tintClickY);

    source.SetKey(Platform::Key::Control, true);
    source.SetKey(Platform::Key::A, true);
    RunPanelFrame(source, input, ui, panel);
    source.SetKey(Platform::Key::A, false);
    source.SetKey(Platform::Key::Control, false);
    RunPanelFrame(source, input, ui, panel);

    source.Type("0.25");
    RunPanelFrame(source, input, ui, panel);

    {
        const std::optional<std::string> text =
            Platform::ReadTextFile(root / "Materials" / "Test.material");
        passed &= Expect(text.has_value(), "the material file should still be readable");
        if (text)
        {
            const Core::Json json = Core::Json::Parse(*text);
            const Core::Json* const tint = json.Find("tint");
            passed &= Expect(
                tint && tint->IsArray() && tint->Size() == 4 && tint->At(0).Get<float>() == 0.25f,
                "typing into the Tint R field in the real Inspector draw path should write the "
                "new value to the material file");
            passed &= Expect(
                tint && tint->Size() == 4 && tint->At(1).Get<float>() == 1.0f &&
                    tint->At(2).Get<float>() == 1.0f && tint->At(3).Get<float>() == 1.0f,
                "editing R should leave G, B and A exactly where they were");
            passed &= Expect(
                json.Value("texture", std::string{}) == "Sprites/Circle.png",
                "editing the tint should not disturb the texture chosen a moment ago");
        }
    }

    return passed;
}

static const TestSupport::Registration gEditorMaterialUiEditingTests{
    "EditorDocument", "editor material UI editing tests should pass",
    RunEditorMaterialUiEditingTests };
