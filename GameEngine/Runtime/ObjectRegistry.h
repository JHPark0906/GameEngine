#pragma once

#include <unordered_map>

namespace GameEngine::Runtime
{

class Object;

/// <summary>런타임 객체에 고유 ID를 발급하고 등록 상태를 추적한다.</summary>
class ObjectRegistry final
{
public:
    /// <summary>객체를 등록하고 새 인스턴스 ID를 발급한다.</summary>
    /// <param name="object">등록할 객체의 비소유 포인터이다.</param>
    /// <returns>객체에 할당된 고유 인스턴스 ID이다.</returns>
    [[nodiscard]] unsigned int RegisterObject(Object* object);

    /// <summary>지정한 인스턴스 ID의 객체를 등록 해제한다.</summary>
    /// <param name="instanceId">등록 해제할 객체의 인스턴스 ID이다.</param>
    void UnregisterObject(unsigned int instanceId);

    /// <summary>인스턴스 ID에 해당하는 살아 있는 객체를 찾는다.</summary>
    [[nodiscard]] Object* FindObject(unsigned int instanceId) const;

private:
    unsigned int mNextInstanceId = 0;
    std::unordered_map<unsigned int, Object*> mObjects;
};

}
