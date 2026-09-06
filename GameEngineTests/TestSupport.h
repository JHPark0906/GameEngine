#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include <vector>

#include "Assets/AssetImporterRegistry.h"
#include "Platform/ITextRasterizer.h"
#include "Serialization/ComponentFactory.h"

namespace TestSupport
{

/// <summary>
/// 한 테스트가 도는 동안 존재하고 그와 함께 지워지는 디렉터리이다.
///
/// 이름이 경로의 일부인 이유는, 아니면 두 번째 인스턴스가 첫 번째의 내용물을 지웠을 것이기
/// 때문이다: 경로는 프로세스 id에서 나오고, 생성자는 거기 있는 것을 비운다. 자기만의
/// 프로젝트 루트가 필요한 테스트는 이름을 넘긴다. 스위트는 CTest 아래에서 저마다의 프로세스로
/// 병렬로 돌 수 있으므로, 프로세스 id가 스위트 사이의 충돌도 막는다.
/// </summary>
class TemporaryDirectory final
{
public:
    explicit TemporaryDirectory(std::string_view name = {});
    ~TemporaryDirectory();

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& GetPath() const { return mPath; }

private:
    std::filesystem::path mPath;
};

/// <summary>돌린 명령 하나의 결과다.</summary>
struct CommandResult
{
    /// <summary>종료 코드다. 시작하지 못했으면 비어 있다.</summary>
    std::optional<int> exitCode;
    /// <summary>표준 출력과 표준 오류를 순서대로 이은 것이다.</summary>
    std::string output;
};

/// <summary>
/// 프로그램 하나를 끝날 때까지 돌리고 종료 코드와 출력을 함께 돌려준다.
///
/// 셸을 거치지 않는 것이 요점이다. 명령을 문자열로 이어 <c>std::system</c>에 주면 인용 규칙이
/// 끼어드는데, 그 규칙은 프로그램마다 다르다 — 공백이 든 경로의 cmake를 그렇게 부르면
/// <c>cmd</c>가 경로를 첫 공백에서 자르고, 시험은 도구가 아니라 인용 때문에 실패한다.
/// 인자를 하나씩 넘기면 그 자리가 아예 없다.
/// </summary>
[[nodiscard]] CommandResult RunCommand(
    const std::filesystem::path& executable, const std::vector<std::string>& arguments,
    const std::filesystem::path& workingDirectory = {});

/// <summary>부모 디렉터리를 만들며 파일 하나를 통째로 쓴다.</summary>
bool WriteFile(const std::filesystem::path& path, std::string_view contents);

/// <summary>
/// 파일 하나를 통째로 읽는다. 없는 파일은 빈 문자열이다.
///
/// 엔진의 <c>Core::ReadTextFile</c>과 달리 실패를 값으로 구분하지 않는 이유는, 시험이
/// 묻는 것이 대개 「내용이 이것인가」이기 때문이다. 없는 것과 빈 것을 갈라야 하는 시험은
/// <c>std::filesystem::exists</c>를 따로 묻는다 — 그 둘을 섮으면 안 되는 자리에서는 섮이지 않는 것이
/// 보이게 하려는 것이 이 이름의 뜻이다.
/// </summary>
[[nodiscard]] std::string ReadFile(const std::filesystem::path& path);

/// <summary>
/// 단언 조건이 거짓이면 메시지를 stderr에 적고 조건 값을 그대로 돌려준다.
/// 실패 문구는 하네스가 스위트 실패를 기록할 때 쓰는 형식과 일치한다.
/// </summary>
/// <param name="condition">참이어야 하는 것이다.</param>
/// <param name="message">거짓일 때 사람이 읽을 문장이다.</param>
/// <returns><paramref name="condition"/> 그대로다. 단언을 이어 붙일 수 있게.</returns>
bool Expect(bool condition, const char* message);

/// <summary>
/// 배치·크기·글자를 재는 검사를 <b>배율 1과 2에서 함께</b> 돌린다.
///
/// 배율 1에서는 논리 픽셀과 물리 픽셀이 같은 수다. 그래서 둘을 헷갈리는 결함 — 곱해야 할
/// 자리에서 안 곱하거나, 한 번 곱할 것을 두 번 곱하는 것 — 이 배율 1에서는 전부 참으로 보인다.
/// 사용자의 화면이 200%이므로 그 결함은 화면에서만 드러나고, 그때는 원인이 배율인지 글꼴인지
/// 배치인지 눈으로 가려야 한다.
///
/// 헬퍼를 한자리에 두는 이유는 각자 만들면 각자 다른 배율을 고르기 때문이다. 어떤 검사는 1.5로,
/// 어떤 검사는 2로 돌면 "배율에서 확인했다"가 무엇을 뜻하는지 시험마다 달라진다.
/// </summary>
/// <param name="check">배율 하나를 받아 그 배율에서 검사하는 호출 가능 객체다.</param>
/// <returns>두 배율 모두에서 참이어야 참이다. 실패한 배율은 stderr에 적힌다.</returns>
[[nodiscard]] bool ForEachUiScale(const std::function<bool(float scale)>& check);

/// <summary>
/// 빌드가 다시 쓰는 중일 수 있는 파일을 읽는다.
///
/// 컴포넌트 스키마처럼 저장소 안에 있으면서 빌드가 매번 다시 쓰는 파일이 있고, 시험이 그것을
/// 읽는다. 두 일이 겹치는 순간 파일은 이름이 바뀌는 중이라 잠깐 열리지 않는다 — 그 순간을
/// 「빌드를 안 했다」로 읽으면 시험이 이유 없이 붉어진다.
///
/// 그래서 <b>없는 것과 지금 바뀌는 중인 것을 가른다</b>: 파일이 아예 없으면 곧바로 빈 값을
/// 답하고(그것은 정말로 빌드하지 않은 것이다), 있는데 읽히지 않으면 잠깐 기다렸다 다시 본다.
/// 기다림에는 끝이 있으므로, 정말로 읽을 수 없는 파일이 시험을 붙잡아 두지는 않는다.
/// </summary>
/// <param name="path">읽을 파일이다.</param>
/// <returns>읽은 내용이며, 파일이 없거나 끝내 읽히지 않으면 빈 값이다.</returns>
[[nodiscard]] std::optional<std::string> ReadFileWhenSettled(const std::filesystem::path& path);

/// <summary>
/// 기본 텍스트 래스터라이저를 만들고, 이 저장소에 들어 있는 폰트 하나를 곧바로 등록해 돌려준다.
///
/// <c>EngineTextRasterizer</c>는 폰트가 하나도 없으면 배치 자체를 거절한다 — 「폰트가 없다」와
/// 「그 글자가 없다」를 같은 것으로 두지 않으려는, <c>FontLibrary</c>가 이미 세운 선택이다.
/// 시스템 폰트로 조용히 넘어가지 않으므로, 텍스트를 다루는 시험마다 폰트를 등록하지 않으면
/// 배치를 묻기도 전에 전부 거절부터 맞는다. 그 준비를 한자리로 모은 것이 이 함수다.
/// </summary>
/// <param name="familyAlias">등록한 폰트를 부를 이름이다. 시험이 여러 별명을 견주지 않는 한
/// 기본값을 그대로 쓴다.</param>
/// <returns>
/// 초기화와 폰트 등록이 모두 됐으면 그 래스터라이저다. 이 저장소에 동봉된 폰트 파일을 찾지
/// 못하는 등 준비가 안 됐으면 null이다 — 부르는 쪽은 그것을 「이 기계에서는 건너뛴다」로
/// 읽는다.
/// </returns>
[[nodiscard]] std::unique_ptr<GameEngine::Platform::ITextRasterizer> CreateTestTextRasterizer(
    std::string_view familyAlias = "TestFont");

/// <summary>
/// 시험이 사용한 프로세스 전역 등록부를 원래 상태로 복원한다.
/// 엔진 임포터와 컴포넌트 팩토리가 미리 등록되어 있으므로 전체 비우기로 시험을 격리해서는 안 된다.
/// 부트스트랩 등록부는 시험 실행 파일에 EditorRegistration.cpp가 포함되지 않아 초기 상태가 비어 있다.
/// 이 등록부는 스냅샷 대신 Reset으로 복원한다.
/// </summary>
class RegistryScope final
{
public:
    RegistryScope();
    ~RegistryScope();

    RegistryScope(const RegistryScope&) = delete;
    RegistryScope& operator=(const RegistryScope&) = delete;

private:
    GameEngine::Assets::AssetImporterRegistry::Registrations mImporters;
    GameEngine::Serialization::ComponentFactory::Registrations mComponentFactories;
};

/// <summary>
/// 등록부에 실린 시험 하나다. 어느 스위트에 속하고, 무엇이라 불리며, 무엇을 부르는지.
/// </summary>
struct RegisteredTest
{
    std::string_view suite;
    std::string_view description;
    bool (*run)() = nullptr;
};

/// <summary>지금까지 자신을 등록한 시험들이다. 스위트 이름 순서는 여기서 정해지지 않는다.</summary>
[[nodiscard]] const std::vector<RegisteredTest>& RegisteredTests();

/// <summary>
/// 각 시험 파일 끝에서 시험을 등록한다. 공유 실행 파일에는 스위트 이름만 둔다.
/// 등록부는 함수 지역 정적으로 첫 등록 시 생성하므로 정적 초기화 순서에 의존하지 않는다.
/// </summary>
class Registration final
{
public:
    Registration(std::string_view suite, std::string_view description, bool (*run)());

    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;
};

}
