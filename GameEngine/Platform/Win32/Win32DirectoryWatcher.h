#pragma once

#include <filesystem>
#include <memory>

#include "../IDirectoryWatcher.h"

namespace GameEngine::Platform::Win32
{

/// <summary>
/// Win32의 디렉터리 감시다. ReadDirectoryChangesW를 겹침(overlapped) 방식으로 걸어 두고, 매
/// 폴링에서 기다리지 않고 완료됐는지만 본다. 하위 디렉터리까지 지켜본다.
/// </summary>
class Win32DirectoryWatcher final : public IDirectoryWatcher
{
public:
    /// <summary>이 디렉터리를 지켜보기 시작한다. 열지 못하면 IsValid가 거짓이다.</summary>
    explicit Win32DirectoryWatcher(const std::filesystem::path& directory);
    ~Win32DirectoryWatcher() override;

    [[nodiscard]] bool IsValid() const override;
    [[nodiscard]] bool PollChanges() override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
