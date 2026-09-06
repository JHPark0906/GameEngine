#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../Assets/TextureData.h"
#include "DynamicTextureSnapshotPool.h"
#include "../UIModel/TextEditModel.h"
#include "../Math/Color.h"
#include "../Platform/IClipboard.h"
#include "../Rendering/RenderFrame.h"
#include "../Rendering/TextRasterizationCache.h"
#include "../Runtime/Input.h"

namespace GameEngine::Rendering
{
class RenderFrameBuilder;
}

namespace GameEngine::UI
{

/// <summary>위젯의 정체성이다. 상호작용 상태 — hot, active, 포커스 — 가 이것으로 기억된다.</summary>
using WidgetId = std::uint64_t;

/// <summary>문자열에서 위젯 id를 만든다. 같은 이름의 위젯 둘은 `index`로 구분한다.</summary>
[[nodiscard]] WidgetId MakeWidgetId(std::string_view name, std::uint64_t index = 0);

/// <summary>
/// 부모 위젯에 속한 `index`번째 하위 위젯의 id다. 벡터 필드의 x·y·z 칸처럼 한 속성이 여러
/// 위젯으로 그려질 때 쓴다. 부모 id에 정수를 더하는 대신 섞으므로, 이웃한 부모들의 하위 id가
/// 서로 겹치지 않는다.
/// </summary>
[[nodiscard]] WidgetId MakeChildWidgetId(WidgetId parent, std::uint64_t index);


/// <summary>
/// UI가 프레임에서 차지하는 자리다.
///
/// 에디터의 UI는 화면 전체이므로 프레임을 소유해도 된다 — 카메라를 픽셀 직교로 잠그고 장면이
/// 끼어들지 못하게 하는 것이 맞다. 반면 게임 안의 채팅창이나 HUD는 장면 위에 얹히는 것이라,
/// 같은 짓을 하면 게임 카메라가 뭉개진다. 그래서 모드가 둘이다.
/// </summary>
enum class UISurfaceMode : unsigned char
{
    /// <summary>
    /// 프레임 전체가 UI다. 카메라를 픽셀 직교 투영으로 잠그고 Transparent 패스에 싣는다.
    /// 기본값이며, 에디터가 쓰는 모드다.
    /// </summary>
    OwnFrame,
    /// <summary>
    /// 장면 위에 얹는 오버레이다. 카메라를 건드리지 않고 — 그래서 장면의 카메라가 그대로 살아
    /// 있고 — draw는 화면 공간으로 Overlay 패스에 실린다.
    /// </summary>
    Overlay,
};

/// <summary>화면 픽셀 단위의 사각형이다. 원점은 렌더 타깃의 좌상단이다.</summary>
struct UIRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] bool Contains(const float pointX, const float pointY) const
    {
        return pointX >= x && pointX < x + width && pointY >= y && pointY < y + height;
    }

    [[nodiscard]] float GetRight() const { return x + width; }
    [[nodiscard]] float GetBottom() const { return y + height; }
};

/// <summary>텍스트의 가로 정렬이다.</summary>
enum class TextAlign : unsigned char
{
    Left,
    Center,
};

/// <summary>
/// UI 텍스트가 맡는 역할이다. 위젯은 자기 글자가 무엇인지만 말하고, 어느 폰트로 그릴지는
/// 역할에 배정된 폰트가 답한다 — 그래서 에디터 코드에는 폰트 파일 이름도 패밀리 이름도
/// 나타나지 않는다.
/// </summary>
enum class UIFontRole : unsigned char
{
    /// <summary>패널 제목과 툴바처럼 화면의 뼈대를 이루는 글자다.</summary>
    Title,
    /// <summary>레이블, 버튼, 입력 칸 등 대부분의 UI 글자다.</summary>
    Body,
    /// <summary>콘솔 출력처럼 글자가 세로로 줄맞춰야 하는 곳의 고정폭 글자다.</summary>
    Monospace,
};

