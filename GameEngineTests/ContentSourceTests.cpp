#include "ContentSourceTests.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "TestSupport.h"
#include "Build/ContentPackWriter.h"
#include "Platform/ContentPackFormat.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/IDirectoryWatcher.h"
#include "Platform/PlatformServices.h"
#include "Platform/PackedContentSource.h"

using TestSupport::Expect;

using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
}

/// <summary>
/// 콘텐츠 소스는 경로를 내주는 대신 바이트로 답하며, 그것이 같은 엔진이 오늘은 디렉터리에
/// 흩어진 파일을, 내일은 실행 파일 안의 파일을 읽게 해 준다.
///
/// 루트는 출발점이 아니라 경계다. 장면은 자기가 쓰는 에셋을 이름으로 가리키고 장면은
/// 콘텐츠이므로, 프로젝트 밖을 가리키는 콘텐츠는 열리는 대신 거부되어야 한다.
/// </summary>
bool RunContentSourceTests()
{
    using GameEngine::Platform::DirectoryContentSource;

    TemporaryDirectory temporaryDirectory("content");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    const bool wrote =
        WriteFile(root / "Scenes" / "Main.scene", "scene-bytes") &&
        WriteFile(root / "Textures" / "Albedo.png", "png-bytes") &&
        WriteFile(root / "Empty.bin", "");
    // Something outside the root that a traversal would reach if nothing stopped it.
    const bool wroteOutside = WriteFile(root.parent_path() / "outside-the-root.txt", "secret");

    const DirectoryContentSource source(root);

    std::vector<std::byte> bytes;
    const bool readsFile = source.Read("Scenes/Main.scene", bytes) &&
        std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "scene-bytes";

    // An empty file is a file. Reporting it as a failure would make "missing" and "empty"
    // indistinguishable to a caller that has to tell the person which one happened.
    const bool readsEmptyFile = source.Read("Empty.bin", bytes) && bytes.empty();

    const bool reportsExistence = source.Exists("Textures/Albedo.png") &&
        !source.Exists("Textures/Missing.png") &&
        // A directory is not a file to read.
        !source.Exists("Textures");

    // A failed read leaves nothing behind, so a caller cannot mistake the previous file's bytes
    // for this one's.
    const bool clearsOnFailure = !source.Read("Missing.file", bytes) && bytes.empty();

    const bool refusesEscape =
        !source.Read("../outside-the-root.txt", bytes) &&
        !source.Read("Scenes/../../outside-the-root.txt", bytes) &&
        !source.Exists("../outside-the-root.txt") &&
        source.ResolveFilePath("../outside-the-root.txt").empty();

    // An absolute path ignores the root entirely, so it is refused for the same reason.
    const bool refusesAbsolute = !source.Read(root / "Scenes" / "Main.scene", bytes);

    const std::vector<std::filesystem::path> listed = source.List();
    const bool listsEverything = listed.size() == 3 &&
        std::ranges::find(listed, std::filesystem::path("Empty.bin")) != listed.end() &&
        std::ranges::find(listed, std::filesystem::path("Scenes/Main.scene").lexically_normal()) !=
            listed.end();

    // The database numbers what it walks, so a listing whose order came from the filesystem
    // would renumber sub-assets between runs on a different machine.
    const bool listIsOrdered = std::ranges::is_sorted(listed);

    const DirectoryContentSource missingRoot(root / "NoSuchDirectory");
    const bool reportsMissingRoot = !missingRoot.IsValid() && missingRoot.List().empty() &&
        !missingRoot.Read("anything", bytes);

    std::error_code cleanupError;
    std::filesystem::remove(root.parent_path() / "outside-the-root.txt", cleanupError);

    return Expect(wrote && wroteOutside, "write the content source test data") &&
        Expect(readsFile, "a source should return a file's bytes") &&
        Expect(readsEmptyFile, "an empty file should read as empty rather than as missing") &&
        Expect(reportsExistence, "a source should report which files it holds") &&
        Expect(clearsOnFailure, "a failed read should leave no bytes behind") &&
        Expect(refusesEscape, "a path that climbs out of the root should be refused") &&
        Expect(refusesAbsolute, "an absolute path should be refused") &&
        Expect(listsEverything, "a source should list every file it holds") &&
        Expect(listIsOrdered, "a listing should be ordered so it is the same on every run") &&
        Expect(reportsMissingRoot, "a source over a missing directory should hold nothing");
}

