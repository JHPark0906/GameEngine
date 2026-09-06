#pragma once

// editor-layer: 1 (Document)

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Core/Json.h"
#include "Core/UndoStack.h"
#include "Math/Vector.h"
#include "Runtime/PropertyDescriptor.h"

namespace GameEngine::Runtime
{
class Component;
class GameObject;
}

namespace GameEditor
{

class IEditorObjectHost;

// 에디터의 구체 undo 커맨드들이다. 스택(Core::UndoStack)은 에디터 중립이라 엔진에 살고, 여기의
// 커맨드들은 Runtime 타입과 편집기의 호스트를 알므로 에디터에 산다.
//
// 대상은 모두 인스턴스 id로 잡고 실행 시점에 호스트에게 물어 해석한다. 객체를
// 재생성하는 커맨드 — 삭제의 undo, 생성/추가/인스턴스화의 redo — 는 재생성된 객체의 새 id를
// 호스트의 별칭 표에 등록할 뿐, 자기와 다른 커맨드들의 저장된 id는 건드리지 않는다:
// FindObject가 해석 전에 별칭 사슬을 따라가므로, 커맨드는 처음 잡은 id를 영원히 쥐고 있으면
// 된다. 스택과 별칭 맵은 열린 장면이 바뀌는 순간 — 장면/프로젝트 열기, Play 왕복 — 함께
// 비워지므로, 커맨드가 사는 동안 "열린 장면"은 기록 시점의 그 장면이다.
//
// 해석에 실패한 커맨드는 false를 반환해 스택이 버리게 하되, 그 전에 무엇이 왜 해석되지 않았는지
// 로그로 말한다 — "아무 일도 일어나지 않는 undo"가 침묵이 되지 않게.

/// <summary>
/// 컴포넌트 하나의 복원 스냅숏이다. 내용은 장면 파일과 같은 컴포넌트 JSON이다.
/// 직렬화와 같은 표현을 써야 속성뿐 아니라 타일맵 격자처럼 파일에 실리는 상태도 복원된다.
/// </summary>
struct ComponentSnapshot
{
    /// <summary>진단 메시지에 쓰는 타입 이름이다. 보존 컴포넌트는 보존된 원래 타입 이름이다.</summary>
    std::string typeName;
    /// <summary>"type"을 포함한 컴포넌트 JSON이다. 이것이 이 컴포넌트 상태의 전부다.</summary>
    GameEngine::Core::Json json;
    /// <summary>
    /// JSON을 뜰 수 있었는지 여부다. 엔진이 직렬화를 모르는 컴포넌트 — 이 프로세스에 속성
    /// 서술이 없는 프로젝트 타입 — 는 스냅숏을 뜰 수 없고, 그 복원은 실패로 로그에 남는다.
    /// </summary>
    bool valid = false;
};

/// <summary>
/// GameObject 하나의 복원 스냅숏이다. 컴포넌트 id들을 함께 기록하는 이유는 복원 후 그 id들의
/// 별칭을 등록해, 그 id를 참조하던 커맨드들 — 속성 편집 — 이 계속 대상을 찾게 하기 위해서다.
/// </summary>
struct GameObjectSnapshot
{
    unsigned int id = 0;
    /// <summary>부모 GameObject의 인스턴스 id다. 루트면 0이다.</summary>
    unsigned int parentId = 0;
    std::string name;
    bool isActive = true;
    /// <summary>(스냅숏 시점의 컴포넌트 id, 스냅숏) 쌍들이다. Transform도 포함된다.</summary>
    std::vector<std::pair<unsigned int, ComponentSnapshot>> components;
    /// <summary>부모 안에서 차지하는 위치다. 루트이면 0이다.</summary>
    std::size_t siblingIndex = 0;
};

/// <summary>컴포넌트의 현재 상태를 스냅숏으로 뜬다.</summary>
[[nodiscard]] ComponentSnapshot SnapshotComponent(const GameEngine::Runtime::Component& component);

/// <summary>
/// 인스펙터 속성 편집 하나다: (컴포넌트 id, 속성 이름, 이전 값, 새 값).
///
/// mergeKey가 0이 아니고 같은 대상·속성·키의 편집이 연이어 기록되면 하나로 흡수된다. 에디터는
/// 텍스트 필드가 포커스를 유지하는 동안의 타이핑에 같은 키를 주고, 포커스가 바뀌면 키를 바꾼다 —
/// 그래서 "12.5"를 치는 네 번의 적용이 한 번의 undo가 되고, 필드를 떠났다 돌아온 편집은 새
/// undo 단계가 된다.
/// </summary>
class PropertyEditCommand final : public GameEngine::Core::IEditCommand
{
public:
    PropertyEditCommand(
        IEditorObjectHost& host, unsigned int componentId, std::string propertyName,
        GameEngine::Runtime::PropertyValue before, GameEngine::Runtime::PropertyValue after,
        std::uint64_t mergeKey);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;
    [[nodiscard]] bool TryMerge(const GameEngine::Core::IEditCommand& next) override;

private:
    [[nodiscard]] bool SetValue(const GameEngine::Runtime::PropertyValue& value) const;

