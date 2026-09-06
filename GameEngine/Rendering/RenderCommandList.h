#pragma once

#include <span>
#include <string_view>

#include "ClipSpace.h"
#include "MeshDrawGeometry.h"
#include "QuadDrawGeometry.h"
#include "RenderFrame.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 백엔드가 GeometryHandle에 대해 resolve한 형상을 공용 렌더 패스가 바라보는 창구이다.
///
/// 패스는 밑에 있는 버퍼를 결코 만지지 않는다: 형상을 그릴 수 있는지만 묻고, 그 참조를 이것을
/// 만든 command list에 도로 건넨다. command list가 구체 타입을 아는 이유는, resolve된 리소스가
/// 언제나 그것을 만든 리졸버와 같은 백엔드의 command list에만 도달하기 때문이다.
/// </summary>
class IResolvedGeometry
{
public:
    virtual ~IResolvedGeometry() = default;

    /// <summary>이 항목이 draw에 쓸 수 있는 버퍼를 갖고 있는지 여부이다.</summary>
    [[nodiscard]] virtual bool IsDrawable() const = 0;

    /// <summary>이 항목을 만든 리졸버의 백엔드 이름이다. 예: "D3D11".</summary>
    [[nodiscard]] virtual const char* GetBackendName() const = 0;

protected:
    IResolvedGeometry() = default;
    IResolvedGeometry(const IResolvedGeometry&) = default;
    IResolvedGeometry& operator=(const IResolvedGeometry&) = default;
    IResolvedGeometry(IResolvedGeometry&&) = default;
    IResolvedGeometry& operator=(IResolvedGeometry&&) = default;
};

/// <summary>
/// 백엔드가 머티리얼 또는 래스터화된 텍스트 이미지에 대해 resolve한 텍스처이다.
///
/// 픽셀 크기가 인터페이스에 있는 이유는 quad 배치가 그것을 필요로 하기 때문이다: 스프라이트와
/// 텍스트 한 줄은 자기가 표시하는 텍스처로 크기가 정해지고, 그 계산은 백엔드마다가 아니라
/// 공용으로 존재한다.
/// </summary>
class IResolvedTexture
{
public:
    virtual ~IResolvedTexture() = default;

    /// <summary>이 항목이 draw에 쓸 수 있는 텍스처를 갖고 있는지 여부이다.</summary>
    [[nodiscard]] virtual bool IsDrawable() const = 0;

    /// <summary>이 항목을 만든 리졸버의 백엔드 이름이다. 예: "D3D11".</summary>
    [[nodiscard]] virtual const char* GetBackendName() const = 0;

    [[nodiscard]] virtual unsigned int GetPixelWidth() const = 0;
    [[nodiscard]] virtual unsigned int GetPixelHeight() const = 0;

    /// <summary>quad 배치가 소비하는 형태의 픽셀 크기이다.</summary>
    [[nodiscard]] QuadTextureSize GetQuadTextureSize() const
    {
        return { GetPixelWidth(), GetPixelHeight() };
    }

protected:
    IResolvedTexture() = default;
    IResolvedTexture(const IResolvedTexture&) = default;
    IResolvedTexture& operator=(const IResolvedTexture&) = default;
    IResolvedTexture(IResolvedTexture&&) = default;
    IResolvedTexture& operator=(IResolvedTexture&&) = default;
};

/// <summary>
/// 뒤에 있는 그래픽 API와 무관한, 한 프레임의 draw 제출 창구이다.
///
/// 렌더 패스가 백엔드마다 하나가 아니라 한 번만 존재할 수 있는 이유가 이것이다. 패스는 무엇을
/// 어디에 그릴지 결정한다 — 프레임을 읽고, 형상을 배치하고, 프레임이 기술할 수 없는 draw를
/// 거부한다. command list는 그것이 GPU에 어떻게 도달하는지를 결정한다: 파이프라인 상태, 상수
/// 저장소, 자기 API의 리소스 바인딩 모델을 소유한다.
///
/// 파이프라인 선택은 별도 호출이 아니라 draw 기록의 일부이므로, 한 파이프라인이 바인딩된 채
/// 다른 draw의 상수가 쓰이는 상태로 남는 일이 없다.
/// </summary>
class IRenderCommandList
{
public:
    virtual ~IRenderCommandList() = default;

    IRenderCommandList(const IRenderCommandList&) = delete;
    IRenderCommandList& operator=(const IRenderCommandList&) = delete;
    IRenderCommandList(IRenderCommandList&&) = delete;
    IRenderCommandList& operator=(IRenderCommandList&&) = delete;

    /// <summary>공용 패스가 내는 진단에 쓰이는 백엔드 이름이다. 예: "D3D11".</summary>
    [[nodiscard]] virtual const char* GetBackendName() const = 0;

