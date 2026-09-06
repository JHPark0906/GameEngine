#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace GameEngine::Platform
{

/// <summary>
/// 애플리케이션이 함께 배포하는 파일들이 오는 곳이다.
///
/// 장면, 모델, 이미지, 프로젝트 서술자, 셰이더를 경로로 직접 열지 않고 소스에 바이트로 요청한다.
/// 소스는 배포 디렉터리 또는 실행 파일 뒤에 덧붙은 콘텐츠 팩을 읽을 수 있다.
/// 플랫폼 이미지 디코더도 파일 경로 대신 인코딩된 이미지 바이트를 받는다.
///
/// 경로는 항상 애플리케이션의 배포 루트 기준 상대 경로이고 슬래시를 쓴다.
/// 디렉터리와 팩에서 같은 경로는 같은 파일을 뜻한다.
/// </summary>
class IContentSource
{
public:
    virtual ~IContentSource() = default;

    IContentSource(const IContentSource&) = delete;
    IContentSource& operator=(const IContentSource&) = delete;
    IContentSource(IContentSource&&) = delete;
    IContentSource& operator=(IContentSource&&) = delete;

    /// <summary>이 배포 상대 경로에 파일이 있는지 여부이다.</summary>
    [[nodiscard]] virtual bool Exists(const std::filesystem::path& relativePath) const = 0;

    /// <summary>
    /// 파일 전체를 읽는다. 파일이 없거나 읽을 수 없으면 false를 반환하고 bytes를 비워 둔다.
    /// 호출자가 작업 문맥을 알기 때문에 실패 진단은 호출자의 몫이다.
    /// 리더는 파싱 전에 파일 전체를 메모리에 올리며, 이 인터페이스는 스트리밍을 제공하지 않는다.
    /// </summary>
    [[nodiscard]] virtual bool Read(
        const std::filesystem::path& relativePath, std::vector<std::byte>& bytes) const = 0;

    /// <summary>
    /// 이 소스가 담은 모든 파일이다. 결정적 순서다. 에셋 데이터베이스가 프로젝트에 무엇이
    /// 있는지 알아내려고 이것을 순회하므로, 실행마다 바뀌는 순서는 데이터베이스를 재배열하고
    /// 그와 함께 sub-asset을 가리키는 로컬 id들까지 재배열했을 것이다.
    /// </summary>
    [[nodiscard]] virtual std::vector<std::filesystem::path> List() const = 0;

    /// <summary>
    /// 이 소스가 읽는 곳이다. 진단에 쓰인다. 디렉터리는 자기 경로를, 팩은 자기가 속한 실행
    /// 파일을 보고한다.
    /// </summary>
    [[nodiscard]] virtual std::filesystem::path GetDescription() const = 0;

    /// <summary>
    /// 이 경로가 가리키는 디스크 위의 실제 파일이다. 없으면 비어 있다 — 팩이 주는 답이 그것인데,
    /// 팩의 파일은 파일이 아니기 때문이다.
    ///
    /// 엔진 밖의 무언가에 경로를 건네야만 하는 소수의 호출자를 위한 것이다: 파일을 복사하는
    /// 빌드 도구, 스스로 파일을 여는 폰트 API. 런타임의 어느 것도 에셋에 실제 경로가 있기를
    /// 요구해서는 안 된다. packed 빌드에서는 어느 에셋에도 실제 경로가 없기 때문이다. 소스의
    /// 종류를 검사하는 대신 인터페이스를 통해 묻는 것이, 그 요구를 답이 있는 자리에 명시된
    /// 채로 유지한다.
    /// </summary>
    [[nodiscard]] virtual std::filesystem::path ResolveFilePath(
        const std::filesystem::path& relativePath) const = 0;

protected:
    IContentSource() = default;
};

}