    IEditorObjectHost* mHost;
    unsigned int mComponentId;
    std::string mPropertyName;
    GameEngine::Runtime::PropertyValue mBefore;
    GameEngine::Runtime::PropertyValue mAfter;
    std::uint64_t mMergeKey;
};

/// <summary>GameObject 이름 변경이다. 속성 편집과 같은 규칙으로 타이핑이 흡수된다.</summary>
class GameObjectNameCommand final : public GameEngine::Core::IEditCommand
{
public:
    GameObjectNameCommand(
        IEditorObjectHost& host, unsigned int gameObjectId, std::string before, std::string after,
        std::uint64_t mergeKey);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;
    [[nodiscard]] bool TryMerge(const GameEngine::Core::IEditCommand& next) override;

private:
    [[nodiscard]] bool SetName(const std::string& name) const;

    IEditorObjectHost* mHost;
    unsigned int mGameObjectId;
    std::string mBefore;
    std::string mAfter;
    std::uint64_t mMergeKey;
};

/// <summary>GameObject 활성 상태 변경이다.</summary>
class GameObjectActiveCommand final : public GameEngine::Core::IEditCommand
{
public:
    GameObjectActiveCommand(IEditorObjectHost& host, unsigned int gameObjectId, bool after);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    [[nodiscard]] bool SetActive(bool isActive) const;

    IEditorObjectHost* mHost;
    unsigned int mGameObjectId;
    bool mAfter;
};

/// <summary>
/// Add Component다. 첫 Apply가 실제 추가를 하므로, 셸은 만들고 Apply가 성공하면 기록한다.
/// 재실행이 만든 컴포넌트의 새 id는 처음 id의 별칭으로 등록된다.
///
/// 추가는 장면 로드와 같은 길로 한다: 컴포넌트 JSON 하나를 만들어 로더에게 넘긴다. 그래서 이
/// 프로세스가 만들 줄 아는 타입이면 그 타입이 되고, 게임 프로젝트가 정의해 이 프로세스가 만들
/// 줄 모르는 타입이면 스키마의 기본값을 실은 보존 컴포넌트가 된다 — 에디터가 배치하고 값을
/// 넣을 수 있는 것이 그 데이터다.
/// </summary>
class AddComponentCommand final : public GameEngine::Core::IEditCommand
{
public:
    /// <summary>이 프로세스가 이름으로 만들 수 있는 컴포넌트를 기본값으로 추가한다.</summary>
    AddComponentCommand(IEditorObjectHost& host, unsigned int gameObjectId, std::string typeName);

    /// <summary>
    /// 주어진 컴포넌트 JSON을 그대로 추가한다. 게임 컴포넌트의 스키마 기본값이 이 길로 온다.
    /// </summary>
    /// <param name="prototype">"type"을 포함한 컴포넌트 JSON이다.</param>
    AddComponentCommand(
        IEditorObjectHost& host, unsigned int gameObjectId, std::string typeName,
        GameEngine::Core::Json prototype);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    IEditorObjectHost* mHost;
    unsigned int mGameObjectId;
    std::string mTypeName;
    /// <summary>추가될 컴포넌트의 JSON이다. 재실행도 같은 것을 다시 붙인다.</summary>
    GameEngine::Core::Json mPrototype;
    /// <summary>첫 Apply가 만든 컴포넌트의 id다. 재실행이 만든 것은 이 id의 별칭이 된다.</summary>
    unsigned int mComponentId = 0;
    /// <summary>이 추가가 함께 붙인 요구 컴포넌트들의 id다. 되돌리기가 함께 걷는다.</summary>
    std::vector<unsigned int> mRequirementIds;
};

/// <summary>
/// 보존 컴포넌트의 멤버 하나를 바꾸는 편집이다: (컴포넌트 id, 멤버 이름, 이전 값, 새 값).
///
/// 게임이 정의한 컴포넌트는 이 프로세스에 속성 서술이 없어서 <see cref="PropertyEditCommand"/>가
/// 잡을 것이 없다. 대신 이 커맨드가 그 컴포넌트가 쥔 JSON의 멤버를 직접 바꾼다 — 편집 대상이
/// 살아 있는 객체의 필드가 아니라 저장될 데이터라는 사실을 그대로 옮긴 것이다.
///
/// mergeKey는 속성 편집과 같은 규칙이다: 같은 필드에서 이어지는 타이핑이 한 단계가 된다.
/// </summary>
class PreservedPropertyEditCommand final : public GameEngine::Core::IEditCommand
{
public:
    PreservedPropertyEditCommand(
        IEditorObjectHost& host, unsigned int componentId, std::string memberName,
        GameEngine::Core::Json before, GameEngine::Core::Json after, std::uint64_t mergeKey);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;
    [[nodiscard]] bool TryMerge(const GameEngine::Core::IEditCommand& next) override;

private:
    [[nodiscard]] bool SetValue(const GameEngine::Core::Json& value) const;

