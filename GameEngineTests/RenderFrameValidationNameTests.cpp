#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "../GameEngine/Rendering/RenderFrame.h"

#include "RenderFrameValidationNameTests.h"
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    using GameEngine::Rendering::RenderFrameValidationError;
    using GameEngine::Rendering::GetRenderFrameValidationErrorName;

    /// <summary>표의 마지막 값이다. 값이 늘면 이 시험이 새 값까지 훑도록 여기도 늘려야 한다.</summary>
    constexpr RenderFrameValidationError LastError =
        RenderFrameValidationError::InvalidDrawParameters;
}

bool RunRenderFrameValidationNameTests()
{
    std::cout << "running render frame validation name tests\n";

    bool passed = true;
    std::vector<std::string_view> seen;
    const auto last = static_cast<unsigned int>(LastError);
    for (unsigned int value = 0; value <= last; ++value)
    {
        const auto error = static_cast<RenderFrameValidationError>(value);
        const std::string_view name = GetRenderFrameValidationErrorName(error);

        passed = Expect(!name.empty(), "every validation error should have a name") && passed;
        // "Unknown"은 switch가 값을 놓쳤을 때만 나오는 대답이다. 컴파일이 막아 주지만, 표를
        // 훑는 이 시험이 그 사실을 말로도 남긴다.
        passed = Expect(
            name != "Unknown",
            "and no error should fall through to the unnamed answer") && passed;

        for (const std::string_view earlier : seen)
        {
            if (earlier == name)
            {
                std::cout << "  " << name << " is used twice, at value " << value << "\n";
                passed = Expect(false, "and two errors should not share one name") && passed;
            }
        }
        seen.push_back(name);
    }

    // 앞의 고리는 표 끝까지만 간다. 그 너머에 값이 생겼다면 이 시험은 그것을 보지 못하므로,
    // 마지막 값 다음이 정말 표 밖인지 여기서 확인한다.
    const auto beyond = static_cast<RenderFrameValidationError>(last + 1u);
    passed = Expect(
        std::string_view{ GetRenderFrameValidationErrorName(beyond) } == "Unknown",
        "and the value past the end of the table should be the only unnamed one") && passed;

    std::cout << "  " << seen.size() << " named validation errors\n";
    return passed;
}

static const TestSupport::Registration gRenderFrameValidationNameTests{
    "RenderFrame", "render frame validation errors should each have their own name",
    RunRenderFrameValidationNameTests };
