#include "EditorPanelHostTests.h"

#include <cstdint>
#include <iostream>
#include <memory>

#include "Views/EditorConsolePanel.h"
#include "Document/EditorContext.h"
#include "Rules/EditorPanelHosts.h"
#include "Platform/PlatformServices.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Input.h"
#include "TestSupport.h"
#include "UI/UIContext.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 패널이 요구하는 좁은 호스트 인터페이스만 구현하고 호출 횟수를 기록한다.
    /// </summary>
    class CountingHost final
        : public GameEditor::IEditorScale
        , public GameEditor::IPropertyEditHost
        , public GameEditor::IAssetDragHost
    {
    public:
        mutable int scaleCalls = 0;
        mutable int propertyEditCalls = 0;
        mutable int assetDragCalls = 0;

        [[nodiscard]] float S(const float logical) const override
        {
            ++scaleCalls;
            return logical;
        }

        void ApplyProperty(
            GameEngine::Runtime::Component&,
            const GameEngine::Runtime::PropertyDescriptor&,
            const GameEngine::Runtime::PropertyValue&,
            std::uint64_t) override
        {
            ++propertyEditCalls;
        }

        [[nodiscard]] std::uint64_t MakeMergeKey(GameEngine::UI::WidgetId) const override
        {
            ++propertyEditCalls;
            return 0;
        }

        void ResetFieldEditingState() override { ++propertyEditCalls; }
        void PerformUndo() override { ++propertyEditCalls; }
        void PerformRedo() override { ++propertyEditCalls; }

        void BeginAssetDrag(GameEngine::Assets::AssetReference) override { ++assetDragCalls; }
        [[nodiscard]] bool IsDraggingAsset() const override
        {
            ++assetDragCalls;
            return false;
        }
        [[nodiscard]] const GameEngine::Assets::AssetReference& GetDraggedAsset() const override
        {
            ++assetDragCalls;
            return mNothing;
        }
        [[nodiscard]] GameEngine::Assets::AssetReference TakeDraggedAsset() override
        {
            ++assetDragCalls;
            return {};
        }

    private:
        GameEngine::Assets::AssetReference mNothing;
    };
}

bool RunEditorPanelHostTests()
{
    // 🔴 창도 셸도 없이 패널을 세운다. 이것이 되는 것이 이 단위의 값이다.
    CountingHost host;
    GameEditor::EditorContext context;
    auto rasterizer = TestSupport::CreateTestTextRasterizer();
    if (!rasterizer)
    {
        std::cout << "  editor panel host tests skipped: no bundled font on this machine\n";
        return true;
    }
    GameEngine::UI::UIContext ui{ nullptr, std::move(rasterizer) };
    GameEditor::EditorConsolePanel console(host, context, ui);

    GameEngine::Runtime::Input input;
    GameEngine::Rendering::RenderFrameBuilder builder;
    ui.BeginFrame(input, { 800, 600 });
    console.Draw({ 0.0f, 0.0f, 400.0f, 300.0f });
    ui.EndFrame(builder);

    // 콘솔은 배율만 받았고, 배율만 쓴다. 다른 호스트를 부를 방법이 <b>생성자에 없다</b> —
    // 그것이 이 시험이 값이 아니라 관계를 재고 있다는 뜻이다.
    const bool consoleUsedTheScale = host.scaleCalls > 0;
    const bool consoleTouchedNothingElse =
        host.propertyEditCalls == 0 && host.assetDragCalls == 0;

    return Expect(
            consoleUsedTheScale,
            "a panel should be buildable without a shell and should ask its host for the scale") &&
        Expect(
            consoleTouchedNothingElse,
            "a panel should not be able to reach services it was not given");
}

static const TestSupport::Registration gEditorPanelHostTests{
    "EditorDocument", "editor panel host tests should pass", RunEditorPanelHostTests };
