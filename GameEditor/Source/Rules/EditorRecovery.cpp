#include "Rules/EditorRecovery.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <stdexcept>
#include <format>
#include <string_view>
#include <system_error>

#include "Diagnostics/Debug.h"
#include "Core/TextFile.h"

namespace GameEditor
{
namespace
{
    constexpr std::wstring_view SceneMarker = L".scene";
    constexpr std::wstring_view Suffix = L".recovered.json";

    [[nodiscard]] std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path)
    {
        if (path.empty()) return {};
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(path, error);
        if (error) return {};
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
        if (error) return {};
        std::wstring key = canonical.generic_wstring();
        std::ranges::transform(key, key.begin(), [](const wchar_t c)
        {
            return static_cast<wchar_t>(std::towlower(c));
        });
        return std::filesystem::path(key);
    }

    [[nodiscard]] std::string ProjectDirectoryKey(const std::filesystem::path& path)
    {
        const std::filesystem::path normalized = NormalizeProjectPath(path);
        if (normalized.empty()) return {};
        std::uint64_t hash = 14695981039346656037ull;
        for (const char8_t c : normalized.generic_u8string())
        {
            hash = (hash ^ static_cast<std::uint8_t>(c)) * 1099511628211ull;
        }
        return std::format("{:016x}", hash);
    }

    [[nodiscard]] std::filesystem::path ReadRecoveryOwner(const std::filesystem::path& directory)
    {
        const std::optional<std::string> text =
            GameEngine::Core::ReadTextFile(directory / "project.path");
        return text && !text->empty()
            ? std::filesystem::path(std::u8string(text->begin(), text->end()))
            : std::filesystem::path{};
    }
}

std::filesystem::path GetRecoveryDirectory()
{
    std::error_code error;
    const std::filesystem::path temporaryDirectory = std::filesystem::temp_directory_path(error);
    if (error)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "No temporary directory is available for recovery snapshots. error=", error.message());
        return {};
    }
    return temporaryDirectory / "GameEditor";
}

std::filesystem::path MakeRecoveryPath(
    const std::filesystem::path& projectFilePath, const unsigned int sceneId,
    const std::filesystem::path& recoveryDirectory)
{
    const std::filesystem::path& directory = recoveryDirectory;
    if (directory.empty())
    {
        return {};
    }
    const std::string key = ProjectDirectoryKey(projectFilePath);
    if (key.empty()) return {};
    // 동일한 이름의 체크아웃도 서로 다른 작업물이다. 파일명은 유지하고 전체 프로젝트
    // 경로로 디렉터리를 구분한다. 해시 충돌은 쓸 때 project.path와 대조해 거절한다.
    return directory / key /
        (projectFilePath.stem().wstring() + std::wstring(SceneMarker) + std::to_wstring(sceneId) +
            std::wstring(Suffix));
}

std::filesystem::path WriteRecoveryFile(
    const std::filesystem::path& projectFilePath, const unsigned int sceneId,
    const std::string_view sceneText, const std::filesystem::path& recoveryDirectory)
{
    const std::filesystem::path fullPath =
        MakeRecoveryPath(projectFilePath, sceneId, recoveryDirectory);
    if (fullPath.empty() || sceneText.empty()) return {};

    const std::filesystem::path directory = fullPath.parent_path();
    std::error_code error;
    const bool created = std::filesystem::create_directories(directory, error);
    if (error) return {};

    const std::filesystem::path owner = ReadRecoveryOwner(directory);
    if (!owner.empty())
    {
        if (NormalizeProjectPath(owner) != NormalizeProjectPath(projectFilePath))
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Recovery directory belongs to another project; nothing was overwritten. path=",
                directory.string());
            return {};
        }
    }
    else
    {
        if (!created && !std::filesystem::is_empty(directory, error))
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Recovery directory has no readable owner; nothing was overwritten. path=",
                directory.string());
            return {};
        }
        if (error) return {};
        const std::filesystem::path requested = std::filesystem::absolute(projectFilePath, error);
        if (error) return {};
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(requested, error);
        if (error) return {};
        const std::u8string utf8 = absolute.generic_u8string();
        if (!GameEngine::Core::WriteTextFileAtomically(
                directory / "project.path", std::string(utf8.begin(), utf8.end())))
        {
            return {};
        }
    }
    if (!GameEngine::Core::WriteTextFileAtomically(fullPath, sceneText))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Could not write a recovery snapshot. path=", fullPath.string());
        return {};
    }
    return fullPath;
}

