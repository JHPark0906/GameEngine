// editor-layer: 3 (Shell)

#include "Shell/EditorBootstrap.h"
#include "App/GameBootstrapRegistry.h"

#include <memory>

namespace GameEditor
{

namespace
{
    std::unique_ptr<GameEngine::App::IGameBootstrap> CreateEditorBootstrap()
    {
        return std::make_unique<EditorBootstrap>();
    }

    /// <summary>
    /// 에디터는 게임 프로젝트다: 진입점이 없고, 정적 초기화에서 자기 bootstrap을 등록한다 —
    /// SampleGame의 컴포넌트가 팩토리를 등록하는 것과 같은 방식이다. 플레이어 진입점이 이
    /// 실행 파일 옆의 GameEditor.gameproject를 열고, 등록된 bootstrap을 그 위에 세운다.
    /// </summary>
    const bool gEditorBootstrapRegistered =
        GameEngine::App::GameBootstrapRegistry::Register(&CreateEditorBootstrap);
}

}
