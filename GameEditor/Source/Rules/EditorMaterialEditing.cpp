#include "pch.h"
#include "Rules/EditorMaterialEditing.h"

#include "Rules/EditorFileWrite.h"
#include "Assets/Asset.h"
#include "Core/Json.h"
#include "Platform/TextFile.h"

#include <optional>
#include <string>

namespace GameEditor
{

bool WriteMaterial(
    const GameEngine::Assets::Material& material, const GameEngine::Assets::MaterialData& data)
{
    const std::filesystem::path& path = material.GetSourcePath();

    // 있는 파일은 객체로 파싱해 texture·tint가 아닌 항목을 그대로 보존한다.
    // EditorSpriteSheetEditing.cpp의 WriteSpriteSheet와 같은 틀이다.
    GameEngine::Core::Json::Object members;
    if (const std::optional<std::string> text = GameEngine::Platform::ReadTextFile(path))
    {
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
            // 깨진 파일을 고쳐 쓰면 사람이 적어 둔 것을 잃는다. 그대로 두고 거절한다.
            return false;
        }
    }

    members.insert_or_assign(
        "texture", GameEngine::Core::Json(data.texture.ToString()));
    members.insert_or_assign(
        "tint",
        GameEngine::Core::Json(GameEngine::Core::Json::Array{
            GameEngine::Core::Json(static_cast<double>(data.tint.r)),
            GameEngine::Core::Json(static_cast<double>(data.tint.g)),
            GameEngine::Core::Json(static_cast<double>(data.tint.b)),
            GameEngine::Core::Json(static_cast<double>(data.tint.a)) }));

    return WriteFileAtomically(path, GameEngine::Core::Json(std::move(members)).Dump());
}

}
