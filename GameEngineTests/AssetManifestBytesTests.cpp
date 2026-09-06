#include "AssetManifestBytesTests.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "Assets/AssetDatabase.h"
#include "Platform/TextFile.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::TemporaryDirectory;
using TestSupport::WriteFile;

namespace
{
    /// <summary>
    /// 이 시험이 재는 표다. 파일 내용과 guid를 손으로 적어 두므로 내용 해시와 크기까지
    /// 결정적이고, 그래서 기대 바이트가 기계와 무관하다.
    /// </summary>
    [[nodiscard]] bool WriteFixedProject(const std::filesystem::path& root)
    {
        const auto sidecar = [](const char* const guid)
        {
            return std::string(R"({"format":"gameengine-meta/1","guid":")") + guid + R"("})";
        };
        return WriteFile(root / "Fixed.gameproject", "{}") &&
            WriteFile(root / "Fixed.gameproject.meta",
                sidecar("0a1b2c3d4e5f60718293a4b5c6d7e8f9")) &&
            WriteFile(root / "Scenes/Main.scene", "{}") &&
            WriteFile(root / "Scenes/Main.scene.meta",
                sidecar("1b2c3d4e5f60718293a4b5c6d7e8f90a")) &&
            WriteFile(root / "Textures/Albedo.png", "eight---") &&
            WriteFile(root / "Textures/Albedo.png.meta",
                sidecar("2c3d4e5f60718293a4b5c6d7e8f90a1b"));
    }
}

bool RunAssetManifestBytesTests()
{
    TemporaryDirectory temporaryDirectory("asset-manifest-bytes");
    const std::filesystem::path root = temporaryDirectory.GetPath();
    if (!Expect(WriteFixedProject(root), "the fixed project should be written"))
    {
        return false;
    }

    const GameEngine::Platform::DirectoryContentSource content(root);
    GameEngine::Assets::AssetDatabase database;
    if (!Expect(database.Refresh(content), "the fixed project should scan"))
    {
        return false;
    }

    const std::filesystem::path manifestPath = root / "Assets" / "AssetDatabase.json";
    if (!Expect(database.SaveManifest(manifestPath), "the manifest should be saved"))
    {
        return false;
    }
    const std::optional<std::string> written = GameEngine::Platform::ReadTextFile(manifestPath);
    if (!Expect(written.has_value(), "the manifest should be readable"))
    {
        return false;
    }

    constexpr std::string_view Expected = R"EXPECTED({
  "formatVersion": 6,
  "assets": [
    {
      "guid": "0a1b2c3d4e5f60718293a4b5c6d7e8f9",
      "type": "ProjectSettings",
      "path": "Fixed.gameproject",
      "contentHash": "08f44b07b5901a25",
      "fileSize": "2",
      "subAssets": [
        {"type": "ProjectSettings", "name": "Fixed"}
      ]
    },
    {
      "guid": "1b2c3d4e5f60718293a4b5c6d7e8f90a",
      "type": "Scene",
      "path": "Scenes/Main.scene",
      "contentHash": "08f44b07b5901a25",
      "fileSize": "2",
      "subAssets": [
        {"type": "Scene", "name": "Main"}
      ]
    },
    {
      "guid": "2c3d4e5f60718293a4b5c6d7e8f90a1b",
      "type": "Sprite",
      "path": "Textures/Albedo.png",
      "contentHash": "7880bd5baa4bfae9",
      "fileSize": "8",
      "subAssets": [
        {"type": "Sprite", "name": "Albedo"}
      ],
      "border": [0, 0, 0, 0],
      "pixelsPerUnit": 100,
      "sheet": {"columns": 1, "frameCount": 0, "frameRate": 12, "rows": 1}
    }
  ]
}
)EXPECTED";
    const bool bytesAreUnchanged = *written == Expected;
    if (!bytesAreUnchanged)
    {
        std::cerr << "the manifest bytes changed. what was written:\n" << *written << '\n';
    }

    return Expect(
        bytesAreUnchanged,
        "the manifest a fixed table produces should be byte for byte what it was");
}

static const TestSupport::Registration gAssetManifestBytesTests{
    "AssetDatabase", "asset manifest bytes tests should pass", RunAssetManifestBytesTests };
