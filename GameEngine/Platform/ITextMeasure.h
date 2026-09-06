#pragma once

#include "ITextRasterizer.h"

namespace GameEngine::Platform
{

/// <summary>글자 블록 하나가 차지하는 픽셀 크기다.</summary>
struct TextExtent
{
    float width = 0.0f;
    float height = 0.0f;
};

/// <summary>
/// "이 글자가 얼마나 넓은가"만 답하는 좁은 창구다.
///
/// 이 인터페이스가 따로 있는 이유는 그 질문을 하는 쪽과 답하는 쪽이 다른 세계에 살기 때문이다.
/// 묻는 것은 UI 배치 — 런타임 — 이고, 답을 이미 갖고 있는 것은 문자열을 글리프로 배치해 두는
/// 렌더링 계층의 캐시다. 런타임이 그 캐시를 직접 알면 "런타임은 렌더링 타입을 모른다"는
/// 불변식이 깨지고, 반대로 런타임이 래스터라이저를 직접 잡으면 그 캐시를 우회해 매 프레임 같은
/// 문자열을 다시 배치하게 된다. 그래서 둘 다 피하는 자리에 질문 하나짜리 창구를 둔다 —
/// 텍스트 필드가 Win32가 아니라 <see cref="IClipboard"/>를 받는 것과 같은 모양이다.
///
/// 요청 타입은 래스터라이저의 것을 그대로 쓴다. 매개변수 목록을 따로 만들면 재는 쪽과 그리는
/// 쪽이 요청을 조립하는 방식이 조금씩 갈라지고, 그 갈라짐이 곧 "잰 크기와 그린 크기가 다르다"가
/// 된다.
/// </summary>
class ITextMeasure
{
public:
    virtual ~ITextMeasure() = default;

    ITextMeasure(const ITextMeasure&) = delete;
    ITextMeasure& operator=(const ITextMeasure&) = delete;

    /// <summary>
    /// 새 프레임을 시작한다. 구현이 결과를 보관한다면 그 수명이 여기서 한 칸 넘어간다.
    /// </summary>
    virtual void BeginFrame() = 0;

    /// <summary>
    /// 이 요청이 그려질 때 차지할 크기를 답한다. 답할 수 없으면 0 크기다 — 재지 못했다는 사실이
    /// 배치를 멈추게 해서는 안 되므로, 실패는 "요구하는 크기가 없다"로 읽힌다.
    /// </summary>
    /// <param name="request">잴 문자열과 폰트 설정이다.</param>
    /// <returns>글자가 차지할 픽셀 크기다.</returns>
    [[nodiscard]] virtual TextExtent Measure(const TextRasterizationRequest& request) = 0;

    /// <summary>그림과 같은 배치에서 문자 경계들을 얻는다. 측정을 제공하지 않는 구현은 빈 목록을 반환한다.</summary>
    [[nodiscard]] virtual std::vector<TextCaretStop> MeasureCarets(const TextRasterizationRequest&)
    {
        return {};
    }

protected:
    ITextMeasure() = default;
};

}