/// <summary>이미지 위젯 위에서 일어난 상호작용이다. 뷰포트 카메라 조작이 이것을 소비한다.</summary>
struct ImageInteraction
{
    bool hovered = false;
    /// <summary>이 위젯 위에서 시작된 왼쪽/가운데 드래그가 진행 중인지이다.</summary>
    bool leftDragging = false;
    bool middleDragging = false;
    /// <summary>드래그 중 이번 프레임의 커서 이동량이다. 픽셀 단위다.</summary>
    float dragDeltaX = 0.0f;
    float dragDeltaY = 0.0f;
    /// <summary>호버 중의 휠 눈금이다.</summary>
    float wheel = 0.0f;
};

/// <summary>
/// 엔진이 그리는 즉시 모드 UI다.
///
/// 위젯은 매 프레임 호출로 선언되고, 그리기는 스프라이트·텍스트 draw로 프레임에 실린다 —
/// 게임이 그려지는 바로 그 계약이다. 입력은 `Runtime::Input`에서 읽는다. 그래서 이 UI는 엔진이
/// 서는 곳이면 어디서든 서고, 플랫폼의 컨트롤이나 윈도잉 시스템을 전혀 모른다. 에디터가 플랫폼
/// 독립이 되는 길이 이것이다: 네이티브 UI를 추상화하는 것이 아니라, UI를 엔진으로 그린다.
///
/// 프레임 사이에 남는 것은 상호작용 상태뿐이다: 어느 위젯이 hot/active인지, 어느 텍스트 필드가
/// 포커스를 갖는지, 목록이 얼마나 스크롤됐는지. 그리기 목록은 매 프레임 다시 만들어진다.
/// </summary>
class UIContext final
{
public:
    /// <summary>
    /// 텍스트 필드가 쓸 클립보드를 받는다. UI는 플랫폼을 모르므로 이것을 스스로 만들지 않는다:
    /// 만드는 일은 구체 구현을 이름 부를 자격이 있는 합성 루트의 것이고, 그래서 테스트는 가짜를
    /// 생성자에 그냥 건네면 된다 — 만들어진 것을 나중에 갈아끼우는 뒷문이 필요 없다.
    /// </summary>
    /// <param name="clipboard">
    /// 시스템 클립보드다. null이면 복사와 붙여넣기가 아무 일도 하지 않는다.
    /// </param>
    /// <param name="textRasterizer">
    /// 위젯의 글자를 배치할, 아직 초기화되지 않은 플랫폼 래스터라이저다. null이면 이 UI는
    /// 글자를 그리지 않는다.
    /// </param>
    UIContext(
        std::unique_ptr<Platform::IClipboard> clipboard,
        std::unique_ptr<Platform::ITextRasterizer> textRasterizer);
    ~UIContext();

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;

    /// <summary>프레임을 시작한다. 이번 프레임의 입력과 화면 크기를 받는다.</summary>
    void BeginFrame(const Runtime::Input& input, Rendering::RenderTargetSize renderTargetSize);

    /// <summary>
    /// 이번 프레임의 포인터를 다른 UI가 이미 가져갔음을 알린다. 다음
    /// <see cref="BeginFrame"/>이 마우스를 화면 밖에 있는 것처럼 읽어, 이 UI의 어떤 위젯도
    /// hover되거나 눌리지 않는다.
    ///
    /// 유지 모드 UI와 즉시 모드 UI가 한 화면에 설 때 같은 클릭이 둘 다에게 가는 것을 막는
    /// 자리다. 키보드는 그대로 둔다: 포인터를 누가 가져갔는지는 타이핑이 어디로 갈지와 다른
    /// 질문이고, 텍스트 필드가 포커스를 쥔 채 다른 곳이 클릭을 받는 것은 정상이다.
    /// </summary>
    /// <param name="consumed">다른 UI가 이 프레임의 포인터를 가져갔으면 true다.</param>
    void SetPointerConsumedExternally(bool consumed) { mPointerConsumedExternally = consumed; }

