#pragma once

// editor-layer: 0 (Rules)

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "Core/Guid.h"

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEngine::Runtime
{
class Scene;
}

namespace GameEngine::Serialization
{
struct ComponentSchema;
}

namespace GameEditor
{

/// <summary>
/// 장면 하나를 훑어, 그 안의 에셋 참조가 어떤 상태인지 센 것이다.
///
/// 세는 것과 바꾸는 것을 가른 이유는 이관이 되돌리기 어려운 단계라서다. 사람에게 무엇이 몇 개
/// 바뀌는지 먼저 보이고 답을 받은 뒤에야 파일을 건드린다.
/// </summary>
struct SceneReferenceSurvey
{
    /// <summary>경로로 적혔고, 정체성을 가진 에셋으로 풀리는 참조 수다. 이관 대상이 이것이다.</summary>
    std::size_t convertible = 0;
    /// <summary>경로로 적혔는데 가리키는 에셋이 없는 참조 수다. 바꿀 대상이 없어 바꿀 수 없다.</summary>
    std::size_t unresolved = 0;
    /// <summary>이미 정체성으로 적힌 참조 수다. 다시 열었을 때 물을 것이 남았는지 가른다.</summary>
    std::size_t alreadyMigrated = 0;
    /// <summary>풀리기는 하는데 정체성이 없는 에셋들이다. 하나라도 있으면 이관하지 않는다.</summary>
    std::vector<std::filesystem::path> assetsWithoutIdentity;
    /// <summary>
    /// 속성을 알 수 없어 참조를 들여다볼 수 없는 컴포넌트 수다. 게임이 정의했고 스키마가 없는
    /// 타입이 이렇게 실려 온다.
    ///
    /// 스키마가 있는 보존 컴포넌트는 여기 들지 않는다. 스키마가 어느 속성이 참조인지
    /// 알려 주므로 그 안의 참조도 세고 옮길 수 있다.
    ///
    /// 그것이 이관을 막는다. 그 안의 경로 참조는 「몇 개 남는다」가 아니라 <b>아예 보이지
    /// 않고</b>, 그래서 이관은 그 장면을 끝났다고 표시한 뒤 나중에 그 에셋을 옮기는 날 조용히
    /// 끊는다 — 이관이 없애려던 바로 그 고장이다. 고치는 값은 게임 프로젝트를 한 번 빌드하는
    /// 것뿐이라, 막고 그렇게 말하는 편이 싸다.
    /// </summary>
    std::size_t opaqueComponents = 0;
    /// <summary>그 컴포넌트들의 타입 이름이다. 중복은 지워져 있고, 사람에게 무엇을 말할지가 이것이다.</summary>
    std::vector<std::string> opaqueTypeNames;
};

/// <summary>장면 하나와 그 장면을 훑은 결과다.</summary>
struct SceneMigrationEntry
{
    /// <summary>프로젝트 루트 기준 장면 경로다.</summary>
    std::filesystem::path scenePath;
    SceneReferenceSurvey survey;
};

/// <summary>
/// 프로젝트 전체를 훑어 세운, 사람에게 물을 이관 계획이다.
///
/// 이관은 이 일감에서 되돌리기 어려운 유일한 단계라 — <c>.meta</c>를 만들고 옮기는 것은 조용히
/// 하지만 이것만은 관문을 지난다 — 물을 내용을 값으로 먼저 세운다. 뷰는 이 값을 한 줄로 옮기고
/// 클릭 둘을 돌려줄 뿐이라, 판정과 실행은 창 없이 전부 시험된다.
/// </summary>
struct SceneMigrationPlan
{
    /// <summary>바꿀 것이 있거나 막는 것이 있는 장면들이다.</summary>
    std::vector<SceneMigrationEntry> scenes;
    /// <summary>
    /// 참조되는데 아직 정체성이 없는 에셋들이다. 중복은 지워져 있다. 비어 있지 않으면 이관하지
    /// 않는다 — 그 참조만 경로로 남으면 한 장면에 두 형식이 섞이기 때문이다.
    /// </summary>
    std::vector<std::filesystem::path> assetsWithoutIdentity;
    /// <summary>정체성으로 바꿀 수 있는 참조의 총수다.</summary>
    std::size_t convertible = 0;
    /// <summary>가리키는 에셋이 없어 바꿀 수 없는 참조의 총수다. 경로로 남는다.</summary>
    std::size_t unresolved = 0;
    /// <summary>속성을 알 수 없어 그 안의 참조가 보이지 않는 컴포넌트의 총수다.</summary>
    std::size_t opaqueComponents = 0;
    /// <summary>그 컴포넌트들의 타입 이름이다. 중복은 지워져 있다.</summary>
    std::vector<std::string> opaqueTypeNames;

