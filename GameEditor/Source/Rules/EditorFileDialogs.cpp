#include "Rules/EditorFileDialogs.h"

namespace GameEditor
{

std::optional<std::filesystem::path> PlatformFileDialogs::ShowOpen(
    const GameEngine::Platform::PlatformServices::FileDialogRequest& request)
{
    return GameEngine::Platform::PlatformServices::ShowOpenFileDialog(request);
}

std::optional<std::filesystem::path> PlatformFileDialogs::ShowSave(
    const GameEngine::Platform::PlatformServices::FileDialogRequest& request)
{
    return GameEngine::Platform::PlatformServices::ShowSaveFileDialog(request);
}

}
