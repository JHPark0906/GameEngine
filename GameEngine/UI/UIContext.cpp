#include "pch.h"
#include "UIContext.h"

#include "TextFit.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../Math/Matrix.h"
#include "../Platform/ITextRasterizer.h"
#include "../Rendering/RenderFrameBuilder.h"
#include "../Assets/ResourceId.h"

namespace GameEngine::UI
{

namespace
{
    constexpr std::chrono::milliseconds DoubleClickInterval{ 400 };

    /// <summary>위젯 팔레트이다. 어두운 에디터 테마 하나로 고정한다.</summary>
    constexpr Math::Color ButtonColor{ 0.22f, 0.24f, 0.27f, 1.0f };
    constexpr Math::Color ButtonHotColor{ 0.28f, 0.31f, 0.35f, 1.0f };
    constexpr Math::Color ButtonActiveColor{ 0.16f, 0.17f, 0.19f, 1.0f };
    constexpr Math::Color SelectableHotColor{ 0.24f, 0.26f, 0.30f, 1.0f };
    constexpr Math::Color SelectableSelectedColor{ 0.20f, 0.34f, 0.52f, 1.0f };
    constexpr Math::Color FieldColor{ 0.13f, 0.14f, 0.16f, 1.0f };
    constexpr Math::Color FieldFocusedColor{ 0.10f, 0.16f, 0.24f, 1.0f };
    constexpr Math::Color FieldSelectionColor{ 0.20f, 0.34f, 0.52f, 1.0f };
    constexpr Math::Color TextColor{ 0.92f, 0.93f, 0.94f, 1.0f };

    /// <summary>왼쪽 정렬 텍스트가 사각형 왼끝에서 들여쓰는 논리 픽셀이다. PushText와 같아야 한다.</summary>
    constexpr float TextLeftInset = 6.0f;
    /// <summary>
    /// 자리에 들어가지 않는 글자를 줄일 때 뒤에 붙는 것이다. U+2026(HORIZONTAL ELLIPSIS)의
    /// UTF-8 바이트를 직접 적는다 — 소스 파일의 인코딩이 무엇이든 같은 세 바이트가 되도록.
    /// </summary>
    constexpr std::string_view TextEllipsis = "\xE2\x80\xA6";
    /// <summary>캐럿 깜박임의 한 주기와, 그중 켜져 있는 시간이다.</summary>
    constexpr std::chrono::milliseconds CaretBlinkPeriod{ 1000 };
    constexpr std::chrono::milliseconds CaretVisibleSpan{ 500 };

    [[nodiscard]] bool HasDynamicTexturePixels(const unsigned int width, const unsigned int height,
        const std::vector<std::byte>& pixels)
    {
        constexpr auto BytesPerPixel = Assets::TextureData::BytesPerPixel;
        return width != 0 && height != 0 &&
            static_cast<std::size_t>(width) <= (std::numeric_limits<std::size_t>::max)() / height / BytesPerPixel &&
            pixels.size() == static_cast<std::size_t>(width) * height * BytesPerPixel;
    }

