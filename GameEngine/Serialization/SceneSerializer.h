#pragma once

#include "../Runtime/SceneLoader.h"


#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace GameEngine::Core
{
class Json;
}

namespace GameEngine::Runtime
{
class Component;
class GameObject;
class ObjectRegistry;
class RuntimeContext;
class Scene;
}

namespace GameEngine::Serialization
{

/// <summary>장면 파일을 런타임 장면 객체로 역직렬화한다.</summary>
class SceneSerializer final
{
public:
    /// <summary>지정한 파일에서 장면과 게임 오브젝트를 불러온다.</summary>
    /// <param name="sceneFile">불러올 장면 파일의 경로이다.</param>
    /// <returns>불러온 장면이며, 파일을 열거나 해석하지 못하면 nullptr이다.</returns>
    /// <summary>
    /// 장면 파일을 읽어, 객체들이 주어진 레지스트리에 등록되는 새 Scene을 만든다. 레지스트리가
    /// 매개변수인 이유는 역직렬화된 객체도 여느 런타임 객체와 같고, 물러나 기댈 주변의
    /// 레지스트리 같은 것이 없기 때문이다.
    /// </summary>
    /// <summary>런타임에 끼울 장면 로더다. LoadFromBytes를 감싼다.</summary>
    [[nodiscard]] static Runtime::SceneLoader MakeSceneLoader();

    [[nodiscard]] static std::unique_ptr<Runtime::Scene> LoadFromBytes(
        std::span<const std::byte> sceneBytes,
        const std::filesystem::path& sceneFile,
        Runtime::RuntimeContext& runtimeContext);

    /// <summary>
    /// 장면을 LoadFromBytes가 읽는 형식 그대로의 JSON 텍스트로 직렬화한다.
    ///
    /// 파일에서 읽은 오브젝트는 그때 읽은 id를 그대로 돌려받는다(<see cref="Runtime::GameObject::GetSerializedId"/>).
    /// 새로 만든 오브젝트에만 새 번호를 매긴다. 출력도 이 번호로 정렬하므로,
    /// 실행 중 인스턴스 id가 달라져도 저장된 id와 parent 및 출력 순서를 유지한다.
    ///
    /// 엔진이 직렬화를 아는 컴포넌트만 쓰인다. 프로젝트가 정의한 MonoBehaviour처럼 엔진이
    /// 속성을 알 수 없는 컴포넌트는 경고와 함께 건너뛴다 — 조용히 잃는 것이 아니라 로그에
    /// 남긴다.
    /// </summary>
    [[nodiscard]] static std::string SaveToText(const Runtime::Scene& scene);

    /// <summary>
    /// 컴포넌트 하나를, 장면 파일 안에 놓이는 것과 똑같은 JSON으로 쓴다.
    ///
    /// "컴포넌트의 상태"의 정의가 저장소에 하나만 있게 하는 자리다: 장면 저장이 이것을 쓰고,
    /// 에디터의 undo 스냅숏도 같은 것을 뜬다. 스냅숏이 자기만의 표현을 따로 세우면, 컴포넌트가
    /// 상태를 하나 늘릴 때마다 두 곳이 따로 갱신되어야 하고 잊은 쪽이 조용히 상태를 잃는다.
    ///
    /// 엔진이 속성으로 기술할 수 없는 컴포넌트 — 프로젝트가 정의했고 이 프로세스에 서술이 없는
    /// 타입 — 이면 값이 없다. 보존된 컴포넌트는 쥐고 있던 JSON 그대로가 결과다.
    /// </summary>
    [[nodiscard]] static std::optional<Core::Json> SaveComponentToJson(
        const Runtime::Component& component);

    /// <summary>
    /// <see cref="SaveComponentToJson"/>이 쓴 컴포넌트 JSON을 게임 오브젝트에 되세운다.
    ///
    /// 장면 로드가 컴포넌트 하나를 만드는 바로 그 길이며, 에디터 undo의 복원도 이 길을 쓴다.
    /// 팩토리가 없는 타입은 잃는 대신 보존 컴포넌트로 실려 붙는다.
    /// </summary>
    /// <param name="componentJson">"type"을 포함한 컴포넌트 JSON 객체다.</param>
    /// <param name="gameObject">컴포넌트를 받을 게임 오브젝트다.</param>
    /// <returns>
    /// 붙은 컴포넌트이며, 만들 수 없으면 nullptr이다. Transform은 객체의 일부라 새로 붙지 않고
    /// 이미 있는 것이 채워지므로, 그 경우 반환은 그 Transform이다.
    /// </returns>
    static Runtime::Component* LoadComponentIntoGameObject(
        const Core::Json& componentJson, Runtime::GameObject& gameObject);
};

}
