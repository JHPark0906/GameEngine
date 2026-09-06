#include "ProjectRuntimeTextMeasureTests.h"

#include <filesystem>
#include <memory>

#include "Rules/EditorFonts.h"
#include "Platform/DirectoryContentSource.h"
#include "Platform/ITextMeasure.h"
#include "Platform/PlatformServices.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/TextRasterizationCache.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 편집기가 자기 폰트를 찾는 자리다. <c>EditorFontLoadTests.cpp</c>와 같은 이유로 staged된
    /// 그 자리를 본다 — 시험 실행 파일 옆이 아니라, 편집기 실행 파일이 서는 자리다.
    /// </summary>
    [[nodiscard]] std::filesystem::path StagedEditorContent()
    {
        return GameEngine::Platform::PlatformServices::GetExecutableDirectory().parent_path() /
            "GameEditor";
    }

    [[nodiscard]] GameEngine::Platform::TextRasterizationRequest MakeRequest()
    {
        GameEngine::Platform::TextRasterizationRequest request;
        request.text = "Hello";
        request.fontSize = 24.0f;
        return request;
    }
}

bool RunProjectRuntimeTextMeasureTests()
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

    // 폰트가 하나도 등록되지 않은 캐시에서 텍스트 측정 결과를 확인한다.
    auto bareCache = std::make_shared<Rendering::TextRasterizationCache>(
        GameEngine::Platform::PlatformServices::CreateTextRasterizer());
    Rendering::CachedTextMeasure bareMeasure(bareCache);
    bareMeasure.BeginFrame();
    const GameEngine::Platform::TextExtent bareExtent = bareMeasure.Measure(MakeRequest());
    const bool measuresAsZeroWithNoFontsRegistered =
        bareExtent.width == 0.0f && bareExtent.height == 0.0f;

    // EditorContext와 같은 파일들을 캐시에 등록한 뒤 텍스트를 측정한다.
    auto registeredCache = std::make_shared<Rendering::TextRasterizationCache>(
        GameEngine::Platform::PlatformServices::CreateTextRasterizer());
    GameEditor::RegisterSceneFonts(source, *registeredCache);
    Rendering::CachedTextMeasure registeredMeasure(registeredCache);
    registeredMeasure.BeginFrame();
    const GameEngine::Platform::TextExtent registeredExtent =
        registeredMeasure.Measure(MakeRequest());
    const bool measuresRealSizeAfterRegistration =
        registeredExtent.width > 0.0f && registeredExtent.height > 0.0f;

    return Expect(
            measuresAsZeroWithNoFontsRegistered,
            "a text measure backed by a cache with no fonts should measure everything as "
            "zero-sized, matching the bug this project's runtime text measure used to have") &&
        Expect(
            measuresRealSizeAfterRegistration,
            "registering the editor's fonts should let the same measure return a real size for "
            "the same string — this is what keeps a layout from collapsing text it can "
            "otherwise draw fine");
}

static const TestSupport::Registration gProjectRuntimeTextMeasureTests{
    "EditorDocument", "project runtime text measure tests should pass",
    RunProjectRuntimeTextMeasureTests };