    /// <summary>
    /// 위젯이 스스로 그리는 글자의 크기다 — 버튼 레이블, 선택 항목, 텍스트 필드의 내용.
    ///
    /// 호출자가 크기를 넘기는 <see cref="DrawLabel"/>과 달리 이 셋은 위젯이 자기 글자를 직접
    /// 그리므로 크기를 물을 곳이 없다. 그래서 문맥이 하나 들고 있고, 이 값을 세우는 것만으로
    /// 그 셋이 함께 움직인다 — 에디터가 자기 본문 크기로 세우는 자리가 여기다. 기본값은 엔진이
    /// 아무 설정 없이 섰을 때의 크기다.
    /// </summary>
    /// <param name="size">논리 픽셀 크기다. 0 이하는 무시한다.</param>
    void SetBodyFontSize(const float size)
    {
        if (size > 0.0f)
        {
            mBodyFontSize = size;
        }
    }

    /// <summary>위젯이 스스로 그리는 글자의 크기다.</summary>
    [[nodiscard]] float GetBodyFontSize() const { return mBodyFontSize; }


    /// <summary>
    /// 이번 프레임의 그리기를 builder에 싣는다. 카메라를 픽셀 직교 투영으로 잠그므로, UI
    /// 프레임에는 장면의 카메라가 끼어들지 않는다.
    /// </summary>
    void EndFrame(Rendering::RenderFrameBuilder& builder);

    // ---- 위젯 ----

    /// <summary>단색 사각형이다. 패널과 구분선이 이것이다.</summary>
    void DrawPanel(const UIRect& rect, const Math::Color& color);

    /// <summary>
    /// 두 점을 잇는 단색 선이다. 축 정렬 사각형으로는 그릴 수 없는 비스듬한 선 — 씬 뷰 기즈모의
    /// 축 같은 것 — 을 위해, 길이만큼 긴 사각형 하나를 중점에서 기울여 그린다.
    /// </summary>
    /// <param name="startX">시작점의 x 픽셀 좌표이다.</param>
    /// <param name="startY">시작점의 y 픽셀 좌표이다.</param>
    /// <param name="endX">끝점의 x 픽셀 좌표이다.</param>
    /// <param name="endY">끝점의 y 픽셀 좌표이다.</param>
    /// <param name="thickness">선의 굵기이다. 픽셀 단위다.</param>
    /// <param name="color">선의 색이다.</param>
    void DrawLine(
        float startX, float startY, float endX, float endY, float thickness,
        const Math::Color& color);

    /// <summary>
    /// 역할에 쓸 폰트를 파일 바이트로 배정한다. 배정되지 않은 역할과 실패한 배정은
    /// 처음 등록에 성공한 폰트로 그린다. 등록된 폰트가 하나도 없으면 텍스트 배치가 실패한다.
    ///
    /// 역할 → 폰트의 대응은 UI가 소유한다: 어떤 글자가 제목이고 어떤 글자가 본문인지는 위젯을
    /// 아는 쪽만 말할 수 있다. 반대로 그 역할에 어느 파일을 줄지는 호출자 — 콘텐츠 소스를 쥔
    /// 애플리케이션 — 의 몫이라, 여기서는 경로가 아니라 바이트를 받는다.
    /// </summary>
    /// <param name="role">폰트를 배정할 역할이다.</param>
    /// <param name="fontBytes">폰트 파일 전체의 바이트다. 호출 뒤에는 놓아도 된다.</param>
    /// <returns>배정에 성공했으면 true다.</returns>
    [[nodiscard]] bool LoadFont(UIFontRole role, std::span<const std::byte> fontBytes);

    /// <summary>
    /// 그 역할에 동봉 폰트가 실제로 배정돼 있는지다. 거짓이면 처음 등록한 폰트를 기본값으로
    /// 사용한다. 등록된 폰트가 하나도 없으면 글자를 그릴 수 없다.
    /// </summary>
    [[nodiscard]] bool IsFontRoleLoaded(const UIFontRole role) const
    {
        return mLoadedFontRoles[static_cast<std::size_t>(role)];
    }

