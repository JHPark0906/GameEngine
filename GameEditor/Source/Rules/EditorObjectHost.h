#pragma once

// editor-layer: 0 (Rules)

namespace GameEngine::Runtime
{
class Component;
class GameObject;
class Scene;
}

namespace GameEditor
{

/// <summary>
/// 편집 명령이 편집기에게 묻는 것 전부다. 넷뿐이다: 잡아 둔 id로 오브젝트나 컴포넌트를 찾고,
/// 되살아난 객체의 새 id를 옛 id에 잇고, 계층의 선택을 옮기고, 편집 중인 장면을 얻는 것.
///
/// 넷이 한 얼굴인 이유는 <b>한 수명에 매여 있기 때문</b>이다 — 넷 다 「지금 열려 있는 문서」가
/// 없으면 뜻이 없다. 둘로 자르면 명령이 인자를 둘 받을 뿐 끊기는 것이 없다.
///
/// <b>id를 잇는 일이 여기 있는 이유.</b> 되돌리기가 객체를 지웠다 다시 만들면 그 객체는 새
/// id로 돌아온다. 명령은 대상을 id로 잡아 두므로, 이어 주지 않으면 그다음 되돌리기가 이미 없는
/// id를 가리키고 조용히 아무 일도 하지 않는다 — 오류도 로그도 없이 「눌렀는데 아무 일도
/// 안 일어남」으로만 보인다.
///
/// 명령의 의존성을 이 인터페이스로 제한해 프로젝트, 런타임, 장면 전체 없이도 시험한다.
/// 찾기는 <c>Component</c>와 <c>GameObject</c>로 나누어 호출자가 종류를 캐스트하지 않는다.
/// </summary>
class IEditorObjectHost
{
public:
    virtual ~IEditorObjectHost() = default;

    /// <summary>그 id의 컴포넌트다. 별칭을 따라간 뒤 찾으며, 없거나 종류가 다르면 null이다.</summary>
    [[nodiscard]] virtual GameEngine::Runtime::Component* FindComponent(unsigned int instanceId) = 0;

    /// <summary>그 id의 오브젝트다. 별칭을 따라간 뒤 찾으며, 없거나 종류가 다르면 null이다.</summary>
    [[nodiscard]] virtual GameEngine::Runtime::GameObject* FindGameObject(
        unsigned int instanceId) = 0;

    /// <summary>옛 id가 이제 새 id라고 적는다. 되살린 객체가 자기 커맨드와 다시 이어진다.</summary>
    virtual void RecordObjectIdAlias(unsigned int oldId, unsigned int newId) = 0;

    /// <summary>계층에서 그 오브젝트를 고른 것으로 한다. 0이면 고른 것이 없다.</summary>
    virtual void SelectObject(unsigned int instanceId) = 0;

    /// <summary>편집 중인 장면이다. 열린 것이 없으면 null이다.</summary>
    [[nodiscard]] virtual GameEngine::Runtime::Scene* GetOpenScene() const = 0;
};

}
