#include "Rules/OrphanSidecars.h"

#include <string_view>

#include "Assets/Asset.h"

namespace GameEditor
{

namespace
{
    /// <summary>이 파일 이름이 그 꼬리로 끝나면 꼬리를 뗀 이름이고, 아니면 비어 있다.</summary>
    [[nodiscard]] std::string WithoutSuffix(
        const std::string& name, const std::string_view suffix)
    {
        return name.size() > suffix.size() && name.ends_with(suffix)
            ? name.substr(0, name.size() - suffix.size())
            : std::string{};
    }
}

bool IsSetAsideSidecar(const std::filesystem::path& path)
{
    const std::string name = path.filename().generic_string();
    const std::string withoutOrphan =
        WithoutSuffix(name, GameEngine::Assets::OrphanedSidecarSuffix);
    if (withoutOrphan.empty())
    {
        return false;
    }
    for (const std::string_view suffix : GameEngine::Assets::SidecarSuffixes)
    {
        if (!WithoutSuffix(withoutOrphan, suffix).empty())
        {
            return true;
        }
    }
    return false;
}

std::filesystem::path SidecarOwnerPath(const std::filesystem::path& sidecarPath)
{
    std::string name = sidecarPath.filename().generic_string();
    // 치워 둔 것이면 그 꼬리를 먼저 뗀다. 치우는 일이 이름 뒤에 하나를 더 붙일 뿐이라, 주인을
    // 묻는 질문은 치우기 전과 후가 같은 답을 내야 한다.
    if (const std::string withoutOrphan =
            WithoutSuffix(name, GameEngine::Assets::OrphanedSidecarSuffix);
        !withoutOrphan.empty())
    {
        name = withoutOrphan;
    }
    for (const std::string_view suffix : GameEngine::Assets::SidecarSuffixes)
    {
        if (const std::string owner = WithoutSuffix(name, suffix); !owner.empty())
        {
            const std::filesystem::path parent = sidecarPath.parent_path();
            return parent.empty() ? std::filesystem::path(owner) : parent / owner;
        }
    }
    return {};
}

std::string OrphanSidecarPlan::Describe() const
{
    // 이번에 묻는 한 단계만 말한다. 두 단계를 한 줄에 실으면 사람이 무엇에 답했는지 흐려진다.
    const std::vector<OrphanSidecar>& asking = IsAskingToDelete() ? toDelete : toSetAside;
    std::string text = IsAskingToDelete()
        ? "Delete " + std::to_string(asking.size()) +
            " metadata files that were set aside and are still unclaimed?"
        : "Set aside " + std::to_string(asking.size()) +
            " metadata files whose asset is gone?";

    constexpr std::size_t NamedFiles = 3;
    for (std::size_t index = 0; index < asking.size() && index < NamedFiles; ++index)
    {
        text += (index == 0 ? " " : ", ") + asking[index].ownerPath.filename().generic_string();
    }
    if (asking.size() > NamedFiles)
    {
        text += ", and " + std::to_string(asking.size() - NamedFiles) + " more";
    }
    if (!keptForReferences.empty())
    {
        // 손대지 않는 것도 말한다. 「몇 개 있었는데 왜 이만큼만 나오지」가 사람의 다음 질문이고,
        // 그 답이 여기 없으면 기능이 일부만 도는 것처럼 보인다.
        text += ". " + std::to_string(keptForReferences.size()) +
            " are kept because scenes still point at them";
    }
    return text + ".";
}

}