    /// <summary>한 줄 텍스트이다. 사각형의 세로 중앙에 놓인다.</summary>
    void DrawLabel(
        const UIRect& rect,
        std::string_view text,
        const Math::Color& color,
        float fontSize = 14.0f,
        TextAlign align = TextAlign::Left,
        UIFontRole role = UIFontRole::Body);

    /// <summary>버튼이다. 이번 프레임에 눌렸으면 true를 반환한다.</summary>
    [[nodiscard]] bool DrawButton(WidgetId id, const UIRect& rect, std::string_view label);

    /// <summary>
    /// 선택 가능한 행이다. 클릭하면 clicked, 짧은 간격의 두 번째 클릭이면 doubleClicked도
    /// 참이 된다.
    /// </summary>
    struct SelectableResult
    {
        bool clicked = false;
        bool doubleClicked = false;
    };
    [[nodiscard]] SelectableResult DrawSelectable(
        WidgetId id, const UIRect& rect, std::string_view label, bool selected);

    /// <summary>
    /// 편집 가능한 한 줄 텍스트 필드이다. 클릭이 포커스를 주고 캐럿을 그 자리에 놓는다. 캐럿은
    /// 방향키·Home/End로 움직이고, Shift와 클릭-드래그가 선택을 만들며, 입력·삭제는 선택을
    /// 대체한다. Ctrl+C/X/V는 클립보드를 오가고, Ctrl+A는 전체 선택이다. 내용이 이번 프레임에
    /// 바뀌었으면 true를 반환한다.
    /// </summary>
    [[nodiscard]] bool DrawTextField(WidgetId id, const UIRect& rect, std::string& text);

    /// <summary>텍스처를 사각형에 채워 그린다. 에디터 뷰가 캡처된 프레임을 이것으로 보인다.</summary>
    void DrawImage(const UIRect& rect, std::shared_ptr<const Assets::TextureData> texture);

    /// <summary>
    /// 텍스처의 한 조각만 사각형에 채워 그린다. 시트에서 프레임 하나를 꺼내 보이는 길이며,
    /// 타일 팔레트가 타일 하나를 그리는 데 쓴다.
    ///
    /// 조각의 크기와 무관하게 사각형은 언제나 다 채워진다: 32픽셀 타일이든 64픽셀 타일이든
    /// 팔레트 칸은 같은 크기로 보여야 하므로, 배율은 텍스처가 아니라 조각을 기준으로 잡는다.
    /// </summary>
    /// <param name="uv">그릴 조각이다. 0..1의 정규화 좌표이고 원점은 좌상단이다.</param>
    void DrawImageRegion(
        const UIRect& rect,
        std::shared_ptr<const Assets::TextureData> texture,
        const Rendering::SpriteUVRect& uv);

    /// <summary>이미지를 그리고, 그 위의 드래그·휠 상호작용을 보고한다.</summary>
    [[nodiscard]] ImageInteraction DrawInteractiveImage(
        WidgetId id, const UIRect& rect, std::shared_ptr<const Assets::TextureData> texture);

    /// <summary>
    /// 목록의 스크롤 상태를 적용한다: 위젯 영역 위의 휠을 소비해 오프셋을 갱신하고, 현재
    /// 오프셋(양수, 행 픽셀)을 반환한다. 호출자는 오프셋만큼 위로 밀어 행을 그리고, 영역 밖
    /// 행은 그리지 않는다.
    /// </summary>
    [[nodiscard]] float ApplyScroll(WidgetId id, const UIRect& rect, float contentHeight);

