#include "Rules/EditorFileWrite.h"

#include "Core/TextFile.h"
#include "Diagnostics/Debug.h"

namespace GameEditor
{

bool WriteFileAtomically(const std::filesystem::path& fullPath, const std::string& text)
{
    const GameEngine::Core::FileWriteResult written =
        GameEngine::Core::WriteTextFileAtomically(fullPath, text);
    if (!written)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to write a file. path=", fullPath.string(),
            ", reason=", written.Describe(),
            written.systemError ? ", error=" : "",
            written.systemError ? written.systemError.message() : std::string{});
    }
    return static_cast<bool>(written);
}

}
