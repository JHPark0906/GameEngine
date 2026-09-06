#include "Document/EditorSettingsStore.h"

#include <utility>

#include "Diagnostics/Debug.h"
#include "Platform/PlatformServices.h"

namespace GameEditor
{

std::filesystem::path EditorSettingsStore::GetDefaultFilePath()
{
    return GameEngine::Platform::PlatformServices::GetExecutableDirectory() /
        "GameEditor.settings.json";
}

EditorSettingsStore::EditorSettingsStore()
    : EditorSettingsStore(GetDefaultFilePath())
{
}

EditorSettingsStore::EditorSettingsStore(std::filesystem::path filePath)
    : mFilePath(std::move(filePath))
{
    // 없는 파일과 깨진 파일은 조용히 기본값이다.
    mSettings = GameEngine::App::EditorSettings::Load(mFilePath);
    mLastSavedText = GameEngine::App::EditorSettings::ToText(mSettings);
}

void EditorSettingsStore::UpdatePanelLayout(std::vector<std::size_t> panelInSlot)
{
    if (mSettings.panelInSlot == panelInSlot)
    {
        return;
    }
    mSettings.panelInSlot = std::move(panelInSlot);
    SaveIfChanged();
}

void EditorSettingsStore::UpdateConsoleWindow(
    const bool floating, const float x, const float y, const float width, const float height)
{
    if (mSettings.consoleFloating == floating && mSettings.consoleWindowX == x &&
        mSettings.consoleWindowY == y && mSettings.consoleWindowWidth == width &&
        mSettings.consoleWindowHeight == height)
    {
        return;
    }
    mSettings.consoleFloating = floating;
    mSettings.consoleWindowX = x;
    mSettings.consoleWindowY = y;
    mSettings.consoleWindowWidth = width;
    mSettings.consoleWindowHeight = height;
    SaveIfChanged();
}

void EditorSettingsStore::SetGridSnapEnabled(const bool enabled)
{
    if (mSettings.gridSnapEnabled == enabled)
    {
        return;
    }
    mSettings.gridSnapEnabled = enabled;
    SaveIfChanged();
}

void EditorSettingsStore::UpdateSceneCamera(
    const GameEngine::Math::Vector3& pivot, const float distance, const float yawDegrees,
    const float pitchDegrees)
{
    if (mSettings.hasSceneCamera && mSettings.cameraPivot == pivot &&
        mSettings.cameraDistance == distance && mSettings.cameraYawDegrees == yawDegrees &&
        mSettings.cameraPitchDegrees == pitchDegrees)
    {
        return;
    }
    mSettings.hasSceneCamera = true;
    mSettings.cameraPivot = pivot;
    mSettings.cameraDistance = distance;
    mSettings.cameraYawDegrees = yawDegrees;
    mSettings.cameraPitchDegrees = pitchDegrees;
    SaveIfChanged();
}

void EditorSettingsStore::RememberLastProject(const std::filesystem::path& projectFilePath)
{
    mSettings.lastProjectPath = projectFilePath;
    SaveIfChanged();
}

void EditorSettingsStore::RememberLastScene(const unsigned int projectSceneId)
{
    mSettings.lastSceneId = projectSceneId;
    mSettings.hasLastScene = true;
    SaveIfChanged();
}

void EditorSettingsStore::SaveIfChanged()
{
    std::string text = GameEngine::App::EditorSettings::ToText(mSettings);
    if (text == mLastSavedText)
    {
        return;
    }
    if (!GameEngine::App::EditorSettings::Save(mSettings, mFilePath))
    {
        // 설정을 못 쓰는 것은 편집을 막을 일이 아니다. 로그로 말하고 다음 사건에 다시 시도한다.
        GameEngine::Diagnostics::Debug::LogWarning(
            "The editor settings could not be saved. path=", mFilePath.string());
        return;
    }
    mLastSavedText = std::move(text);
}

}
