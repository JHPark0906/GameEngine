#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Diagnostics/Debug.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/PlatformServices.h"
#include "Platform/Win32/Win32Diagnostics.h"

#include "DuplicateAssetIdentityTests.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    constexpr std::string_view SharedGuid = "0a1b2c3d4e5f60718293a4b5c6d7e8f9";

    /// <summary>
    /// 최소 프로젝트 하나에 스프라이트 두 개를 놓는다. <paramref name="secondShareGuid"/>가 참이면
    /// 둘이 같은 GUID를 갖고, 거짓이면 서로 다른 GUID를 갖는다 — 메시지가 실제로 GUID의 <b>충돌</b>에
    /// 반응하는지, 그냥 파일 둘이 있으면 항상 나오는지를 가르는 음성 대조다.
    /// </summary>
    [[nodiscard]] std::filesystem::path WriteProjectWithTwoSprites(
        const std::filesystem::path& root, const bool secondShareGuid)
    {
        const std::string sidecarA =
            R"({"format":"gameengine-meta/1","guid":")" + std::string(SharedGuid) +
            R"(","pixelsPerUnit":16.0})";
        const std::string secondGuid =
            secondShareGuid ? std::string(SharedGuid) : "f9e8d7c6b5a4938271605f4e3d2c1b0a";
        const std::string sidecarB =
            R"({"format":"gameengine-meta/1","guid":")" + secondGuid +
            R"(","pixelsPerUnit":16.0})";

        const std::filesystem::path projectFile = root / "DuplicateIdentityTest.gameproject";
        const bool wrote =
            WriteFile(projectFile,
                R"({"projectName": "DuplicateIdentityTest", "assetRootPath": ".",)"
                R"( "window": { "width": 1280, "height": 720 }, "targetFrameRate": 60,)"
                R"( "initialSceneId": 0, "scenes": [] })") &&
            WriteFile(root / "circle-16.png", "png-data-a") &&
            WriteFile(root / "circle-16.png.meta", sidecarA) &&
            WriteFile(root / "window-32px.png", "png-data-b") &&
            WriteFile(root / "window-32px.png.meta", sidecarB);
        return wrote ? projectFile : std::filesystem::path{};
    }

    /// <summary>이 실행 동안 나온 오류 로그를 모은다.</summary>
    class ErrorCollector
    {
    public:
        ErrorCollector()
            : mListenerId(GameEngine::Diagnostics::Debug::AddLogListener(
                  [this](const GameEngine::Diagnostics::LogEntry& entry)
                  {
                      if (entry.level == GameEngine::Diagnostics::LogLevel::Error)
                      {
                          mMessages.push_back(entry.message);
                      }
                  }))
        {
        }

        ~ErrorCollector() { GameEngine::Diagnostics::Debug::RemoveLogListener(mListenerId); }

        ErrorCollector(const ErrorCollector&) = delete;
        ErrorCollector& operator=(const ErrorCollector&) = delete;

        [[nodiscard]] const std::vector<std::string>& Messages() const { return mMessages; }

    private:
        GameEngine::Diagnostics::Debug::LogListenerId mListenerId;
        std::vector<std::string> mMessages;
    };

    /// <summary>메시지 벡터 중 하나라도 <paramref name="needle"/>을 담고 있는지.</summary>
    [[nodiscard]] bool AnyContains(
        const std::vector<std::string>& messages, const std::string_view needle)
    {
        return std::ranges::any_of(messages, [&](const std::string& message)
        {
            return message.find(needle) != std::string::npos;
        });
    }

    /// <summary>
    /// 중복 GUID 메시지 자체 — GUID, 양쪽 경로, 고칠 방법이 한 문장에 다 있는지.
    /// </summary>
    [[nodiscard]] bool RunDuplicateGuidMessageTests()
    {
        bool passed = true;

        {
            TemporaryDirectory temporaryDirectory("duplicate-guid");
            const std::filesystem::path projectFile =
                WriteProjectWithTwoSprites(temporaryDirectory.GetPath(), true);
            if (!Expect(!projectFile.empty(), "the duplicate-guid fixture should be written"))
            {
                return false;
            }

            const GameEngine::Platform::DirectoryContentSource content(
                temporaryDirectory.GetPath());
            GameEngine::Assets::AssetDatabase database;
            ErrorCollector errors;
            static_cast<void>(database.Refresh(content));

            const std::vector<std::string>& messages = errors.Messages();
            passed = Expect(
                AnyContains(messages, std::string(SharedGuid)),
                "the duplicate should be reported with the guid that collided") && passed;
            passed = Expect(
                AnyContains(messages, "circle-16.png") && AnyContains(messages, "window-32px.png"),
                "and it should name both files, not just the one that lost") && passed;
            passed = Expect(
                AnyContains(messages, "clean build"),
                "and it should say what to do about it") && passed;

            if (!passed)
            {
                for (const std::string& message : messages)
                {
                    std::cout << "  error: " << message << "\n";
                }
            }
        }

        // 되돌림: GUID가 서로 다르면 이 메시지가 안 나와야 한다. 그러지 않으면 파일 둘이 있기만
        // 해도 항상 찍히는 문장이고, 「충돌」을 재는 것이 아니게 된다.
        {
            TemporaryDirectory temporaryDirectory("distinct-guid");
            const std::filesystem::path projectFile =
                WriteProjectWithTwoSprites(temporaryDirectory.GetPath(), false);
            if (!Expect(!projectFile.empty(), "the distinct-guid fixture should be written"))
            {
                return false;
            }

            const GameEngine::Platform::DirectoryContentSource content(
                temporaryDirectory.GetPath());
            GameEngine::Assets::AssetDatabase database;
            ErrorCollector errors;
            static_cast<void>(database.Refresh(content));

            passed = Expect(
                !AnyContains(errors.Messages(), "claim the same identity"),
                "and two assets with distinct guids should not trigger it") && passed;
        }

        return passed;
    }

    /// <summary>
    /// 로그 파일 미러 — 창도 콘솔도 없는 실행에서도 오류가 디스크에 남는지.
    /// </summary>
    [[nodiscard]] bool RunLogFileMirrorTests()
    {
        std::filesystem::path logPath = GameEngine::Platform::PlatformServices::GetExecutablePath();
        logPath.replace_extension(L".log");

        const GameEngine::Diagnostics::Debug::LogListenerId listenerId =
            GameEngine::Platform::Win32::InstallLogFileMirror();
        bool passed = Expect(
            listenerId != 0, "the log file mirror should install beside the test executable");
        if (!passed)
        {
            return false;
        }

        const std::string marker =
            "duplicate asset identity test marker: this line should reach the log file";
        GameEngine::Diagnostics::Debug::LogError(marker.c_str());

        GameEngine::Diagnostics::Debug::RemoveLogListener(listenerId);

        std::ifstream file(logPath);
        const std::string contents(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        passed = Expect(
            contents.find(marker) != std::string::npos,
            "and a logged error should actually reach that file") && passed;
        return passed;
    }
}

bool RunDuplicateAssetIdentityTests()
{
    std::cout << "running duplicate asset identity tests\n";
    bool passed = RunDuplicateGuidMessageTests();
    passed = RunLogFileMirrorTests() && passed;
    return passed;
}

static const TestSupport::Registration gDuplicateAssetIdentityTests{
    "AssetDatabase", "a duplicate asset identity should be diagnosable without a window",
    RunDuplicateAssetIdentityTests };