    /// <summary>
    /// 시스템 클립보드에 텍스트를 놓는다. 클립보드 설비가 없는 환경에서는 아무 일도 하지 않는다.
    ///
    /// 텍스트 필드의 Ctrl+C와 콘텐츠 브라우저의 에셋 참조 복사가 같은 클립보드를 사용한다.
    /// UI가 생성자에서 받은 인스턴스를 공유하며 별도의 전역 클립보드를 만들지 않는다.
    /// </summary>
    void SetClipboardText(std::string_view text);

    // ---- 조회 ----

    /// <summary>어느 텍스트 필드가 포커스를 갖고 있는지이다. 단축키 처리가 이것을 비켜 간다.</summary>
    [[nodiscard]] bool IsAnyTextFieldFocused() const { return mFocusedField != 0; }

    /// <summary>텍스트 필드의 포커스를 거둔다. 선택이 바뀌는 순간 등에 쓴다.</summary>
    void ClearFieldFocus() { mFocusedField = 0; }

    /// <summary>이 텍스트 필드가 포커스를 갖고 있는지이다. 편집 중인 칸은 값으로 덮어쓰지 않는다.</summary>
    [[nodiscard]] bool IsFieldFocused(const WidgetId id) const { return mFocusedField == id; }

    /// <summary>포커스를 가진 텍스트 필드의 id다. 없으면 0이다. 포커스 세대 추적이 읽는다.</summary>
    [[nodiscard]] WidgetId GetFocusedField() const { return mFocusedField; }

    /// <summary>
    /// 포커스된 텍스트 필드의 편집 상태다. 캐럿과 선택 양끝은 UTF-8 바이트 오프셋이고, 선택이
    /// 없으면 양끝이 캐럿과 같다. 테스트가 편집 모델을 관찰하는 창이다.
    /// </summary>
    struct TextEditState
    {
        std::size_t caret = 0;
        std::size_t selectionBegin = 0;
        std::size_t selectionEnd = 0;

        [[nodiscard]] bool HasSelection() const { return selectionBegin != selectionEnd; }
    };
    [[nodiscard]] TextEditState GetTextEditState() const;

    // ---- 이번 프레임의 원시 입력. 드래그 앤 드롭처럼 위젯 하나에 갇히지 않는 제스처가 읽는다.
    [[nodiscard]] float GetMouseX() const { return mMouseX; }
    [[nodiscard]] float GetMouseY() const { return mMouseY; }
    [[nodiscard]] bool IsMouseDown() const { return mMouseDown; }
    [[nodiscard]] bool WasMouseReleased() const { return mMouseReleased; }

    // ---- 플랫폼이 말한 그대로의 포인터.
    //
    // 위의 넷은 <see cref="SetPointerConsumedExternally"/>가 참인 프레임에 지워진다 — 커서는
    // 화면 밖에 있는 것처럼 읽히고 버튼은 눌리지 않은 것으로 읽힌다. 그것이 즉시 모드 위젯에는
    // 옳지만, <b>유지 모드 요소가 시작한 제스처</b>에는 틀린다: 떠 있는 창의 제목줄을 잡고 끄는
    // 동안 포인터는 계속 유지 모드의 것이므로, 그 제스처가 위의 값을 읽으면 커서가 화면 밖에
    // 있고 버튼이 떼어진 것으로 보여 창이 움직이지 않는다.
    //
    // 그래서 지워지지 않는 값을 따로 둔다. 이것을 읽어도 되는 것은 <b>누가 포인터를 가졌는지
    // 이미 아는</b> 쪽뿐이다 — 즉시 모드 위젯은 위의 넷을 그대로 쓴다.
    [[nodiscard]] float GetPointerX() const { return mHitX; }
    [[nodiscard]] float GetPointerY() const { return mHitY; }
    [[nodiscard]] bool IsPointerDown() const { return mPointerDown; }

    /// <summary>
    /// 콘텐츠 배율이다. 글꼴 크기에 곱해지므로, 호출자가 넘기는 fontSize는 96 DPI 기준의
    /// 논리 크기이고 화면에서는 배율만큼 커진다. 사각형은 이미 픽셀이라 호출자가 직접 곱한다.
    /// </summary>

