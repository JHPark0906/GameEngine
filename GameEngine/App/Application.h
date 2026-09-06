#pragma once

#include <memory>

#include "ProjectSettings.h"

namespace GameEngine::App
{

class IGameBootstrap;

/// <summary>
/// 플레이어 창과 엔진 루프의 수명을 관리하는 애플리케이션 진입 객체이다.
/// </summary>
class Application final
{
public:
    /// <summary>애플리케이션 실행에 필요한 플랫폼 및 렌더링 객체를 인수한다.</summary>
    /// <param name="projectSettings">창, 장면 및 렌더링 설정이다.</param>
    explicit Application(ProjectSettings projectSettings);
    Application(
        ProjectSettings projectSettings,
        std::unique_ptr<IGameBootstrap> gameBootstrap);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /// <summary>창과 엔진 루프를 초기화한다.</summary>
    /// <returns>모든 하위 시스템이 정상적으로 초기화되었으면 true이다.</returns>
    [[nodiscard]] bool Initialize();

    /// <summary>메시지 처리와 엔진 프레임 실행을 종료 요청까지 반복한다.</summary>
    /// <returns>플랫폼 메시지 루프의 종료 코드이다.</returns>
    [[nodiscard]] int Run();

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

}
