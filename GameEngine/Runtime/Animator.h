#pragma once

#include <utility>
#include <vector>

#include "Renderer.h"
#include "../Assets/AssetReference.h"
#include "../Math/Color.h"

namespace GameEngine::Runtime
{

/// <summary>
/// 스킨드 메시를 골격 애니메이션으로 그리는 컴포넌트다. Renderer를 상속해 보이기·재질·정렬
/// 순서를 MeshRenderer와 같은 방식으로 다루되, 형상은 정적 메시가 아니라 뼈에 매인 정점을 가진
/// SkinnedMeshData다.
///
/// mesh·skeleton·clip은 material·MeshRenderer::mesh와 같은 모양으로 AssetReference다 —
/// 컴포넌트는 참조만 쥐고, 실제 페이로드는 그것을 쓰는 자리(SceneRendering)가 AssetDatabase로
/// 그때그때 resolve한다. 뼈 행렬도 그래서 여기 없다: 채점(Animation::SamplePose)은 골격과 클립
/// 둘 다가 resolve된 뒤에야 할 수 있는 일이고, 이 컴포넌트는 그 둘의 이름만 알 뿐이다 —
/// SceneRenderPass::CollectSkinnedMeshes가 매 프레임 그 자리에서 채점한다.
/// </summary>
class Animator final : public Renderer
{
public:
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }

    [[nodiscard]] const Assets::AssetReference& GetMesh() const { return mMesh; }
    void SetMesh(Assets::AssetReference mesh) { mMesh = std::move(mesh); }

    [[nodiscard]] const Assets::AssetReference& GetSkeleton() const { return mSkeleton; }
    void SetSkeleton(Assets::AssetReference skeleton) { mSkeleton = std::move(skeleton); }

    [[nodiscard]] const Assets::AssetReference& GetClip() const { return mClip; }
    void SetClip(Assets::AssetReference clip) { mClip = std::move(clip); }

    /// <summary>월드 스프라이트와 같은 패스에서 정렬 순서에 참여한다. 메시 깊이 검사는 유지한다.</summary>
    [[nodiscard]] bool SortsWithSprites() const { return mSortWithSprites; }
    void SetSortWithSprites(bool sortWithSprites) { mSortWithSprites = sortWithSprites; }

    /// <summary>재질 tint에 성분별로 곱하는 인스턴스 색이다. 부분 투명도는 깊이를 쓰지 않고 블렌딩한다.</summary>
    [[nodiscard]] const Math::Color& GetColor() const { return mColor; }
    void SetColor(const Math::Color& color) { mColor = color; }

    void CollectAssetReferences(std::vector<Assets::AssetReference>& references) const override
    {
        Renderer::CollectAssetReferences(references);
        if (mMesh.IsValid())
        {
            references.push_back(mMesh);
        }
        if (mSkeleton.IsValid())
        {
            references.push_back(mSkeleton);
        }
        if (mClip.IsValid())
        {
            references.push_back(mClip);
        }
    }

    /// <summary>재생 중인지 여부다. 멈추면 그 자리의 포즈에 머문다.</summary>
    [[nodiscard]] bool IsPlaying() const { return mPlaying; }
    void SetPlaying(const bool playing) { mPlaying = playing; }

    /// <summary>클립을 처음부터 다시 재생한다.</summary>
    void Restart();

    /// <summary>클립이 시작한 뒤 흐른 시간이다. 초 단위다.</summary>
    [[nodiscard]] float GetElapsedSeconds() const { return mElapsedSeconds; }
    /// <summary>외부 시계와 포즈를 맞출 재생 시간이다. 음수나 유한하지 않은 값은 0으로 정리한다.</summary>
    void SetElapsedSeconds(float elapsedSeconds);

private:
    void UpdateBehaviour(float deltaTime) override;

    Assets::AssetReference mMesh;
    Assets::AssetReference mSkeleton;
    Assets::AssetReference mClip;
    bool mPlaying = true;
    bool mSortWithSprites = false;
    Math::Color mColor = Math::Color::White;
    /// <summary>재생 시간이다. 장면에 저장되지 않는 런타임 상태다.</summary>
    float mElapsedSeconds = 0.0f;
};

}
