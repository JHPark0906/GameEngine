#include "TextFileTests.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "Platform/TextFile.h"
#include "TestSupport.h"

using TestSupport::Expect;

using TestSupport::TemporaryDirectory;

namespace
{
}

bool RunTextFileTests()
{
    using GameEngine::Platform::FileWriteError;
    using GameEngine::Platform::ReadTextFile;
    using GameEngine::Platform::WriteTextFile;
    using GameEngine::Platform::WriteTextFileAtomically;

    TemporaryDirectory temporaryDirectory("text-file");
    const std::filesystem::path root = temporaryDirectory.GetPath();

    // 없는 파일은 빈 파일과 다른 사실이다. 값으로 갈라 놓지 않으면 부르는 쪽이 둘을 섞는다.
    const bool missingIsNotEmpty = !ReadTextFile(root / "absent.txt").has_value();

    // 🔴 줄 끝은 바뀌지 않는다. 텍스트 모드로 읽으면 Windows에서 \r\n이 \n이 되어, 해시를 재는
    // 자리에서 파일과 읽은 것이 달라진다.
    const std::string mixed = "first\r\nsecond\nthird\r\n";
    const bool wroteMixed = static_cast<bool>(WriteTextFile(root / "lines.txt", mixed));
    const bool keepsLineEndings = wroteMixed && ReadTextFile(root / "lines.txt") == mixed;

    // 빈 파일은 쓸 수 있고, 읽으면 빈 문자열이다.
    const bool wroteEmpty = static_cast<bool>(WriteTextFile(root / "empty.txt", ""));
    const bool emptyRoundTrips = wroteEmpty && ReadTextFile(root / "empty.txt") == std::string{};

    // 덮어쓰기는 남기지 않는다. 짧은 내용으로 덮었을 때 앞 내용의 꼬리가 남으면 안 된다.
    const bool overwritten = static_cast<bool>(WriteTextFile(root / "lines.txt", "short")) &&
        ReadTextFile(root / "lines.txt") == "short";

    // 원자적 쓰기도 같은 왕복을 한다.
    const std::filesystem::path kept = root / "kept.txt";
    const bool wroteAtomically = static_cast<bool>(WriteTextFileAtomically(kept, "first")) &&
        ReadTextFile(kept) == "first" &&
        static_cast<bool>(WriteTextFileAtomically(kept, "second")) &&
        ReadTextFile(kept) == "second";
    // 임시 파일은 남지 않는다. 남으면 다음 쓰기가 그것을 보고 멈추거나 덮게 된다.
    std::error_code error;
    std::filesystem::path temporary = kept;
    temporary += ".tmp";
    const bool leavesNothingBehind = !std::filesystem::exists(temporary, error);

    // 🔴 실패했을 때 목적지가 그대로다. 쓸 수 없는 자리를 만드는 가장 확실한 방법은 목적지를
    // 디렉터리로 두는 것이다 — 임시 파일에는 쓰이지만 그 자리로 옮겨 갈 수 없다.
    const std::filesystem::path blocked = root / "blocked.txt";
    const bool madeBlocker = std::filesystem::create_directory(blocked, error) && !error;
    const GameEngine::Platform::FileWriteResult refused =
        WriteTextFileAtomically(blocked, "would overwrite a directory");
    const bool failsWithoutDamage = madeBlocker && !refused &&
        refused.error == FileWriteError::ReplaceFailed &&
        std::filesystem::is_directory(blocked, error) &&
        // 그리고 임시 파일을 치우고 간다. 두고 가면 그 자리가 다음 쓰기의 걸림돌이 된다.
        !std::filesystem::exists(std::filesystem::path(blocked).concat(".tmp"), error);
    // 사람에게 무엇이 남아 있는지 말한다. 「쓰지 못했다」와 「썼는데 못 옮겼다」는 다른 사실이다.
    const bool saysWhatSurvived =
        refused.Describe().find("still holds") != std::string_view::npos;

    return Expect(missingIsNotEmpty, "a file that is not there should not read as an empty one") &&
        Expect(keepsLineEndings, "reading and writing should leave line endings exactly as they are") &&
        Expect(emptyRoundTrips, "an empty file should be writable and read back as empty") &&
        Expect(overwritten, "writing over a file should leave none of what was there") &&
        Expect(wroteAtomically, "an atomic write should land, twice over") &&
        Expect(leavesNothingBehind, "an atomic write should leave no temporary file behind") &&
        Expect(failsWithoutDamage, "a failed atomic write should leave the destination untouched") &&
        Expect(saysWhatSurvived, "the failure should say what the file still holds");
}

static const TestSupport::Registration gTextFileTests{
    "ContentSource", "text file tests should pass", RunTextFileTests };
