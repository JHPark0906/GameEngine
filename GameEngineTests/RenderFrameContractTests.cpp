#include "RenderFrameTests.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "TestSupport.h"

using TestSupport::Expect;

namespace
{
    /// <summary>
    /// 이 시험이 지키는 계약은 한 문장이다: <c>RenderFrame</c>이 나르는 어떤 타입도 자기가
    /// 소유하지 않은 것을 생 포인터로 가리키지 않는다.
    ///
    /// 프레임이 게임 스레드에서 만들어져 렌더 스레드로 건너가면, 그 뒤 게임 스레드는 장면을
    /// 계속 바꾼다. 값과 <c>shared_ptr&lt;const T&gt;</c>만 담긴 프레임은 그 변경과 무관하게
    /// 자기 것을 끝까지 쥐고 있지만, 생 포인터가 하나라도 섞이면 렌더 스레드가 이미 사라진
    /// 것을 읽는다. 그 실패는 로그도 예외도 없이 나타나며 재현되지 않는다.
    ///
    /// 컴파일 시간에 멤버를 훑을 방법이 없어서 — C++에 리플렉션이 없다 — 헤더의 멤버 선언을
    /// 읽는다. <c>LayeringTests</c>가 <c>#include</c>를 같은 방식으로 읽으므로 새 수법은 아니다.
    /// </summary>
    const std::filesystem::path FrameHeaderPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() /
        "GameEngine" / "Rendering" / "RenderFrame.h";

    /// <summary>
    /// 캡처가 렌더 스레드로 건너갈 때 쓰는 값 타입들이다. 프레임과 같은 이유로 같은 계약이
    /// 걸린다 — 건너간 뒤 게임 스레드가 장면을 계속 바꾼다.
    /// </summary>
    const std::filesystem::path SubmissionHeaderPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() /
        "GameEngine" / "App" / "RenderSubmission.h";

    /// <summary>
    /// 이 계약에서 면제된 타입들이다. 각 항목은 결정이므로 근거와 함께만 늘어난다.
    ///
    /// <c>ValidatedRenderFrame</c>은 자기가 방금 검증한 프레임을 가리킨다. 장면의 것이 아니라
    /// 프레임 자신이고, 큐를 건너지도 않는다 — 검증한 그 자리에서 쓰이고 버려진다.
    /// </summary>
    const std::vector<std::string> ExemptTypes{ "ValidatedRenderFrame" };

    /// <summary>여는 중괄호가 몇 개 더 열렸는지다. 함수 본문을 멤버 선언과 가르는 데 쓴다.</summary>
    [[nodiscard]] int BraceDelta(const std::string_view line)
    {
        int delta = 0;
        for (const char character : line)
        {
            delta += character == '{' ? 1 : (character == '}' ? -1 : 0);
        }
        return delta;
    }

    [[nodiscard]] std::string_view TrimLeft(std::string_view line)
    {
        const std::size_t first = line.find_first_not_of(" \t");
        return first == std::string_view::npos ? std::string_view{} : line.substr(first);
    }

    /// <summary>
    /// 줄 끝의 공백과 캐리지 리턴을 뗀다. 작업 트리의 줄바꿈은 체크아웃 설정이 정하므로, 이
    /// 검사가 그것에 좌우되면 같은 커밋이 기계마다 다르게 통과한다.
    /// </summary>
    [[nodiscard]] std::string_view TrimRight(std::string_view line)
    {
        const std::size_t last = line.find_last_not_of(" \t\r");
        return last == std::string_view::npos ? std::string_view{} : line.substr(0, last + 1);
    }

    /// <summary>이 줄이 타입 선언을 여는 줄이면 그 타입 이름이고, 아니면 빈 문자열이다.</summary>
    [[nodiscard]] std::string DeclaredTypeName(const std::string_view line)
    {
        const std::string_view trimmed = TrimLeft(line);
        for (const std::string_view keyword : { "struct ", "class " })
        {
            if (!trimmed.starts_with(keyword))
            {
                continue;
            }
            const std::string_view rest = TrimRight(trimmed.substr(keyword.size()));
            const std::size_t end = rest.find_first_of(" :;{<");
            // 전방 선언(`struct RenderFrame;`)에는 본문이 없으므로 이름을 낼 필요가 없다.
            if (end != std::string_view::npos && rest[end] == ';')
            {
                return {};
            }
            return std::string(rest.substr(0, end == std::string_view::npos ? rest.size() : end));
        }
        return {};
    }

