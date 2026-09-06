#pragma once

#include <memory>
#include <string>
#include <utility>

#include "../Math/Color.h"
#include "../Math/Vector.h"
#include "Renderer.h"

namespace GameEngine::Runtime
{

/// <summary>글리프 아틀라스를 사용해 UTF-8 텍스트를 출력하는 렌더러이다.</summary>
class TextRenderer final : public Renderer
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const ComponentType& StaticType();
    [[nodiscard]] const ComponentType& GetComponentType() const override { return StaticType(); }
    enum class Space : unsigned char
    {
        World,
        Screen,
    };

    enum class Alignment : unsigned char
    {
        Left,
        Center,
        Right,
    };

    /// <summary>
    /// 글자 블록이 자기 사각형 안에서 세로로 놓이는 자리다. 화면 공간 텍스트에만 뜻이 있다 —
    /// 월드 텍스트에는 담을 사각형이 없다.
    ///
    /// 이것이 여기 있는 이유는 호출자가 답할 수 없는 질문이기 때문이다. 가운데에 놓으려면 글자
    /// 블록의 높이를 알아야 하고, 그것은 글꼴과 크기와 줄 수가 정한다 — 위젯이 어림잡으면 글꼴이
    /// 바뀔 때 그 어림이 틀리는데, 틀렸다는 것을 아무도 모른다.
    /// </summary>
    enum class VerticalAlignment : unsigned char
    {
        Top,
        Middle,
        Bottom,
    };

    [[nodiscard]] const std::string& GetText() const { return mText; }
    void SetText(std::string text) { mText = std::move(text); }

    [[nodiscard]] const std::string& GetFontFamily() const { return mFontFamily; }
    void SetFontFamily(std::string fontFamily);

    [[nodiscard]] float GetFontSize() const { return mFontSize; }
    void SetFontSize(float fontSize);

    [[nodiscard]] const Math::Color& GetColor() const { return mColor; }
    void SetColor(const Math::Color& color) { mColor = color; }

    /// <summary>글자 블록 뒤에 그릴 배경색이다. 기본 알파 0이면 배경 draw를 만들지 않는다.</summary>
    [[nodiscard]] const Math::Color& GetBackgroundColor() const { return mBackgroundColor; }
    void SetBackgroundColor(const Math::Color& color) { mBackgroundColor = color; }

    /// <summary>
    /// 글자 블록 바깥의 한쪽 여백이다. x는 좌우 각각, y는 상하 각각에 적용하는 논리 픽셀이다.
    /// 화면 텍스트는 Canvas 배율을, 월드 텍스트는 PixelsPerUnit을 통해 그리는 단위로 변환한다.
    /// 글자 배치나 줄바꿈 폭은 바꾸지 않는다.
    /// </summary>
    [[nodiscard]] const Math::Vector2& GetBackgroundPadding() const { return mBackgroundPadding; }
    /// <summary>배경 여백을 설정한다. 음수나 유한하지 않은 성분은 0으로 정리한다.</summary>
    void SetBackgroundPadding(const Math::Vector2& padding);

    [[nodiscard]] Space GetSpace() const { return mSpace; }
    void SetSpace(Space space) { mSpace = space; }

    [[nodiscard]] Alignment GetAlignment() const { return mAlignment; }
    void SetAlignment(Alignment alignment) { mAlignment = alignment; }

    [[nodiscard]] VerticalAlignment GetVerticalAlignment() const { return mVerticalAlignment; }
    void SetVerticalAlignment(const VerticalAlignment alignment)
    {
        mVerticalAlignment = alignment;
    }

    [[nodiscard]] float GetMaxWidth() const { return mMaxWidth; }
    void SetMaxWidth(float maxWidth);

    [[nodiscard]] float GetLineSpacing() const { return mLineSpacing; }
    void SetLineSpacing(float lineSpacing);

    [[nodiscard]] float GetPixelsPerUnit() const { return mPixelsPerUnit; }
    void SetPixelsPerUnit(float pixelsPerUnit);

private:
    std::string mText;
    std::string mFontFamily = "Segoe UI";
    Math::Color mColor = Math::Color::White;
    Math::Color mBackgroundColor = Math::Color::Clear;
    Math::Vector2 mBackgroundPadding{ 0.0f, 0.0f };
    float mFontSize = 32.0f;
    float mMaxWidth = 0.0f;
    float mLineSpacing = 1.0f;
    float mPixelsPerUnit = 100.0f;
    Space mSpace = Space::Screen;
    Alignment mAlignment = Alignment::Left;
    VerticalAlignment mVerticalAlignment = VerticalAlignment::Top;
};

}
