#pragma once

// editor-layer: 2 (Views)

#include <cstddef>
#include <optional>
#include <vector>

#include "Rules/EditorMenuBarState.h"
#include "Rules/EditorMenuModel.h"

namespace GameEngine::Runtime
{
class Button;
class GameObject;
class RectTransform;
class Scene;
class SpriteRenderer;
class TextRenderer;
}

namespace GameEditor
{

/// <summary>머리줄 글자의 좌우 여백이다. 머리줄의 폭은 이 여백과 글자에서 나온다.</summary>
inline constexpr float MenuHeadingPadding = 6.0f;

/// <summary>
/// 펼친 목록의 한 줄에서 글자와 줄 테두리 사이의 여백이다. 줄의 <b>높이도</b> 여기서 나온다 —
/// 줄 높이를 따로 상수로 두면 글자가 커질 때 줄만 그대로 남아 글자가 넘친다.
/// </summary>
inline constexpr float MenuRowPadding = 4.0f;

/// <summary>
/// 항목 이름과 오른쪽 단축키 사이에 반드시 남는 간격이다. 이것이 없으면 이름이 긴 줄에서
/// 둘이 맞붙어 한 낱말처럼 읽힌다.
/// </summary>
inline constexpr float MenuShortcutGap = 24.0f;

/// <summary>펼친 목록의 안쪽 여백이다. 목록의 폭과 높이가 이만큼씩 자란다.</summary>
inline constexpr float MenuListInset = 4.0f;

/// <summary>구분선 한 줄이 차지하는 높이다. 항목보다 낮되 0은 아니다 — 선도 자리를 차지한다.</summary>
inline constexpr float MenuSeparatorHeight = 7.0f;

/// <summary>머리줄 하나가 쥐는 조각들이다.</summary>
struct MenuHeadingParts
{
    GameEngine::Runtime::GameObject* object = nullptr;
    GameEngine::Runtime::RectTransform* rect = nullptr;
    GameEngine::Runtime::TextRenderer* label = nullptr;

    /// <summary>
    /// 강조 바탕이다. 색 하나로 「지금 이것이 열려 있다」와 「여기 커서가 있다」를 말한다.
    ///
    /// 이것이 컴포넌트인 것이 곧 유지 모드 선택이다 — 같은 사각형을 즉시 모드로 한 번 더
    /// 그리면, 그 그림은 유지 모드 층 아래에 깔려 패널에 덮인다.
    /// </summary>
    GameEngine::Runtime::SpriteRenderer* background = nullptr;

    /// <summary>
    /// 이 머리줄이 눌리는 자리다. 커서가 여기 있는지를 묻는 것은 이벤트 시스템의 몫이며,
    /// 그래야 메뉴가 겹침과 창 규칙 위에 함께 실린다.
    /// </summary>
    GameEngine::Runtime::Button* button = nullptr;
};

/// <summary>메뉴 막대다. 머리줄들은 선언된 순서 그대로 왼쪽부터 놓인다.</summary>
struct MenuBarParts
{
    GameEngine::Runtime::GameObject* object = nullptr;
    GameEngine::Runtime::RectTransform* rect = nullptr;
    std::vector<MenuHeadingParts> headings;
};

/// <summary>펼친 목록의 한 줄이다. 구분선이면 글자가 없다.</summary>
struct MenuRowParts
{
    GameEngine::Runtime::GameObject* object = nullptr;
    GameEngine::Runtime::RectTransform* rect = nullptr;
    GameEngine::Runtime::TextRenderer* label = nullptr;
    /// <summary>단축키다. 적을 것이 없는 줄에서는 자식 자체를 만들지 않으므로 비어 있다.</summary>
    GameEngine::Runtime::TextRenderer* shortcut = nullptr;

    /// <summary>커서가 얹혔을 때의 강조 바탕이다. 구분선에는 없다.</summary>
    GameEngine::Runtime::SpriteRenderer* background = nullptr;

    /// <summary>
    /// 이 줄이 눌리는 자리다. 구분선에는 없다 — 누를 것이 없는 줄에 버튼을 두면 그 줄이
    /// 커서를 가로채면서 아무 일도 하지 않는다.
    ///
    /// 고를 수 없는 줄은 이 버튼이 <c>interactable</c>을 잃는다. 지우지 않는 이유는 그때
    /// 커서가 이 줄을 뚫고 목록 판으로 떨어져야 메뉴가 열린 채로 남기 때문이다.
    /// </summary>
    GameEngine::Runtime::Button* button = nullptr;

    /// <summary>이 줄이 부르는 명령이다. 구분선에서는 뜻이 없다.</summary>
    EditorMenuCommand command = EditorMenuCommand::NewProject;
    bool isSeparator = false;
};

/// <summary>펼친 목록이다. 줄들은 표의 순서 그대로이며 구분선도 그 자리에 들어 있다.</summary>
struct MenuListParts
{
    GameEngine::Runtime::GameObject* object = nullptr;
    GameEngine::Runtime::RectTransform* rect = nullptr;

