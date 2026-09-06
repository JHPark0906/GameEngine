#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include <variant>

#include "../Assets/MeshData.h"
#include "../Assets/SkinnedMeshData.h"
#include "../Assets/TextureData.h"
#include "../Math/Color.h"
#include "../Math/Matrix.h"
#include "../Math/Vector.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 이 파일의 계약: 프레임이 나르는 어떤 타입도 자기가 소유하지 않은 것을 생 포인터로 가리키지
/// 않는다. 값이거나 <c>shared_ptr&lt;const T&gt;</c>다.
///
/// 프레임은 만들어진 뒤 자기를 만든 장면보다 오래 산다 — 게임 스레드가 프레임을 넘기고 곧바로
/// 장면을 바꾸기 때문이다. 소유를 나눠 쥔 것은 그 변경을 견디지만, 생 포인터 하나면 렌더 쪽이
/// 이미 사라진 것을 읽는다. `GameEngineTests`의 프레임 포인터 계약 시험이 이 줄을 검사한다.
/// </summary>
struct RenderFrame;
class RenderFrameBuilder;

/// <summary>계약 검증에 성공했을 때만 만들어질 수 있는 프레임 능력 타입이다.</summary>
class ValidatedRenderFrame final
{
public:
    [[nodiscard]] const RenderFrame& GetFrame() const;

private:
    explicit ValidatedRenderFrame(const RenderFrame& frame) : mFrame(&frame) {}

    const RenderFrame* mFrame = nullptr;

    friend struct RenderFrame;
};

/// <summary>
/// 한 프레임 동안 캡처된 API 독립적 카메라 상태이다.
///
/// 투영은 엔진의 표준 클립 공간을 사용한다: 왼손 좌표계, +Y 위쪽, 깊이 [0, 1]. 다른 클립 공간을
/// 기대하는 API의 백엔드는 GPU로 가는 길에 스스로 변환한다. 프론트엔드는 백엔드 전용 행렬을
/// 만들지 않는다.
/// </summary>
struct CameraRenderData
{
    Math::Matrix4x4 view;
    Math::Matrix4x4 projection;
    /// <summary>이 프레임의 draw에 앞서 렌더 타깃을 지울 색이다.</summary>
    Math::Color clearColor = Math::Color::Black;
};

enum class LightKind : unsigned char
{
    Directional,
    Point,
};

/// <summary>
/// 한 프레임 동안 캡처된 API 독립적 광원이다. 프레임의 조명은 장면이 말한 그대로이며, 백엔드는
/// 여기에 없는 빛을 지어내지 않는다: 조명이 없는 프레임은 주변광만으로 그려진다.
///
/// 색에는 세기가 이미 곱해져 있다. 프론트엔드가 컴포넌트의 색과 세기를 하나로 접어 두면 두 백엔드가
/// 그것을 다르게 곱할 여지가 없다.
/// </summary>
struct LightRenderData
{
    LightKind kind = LightKind::Directional;
    /// <summary>빛이 나아가는 월드 방향이다. 단위 벡터이며 방향광에서만 쓰인다.</summary>
    Math::Vector3 direction{ 0.0f, 0.0f, 1.0f };
    /// <summary>월드 위치이다. 점광에서만 쓰인다.</summary>
    Math::Vector3 position;
    Math::Color color = Math::Color::White;
    /// <summary>점광이 닿는 월드 거리이다. 그 너머의 표면은 이 빛을 받지 않는다.</summary>
    float range = 10.0f;

    [[nodiscard]] bool IsValid() const
    {
        if (!direction.IsFinite() || !position.IsFinite() || !color.IsFinite() || !std::isfinite(range))
        {
            return false;
        }
        if (kind == LightKind::Directional)
        {
            return direction.GetLengthSquared() > 0.0f;
        }
        return kind == LightKind::Point && range > 0.0f;
    }
};

/// <summary>
/// 한 프레임이 실을 수 있는 광원 수의 상한이다. 메시 셰이더도 같은 수의 광원 슬롯을 가진다.
/// 프론트엔드는 넘치는 광원을 버리고, 무엇을 버릴지는 프론트엔드가 정한다.
/// </summary>
inline constexpr std::size_t MaxFrameLights = 3;

/// <summary>프레임이 구성될 때 대상으로 삼은 렌더 타깃의 픽셀 크기이다.</summary>
struct RenderTargetSize
{
    unsigned int width = 0;
    unsigned int height = 0;

    [[nodiscard]] bool IsValid() const { return width > 0 && height > 0; }

    [[nodiscard]] float GetAspectRatio() const
    {
        return IsValid() ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    }
};

/// <summary>
/// 렌더링된 한 프레임의 픽셀이다. RGBA8, 위에서 아래 순서다.
///
/// 오프스크린 렌더링 결과를 에디터 뷰와 백엔드 비교에 사용한다. 데스크톱 화면 캡처와 달리
/// 다른 창의 가림이나 컴포지터에 의존하지 않으며, 디스플레이가 없는 머신에서도 읽을 수 있다.
/// </summary>
struct CapturedImage
{
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<std::byte> pixels;

    static constexpr unsigned int BytesPerPixel = 4;

    [[nodiscard]] std::size_t GetByteSize() const
    {
        return static_cast<std::size_t>(width) * height * BytesPerPixel;
    }

    [[nodiscard]] bool IsValid() const
    {
        return width > 0 && height > 0 && pixels.size() == GetByteSize();
    }
};

/// <summary>프레임 내부 형상 테이블을 참조하는 핸들이다.</summary>
struct GeometryHandle
{
    static constexpr std::uint32_t InvalidIndex = (std::numeric_limits<std::uint32_t>::max)();

    std::uint32_t index = InvalidIndex;

    [[nodiscard]] bool IsValid() const { return index != InvalidIndex; }
};

/// <summary>프레임 내부 머티리얼 테이블을 참조하는 핸들이다.</summary>
struct MaterialHandle
{
    static constexpr std::uint32_t InvalidIndex = (std::numeric_limits<std::uint32_t>::max)();