    [[nodiscard]] std::uint64_t HashText(const std::string_view text)
    {
        // FNV-1a. 위젯 id는 프레임 간 안정성만 필요하므로 품질 요구가 낮다.
        std::uint64_t hash = 14695981039346656037ull;
        for (const char character : text)
        {
            hash ^= static_cast<unsigned char>(character);
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

std::atomic<std::uint64_t> UIContext::sNextDynamicTextureIndex{ 1 };

WidgetId MakeWidgetId(const std::string_view name, const std::uint64_t index)
{
    const std::uint64_t hash = HashText(name) ^ (index * 0x9E3779B97F4A7C15ull);
    return hash == 0 ? 1 : hash;
}

WidgetId MakeChildWidgetId(const WidgetId parent, const std::uint64_t index)
{
    // splitmix64의 마무리 단계: 부모와 인덱스를 합친 값을 고르게 흩는다.
    std::uint64_t hash = parent ^ ((index + 1) * 0x9E3779B97F4A7C15ull);
    hash ^= hash >> 30;
    hash *= 0xBF58476D1CE4E5B9ull;
    hash ^= hash >> 27;
    hash *= 0x94D049BB133111EBull;
    hash ^= hash >> 31;
    return hash == 0 ? 1 : hash;
}

UIContext::UIContext(
    std::unique_ptr<Platform::IClipboard> clipboard,
    std::unique_ptr<Platform::ITextRasterizer> textRasterizer)
    : mTextCache(std::move(textRasterizer))
    , mClipboard(std::move(clipboard))
{
    // 단색 사각형은 전부 이 1x1 흰 픽셀을 틴트해 그린다. id는 동적 도메인의 0번 자리를 쓰며,
    // 픽셀이 결코 바뀌지 않으므로 백엔드가 한 번 업로드해 계속 쓴다.
    auto white = std::make_shared<Assets::TextureData>();
    white->id = Assets::MakeResourceId(Assets::ResourceIdDomain::Dynamic, 0);
    white->width = 1;
    white->height = 1;
    white->pixels.assign(4, std::byte{ 0xFF });
    mWhiteTexture = std::move(white);
}

UIContext::~UIContext() = default;

void UIContext::BeginFrame(
    const Runtime::Input& input, const Rendering::RenderTargetSize renderTargetSize)
{
    mCommands.clear();
    mRenderTargetSize = renderTargetSize;

    const Math::Vector2Int cursor = input.GetMousePosition();
    const Math::Vector2Int delta = input.GetMouseDelta();
    mMouseX = static_cast<float>(cursor.GetX());
    mMouseY = static_cast<float>(cursor.GetY());
    mHitX = mMouseX;
    mHitY = mMouseY;
    mPointerDown = input.GetMouseButton(Platform::MouseButton::Left);
    mMouseDeltaX = static_cast<float>(delta.GetX());
    mMouseDeltaY = static_cast<float>(delta.GetY());
    mMouseDown = input.GetMouseButton(Platform::MouseButton::Left);
    mMousePressed = input.GetMouseButtonDown(Platform::MouseButton::Left);
    mMouseReleased = input.GetMouseButtonUp(Platform::MouseButton::Left);
    mMiddleDown = input.GetMouseButton(Platform::MouseButton::Middle);
    mMiddlePressed = input.GetMouseButtonDown(Platform::MouseButton::Middle);
    mMiddleReleased = input.GetMouseButtonUp(Platform::MouseButton::Middle);
    mWheel = input.GetMouseWheel();
    mTypedText = input.GetTypedText();
    mCompositionText = input.GetCompositionText();
    mBackspacePressed = input.GetKeyDown(Platform::Key::Backspace);
    mEnterPressed = input.GetKeyDown(Platform::Key::Enter);
    mDeletePressed = input.GetKeyDown(Platform::Key::Delete);
    mLeftPressed = input.GetKeyDown(Platform::Key::Left);
    mRightPressed = input.GetKeyDown(Platform::Key::Right);
    mHomePressed = input.GetKeyDown(Platform::Key::Home);
    mEndPressed = input.GetKeyDown(Platform::Key::End);
    mShiftDown = input.GetKey(Platform::Key::Shift);
    const bool controlDown = input.GetKey(Platform::Key::Control);
    mCopyPressed = controlDown && input.GetKeyDown(Platform::Key::C);
    mCutPressed = controlDown && input.GetKeyDown(Platform::Key::X);
    mPastePressed = controlDown && input.GetKeyDown(Platform::Key::V);
    mSelectAllPressed = controlDown && input.GetKeyDown(Platform::Key::A);
    if (mPointerConsumedExternally)
    {
        // 다른 UI가 이 프레임의 포인터를 가져갔다. 커서를 화면 밖에 둔 것처럼 읽어 아무 위젯도
        // hover되거나 눌리지 않게 한다 — 버튼 상태만 지우면 커서가 위에 얹힌 것처럼 보인다.
        mMouseX = -1.0e6f;
        mMouseY = -1.0e6f;
        mMouseDown = false;
        mMousePressed = false;
        mMouseReleased = false;
        mMiddleDown = false;
        mMiddlePressed = false;
        mMiddleReleased = false;
        mWheel = 0.0f;
    }
    // 지난 프레임의 선언들이 정한 임자가 이번 프레임의 답이다. 후보는 다시 비워, 이번 프레임의
    // 선언들이 다음 프레임의 임자를 정하게 한다.
    mHoveredWidget = mHoverCandidate;
    mHoverCandidate = 0;
    mClickConsumed = false;
    mFocusedFieldDrawn = false;

    mTextCache.BeginFrame();
}

void UIContext::EndFrame(Rendering::RenderFrameBuilder& builder)
{
    // 빈 곳을 클릭했으면 포커스를 거둔다. 위젯들이 모두 선언된 뒤에야 "빈 곳"이었는지 안다.
    if (mMousePressed && !mClickConsumed)
    {
        mFocusedField = 0;
        mActiveWidget = 0;
    }
    // 포커스 필드가 이번 프레임에 선언되지 않았으면 — 스크롤로 컬링됐거나 패널이 닫혔으면 —
    // 포커스를 거둔다. 보이지 않는 필드가 포커스를 쥐고 있으면 타이핑은 어디에도 닿지 않으면서
    // 단축키만 계속 삼킨다.
    if (mFocusedField != 0 && !mFocusedFieldDrawn)
    {
        mFocusedField = 0;
    }
    // 활성 위젯은 자기를 잡은 버튼을 뗄 때 풀린다.
    if ((mMouseReleased && mActiveButton == Platform::MouseButton::Left) ||
        (mMiddleReleased && mActiveButton == Platform::MouseButton::Middle))
    {
        mActiveWidget = 0;
    }
    // UI는 픽셀로 말한다. 카메라를 픽셀 직교 투영으로 잠가, 어느 장면 카메라도 끼어들지 못한다.
    // 오버레이는 카메라를 건드리지 않는다: 장면이 자기 카메라로 그려진 위에 얹히는
    // 것이므로, 여기서 픽셀 직교로 잠그면 장면이 뭉개진다. 프레임을 소유할 때만 잠근다.
    const bool ownsFrame = mSurfaceMode == UISurfaceMode::OwnFrame;
    if (ownsFrame)
    {
        Rendering::CameraRenderData camera;
        camera.view = Math::Matrix4x4::Identity();
        camera.projection = Math::Matrix4x4::CreateOrthographicOffCenterLeftHanded(
            0.0f, static_cast<float>(mRenderTargetSize.width),
            static_cast<float>(mRenderTargetSize.height), 0.0f,
            0.0f, 1.0f);
        camera.clearColor = { 0.09f, 0.10f, 0.11f, 1.0f };
        builder.LockCamera(camera);
    }

    // 오버레이의 draw는 장면에 묻히지 않도록 Overlay 패스로 간다. 프레임을 소유할 때는
    // Transparent 패스로 간다.
    const Rendering::RenderPass uiPass =
        ownsFrame ? Rendering::RenderPass::Transparent : Rendering::RenderPass::Overlay;

    // 선언 순서가 곧 그리기 순서다: 나중 것이 위에 온다. 정렬 키가 그 순서를 실어 나른다.
    const Rendering::PipelineHandle spritePipeline =
        builder.AddPipeline({ Rendering::PipelineKind::Sprite });
    const Rendering::PipelineHandle textPipeline =
        builder.AddPipeline({ Rendering::PipelineKind::Text });
    int order = 0;
    for (const DrawCommand& command : mCommands)
    {
        if (command.isText)
        {
            // 배치가 여러 아틀라스 페이지에 걸치면 페이지마다 draw 하나다. 정렬 키는 명령
            // 하나가 통째로 가져가므로 한 문자열의 run들이 이웃한 순서로 남는다.
            for (const Rendering::ShapedTextRun& run : command.text.shaped->runs)
            {
                Rendering::TextDraw draw;
                draw.pipeline = textPipeline;
                draw.page = run.page;
                draw.glyphs = run.glyphs;
                draw.tint = command.text.color;
                draw.space = Rendering::TextSpace::Screen;
                draw.localToWorld = Math::Matrix4x4::CreateTranslation(
                    { command.text.x, command.text.y, 0.0f });
                static_cast<void>(builder.TryAddDraw(
                    uiPass, std::move(draw), order));
            }
            ++order;
            continue;
        }

        Rendering::SpriteDraw draw;
        draw.pipeline = spritePipeline;
        draw.material = builder.AddMaterial({ command.quad.texture });
        draw.tint = command.quad.color;
        // 단위 quad를 텍스처 크기/픽셀 배율로 늘리는 공용 배치를 그대로 쓴다: 텍스처가 w×h일 때
        // 배율 s = rect/texture 크기가 되도록 localToWorld에 싣는다. 픽셀 직교 투영이 잠겨
        // 있으므로 월드 단위가 곧 픽셀이다.
        const float textureWidth = static_cast<float>(command.quad.texture->width);
        const float textureHeight = static_cast<float>(command.quad.texture->height);
        // 배율은 텍스처가 아니라 그리는 조각을 기준으로 잡는다. 공용 quad 배치가 프레임의 픽셀
        // 크기로 사각형을 만들기 때문이다 — 조각으로 나누지 않으면 시트에서 꺼낸 타일이 시트가
        // 몇 칸인지에 따라 작아진다.
        const float frameWidth = textureWidth * command.quad.uv.width;
        const float frameHeight = textureHeight * command.quad.uv.height;
        draw.pixelsPerUnit = 1.0f;
        draw.uvRect = command.quad.uv;
        // 오버레이에는 잠긴 카메라가 없으므로 draw가 스스로 화면 공간이라 말해야 한다.
        // 프레임을 소유할 때는 카메라가 이미 픽셀 직교라 월드 공간 그대로도 같은 결과다.
        draw.space = ownsFrame ? Rendering::DrawSpace::World : Rendering::DrawSpace::Screen;
        draw.localToWorld =
            Math::Matrix4x4::CreateScale(
                { frameWidth > 0.0f ? command.quad.rect.width / frameWidth : 0.0f,
                  frameHeight > 0.0f ? command.quad.rect.height / frameHeight : 0.0f,
                  1.0f }) *
            Math::Matrix4x4::CreateRotationZDegrees(command.quad.rotationDegrees) *
            Math::Matrix4x4::CreateTranslation(
                { command.quad.rect.x + command.quad.rect.width * 0.5f,
                  command.quad.rect.y + command.quad.rect.height * 0.5f,
                  0.0f });
        // 픽셀 좌표는 +y가 아래인데 스프라이트 quad는 +y가 위라서, 세로를 뒤집어야 이미지의
        // 첫 행이 위에 온다. 그 뒤집기를 누가 하느냐가 두 갈래로 갈린다:
        // <b>엔진은 자기가 확실히 아는 공간만 자동으로 보정하고, y가 아래인 월드 카메라는 그
        // 카메라의 주인이 말한다.</b> 화면 공간이라고 선언한 draw는 그리기가 MakeUvTransform에서
        // 되돌려 읽으므로 여기서 또 켜면 두 번 뒤집혀 제자리로 돌아온다. 프레임을 소유할 때는
        // 월드 공간인데도 카메라가 픽셀 직교라 +y가 아래이고, 그리기는 그 카메라를 볼 수 없다 —
        // 그래서 그 갈래에서만 우리가 말한다.
        draw.flipY = draw.space == Rendering::DrawSpace::World;
        static_cast<void>(builder.TryAddDraw(
            uiPass, std::move(draw), order++));
    }
}

bool UIContext::IsHovered(const WidgetId id, const UIRect& rect)
{
    if (!rect.Contains(mHitX, mHitY))
    {
        return false;
    }
    // 커서 아래 있었다는 사실은 임자를 가려내는 데 쓰인다. 뒤에 선언되는 위젯이 덮어쓰므로,
    // 프레임이 끝나면 맨 위의 것만 남는다. 포인터를 남이 가져간 프레임에도 이것은 계속한다.
    mHoverCandidate = id;
    return !mPointerConsumedExternally && mHoveredWidget == id;
}

void UIContext::PushQuad(
    const UIRect& rect, const Math::Color& color,
    std::shared_ptr<const Assets::TextureData> texture, const float rotationDegrees)
{
    DrawCommand command;
    command.isText = false;
    command.quad.rect = rect;
    command.quad.color = color;
    command.quad.texture = texture ? std::move(texture) : mWhiteTexture;
    command.quad.rotationDegrees = rotationDegrees;
    mCommands.push_back(std::move(command));
}

namespace
{
    /// <summary>
    /// 역할이 텍스트 래스터라이저에게 자기를 부르는 이름이다. 폰트 파일의 실제 패밀리 이름이
    /// 아니라 별명이므로, 어떤 폰트가 배정되든 역할의 이름은 그대로다.
    /// </summary>
    [[nodiscard]] const char* GetFontRoleAlias(const GameEngine::UI::UIFontRole role)
    {
        switch (role)
        {
        case GameEngine::UI::UIFontRole::Title: return "ui.title";
        case GameEngine::UI::UIFontRole::Monospace: return "ui.monospace";
        default: return "ui.body";
        }
    }
}

bool UIContext::LoadFont(const UIFontRole role, const std::span<const std::byte> fontBytes)
{
    const bool loaded = mTextCache.RegisterFont(GetFontRoleAlias(role), fontBytes);
    mLoadedFontRoles[static_cast<std::size_t>(role)] = loaded;
    return loaded;
}

void UIContext::PushText(
    const UIRect& rect, const std::string_view text, const Math::Color& color,
    const float fontSize, const TextAlign align, const UIFontRole role)
{
    if (text.empty())
    {
        return;
    }

    const auto shape = [&](const std::string_view candidate)
    {
        Platform::TextRasterizationRequest request;
        request.text = std::string(candidate);
        request.fontSize = fontSize * mScale;
        // 배정에 실패했거나 배정되지 않은 역할은 패밀리를 비워 둔다 — 가장 먼저 등록에 성공한
        // 역할의 폰트로 그려진다(FontLibrary::SelectGlyph의 규칙이다).
        if (mLoadedFontRoles[static_cast<std::size_t>(role)])
        {
            request.fontFamily = GetFontRoleAlias(role);
        }
        return mTextCache.Resolve(request);
    };

    // 글자는 받은 사각형 안에 맞춘다. 라벨, 버튼, 목록 행이 모두 이 경로를 사용해
    // 긴 문구가 이웃 위젯을 덮지 않도록 같은 폭 제한을 적용한다.
    const float inset = align == TextAlign::Center ? 0.0f : TextLeftInset * mScale;
    const UI::TextFitResult fit = UI::FitTextToWidth(
        text, rect.width - inset, TextEllipsis,
        [&](const std::string_view candidate)
        {
            const std::shared_ptr<const Rendering::ShapedText> measured = shape(candidate);
            return measured ? static_cast<float>(measured->width) : 0.0f;
        });

    std::string fitted;
    if (fit.truncated)
    {
        fitted.assign(text.substr(0, fit.length));
        fitted.append(TextEllipsis);
    }
    const std::shared_ptr<const Rendering::ShapedText> shaped =
        fit.truncated ? shape(fitted) : shape(text);
    if (!shaped)
    {
        return;
    }

    DrawCommand command;
    command.isText = true;
    command.text.shaped = shaped;
    command.text.color = color;
    // 텍스트 draw는 블록 중심으로 배치되므로, 사각형 안에서의 정렬을 중심 좌표로 옮긴다.
    command.text.y = rect.y + rect.height * 0.5f;
    command.text.x = align == TextAlign::Center
        ? rect.x + rect.width * 0.5f
        : rect.x + TextLeftInset * mScale + static_cast<float>(shaped->width) * 0.5f;
    mCommands.push_back(std::move(command));
}

void UIContext::DrawPanel(const UIRect& rect, const Math::Color& color)
{
    PushQuad(rect, color, nullptr);
}

void UIContext::DrawLine(
    const float startX, const float startY, const float endX, const float endY,
    const float thickness, const Math::Color& color)
{
    const float deltaX = endX - startX;
    const float deltaY = endY - startY;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (length <= 0.0f || thickness <= 0.0f)
    {
        return;
    }
    // 각도는 픽셀 좌표계에서 잰다 — +y가 아래인 그 좌표계에서 회전도 함께 적용되므로, 화면에서
    // 보이는 기울기가 두 점을 잇는 방향과 맞는다.
    const float centerX = (startX + endX) * 0.5f;
    const float centerY = (startY + endY) * 0.5f;
    const UIRect rect{ centerX - length * 0.5f, centerY - thickness * 0.5f, length, thickness };
    const float degrees = std::atan2(deltaY, deltaX) * 180.0f / 3.14159265358979323846f;
    PushQuad(rect, color, nullptr, degrees);
}

void UIContext::DrawLabel(
    const UIRect& rect, const std::string_view text, const Math::Color& color,
    const float fontSize, const TextAlign align, const UIFontRole role)
{
    PushText(rect, text, color, fontSize, align, role);
}

void UIContext::SetClipboardText(const std::string_view text)
{
    if (mClipboard)
    {
        mClipboard->SetText(text);
    }
}

bool UIContext::DrawButton(const WidgetId id, const UIRect& rect, const std::string_view label)
{
    const bool hovered = IsHovered(id, rect);
    if (hovered && mMousePressed)
    {
        mActiveWidget = id;
        mActiveButton = Platform::MouseButton::Left;
        mClickConsumed = true;
    }
    // 눌림과 뗌이 같은 위젯 위여야 발화한다. 눌러 놓고 밖으로 끌고 나가 떼는 것은 취소다.
    const bool clicked = hovered && mMouseReleased && mActiveWidget == id &&
        mActiveButton == Platform::MouseButton::Left;

    const Math::Color color = mActiveWidget == id ? ButtonActiveColor
        : hovered ? ButtonHotColor
        : ButtonColor;
    PushQuad(rect, color, nullptr);
    PushText(rect, label, TextColor, mBodyFontSize, TextAlign::Center, UIFontRole::Body);
    return clicked;
}

UIContext::SelectableResult UIContext::DrawSelectable(
    const WidgetId id, const UIRect& rect, const std::string_view label, const bool selected)
{
    const bool hovered = IsHovered(id, rect);
    SelectableResult result;
    if (hovered && mMousePressed)
    {
        mClickConsumed = true;
        result.clicked = true;

        const auto now = std::chrono::steady_clock::now();
        result.doubleClicked =
            mLastClickedWidget == id && now - mLastClickTime <= DoubleClickInterval;
        mLastClickedWidget = id;
        mLastClickTime = now;
    }

    if (selected)
    {
        PushQuad(rect, SelectableSelectedColor, nullptr);
    }
    else if (hovered)
    {
        PushQuad(rect, SelectableHotColor, nullptr);
    }
    PushText(rect, label, TextColor, mBodyFontSize, TextAlign::Left, UIFontRole::Body);
    return result;
}

bool UIContext::DrawTextField(const WidgetId id, const UIRect& rect, std::string& text)
{
    const bool hovered = IsHovered(id, rect);
    if (hovered && mMousePressed)
    {
        mFocusedField = id;
        mClickConsumed = true;
        // 눌림은 이 필드의 것이다: 드래그 선택이 필드 밖으로 나가도 계속 이 필드의 것으로
        // 남고, 뗌이 다른 위젯의 클릭으로 읽히지 않는다.
        mActiveWidget = id;
        mActiveButton = Platform::MouseButton::Left;
    }
    const bool focused = mFocusedField == id;
    if (focused)
    {
        // 포커스 필드가 이번 프레임에 실제로 그려졌다는 표시다. 프레임 끝까지 이 표시가 없으면
        // — 스크롤 컬링, 닫힌 패널 — 포커스는 거둬진다.
        mFocusedFieldDrawn = true;
    }

    bool changed = false;
    if (focused)
    {
        if (mEditField != id)
        {
            // 포커스가 이 프레임에 왔다. 캐럿은 끝에서 시작하고, 클릭 포커스면 바로 아래에서
            // 클릭 위치로 옮겨진다.
            mEditField = id;
            mTextEdit.ResetTo(text);
            mCaretBlinkOrigin = std::chrono::steady_clock::now();
        }
        // 편집되지 않는 프레임에 텍스트가 바깥에서 바뀌었을 수 있다. 캐럿과 앵커를 텍스트 안
        // 문자 경계로 되돌린 뒤에만 편집한다.
        mTextEdit.ClampTo(text);

        const float textLeft = rect.x + TextLeftInset * mScale;
        if (mMousePressed && hovered)
        {
            mTextEdit.PlaceCaret(CaretIndexFromX(text, mMouseX - textLeft), mShiftDown);
            mCaretBlinkOrigin = std::chrono::steady_clock::now();
        }
        else if (mMouseDown && mActiveWidget == id)
        {
            // 드래그 선택: 앵커는 누른 자리에 남고 캐럿이 커서를 따라간다.
            const std::size_t dragged = CaretIndexFromX(text, mMouseX - textLeft);
            if (dragged != mTextEdit.GetCaret())
            {
                mTextEdit.PlaceCaret(dragged, true);
                mCaretBlinkOrigin = std::chrono::steady_clock::now();
            }
        }

        // 키와 타이핑은 편집 모델의 것이다. 위젯이 하는 일은 이번 프레임의 입력을 모아 넘기고,
        // 모델이 돌려준 것을 화면과 클립보드에 옮기는 것뿐이다.
        UIModel::TextEditModel::Input editInput;
        editInput.typedText = mTypedText;
        editInput.backspace = mBackspacePressed;
        editInput.deleteForward = mDeletePressed;
        editInput.moveLeft = mLeftPressed;
        editInput.moveRight = mRightPressed;
        editInput.moveToStart = mHomePressed;
        editInput.moveToEnd = mEndPressed;
        editInput.extendSelection = mShiftDown;
        editInput.selectAll = mSelectAllPressed;
        editInput.copy = mCopyPressed;
        editInput.cut = mCutPressed;
        editInput.paste = mPastePressed && mClipboard != nullptr;
        std::string pasted;
        if (editInput.paste)
        {
            pasted = mClipboard->GetText();
            editInput.pastedText = pasted;
        }

        const UIModel::TextEditModel::Result edit = mTextEdit.Apply(text, editInput);
        if (edit.wroteClipboard && mClipboard)
        {
            mClipboard->SetText(edit.clipboardText);
        }
        if (edit.textChanged || edit.caretMoved)
        {
            mCaretBlinkOrigin = std::chrono::steady_clock::now();
        }
        changed = edit.textChanged;

        if (mEnterPressed)
        {
            mFocusedField = 0;
        }
    }

    // Enter가 이 프레임에 포커스를 거뒀으면 캐럿과 선택 없이 보통 필드로 그린다.
    const bool stillFocused = mFocusedField == id;
    PushQuad(rect, stillFocused ? FieldFocusedColor : FieldColor, nullptr);
    const std::size_t caretIndex = mTextEdit.GetCaret();
    if (stillFocused)
    {
        const UIModel::TextEditModel::Selection selection = mTextEdit.GetSelection();
        if (selection.HasSelection())
        {
            // 선택 배경은 텍스트보다 먼저 실려야 글자가 그 위에 보인다.
            const float textLeft = rect.x + TextLeftInset * mScale;
            const float beginX = textLeft +
                MeasureFieldText(std::string_view(text).substr(0, selection.begin));
            const float endX = textLeft +
                MeasureFieldText(std::string_view(text).substr(0, selection.end));
            PushQuad(
                { beginX, rect.y + rect.height * 0.15f, endX - beginX, rect.height * 0.7f },
                FieldSelectionColor, nullptr);
        }
    }
    // 조합 중인 글자는 아직 필드의 내용이 아니다. 화면에만 캐럿 자리에 끼워 보여 주고, 확정될
    // 때 비로소 WM_CHAR를 거쳐 text로 들어온다 — 그래야 한글을 치는 동안 무엇이 만들어지고
    // 있는지 보이면서도, 필드가 쥔 문자열은 확정된 것만 담는다. 편집 모델이 조합을 모르는
    // 이유도 그것이다: 조합은 플랫폼이 만들고 있는 것이지 이 필드의 상태가 아니다.
    const bool composing = stillFocused && !mCompositionText.empty();
    std::string displayText = text;
    if (composing)
    {
        displayText.insert(caretIndex, mCompositionText);
    }
    PushText(rect, displayText, TextColor, mBodyFontSize, TextAlign::Left, UIFontRole::Body);
    if (composing)
    {
        // 조합 중임을 밑줄로 알린다. 확정된 글자와 구별되지 않으면 사람이 지금 무엇을 고치고
        // 있는지 알 수 없다.
        const float textLeft = rect.x + TextLeftInset * mScale;
        const float underlineBegin =
            textLeft + MeasureFieldText(std::string_view(displayText).substr(0, caretIndex));
        const float underlineEnd = textLeft +
            MeasureFieldText(
                std::string_view(displayText).substr(0, caretIndex + mCompositionText.size()));
        PushQuad(
            { underlineBegin, rect.y + rect.height * 0.78f,
              underlineEnd - underlineBegin, (std::max)(1.0f, mScale) },
            TextColor, nullptr);
    }
    if (stillFocused)
    {
        // 캐럿은 문자 경계의 세로선이다. 움직인 직후에는 켜져 있고, 가만히 두면 깜박인다.
        const auto sinceMove = std::chrono::steady_clock::now() - mCaretBlinkOrigin;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(sinceMove) %
                CaretBlinkPeriod < CaretVisibleSpan)
        {
            // 조합 중에는 캐럿이 조합 문자열 끝에 선다 — 다음 글자가 거기에 붙기 때문이다.
            const std::size_t caretOffset =
                composing ? caretIndex + mCompositionText.size() : caretIndex;
            const float caretX = rect.x + TextLeftInset * mScale +
                MeasureFieldText(std::string_view(displayText).substr(0, caretOffset));
            PushQuad(
                { caretX, rect.y + rect.height * 0.15f,
                  (std::max)(1.0f, mScale), rect.height * 0.7f },
                TextColor, nullptr);
        }
    }
    return changed;
}

UIContext::TextEditState UIContext::GetTextEditState() const
{
    // 편집 모델이 쥔 것을 위젯의 언어로 비춘다. 이 구조체는 테스트가 편집을 관찰하는 창이고,
    // 소유자는 모델이다.
    const UIModel::TextEditModel::Selection selection = mTextEdit.GetSelection();
    TextEditState state;
    state.caret = mTextEdit.GetCaret();
    state.selectionBegin = selection.begin;
    state.selectionEnd = selection.end;
    return state;
}

std::size_t UIContext::CaretIndexFromX(const std::string& text, const float x)
{
    if (x <= 0.0f || text.empty())
    {
        return 0;
    }
    // 왼쪽부터 문자 경계마다 접두사 폭을 재서, 클릭 x를 처음 넘는 경계와 그 앞 경계 중 가까운
    // 쪽을 고른다. 접두사 문자열은 래스터화 캐시에 남으므로 같은 자리를 다시 재는 프레임은
    // 캐시 적중이다.
    std::size_t previousBoundary = 0;
    float previousWidth = 0.0f;
    std::size_t boundary = 0;
    while (boundary < text.size())
    {
        boundary = UIModel::TextEditModel::NextCharBoundary(text, boundary);
        const float width = MeasureFieldText(std::string_view(text).substr(0, boundary));
        if (width >= x)
        {
            return x - previousWidth <= width - x ? previousBoundary : boundary;
        }
        previousBoundary = boundary;
        previousWidth = width;
    }
    return text.size();
}

float UIContext::MeasureFieldText(const std::string_view text)
{
    if (text.empty())
    {
        return 0.0f;
    }
    // 배치된 블록의 폭을 잣대로 사용한다. 캐시된 레이아웃을 재사용하므로 매 프레임 다시 재지 않는다.
    Platform::TextRasterizationRequest request;
    request.text = std::string(text);
    request.fontSize = mBodyFontSize * mScale;
    // 그린 것과 같은 폰트로 재야 캐럿이 글자와 어긋나지 않는다.
    if (mLoadedFontRoles[static_cast<std::size_t>(UIFontRole::Body)])
    {
        request.fontFamily = GetFontRoleAlias(UIFontRole::Body);
    }
    const std::shared_ptr<const Rendering::ShapedText> shaped = mTextCache.Resolve(request);
    return shaped ? static_cast<float>(shaped->width) : 0.0f;
}

void UIContext::DrawImage(const UIRect& rect, std::shared_ptr<const Assets::TextureData> texture)
{
    if (texture && texture->IsValid())
    {
        PushQuad(rect, Math::Color::White, std::move(texture));
    }
    else
    {
        PushQuad(rect, { 0.0f, 0.0f, 0.0f, 1.0f }, nullptr);
    }
}

void UIContext::DrawImageRegion(
    const UIRect& rect,
    std::shared_ptr<const Assets::TextureData> texture,
    const Rendering::SpriteUVRect& uv)
{
    if (!texture || !texture->IsValid() || !uv.IsValid())
    {
        PushQuad(rect, { 0.0f, 0.0f, 0.0f, 1.0f }, nullptr);
        return;
    }
    DrawCommand command;
    command.isText = false;
    command.quad.rect = rect;
    command.quad.color = Math::Color::White;
    command.quad.texture = std::move(texture);
    command.quad.uv = uv;
    mCommands.push_back(std::move(command));
}

ImageInteraction UIContext::DrawInteractiveImage(
    const WidgetId id, const UIRect& rect, std::shared_ptr<const Assets::TextureData> texture)
{
    DrawImage(rect, std::move(texture));

    ImageInteraction interaction;
    interaction.hovered = IsHovered(id, rect);
    if (interaction.hovered && (mMousePressed || mMiddlePressed))
    {
        mActiveWidget = id;
        mActiveButton = mMousePressed ? Platform::MouseButton::Left : Platform::MouseButton::Middle;
        mClickConsumed = true;
    }
    // 드래그는 이 위젯 위에서 시작됐을 때만, 그것도 시작한 그 버튼으로만 이 위젯의 것이다.
    // 밖으로 나가도 버튼을 쥔 동안은 계속된다 — 궤도 회전 중에 커서가 뷰를 벗어나는 일은 흔하다.
    if (mActiveWidget == id)
    {
        interaction.leftDragging = mMouseDown && mActiveButton == Platform::MouseButton::Left;
        interaction.middleDragging = mMiddleDown && mActiveButton == Platform::MouseButton::Middle;
        if (interaction.leftDragging || interaction.middleDragging)
        {
            interaction.dragDeltaX = mMouseDeltaX;
            interaction.dragDeltaY = mMouseDeltaY;
        }
    }
    if (interaction.hovered)
    {
        interaction.wheel = mWheel;
    }
    return interaction;
}

float UIContext::ApplyScroll(const WidgetId id, const UIRect& rect, const float contentHeight)
{
    float& offset = mScrollOffsets[id];
    if (IsHovered(id, rect) && mWheel != 0.0f)
    {
        constexpr float PixelsPerNotch = 48.0f;
        offset -= mWheel * PixelsPerNotch;
    }
    const float maximumOffset = (std::max)(contentHeight - rect.height, 0.0f);
    offset = std::clamp(offset, 0.0f, maximumOffset);
    return offset;
}

void UIContext::UpdateDynamicTexture(
    std::shared_ptr<Assets::TextureData>& texture,
    const unsigned int width, const unsigned int height,
    const std::vector<std::byte>& rgbaPixels)
{
    if (!HasDynamicTexturePixels(width, height, rgbaPixels))
    {
        return;
    }
    // 발행된 프레임과 렌더 스레드는 이전 픽셀을 계속 읽을 수 있다. CPU 스냅숏을 새로
    // 완성한 뒤 교체하고, 같은 크기의 GPU 텍스처는 id와 revision으로 재사용한다.
    auto updated = mDynamicTextureSnapshots.Acquire(rgbaPixels.size());
    const bool sameSize = texture && texture->width == width && texture->height == height;
    updated->id = sameSize ? texture->id : Assets::MakeResourceId(
        Assets::ResourceIdDomain::Dynamic, sNextDynamicTextureIndex++);
    updated->revision = sameSize ? texture->revision + 1 : 1;
    updated->width = width;
    updated->height = height;
    updated->pixels = rgbaPixels;
    texture = mDynamicTextureSnapshots.Publish(std::move(updated));
}

std::shared_ptr<const Assets::TextureData> UIContext::MakeDynamicTexture(
    const unsigned int width, const unsigned int height,
    const std::vector<std::byte>& rgbaPixels)
{
    if (!HasDynamicTexturePixels(width, height, rgbaPixels))
    {
        return nullptr;
    }
    auto texture = mDynamicTextureSnapshots.Acquire(rgbaPixels.size());
    texture->id = Assets::MakeResourceId(
        Assets::ResourceIdDomain::Dynamic, sNextDynamicTextureIndex++);
    texture->width = width;
    texture->height = height;
    texture->pixels = rgbaPixels;
    return mDynamicTextureSnapshots.Publish(std::move(texture));
}

}
