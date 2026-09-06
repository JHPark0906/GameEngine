#include "Views/EditorConsolePanel.h"

#include "Rules/EditorPanelCommon.h"
#include "Math/Color.h"

namespace GameEditor
{

namespace
{
    using GameEngine::Math::Color;
    using GameEngine::UI::UIRect;
}

EditorConsolePanel::EditorConsolePanel(
    IEditorScale& scale, EditorContext& context, GameEngine::UI::UIContext& ui)
    : mScale(scale), mContext(context), mUI(ui)
{
}

float EditorConsolePanel::S(const float logical) const
{
    return mScale.S(logical);
}

void EditorConsolePanel::Draw(const GameEngine::UI::UIRect content)
{
    // 최신이 위다: 즉시 모드 목록은 위에서부터 그려지고, 콘솔에서 찾는 것은 거의 언제나 마지막
    // 로그이기 때문이다.
    // 로그가 바뀌었을 때만 스냅샷을 다시 받는다. 콘솔은 매 프레임 그려지지만 로그는 가끔 온다.
    const std::uint64_t logRevision = GameEngine::Diagnostics::Debug::GetLogRevision();
    if (logRevision != mConsoleLogRevision)
    {
        mConsoleLogs = GameEngine::Diagnostics::Debug::GetRecentLogs();
        mConsoleLogRevision = logRevision;
    }
    const std::vector<GameEngine::Diagnostics::LogEntry>& logs = mConsoleLogs;
    const float offset = mUI.ApplyScroll(
        GameEngine::UI::MakeWidgetId("console-scroll"), content,
        static_cast<float>(logs.size()) * S(RowHeight));
    float y = content.y - offset;
    for (auto entry = logs.rbegin(); entry != logs.rend(); ++entry)
    {
        const UIRect rowRect{ content.x, y, content.width, S(RowHeight) };
        y += S(RowHeight);
        if (rowRect.y < content.y || rowRect.GetBottom() > content.GetBottom())
        {
            continue;
        }
        const Color color = entry->level == GameEngine::Diagnostics::LogLevel::Error ? ErrorColor
            : entry->level == GameEngine::Diagnostics::LogLevel::Warning ? WarningColor
            : DimTextColor;
        // 로그는 고정폭이다: 경로와 수치가 줄마다 같은 자리에 서야 훑어 읽을 수 있다.
        mUI.DrawLabel(
            { rowRect.x + S(Padding), rowRect.y, rowRect.width - 2.0f * S(Padding), rowRect.height },
            entry->message, color, SecondaryFontSize, GameEngine::UI::TextAlign::Left,
            GameEngine::UI::UIFontRole::Monospace);
    }
}

}
