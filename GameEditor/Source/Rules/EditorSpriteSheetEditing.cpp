#include "pch.h"
#include "Rules/EditorSpriteSheetEditing.h"

#include "Rules/EditorFileWrite.h"
#include "Assets/AssetDatabase.h"
#include "Core/Json.h"
#include "Platform/TextFile.h"

#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace GameEditor
{

namespace
{
    [[nodiscard]] bool IsValidSheet(const GameEngine::Assets::Sprite::Sheet& sheet)
    {
        return sheet.columns > 0 && sheet.rows > 0 && sheet.frameCount >= 0 &&
            sheet.frameCount <= sheet.columns * sheet.rows && sheet.frameRate > 0.0f;
    }

    /// <summary>이 스프라이트의 사이드카가 놓일 절대 경로다. 아직 없으면 원본 옆의 새 이름이다.</summary>
    [[nodiscard]] std::filesystem::path ResolveSidecarPath(
        const GameEngine::Assets::AssetDatabase& database, const GameEngine::Assets::Sprite& sprite,
        const std::filesystem::path& existingRelative)
    {
        if (!existingRelative.empty())
        {
            return database.GetProjectRootPath() / existingRelative;
        }
        const std::string_view suffix = sprite.GetSidecarSuffix();
        std::filesystem::path path = sprite.GetSourcePath();
        path += std::filesystem::path(std::u8string(suffix.begin(), suffix.end()));
        return path;
    }
}

bool WriteSpriteSheet(
    const GameEngine::Assets::AssetDatabase& database, const GameEngine::Assets::Sprite& sprite,
    const GameEngine::Assets::Sprite::Sheet& sheet)
{
    if (!IsValidSheet(sheet))
    {
        return false;
    }

    const std::filesystem::path existingRelative = database.GetSidecarPath(sprite);
    const std::filesystem::path absolutePath = ResolveSidecarPath(database, sprite, existingRelative);

    // 있는 사이드카는 객체로 파싱해 시트가 아닌 항목(guid, pixelsPerUnit, border...)을 그대로
    // 보존한다. AssetIdentityIssue.cpp의 IssueMissingIdentities가 정체성을 더할 때 쓰는 것과
    // 같은 틀이다.
    GameEngine::Core::Json::Object members;
    if (!existingRelative.empty())
    {
        const std::optional<std::string> text = GameEngine::Platform::ReadTextFile(absolutePath);
        if (!text)
        {
            return false;
        }
        try
        {
            const GameEngine::Core::Json parsed = GameEngine::Core::Json::Parse(*text);
            if (parsed.IsObject())
            {
                members = parsed.AsObject();
            }
        }
        catch (const GameEngine::Core::JsonError&)
        {
            // 깨진 사이드카를 고쳐 쓰면 사람이 적어 둔 것을 잃는다. 그대로 두고 거절한다 —
            // 등록할 때 이미 그 사실이 로그에 남았다.
            return false;
        }
    }
    if (!members.contains("format"))
    {
        members.emplace("format", GameEngine::Core::Json(std::string(GameEngine::Assets::MetaFormat)));
    }
    members.insert_or_assign(
        "sheet",
        GameEngine::Core::Json(GameEngine::Core::Json::Object{
            { "columns", GameEngine::Core::Json(static_cast<double>(sheet.columns)) },
            { "rows", GameEngine::Core::Json(static_cast<double>(sheet.rows)) },
            { "frameCount", GameEngine::Core::Json(static_cast<double>(sheet.frameCount)) },
            { "frameRate", GameEngine::Core::Json(static_cast<double>(sheet.frameRate)) } }));

    return WriteFileAtomically(absolutePath, GameEngine::Core::Json(std::move(members)).Dump());
}

}