    std::uint32_t index = InvalidIndex;

    [[nodiscard]] bool IsValid() const { return index != InvalidIndex; }
};

/// <summary>현재 Draw가 요구하는 API 독립적 그래픽 파이프라인 종류이다.</summary>
enum class PipelineKind : unsigned char
{
    Mesh,
    Sprite,
    Text,
    SkinnedMesh,
};

/// <summary>프레임 내부 파이프라인 테이블을 참조하는 핸들이다.</summary>
struct PipelineHandle
{
    static constexpr std::uint32_t InvalidIndex = (std::numeric_limits<std::uint32_t>::max)();

    std::uint32_t index = InvalidIndex;

    [[nodiscard]] bool IsValid() const { return index != InvalidIndex; }
};


/// <summary>
/// 한 draw를 위한 완성된 메시 형상이다. 파일 경로가 아니라 임포트된 정점을 담는다: 텍스트 draw가
/// 폰트 이름이 아니라 래스터화된 픽셀을 나르듯, 백엔드는 프레임이 실어 온 것을 업로드할 뿐 모델
/// 파일을 열지 않는다.
/// </summary>
struct Geometry
{
    std::shared_ptr<const Assets::MeshData> data;
};

/// <summary>프레임 내부 스킨드 형상 테이블을 참조하는 핸들이다. <see cref="GeometryHandle"/>과 별개
/// 테이블이다: 스키닝된 정점은 바이트 레이아웃이 달라 같은 핸들 공간에 섞으면 어느 정점 형식으로
/// 읽을지가 데이터 밖에서 정해져야 한다.</summary>
struct SkinnedGeometryHandle
{
    static constexpr std::uint32_t InvalidIndex = (std::numeric_limits<std::uint32_t>::max)();

    std::uint32_t index = InvalidIndex;

    [[nodiscard]] bool IsValid() const { return index != InvalidIndex; }
};

/// <summary>한 draw를 위한 완성된, 뼈 영향 정보를 가진 메시 형상이다. <see cref="Geometry"/>와
/// 같은 이유로 파일 경로가 아니라 임포트된 정점을 담는다.</summary>
struct SkinnedGeometry
{
    std::shared_ptr<const Assets::SkinnedMeshData> data;
};

/// <summary>
/// 한 draw를 위한 완성된 기본 색상 픽셀이다. 파일 경로가 아니라 디코딩된 이미지를 담으므로,
/// 백엔드는 프레임이 실어 온 것을 업로드할 뿐 이미지 파일을 열지 않는다.
/// </summary>
struct Material
{
    std::shared_ptr<const Assets::TextureData> baseColorTexture;
};

/// <summary>백엔드가 API별 상태로 해석할 파이프라인 설명이다.</summary>
struct Pipeline
{
    PipelineKind kind = PipelineKind::Mesh;
};

/// <summary>한 프레임 동안 캡처된 API 독립적 메시 draw 요청이다.</summary>
struct MeshDraw
{
    PipelineHandle pipeline;
    GeometryHandle geometry;
    MaterialHandle material;
    Math::Matrix4x4 localToWorld;
    /// <summary>
    /// 알베도 텍스처에 곱해질 재질과 인스턴스의 색이다.
    /// 기본값은 흰색이므로 명시적으로 채우지 않으면 텍스처 색을 유지한다.
    /// </summary>
    Math::Color tint = Math::Color::White;

    /// <summary>
    /// 부분 투명도는 source-alpha 블렌딩과 깊이 검사만 사용하고 깊이를 쓰지 않는다.
    /// 0 이하인 알파는 공통 메시 패스가 그리기를 생략한다. 텍스처 알파만으로는 모드를 바꾸지 않는다.
    /// </summary>
    [[nodiscard]] bool UsesAlphaBlending() const { return tint.a < 1.0f; }
};

/// <summary>
/// 한 프레임 동안 캡처된 API 독립적 스킨드 메시 draw 요청이다. <see cref="MeshDraw"/>에 뼈 행렬
/// 배열이 더해진 모양이다.
///
/// 뼈 행렬은 리소스 리졸버를 거치지 않는다: 지오메트리·머티리얼은 자산 id로 캐시해 재사용하는
/// 정적 데이터지만, 뼈 행렬은 포즈가 바뀔 때마다 — 보통 매 프레임 — 다시 계산되는 값이라 캐시할
/// 것이 없다. <c>MeshShading</c>이 매 프레임 새로 만들어지는 것과 같은 이유로, draw 자체에
/// 싣는다.
/// </summary>
struct SkinnedMeshDraw
{
    PipelineHandle pipeline;
    SkinnedGeometryHandle geometry;
    MaterialHandle material;
    Math::Matrix4x4 localToWorld;
    /// <summary>
    /// 뼈마다 하나씩, 골격의 뼈 배열과 같은 순서인 <b>오브젝트 로컬(모델) 공간</b> 스키닝
    /// 행렬이다 — 바인드 포즈의 역행렬에 지금 포즈의 뼈-로컬 변환을 곱한 것으로, 오브젝트
    /// 자신의 <see cref="localToWorld"/>는 여기 섞이지 않는다. 그래서 오브젝트를 옮기거나
    /// 돌리는 것은 정적 메시와 똑같이 <c>localToWorld</c> 하나로 되고, 애니메이터는 자신이
    /// 세상 어디에 있는지 몰라도 된다. 정점의 <c>boneIndices</c>가 이 배열의 인덱스다. 비어
    /// 있으면 이 draw는 유효하지 않다.
    /// </summary>
    std::shared_ptr<const std::vector<Math::Matrix4x4>> boneMatrices;
    /// <summary><see cref="MeshDraw::tint"/>와 같다.</summary>
    Math::Color tint = Math::Color::White;
    /// <summary>정적 메시와 같은 부분 투명도 정책이다.</summary>
    [[nodiscard]] bool UsesAlphaBlending() const { return tint.a < 1.0f; }
};

