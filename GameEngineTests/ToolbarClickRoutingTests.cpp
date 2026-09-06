#include "ToolbarClickRoutingTests.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Views/EditorToolbarButton.h"
#include "Rules/EditorToolbarLayout.h"
#include "Views/EditorToolbarView.h"
#include "Platform/IInput.h"
#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "Runtime/Button.h"
#include "Runtime/Canvas.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/RectTransform.h"
#include "Runtime/RuntimeContext.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UILayoutSystem.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 툴바 형태의 클릭 라우팅을 검사하는 버튼 열두 개다.
    /// </summary>
    constexpr const char* Labels[] = {
        "New Project", "Open Project", "Save Scene", "New Scene", "New Script",
        "Rename Scene", "Delete Scene", "Undo", "Redo", "Snap: Off", "Build", "Play" };

    /// <summary>창의 논리 폭이다. 넓게 두어 열두 개가 한 줄에 놓이게 한다.</summary>
    constexpr float LogicalWindowWidth = 1200.0f;
    constexpr float SurfaceHeight = 600.0f;

    /// <summary>
    /// 진짜 툴바 버튼들을 세우고, 사람이 하는 것과 같은 길로 클릭을 흘려 준다: 커서를 옮기고,
    /// 누르고, 뗀다. 뷰에 「이 버튼이 눌렸다」고 직접 말하지 않는 것이 요점이다 — 그렇게 물으면
    /// 커서가 그 버튼에 실제로 닿는지는 영영 확인되지 않는다.
    /// </summary>
    class ToolbarHarness final
    {
    public:
        [[nodiscard]] bool Open(const float scale)
        {
            mScale = scale;
            auto rasterizer = TestSupport::CreateTestTextRasterizer();
            if (!rasterizer || !rasterizer->Initialize())
            {
                return false;
            }
            mMeasure = std::make_unique<GameEngine::Rendering::CachedTextMeasure>(
                std::make_shared<GameEngine::Rendering::TextRasterizationCache>(
                    std::move(rasterizer)));

            mSceneManager = std::make_unique<GameEngine::Runtime::SceneManager>(mContext);
            auto scene = std::make_unique<GameEngine::Runtime::Scene>(mContext, "EditorUI");
            GameEngine::Runtime::Scene& sceneRef = *scene;

            GameEngine::Runtime::GameObject* const root = sceneRef.CreateGameObject("EditorUI");
            GameEngine::Runtime::Canvas* const canvas =
                root ? root->AddComponent<GameEngine::Runtime::Canvas>() : nullptr;
            if (!canvas)
            {
                return false;
            }
            canvas->SetScaleFactor(scale);

            GameEngine::Runtime::GameObject* const strip = sceneRef.CreateGameObject("Toolbar");
            mStripRect =
                strip ? strip->AddComponent<GameEngine::Runtime::RectTransform>() : nullptr;
            if (!mStripRect)
            {
                return false;
            }
            static_cast<void>(strip->GetTransform().SetParent(&root->GetTransform()));
            mStripRect->SetAnchorMin({ 0.0f, 0.0f });
            mStripRect->SetAnchorMax({ 1.0f, 0.0f });
            mStripRect->SetOffsetMin({ 0.0f, 0.0f });
            mStripRect->SetOffsetMax({ 0.0f, GameEditor::ToolbarLayoutMetrics{}.rowHeight });

            // 편집기와 같은 계층을 세운다: 띠 아래에 버튼들이 사는 줄이 한 겹 더 있다.
            // 그 한 겹이 자식의 보이는 사각형을 자르면 버튼은 그려지되 눌리지 않는다.
            GameEngine::Runtime::GameObject* const group =
                sceneRef.CreateGameObject("ToolbarButtons");
            GameEngine::Runtime::RectTransform* const groupRect =
                group ? group->AddComponent<GameEngine::Runtime::RectTransform>() : nullptr;
            if (!groupRect)
            {
                return false;
            }
            static_cast<void>(group->GetTransform().SetParent(&strip->GetTransform()));
            groupRect->SetAnchorMin({ 0.0f, 0.0f });
            groupRect->SetAnchorMax({ 1.0f, 1.0f });
            groupRect->SetOffsetMin({ 0.0f, 0.0f });
            groupRect->SetOffsetMax({ 0.0f, 0.0f });

            for (const char* const label : Labels)
            {
                mButtons.push_back(GameEditor::BuildToolbarButton(
                    sceneRef, *group, label, GameEditor::ToolbarLabelFontSize));
            }
            static_cast<void>(mSceneManager->AddScene(std::move(scene)));

            // 툴바가 프레임마다 하는 것과 같이, 잰 폭으로 자리를 정한다. 두 번 도는 이유는 첫
            // 프레임에는 잰 것이 없기 때문이다.
            Place();
            Place();
            return true;
        }

        /// <summary>버튼 하나의 그려지는 사각형이다. 픽셀이다.</summary>
        [[nodiscard]] GameEngine::Runtime::RectTransform::Rect DrawnRect(
            const std::size_t index) const
        {
            return mButtons[index].rect->GetResolvedRect();
        }

        /// <summary>그 자리를 한 번 클릭하고, 그 프레임에 클릭을 말한 버튼들의 이름을 낸다.</summary>
        [[nodiscard]] std::vector<std::string> ClickAt(const float x, const float y)
        {
            Step(x, y, true, false);
            Step(x, y, false, true);

            std::vector<std::string> clicked;
            for (std::size_t index = 0; index < mButtons.size(); ++index)
            {
                const GameEngine::Runtime::Button* const button = mButtons[index].button;
                if (button && button->WasClickedThisFrame())
                {
                    clicked.emplace_back(Labels[index]);
                }
            }
            return clicked;
        }

    private:
        void Place()
        {
            mLayout.Synchronize(
                *mSceneManager, LogicalWindowWidth * mScale, SurfaceHeight, mMeasure.get());

            std::vector<float> desired;
            desired.reserve(mButtons.size());
            for (const GameEditor::ToolbarButtonParts& parts : mButtons)
            {
                desired.push_back(parts.rect ? parts.rect->GetDesiredSize().width : 0.0f);
            }
            const GameEditor::ToolbarLayout placed = GameEditor::ComputeToolbarRowRects(
                desired, mStripRect->GetResolvedRect().width, mScale, mMetrics);
            for (std::size_t index = 0; index < mButtons.size(); ++index)
            {
                GameEngine::Runtime::RectTransform* const rect = mButtons[index].rect;
                if (!rect)
                {
                    continue;
                }
                const GameEngine::UI::UIRect& box = placed.buttons[index];
                rect->SetOffsetMin({ box.x, box.y });
                rect->SetOffsetMax({ box.x + box.width, box.y + box.height });
            }
            mStripRect->SetOffsetMax({ 0.0f, placed.height });
            mLayout.Synchronize(
                *mSceneManager, LogicalWindowWidth * mScale, SurfaceHeight, mMeasure.get());
        }

        void Step(const float x, const float y, const bool press, const bool release)
        {
            GameEngine::Platform::InputState state;
            state.cursor.x = static_cast<int>(x);
            state.cursor.y = static_cast<int>(y);
            const auto left = static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left);
            state.mouseButtons[left] = press;
            state.mousePresses[left] = press ? 1 : 0;
            state.mouseReleases[left] = release ? 1 : 0;
            mInput.BeginFrameWithState(state);

            mLayout.Synchronize(
                *mSceneManager, LogicalWindowWidth * mScale, SurfaceHeight, mMeasure.get());
            static_cast<void>(mEvents.Synchronize(*mSceneManager, mInput));
        }

        GameEngine::Runtime::ObjectRegistry mRegistry;
        GameEngine::Runtime::Input mInput;
        GameEngine::Runtime::RuntimeContext mContext{ mRegistry, mInput };
        std::unique_ptr<GameEngine::Runtime::SceneManager> mSceneManager;
        std::unique_ptr<GameEngine::Rendering::CachedTextMeasure> mMeasure;
        GameEngine::Runtime::UILayoutSystem mLayout;
        GameEngine::Runtime::UIEventSystem mEvents;
        GameEngine::Runtime::RectTransform* mStripRect = nullptr;
        std::vector<GameEditor::ToolbarButtonParts> mButtons;
        GameEditor::ToolbarLayoutMetrics mMetrics;
        float mScale = 1.0f;
    };

    /// <summary>주어진 배율에서 열두 버튼을 하나씩 눌러 본다.</summary>
    [[nodiscard]] bool CheckClickRoutingAtScale(const float scale)
    {
        ToolbarHarness harness;
        if (!harness.Open(scale))
        {
            std::cout << "  toolbar click routing tests skipped: no text rasterizer\n";
            return true;
        }

        bool passed = true;
        for (std::size_t index = 0; index < std::size(Labels); ++index)
        {
            const GameEngine::Runtime::RectTransform::Rect rect = harness.DrawnRect(index);
            const float centreX = rect.x + rect.width * 0.5f;
            const float centreY = rect.y + rect.height * 0.5f;
            const std::vector<std::string> clicked = harness.ClickAt(centreX, centreY);

            const bool onlyThisOne =
                clicked.size() == 1 && clicked.front() == Labels[index];
            if (!onlyThisOne)
            {
                std::cerr << "  clicking the middle of \"" << Labels[index] << "\" at "
                          << centreX << "," << centreY << " was answered by ";
                if (clicked.empty())
                {
                    std::cerr << "nothing at all";
                }
                else
                {
                    for (const std::string& name : clicked)
                    {
                        std::cerr << "\"" << name << "\" ";
                    }
                }
                std::cerr << "\n";
            }
            passed &= Expect(
                onlyThisOne, "the button under the cursor is the one that answers the click");
        }
        return passed;
    }
}

bool RunToolbarClickRoutingTests()
{
    return TestSupport::ForEachUiScale(CheckClickRoutingAtScale);
}

static const TestSupport::Registration gToolbarClickRoutingTests{
    "UIEvent", "toolbar click routing tests should pass", RunToolbarClickRoutingTests };
