#pragma once

// editor-layer: 2 (Views)

#include <cstdint>
#include <vector>

#include "Diagnostics/Debug.h"
#include "UI/UIContext.h"
#include "Rules/EditorPanelHosts.h"

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 콘솔 패널이다: 엔진 로그를 최신이 위로 보인다. 로그 스냅샷과 그 시점의 리비전을 자신이
/// 소유해, 로그가 바뀐 프레임에만 스냅샷을 다시 받는다.
/// </summary>
class EditorConsolePanel final
{
public:
    EditorConsolePanel(IEditorScale& scale, EditorContext& context, GameEngine::UI::UIContext& ui);

    /// <summary>패널 내용을 그린다. 제목줄 프레임은 셸이 이미 그렸고, 그 아래 영역을 받는다.</summary>
    void Draw(GameEngine::UI::UIRect content);

private:
    /// <summary>96 DPI 기준의 논리 길이를 이 화면의 픽셀로 바꾼다. 셸의 배율을 따른다.</summary>
    [[nodiscard]] float S(float logical) const;

    IEditorScale& mScale;
    EditorContext& mContext;
    GameEngine::UI::UIContext& mUI;

    // 콘솔이 보이는 로그 스냅샷과 그 시점의 로그 번호. 번호가 바뀔 때만 다시 받는다.
    std::vector<GameEngine::Diagnostics::LogEntry> mConsoleLogs;
    std::uint64_t mConsoleLogRevision = ~0ull;
};

}
