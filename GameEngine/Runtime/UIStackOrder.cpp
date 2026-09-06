#include "pch.h"
#include "UIStackOrder.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Canvas.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Selectable.h"
#include "Transform.h"
#include "UIWindow.h"

namespace GameEngine::Runtime
{

namespace
{
    template <typename TObject>
    void AppendBranch(TObject& object, UIStack<TObject>& stack, const UIWindow* window,
        const UIWindow* modalAncestor, bool retainedUI, std::vector<TObject*>& deferredWindows)
    {
        if (!object.IsActiveInHierarchy())
        {
            return;
        }
        if (const auto* ownWindow = object.template GetComponent<UIWindow>())
        {
            window = ownWindow;
            if (ownWindow->IsModal())
            {
                modalAncestor = ownWindow;
                stack.activeModal = ownWindow;
            }
        }
        retainedUI = retainedUI || window || object.template GetComponent<Canvas>();
        // World-only objects still lead to possible UI descendants, but need no stack entry.
        if (retainedUI || object.template GetComponent<Selectable>())
        {
            stack.entries.push_back({ &object, window, modalAncestor, retainedUI ? 1u : 0u });
        }
        for (const Transform* child : object.GetTransform().GetChildren())
        {
            if (child && child->GetGameObject())
            {
                TObject& childObject = *child->GetGameObject();
                if (childObject.template GetComponent<UIWindow>())
                {
                    deferredWindows.push_back(&childObject);
                }
                else
                {
                    AppendBranch(childObject, stack, window, modalAncestor, retainedUI, deferredWindows);
                }
            }
        }
    }

    template <typename TObject>
    void AppendWindowBranch(TObject& object, UIStack<TObject>& stack, const UIWindow* modalAncestor)
    {
        if (!object.IsActiveInHierarchy()) return;
        const auto* window = object.template GetComponent<UIWindow>();
        if (window->IsModal()) modalAncestor = window;
        // A window's ordinary descendants form one group below all of its nested window branches.
        std::vector<TObject*> nestedWindows;
        AppendBranch(object, stack, window, modalAncestor, true, nestedWindows);
        for (TObject* childWindow : nestedWindows)
        {
            AppendWindowBranch(*childWindow, stack, modalAncestor);
        }
    }

    template <typename TObject, typename TManager>
    [[nodiscard]] UIStack<TObject> BuildStack(TManager& scenes)
    {
        UIStack<TObject> stack;
        std::vector<TObject*> rootWindows;
        std::vector<unsigned int> sceneIds;
        sceneIds.reserve(scenes.GetActiveScenes().size());
        for (const auto& entry : scenes.GetActiveScenes())
        {
            sceneIds.push_back(entry.first);
        }
        std::ranges::sort(sceneIds);
        for (const unsigned int sceneId : sceneIds)
        {
            if (auto* scene = scenes.GetScene(sceneId))
            {
                for (auto* root : scene->GetRootGameObjects())
                {
                    if (root->template GetComponent<UIWindow>())
                    {
                        rootWindows.push_back(root);
                    }
                    else
                    {
                        AppendBranch<TObject>(*root, stack, nullptr, nullptr, false, rootWindows);
                    }
                }
            }
        }
        for (TObject* window : rootWindows)
        {
            AppendWindowBranch(*window, stack, nullptr);
        }
        if (stack.activeModal)
        {
            std::stable_partition(stack.entries.begin(), stack.entries.end(), [&stack](const auto& entry)
            {
                return entry.modalAncestor != stack.activeModal;
            });
        }
        std::uint64_t order = 0;
        for (auto& entry : stack.entries)
        {
            if (entry.overlayOrder != 0)
            {
                entry.overlayOrder = ++order;
            }
        }
        return stack;
    }
}

UIStack<GameObject> BuildUIStack(SceneManager& scenes)
{
    return BuildStack<GameObject>(scenes);
}

UIStack<const GameObject> BuildUIStack(const SceneManager& scenes)
{
    return BuildStack<const GameObject>(scenes);
}

}
