#include "pch.h"
#include "ComponentFactory.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace GameEngine::Serialization
{

namespace
{
    struct FactoryTable
    {
        std::unordered_map<std::string, ComponentFactory::Factory> factories;
        std::mutex mutex;
    };

    /// <summary>
    /// 처음 쓰일 때 만들어지는 표이다.
    ///
    /// 네임스페이스 범위 객체여서는 안 된다: 프로젝트는 정적 초기화에서 자기 컴포넌트 타입을
    /// 등록하는데, 번역 단위들의 초기화 실행 순서는 정해져 있지 않아서, 그런 등록이 아직
    /// 만들어지지 않은 표에 닿을 수 있다. 함수 지역 static은 그것이 언제든 첫 호출이 만든다.
    /// </summary>
    [[nodiscard]] FactoryTable& GetFactoryTable()
    {
        static FactoryTable table;
        return table;
    }
}

bool ComponentFactory::Register(std::string type, Factory factory)
{
    if (type.empty() || !factory)
    {
        return false;
    }

    FactoryTable& table = GetFactoryTable();
    std::lock_guard lock(table.mutex);
    return table.factories.emplace(std::move(type), std::move(factory)).second;
}

ComponentFactory::Registrations ComponentFactory::Snapshot()
{
    FactoryTable& table = GetFactoryTable();
    const std::lock_guard lock(table.mutex);
    Registrations registrations;
    registrations.reserve(table.factories.size());
    for (const auto& [type, factory] : table.factories)
    {
        registrations.push_back({ type, factory });
    }
    return registrations;
}

void ComponentFactory::Restore(const Registrations& registrations)
{
    FactoryTable& table = GetFactoryTable();
    const std::lock_guard lock(table.mutex);
    table.factories.clear();
    for (const auto& [type, factory] : registrations)
    {
        table.factories.emplace(type, factory);
    }
}

bool ComponentFactory::Unregister(const std::string_view type)
{
    FactoryTable& table = GetFactoryTable();
    std::lock_guard lock(table.mutex);
    return table.factories.erase(std::string(type)) != 0;
}

bool ComponentFactory::IsRegistered(const std::string_view type)
{
    FactoryTable& table = GetFactoryTable();
    std::lock_guard lock(table.mutex);
    return table.factories.contains(std::string(type));
}

bool ComponentFactory::Create(
    const std::string_view type,
    const Core::Json& json,
    Runtime::GameObject& gameObject)
{
    Factory factory;
    {
        FactoryTable& table = GetFactoryTable();
        std::lock_guard lock(table.mutex);
        const auto iterator = table.factories.find(std::string(type));
        if (iterator == table.factories.end())
        {
            return false;
        }
        factory = iterator->second;
    }
    return factory(json, gameObject);
}

}