    /// <summary>
    /// 이 API가 정점 셰이더 출력에 기대하는 클립 공간이다. 프레임의 투영은 언제나 엔진의 클립
    /// 공간이므로, 다른 공간을 쓰는 백엔드는 여기서 그렇게 선언하고 공용 패스가 보정한다 —
    /// 백엔드가 각자 자기 행렬을 고치는 것이 아니라.
    /// </summary>
    [[nodiscard]] virtual ClipSpaceConvention GetClipSpace() const = 0;

    /// <summary>
    /// 이 command list가 resolve된 리소스를 해석할 수 있는지 여부이다.
    ///
    /// command list는 제출을 위해 참조를 자기 구체 타입으로 되돌려 캐스팅하는데, 이것이 건전한
    /// 이유는 렌더러가 같은 API의 리졸버 하나와 command list 하나만 소유하기 때문이다. 그
    /// 불변식은 타입 시스템 밖에 있으므로, 믿는 대신 여기서 검사한다: 불일치는 배선 실수이고,
    /// 검사가 없었다면 캐스팅 지점의 미정의 동작이 됐을 것이다.
    /// </summary>
    template <typename TResolved>
    [[nodiscard]] bool Accepts(const TResolved& resource) const
    {
        return std::string_view(resource.GetBackendName()) == std::string_view(GetBackendName());
    }

    /// <summary>인덱스 메시 draw 하나를 기록한다. 백엔드가 건너뛰었으면 false를 반환한다.</summary>
    [[nodiscard]] virtual bool DrawMesh(
        const MeshShading& transform,
        const IResolvedGeometry& geometry,
        const IResolvedTexture& material) = 0;

    /// <summary>
    /// 인덱스 스킨드 메시 draw 하나를 기록한다. <see cref="DrawMesh"/>와 같지만, 정점이
    /// <c>boneMatrices</c>로 골격 공간에서 다시 움직여진다.
    ///
    /// <c>boneMatrices</c>는 <see cref="MaxSkinnedMeshBones"/>를 넘을 수 없다 — 셰이더의 상수
    /// 버퍼가 그 크기로 고정되어 있어, 넘긴 것은 백엔드가 조용히 자르는 대신 거부한다.
    /// </summary>
    [[nodiscard]] virtual bool DrawSkinnedMesh(
        const MeshShading& transform,
        const IResolvedGeometry& geometry,
        const IResolvedTexture& material,
        std::span<const Math::Matrix4x4> boneMatrices) = 0;

    /// <summary>
    /// 텍스처 입힌 quad draw 하나를 기록한다. 파이프라인 종류가 sprite 또는 text 파이프라인을
    /// 고르며, 둘은 소비하는 상수와 텍스처 샘플링 방식이 다르다.
    /// </summary>
    [[nodiscard]] virtual bool DrawQuad(
        PipelineKind pipelineKind,
        const QuadTransform& transform,
        const IResolvedTexture& texture) = 0;

    /// <summary>
    /// 같은 텍스처와 파이프라인을 쓰는 quad 여러 개를 한 번에 기록한다.
    ///
    /// 프레임 표현은 이미 타일맵 레이어와 텍스트 한 줄을 패킷 하나로 나르는데, 제출이 타일
    /// 하나·글리프 하나마다 호출로 흩어지면 그 배치는 백엔드에 닿기 전에 사라진다. 배치를
    /// 인터페이스에 두면 각 백엔드가 자기 API에서 최선인 방법 — 인스턴싱이든, 상수를 한 번에
    /// 준비하고 그리는 것이든 — 을 고를 수 있고, 공용 패스는 그 선택을 알 필요가 없다.
    ///
    /// 전부 기록했으면 true다. 프레임 제출 예산을 넘겨 일부만 기록했으면 false이며, 그때 어디서
    /// 멈추는지는 백엔드가 아니라 `RenderSubmissionPolicy`가 정한다 — 두 백엔드가 같은 자리에서
    /// 함께 멈추게 하는 것이 이 정책이 공용인 이유다.
    /// </summary>
    /// <param name="pipelineKind">이 배치가 쓸 파이프라인 종류이다.</param>
    /// <param name="transforms">그릴 quad들의 배치이다. 비어 있으면 아무것도 하지 않는다.</param>
    /// <param name="texture">배치 전체가 함께 보는 텍스처이다.</param>
    /// <returns>배치 전체를 기록했으면 true이다.</returns>
    [[nodiscard]] virtual bool DrawQuads(
        PipelineKind pipelineKind,
        std::span<const QuadTransform> transforms,
        const IResolvedTexture& texture) = 0;

    /// <summary>
    /// 패스가 끝난 뒤 이 백엔드가 바인딩한 채 남긴 상태를 복원한다. 파이프라인 상태를 command
    /// list 자체에 싣는 API는 여기서 아무것도 하지 않는다.
    /// </summary>
    virtual void EndPass(RenderPass pass) = 0;

protected:
    IRenderCommandList() = default;
};

}
