#pragma once

#include <cstddef>
#include <span>

namespace GameEngine::Platform
{

/// <summary>
/// 핸들로 읽는 대신 메모리로서 존재하는 파일의 바이트이다.
///
/// 매핑의 어느 페이지가 실제로 상주하는지는 운영 체제가 정하고 압박이 오면 도로 내릴 수 있으므로,
/// 큰 파일은 무언가 건드리기 전까지 메모리가 아니라 주소 공간만 차지한다. 게임 자신의 콘텐츠가
/// 원하는 성질이 바로 그것이다: 실행 파일에 덧붙은 프로젝트는 한 번 매핑되고, 아무것도 로드하지
/// 않은 부분은 결코 아무것도 차지하지 않는다.
///
/// 바이트는 이 객체가 사는 동안만 유효하며 그 이상은 아니다.
/// </summary>
class IFileMapping
{
public:
    virtual ~IFileMapping() = default;

    IFileMapping(const IFileMapping&) = delete;
    IFileMapping& operator=(const IFileMapping&) = delete;
    IFileMapping(IFileMapping&&) = delete;
    IFileMapping& operator=(IFileMapping&&) = delete;

    /// <summary>파일 전체이다. 매핑을 만들 수 없었으면 비어 있다.</summary>
    [[nodiscard]] virtual std::span<const std::byte> GetBytes() const = 0;

protected:
    IFileMapping() = default;
};

}
