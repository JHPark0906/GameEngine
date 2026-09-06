#pragma once

#include <cstdint>
#include <vector>

namespace GameEngine::Runtime
{

class GameObject;
class SceneManager;
class UIWindow;

template <typename TObject>
struct UIStackEntry
{
    TObject* object = nullptr;
    const UIWindow* window = nullptr;
    const UIWindow* modalAncestor = nullptr;
    /// <summary>Canvas 또는 UIWindow 계층의 객체 묶음 순서다. 독립 화면 렌더러는 0이다.</summary>
    std::uint64_t overlayOrder = 0;
};

template <typename TObject>
struct UIStack
{
    std::vector<UIStackEntry<TObject>> entries;
    const UIWindow* activeModal = nullptr;
};

/// <summary>
/// 입력과 그리기가 공유하는 화면 UI 순서다. 장면 ID, 루트/자식의 계층 순서로 모으고,
/// 창은 창 밖 바탕 위에, 각 창의 일반 내용은 중첩 창 가지 아래에 놓는다. 최상위 모달은
/// 다른 창 위에 놓인다. 같은 객체의 렌더러는
/// 이 순서 안에서 자기 sortingOrder를 사용한다. 꺼진 가지는 포함하지 않는다.
/// </summary>
[[nodiscard]] UIStack<GameObject> BuildUIStack(SceneManager& scenes);
[[nodiscard]] UIStack<const GameObject> BuildUIStack(const SceneManager& scenes);

}
