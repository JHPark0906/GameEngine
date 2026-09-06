#pragma once

// editor-layer: 0 (Rules)

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Guid.h"

namespace GameEditor
{

/// <summary>주인이 사라진 사이드카 파일 하나다.</summary>
struct OrphanSidecar
{
    /// <summary>사이드카 파일의 프로젝트 상대 경로다.</summary>
    std::filesystem::path sidecarPath;
    /// <summary>
    /// 이 사이드카가 곁에 있던 에셋의 경로다. 파일 이름에서 꼬리를 뗀 것이라, 지금은 없는 그
    /// 파일이 무엇이었는지를 말해 주는 유일한 기록이다.
    /// </summary>
    std::filesystem::path ownerPath;
    /// <summary>그 안에 적힌 정체성이다. 읽지 못했으면 무효다.</summary>
    GameEngine::Core::Guid guid;
};

/// <summary>
/// 사람에게 물을 사이드카 정리 계획이다.
///
/// 두 단계가 한 계획에 함께 있지만 <b>한 번에 하나만 묻는다</b>. 지우는 쪽이 되돌릴 수 없으므로
/// 그것이 먼저 사람 눈에 오고, 치우는 쪽은 그다음 기회에 묻는다 — 한 줄에 두 동작을 실으면
/// 사람이 무엇에 답했는지 흐려진다.
/// </summary>
struct OrphanSidecarPlan
{
    /// <summary>1단계로 옆으로 치울 것들이다. 이름만 바뀐다.</summary>
    std::vector<OrphanSidecar> toSetAside;
    /// <summary>2단계로 지울 것들이다. 이미 치워져 있었고 여전히 주인이 없다.</summary>
    std::vector<OrphanSidecar> toDelete;
    /// <summary>
    /// 주인은 없지만 장면이 그 정체성을 가리키고 있어 손대지 않는 것들이다. 고아가 아니라
    /// 「파일이 없는 에셋」이며, 이 사이드카가 그 사실을 아는 마지막 자리다.
    /// </summary>
    std::vector<OrphanSidecar> keptForReferences;

    /// <summary>지금 물을 것이 지우는 쪽인지다. 되돌릴 수 없는 쪽이 언제나 먼저다.</summary>
    [[nodiscard]] bool IsAskingToDelete() const { return !toDelete.empty(); }
    [[nodiscard]] bool HasSomethingToAsk() const
    {
        return !toDelete.empty() || !toSetAside.empty();
    }

    /// <summary>툴바 확인 줄에 놓을 한 줄이다. 이번에 묻는 한 단계만 말한다.</summary>
    [[nodiscard]] std::string Describe() const;
};

/// <summary>
/// 사이드카 파일 이름에서 그 주인이었을 에셋의 경로를 얻는다.
///
/// <c>Textures/Albedo.png.meta</c>는 <c>Textures/Albedo.png</c>를, 치워 둔
/// <c>Textures/Albedo.png.meta.orphan</c>도 같은 것을 답한다. 치워 둔 파일의 주인이 돌아왔는지를
/// 이것으로 묻는다 — <c>.orphan</c>을 사이드카 이름으로 인정해 스캔에게 묻는 길도 있지만, 그러면
/// 스캔이 치워 둔 파일을 다시 읽게 되어 치운 의미가 없어진다.
/// </summary>
/// <returns>주인의 경로이며, 사이드카 이름이 아니면 비어 있다.</returns>
[[nodiscard]] std::filesystem::path SidecarOwnerPath(const std::filesystem::path& sidecarPath);

/// <summary>이 경로가 옆으로 치워 둔 사이드카의 이름인지다.</summary>
[[nodiscard]] bool IsSetAsideSidecar(const std::filesystem::path& path);

}
