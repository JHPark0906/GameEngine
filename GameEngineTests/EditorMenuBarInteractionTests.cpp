#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>

#include "Views/EditorMenuBarView.h"
#include "Rules/EditorMenuModel.h"
#include "Rules/EditorPanelCommon.h"

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
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "Runtime/UIEventSystem.h"
#include "Runtime/UIWindow.h"
#include "Runtime/UILayoutSystem.h"

#include "EditorMenuBarInteractionTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEditor::EditorMenuCommand;
    using GameEditor::MenuCommandAvailability;
    using GameEditor::MenuPointer;
    using GameEditor::MenuRowParts;

    constexpr float SurfaceWidth = 1200.0f;
    constexpr float SurfaceHeight = 800.0f;

    /// <summary>
    /// 진짜 메뉴 막대 하나를 세우고, 진짜 클릭을 이벤트 시스템에 흘려 준다.
    ///
    /// 좌표를 뷰에 직접 넘기지 않는 것이 이 자리의 요점이다. 뷰는 커서가 어디 있는지 모르고
    /// 자기 버튼이 눌렸는지만 알므로, 시험도 사람이 하는 것과 같은 길 — 커서를 옮기고, 누르고,
    /// 떼는 — 로만 물어야 그 무지가 진짜인지 확인된다.
    /// </summary>
    class MenuBarHarness final
    {
    public:
        [[nodiscard]] bool Open()
        {
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
            mScene = &sceneRef;

            mRoot = sceneRef.CreateGameObject("EditorUI");
            GameEngine::Runtime::Canvas* const canvas =
                mRoot ? mRoot->AddComponent<GameEngine::Runtime::Canvas>() : nullptr;
            if (!canvas)
            {
                return false;
            }
            canvas->SetScaleFactor(1.0f);

            // 메뉴가 덮는 자리에 버튼 하나를 미리 세워 둔다. 열린 목록이 이 버튼을 가려도
            // 함께 눌리지 않는지가 이 시험이 지키는 것 중 가장 값진 하나다.
            mUnderneath = AddButtonBeneath(sceneRef, *mRoot);

            mView.Build(sceneRef, *mRoot, GameEditor::RowFontSize);
            static_cast<void>(mSceneManager->AddScene(std::move(scene)));

            // 세운 직후에는 잰 것이 없다. 두 프레임을 돌려야 머리줄이 자기 자리를 갖는다.
            Idle();
            Idle();
            return true;
        }

        /// <summary>아무 일도 없는 한 프레임이다. 커서는 그 자리에 그대로 있다.</summary>
        void Idle() { static_cast<void>(Step(mCursorX, mCursorY, false, false)); }

        /// <summary>커서를 옮긴다. 누르지 않으므로 열린 것은 그대로 남는다.</summary>
        void MoveTo(const float x, const float y) { static_cast<void>(Step(x, y, false, false)); }

        /// <summary>
        /// 그 자리를 한 번 클릭한다 — 누르는 프레임과 떼는 프레임 둘이며, 클릭이 완성되는
        /// 것은 뒤쪽이다. 버튼은 누른 자리와 뗀 자리가 같을 때만 클릭을 말한다.
        /// </summary>
        [[nodiscard]] std::optional<EditorMenuCommand> ClickAt(const float x, const float y)
        {
            static_cast<void>(Step(x, y, true, false));
            return Step(x, y, false, true);
        }

        /// <summary>Esc다.</summary>
        void Cancel()
        {
            const MenuPointer pointer{ false, true };
            static_cast<void>(mView.Update(pointer, mAvailability, SurfaceWidth, 1.0f));
            Idle();
        }

        void SetAvailability(const MenuCommandAvailability& availability)
        {
            mAvailability = availability;
        }

        [[nodiscard]] GameEditor::EditorMenuBarView& View() { return mView; }
        [[nodiscard]] const GameEngine::Runtime::RectTransform* Heading(
            const std::size_t index) const
        {
            return index < mView.GetBar().headings.size() ? mView.GetBar().headings[index].rect
                                                          : nullptr;
        }
        [[nodiscard]] const GameEngine::Runtime::Button* Underneath() const
        {
            return mUnderneath;
        }

        /// <summary>
        /// 화면 한쪽에 모달 창을 세운다. 모달은 자기 자리에 있든 없든 <b>모든</b> 입력을
        /// 가지므로, 이것이 서 있는 동안 메뉴는 아무것도 받지 않아야 한다.
        /// </summary>
        void RaiseModal()
        {
            using namespace GameEngine::Runtime;
            Scene* const scene = mScene;
            if (!scene || !mRoot || mModal)
            {
                return;
            }
            mModal = scene->CreateGameObject("Modal");
            if (!mModal)
            {
                return;
            }
            static_cast<void>(mModal->GetTransform().SetParent(&mRoot->GetTransform()));
            RectTransform* const rect = mModal->AddComponent<RectTransform>();
            if (rect)
            {
                // 메뉴에서 멀리 떨어뜨려 둔다. 겹쳐 놓으면 「모달이라 못 받는다」와 「가려서
                // 못 받는다」가 구별되지 않아, 이 시험이 무엇을 재는지 알 수 없게 된다.
                rect->SetAnchorMin({ 0.0f, 0.0f });
                rect->SetAnchorMax({ 0.0f, 0.0f });
                rect->SetOffsetMin({ 900.0f, 700.0f });
                rect->SetOffsetMax({ 1100.0f, 780.0f });
            }
            if (UIWindow* const window = mModal->AddComponent<UIWindow>())
            {
                window->SetModal(true);
            }
            static_cast<void>(mModal->AddComponent<Button>());
            Idle();
        }

    private:
        [[nodiscard]] static GameEngine::Runtime::Button* AddButtonBeneath(
            GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent)
        {
            using namespace GameEngine::Runtime;
            GameObject* const object = scene.CreateGameObject("Underneath");
            if (!object)
            {
                return nullptr;
            }
            static_cast<void>(object->GetTransform().SetParent(&parent.GetTransform()));
            RectTransform* const rect = object->AddComponent<RectTransform>();
            if (!rect)
            {
                return nullptr;
            }
            // 메뉴 막대 바로 아래의 넓은 자리다. 어느 메뉴를 펼치든 그 목록이 이 위에 선다.
            rect->SetAnchorMin({ 0.0f, 0.0f });
            rect->SetAnchorMax({ 0.0f, 0.0f });
            rect->SetOffsetMin({ 0.0f, 0.0f });
            rect->SetOffsetMax({ 600.0f, 600.0f });
            return object->AddComponent<Button>();
        }

        [[nodiscard]] std::optional<EditorMenuCommand> Step(
            const float x, const float y, const bool press, const bool release)
        {
            mCursorX = x;
            mCursorY = y;

            GameEngine::Platform::InputState state;
            state.cursor.x = static_cast<int>(x);
            state.cursor.y = static_cast<int>(y);
            const auto left = static_cast<std::size_t>(GameEngine::Platform::MouseButton::Left);
            // 누른 프레임에는 버튼이 내려가 있고, 뗀 프레임에는 올라와 있다. 누적된 횟수가
            // 그 사이에 일어난 일을 말하므로, 한 프레임 안의 클릭도 사라지지 않는다.
            state.mouseButtons[left] = press;
            state.mousePresses[left] = press ? 1 : 0;
            state.mouseReleases[left] = release ? 1 : 0;
            mInput.BeginFrameWithState(state);

            mLayout.Synchronize(*mSceneManager, SurfaceWidth, SurfaceHeight, mMeasure.get());
            static_cast<void>(mEvents.Synchronize(*mSceneManager, mInput));

            const MenuPointer pointer{ release, false };
            std::optional<EditorMenuCommand> chosen =
                mView.Update(pointer, mAvailability, SurfaceWidth, 1.0f);

            // 뷰가 이 프레임에 옮긴 사각형은 다음 배치에서야 자리를 갖는다. 사람이 보는 화면도
            // 그렇다 — 누른 다음 프레임에 목록이 나타난다.
            mLayout.Synchronize(*mSceneManager, SurfaceWidth, SurfaceHeight, mMeasure.get());
            return chosen;
        }

        GameEngine::Runtime::ObjectRegistry mRegistry;
        GameEngine::Runtime::Input mInput;
        GameEngine::Runtime::RuntimeContext mContext{ mRegistry, mInput };
        std::unique_ptr<GameEngine::Runtime::SceneManager> mSceneManager;
        std::unique_ptr<GameEngine::Rendering::CachedTextMeasure> mMeasure;
        const GameEngine::Runtime::UILayoutSystem mLayout;
        GameEngine::Runtime::UIEventSystem mEvents;
        GameEngine::Runtime::Scene* mScene = nullptr;
        GameEngine::Runtime::GameObject* mRoot = nullptr;
        GameEngine::Runtime::Button* mUnderneath = nullptr;
        GameEngine::Runtime::GameObject* mModal = nullptr;
        GameEditor::EditorMenuBarView mView;
        MenuCommandAvailability mAvailability;
        float mCursorX = -1.0f;
        float mCursorY = -1.0f;
    };

    /// <summary>사각형 한가운데다. 가장자리를 겨누면 한 픽셀 차이로 답이 갈린다.</summary>
    [[nodiscard]] float MidX(const GameEngine::Runtime::RectTransform& rect)
    {
        return rect.GetVisibleRect().x + rect.GetVisibleRect().width * 0.5f;
    }
    [[nodiscard]] float MidY(const GameEngine::Runtime::RectTransform& rect)
    {
        return rect.GetVisibleRect().y + rect.GetVisibleRect().height * 0.5f;
    }
}

