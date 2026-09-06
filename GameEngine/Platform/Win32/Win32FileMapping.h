#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include "../IFileMapping.h"

namespace GameEngine::Platform::Win32
{

/// <summary>Win32 섹션 API로 파일을 읽기 전용 매핑한다.</summary>
class Win32FileMapping final : public IFileMapping
{
public:
    explicit Win32FileMapping(const std::filesystem::path& path);
    ~Win32FileMapping() override;

    [[nodiscard]] std::span<const std::byte> GetBytes() const override { return mBytes; }

private:
    std::span<const std::byte> mBytes;
};

}