/// <summary>
/// 스프라이트의 nine-slice 테두리이다. 각 변에서 잰 픽셀 단위이며, 단위는 draw의 공간을 따른다:
/// 월드 draw는 텍스처 픽셀이고, 화면 draw는 프론트엔드가 화면 배율을 이미 곱해 넘긴 화면
/// 픽셀이다. <c>size</c>가 같은 규칙을 따르므로 둘은 언제나 같은 단위다.
///
/// UV 비율이 아니라 픽셀인 이유는 테두리가 그렇게 저작되기 때문이다: 스프라이트의 메타데이터는
/// "이 패널의 틀은 4픽셀 폭"이라고 말하고, 그 말은 이미지가 어떤 크기로 임포트되든 참으로 남는다.
/// </summary>
struct SpriteBorder
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    [[nodiscard]] bool IsValid() const
    {
        return std::isfinite(left) && left >= 0.0f && std::isfinite(top) && top >= 0.0f &&
            std::isfinite(right) && right >= 0.0f && std::isfinite(bottom) && bottom >= 0.0f;
    }
};

/// <summary>
/// 텍스처 안의 정규화된 사각형이다. 0..1 좌표이며 원점은 좌상단이라, UV로 그대로 읽힌다.
/// </summary>
struct SpriteUVRect
{
    float u = 0.0f;
    float v = 0.0f;
    float width = 1.0f;
    float height = 1.0f;

    /// <summary>이미지 전체를 가리키는지 여부다. 시트가 아닌 스프라이트가 이것이다.</summary>
    [[nodiscard]] bool IsWholeImage() const
    {
        return u == 0.0f && v == 0.0f && width == 1.0f && height == 1.0f;
    }

    [[nodiscard]] bool IsValid() const
    {
        return std::isfinite(u) && std::isfinite(v) && std::isfinite(width) &&
            std::isfinite(height) && width > 0.0f && height > 0.0f &&
            u >= 0.0f && v >= 0.0f && u + width <= 1.0001f && v + height <= 1.0001f;
    }
};

/// <summary>draw가 어느 공간에 놓이는지다: 월드인가 렌더 타깃 픽셀인가.</summary>
enum class DrawSpace : unsigned char
{
    /// <summary>draw가 월드 단위의 로컬 변환과 활성 카메라를 사용한다.</summary>
    World,
    /// <summary>draw가 렌더 타깃 좌상단을 원점으로 하는 픽셀 좌표를 사용한다. 카메라를 쓰지 않는다.</summary>
    Screen,
};

/// <summary>
/// 텍스트 호출부가 사용하는 DrawSpace의 호환 별칭이다.
/// </summary>
using TextSpace = DrawSpace;

/// <summary>한 프레임 동안 캡처된 API 독립적 스프라이트 draw 요청이다.</summary>
struct SpriteDraw
{
    PipelineHandle pipeline;
    MaterialHandle material;
    Math::Matrix4x4 localToWorld;
    Math::Color tint = Math::Color::White;
    float pixelsPerUnit = 100.0f;

    /// <summary>
    /// 이 스프라이트가 월드에 놓이는지 화면 픽셀에 놓이는지다. 화면 공간이면 `localToWorld`는
    /// 렌더 타깃 좌상단이 원점인 픽셀 변환이고 카메라를 전혀 쓰지 않으며, `pixelsPerUnit`은
    /// 무시된다 — 크기가 이미 픽셀이기 때문이다. 장면 위의 오버레이 패널은 이 화면 공간을 쓴다.
    /// </summary>
    DrawSpace space = DrawSpace::World;
    bool flipX = false;
    bool flipY = false;

    /// <summary>
    /// 스프라이트를 아홉 조각으로 늘일지 여부이다: 모서리는 제 크기를 지키고, 가장자리는 자기
    /// 방향으로 늘어나며, 가운데가 나머지를 채운다. simple 스프라이트는 `border`와 `size`를
    /// 무시하고 텍스처에서 자기 크기를 얻는다.
    /// </summary>
    bool sliced = false;
    /// <summary>
    /// 아홉 조각의 테두리이며, <b>텍스처 픽셀</b>이다.
    ///
    /// 이 단위가 계약인 이유는 이 값이 UV를 정하기 때문이다: 조각이 그림의 어느 부분을 읽는지는
    /// <c>border / 텍스처 크기</c>로 나오므로, 여기에 화면 배율이 섞여 들어오면 모서리가 그림의
    /// 엉뚱한 넓이를 읽는다. 화면에서 조각을 얼마나 크게 그릴지는 <see cref="borderScale"/>이
    /// 따로 말한다.
    /// </summary>
    SpriteBorder border;
    /// <summary>
    /// 테두리를 화면에서 얼마나 크게 그릴지의 배수이며, <b>기하에만</b> 쓰인다. 기본은 1이다.
    ///
    /// 화면 공간 UI는 200% 화면에서 모서리도 두 배로 그려야 그림이 같은 모양으로 커진다. 그
    /// 곱셈을 <see cref="border"/>에 미리 해 두면 UV가 함께 어긋나므로, 곱하는 쪽과 읽는 쪽이
    /// 서로의 값을 건드리지 않도록 여기 따로 싣는다.
    /// </summary>
    float borderScale = 1.0f;
    /// <summary>
    /// sliced 스프라이트가 늘어나 채울 크기이다. sliced draw에서만 쓰이며, 단위는 draw의 공간을
    /// 따른다: 월드 draw는 월드 단위, 화면 draw는 화면 픽셀이다.
    /// </summary>
    Math::Vector2 size{ 1.0f, 1.0f };

