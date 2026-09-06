#include "SceneFontRegistrationTests.h"

#include <filesystem>
#include <memory>

#include "Rules/EditorFonts.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/PlatformServices.h"
#include "Rendering/TextRasterizationCache.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 편집기가 자기 폰트를 찾는 자리다. staged된 그 자리를 보는 이유는
    /// <c>EditorFontLoadTests.cpp</c>와 같다 — 저장소의 원본이 아니라 배포에 실제로 실린
    /// 파일을 묻는 것이 이 시험의 요점이다.
    /// </summary>
    [[nodiscard]] std::filesystem::path StagedEditorContent()
    {
        return GameEngine::Platform::PlatformServices::GetExecutableDirectory().parent_path() /
            "GameEditor";
    }

    [[nodiscard]] GameEngine::Platform::TextRasterizationRequest MakeRequest()
    {
        // sceneTextCache에는 역할 개념이 없으므로 등록되지 않은 이름을 요청하여 대체 폰트를 확인한다.
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = "A";
        request.fontFamily = "Whatever the scene happens to ask for";
        request.fontSize = 24.0f;
        return request;
    }
}

bool RunSceneFontRegistrationTests()
{
    namespace Rendering = GameEngine::Rendering;

    const std::filesystem::path content = StagedEditorContent();
    std::error_code error;
    if (!Expect(
            std::filesystem::is_directory(content, error) && !error,
            "the editor's content should be staged beside the tests; build GameEditor"))
    {
        return false;
    }
    const GameEngine::Platform::DirectoryContentSource source(content);

    // 폰트가 없는 캐시는 요청한 이름과 무관하게 모든 텍스트 요청을 거절해야 한다.
    Rendering::TextRasterizationCache bare(GameEngine::Platform::PlatformServices::CreateTextRasterizer());
    bare.BeginFrame();
    const std::shared_ptr<const Rendering::ShapedText> beforeRegistration =
        bare.Resolve(MakeRequest());
    const bool failsWithNoFontsRegistered = beforeRegistration == nullptr;

    // 편집기 UI의 폰트 세 개를 이 캐시에도 등록하면, 등록되지 않은 이름의 요청은
    // 등록된 폰트 중 첫 번째로 그려진다.
    Rendering::TextRasterizationCache registered(
        GameEngine::Platform::PlatformServices::CreateTextRasterizer());
    GameEditor::RegisterSceneFonts(source, registered);
    registered.BeginFrame();
    const std::shared_ptr<const Rendering::ShapedText> afterRegistration =
        registered.Resolve(MakeRequest());
    const bool succeedsAfterRegistration =
        afterRegistration != nullptr && afterRegistration->IsValid() &&
        !afterRegistration->runs.empty();

    // 실제로 잉크가 있는지까지 본다 — run이 있어도 페이지가 비었으면 여전히 안 보이는
    // 글자다.
    bool everyRunHasInk = succeedsAfterRegistration;
    if (succeedsAfterRegistration)
    {
        for (const Rendering::ShapedTextRun& run : afterRegistration->runs)
        {
            if (!run.page)
            {
                everyRunHasInk = false;
                continue;
            }
            std::size_t inkSum = 0;
            for (const std::byte pixel : run.page->alphaPixels)
            {
                inkSum += static_cast<unsigned char>(pixel);
            }
            everyRunHasInk = everyRunHasInk && inkSum > 0;
        }
    }

    return Expect(
            failsWithNoFontsRegistered,
            "a text cache with no fonts registered should reject every request, matching the "
            "bug: this is the state Application's sceneTextCache used to be left in") &&
        Expect(
            succeedsAfterRegistration,
            "registering the editor's fonts into the scene cache should let an unmatched "
            "request resolve") &&
        Expect(
            everyRunHasInk,
            "the resolved text should point at a page that actually has ink, not just an "
            "empty page");
}

static const TestSupport::Registration gSceneFontRegistrationTests{
    "EditorDocument", "scene font registration tests should pass", RunSceneFontRegistrationTests };