/// <summary>
/// 자기를 실행하는 파일에 packed된 프로젝트는 자기가 만들어진 디렉터리와 정확히 같게 읽혀야
/// 한다. 패킹을 두 번째 엔진이 아니라 배포의 선택으로 만들어 주는 것이 오직 그것이기
/// 때문이다.
///
/// 팩은 완성된 실행 파일 뒤에 덧붙으므로, 그 앞의 바이트는 건드려지지 않은 채 살아남아야
/// 하고, 팩 없는 파일은 오류가 아니라 "팩 없음"으로 읽혀야 한다 — 개발 빌드가 옆의 파일들로
/// 물러설 수 있는 이유가 그것이다.
/// </summary>
bool RunContentPackTests()
{
    using GameEngine::Build::AppendContentPack;
    using GameEngine::Platform::PackedContentSource;

    TemporaryDirectory temporaryDirectory("pack");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    // Bytes standing in for a linked executable, including a zero and a byte above 0x7f so a
    // reader that treated the file as text would corrupt them.
    const std::string executableBytes("MZ\x00\x90\xff binary image", 20);
    const std::filesystem::path executablePath = root / "Packed.bin";

    const bool wrote =
        WriteFile(root / "content" / "Root.gameproject", "{\"projectName\":\"Packed\"}") &&
        WriteFile(root / "content" / "Scenes" / "Main.scene", "scene bytes") &&
        WriteFile(root / "content" / "Nested" / "Deep" / "Model.bin", std::string(5000, 'm')) &&
        WriteFile(root / "content" / "Empty.bin", "") &&
        WriteFile(executablePath, executableBytes);

    // A file with nothing appended is not a pack, and saying so is how the engine knows to read
    // the directory instead.
    const PackedContentSource unpacked(executablePath);
    const bool unpackedIsNotAPack = !unpacked.IsValid() && unpacked.List().empty();

    const std::vector<std::filesystem::path> packedPaths{
        "Root.gameproject",
        "Scenes/Main.scene",
        "Nested/Deep/Model.bin",
        "Empty.bin",
    };
    const bool appended =
        wrote && AppendContentPack(executablePath, root / "content", packedPaths);

    const PackedContentSource packed(executablePath);
    std::vector<std::byte> bytes;
    const bool readsBack = packed.IsValid() &&
        packed.Read("Scenes/Main.scene", bytes) &&
        std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "scene bytes";

    // A file large enough to span more than one buffer, in a nested directory, to catch an index
    // that stored a size or an offset wrongly.
    const bool readsLargeNestedFile = packed.Read("Nested/Deep/Model.bin", bytes) &&
        bytes.size() == 5000 &&
        std::ranges::all_of(bytes, [](const std::byte value) { return value == std::byte{'m'}; });

    const bool readsEmptyFile = packed.Read("Empty.bin", bytes) && bytes.empty();

    const bool reportsContents = packed.Exists("Root.gameproject") &&
        !packed.Exists("Missing.file") && packed.List().size() == packedPaths.size();

    // The engine asks for paths the way an asset database spells them, which is not always the
    // way a build wrote them.
    const bool matchesRegardlessOfSpelling = packed.Exists("scenes/main.scene") &&
        packed.Exists("Nested\\Deep\\Model.bin");

    // Everything in front of the pack is the executable, and appending must not have moved a
    // byte of it or the file would no longer run.
    std::vector<std::byte> imageBytes;
    const GameEngine::Platform::DirectoryContentSource rootSource(root);
    const bool imageIntact = rootSource.Read("Packed.bin", imageBytes) &&
        imageBytes.size() > executableBytes.size() &&
        std::memcmp(imageBytes.data(), executableBytes.data(), executableBytes.size()) == 0;

    // A footer whose index offset falls inside the footer itself would underflow the index size
    // into an enormous span. Corrupt numbers must be a rejected file, never undefined
    // behaviour.
    namespace Format = GameEngine::Platform::ContentPackFormat;
    const std::filesystem::path malformedPath = root / "Malformed.bin";
    std::string malformed = executableBytes;
    malformed.append(10, 'x');
    const std::uint64_t claimedPackSize = 10 + Format::FooterSize;
    const auto appendNumber = [&malformed](const auto& numberBytes)
    {
        for (const std::byte value : numberBytes)
        {
            malformed.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
        }
    };
    appendNumber(Format::WriteUInt32(1));
    appendNumber(Format::WriteUInt64(claimedPackSize - 1));
    appendNumber(Format::WriteUInt64(claimedPackSize));
    malformed.append(Format::Magic);
    const bool malformedFooterRefused =
        WriteFile(malformedPath, malformed) &&
        !PackedContentSource(malformedPath).IsValid();

    // A truncated pack is a corrupt one. Reading half an index and drawing whatever it happened
    // to contain would be worse than refusing.
    const std::filesystem::path truncatedPath = root / "Truncated.bin";
    const bool truncatedRefused =
        WriteFile(
            truncatedPath,
            std::string(
                reinterpret_cast<const char*>(imageBytes.data()),
                imageBytes.size() - 4)) &&
        !PackedContentSource(truncatedPath).IsValid();

    return Expect(wrote, "write the content pack test data") &&
        Expect(unpackedIsNotAPack, "a file with nothing appended should report no pack") &&
        Expect(appended, "appending a pack should succeed") &&
        Expect(readsBack, "a packed file should read back byte for byte") &&
        Expect(readsLargeNestedFile, "a large nested file should read back whole") &&
        Expect(readsEmptyFile, "an empty packed file should read as empty rather than missing") &&
        Expect(reportsContents, "a pack should report what it holds") &&
        Expect(matchesRegardlessOfSpelling, "a lookup should not depend on how a path is spelled") &&
        Expect(imageIntact, "appending a pack must leave the executable's own bytes untouched") &&
        Expect(truncatedRefused, "a truncated pack should be refused rather than half-read") &&
        Expect(malformedFooterRefused, "a footer pointing into itself should be refused");
}