    /// <summary>
    /// 목록 판 자체를 덮는 버튼이다. 아무 일도 하지 않고 <b>막기만</b> 한다.
    ///
    /// 두 가지가 여기에 달려 있다. 줄 사이의 여백을 누른 것이 「밖」이 되지 않는 것이 하나이고,
    /// 열린 목록 아래의 버튼이 함께 눌리지 않는 것이 다른 하나다 — 목록이 덮은 자리를 아무도
    /// 주장하지 않으면 그 아래가 대신 주장한다.
    /// </summary>
    GameEngine::Runtime::Button* blocker = nullptr;

    std::vector<MenuRowParts> rows;
};

/// <summary>
/// 메뉴 막대와 그 머리줄들을 세운다.
///
/// 폭을 여기서 만들지 않는다. 머리줄 하나는 가로 <c>LayoutGroup</c>이고 그 안의 글자가
/// <c>LayoutElement</c>로 자기를 재므로, 머리줄의 폭은 <b>글자에서 올라온다</b> — 툴바 버튼과
/// 같은 길이다. 막대는 가로 <c>ContentFit</c>이라 머리줄들을 그 요구 폭 그대로 왼쪽부터
/// 붙여 놓는다.
///
/// 글자에서 크기를 얻는 길이 엔진에 하나뿐이어야 하는 이유는, 두 번째 길이 생기면 그 둘이
/// 어긋나는 날 화면에서는 <b>둘 다 옳아 보이기</b> 때문이다 — 상자와 그 안의 글자가 서로 맞지
/// 않는 것으로만 나타나고, 어느 쪽이 틀렸는지는 코드에서만 알 수 있다.
/// </summary>
/// <param name="scene">막대가 놓일 장면이다.</param>
/// <param name="parent">막대가 매달릴 부모 오브젝트다.</param>
/// <param name="fontSize">머리줄 글자 크기다. 논리 단위이며 배율은 재는 쪽이 곱한다.</param>
[[nodiscard]] MenuBarParts BuildMenuBar(
    GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent, float fontSize);

/// <summary>
/// 메뉴 하나의 펼친 목록을 세운다.
///
/// 줄 하나는 가로 <c>LayoutGroup</c>이고 이름과 단축키가 그 자식이므로, 줄의 폭은 「이름 +
/// 간격 + 단축키」로 <b>올라온다</b>. 목록은 세로 <c>LayoutGroup</c>이자 <c>ContentFit</c>이며,
/// 가로지르는 방향에서 가장 큰 줄이 곧 목록의 폭이라는 것은 <c>LayoutGroup</c>의 계약 그 자체다
/// — 여기서 가장 긴 줄을 다시 고르지 않는다.
/// </summary>
/// <param name="scene">목록이 놓일 장면이다.</param>
/// <param name="parent">목록이 매달릴 부모 오브젝트다.</param>
/// <param name="menuIndex">표에서의 메뉴 번호다. 범위를 벗어나면 빈 목록이 나온다.</param>
/// <param name="fontSize">항목 글자 크기다. 논리 단위이며 배율은 재는 쪽이 곱한다.</param>
[[nodiscard]] MenuListParts BuildMenuList(
    GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
    std::size_t menuIndex, float fontSize);

/// <summary>
/// 이번 프레임에 사람이 무엇을 했는지다. <b>어디를</b>는 들어 있지 않다 — 그 물음의 답은
/// 이벤트 시스템이 이미 갖고 있고, 여기서 좌표를 다시 받으면 그것을 두 번 묻는 셈이 된다.
/// </summary>
struct MenuPointer
{
    /// <summary>
    /// 이번 프레임에 어딘가에서 클릭이 완성됐는지다. 자리가 아니라 입력 그 자체의 사실이며,
    /// 메뉴는 이것과 「내 요소 중 아무도 클릭되지 않았다」를 합쳐 <b>밖</b>을 안다.
    /// </summary>
    bool clicked = false;

