#pragma once
#include "Renderer.h"
#include "../Assets/AssetReference.h"
#include "../Math/Color.h"

#include <memory>
#include <utility>
#include <vector>

namespace GameEngine::Runtime
{

/// <summary>메시 형상을 장면에 출력하는 렌더러 컴포넌트이다.</summary>
class MeshRenderer final : public Renderer
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
    [[nodiscard]] const Assets::AssetReference& GetMesh() const { return mMesh; }

    void CollectAssetReferences(std::vector<Assets::AssetReference>& references) const override
    {
        Renderer::CollectAssetReferences(references);
        if (mMesh.IsValid())
        {
            references.push_back(mMesh);
        }
    }
    void SetMesh(Assets::AssetReference mesh) { mMesh = std::move(mesh); }

    /// <summary>월드 스프라이트와 같은 패스에서 SortingOrder 정렬에 참여한다. 메시 깊이 검사는 유지한다.</summary>
    [[nodiscard]] bool SortsWithSprites() const { return mSortWithSprites; }
    void SetSortWithSprites(bool sortWithSprites) { mSortWithSprites = sortWithSprites; }

    /// <summary>
    /// 재질 tint에 성분별로 곱하는 이 인스턴스의 색이다. 기본값은 흰색이다.
    /// 최종 알파가 0이면 그리지 않고, 0과 1 사이면 깊이 검사만 유지하며 알파 블렌딩한다.
    /// </summary>
    [[nodiscard]] const Math::Color& GetColor() const { return mColor; }
    void SetColor(const Math::Color& color) { mColor = color; }

private:
    Assets::AssetReference mMesh;
    bool mSortWithSprites = false;
    Math::Color mColor = Math::Color::White;
};

}