    /// <summary>
    /// 이 UI가 프레임을 소유할지, 장면 위에 얹힐지를 정한다. 기본은 프레임 소유다.
    /// </summary>
    /// <param name="mode">프레임을 소유하려면 OwnFrame, 오버레이면 Overlay이다.</param>
    void SetSurfaceMode(const UISurfaceMode mode) { mSurfaceMode = mode; }

    /// <summary>이 UI가 지금 어느 자리를 차지하는지다.</summary>
    /// <returns>현재 설정된 모드이다.</returns>
    [[nodiscard]] UISurfaceMode GetSurfaceMode() const { return mSurfaceMode; }

    void SetScale(const float scale) { mScale = scale > 0.0f ? scale : 1.0f; }
    [[nodiscard]] float GetScale() const { return mScale; }

    /// <summary>
    /// CPU 픽셀을 이번 프레임의 텍스처로 감싼다. id는 매번 새로 발급되므로 픽셀이 매 프레임
    /// 바뀌는 이미지 — 캡처된 뷰 — 에 안전하다.
    /// </summary>
    [[nodiscard]] std::shared_ptr<const Assets::TextureData> MakeDynamicTexture(
        unsigned int width, unsigned int height, const std::vector<std::byte>& rgbaPixels);

    /// <summary>
    /// 매 프레임 바뀌는 이미지를 새로운 CPU 스냅숏으로 발행한다. 기존 프레임이 쥔 픽셀은
    /// 변하지 않는다. 크기가 같으면 같은 id와 증가한 revision으로 GPU 텍스처를 재사용하고,
    /// 크기가 다르거나 처음이면 새 id를 발급한다. 호출자는 texture 변수 자체를 한 스레드에서
    /// 갱신하고, 완성된 프레임으로 다른 스레드에 스냅숏의 소유권을 전달한다.
    /// </summary>
    void UpdateDynamicTexture(
        std::shared_ptr<Assets::TextureData>& texture,
        unsigned int width, unsigned int height, const std::vector<std::byte>& rgbaPixels);

private:
    struct QuadDrawData
    {
        UIRect rect;
        Math::Color color;
        std::shared_ptr<const Assets::TextureData> texture;
        /// <summary>텍스처에서 그릴 조각이다. 기본은 이미지 전체다.</summary>
        Rendering::SpriteUVRect uv;
        /// <summary>사각형 중심을 축으로 한 회전이다. 선이 아닌 사각형은 0이다.</summary>
        float rotationDegrees = 0.0f;
    };
    struct TextDrawData
    {
        float x = 0.0f;
        float y = 0.0f;
        std::shared_ptr<const Rendering::ShapedText> shaped;
        Math::Color color;
    };
    struct DrawCommand
    {
        bool isText = false;
        QuadDrawData quad;
        TextDrawData text;
    };

    void PushQuad(const UIRect& rect, const Math::Color& color,
        std::shared_ptr<const Assets::TextureData> texture, float rotationDegrees = 0.0f);
    void PushText(
        const UIRect& rect, std::string_view text, const Math::Color& color, float fontSize,
        TextAlign align, UIFontRole role);
    /// <summary>
    /// 이 위젯이 포인터의 임자인지다. 그리고 그 판정에 쓰일 이번 프레임의 후보를 남긴다.
    ///
    /// 선언 순서가 유일한 순서다: 커서 아래 마지막으로 선언된 위젯이 포인터를 갖는다. 그리는
    /// 것도 같은 순서라 나중에 선언된 것이 위에 얹히므로, 눈에 보이는 맨 위의 것이 곧 임자다.
    /// 즉시 모드는 호출부가 결과를 그 자리에서 받아야 해서 "마지막"을 프레임 도중에는 알 수
    /// 없다. 그래서 답은 직전 프레임이 정한 임자이고, 이번 프레임의 선언들은 다음 프레임의
    /// 임자를 정한다 — 위젯이 커서 아래 처음 나타난 프레임에는 아직 임자가 아니다.
    /// </summary>
    [[nodiscard]] bool IsHovered(WidgetId id, const UIRect& rect);