    /// <summary>
    /// 이 줄이 생 포인터 멤버를 선언하는지다.
    ///
    /// 멤버 선언은 세미콜론으로 끝나고 괄호를 갖지 않는다 — 괄호가 있으면 함수이고, 함수의
    /// 반환형이나 매개변수에 있는 포인터는 이 계약의 대상이 아니다. 프레임 안의 벡터를 가리켜
    /// 돌려주는 접근자가 그런 경우다.
    /// </summary>
    [[nodiscard]] bool DeclaresPointerMember(const std::string_view line)
    {
        const std::string_view trimmed = TrimLeft(line);
        if (trimmed.starts_with("//") || trimmed.starts_with("*") || trimmed.starts_with("/*"))
        {
            return false;
        }
        if (trimmed.find(';') == std::string_view::npos ||
            trimmed.find('(') != std::string_view::npos)
        {
            return false;
        }
        const std::size_t star = trimmed.find('*');
        if (star == std::string_view::npos)
        {
            return false;
        }
        // `shared_ptr<const T>`처럼 소유를 말하는 것은 별을 쓰지 않으므로 여기 오지 않는다.
        // 남은 별은 선언의 것이며, 그 뒤에 이름이 와야 멤버다.
        const std::string_view afterStar = TrimLeft(trimmed.substr(star + 1));
        return !afterStar.empty() &&
            (std::isalpha(static_cast<unsigned char>(afterStar.front())) != 0 ||
                afterStar.front() == '_');
    }

}

namespace
{
    /// <summary>
    /// 한 헤더의 멤버 선언을 훑어 생 포인터를 모은다. 찾은 것은 「타입 (파일:줄): 선언」으로
    /// 적히고, 그 헤더가 담아야 할 타입을 못 찾았으면 그것도 실패로 답한다 — 검사가 아무것도
    /// 안 보고 통과하는 것이 가장 나쁜 결과라서다.
    /// </summary>
    /// <param name="headerPath">훑을 헤더다.</param>
    /// <param name="requiredType">그 헤더에 반드시 있어야 하는 타입 이름이다.</param>
    /// <param name="offenders">찾은 생 포인터 멤버가 덧붙는다.</param>
    /// <returns>헤더를 읽었고 요구한 타입을 찾았으면 true이다.</returns>
    [[nodiscard]] bool ScanHeader(
        const std::filesystem::path& headerPath,
        const std::string_view requiredType,
        std::vector<std::string>& offenders)
    {
        std::ifstream stream(headerPath, std::ios::binary);
        if (!stream)
        {
            std::cerr << "FAILED: a contract header could not be read. path="
                      << headerPath.string() << '\n';
            return false;
        }
        const std::string fileName = headerPath.filename().string();

        std::string currentType;
    int typeDepth = 0;
    int depth = 0;
    // 선언 줄과 여는 중괄호가 다른 줄에 있는 것이 이 저장소의 스타일이라, 본문이 실제로 열린
    // 뒤에야 타입이 끝났는지 물을 수 있다. 그 전에 물으면 선언 줄에서 바로 끝난 것이 된다.
    bool insideBody = false;
    std::string line;
        std::size_t lineNumber = 0;
        bool sawRequiredType = false;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            const std::string_view trimmed = TrimRight(line);
            if (currentType.empty())
            {
                if (std::string declared = DeclaredTypeName(trimmed); !declared.empty())
                {
                    currentType = std::move(declared);
                    typeDepth = depth;
                    insideBody = false;
                    sawRequiredType = sawRequiredType || currentType == requiredType;
                }
            }
            else if (insideBody && depth == typeDepth + 1 && DeclaresPointerMember(trimmed) &&
                std::ranges::find(ExemptTypes, currentType) == ExemptTypes.end())
            {
                offenders.emplace_back(
                    currentType + " (" + fileName + ":" + std::to_string(lineNumber) + "): " +
                    std::string(TrimLeft(trimmed)));
            }

            depth += BraceDelta(trimmed);
            if (!currentType.empty())
            {
                insideBody = insideBody || depth > typeDepth;
                if (insideBody && depth <= typeDepth)
                {
                    currentType.clear();
                }
            }
        }
        return sawRequiredType;
    }
}

bool RunRenderFramePointerContractTests()
{
    std::vector<std::string> offenders;
    // 계약이 걸린 헤더들이다. 스레드를 건너는 타입이 새로 생기면 여기 한 줄이 는다 — 그것이
    // 설계 결정이라, 목록에 더하는 일이 곧 그 결정을 적는 일이 된다.
    const bool sawFrameType = ScanHeader(FrameHeaderPath, "RenderFrame", offenders);
    const bool sawSubmissionType = ScanHeader(SubmissionHeaderPath, "CaptureJob", offenders);

    for (const std::string& offender : offenders)
    {
        std::cerr << "  raw pointer member: " << offender << '\n';
    }

    return Expect(
               sawFrameType && sawSubmissionType,
               "the contract scan should have found the types it guards in their headers") &&
        Expect(
            ExemptTypes.size() == 1,
            "the exemption list should not have grown; each entry is a decision, not a habit") &&
        Expect(
            offenders.empty(),
            "no type that crosses to the render thread may hold a raw pointer, because it outlives "
            "the scene state it was built from");
}

static const TestSupport::Registration gRenderFramePointerContractTests{
    "RenderFrame", "render frame pointer contract tests should pass", RunRenderFramePointerContractTests };
