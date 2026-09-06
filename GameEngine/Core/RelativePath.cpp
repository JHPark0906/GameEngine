#include "pch.h"
#include "RelativePath.h"

#include <algorithm>
#include <system_error>

namespace GameEngine::Core
{

bool EscapesRoot(const std::filesystem::path& relativePath)
{
    return relativePath.is_absolute() ||
        std::ranges::any_of(
            relativePath,
            [](const std::filesystem::path& part)
            {
                return part == "..";
            });
}

std::optional<std::filesystem::path> RelativePathWithin(
    const std::filesystem::path& root, const std::filesystem::path& path)
{
    if (path.empty())
    {
        return std::nullopt;
    }

    std::filesystem::path relativePath = path;
    if (path.is_absolute())
    {
        if (root.empty())
        {
            // 옮길 기준이 없다. 절대 경로를 무엇에 대해 상대로 만들지 답할 수 없으므로, 여기서
            // 무언가를 지어내는 대신 답하지 않는다.
            return std::nullopt;
        }
        std::error_code error;
        const std::filesystem::path absolutePath = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            return std::nullopt;
        }
        relativePath = absolutePath.lexically_relative(root.lexically_normal());
    }

    // 먼저 다듬고 나서 묻는다. 다듬기 전의 Scenes/../.. 는 첫 요소가 .. 이 아니어서, 순서가
    // 반대면 밖을 가리키는 경로가 검사를 지나간다.
    relativePath = relativePath.lexically_normal();
    if (relativePath.empty() || relativePath == "." || EscapesRoot(relativePath))
    {
        return std::nullopt;
    }
    // 슬래시 형식으로 돌려준다. 이 값이 곧 파일에 적히거나 조회 키가 되므로, 같은 경로가 두
    // 글자로 갈라지면 그 둘은 같은 것을 가리키면서 서로 다른 키가 된다.
    return std::filesystem::path(relativePath.generic_wstring());
}

}
