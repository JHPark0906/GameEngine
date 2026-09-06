#pragma once

#include <memory>

#include "../Platform/ITextMeasure.h"
#include "TextRasterizationCache.h"

namespace GameEngine::Rendering
{

/// <summary>
/// 글자 크기 질문을 배치 캐시에 위임하는 답변자다.
///
/// 캐시는 이미 문자열마다 완성된 배치를 보관하고 그 안에 블록 크기가 들어 있다. 그래서 재는
/// 일은 새로 하는 계산이 아니라 이미 있는 답을 꺼내는 일이고, 같은 문자열을 매 프레임 다시
/// 묻는 배치 시스템에게 그 차이는 크다 — 셰이핑 한 번이 해시 조회 한 번이 된다.
///
/// 그리는 쪽과 <b>같은 인스턴스</b>로 재는 것이 그만큼 중요하다. 짐을 지는 것은 같은 코드가
/// 아니라 그 인스턴스가 쥔 상태 — 등록된 글꼴 — 이다. 같은 코드로 재더라도 캐시가 둘이면 한쪽에만
/// 등록된 글꼴이 같은 문자열에 두 값의 폭을 주고, 그 어긋남은 로그도 실패도 없이 "라벨이 자기
/// 글자에 맞는 자리를 갖는다"는 약속을 깬다. 그래서 이 답변자는 캐시를 만들지 않고 나눠 쥔다.
/// </summary>
class CachedTextMeasure final : public Platform::ITextMeasure
{
public:
    /// <summary>
    /// 이 세계의 글자 배치 캐시를 나눠 쥔다. 소유하지 않고 나눠 쥐는 것이 요점이다: 그리는 쪽과
    /// 같은 캐시라야 등록된 글꼴이 양쪽에 같이 보이고, 잰 폭과 그린 폭이 갈라지지 않는다.
    /// </summary>
    /// <param name="cache">이 세계의 글자 배치 캐시다. null이면 아무것도 재지 못한다.</param>
    explicit CachedTextMeasure(std::shared_ptr<TextRasterizationCache> cache)
        : mCache(std::move(cache))
    {
    }

    void BeginFrame() override
    {
        // 캐시를 그리는 쪽과 나눠 쥐면 프레임 시작도 두 번 알려질 수 있다. 두 번째는 아무것도
        // 하지 않는 것이 아니라 한 세대를 더 넘기므로, 프레임을 소유한 쪽만 부른다는 계약이
        // 필요하다 — 오늘 그것은 장면 패스이고, 이 잣대는 알리지 않는다.
    }

    [[nodiscard]] Platform::TextExtent Measure(
        const Platform::TextRasterizationRequest& request) override
    {
        if (!mCache)
        {
            return {};
        }
        const std::shared_ptr<const ShapedText> shaped = mCache->Resolve(request);
        if (!shaped)
        {
            return {};
        }
        return { static_cast<float>(shaped->width), static_cast<float>(shaped->height) };
    }

    [[nodiscard]] std::vector<Platform::TextCaretStop> MeasureCarets(
        const Platform::TextRasterizationRequest& request) override
    {
        const auto shaped = mCache ? mCache->Resolve(request) : nullptr;
        return shaped ? shaped->caretStops : std::vector<Platform::TextCaretStop>{};
    }

private:
    std::shared_ptr<TextRasterizationCache> mCache;
};

}