    /// <summary>
    /// 이관을 막는 것이 있는지다. 둘이 막는다.
    ///
    /// 하나는 정체성 없는 에셋을 가리키는 참조다. 그 참조만 경로로 남으면 한 장면에 두 형식이
    /// 섞이는데, 그것은 발급을 한 번 더 돌리면 사라지는 상태다.
    ///
    /// 다른 하나는 들여다볼 수 없는 컴포넌트다. 그 안의 참조는 남는 것이 아니라 <b>보이지
    /// 않는</b> 것이라, 막지 않으면 이관은 끝났다고 말하고 그 참조들은 나중에 조용히 끊긴다.
    ///
    /// 가리키는 것이 없는 참조는 막지 않는다. 형식을 바꿔 얻을 것이 없고, 막으면 오타 하나로
    /// 그 프로젝트가 영원히 이관되지 않는다. 그 하나에 한해 두 형식이 섞이며, 그 사실은
    /// 질문과 로그에 적힌다.
    /// </summary>
    [[nodiscard]] bool IsBlocked() const
    {
        return !assetsWithoutIdentity.empty() || opaqueComponents > 0;
    }

    /// <summary>
    /// 사람에게 물을 것이 있는지다.
    ///
    /// 들여다볼 수 없는 컴포넌트만 있고 바꿀 것이 없으면 묻지 않는다. 물어 봐야 사람이 지금
    /// 할 일이 없고, 프로젝트를 열 때마다 답할 수 없는 질문이 뜬다. 대신 그 프로젝트는
    /// <see cref="EditorContext::AreSceneReferencesMigrated"/>가 거짓으로 남아 새 참조를 계속
    /// 경로로 적는다 — 볼 수 없는 것을 보았다고 치지 않는 쪽이다.
    /// </summary>
    [[nodiscard]] bool HasSomethingToAsk() const
    {
        return convertible > 0 || !assetsWithoutIdentity.empty();
    }

    /// <summary>
    /// 툴바 확인 줄에 놓을 한 줄이다. 장면이 많으면 앞의 몇만 적고 나머지는 수로 줄인다 —
    /// 창이 좁아도 잘리는 것이 질문이어서는 안 된다.
    /// </summary>
    [[nodiscard]] std::string Describe() const;
};

/// <summary>
/// 장면의 에셋 참조들을 세되 아무것도 바꾸지 않는다.
///
/// <see cref="MigrateSceneReferences"/>와 같은 길을 걷는다. 세는 쪽과 바꾸는 쪽이 서로 다른
/// 참조를 보면 사람이 승인한 수와 실제로 바뀐 수가 어긋나므로, 걷는 규칙은 한 곳에만 있다.
/// </summary>
/// <param name="schemas">
/// 이 프로세스에 등록되지 않은 게임 컴포넌트의 스키마다.
/// 보존 데이터의 어느 속성이 참조인지 알려 준다.
/// </param>
[[nodiscard]] SceneReferenceSurvey SurveySceneReferences(
    const GameEngine::Runtime::Scene& scene, const GameEngine::Assets::AssetDatabase& database,
    std::span<const GameEngine::Serialization::ComponentSchema> schemas);

/// <summary>
/// 경로로 적힌 참조를 그 에셋의 정체성으로 바꾼다. 가리키는 에셋이 없거나 그 에셋에 정체성이
/// 없는 참조는 건드리지 않는다 — 바꿀 값이 없기 때문이다.
/// </summary>
/// <returns>실제로 바꾼 참조 수다.</returns>
[[nodiscard]] std::size_t MigrateSceneReferences(
    GameEngine::Runtime::Scene& scene, const GameEngine::Assets::AssetDatabase& database,
    std::span<const GameEngine::Serialization::ComponentSchema> schemas);

/// <summary>
/// 이 장면이 정체성으로 가리키고 있는 에셋들의 guid다.
///
/// 주인 없는 사이드카를 치우기 전에 물어야 하는 것이 이것이다. 장면이 가리키는 정체성의
/// 사이드카는 고아가 아니라 <b>파일이 없는 에셋</b>이며, 그것을 치우면 사람이 무엇을 잃었는지
/// 알아낼 마지막 단서까지 사라진다.
///
/// 이관과 같은 걸음을 쓴다 — 보존된 컴포넌트 안까지 본다. 한쪽만 보면 게임 컴포넌트가 가리키는
/// 에셋의 사이드카가 고아로 분류된다.
/// </summary>
[[nodiscard]] std::vector<GameEngine::Core::Guid> CollectReferencedGuids(
    const GameEngine::Runtime::Scene& scene,
    std::span<const GameEngine::Serialization::ComponentSchema> schemas);

}
