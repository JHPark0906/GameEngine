#pragma once

#include <string>
#include <utility>

namespace GameEngine::Runtime
{

class ObjectRegistry;
class GameObject;

/// <summary>
/// 런타임 객체에 고유 인스턴스 ID와 레지스트리 수명 연동을 제공한다.
///
/// 레지스트리는 프로세스 전역에서 읽는 대신 인자로 건네진다. 그래서 런타임 객체는 살아 있는
/// Game 없이도 홀로 만들어 시험할 수 있고, 두 Game이 한 프로세스에 공존할 수 있다.
/// </summary>
class Object
{
public:
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    /// <summary>객체에 할당된 고유 인스턴스 ID를 반환한다.</summary>
    /// <returns>현재 런타임 레지스트리에서 유일한 인스턴스 ID이다.</returns>
    [[nodiscard]] unsigned int GetInstanceId() const { return mInstanceId; }

    /// <summary>객체 이름을 반환한다.</summary>
    [[nodiscard]] const std::string& GetName() const { return mName; }

    /// <summary>객체 이름을 변경한다.</summary>
    void SetName(std::string name) { mName = std::move(name); }

protected:
    /// <summary>주어진 레지스트리에 등록하고 그로부터 인스턴스 id를 받는다.</summary>
    explicit Object(ObjectRegistry& objectRegistry, std::string name = {});

    /// <summary>
    /// 소유자가 등록할 때까지 객체를 미등록 상태로 둔다. Component가 이 길을 밟는다: 언제나
    /// GameObject::AddComponent를 통해 만들어지고, 그것이 자기 GameObject가 이미 속한
    /// 레지스트리에 등록해 주므로, 컴포넌트 생성자에는 레지스트리 인자가 필요 없고 프로젝트가
    /// 정의한 컴포넌트도 영향받지 않는다.
    /// </summary>
    explicit Object(std::string name = {});

    /// <summary>이 객체가 속한 레지스트리이다. 객체가 등록된 뒤에만 유효하다.</summary>
    [[nodiscard]] ObjectRegistry& GetObjectRegistry() const;

private:
    friend class GameObject;

    /// <summary>미등록 상태로 만들어진 객체를 등록한다.</summary>
    void RegisterWith(ObjectRegistry& objectRegistry);

    unsigned int mInstanceId = 0;
    std::string mName;

    /// <summary>
    /// 이 객체의 id를 발급한 레지스트리이다. 파괴 시점에 설치된 레지스트리와 다를 수 있다.
    /// id는 레지스트리마다 독립적으로 발급하므로, 등록 해제는 반드시 발급한 레지스트리에 해야
    /// 다른 Game의 같은 id를 가진 객체를 지우지 않는다.
    /// </summary>
    ObjectRegistry* mObjectRegistry = nullptr;
};

}
