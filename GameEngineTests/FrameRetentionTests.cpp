#include "FrameRetentionTests.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Rendering/D3D12/D3D12FrameResources.h"
#include "Rendering/FrameBoundCache.h"
#include "TestSupport.h"

using TestSupport::Expect;
using TestSupport::ReadFile;

namespace
{
    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        // 이 파일은 <repo>/GameEngineTests/FrameRetentionTests.cpp에 있다.
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    /// <summary>
    /// 항목 하나만 들어가는 캐시에 A를 넣고, 프레임을 넘긴 뒤 B를 넣어 본다. A가 남았는지가
    /// 보존 창이 지난 프레임을 지키는지에 대한 답이다.
    /// </summary>
    [[nodiscard]] bool SurvivesNextFrame(const std::uint64_t retainedFrames)
    {
        GameEngine::Rendering::FrameBoundCache<std::uint64_t, int> cache(
            1, 64, retainedFrames);
        cache.BeginFrame();
        if (!cache.MakeRoom(16) || cache.Insert(1, 10, 16) == nullptr)
        {
            return false;
        }
        // 프레임 N의 command list가 아직 실행 중인 그 순간이다.
        cache.BeginFrame();
        return !cache.MakeRoom(16);
    }

    /// <summary>선언 하나의 본문이다. 여는 자리부터 그 선언을 닫는 세미콜론까지.</summary>
    [[nodiscard]] std::string DeclarationAt(const std::string& source, const std::size_t start)
    {
        const std::size_t end = source.find(';', start);
        return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
}

bool RunFrameRetentionTests()
{
    namespace D3D12 = GameEngine::Rendering::D3D12;

    // 보존 창이 한 프레임이면 지난 프레임이 쓴 항목은 이미 자유다. CPU만 읽는 캐시에는 그것이
    // 맞고, GPU 리소스를 쥔 캐시에는 그것이 사용 중인 리소스를 해제하는 일이 된다.
    const bool oneFrameReleasesLastFrame = !SurvivesNextFrame(1);

    // in flight 프레임 수에서 나온 창은 그 기간을 지킨다.
    const bool inFlightWindowHoldsIt = SurvivesNextFrame(D3D12::FrameSlotRetention);

    // 창은 in flight 프레임 수보다 좁을 수 없다. 좁으면 아직 실행 중인 프레임이 참조하는 항목이
    // 퇴거될 수 있고, 그것이 이 시험이 막는 전부다.
    const bool windowCoversFramesInFlight = D3D12::FrameSlotRetention > D3D12::FramesInFlight;

    // 🔴 그 창은 유도되어야 한다. 같은 숫자를 두 곳에 적으면 한쪽만 바뀌는 날이 오고, 그날
    // 어긋났다는 것은 화면에 나타나기 전까지 아무데도 적히지 않는다.
    const std::string frameResources =
        ReadFile(RepositoryRoot() / "GameEngine/Rendering/D3D12/D3D12FrameResources.h");
    const std::size_t retentionLine = frameResources.find("FrameSlotRetention =");
    const bool retentionIsDerived = retentionLine != std::string::npos &&
        DeclarationAt(frameResources, retentionLine).find("FramesInFlight") != std::string::npos;

    // GPU 바인딩을 담는 캐시는 전부 그 값을 받아야 한다. 기본값(한 프레임)으로 만들어진 것이
    // 하나라도 있으면 그것이 다음 결함이다.
    int gpuCaches = 0;
    int gpuCachesWithoutTheWindow = 0;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::directory_iterator(RepositoryRoot() / "GameEngine/Rendering/D3D12"))
    {
        if (entry.path().extension() != ".cpp")
        {
            continue;
        }
        const std::string source = ReadFile(entry.path());
        for (std::size_t at = source.find("FrameBoundCache<"); at != std::string::npos;
            at = source.find("FrameBoundCache<", at + 1))
        {
            const std::string declaration = DeclarationAt(source, at);
            // 형식만 적은 자리(선언이 아닌 곳)는 세지 않는다.
            if (declaration.find('{') == std::string::npos)
            {
                continue;
            }
            ++gpuCaches;
            if (declaration.find("FrameSlotRetention") == std::string::npos)
            {
                ++gpuCachesWithoutTheWindow;
            }
        }
    }
    const bool everyGpuCacheUsesTheWindow = gpuCaches > 0 && gpuCachesWithoutTheWindow == 0;

    return Expect(
            oneFrameReleasesLastFrame,
            "a one frame window should release what the previous frame used") &&
        Expect(
            inFlightWindowHoldsIt,
            "a window taken from the frames in flight should hold it") &&
        Expect(
            windowCoversFramesInFlight,
            "the retention window should be wider than the frames in flight") &&
        Expect(
            retentionIsDerived,
            "the retention window should be defined from the frames in flight, not copied") &&
        Expect(
            everyGpuCacheUsesTheWindow,
            "every D3D12 cache holding GPU bindings should take that retention window");
}

static const TestSupport::Registration gFrameRetentionTests{
    "RenderCache", "frame retention tests should pass", RunFrameRetentionTests };
