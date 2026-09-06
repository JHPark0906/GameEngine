#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"
#include "SkinnedMeshData.h"

namespace GameEngine::Assets
{

/// <summary>임포터가 파일에서 읽어 낸 정점 하나이다. 엔진 레이아웃으로 변환되기 전의 형태다.</summary>
struct ImportedMeshVertex
{
    float position[3]{};
    float normal[3]{};
    float textureCoordinate[2]{};
};

/// <summary>임포터가 파일에서 읽어 낸 메시 하나이다. 이름과 파싱된 형상을 담는다.</summary>
struct ImportedMesh
{
    /// <summary>
    /// 파일 안에서 이 메시가 불리는 이름이다. 그래서 sub-asset을 위치만이 아니라 이름으로
    /// 보여주고 가리킬 수 있다. 파일이 이름을 붙이지 않았으면 비어 있다.
    /// </summary>
    std::string name;
    std::vector<ImportedMeshVertex> vertices;
    std::vector<unsigned int> indices;
};

/// <summary>바이너리 FBX 파일을 외부 라이브러리 없이 파싱해 안에 담긴 메시들을 내놓는다.</summary>
class FbxImporter final
{
public:
    /// <summary>
    /// Binary FBX 7.x 정적 메시를 좌수 좌표계와 센티미터 단위로 읽는다.
    ///
    /// 파일을 메시 하나로 합치는 대신 geometry 인스턴스마다 메시 하나를 내놓는다. 그래서 여러
    /// 메시를 담은 파일은 Unity의 모델 파일처럼 여러 에셋이 된다. 순서는 실행 간에 안정적이다 —
    /// geometry를 id 순으로 방문한다 — AssetReference의 로컬 id가 가리키는 것이 바로 그
    /// 순서이기 때문이다.
    /// </summary>
    [[nodiscard]] static bool Load(
        std::span<const std::byte> fileBytes,
        std::vector<ImportedMesh>& meshes,
        std::string& error);

    /// <summary>
    /// Binary FBX 7.x의 스킨드 메시·골격·애니메이션 클립을 같은 좌수 좌표계와 센티미터 단위로
    /// 읽는다. Skin이 없는 파일에서는 Model/조상의 TRS 애니메이션으로 움직이는 geometry를
    /// Model 뼈에 무게 1로 묶어 기존 Animator로 재생한다. 정적 메시의 <c>Load</c> 결과는 유지된다.
    /// 이 rigid 클립은 시작을 0초로 맞추며 Euler 곡선을 120Hz 이상, 회전 간격 45도 이하로
    /// 구워 여러 바퀴 회전을 보존한다. 선형/상수/비가중 user·time-independent auto cubic을 지원하고, 지원하지 않는
    /// tangent 모드는 오류로 알린다. 클립당 최대 262144개 TRS 샘플이며 morph는 포함하지 않는다.
    ///
    /// 정점은 <c>Load</c>가 정적 메시에 쓰는 것과 같은 공간에 놓인다: geometry를 인스턴스화한
    /// Model의 전체 전역 변환(geometric 포함)이 이미 구워져 있다. 골격의 뿌리 뼈들이 그 같은
    /// 전역 공간을 기준으로 서 있어야 스킨 행렬이 뜻대로 들어맞으므로, 그 정합은 이 함수 자신이
    /// 지킨다 — 호출자는 두 산출물을 그대로 짝지어 쓰면 된다.
    ///
    /// <paramref name="meshNames"/>는 <paramref name="meshes"/>와 나란하다. <c>SkinnedMeshData</c>
    /// 자신은 이름을 모르는 페이로드 타입이라(<c>MeshData</c>가 그렇듯) 이름이 필요한 호출자 —
    /// sub-asset 목록을 짓는 임포터 레지스트리 — 에게 따로 건넨다.
    /// <paramref name="rigidAnimation"/>은 rigid fallback 성공 때만 true다. 레지스트리가 기존
    /// 정적 메시의 localId를 보존하면서 rigid 산출물을 뒤에 붙일 수 있게 구분한다.
    /// </summary>
    [[nodiscard]] static bool LoadSkeleton(
        std::span<const std::byte> fileBytes,
        std::vector<SkinnedMeshData>& meshes,
        std::vector<std::string>& meshNames,
        Animation::Skeleton& skeleton,
        std::vector<Animation::AnimationClip>& clips,
        std::string& error,
        bool* rigidAnimation = nullptr);
};

}