    /// <summary>
    /// 텍스처에서 이 draw가 보여 줄 사각형이다. 0..1 정규화 좌표, 원점은 좌상단이며, 기본값은
    /// 이미지 전체다. 스프라이트 시트의 한 프레임이 이것으로 실린다 — 아틀라스 텍스트의 글리프
    /// quad가 페이지의 일부를 그리는 것과 같은 방식이라, 백엔드에는 "시트"라는 개념이 없다.
    ///
    /// nine-slice와는 함께 쓰지 않는다: 늘어나는 아홉 조각의 경계는 텍스처 전체를 기준으로
    /// 계산되므로, sliced draw는 이 사각형을 무시하고 이미지 전체를 쓴다. 프론트엔드도 둘 중
    /// 하나만 싣는다.
    /// </summary>
    SpriteUVRect uvRect;
};


/// <summary>
/// 타일맵의 칸 하나다: 격자에서의 자리와, 타일셋에서 그 칸이 보여 줄 사각형이다. 빈 칸은 실리지
/// 않으므로 목록에 있는 것은 모두 그려진다.
/// </summary>
struct TilemapTile
{
    int column = 0;
    int row = 0;
    SpriteUVRect uv;

    [[nodiscard]] bool IsValid() const { return column >= 0 && row >= 0 && uv.IsValid(); }
};

/// <summary>
/// 한 프레임 동안 캡처된 API 독립적 타일맵 draw 요청이다.
///
/// 타일 하나하나가 draw 패킷이 되면 격자 하나가 수천 패킷이 되고, 정렬과 검증이 그 수만큼
/// 반복된다. 그래서 한 레이어가 패킷 하나이고, 그 안에 같은 타일셋을 쓰는 칸들이 함께 실린다 —
/// 아틀라스 텍스트가 글리프 quad들을 한 패킷에 싣는 것과 같은 모양이며, 백엔드에는 "타일맵"
/// 이라는 개념이 필요 없다: 같은 텍스처로 quad 여럿을 그릴 뿐이다.
/// </summary>
struct TilemapDraw
{
    PipelineHandle pipeline;
    /// <summary>타일셋 텍스처다. 한 패킷의 모든 칸이 이것을 공유한다.</summary>
    MaterialHandle material;
    /// <summary>타일맵의 원점 변환이다. 칸 (0, 0)의 왼쪽 아래 모서리가 이 원점이다.</summary>
    Math::Matrix4x4 localToWorld;
    Math::Color tint = Math::Color::White;
    /// <summary>칸 하나의 크기다. 월드 단위이며 양수다.</summary>
    Math::Vector2 cellSize{ 1.0f, 1.0f };
    /// <summary>그릴 칸들이다. 프론트엔드 캐시와 공유되는 완성 데이터다.</summary>
    std::shared_ptr<const std::vector<TilemapTile>> tiles;
};

/// <summary>
/// 프론트엔드가 한 텍스트 draw를 위해 래스터화한 커버리지 픽셀이다. 픽셀당 1바이트, 위에서 아래
/// 순서다.
///
/// 프레임이 폰트 설명이 아니라 완성된 픽셀을 나르는 이유는, 문자열을 픽셀로 바꾸는 일이 그래픽스
/// API의 일이 아니라 플랫폼의 일이기 때문이다: 줄바꿈, 정렬, 힌팅이 어느 백엔드가 그리는지에
/// 의존해서는 안 된다. 백엔드는 이것을 업로드할 뿐 그 이상 아무것도 하지 않는다.
///
/// draw들은 shared_ptr로 이미지를 공유하므로 같은 문자열의 반복은 래스터 한 번 비용이고, 백엔드가
/// 끝나기 전에 프론트엔드 캐시가 항목을 퇴거해도 프레임은 유효하게 남는다.
/// </summary>
struct RasterizedTextImage
{
    /// <summary>
    /// 백엔드 GPU 캐시를 위한 정체성이다. 하나의 id가 다른 픽셀에 재사용되는 일은 없으므로,
    /// 백엔드는 id에 대고 업로드를 캐시해도 된다.
    /// </summary>
    std::uint64_t id = 0;
    /// <summary>
    /// 같은 id 아래에서 픽셀이 바뀔 때마다 오른다. 글리프 아틀라스 페이지가 새 글리프를 받아
    /// 넣는 것이 그 경우다: 이미 쓰인 슬롯의 픽셀은 결코 바뀌지 않고 빈 자리만 채워지므로,
    /// 백엔드는 revision이 달라졌을 때 같은 텍스처에 다시 올리기만 하면 된다. 크기는 id의
    /// 일부다 — 크기가 바뀌면 새 id다.
    /// </summary>
    std::uint64_t revision = 1;
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<std::byte> alphaPixels;

    [[nodiscard]] bool IsValid() const
    {
        return id != 0 && width > 0 && height > 0 &&
            alphaPixels.size() == static_cast<std::size_t>(width) * height;
    }
};

/// <summary>
/// Draw 패킷은 런타임 컴포넌트 종류가 아니라 렌더링 단계로 묶인다.
///
/// 게임 공간과 오버레이 공간은 구분된다: Opaque와 Transparent는 카메라가 보는 세계이고,
/// Overlay는 그 위에 렌더 타깃 픽셀로 놓이는 UI — 점수, 대화, HUD — 다. 둘을 나누는 이유는
/// 뷰가 다르게 취급하기 때문이다. 게임 뷰는 둘 다 보이지만, 편집 카메라로 세계를 보는 씬 뷰는
/// 세계만 본다. 오버레이는 카메라와 무관하므로 편집 카메라에서도 같은 자리에 붙어 세계를
/// 가릴 뿐이다.
/// </summary>
enum class RenderPass : unsigned char
{
    Opaque,
    Transparent,
    /// <summary>카메라와 무관하게 렌더 타깃 위에 놓이는 화면 공간 UI다. 마지막에 그려진다.</summary>
    Overlay,
    Count,
};

