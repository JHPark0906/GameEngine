#pragma once

// editor-layer: 0 (Rules)

#include <array>

#include "UI/UIContext.h"

namespace GameEngine::Platform
{
class IContentSource;
}

namespace GameEditor
{

/// <summary>역할 하나에 배정된 폰트 파일이다.</summary>
struct EditorFont
{
    GameEngine::UI::UIFontRole role;
    const char* relativePath;
};

/// <summary>
/// 에디터 UI는 실행 파일 옆에 배포된 자체 Content의 폰트를 쓴다.
/// 제목은 굵게, 본문은 그보다 가볍게, 콘솔은 로그 열을 맞추는 고정폭으로 배정한다.
///
/// 이 목록이 헤더에 있는 이유는 <b>이것을 읽는 쪽이 둘이기 때문</b>이다: 에디터가 이 파일들을
/// 싣고, 시험이 「정말 실렸는가」를 묻는다. 목록을 시험이 따로 적으면 그 둘은 언젠가 갈리고,
/// 갈린 날 시험은 <b>에디터가 청하지 않는 파일</b>이 있는지를 검사하게 된다.
///
/// 그 질문이 필요한 이유는 실패가 조용하기 때문이다. 파일을 못 찾으면 경고 한 줄이 남고 그
/// 역할은 비며, 비면 래스터라이저가 먼저 등록된 다른 역할의 폰트로 보낸다. 화면에는 여전히
/// 글자가 나오므로 사람 눈으로는 구별되지 않는다 — 특히 그 다른 역할도 한글을 그릴 수 있는
/// 기계에서는.
/// </summary>
inline constexpr std::array<EditorFont, 3> EditorFonts{
    EditorFont{ GameEngine::UI::UIFontRole::Title, "Fonts/NanumSquareNeoOTF-Eb.otf" },
    EditorFont{ GameEngine::UI::UIFontRole::Body, "Fonts/NanumSquareNeoOTF-Bd.otf" },
    EditorFont{ GameEngine::UI::UIFontRole::Monospace, "Fonts/D2Coding-Ver1.3.3-20260725.ttf" },
};

/// <summary>
/// 위 목록을 그 소스에서 읽어 UI 문맥의 역할에 배정한다. 하나라도 읽히지 않거나 등록이
/// 거절되면 <b>경고 한 줄을 남기고 그 역할만 빈다</b> — 편집기는 그대로 뜨고, 그 역할의 글자는
/// 먼저 등록에 성공한 다른 역할의 폰트로 그려진다.
///
/// 셸의 멤버가 아니라 자유 함수인 이유는 이것을 <b>창 없이 부를 수 있어야</b> 하기 때문이다.
/// 「동봉 폰트가 실제로 실렸는가」는 화면으로 답할 수 없는 질문이고 — 다른 역할의 폰트도 한글을
/// 그릴 수 있으면 fallback도 한글로 그려진다 — 그래서 시험이 이 함수를 그대로 불러 답한다.
/// </summary>
/// <param name="source">폰트 파일을 읽을 콘텐츠 소스다. 편집기는 실행 파일 옆을 준다.</param>
/// <param name="ui">역할을 배정받을 UI 문맥이다.</param>
void LoadEditorFonts(const GameEngine::Platform::IContentSource& source, GameEngine::UI::UIContext& ui);

/// <summary>
/// 위 목록을 그 소스에서 읽어, 편집기 UI가 아니라 <b>장면의</b> 글자 배치 캐시에 등록한다.
///
/// Application의 sceneTextCache에도 폰트를 등록해야 한다. 폰트가 없는 캐시는
/// <c>FontLibrary::SelectGlyph</c>의 규칙에 따라 모든 요청에 실패한다.
///
/// 모르는 이름의 요청은 먼저 등록된 폰트로 그려진다. 편집기 UI와 장면이 같은 목록을
/// 사용하도록 이 함수도 <see cref="EditorFonts"/> 배열의 파일을 같은 순서로 등록한다.
/// </summary>
/// <param name="source">폰트 파일을 읽을 콘텐츠 소스다.</param>
/// <param name="sceneTextCache">등록할 대상이다.</param>
void RegisterSceneFonts(
    const GameEngine::Platform::IContentSource& source,
    GameEngine::Rendering::TextRasterizationCache& sceneTextCache);

}
