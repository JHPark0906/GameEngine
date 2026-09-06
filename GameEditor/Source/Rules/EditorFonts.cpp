#include "Rules/EditorFonts.h"

#include <cstddef>
#include <vector>

#include "Diagnostics/Debug.h"
#include "Platform/IContentSource.h"
#include "Rendering/TextRasterizationCache.h"

namespace GameEditor
{

void LoadEditorFonts(
    const GameEngine::Platform::IContentSource& source, GameEngine::UI::UIContext& ui)
{
    // 파일 경로가 아니라 콘텐츠 소스로 읽는다: 패키징된 에디터에는 디스크의 Fonts 폴더가 없을
    // 수 있고, 그때도 같은 코드가 같은 바이트를 받아야 한다. 하나라도 실패하면 그 역할만 다른
    // 역할의 폰트로 대신 그려지고(먼저 등록된 것이 있다면), 에디터는 그대로 뜬다.
    for (const EditorFont& font : EditorFonts)
    {
        std::vector<std::byte> bytes;
        if (!source.Read(font.relativePath, bytes) || !ui.LoadFont(font.role, bytes))
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Failed to load an editor font; that role will draw with another loaded font "
                "instead. path=",
                font.relativePath);
        }
    }
}

namespace
{
    /// <summary>
    /// 장면 쪽 등록에 쓰는 별명이다. 편집기 UI의 역할별 별명과 다른 이름 공간을 쓰는 이유는
    /// 이 캐시에는 역할이라는 개념이 없기 때문이다 — 장면의 텍스트는 아무 이름이나 청할 수
    /// 있고, 등록되지 않은 이름은 등록 순서상 가장 먼저인 폰트로 대신 그려진다. 그래서 이
    /// 별명이 실제로 요청과 맞아떨어질 일은 드물고, 이 함수의 목적도 그것이 아니다: 폰트가
    /// 하나도 없어 <b>모든</b> 요청이 실패하는 상태를 벗어나는 것이다.
    /// </summary>
    [[nodiscard]] const char* SceneFontAlias(const GameEngine::UI::UIFontRole role)
    {
        switch (role)
        {
        case GameEngine::UI::UIFontRole::Title: return "scene.title";
        case GameEngine::UI::UIFontRole::Body: return "scene.body";
        case GameEngine::UI::UIFontRole::Monospace: return "scene.monospace";
        }
        return "scene.body";
    }
}

void RegisterSceneFonts(
    const GameEngine::Platform::IContentSource& source,
    GameEngine::Rendering::TextRasterizationCache& sceneTextCache)
{
    for (const EditorFont& font : EditorFonts)
    {
        std::vector<std::byte> bytes;
        if (!source.Read(font.relativePath, bytes) ||
            !sceneTextCache.RegisterFont(SceneFontAlias(font.role), bytes))
        {
            GameEngine::Diagnostics::Debug::LogWarning(
                "Failed to read a scene font; it will not be available for scene text to fall "
                "back on. path=",
                font.relativePath);
        }
    }
}

}
