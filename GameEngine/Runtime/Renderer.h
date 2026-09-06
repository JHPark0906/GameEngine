#pragma once
#include <filesystem>
#include <utility>
#include <vector>

#include "Behaviour.h"
#include "../Assets/AssetReference.h"

namespace GameEngine::Runtime
{

/// <summary>렌더링 가능한 컴포넌트가 공유하는 기반 형식이다.</summary>
class Renderer : public Behaviour
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
    [[nodiscard]] bool IsVisible() const { return mVisible; }
    void SetVisible(bool visible) { mVisible = visible; }

    [[nodiscard]] bool CastsShadows() const { return mCastShadows; }
    void SetCastShadows(bool castShadows) { mCastShadows = castShadows; }

    [[nodiscard]] bool ReceivesShadows() const { return mReceiveShadows; }
    void SetReceiveShadows(bool receiveShadows) { mReceiveShadows = receiveShadows; }

    [[nodiscard]] const Assets::AssetReference& GetMaterial() const { return mMaterial; }

    void CollectAssetReferences(std::vector<Assets::AssetReference>& references) const override
    {
        if (mMaterial.IsValid())
        {
            references.push_back(mMaterial);
        }
    }
    void SetMaterial(Assets::AssetReference material) { mMaterial = std::move(material); }

    [[nodiscard]] int GetSortingOrder() const { return mSortingOrder; }
    void SetSortingOrder(int sortingOrder) { mSortingOrder = sortingOrder; }

    /// <summary>현재 장면에서 이 렌더러를 그릴 수 있는 상태인지 확인한다.</summary>
    [[nodiscard]] bool IsRenderable() const { return mVisible && IsActiveAndEnabled(); }

private:
    Assets::AssetReference mMaterial;
    int mSortingOrder = 0;
    bool mVisible = true;
    bool mCastShadows = true;
    bool mReceiveShadows = true;
};

}