bool RecoveryFileBelongsToProject(
    const std::filesystem::path& filePath, const std::filesystem::path& projectFilePath,
    const unsigned int sceneId)
{
    const std::filesystem::path owner = ReadRecoveryOwner(filePath.parent_path());
    std::string stem;
    unsigned int recordedSceneId = 0;
    return !owner.empty() && NormalizeProjectPath(owner) == NormalizeProjectPath(projectFilePath) &&
        ParseRecoveryFileName(filePath, stem, recordedSceneId) && recordedSceneId == sceneId;
}

bool ParseRecoveryFileName(
    const std::filesystem::path& fileName, std::string& projectStem, unsigned int& sceneId)
{
    const std::wstring name = fileName.filename().wstring();
    if (name.size() <= Suffix.size() || !name.ends_with(Suffix))
    {
        return false;
    }
    const std::wstring withoutSuffix = name.substr(0, name.size() - Suffix.size());

    // 프로젝트 이름에도 ".scene"이 들어갈 수 있으므로 마지막 것에서 가른다.
    const std::size_t markerAt = withoutSuffix.rfind(SceneMarker);
    if (markerAt == std::wstring::npos || markerAt == 0)
    {
        return false;
    }
    const std::wstring digits = withoutSuffix.substr(markerAt + SceneMarker.size());
    if (digits.empty() ||
        !std::all_of(digits.begin(), digits.end(), [](const wchar_t c) { return c >= L'0' && c <= L'9'; }))
    {
        return false;
    }

    unsigned long long parsed = 0;
    for (const wchar_t c : digits)
    {
        parsed = parsed * 10 + static_cast<unsigned long long>(c - L'0');
        if (parsed > 0xFFFFFFFFull)
        {
            return false;
        }
    }

    projectStem = std::filesystem::path(withoutSuffix.substr(0, markerAt)).string();
    sceneId = static_cast<unsigned int>(parsed);
    return true;
}

std::vector<RecoverySnapshot> CollectRecoverySnapshots(
    const std::filesystem::path& recoveryDirectory)
{
    const std::filesystem::path& directory = recoveryDirectory;
    std::vector<RecoverySnapshot> snapshots;
    if (directory.empty())
    {
        return snapshots;
    }

    std::error_code error;
    // 디렉터리가 없는 것은 흔한 상태다 — 사고가 한 번도 없었다는 뜻이므로 오류로 적지 않는다.
    const auto append = [&snapshots](const std::filesystem::directory_entry& entry,
                            const std::filesystem::path& owner)
    {
        std::error_code fileError;
        if (!entry.is_regular_file(fileError) || fileError) return;
        RecoverySnapshot snapshot;
        if (!ParseRecoveryFileName(entry.path(), snapshot.projectStem, snapshot.sceneId))
        {
            return;
        }
        snapshot.filePath = entry.path();
        snapshot.projectFilePath = owner;
        std::error_code timeError;
        snapshot.writeTime = std::filesystem::last_write_time(entry.path(), timeError);
        snapshots.push_back(std::move(snapshot));
    };
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        const std::filesystem::directory_entry entry = *iterator;
        std::error_code entryError;
        if (entry.is_symlink(entryError)) continue;
        if (!entry.is_directory(entryError))
        {
            append(entry, {});
            continue;
        }
        const std::filesystem::path owner = ReadRecoveryOwner(entry.path());
        if (owner.empty() || entry.path().filename() != ProjectDirectoryKey(owner)) continue;
        for (std::filesystem::directory_iterator files(entry.path(), entryError), filesEnd;
             !entryError && files != filesEnd; files.increment(entryError))
        {
            append(*files, owner);
        }
    }

    std::sort(
        snapshots.begin(), snapshots.end(),
        [](const RecoverySnapshot& left, const RecoverySnapshot& right)
        { return left.writeTime > right.writeTime; });
    return snapshots;
}