/// <summary>
/// 텍스트 블록 안의 글리프 하나가 어디 놓이고 아틀라스의 어느 픽셀을 보여주는지다. 좌표는
/// 블록 중심 원점의 로컬 픽셀이고 +y가 블록 위쪽이다 — draw의 변환이 블록을 중심으로
/// 배치하는 것과 짝이 맞는 표현이다.
/// </summary>
struct TextGlyphQuad
{
    float centerX = 0.0f;
    float centerY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    /// <summary>아틀라스 페이지 안에서 이 글리프가 차지하는 UV 사각형이다.</summary>
    float u = 0.0f;
    float v = 0.0f;
    float uWidth = 0.0f;
    float vHeight = 0.0f;

    [[nodiscard]] bool IsValid() const
    {
        return std::isfinite(centerX) && std::isfinite(centerY) &&
            std::isfinite(width) && width > 0.0f && std::isfinite(height) && height > 0.0f &&
            std::isfinite(u) && std::isfinite(v) &&
            std::isfinite(uWidth) && uWidth > 0.0f && std::isfinite(vHeight) && vHeight > 0.0f;
    }
};

/// <summary>
/// 한 프레임 동안 캡처된 API 독립적 텍스트 draw 요청이다. 아틀라스 페이지 하나와 거기서 그릴
/// 글리프 quad들을 실을 뿐이며, 그 배치를 만든 폰트 패밀리, 크기, 줄바꿈 폭, 줄 간격, 정렬은
/// 프론트엔드가 소비하고 백엔드에는 결코 도달하지 않는다. 여러 페이지에 걸친 문자열은
/// 프론트엔드가 페이지마다 하나씩의 draw로 쪼갠다.
/// </summary>
struct TextDraw
{
    PipelineHandle pipeline;
    /// <summary>글리프들이 사는 아틀라스 페이지다. 프레임과 shared_ptr로 공유된다.</summary>
    std::shared_ptr<const RasterizedTextImage> page;
    /// <summary>이 페이지에서 그릴 글리프들이다. 프론트엔드 캐시와 공유되는 완성 데이터다.</summary>
    std::shared_ptr<const std::vector<TextGlyphQuad>> glyphs;
    Math::Matrix4x4 localToWorld;
    Math::Color tint = Math::Color::White;
    /// <summary>월드 공간 텍스트의 월드 단위당 픽셀 수이다. 스크린 공간 텍스트는 항상 픽셀 치수를 쓴다.</summary>
    float pixelsPerUnit = 100.0f;
    TextSpace space = TextSpace::Screen;
};

using DrawPayload = std::variant<MeshDraw, SpriteDraw, TextDraw, TilemapDraw, SkinnedMeshDraw>;

/// <summary>
/// API 독립적 draw 요청이다. 새 렌더링 기능은 payload 생산자와 파이프라인 지원만 추가하면 되고,
/// 그래픽 장치 인터페이스나 RenderFrame의 저장 구조는 바뀌지 않는다.
/// </summary>
struct DrawPacket
{
    /// <summary>렌더 패스 안에서 패킷 순서를 정하는 단 하나의 권위 있는 키이다.</summary>
    RenderPass pass = RenderPass::Opaque;
    int sortingOrder = 0;
    unsigned int instanceId = 0;
    /// <summary>
    /// draw 원점의 카메라 공간 깊이다. Transparent 패스는 같은 sortingOrder 안에서 먼 것부터
    /// 그리고, 그것이 블렌딩이 맞게 겹치는 유일한 순서다. 다른 패스는 이 값을 쓰지 않는다.
    /// builder가 프레임의 카메라로 계산하므로 프론트엔드는 이것을 채우지 않는다.
    /// </summary>
    float viewDepth = 0.0f;
    DrawPayload payload;
    /// <summary>Ordering of overlay object groups; zero denotes an ungrouped draw.</summary>
    std::uint64_t overlayStackOrder = 0;
};

/// <summary>
/// Overlay object groups precede renderer sortingOrder. Within a group, sortingOrder is ascending,
/// Transparent viewDepth is descending, and instanceId breaks ties. Equal keys retain insertion order.
/// </summary>
[[nodiscard]] inline bool DrawPacketPrecedes(const DrawPacket& left, const DrawPacket& right)
{
    if (left.pass == RenderPass::Overlay && left.overlayStackOrder != right.overlayStackOrder)
    {
        return left.overlayStackOrder < right.overlayStackOrder;
    }
    if (left.sortingOrder != right.sortingOrder)
    {
        return left.sortingOrder < right.sortingOrder;
    }
    if (left.pass == RenderPass::Transparent && left.viewDepth != right.viewDepth)
    {
        return left.viewDepth > right.viewDepth;
    }
    return left.instanceId < right.instanceId;
}

enum class DrawPacketKind : unsigned char
{
    Mesh,
    Sprite,
    Text,
    Tilemap,
    SkinnedMesh,
};

namespace Detail
{
    /// <summary>형식에 의존하는 거짓이다. 인스턴스화된 가지에서만 static_assert가 터지게 한다.</summary>
    template <typename T>
    inline constexpr bool AlwaysFalse = false;
}

/// <summary>
/// payload 형식 하나가 어느 <see cref="DrawPacketKind"/>인지이다.
///
/// 모든 payload는 이 특성을 명시적으로 정의해야 한다. 형식에 대응이 없으면 컴파일이 실패하므로,
/// 새 형식이 다른 종류로 조용히 분류되지 않는다.
/// </summary>
/// <typeparam name="T">DrawPayload variant의 대안 하나이다.</typeparam>
template <typename T>
struct DrawTraits
{
    static_assert(
        Detail::AlwaysFalse<T>,
        "A new DrawPayload alternative needs a DrawTraits specialization naming its "
        "DrawPacketKind.");
};

template <>
struct DrawTraits<MeshDraw>
{
    static constexpr DrawPacketKind Kind = DrawPacketKind::Mesh;
};

template <>
struct DrawTraits<SpriteDraw>
{
    static constexpr DrawPacketKind Kind = DrawPacketKind::Sprite;
};

template <>
struct DrawTraits<TextDraw>
{
    static constexpr DrawPacketKind Kind = DrawPacketKind::Text;
};

