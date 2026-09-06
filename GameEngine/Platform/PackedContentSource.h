#pragma once


#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "IContentSource.h"
#include "IFileMapping.h"

namespace GameEngine::Platform
{

/// <summary>
/// 자기를 실행하는 파일 안에 저장된 프로젝트이다.
///
/// 팩이 덧붙지 않은 실행 파일 위에 이것을 만드는 것은 오류가 아니다: 스스로 무효라고 보고하고
/// 애플리케이션은 옆의 파일들을 읽는 쪽으로 물러선다. 그 대체 경로가 개발 이야기의 전부다 —
/// 텍스처를 고치고 다시 실행, 패킹 단계 없음 — 그리고 팩이 기존 질문의 대체가 아니라 두 번째
/// 답인 이유이기도 하다.
///
/// 파일은 핸들로 읽는 대신 매핑되므로, 프로젝트의 어느 부분이 상주하는지는 운영 체제의 결정이다:
/// 페이지는 무언가 건드릴 때 도착하고 압박이 오면 도로 내려간다. 그 안에서 파일 하나를 읽는
/// 것은 seek와 read가 아니라 메모리에서의 복사이고, 같은 파일을 두 번 읽으면 두 번째는 비용이
/// 없다.
/// </summary>
class PackedContentSource final : public IContentSource
{
public:
    explicit PackedContentSource(std::filesystem::path executablePath);

    /// <summary>파일에 정말 팩이 덧붙어 있었고 그 인덱스를 읽을 수 있었는지 여부이다.</summary>
    [[nodiscard]] bool IsValid() const { return mIsValid; }

    [[nodiscard]] bool Exists(const std::filesystem::path& relativePath) const override;
    [[nodiscard]] bool Read(
        const std::filesystem::path& relativePath, std::vector<std::byte>& bytes) const override;
    [[nodiscard]] std::vector<std::filesystem::path> List() const override;
    [[nodiscard]] std::filesystem::path GetDescription() const override { return mExecutablePath; }

    /// <summary>
    /// 항상 비어 있다. packed 파일은 파일이 아니라 실행 파일의 한 영역이고, 아닌 척하면 실행
    /// 파일 자신을 여는 경로를 내주게 된다.
    /// </summary>
    [[nodiscard]] std::filesystem::path ResolveFilePath(const std::filesystem::path&) const override
    {
        return {};
    }

private:
    struct Entry
    {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    [[nodiscard]] bool ReadIndex();

    /// <summary>팩의 바이트이다. 파일을 매핑할 수 없었으면 비어 있다.</summary>
    [[nodiscard]] std::span<const std::byte> GetPackBytes() const;

    std::filesystem::path mExecutablePath;
    std::unique_ptr<IFileMapping> mMapping;
    /// <summary>파일 안에서 팩이 시작하는 곳이다. 항목 오프셋이 팩 기준 상대로 남게 한다.</summary>
    std::uint64_t mPackOffset = 0;
    std::unordered_map<std::string, Entry> mEntries;
    /// <summary>쓰인 그대로의 경로들이다. 인덱스 순이라, 목록은 빌드가 만든 그대로다.</summary>
    std::vector<std::filesystem::path> mPaths;
    bool mIsValid = false;
};

}
