#pragma once

#include <memory>

#include "IGameBootstrap.h"

namespace GameEngine::App
{

/// <summary>
/// 프로젝트가 자기 bootstrap을 엔진에 알리는 자리다.
///
/// 게임 프로젝트에는 진입점이 없다: 플레이어 진입점은 엔진의 것이고, 프로젝트는 콘텐츠와 C++
/// 컴포넌트를 기여할 뿐이다. 컴포넌트가 정적 초기화에서 ComponentFactory에 자신을 등록하듯,
/// 프로젝트가 엔진 수명 주기에 끼어들고 싶으면 — 에디터가 그렇다: 캡처 뷰와 UI 프론트엔드를
/// 원한다 — 같은 방식으로 bootstrap 팩토리를 등록한다. 그래서 에디터는 다른 어떤 게임과도
/// 같은 모양의 프로젝트이고, 엔진은 "에디터"라는 특별한 경우를 모른다.
///
/// 하나의 실행 파일은 하나의 게임이므로 자리는 하나다. 두 번째 등록은 거절되고 로그로 말한다.
/// </summary>
class GameBootstrapRegistry final
{
public:
    using Factory = std::unique_ptr<IGameBootstrap> (*)();

    /// <summary>프로젝트의 bootstrap 팩토리를 등록한다. 정적 초기화에서 부르도록 만들어졌다.</summary>
    /// <returns>등록됐으면 true이다. 이미 다른 팩토리가 있으면 false다.</returns>
    [[nodiscard]] static bool Register(Factory factory);

    /// <summary>등록된 팩토리로 bootstrap을 만든다. 등록된 것이 없으면 null이다 — 보통의 게임이다.</summary>
    [[nodiscard]] static std::unique_ptr<IGameBootstrap> Create();

    /// <summary>등록을 지운다. 테스트가 서로를 오염시키지 않게 하는 용도다.</summary>
    static void Reset();
};

}