bool RunEditorMenuBarInteractionTests()
{
    std::cout << "running editor menu bar interaction tests\n";

    MenuBarHarness harness;
    if (!harness.Open())
    {
        std::cout << "  menu bar interaction tests skipped: no text rasterizer on this machine\n";
        return true;
    }

    // 아무것도 열려 있지 않은 편집기다. File 메뉴가 「고를 수 있는 항목·구분선·지금은 고를 수
    // 없는 항목」을 한꺼번에 갖는 상태이며, 그래서 세 가지 누름을 한 목록에서 물을 수 있다.
    constexpr MenuCommandAvailability emptyEditor{ false, false, false, false, false };
    harness.SetAvailability(emptyEditor);

    const GameEngine::Runtime::RectTransform* const file = harness.Heading(0);
    const GameEngine::Runtime::RectTransform* const edit = harness.Heading(1);
    if (!Expect(file != nullptr && edit != nullptr, "the bar should carry File and Edit") ||
        !Expect(harness.Underneath() != nullptr, "and a button should sit under the menus"))
    {
        return false;
    }
    const float fileX = MidX(*file);
    const float fileY = MidY(*file);

    bool passed = true;

    // 목록은 막대의 자식이 아니다. 막대는 가로 ContentFit이라 자식을 머리줄로 세어 옆에
    // 늘어놓으므로, 목록이 그 안에 있으면 네 번째 머리줄이 되어 막대가 화면 끝까지 길어진다.
    const std::size_t barChildren =
        harness.View().GetBar().object
        ? harness.View().GetBar().object->GetTransform().GetChildren().size()
        : 0;
    passed = Expect(
        barChildren == GameEditor::GetEditorMenus().size(),
        "the bar should hold its headings and nothing else") && passed;

    // ⑴ 먼저 이 시험이 진짜 클릭을 만드는지부터 확인한다. 메뉴가 닫혀 있을 때 그 아래 버튼이
    //    눌리지 않는다면, 뒤에 나오는 「가려진 버튼이 눌리지 않는다」는 아무것도 재지 못한 채
    //    통과한다 — 통과하는 시험이 아무것도 재지 않는 것이 시험이 실패하는 가장 조용한 방식이다.
    static_cast<void>(harness.ClickAt(300.0f, 300.0f));
    passed = Expect(
        harness.Underneath()->WasClickedThisFrame(),
        "with no menu open, a click should reach the button beneath") && passed;

    // ⑵ 머리줄을 누르면 그 메뉴가 열리고, 같은 것을 다시 누르면 닫힌다.
    passed = Expect(!harness.View().IsOpen(), "nothing should be open to begin with") && passed;
    static_cast<void>(harness.ClickAt(fileX, fileY));
    passed = Expect(harness.View().IsOpen(), "clicking a heading should open its menu") && passed;
    static_cast<void>(harness.ClickAt(fileX, fileY));
    passed = Expect(!harness.View().IsOpen(), "clicking the same heading should close it") && passed;

    // ⑵ 다른 머리줄은 한 동작으로 옮겨 간다. Esc는 아무것도 고르지 않고 닫는다.
    static_cast<void>(harness.ClickAt(fileX, fileY));
    static_cast<void>(harness.ClickAt(MidX(*edit), MidY(*edit)));
    passed = Expect(harness.View().IsOpen(), "clicking another heading should move, not close") &&
        passed;
    harness.Cancel();
    passed = Expect(!harness.View().IsOpen(), "Esc should close the menu") && passed;

    // ── 여기서부터 File 메뉴가 열린 채다 ──────────────────────────────────
    static_cast<void>(harness.ClickAt(fileX, fileY));
    const GameEditor::MenuListParts* const list = harness.View().GetOpenList();
    if (!Expect(list != nullptr && list->rect != nullptr, "an open menu should have a list"))
    {
        return false;
    }

    // ⑶ 목록은 자기 머리줄 바로 아래에, 그 왼쪽에 맞춰 선다.
    const GameEngine::Runtime::RectTransform::Rect panel = list->rect->GetVisibleRect();
    const GameEngine::Runtime::RectTransform::Rect heading = file->GetVisibleRect();
    passed = Expect(
        std::fabs(panel.x - heading.x) < 0.01f &&
            std::fabs(panel.y - (heading.y + heading.height)) < 0.01f,
        "the open list should stand flush under its own heading") && passed;

    const MenuRowParts* enabledRow = nullptr;
    const MenuRowParts* separatorRow = nullptr;
    const MenuRowParts* disabledRow = nullptr;
    for (const MenuRowParts& row : list->rows)
    {
        if (row.isSeparator)
        {
            if (!separatorRow)
            {
                separatorRow = &row;
            }
            continue;
        }
        const bool enabled = GameEditor::IsEditorMenuCommandEnabled(row.command, emptyEditor);
        if (enabled && !enabledRow)
        {
            enabledRow = &row;
        }
        if (!enabled && !disabledRow)
        {
            disabledRow = &row;
        }
    }
    if (!Expect(
            enabledRow && separatorRow && disabledRow,
            "the File menu should offer an item, a separator and an item that is not available"))
    {
        return false;
    }

    // ⑷ 열린 목록이 덮은 버튼은 <b>함께 눌리지 않는다.</b> 목록이 자기가 덮은 자리를
    //    주장하지 않으면 그 아래가 대신 주장하고, 화면에서는 메뉴가 멀쩡해 보이는 채로
    //    엉뚱한 명령이 하나 더 돈다.
    static_cast<void>(harness.ClickAt(MidX(*separatorRow->rect), MidY(*separatorRow->rect)));
    passed = Expect(
        !harness.Underneath()->WasClickedThisFrame(),
        "a click inside the open list should not reach the button beneath it") && passed;

    // ⑸ 구분선을 눌러도 아무 일이 없고 메뉴는 열린 채로 남는다 — 그 누름은 목록을 겨눈
    //    것이지 떠난 것이 아니다.
    passed = Expect(
        harness.View().IsOpen(),
        "clicking a separator should choose nothing and leave the menu open") && passed;

    // ⑹ 고를 수 없는 줄도 마찬가지다. 그 줄은 커서를 가로채지 않으므로 클릭은 판으로 떨어진다.
    passed = Expect(
        !harness.ClickAt(MidX(*disabledRow->rect), MidY(*disabledRow->rect)).has_value() &&
            harness.View().IsOpen(),
        "clicking an unavailable item should choose nothing and leave the menu open") && passed;

    // ⑺ 그 줄은 사라지지 않고 흐려지며, 받지 않는다고 컴포넌트가 말한다.
    passed = Expect(
        disabledRow->label && disabledRow->label->GetColor() == GameEditor::DimTextColor &&
            disabledRow->button && !disabledRow->button->IsInteractable(),
        "an unavailable item should be dimmed and should not take the cursor") && passed;
    passed = Expect(
        enabledRow->label && enabledRow->label->GetColor() == GameEditor::TextColor &&
            enabledRow->button && enabledRow->button->IsInteractable(),
        "an available item should be bright and should take the cursor") && passed;

    // ⑻ 목록 안이지만 어느 줄도 아닌 자리 — 위아래 여백 — 도 「밖」이 아니다.
    passed = Expect(
        !harness.ClickAt(panel.x + 1.0f, panel.y + 1.0f).has_value() && harness.View().IsOpen(),
        "clicking the list's own inset should not close it") && passed;

    // ⑼ 고를 수 있는 줄을 누르면 그 명령이 나오고 메뉴가 닫힌다.
    const std::optional<EditorMenuCommand> chosen =
        harness.ClickAt(MidX(*enabledRow->rect), MidY(*enabledRow->rect));
    passed = Expect(
        chosen.has_value() && *chosen == enabledRow->command && !harness.View().IsOpen(),
        "choosing an item should report its command and close the menu") && passed;

    // ⑽ 메뉴 어디도 아닌 자리를 누르면 닫힌다. 「밖」은 좌표가 아니라 남은 것으로 정해진다 —
    //    이 클릭은 메뉴 밑의 그 버튼이 받는다.
    static_cast<void>(harness.ClickAt(fileX, fileY));
    static_cast<void>(harness.ClickAt(10.0f, SurfaceHeight - 100.0f));
    passed = Expect(
        !harness.View().IsOpen(), "a click that no menu element took should close the menu") &&
        passed;

    // ⑽½ Undo도 같은 규칙을 탄다. 되돌릴 것이 없으면 그 줄은 커서를 받지 않으므로, 눌러도
    //     아무 일이 없고 메뉴는 열린 채로 남는다 — 스택이 빈 편집기에서 Undo가 「눌리는데
    //     아무 일도 없는 항목」이면 사람은 그것을 고장으로 읽는다.
    static_cast<void>(harness.ClickAt(MidX(*edit), MidY(*edit)));
    const GameEditor::MenuListParts* const editList = harness.View().GetOpenList();
    const MenuRowParts* undoRow = nullptr;
    if (editList)
    {
        for (const MenuRowParts& row : editList->rows)
        {
            if (row.command == EditorMenuCommand::Undo && !row.isSeparator)
            {
                undoRow = &row;
            }
        }
    }
    passed = Expect(undoRow != nullptr, "the Edit menu should offer Undo") && passed;
    if (undoRow && undoRow->rect)
    {
        passed = Expect(
            !harness.ClickAt(MidX(*undoRow->rect), MidY(*undoRow->rect)).has_value() &&
                harness.View().IsOpen(),
            "pressing Undo with nothing to undo should do nothing and leave the menu open") &&
            passed;
    }
    harness.Cancel();

    // 모달이 열려 있으면 포인터 위치와 관계없이 메뉴 입력을 차단해야 한다.
    harness.RaiseModal();
    static_cast<void>(harness.ClickAt(fileX, fileY));
    passed = Expect(
        !harness.View().IsOpen(),
        "a modal standing anywhere should keep the menu from opening at all") && passed;

    return Expect(passed, "the menu bar's on-screen behaviour should hold");
}

static const TestSupport::Registration gEditorMenuBarInteractionTests{
    "EditorDocument", "editor menu bar interaction tests should pass",
    RunEditorMenuBarInteractionTests };