    // ---- 텍스트 필드의 편집. 캐럿·선택·편집 연산은 UIModel::TextEditModel이 쥔다.
    /// <summary>텍스트 왼쪽 끝에서 x 픽셀에 가장 가까운 UTF-8 문자 경계다.</summary>
    [[nodiscard]] std::size_t CaretIndexFromX(const std::string& text, float x);
    /// <summary>이 텍스트가 필드 글꼴로 그려질 때의 픽셀 폭이다. 래스터화 캐시로 잰다.</summary>
    [[nodiscard]] float MeasureFieldText(std::string_view text);

    /// <summary>모든 단색 사각형이 공유하는 1x1 흰 텍스처이다. 색은 틴트가 정한다.</summary>
    std::shared_ptr<const Assets::TextureData> mWhiteTexture;
    Rendering::TextRasterizationCache mTextCache;
    /// <summary>폰트가 배정된 역할들이다. 배정되지 않은 역할은 기본 UI 폰트로 그려진다.</summary>
    std::array<bool, 3> mLoadedFontRoles{};

    std::vector<DrawCommand> mCommands;
    Rendering::RenderTargetSize mRenderTargetSize;

    // 입력 스냅숏: BeginFrame에서 읽어 이번 프레임의 위젯들이 공유한다.
    float mMouseX = 0.0f;
    float mMouseY = 0.0f;
    /// <summary>
    /// 히트 테스트에만 쓰는 진짜 커서다. 포인터를 남이 가져간 프레임에는 mMouseX·mMouseY가
    /// 화면 밖으로 치워지지만 이쪽은 그대로 남아, 그동안에도 임자 후보가 계속 갱신된다. 그
    /// 덕에 포인터가 돌아온 첫 프레임에 임자가 이미 정해져 있고 그 프레임의 클릭이 살아난다.
    /// </summary>
    float mHitX = 0.0f;
    /// <summary>왼쪽 버튼의 원시 상태다. 포인터를 남이 가져간 프레임에도 지워지지 않는다.</summary>
    bool mPointerDown = false;
    float mHitY = 0.0f;
    /// <summary>위젯이 스스로 그리는 글자의 크기다. 엔진 기본값이며 호출자가 바꿀 수 있다.</summary>
    float mBodyFontSize = 13.0f;
    bool mMouseDown = false;
    bool mMousePressed = false;
    bool mMouseReleased = false;
    bool mMiddleDown = false;
    bool mMiddlePressed = false;
    bool mMiddleReleased = false;
    float mMouseDeltaX = 0.0f;
    float mMouseDeltaY = 0.0f;
    float mWheel = 0.0f;
    std::string mTypedText;
    /// <summary>IME가 조합 중인 문자열이다. 포커스된 필드가 캐럿 자리에 이것을 보여 준다.</summary>
    std::string mCompositionText;
    bool mBackspacePressed = false;
    bool mEnterPressed = false;
    bool mDeletePressed = false;
    bool mLeftPressed = false;
    bool mRightPressed = false;
    bool mHomePressed = false;
    bool mEndPressed = false;
    bool mShiftDown = false;
    bool mCopyPressed = false;
    bool mCutPressed = false;
    bool mPastePressed = false;
    bool mSelectAllPressed = false;

