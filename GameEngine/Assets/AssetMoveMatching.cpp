#include "pch.h"
#include "AssetMoveMatching.h"

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace GameEngine::Assets
{

namespace
{
    /// <summary>
    /// 이 짝을 이동으로 볼 수 있는지 판정한다. 규칙 하나하나가 「잘못 옮기는 것」을 막는 자리라,
    /// 순서와 무관하게 전부 통과해야 한다.
    /// </summary>
    [[nodiscard]] MoveRefusal Judge(const DepartedAsset& from, const ArrivedFile& to)
    {
        if (from.path.extension() != to.path.extension())
        {
            return MoveRefusal::DifferentExtension;
        }
        // 옮기기는 폴더를 바꾸고 이름을 남기며, 이름 바꾸기는 이름을 바꾸고 폴더를 남긴다. 둘 다
        // 바뀐 것은 「지우고 다른 것을 넣었다」와 구별할 방법이 없다.
        const bool sameName = from.path.filename() == to.path.filename();
        const bool sameFolder = from.path.parent_path() == to.path.parent_path();
        if (!sameName && !sameFolder)
        {
            return MoveRefusal::UnrelatedLocation;
        }
        if (to.hasSidecar)
        {
            return MoveRefusal::DestinationHasSidecar;
        }
        return MoveRefusal::None;
    }
}

std::vector<AssetMove> MatchAssetMoves(
    const std::span<const DepartedAsset> departed, const std::span<const ArrivedFile> arrived)
{
    // 내용으로 먼저 모은다. 양쪽 모두 후보가 정확히 하나일 때만 이을 수 있으므로, 각 내용이 몇
    // 번 나타나는지가 첫 질문이다.
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> arrivedByContent;
    for (std::size_t index = 0; index < arrived.size(); ++index)
    {
        arrivedByContent[arrived[index].contentHash].push_back(index);
    }
    std::unordered_map<std::uint64_t, std::size_t> departedCountByContent;
    for (const DepartedAsset& asset : departed)
    {
        if (asset.guid.IsValid())
        {
            ++departedCountByContent[asset.contentHash];
        }
    }

    std::vector<AssetMove> moves;
    for (const DepartedAsset& asset : departed)
    {
        // 정체성이 없으면 옮길 것이 없다. 그런 에셋은 경로가 곧 이름이었으므로 새 자리에서 새로
        // 발급받는 것이 맞다.
        if (!asset.guid.IsValid())
        {
            continue;
        }
        const auto candidates = arrivedByContent.find(asset.contentHash);
        if (candidates == arrivedByContent.end())
        {
            // 같은 내용이 나타난 곳이 없다. 옮긴 것이 아니라 지워진 것이며, 그것은 이 함수가
            // 답할 질문이 아니다.
            continue;
        }

        AssetMove move;
        move.from = asset.path;
        move.guid = asset.guid;
        if (candidates->second.size() != 1 || departedCountByContent[asset.contentHash] != 1)
        {
            move.refusal = MoveRefusal::SeveralCandidates;
            moves.push_back(std::move(move));
            continue;
        }
        const ArrivedFile& destination = arrived[candidates->second.front()];
        move.to = destination.path;
        move.refusal = Judge(asset, destination);
        moves.push_back(std::move(move));
    }
    return moves;
}

std::string_view DescribeMoveRefusal(const MoveRefusal refusal)
{
    switch (refusal)
    {
    case MoveRefusal::None:
        return "it moved";
    case MoveRefusal::SeveralCandidates:
        return "more than one file has the same contents, so which one it became cannot be told";
    case MoveRefusal::DifferentExtension:
        return "the extension differs, and moving a file does not change it";
    case MoveRefusal::UnrelatedLocation:
        return "neither the name nor the folder is the same, so this is a delete and an add";
    case MoveRefusal::DestinationHasSidecar:
        return "the file that appeared already carries metadata of its own";
    }
    return "unknown";
}

}