    /// <summary>Esc다. 아무것도 고르지 않고 닫는다.</summary>
    bool cancelled = false;
};

/// <summary>
/// 메뉴 막대다. 무엇이 열려 있는지는 <see cref="EditorMenuBarState"/>가 알고, 그것이 화면의
/// 어디인지는 이 클래스가 안다.
///
/// 그리기가 <b>유지 모드</b>인 것은 의도된 선택이다. 펼친 목록은 그 아래의 모든 패널 위에
/// 떠야 하는데, 즉시 모드 층은 유지 모드 층 아래에 통째로 깔리므로 즉시 모드로 그린 목록은
/// 도킹된 패널에 덮인다. 게다가 강조는 줄의 배경색 하나이고, 그 색을 컴포넌트가 이미 그릴 수
/// 있다 — 같은 것을 두 경로로 그릴 이유가 없다.
///
/// 같은 이유로 <b>커서가 무엇 위에 있는지도 이 클래스가 재지 않는다.</b> 머리줄과 줄이
/// <see cref="GameEngine::Runtime::Button"/>이므로 그 물음은 이벤트 시스템의 것이고, 그래야
/// 겹침·창·모달 규칙이 메뉴에도 그대로 적용된다. 좌표를 직접 재면 그 규칙들이 메뉴만 비켜
/// 가며, 그 결과는 <b>열린 목록 아래의 버튼이 함께 눌리는</b> 것으로 나타난다 — 화면에서는
/// 메뉴가 멀쩡해 보이고 엉뚱한 명령이 하나 더 도는 것으로만 보인다.
///
/// 펼친 목록이 <see cref="GameEngine::Runtime::UIWindow"/>인 것도 그 규칙에 실리기 위해서다.
/// 목록은 자기가 덮은 것을 이겨야 하고, 그 「이긴다」는 쌓인 순서로 답해진다.
/// </summary>
class EditorMenuBarView final
{
public:
    /// <summary>막대와 메뉴마다의 목록을 세운다. 목록들은 닫힌 채로 시작한다.</summary>
    /// <param name="scene">막대가 놓일 장면이다.</param>
    /// <param name="parent">막대가 매달릴 부모 오브젝트다.</param>
    /// <param name="fontSize">글자 크기다. 논리 단위이며 배율은 재는 쪽이 곱한다.</param>
    void Build(
        GameEngine::Runtime::Scene& scene, GameEngine::Runtime::GameObject& parent,
        float fontSize);

    /// <summary>
    /// 이번 프레임의 포인터로 메뉴를 갱신하고, 골라진 명령이 있으면 그것을 돌려준다.
    ///
    /// <b>배치가 끝난 뒤에 불러야 한다.</b> 머리줄의 자리와 목록의 요구 크기를 배치에서 읽어
    /// 쓰기 때문이며, 그래서 세운 첫 프레임에는 아직 잴 것이 없다 — 툴바와 같은 두 프레임
    /// 춤이다.
    ///
    /// 「밖」의 판정이 여기 있는 이유는 자리를 아는 곳이 여기뿐이기 때문이다.
    /// <see cref="EditorMenuBarState"/>는 무엇이 열려 있는지만 알지 그것이 화면의 어디인지
    /// 모르므로, 그쪽에 판정을 두면 자리를 아는 곳이 둘이 된다.
    /// </summary>
    /// <param name="pointer">이번 프레임에 사람이 한 일이다. 자리는 들어 있지 않다.</param>
    /// <param name="availability">명령들이 지금 고를 수 있는지다.</param>
    /// <param name="clientWidth">창의 가로 폭이다. 목록이 이 밖으로 나가지 않는다.</param>
    /// <param name="scale">이 면의 픽셀 배율이다.</param>
    /// <returns>이번 프레임에 골라진 명령이다. 아무것도 고르지 않았으면 값이 없다.</returns>
    [[nodiscard]] std::optional<EditorMenuCommand> Update(
        const MenuPointer& pointer, const MenuCommandAvailability& availability,
        float clientWidth, float scale);

    /// <summary>펼쳐진 메뉴가 있는지다. 셸이 이 프레임의 포인터를 메뉴가 먹었는지 알아야 한다.</summary>
    [[nodiscard]] bool IsOpen() const { return mState.IsOpen(); }

    /// <summary>막대 자신의 사각형이다. 도킹이 비워 두어야 할 높이가 여기서 나온다.</summary>
    [[nodiscard]] const MenuBarParts& GetBar() const { return mBar; }

    /// <summary>지금 펼쳐진 목록이다. 닫혀 있으면 비어 있다.</summary>
    [[nodiscard]] const MenuListParts* GetOpenList() const;

private:
    /// <summary>이번 프레임에 클릭이 완성된 머리줄이다. 없으면 값이 없다.</summary>
    [[nodiscard]] std::optional<std::size_t> ClickedHeading() const;

    /// <summary>열린 목록을 머리줄 아래에 놓고, 닫힌 것들은 꺼 둔다.</summary>
    void PlaceOpenList(float clientWidth, float scale);

    /// <summary>줄이 지금 고를 수 있는지를 컴포넌트에 반영한다. 흐림도 여기서 나온다.</summary>
    void RefreshRows(const MenuCommandAvailability& availability);

    EditorMenuBarState mState;
    MenuBarParts mBar;
    std::vector<MenuListParts> mLists;
};

}