bool RunDirectoryWatcherTests()
{
    TemporaryDirectory temporaryDirectory("watch");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    if (!WriteFile(root / "seed.txt", "seed"))
    {
        return Expect(false, "could not create the directory watcher test data");
    }

    const std::unique_ptr<GameEngine::Platform::IDirectoryWatcher> watcher =
        GameEngine::Platform::PlatformServices::CreateDirectoryWatcher(root);
    if (!Expect(watcher && watcher->IsValid(), "a watcher on an existing directory should be valid"))
    {
        return false;
    }

    // 변동이 도착할 때까지 기다린다. 알림은 비동기라 즉시 오지 않지만, 2초 안에는 온다.
    const auto waitForChange = [&watcher]()
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            if (watcher->PollChanges())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    // 남은 알림이 다 흘러나갈 때까지 비운다. 쓰기 하나가 알림 여럿이 될 수 있다.
    const auto drain = [&watcher]()
    {
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (!watcher->PollChanges())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };

    // 감시가 선 뒤 아무것도 하지 않았으니 조용해야 한다.
    const bool quietAtStart = !watcher->PollChanges();

    // 하위 디렉터리에 새 파일: 재귀 감시가 이것을 본다.
    const bool wroteNested = WriteFile(root / "Textures" / "New.png", "png-bytes");
    const bool sawNestedWrite = wroteNested && waitForChange();
    const bool quietAfterWrite = drain();

    // 지우기도 변동이다.
    std::error_code removeError;
    const bool removed = std::filesystem::remove(root / "Textures" / "New.png", removeError);
    const bool sawRemoval = removed && waitForChange();
    const bool quietAfterRemoval = drain();

    // 없는 디렉터리는 열 수 없고, 그 감시는 아무것도 알리지 않는다.
    const std::unique_ptr<GameEngine::Platform::IDirectoryWatcher> missing =
        GameEngine::Platform::PlatformServices::CreateDirectoryWatcher(root / "does-not-exist");
    const bool missingIsInvalid = missing && !missing->IsValid() && !missing->PollChanges();

    return Expect(quietAtStart, "a fresh watcher should report no changes") &&
        Expect(sawNestedWrite, "a file written in a subdirectory should be reported") &&
        Expect(quietAfterWrite, "a watcher should fall quiet once the notifications are drained") &&
        Expect(sawRemoval, "a deleted file should be reported") &&
        Expect(quietAfterRemoval, "a watcher should fall quiet after the removal is drained") &&
        Expect(missingIsInvalid, "a watcher on a missing directory should be invalid and silent");
}

static const TestSupport::Registration gContentSourceTests{
    "ContentSource", "content source tests should pass", RunContentSourceTests };

static const TestSupport::Registration gContentPackTests{
    "ContentSource", "content pack tests should pass", RunContentPackTests };

static const TestSupport::Registration gDirectoryWatcherTests{
    "ContentSource", "directory watcher tests should pass", RunDirectoryWatcherTests };