std::vector<RecoverySnapshot> FindSnapshotsForProject(
    const std::vector<RecoverySnapshot>& snapshots, const std::filesystem::path& projectFilePath)
{
    const std::filesystem::path project = NormalizeProjectPath(projectFilePath);
    std::vector<RecoverySnapshot> found;
    for (const RecoverySnapshot& snapshot : snapshots)
    {
        if (!snapshot.projectFilePath.empty() &&
            NormalizeProjectPath(snapshot.projectFilePath) == project)
        {
            found.push_back(snapshot);
        }
    }
    return found;
}

std::vector<RecoverySnapshot> FindOrphanSnapshots(
    const std::vector<RecoverySnapshot>& snapshots,
    const std::function<bool(const std::string&)>& projectExists)
{
    std::vector<RecoverySnapshot> orphans;
    if (!projectExists)
    {
        return orphans;
    }
    for (const RecoverySnapshot& snapshot : snapshots)
    {
        std::error_code error;
        const bool exists = snapshot.projectFilePath.empty()
            ? projectExists(snapshot.projectStem)
            : std::filesystem::exists(snapshot.projectFilePath, error);
        if (!exists && !error)
        {
            orphans.push_back(snapshot);
        }
    }
    return orphans;
}

RecoveryChoice ChoiceFromDialogAnswer(
    const std::optional<std::size_t> chosen, const std::size_t restoreIndex,
    const std::size_t discardIndex)
{
    if (!chosen)
    {
        return RecoveryChoice::Keep;
    }
    if (*chosen == restoreIndex)
    {
        return RecoveryChoice::Restore;
    }
    if (*chosen == discardIndex)
    {
        return RecoveryChoice::Discard;
    }
    // 모르는 첨자다. 무엇을 고른 것인지 모를 때 파일을 지우는 것이 가장 나쁜 답이다.
    return RecoveryChoice::Keep;
}

std::string FormatSnapshotTime(const std::filesystem::file_time_type writeTime)
{
    if (writeTime == std::filesystem::file_time_type{})
    {
        return {};
    }
    const std::chrono::system_clock::time_point wallClock =
        std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
    // 초는 적지 않는다. 어느 사본이 최근 것인지를 가리는 데 필요한 것은 분까지다.
    const std::chrono::sys_time<std::chrono::minutes> minutes =
        std::chrono::floor<std::chrono::minutes>(wallClock);

    // 사람이 시계를 보고 "그때 닫았지"라고 알아보는 것이 이 글의 전부이므로 지역 시각으로
    // 적는다. UTC로 적으면 그 단서가 몇 시간씩 어긋난다. 시간대 표를 얻지 못하는 환경이
    // 있으므로 그때는 UTC로 적는다 — 어긋난 시각이 시각이 없는 것보다는 낫다.
    try
    {
        return std::format(
            "{:%Y-%m-%d %H:%M}", std::chrono::current_zone()->to_local(minutes));
    }
    catch (const std::runtime_error&)
    {
        return std::format("{:%Y-%m-%d %H:%M}", minutes);
    }
}

bool ApplyRecoveryChoice(
    const RecoverySnapshot& snapshot, const RecoveryChoice choice,
    const std::function<bool(const RecoverySnapshot&)>& restore)
{
    switch (choice)
    {
    case RecoveryChoice::Keep:
        return false;
    case RecoveryChoice::Discard:
        static_cast<void>(DeleteRecoverySnapshot(snapshot.filePath));
        return false;
    case RecoveryChoice::Restore:
        // 되살리기가 실패하면 사본을 그대로 둔다. 실패한 뒤 파일까지 없으면 사람은 두 번
        // 잃는다 — 되살리지도 못하고, 다시 시도할 것도 남지 않는다.
        if (restore && restore(snapshot))
        {
            return true;
        }
        GameEngine::Diagnostics::Debug::LogError(
            "A recovery snapshot could not be restored, so it was left in place. path=",
            snapshot.filePath.string());
        return false;
    }
    return false;
}

bool DeleteRecoverySnapshot(const std::filesystem::path& filePath)
{
    std::error_code error;
    std::filesystem::remove(filePath, error);
    if (error)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Could not delete a recovery snapshot. path=", filePath.string(),
            ", error=", error.message());
        return false;
    }
    GameEngine::Diagnostics::Debug::Log(
        "Deleted a recovery snapshot. path=", filePath.filename().string());
    return true;
}

}