template <>
struct DrawTraits<TilemapDraw>
{
    static constexpr DrawPacketKind Kind = DrawPacketKind::Tilemap;
};

template <>
struct DrawTraits<SkinnedMeshDraw>
{
    static constexpr DrawPacketKind Kind = DrawPacketKind::SkinnedMesh;
};

[[nodiscard]] inline DrawPacketKind GetDrawPacketKind(const DrawPacket& packet)
{
    return std::visit([](const auto& draw)
    {
        return DrawTraits<std::decay_t<decltype(draw)>>::Kind;
    }, packet.payload);
}

enum class RenderFrameValidationError : unsigned char
{
    None,
    InvalidResource,
    InvalidGeometry,
    InvalidMaterial,
    InvalidPipeline,
    InvalidCamera,
    InvalidLight,
    PacketPassMismatch,
    PacketSortOrder,
    PipelinePayloadMismatch,
    InvalidDrawResource,
    InvalidDrawTransform,
    InvalidDrawColor,
    InvalidDrawParameters,
};

/// <summary>
/// 검증 오류의 이름이다. 진단은 숫자 대신 이 이름을 싣는다 — 로그를 읽는 사람이 열거자를
/// 세어 보지 않아도 되게 하려는 것이다.
///
/// switch에 <b>default가 없다</b>. 새 오류가 열거형에 들어오면 여기서 컴파일이 멈추므로,
/// 이름 없는 오류가 로그에 숫자로 새어 나갈 수 없다. /W4는 이 경고(C4062)를 내지 않으니
/// 아래에서 직접 오류로 올린다.
/// </summary>
#pragma warning(push)
#pragma warning(error : 4061 4062)
[[nodiscard]] inline const char* GetRenderFrameValidationErrorName(
    const RenderFrameValidationError error)
{
    switch (error)
    {
    case RenderFrameValidationError::None:
        return "None";
    case RenderFrameValidationError::InvalidResource:
        return "InvalidResource";
    case RenderFrameValidationError::InvalidGeometry:
        return "InvalidGeometry";
    case RenderFrameValidationError::InvalidMaterial:
        return "InvalidMaterial";
    case RenderFrameValidationError::InvalidPipeline:
        return "InvalidPipeline";
    case RenderFrameValidationError::InvalidCamera:
        return "InvalidCamera";
    case RenderFrameValidationError::InvalidLight:
        return "InvalidLight";
    case RenderFrameValidationError::PacketPassMismatch:
        return "PacketPassMismatch";
    case RenderFrameValidationError::PacketSortOrder:
        return "PacketSortOrder";
    case RenderFrameValidationError::PipelinePayloadMismatch:
        return "PipelinePayloadMismatch";
    case RenderFrameValidationError::InvalidDrawResource:
        return "InvalidDrawResource";
    case RenderFrameValidationError::InvalidDrawTransform:
        return "InvalidDrawTransform";
    case RenderFrameValidationError::InvalidDrawColor:
        return "InvalidDrawColor";
    case RenderFrameValidationError::InvalidDrawParameters:
        return "InvalidDrawParameters";
    }

    // 위 switch가 모든 값을 덮으므로 여기에는 닿지 않는다. 반환 없이는 함수가 성립하지 않아
    // 남겨 둘 뿐이다.
    return "Unknown";
}
#pragma warning(pop)

/// <summary>검증의 결과이다: 무엇이 잘못됐고, 어느 패스의 몇 번째 패킷에서였는지.</summary>
struct RenderFrameValidationResult
{
    RenderFrameValidationError error = RenderFrameValidationError::None;
    RenderPass pass = RenderPass::Opaque;
    std::size_t packetIndex = 0;

    [[nodiscard]] bool IsValid() const { return error == RenderFrameValidationError::None; }
};

/// <summary>
/// 관례상 불변인 한 프레임의 렌더링 스냅숏이다.
/// Runtime 컴포넌트나 AssetDatabase 포인터를 의도적으로 담지 않으므로, 백엔드는 장면 수명이나
/// 특정 그래픽 API에 의존하지 않고 이것을 소비할 수 있다.
/// </summary>
struct RenderFrame
{
    [[nodiscard]] const std::optional<CameraRenderData>& GetCamera() const { return mCamera; }

    /// <summary>
    /// 이 프레임이 구성될 때 대상으로 삼은 렌더 타깃의 크기이다. 스크린 공간 작업은 장치에 묻지
    /// 않고 여기서 읽으므로, 프레임은 자기 출력을 스스로 완전히 기술한다.
    /// </summary>
    [[nodiscard]] RenderTargetSize GetRenderTargetSize() const { return mRenderTargetSize; }

    /// <summary>이 프레임의 광원이다. 많아야 MaxFrameLights개다.</summary>
    [[nodiscard]] const std::vector<LightRenderData>& GetLights() const { return mLights; }

    /// <summary>방향 없이 모든 면에 더해지는 빛이다. 장면이 말하지 않았으면 검정이다.</summary>
    [[nodiscard]] const Math::Color& GetAmbientLight() const { return mAmbientLight; }

    [[nodiscard]] const std::vector<DrawPacket>& GetDrawPackets(const RenderPass pass) const
    {
        if (pass >= RenderPass::Count)
        {
            static const std::vector<DrawPacket> sEmptyPackets;
            return sEmptyPackets;
        }
        return mDrawPacketQueues[static_cast<std::size_t>(pass)];
    }


    [[nodiscard]] const Geometry* GetGeometry(const GeometryHandle handle) const
    {
        return handle.IsValid() && handle.index < mGeometries.size() ? &mGeometries[handle.index] : nullptr;
    }

    [[nodiscard]] const SkinnedGeometry* GetSkinnedGeometry(const SkinnedGeometryHandle handle) const
    {
        return handle.IsValid() && handle.index < mSkinnedGeometries.size()
            ? &mSkinnedGeometries[handle.index] : nullptr;
    }