    /// <summary>눌림이 시작된 위젯이다. 버튼은 눌림과 뗌이 같은 위젯 위일 때만 발화한다.</summary>
    WidgetId mActiveWidget = 0;
    /// <summary>
    /// 활성 위젯을 잡은 마우스 버튼이다. 그 버튼을 뗄 때만 풀린다 — 가운데 드래그가 끝난 뒤
    /// 왼쪽 버튼이 그 위젯의 드래그로 읽히는 일이 없도록.
    /// </summary>
    Platform::MouseButton mActiveButton = Platform::MouseButton::Left;
    /// <summary>다른 UI가 이번 프레임의 포인터를 가져갔는지다. 그렇다면 마우스를 읽지 않는다.</summary>
    bool mPointerConsumedExternally = false;
    WidgetId mFocusedField = 0;
    /// <summary>이번 프레임에 어떤 위젯이 클릭을 소비했는지이다. 빈 곳 클릭이 포커스를 거둔다.</summary>
    bool mClickConsumed = false;
    /// <summary>
    /// 이번 프레임에 커서 아래에서 마지막으로 선언된 위젯이다. 선언될 때마다 덮어쓰므로 프레임이
    /// 끝나면 맨 위의 것이 남는다. 다음 프레임의 임자가 이 값이다.
    /// </summary>
    WidgetId mHoverCandidate = 0;
    /// <summary>직전 프레임이 정한 포인터의 임자다. 이번 프레임에 hover를 답하는 것이 이 값이다.</summary>
    WidgetId mHoveredWidget = 0;
    /// <summary>
    /// 포커스 필드가 이번 프레임에 선언됐는지다. 선언되지 않은 채 프레임이 끝나면 — 스크롤로
    /// 컬링됐거나 패널이 닫혔으면 — EndFrame이 포커스를 거둔다: 보이지 않는 필드가 포커스를
    /// 쥐면 타이핑은 어디에도 닿지 않으면서 단축키만 삼킨다.
    /// </summary>
    bool mFocusedFieldDrawn = false;

    // 텍스트 편집 상태. 포커스는 한 필드만 가지므로 상태도 한 벌이고, 어느 필드의 것인지를
    // mEditField가 말한다 — 포커스가 옮겨 온 프레임에 이전 필드의 캐럿이 새 필드에 남지 않도록.
    WidgetId mEditField = 0;
    /// <summary>
    /// 캐럿과 선택, 그리고 그것들을 움직이는 편집 연산이다. 위젯이 하는 일은 그리기와 입력
    /// 전달이고, 무엇이 어떻게 바뀌는지는 이 모델이 안다 — 유지 모드 입력 필드도 같은 모델을
    /// 쓸 수 있도록 위젯 밖에 있다.
    /// </summary>
    UIModel::TextEditModel mTextEdit;
    /// <summary>
    /// 캐럿 깜박임의 기준 시각이다. 캐럿이 움직이거나 내용이 바뀔 때마다 되돌아가, 편집 중에는
    /// 캐럿이 늘 보인다.
    /// </summary>
    std::chrono::steady_clock::time_point mCaretBlinkOrigin;

    /// <summary>텍스트 필드의 클립보드다. 생성자가 받은 것이며 null일 수 있다.</summary>
    std::unique_ptr<Platform::IClipboard> mClipboard;

    /// <summary>더블클릭 판정: 마지막 클릭의 위젯과 시각이다.</summary>
    WidgetId mLastClickedWidget = 0;
    std::chrono::steady_clock::time_point mLastClickTime;

    std::unordered_map<WidgetId, float> mScrollOffsets;

    Detail::DynamicTextureSnapshotPool mDynamicTextureSnapshots;

    /// <summary>
    /// 동적 텍스처가 다음에 받을 번호이다. 재사용되지 않으며, 모든 UIContext가 공유한다 —
    /// 백엔드 캐시는 리소스 id로 항목을 찾으므로, 컨텍스트마다 1부터 세면 두 번째 컨텍스트의
    /// 텍스처가 첫 번째 것의 캐시 항목을 덮어쓴다.
    /// </summary>
    static std::atomic<std::uint64_t> sNextDynamicTextureIndex;

    /// <summary>이 UI가 프레임을 소유하는지 장면 위에 얹히는지다.</summary>
    UISurfaceMode mSurfaceMode = UISurfaceMode::OwnFrame;
    float mScale = 1.0f;
};

}
