#include "pch.h"
#include "Asset.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <utility>

namespace GameEngine::Assets
{

Asset::Asset(
    const AssetKey id,
    const AssetType type,
    std::filesystem::path relativePath,
    std::filesystem::path sourcePath,
    const std::uint64_t contentHash,
    const std::uintmax_t fileSize)
    : mId(id),
      mType(type),
      mRelativePath(std::move(relativePath)),
      mSourcePath(std::move(sourcePath)),
      mContentHash(contentHash),
      mFileSize(fileSize)
{
}

ProjectSettingsAsset::ProjectSettingsAsset(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::ProjectSettings, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

SceneAsset::SceneAsset(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::Scene, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

int Sprite::Sheet::GetFrameCount() const
{
    const int cells = (std::max)(columns, 1) * (std::max)(rows, 1);
    return frameCount > 0 ? (std::min)(frameCount, cells) : cells;
}

Sprite::Sprite(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize,
    const float pixelsPerUnit, const Border border, const Sheet sheet)
    : Asset(id, AssetType::Sprite, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize),
      mPixelsPerUnit((std::max)(pixelsPerUnit, 0.001f)),
      mBorder{
          (std::max)(border.left, 0.0f),
          (std::max)(border.top, 0.0f),
          (std::max)(border.right, 0.0f),
          (std::max)(border.bottom, 0.0f)},
      mSheet{
          (std::max)(sheet.columns, 1),
          (std::max)(sheet.rows, 1),
          (std::max)(sheet.frameCount, 0),
          sheet.frameRate > 0.0f ? sheet.frameRate : 12.0f}
{
}

void Sprite::GetFrameRect(
    const int frameIndex, float& u, float& v, float& width, float& height) const
{
    // 프레임은 좌상단에서 시작해 행 우선으로 센다 — 시트를 그린 사람이 읽는 순서 그대로다.
    const int frames = mSheet.GetFrameCount();
    const int wrapped = ((frameIndex % frames) + frames) % frames;
    const int column = wrapped % mSheet.columns;
    const int row = wrapped / mSheet.columns;
    width = 1.0f / static_cast<float>(mSheet.columns);
    height = 1.0f / static_cast<float>(mSheet.rows);
    u = static_cast<float>(column) * width;
    v = static_cast<float>(row) * height;
}


bool Sprite::HasBorder() const
{
    return mBorder.left > 0.0f || mBorder.top > 0.0f ||
        mBorder.right > 0.0f || mBorder.bottom > 0.0f;
}

namespace
{
    /// <summary>
    /// 사이드카와 매니페스트가 같은 모양으로 싣는 스프라이트 메타데이터를 읽는다. 두 파일의
    /// 항목 이름과 검사가 같아야 하므로 검사도 한 벌만 둔다 — 다른 점은 "항목이 없어도 되는가"
    /// 뿐이고, 그것은 부르는 쪽이 정한다.
    /// </summary>
    void ReadSpriteMembers(
        const Core::Json& json,
        float& pixelsPerUnit,
        Sprite::Border& border,
        Sprite::Sheet& sheet)
    {
        if (const Core::Json* value = json.Find("pixelsPerUnit"))
        {
            pixelsPerUnit = value->Get<float>();
            if (pixelsPerUnit <= 0.0f)
            {
                throw Core::JsonError("pixelsPerUnit must be positive");
            }
        }
        if (const Core::Json* value = json.Find("border"))
        {
            if (!value->IsArray() || value->Size() != 4 ||
                !value->At(0).IsNumber() || !value->At(1).IsNumber() ||
                !value->At(2).IsNumber() || !value->At(3).IsNumber())
            {
                throw Core::JsonError("border must be [left, top, right, bottom]");
            }
            border = {
                value->At(0).Get<float>(), value->At(1).Get<float>(),
                value->At(2).Get<float>(), value->At(3).Get<float>()};
            if (border.left < 0.0f || border.top < 0.0f ||
                border.right < 0.0f || border.bottom < 0.0f)
            {
                throw Core::JsonError("border values cannot be negative");
            }
        }
        if (const Core::Json* value = json.Find("sheet"))
        {
            // 시트는 선택 사항이다: 없으면 이미지 한 장이 곧 한 프레임이다. 있으면 열과 행이
            // 격자를 정하고, frameCount는 마지막 줄의 빈 칸을 잘라 낸다.
            sheet.columns = value->Value("columns", 1);
            sheet.rows = value->Value("rows", 1);
            sheet.frameCount = value->Value("frameCount", 0);
            sheet.frameRate = value->Value("frameRate", 12.0f);
            if (sheet.columns <= 0 || sheet.rows <= 0 || sheet.frameCount < 0 ||
                sheet.frameCount > sheet.columns * sheet.rows || sheet.frameRate <= 0.0f)
            {
                throw Core::JsonError(
                    "sheet needs positive columns, rows, and frameRate, and a frameCount the "
                    "grid can hold");
            }
        }
    }
}

Core::Json Asset::MakeDefaultSidecar(const Core::Guid guid) const
{
    return Core::Json(Core::Json::Object{
        { "format", Core::Json(std::string(MetaFormat)) },
        { "guid", Core::Json(guid.ToString()) } });
}

Core::Json Sprite::MakeDefaultSidecar(const Core::Guid guid) const
{
    Core::Json::Object members = Asset::MakeDefaultSidecar(guid).AsObject();
    members.emplace("pixelsPerUnit", Core::Json(static_cast<double>(mPixelsPerUnit)));
    return Core::Json(std::move(members));
}


void Sprite::ReadSidecarMetadata(const Core::Json& json)
{
    // 사람이 쓰는 파일이라 적지 않은 항목은 기본값으로 둔다. 전부 읽어 낸 뒤에야 이 객체에
    // 옮기므로, 뒤쪽 항목이 잘못된 사이드카가 앞쪽 항목만 반쯤 적용된 스프라이트를 남기지 않는다.
    float parsedPixelsPerUnit = mPixelsPerUnit;
    Border parsedBorder = mBorder;
    Sheet parsedSheet = mSheet;
    ReadSpriteMembers(json, parsedPixelsPerUnit, parsedBorder, parsedSheet);
    mPixelsPerUnit = parsedPixelsPerUnit;
    mBorder = parsedBorder;
    mSheet = parsedSheet;
}

void Sprite::WriteManifestMetadata(Core::Json::Object& members) const
{
    members.emplace("pixelsPerUnit", Core::Json(static_cast<double>(mPixelsPerUnit)));
    members.emplace("border", Core::Json(Core::Json::Array{
        Core::Json(static_cast<double>(mBorder.left)),
        Core::Json(static_cast<double>(mBorder.top)),
        Core::Json(static_cast<double>(mBorder.right)),
        Core::Json(static_cast<double>(mBorder.bottom)) }));
    members.emplace("sheet", Core::Json(Core::Json::Object{
        { "columns", Core::Json(static_cast<double>(mSheet.columns)) },
        { "rows", Core::Json(static_cast<double>(mSheet.rows)) },
        { "frameCount", Core::Json(static_cast<double>(mSheet.frameCount)) },
        { "frameRate", Core::Json(static_cast<double>(mSheet.frameRate)) } }));
}

void Sprite::ReadManifestMetadata(const Core::Json& json)
{
    // 매니페스트는 빌드가 쓴 것이라 이 두 항목은 반드시 있다. 없으면 손상이므로 던진다 —
    // 조용히 기본값으로 읽으면 모든 스프라이트가 잘못된 배율로 그려진다. 시트는 선택 항목이라,
    // 이것이 없는 패키지도 그대로 읽힌다.
    if (!json.Find("pixelsPerUnit") || !json.Find("border"))
    {
        throw Core::JsonError("a Sprite manifest record needs pixelsPerUnit and border");
    }
    ReadSpriteMembers(json, mPixelsPerUnit, mBorder, mSheet);
}

Mesh::Mesh(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::Mesh, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

Skeleton::Skeleton(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::Skeleton, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

AnimationClip::AnimationClip(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::AnimationClip, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

SkinnedMesh::SkinnedMesh(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::SkinnedMesh, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

Font::Font(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::Font, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

AudioClip::AudioClip(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::AudioClip, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

Icon::Icon(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::Icon, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

Material::Material(
    const AssetKey id, std::filesystem::path relativePath, std::filesystem::path sourcePath,
    const std::uint64_t contentHash, const std::uintmax_t fileSize)
    : Asset(id, AssetType::Material, std::move(relativePath), std::move(sourcePath),
          contentHash, fileSize)
{
}

}
