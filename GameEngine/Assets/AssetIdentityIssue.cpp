#include "pch.h"
#include "AssetIdentityIssue.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Asset.h"
#include "AssetDatabase.h"
#include "../Core/Guid.h"
#include "../Core/Json.h"
#include "../Platform/TextFile.h"
#include "../Diagnostics/Debug.h"
#include "../Platform/IContentSource.h"

namespace GameEngine::Assets
{

namespace
{
    /// <summary>
    /// 사이드카 하나를 놓는다. 쓰다 중간에 실패해도 반쪽짜리 정체성이 남지 않게 원자적으로 쓴다 —
    /// 잘린 사이드카는 읽힐 수 없는 정체성이고, 그것은 정체성이 없는 것보다 나쁘다.
    /// </summary>
    [[nodiscard]] bool WriteSidecar(const std::filesystem::path& path, const std::string& text)
    {
        const Platform::FileWriteResult written = Platform::WriteTextFileAtomically(path, text);
        if (!written)
        {
            Diagnostics::Debug::LogError(
                "Failed to write asset metadata. path=", path.string(),
                ", reason=", written.Describe());
        }
        return static_cast<bool>(written);
    }
}

std::size_t IssueMissingIdentities(
    const AssetDatabase& database, const Platform::IContentSource& source)
{
    const std::filesystem::path& projectRoot = database.GetProjectRootPath();
    std::size_t written = 0;
    for (const std::unique_ptr<Asset>& asset : database.GetAssets())
    {
        // 이미 정체성이 있으면 손대지 않는다. 있는 것이 언제나 이긴다 — 다시 발급하면 그것을
        // 가리키던 모든 참조가 아무것도 가리키지 않게 된다.
        if (asset->GetGuid().IsValid())
        {
            continue;
        }
        const std::string_view suffix = asset->GetSidecarSuffix();
        if (suffix.empty())
        {
            continue;
        }

        const Core::Guid guid = Core::MakeGuid();
        const std::filesystem::path existing = database.GetSidecarPath(*asset);
        if (existing.empty())
        {
            // 사이드카가 없다: 기본 내용에 정체성을 실어 새로 놓는다.
            std::filesystem::path sidecarPath = asset->GetSourcePath();
            sidecarPath += std::filesystem::path(std::u8string(suffix.begin(), suffix.end()));
            if (!WriteSidecar(sidecarPath, asset->MakeDefaultSidecar(guid).Dump()))
            {
                continue;
            }
            Diagnostics::Debug::Log(
                "Created asset metadata. path=", sidecarPath.filename().string(),
                ", guid=", guid.ToString());
        }
        else
        {
            // 사이드카가 있지만 정체성이 없으면 그 파일에 정체성만 더한다.
            // 새 사이드카를 만들면 설정이 둘이 되고 사람이 적은 기존 값이 빠질 수 있다.
            std::vector<std::byte> bytes;
            if (!source.Read(existing, bytes))
            {
                continue;
            }
            Core::Json::Object members;
            try
            {
                const Core::Json parsed =
                    Core::Json::ParseBytes(bytes);
                if (parsed.IsObject())
                {
                    members = parsed.AsObject();
                }
            }
            catch (const std::exception&)
            {
                // 깨진 사이드카를 고쳐 쓰면 사람이 적어 둔 것을 잃는다. 그대로 두고 넘어간다 —
                // 등록할 때 이미 그 사실이 로그에 남았다.
                continue;
            }
            // 파일이 이미 정체성을 갖고 있으면 데이터베이스가 오래된 것이지 정체성이 없는
            // 것이 아니다. 덮어쓰면 그것을 가리키던 참조가 전부 끊긴다.
            if (Core::Guid::Parse(
                    Core::Json(members).Value("guid", std::string{})))
            {
                continue;
            }
            members.insert_or_assign("guid", Core::Json(guid.ToString()));
            members.emplace("format", Core::Json(std::string(
                MetaFormat)));
            if (!WriteSidecar(
                    projectRoot / existing, Core::Json(std::move(members)).Dump()))
            {
                continue;
            }
            Diagnostics::Debug::Log(
                "Gave an identity to existing asset metadata. path=",
                existing.filename().string(), ", guid=", guid.ToString());
        }
        ++written;
    }
    return written;
}

}
