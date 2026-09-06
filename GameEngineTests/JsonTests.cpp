#include "JsonTests.h"

#include <filesystem>
#include <iostream>
#include <string>

#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Core/Json.h"
#include "Platform/DirectoryContentSource.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{

    /// <summary>UTF-8 바이트 순서 표식이다. 눈에 보이지 않으므로 값으로 적는다.</summary>
    const std::string ByteOrderMark = "\xEF\xBB\xBF";
}

bool RunJsonByteOrderMarkTests()
{
    const std::string document = R"({"pixelsPerUnit": 16.0, "border": [8.0, 8.0, 8.0, 8.0]})";
    bool passed = true;

    // 표식이 붙은 문서와 붙지 않은 문서는 같은 값이다.
    try
    {
        const GameEngine::Core::Json plain = GameEngine::Core::Json::Parse(document);
        const GameEngine::Core::Json marked =
            GameEngine::Core::Json::Parse(ByteOrderMark + document);
        passed &= Expect(
            plain.Dump() == marked.Dump(),
            "a document that begins with a byte order mark parses to the same value");
    }
    catch (const std::exception& exception)
    {
        std::cerr << "  parsing threw: " << exception.what() << "\n";
        passed &= Expect(false, "neither document should fail to parse");
    }

    // 표식은 문서의 시작에서만 지나친다. 값 안에 나타난 그 바이트는 여전히 데이터이고, 값
    // 사이에 나타난 것은 여전히 오류다 — 공백이 아니기 때문이다.
    try
    {
        const GameEngine::Core::Json inside =
            GameEngine::Core::Json::Parse("{\"name\": \"" + ByteOrderMark + "\"}");
        passed &= Expect(
            inside.IsObject() && inside.Find("name") != nullptr &&
                inside.Find("name")->Get<std::string>() == ByteOrderMark,
            "the same bytes inside a string stay part of that string");
    }
    catch (const std::exception&)
    {
        passed &= Expect(false, "a string containing those bytes should still parse");
    }

    bool rejectedInTheMiddle = false;
    try
    {
        static_cast<void>(GameEngine::Core::Json::Parse("{" + ByteOrderMark + "\"a\": 1}"));
    }
    catch (const std::exception&)
    {
        rejectedInTheMiddle = true;
    }
    passed &= Expect(
        rejectedInTheMiddle, "those bytes between values are still an error, not whitespace");

    // BOM이 붙은 사이드카도 실제 에셋 등록 경로로 읽어 본다.
    TestSupport::TemporaryDirectory projectDirectory("json-byte-order-mark");
    const std::filesystem::path root = projectDirectory.GetPath();
    const bool wrote =
        TestSupport::WriteFile(root / "Marked.gameproject", "{}") &&
        TestSupport::WriteFile(root / "Panel.png", "png-data") &&
        TestSupport::WriteFile(root / "Panel.png.meta", ByteOrderMark + document);
    if (!Expect(wrote, "the byte-order-mark sidecar test data should be written"))
    {
        return false;
    }

    const GameEngine::Platform::DirectoryContentSource content(root);
    GameEngine::Assets::AssetDatabase database;
    passed &= Expect(database.Refresh(content), "the project should open");

    const auto* const sprite = database.FindAsset<GameEngine::Assets::Sprite>("Panel.png");
    passed &= Expect(sprite != nullptr, "the sprite should be registered");
    if (sprite)
    {
        // 표식 때문에 사이드카가 버려졌다면 이 둘이 기본값으로 남는다. 테두리가 없으면 9-슬라이스
        // 자체가 일어나지 않으므로, 화면에서는 그림이 늘어난 모습으로만 보인다.
        passed &= Expect(
            sprite->GetPixelsPerUnit() == 16.0f,
            "the sidecar's pixels-per-unit should survive the byte order mark");
        passed &= Expect(
            sprite->HasBorder() && sprite->GetBorder().left == 8.0f,
            "the sidecar's nine-slice border should survive the byte order mark");
    }

    return passed;
}

static const TestSupport::Registration gJsonByteOrderMarkTests{
    "Core", "json byte order mark tests should pass", RunJsonByteOrderMarkTests };