    [[nodiscard]] const Material* GetMaterial(const MaterialHandle handle) const
    {
        return handle.IsValid() && handle.index < mMaterials.size() ? &mMaterials[handle.index] : nullptr;
    }

    [[nodiscard]] const Pipeline* GetPipeline(const PipelineHandle handle) const
    {
        return handle.IsValid() && handle.index < mPipelines.size() ? &mPipelines[handle.index] : nullptr;
    }

    template <typename T>
    [[nodiscard]] std::vector<const T*> GetDraws(const RenderPass pass) const
    {
        std::vector<const T*> draws;
        for (const DrawPacket& packet : GetDrawPackets(pass))
        {
            if (const T* draw = std::get_if<T>(&packet.payload))
            {
                draws.push_back(draw);
            }
        }
        return draws;
    }

    [[nodiscard]] RenderFrameValidationResult Validate() const
    {
        if (mCamera && (!mCamera->view.IsFinite() || !mCamera->projection.IsFinite() ||
            !mCamera->clearColor.IsFinite()))
        {
            return { RenderFrameValidationError::InvalidCamera };
        }
        if (mLights.size() > MaxFrameLights || !mAmbientLight.IsFinite() ||
            !std::ranges::all_of(mLights, [](const LightRenderData& light) { return light.IsValid(); }))
        {
            return { RenderFrameValidationError::InvalidLight };
        }
        for (const Geometry& geometry : mGeometries)
        {
            // A geometry entry without usable vertices is a frontend bug: the frontend drops a draw
            // it could not import rather than passing one on for a backend to skip.
            if (!geometry.data || !geometry.data->IsValid())
            {
                return { RenderFrameValidationError::InvalidGeometry };
            }
        }
        for (const SkinnedGeometry& geometry : mSkinnedGeometries)
        {
            // Same rule as the static table above: an entry that could not be imported is dropped
            // by the frontend rather than handed on for a backend to skip.
            if (!geometry.data || !geometry.data->IsValid())
            {
                return { RenderFrameValidationError::InvalidGeometry };
            }
        }
        for (const Material& material : mMaterials)
        {
            // A material without usable pixels is a frontend bug, in the same way an empty text
            // image is: the frontend drops a draw it could not decode.
            if (!material.baseColorTexture || !material.baseColorTexture->IsValid())
            {
                return { RenderFrameValidationError::InvalidMaterial };
            }
        }
        for (const Pipeline& pipeline : mPipelines)
        {
            if (pipeline.kind != PipelineKind::Mesh && pipeline.kind != PipelineKind::Sprite &&
                pipeline.kind != PipelineKind::Text && pipeline.kind != PipelineKind::SkinnedMesh)
            {
                return { RenderFrameValidationError::InvalidPipeline };
            }
        }

        for (std::size_t passIndex = 0; passIndex < mDrawPacketQueues.size(); ++passIndex)
        {
            const RenderPass pass = static_cast<RenderPass>(passIndex);
            const std::vector<DrawPacket>& packets = mDrawPacketQueues[passIndex];
            for (std::size_t packetIndex = 0; packetIndex < packets.size(); ++packetIndex)
            {
                const DrawPacket& packet = packets[packetIndex];
                if (packet.pass != pass)
                {
                    return { RenderFrameValidationError::PacketPassMismatch, pass, packetIndex };
                }
                if (!std::isfinite(packet.viewDepth))
                {
                    return { RenderFrameValidationError::InvalidDrawTransform, pass, packetIndex };
                }
                if (packetIndex > 0 && DrawPacketPrecedes(packet, packets[packetIndex - 1]))
                {
                    return { RenderFrameValidationError::PacketSortOrder, pass, packetIndex };
                }

                const Pipeline* pipeline = std::visit([this](const auto& draw)
                {
                    return GetPipeline(draw.pipeline);
                }, packet.payload);
                if (!pipeline)
                {
                    return { RenderFrameValidationError::InvalidPipeline, pass, packetIndex };
                }

                const DrawPacketKind kind = GetDrawPacketKind(packet);
                if ((kind == DrawPacketKind::Mesh && pipeline->kind != PipelineKind::Mesh) ||
                    (kind == DrawPacketKind::Sprite && pipeline->kind != PipelineKind::Sprite) ||
                    (kind == DrawPacketKind::Text && pipeline->kind != PipelineKind::Text) ||
                    (kind == DrawPacketKind::Tilemap && pipeline->kind != PipelineKind::Sprite) ||
                    (kind == DrawPacketKind::SkinnedMesh && pipeline->kind != PipelineKind::SkinnedMesh))
                {
                    return { RenderFrameValidationError::PipelinePayloadMismatch, pass, packetIndex };
                }

                const RenderFrameValidationError drawError = std::visit([this](const auto& draw)
                {
                    using DrawType = std::decay_t<decltype(draw)>;
                    if constexpr (std::is_same_v<DrawType, MeshDraw>)
                    {
                        if (!draw.localToWorld.IsFinite()) return RenderFrameValidationError::InvalidDrawTransform;
                        return GetGeometry(draw.geometry) && GetMaterial(draw.material)
                            ? RenderFrameValidationError::None
                            : RenderFrameValidationError::InvalidDrawResource;
                    }
                    else if constexpr (std::is_same_v<DrawType, SpriteDraw>)
                    {
                        if (!draw.localToWorld.IsFinite()) return RenderFrameValidationError::InvalidDrawTransform;
                        if (!draw.tint.IsFinite()) return RenderFrameValidationError::InvalidDrawColor;
                        if (!GetMaterial(draw.material)) return RenderFrameValidationError::InvalidDrawResource;
                        // A sliced sprite with no usable size or a negative border is a frontend
                        // bug, and is rejected here for the same reason a text draw without pixels
                        // is: a backend must never have to guess what was meant.
                        if (!draw.uvRect.IsValid())
                        {
                            return RenderFrameValidationError::InvalidDrawParameters;
                        }
                        if (draw.sliced &&
                            (!draw.border.IsValid() ||
                             !std::isfinite(draw.size.GetX()) || draw.size.GetX() <= 0.0f ||
                             !std::isfinite(draw.size.GetY()) || draw.size.GetY() <= 0.0f))
                        {
                            return RenderFrameValidationError::InvalidDrawParameters;
                        }
                        return std::isfinite(draw.pixelsPerUnit) && draw.pixelsPerUnit > 0.0f
                            ? RenderFrameValidationError::None
                            : RenderFrameValidationError::InvalidDrawParameters;
                    }
                    else if constexpr (std::is_same_v<DrawType, TilemapDraw>)
                    {
                        if (!draw.localToWorld.IsFinite()) return RenderFrameValidationError::InvalidDrawTransform;
                        if (!draw.tint.IsFinite()) return RenderFrameValidationError::InvalidDrawColor;
                        if (!GetMaterial(draw.material)) return RenderFrameValidationError::InvalidDrawResource;
                        // 빈 타일맵은 draw가 아니라 아무것도 아니다: 프론트엔드가 그런 레이어를
                        // 싣지 않으므로, 여기 도착한 목록은 비어 있을 수 없다.
                        if (!draw.tiles || draw.tiles->empty() ||
                            !std::ranges::all_of(*draw.tiles,
                                [](const TilemapTile& tile) { return tile.IsValid(); }))
                        {
                            return RenderFrameValidationError::InvalidDrawParameters;
                        }
                        return std::isfinite(draw.cellSize.GetX()) && draw.cellSize.GetX() > 0.0f &&
                            std::isfinite(draw.cellSize.GetY()) && draw.cellSize.GetY() > 0.0f
                            ? RenderFrameValidationError::None
                            : RenderFrameValidationError::InvalidDrawParameters;
                    }
                    else if constexpr (std::is_same_v<DrawType, TextDraw>)
                    {
                        if (!draw.localToWorld.IsFinite()) return RenderFrameValidationError::InvalidDrawTransform;
                        if (!draw.tint.IsFinite()) return RenderFrameValidationError::InvalidDrawColor;
                        // A text packet without a usable atlas page is a frontend bug: the frontend
                        // drops a draw it could not shape rather than passing one on for a backend
                        // to skip. An empty or malformed glyph list is the same bug in the other
                        // half of the payload.
                        if (!draw.page || !draw.page->IsValid())
                        {
                            return RenderFrameValidationError::InvalidDrawResource;
                        }
                        if (!draw.glyphs || draw.glyphs->empty() ||
                            !std::ranges::all_of(*draw.glyphs,
                                [](const TextGlyphQuad& glyph) { return glyph.IsValid(); }))
                        {
                            return RenderFrameValidationError::InvalidDrawParameters;
                        }
                        return std::isfinite(draw.pixelsPerUnit) && draw.pixelsPerUnit > 0.0f &&
                            (draw.space == TextSpace::World || draw.space == TextSpace::Screen)
                            ? RenderFrameValidationError::None
                            : RenderFrameValidationError::InvalidDrawParameters;
                    }
                    else if constexpr (std::is_same_v<DrawType, SkinnedMeshDraw>)
                    {
                        if (!draw.localToWorld.IsFinite()) return RenderFrameValidationError::InvalidDrawTransform;
                        if (!draw.tint.IsFinite()) return RenderFrameValidationError::InvalidDrawColor;
                        if (!GetSkinnedGeometry(draw.geometry) || !GetMaterial(draw.material))
                        {
                            return RenderFrameValidationError::InvalidDrawResource;
                        }
                        // 뼈 행렬이 없거나 빈 배열이면 이 draw는 정점을 어디로 옮길지 답할 수
                        // 없다 — 무편향 항등 행렬로 조용히 대신하지 않는다. 배열 안에 값이 아닌
                        // 것(NaN 등)이 섞여 있어도 마찬가지다.
                        if (!draw.boneMatrices || draw.boneMatrices->empty() ||
                            !std::ranges::all_of(*draw.boneMatrices,
                                [](const Math::Matrix4x4& matrix) { return matrix.IsFinite(); }))
                        {
                            return RenderFrameValidationError::InvalidDrawParameters;
                        }
                        return RenderFrameValidationError::None;
                    }
                    else
                    {
                        // payload에 종류가 하나 늘고 여기 붙이는 것을 잊으면 빌드가 선다 — 위
                        // DrawTraits의 주석이 설명하는 바로 그 사고를 이 자리에서도 막는다.
                        static_assert(
                            Detail::AlwaysFalse<DrawType>,
                            "every draw payload needs its own validation branch here");
                        return RenderFrameValidationError::None;
                    }
                }, packet.payload);
                if (drawError != RenderFrameValidationError::None)
                {
                    return { drawError, pass, packetIndex };
                }
            }
        }
        return {};
    }

    /// <summary>이 프레임이 렌더링 계약을 만족하면 dispatch 능력 객체를 반환한다.</summary>
    [[nodiscard]] std::optional<ValidatedRenderFrame> TryValidate(RenderFrameValidationResult& validation) const
    {
        validation = Validate();
        if (!validation.IsValid()) return std::nullopt;
        return ValidatedRenderFrame(*this);
    }

private:
    friend class RenderFrameBuilder;

    std::optional<CameraRenderData> mCamera;
    RenderTargetSize mRenderTargetSize;
    std::vector<LightRenderData> mLights;
    Math::Color mAmbientLight = Math::Color::Black;
    std::array<std::vector<DrawPacket>, static_cast<std::size_t>(RenderPass::Count)> mDrawPacketQueues;
    std::vector<Geometry> mGeometries;
    std::vector<SkinnedGeometry> mSkinnedGeometries;
    std::vector<Material> mMaterials;
    std::vector<Pipeline> mPipelines;
};

inline const RenderFrame& ValidatedRenderFrame::GetFrame() const
{
    return *mFrame;
}

}