    IEditorObjectHost* mHost;
    unsigned int mComponentId;
    std::string mMemberName;
    GameEngine::Core::Json mBefore;
    GameEngine::Core::Json mAfter;
    std::uint64_t mMergeKey;
};


/// <summary>
/// 컴포넌트 제거다. 생성 시점에 속성 스냅숏을 떠 두고, undo가 타입 이름으로 다시 만들어 스냅숏을
/// 부어 넣는다. 복원된 컴포넌트는 목록 끝에 붙는다 — GameObject에 위치 지정 추가가 없어서,
/// 컴포넌트 순서까지는 복원하지 않는다.
/// </summary>
class RemoveComponentCommand final : public GameEngine::Core::IEditCommand
{
public:
    RemoveComponentCommand(IEditorObjectHost& host, const GameEngine::Runtime::Component& component);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    IEditorObjectHost* mHost;
    unsigned int mGameObjectId;
    unsigned int mComponentId;
    ComponentSnapshot mSnapshot;
};

/// <summary>
/// 계층 창의 GameObject 생성이다. 첫 Apply가 실제 생성을 하고 만든 객체를 선택한다 — 재실행도
/// 같은 길이라 redo가 선택을 되살린다. 재실행이 만든 객체와 Transform의 새 id는 처음 id의
/// 별칭이 된다.
/// </summary>
class CreateGameObjectCommand final : public GameEngine::Core::IEditCommand
{
public:
    CreateGameObjectCommand(IEditorObjectHost& host, std::string name);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    IEditorObjectHost* mHost;
    std::string mName;
    unsigned int mGameObjectId = 0;
    unsigned int mTransformId = 0;
};

/// <summary>
/// GameObject 삭제다. 생성 시점에 자기와 자손 전체 — 이름, 활성, 부모, 컴포넌트 속성 — 를
/// 부모 우선 순서로 스냅숏해 두고, undo가 그 순서대로 다시 세운다. 복원은 전부-또는-없음이다:
/// 도중에 실패하면 이미 세운 것을 되지우고 별칭 없이 false를 반환해, 스택의 버림 정책과
/// 맞물린다. 부모의 자식 목록에서도 원래 위치로 돌아가 레이아웃과 UI 순서를 보존한다.
/// </summary>
class DeleteGameObjectCommand final : public GameEngine::Core::IEditCommand
{
public:
    DeleteGameObjectCommand(IEditorObjectHost& host, GameEngine::Runtime::GameObject& gameObject);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    IEditorObjectHost* mHost;
    /// <summary>삭제되는 부분 트리다. [0]이 뿌리이고, 부모가 자식보다 먼저다.</summary>
    std::vector<GameObjectSnapshot> mObjects;
};

/// <summary>
/// 콘텐츠 브라우저의 모델 인스턴스화다. 인스턴스화가 이미 일어난 뒤에 기록된다: 생성 시점에
/// 만들어진 부분 트리를 스냅숏하고, undo가 그것을 지우며, redo는 모델을 다시 임포트하는 대신
/// 스냅숏에서 되세운다 — 삭제 커맨드의 undo와 같은 기계다. redo는 되살린 뿌리를 선택한다.
/// </summary>
class InstantiateModelCommand final : public GameEngine::Core::IEditCommand
{
public:
    InstantiateModelCommand(IEditorObjectHost& host, GameEngine::Runtime::GameObject& gameObject);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    IEditorObjectHost* mHost;
    /// <summary>인스턴스화된 부분 트리다. [0]이 뿌리이고, 부모가 자식보다 먼저다.</summary>
    std::vector<GameObjectSnapshot> mObjects;
};

/// <summary>
/// 계층 드래그의 재부모화다. 셸이 SetParent(worldPositionStays=true)를 실행한 뒤 성공했을 때만
/// 앞뒤 상태로 기록한다. undo/redo는 월드 유지 재계산을 다시 하는 대신 기록된 로컬 값을 그대로
/// 되세운다 — 부동소수 재계산이 값을 흔들지 않게. 부모 안의 형제 위치도 함께 복원한다.
/// </summary>
class ReparentGameObjectCommand final : public GameEngine::Core::IEditCommand
{
public:
    struct TransformState
    {
        GameEngine::Math::Vector3 position;
        GameEngine::Math::Vector3 rotation;
        GameEngine::Math::Vector3 scale;
        /// <summary>부모 안의 위치다. 생략한 호출은 SetParent의 마지막 위치를 유지한다.</summary>
        std::size_t siblingIndex = (std::numeric_limits<std::size_t>::max)();
    };

    ReparentGameObjectCommand(
        IEditorObjectHost& host, unsigned int gameObjectId,
        unsigned int oldParentId, const TransformState& oldLocal,
        unsigned int newParentId, const TransformState& newLocal);

    [[nodiscard]] bool Apply() override;
    [[nodiscard]] bool Revert() override;

private:
    [[nodiscard]] bool Reparent(unsigned int parentId, const TransformState& local) const;

    IEditorObjectHost* mHost;
    unsigned int mGameObjectId;
    unsigned int mOldParentId;
    unsigned int mNewParentId;
    TransformState mOldLocal;
    TransformState mNewLocal;
};

}
