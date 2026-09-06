#include "Rules/EditorMaterialTemplate.h"

#include <system_error>

#include "Platform/TextFile.h"
#include "Diagnostics/Debug.h"

namespace GameEditor
{

namespace
{
    /// <summary>
    /// 텍스처 없이 흰색 tint만 있는 머티리얼이다. <c>MaterialImporter</c>가 읽는 그대로의
    /// 필드 이름이다 — 빈 문자열인 texture는 참조 없음으로 파싱된다.
    /// </summary>
    constexpr std::string_view DefaultMaterialJson =
        "{\n"
        "    \"texture\": \"\",\n"
        "    \"tint\": [1.0, 1.0, 1.0, 1.0]\n"
        "}\n";
}

std::optional<std::filesystem::path> CreateMaterialAsset(
    const std::filesystem::path& chosenFilePath)
{
    std::error_code error;
    if (std::filesystem::exists(chosenFilePath, error) && !error)
    {
        // 사람이 쓴 머티리얼을 템플릿으로 지우는 것은 되돌릴 수 없다.
        GameEngine::Diagnostics::Debug::LogError(
            "A file already exists at that path. path=", chosenFilePath.string());
        return std::nullopt;
    }
    if (error)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to inspect the chosen material path. path=", chosenFilePath.string(),
            ", error=", error.message());
        return std::nullopt;
    }

    std::filesystem::create_directories(chosenFilePath.parent_path(), error);
    if (error)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to create the material's directory. path=",
            chosenFilePath.parent_path().string(), ", error=", error.message());
        return std::nullopt;
    }

    const GameEngine::Platform::FileWriteResult written =
        GameEngine::Platform::WriteTextFile(chosenFilePath, DefaultMaterialJson);
    if (!written)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to write a new material file. path=", chosenFilePath.string(),
            ", reason=", written.Describe());
        return std::nullopt;
    }
    return chosenFilePath;
}

}
