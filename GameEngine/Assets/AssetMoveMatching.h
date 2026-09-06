#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "../Core/Guid.h"

namespace GameEngine::Assets
{

/// <summary>재스캔에서 자리가 비어 버린 에셋이다. 옮겨졌을 수도, 지워졌을 수도 있다.</summary>
struct DepartedAsset
{
    std::filesystem::path path;
    std::uint64_t contentHash = 0;
    Core::Guid guid;
};

/// <summary>재스캔에서 새로 나타났고 아직 정체성이 없는 파일이다.</summary>
struct ArrivedFile
{
    std::filesystem::path path;
    std::uint64_t contentHash = 0;
    /// <summary>이 파일이 이미 자기 사이드카를 갖고 있는지다. 가졌으면 남의 것을 받지 않는다.</summary>
    bool hasSidecar = false;
};

/// <summary>
/// 짝이 지어졌는데도 이동으로 보지 않은 이유다. 사람에게 왜 안 옮겼는지 말하기 위해 남긴다 —
/// 아무 말 없이 안 옮기면 기능이 고장 난 것과 구별되지 않는다.
/// </summary>
enum class MoveRefusal : unsigned char
{
    /// <summary>이동이다. 옮겨도 된다.</summary>
    None,
    /// <summary>같은 내용의 후보가 여럿이라 어느 것인지 말할 수 없다.</summary>
    SeveralCandidates,
    /// <summary>확장자가 다르다. 이동은 확장자를 바꾸지 않는다.</summary>
    DifferentExtension,
    /// <summary>이름도 폴더도 둘 다 달라 옮긴 것이라 볼 근거가 없다.</summary>
    UnrelatedLocation,
    /// <summary>받을 자리가 이미 자기 사이드카를 갖고 있다.</summary>
    DestinationHasSidecar,
};

/// <summary>사라진 자리와 나타난 자리를 이은 결과 하나다.</summary>
struct AssetMove
{
    std::filesystem::path from;
    std::filesystem::path to;
    Core::Guid guid;
    /// <summary><see cref="MoveRefusal::None"/>이면 옮긴다. 아니면 그 이유를 말하고 두고 본다.</summary>
    MoveRefusal refusal = MoveRefusal::None;
};

/// <summary>
/// 사라진 에셋들과 새로 나타난 파일들을 내용으로 짝지어, 무엇이 옮겨진 것인지 판정한다.
///
/// 파일도 데이터베이스도 모른다. 두 목록과 규칙만 아는 순수 함수라 화면도 디스크도 없이 시험할
/// 수 있고, 그래서 「무엇을 이동으로 보는가」가 파일을 옮기는 코드 안에 묻히지 않는다.
///
/// 판정이 틀렸을 때의 대가가 비대칭이라 규칙이 좁다. 옮기지 못하면 참조가 끊기고 사람이 알아채지만,
/// 잘못 옮기면 <b>참조가 조용히 다른 것을 가리킨다</b>. 그래서 내용이 같은 짝이 유일하고, 확장자가
/// 같고, 이름이 같거나 폴더가 같고, 받을 자리에 사이드카가 없을 때에만 이동으로 본다.
///
/// 여기서 Unity와 길이 갈린다. Unity는 내용 기반 이동 감지를 하지 않아, <c>.meta</c> 없이 옮겨진
/// 파일에는 새 정체성을 주고 그것을 가리키던 참조를 끊는다. 이 엔진은 내용
/// 해시를 따라 이동을 이으며, 그 대가로 위 조건 다섯을 건다 — 조건이 곧 이 선택의 안전장치이므로
/// 하나라도 완화하면 남는 것은 「조용히 틀린 참조」뿐이다.
/// </summary>
/// <param name="departed">자리가 빈 에셋들이다. 정체성이 없는 것은 옮길 것이 없으므로 무시된다.</param>
/// <param name="arrived">새로 나타난 파일들이다.</param>
/// <returns>
/// 이은 짝들이다. 각각이 옮길 것인지, 아니면 왜 안 옮기는지를 담는다. 짝을 아예 찾지 못한
/// 에셋은 여기 없다 — 그것은 지워진 것이고 이 함수가 답할 질문이 아니다.
/// </returns>
[[nodiscard]] std::vector<AssetMove> MatchAssetMoves(
    std::span<const DepartedAsset> departed, std::span<const ArrivedFile> arrived);

/// <summary>거절 이유를 사람이 읽는 한 마디로 옮긴다. 로그가 이것을 쓴다.</summary>
[[nodiscard]] std::string_view DescribeMoveRefusal(MoveRefusal refusal);

}
