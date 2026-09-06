#pragma once

// editor-layer: 0 (Rules)

#include <cstdint>

#include "Assets/AssetReference.h"
#include "Runtime/PropertyDescriptor.h"
#include "UI/UIContext.h"

namespace GameEngine::Runtime
{
class Component;
}

namespace GameEditor
{

class ISceneTool;

/// <summary>
/// 논리 픽셀을 이 창의 픽셀로 바꾼다. 배율은 창이 주고 셸이 프레임마다 받으므로, 그것을 아는
/// 것은 셸 하나다 — 패널은 묻기만 한다.
/// </summary>
class IEditorScale
{
public:
    virtual ~IEditorScale() = default;

    [[nodiscard]] virtual float S(float logical) const = 0;
};

/// <summary>
/// 속성 편집이 지나는 길목이다. 다섯이 한 덩어리인 이유는 undo 병합 때문이다: 쓰기와 병합 키와
/// 필드 상태 버리기와 되돌리기는 한 이야기의 네 문장이라, 하나만 받은 쪽은 나머지를 자기 식으로
/// 다시 만들게 된다.
/// </summary>
class IPropertyEditHost
{
public:
    virtual ~IPropertyEditHost() = default;

    virtual void ApplyProperty(
        GameEngine::Runtime::Component& component,
        const GameEngine::Runtime::PropertyDescriptor& descriptor,
        const GameEngine::Runtime::PropertyValue& value,
        std::uint64_t mergeKey) = 0;

    [[nodiscard]] virtual std::uint64_t MakeMergeKey(GameEngine::UI::WidgetId source) const = 0;

    virtual void ResetFieldEditingState() = 0;

    virtual void PerformUndo() = 0;
    virtual void PerformRedo() = 0;
};

/// <summary>
/// 콘텐츠 브라우저에서 집은 에셋을 다른 패널에 놓는 동안의 상태에 접근한다.
/// 상태는 패널들을 함께 그리는 셸이 소유하고, 패널은 이 인터페이스를 통해 사용한다.
/// </summary>
class IAssetDragHost
{
public:
    virtual ~IAssetDragHost() = default;

    virtual void BeginAssetDrag(GameEngine::Assets::AssetReference reference) = 0;
    [[nodiscard]] virtual bool IsDraggingAsset() const = 0;
    [[nodiscard]] virtual const GameEngine::Assets::AssetReference& GetDraggedAsset() const = 0;
    [[nodiscard]] virtual GameEngine::Assets::AssetReference TakeDraggedAsset() = 0;
};

/// <summary>씬 뷰의 입력을 나눠 갖는 도구를 내준다. 어떤 도구인지는 씬 뷰의 관심 밖이다.</summary>
class ISceneToolHost
{
public:
    virtual ~ISceneToolHost() = default;

    [[nodiscard]] virtual ISceneTool& GetSceneTool() = 0;
};

}
