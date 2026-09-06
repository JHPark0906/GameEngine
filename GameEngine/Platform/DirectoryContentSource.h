#pragma once

#include "IContentSource.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>
/// 디렉터리에 흩어진 파일들이다. 작업 중인 프로젝트가 읽히는 방식이자, 에디터가 연 프로젝트를
/// 읽는 방식이다.
///
/// 루트는 출발점이 아니라 경계다: 루트 밖으로 해석되는 경로는 읽는 대신 거부한다. 장면 파일은
/// 자기가 쓰는 에셋을 이름으로 가리키고, 장면은 콘텐츠다 — 콘텐츠가 `../../../etc/passwd`를
/// 가리켰다고 엔진이 그것을 열어 줄 수는 없다.
/// </summary>
class DirectoryContentSource final : public IContentSource
{
public:
    explicit DirectoryContentSource(std::filesystem::path rootPath);

    /// <summary>루트가 존재하고 디렉터리인지 여부이다.</summary>
    [[nodiscard]] bool IsValid() const { return mIsValid; }

    [[nodiscard]] const std::filesystem::path& GetRootPath() const { return mRootPath; }

    [[nodiscard]] bool Exists(const std::filesystem::path& relativePath) const override;
    [[nodiscard]] bool Read(
        const std::filesystem::path& relativePath, std::vector<std::byte>& bytes) const override;
    [[nodiscard]] std::vector<std::filesystem::path> List() const override;
    [[nodiscard]] std::filesystem::path GetDescription() const override { return mRootPath; }

    /// <summary>상대 경로가 가리키는 절대 경로이다. 루트를 벗어나면 비어 있다.</summary>
    [[nodiscard]] std::filesystem::path ResolveFilePath(
        const std::filesystem::path& relativePath) const override;

private:
    std::filesystem::path mRootPath;
    bool mIsValid = false;
};

}
