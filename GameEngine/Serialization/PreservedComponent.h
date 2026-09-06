#pragma once

#include <memory>
#include <string>

#include "../Core/Json.h"
#include "../Runtime/Component.h"

namespace GameEngine::Serialization
{

/// <summary>
/// 엔진이 만들 줄 모르는 컴포넌트를, 잃는 대신 실은 채로 보존한다.
///
/// 프로젝트가 정의한 MonoBehaviour의 팩토리는 그 프로젝트의 실행 파일 안에만 있어서, 에디터가
/// 장면을 열면 그런 컴포넌트를 만들 수 없다. 경고와 함께 떨어뜨리면 그 장면을 저장하는 순간
/// 경고가 데이터 손실이 된다 — 에디터에서 Transform 하나 고쳐 저장했더니 스크립트가 사라지는
/// 식으로.
///
/// 그래서 만들 수 없는 컴포넌트는 파싱된 JSON 그대로를 쥔 이 컴포넌트가 되어 객체에 붙는다.
/// 아무것도 하지 않고, 업데이트에 참여하지 않으며, 저장될 때 쥔 것을 도로 쓴다. 그 장면을
/// 팩토리가 있는 프로세스 — 게임 자신 — 가 다시 열면 원래 컴포넌트가 되살아난다.
///
/// 에셋 참조는 수집하지 않는다: 모르는 컴포넌트의 어느 속성이 참조인지 알 방법이 없다. 그런
/// 참조가 있다면 프리로드를 놓치고 프레임 안 예비 로드로 잡히는데, 그것은 로그에 남는다.
/// </summary>
class PreservedComponent final : public Runtime::Component
{
public:
    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>
    [[nodiscard]] static const Runtime::ComponentType& StaticType();
    [[nodiscard]] const Runtime::ComponentType& GetComponentType() const override
    {
        return StaticType();
    }

    /// <summary>보존할 컴포넌트의 JSON 전체를 받는다. "type"을 포함한 객체 그대로다.</summary>
    void SetData(Core::Json data);

    /// <summary>보존 중인 컴포넌트의 JSON이다. 저장이 이것을 그대로 쓴다.</summary>
    [[nodiscard]] const Core::Json& GetData() const { return mData; }

    /// <summary>보존 중인 컴포넌트의 원래 타입 이름이다. 진단과 인스펙터 표시에 쓰인다.</summary>
    [[nodiscard]] const std::string& GetPreservedTypeName() const { return mTypeName; }

private:
    [[nodiscard]] std::unique_ptr<Runtime::Component> Clone() const override;

    Core::Json mData;
    std::string mTypeName;
};

}
