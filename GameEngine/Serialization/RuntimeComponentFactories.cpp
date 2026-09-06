#include "pch.h"
#include "RuntimeComponentFactories.h"

#include "ComponentFactory.h"
#include "../Assets/AssetReference.h"
#include "../Core/Json.h"
#include "../Diagnostics/Debug.h"

#include "../Runtime/Animator.h"
#include "../Runtime/AudioListener.h"
#include "../Runtime/AudioSource.h"
#include "../Runtime/Button.h"
#include "../Runtime/InputField.h"
#include "../Runtime/BoxCollider2D.h"
#include "../Runtime/BoxCollider3D.h"
#include "../Runtime/Camera.h"
#include "../Runtime/Canvas.h"
#include "../Runtime/ContentFit.h"
#include "../Runtime/Dropdown.h"
#include "../Runtime/ComponentType.h"
#include "../Runtime/GameObject.h"
#include "../Runtime/Light.h"
#include "../Runtime/MeshRenderer.h"
#include "../Runtime/PropertyDescriptor.h"
#include "../Runtime/LayoutElement.h"
#include "../Runtime/RectMask.h"
#include "../Runtime/RectTransform.h"
#include "../Runtime/Rigidbody2D.h"
#include "../Runtime/Rigidbody3D.h"
#include "../Runtime/ScrollRect.h"
#include "../Runtime/SpriteAnimator.h"
#include "../Runtime/SpriteRenderer.h"
#include "../Runtime/TextRenderer.h"
#include "../Runtime/TilemapCollider2D.h"
#include "../Runtime/TilemapRenderer.h"
#include "../Runtime/Transform.h"
#include "../Runtime/UIWindow.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace GameEngine::Serialization
{

namespace
{
    Math::Vector2 ReadVector2(const Core::Json& value, const Math::Vector2& fallback)
    {
        if (!value.IsArray() || value.Size() != 2 ||
            !value.At(0).IsNumber() || !value.At(1).IsNumber())
        {
            return fallback;
        }
        return {value.At(0).Get<float>(), value.At(1).Get<float>()};
    }

    Math::Vector3 ReadVector3(const Core::Json& value, const Math::Vector3& fallback)
    {
        if (!value.IsArray() || value.Size() != 3 ||
            !value.At(0).IsNumber() || !value.At(1).IsNumber() || !value.At(2).IsNumber())
        {
            return fallback;
        }
        return { value.At(0).Get<float>(), value.At(1).Get<float>(), value.At(2).Get<float>() };
    }

    Math::Color ReadColor(const Core::Json& value, const Math::Color& fallback)
    {
        if (!value.IsArray() || (value.Size() != 3 && value.Size() != 4) ||
            !value.At(0).IsNumber() || !value.At(1).IsNumber() || !value.At(2).IsNumber() ||
            (value.Size() == 4 && !value.At(3).IsNumber()))
        {
            return fallback;
        }
        return {
            value.At(0).Get<float>(), value.At(1).Get<float>(), value.At(2).Get<float>(),
            value.Size() == 4 ? value.At(3).Get<float>() : 1.0f
        };
    }

    /// <summary>enum 오류 메시지의 허용 이름 나열이다: "'simple' or 'sliced'" 꼴.</summary>
    [[nodiscard]] std::string JoinQuotedNames(const std::span<const std::string> names)
    {
        std::string joined;
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            if (index > 0)
            {
                joined += names.size() > 2 ? ", " : " ";
                if (index == names.size() - 1)
                {
                    joined += "or ";
                }
            }
            joined += '\'' + names[index] + '\'';
        }
        return joined;
    }

    /// <summary>
    /// 값 하나를 서술의 setter로 넘기고, setter가 거절하면 경고를 남긴다.
    ///
    /// 결과를 버리면 거절이 아무 흔적도 남기지 않는다. 파일은 값을 말했는데 객체에는 기본값이
    /// 남아 있고, 그 차이를 볼 방법이 없다. 같은 복원을 하는 undo 경로도 경고로 말한다. 로드
    /// 한 번에 한 줄이므로 스팸이 되지 않는다.
    /// </summary>
    /// <param name="type">속성을 선언한 컴포넌트 타입이다. 메시지의 이름에 쓰인다.</param>
    /// <param name="descriptor">적용할 속성의 서술이다.</param>
    /// <param name="component">값을 받을 컴포넌트다.</param>
    /// <param name="value">setter에 넘길 값이다.</param>
    void ApplyValue(
        const Runtime::ComponentType& type,
        const Runtime::PropertyDescriptor& descriptor,
        Runtime::Component& component,
        Runtime::PropertyValue value)
    {
        if (!descriptor.TrySet(component, std::move(value)))
        {
            Diagnostics::Debug::LogWarning(
                "Scene load could not apply a component property; the default remains. type=",
                type.GetName(), ", property=", descriptor.GetName());
        }
    }

    /// <summary>
    /// 속성 하나를 JSON에서 읽어 서술의 setter로 적용한다. 없는 멤버는 기본값을 유지하고,
    /// 종류에 맞지 않는 형태의 값은 조용히 기본값으로 남는다. 예외는 enum
    /// 문자열이다: 이름 표 밖의 문자열은 오타일 가능성이 높아, 기본값으로 덮는 대신 형식
    /// 오류로 거절한다 — 그 거절은 장면 전체가 아니라 이 컴포넌트 하나를 강등시킨다
    /// (<c>SceneSerializer</c>의 AddComponents).
    /// </summary>
    void ApplyProperty(
        const Core::Json& json,
        const Runtime::ComponentType& type,
        const Runtime::PropertyDescriptor& descriptor,
        Runtime::Component& component)
    {
        using Runtime::PropertyKind;
        using Runtime::PropertyValue;

        const std::string name(descriptor.GetName());
        if (const Core::Json* const member = json.Find(name))
        {
            switch (descriptor.GetKind())
            {
            case PropertyKind::Bool:
                if (member->IsBoolean())
                {
                    ApplyValue(type, descriptor, component, PropertyValue{ member->Get<bool>() });
                }
                break;
            case PropertyKind::Int:
                if (member->IsNumber())
                {
                    ApplyValue(type, descriptor, component, PropertyValue{ member->Get<int>() });
                }
                break;
            case PropertyKind::Float:
                if (member->IsNumber())
                {
                    ApplyValue(type, descriptor, component, PropertyValue{ member->Get<float>() });
                }
                break;
            case PropertyKind::String:
                if (member->IsString())
                {
                    ApplyValue(
                        type, descriptor, component,
                        PropertyValue{ member->Get<std::string>() });
                }
                break;
            case PropertyKind::Vector2:
            {
                const Math::Vector2 current =
                    std::get<Math::Vector2>(descriptor.Get(component));
                ApplyValue(
                    type, descriptor, component,
                    PropertyValue{ ReadVector2(*member, current) });
                break;
            }
            case PropertyKind::Vector3:
            {
                const Math::Vector3 current =
                    std::get<Math::Vector3>(descriptor.Get(component));
                ApplyValue(
                    type, descriptor, component,
                    PropertyValue{ ReadVector3(*member, current) });
                break;
            }
            case PropertyKind::Color:
            {
                const Math::Color current = std::get<Math::Color>(descriptor.Get(component));
                ApplyValue(
                    type, descriptor, component, PropertyValue{ ReadColor(*member, current) });
                break;
            }
            case PropertyKind::AssetReference:
                ApplyValue(
                    type, descriptor, component,
                    PropertyValue{ Assets::AssetReference::Parse(
                        member->IsString() ? member->Get<std::string>() : std::string{}) });
                break;
            case PropertyKind::Enum:
                if (member->IsString())
                {
                    const std::string text = member->Get<std::string>();
                    const std::span<const std::string> names = descriptor.GetEnumNames();
                    int value = -1;
                    for (std::size_t index = 0; index < names.size(); ++index)
                    {
                        if (names[index] == text)
                        {
                            value = static_cast<int>(index);
                            break;
                        }
                    }
                    if (value < 0)
                    {
                        // 이 컴포넌트에 대한 판정이지 장면에 대한 판정이 아니다. 장면 로더가
                        // 이 형식을 잡아 컴포넌트 하나만 보존 데이터로 강등한다.
                        throw ComponentValueError(
                            std::string(type.GetName()) + "." + name + " must be " +
                            JoinQuotedNames(names));
                    }
                    ApplyValue(type, descriptor, component, PropertyValue{ value });
                }
                break;
            }
        }

        // 상수 주석은 값이 아니라 표기 규약이다. 다른 표기를 말하는 파일은 조용히 다르게 읽는
        // 대신 형식 오류로 거절한다 — Transform의 rotationUnit 검사와 같은 의미론이다.
        for (const auto& [constantName, expected] : descriptor.GetConstants())
        {
            const Core::Json* const constant = json.Find(constantName);
            if (constant && constant->IsString() && constant->Get<std::string>() != expected)
            {
                throw Core::JsonError(
                    std::string(type.GetName()) + "." + constantName + " must be '" + expected +
                    "'");
            }
        }
    }

    void ApplyProperties(
        const Core::Json& json,
        const Runtime::ComponentType& type,
        Runtime::Component& component)
    {
        const Runtime::PropertyRestoreScope restore(component);
        for (const Runtime::PropertyDescriptor* const descriptor :
             Runtime::CollectProperties(type))
        {
            if (Runtime::HasTrait(descriptor->GetTraits(), Runtime::PropertyTraits::NotSerialized))
            {
                continue;
            }
            ApplyProperty(json, type, *descriptor, component);
        }
    }

}

std::span<const Runtime::ComponentType* const> EngineComponentTypes()
{
    // 속성 서술과 일반 팩토리로 컴포넌트를 만든다. 새 컴포넌트는 자기 속성을 선언하고
    // 이 목록에 등록하면 직렬화와 에디터가 같은 선언을 사용한다.
    static const Runtime::ComponentType* const types[] = {
        &Runtime::Transform::StaticType(),
        &Runtime::Camera::StaticType(),
        &Runtime::Light::StaticType(),
        &Runtime::SpriteRenderer::StaticType(),
        &Runtime::SpriteAnimator::StaticType(),
        &Runtime::MeshRenderer::StaticType(),
        &Runtime::Animator::StaticType(),
        &Runtime::TextRenderer::StaticType(),
        &Runtime::TilemapRenderer::StaticType(),
        &Runtime::AudioSource::StaticType(),
        &Runtime::AudioListener::StaticType(),
        &Runtime::BoxCollider2D::StaticType(),
        &Runtime::BoxCollider3D::StaticType(),
        &Runtime::TilemapCollider2D::StaticType(),
        &Runtime::Rigidbody2D::StaticType(),
        &Runtime::Rigidbody3D::StaticType(),
        &Runtime::Canvas::StaticType(),
        &Runtime::LayoutElement::StaticType(),
        &Runtime::RectMask::StaticType(),
        &Runtime::RectTransform::StaticType(),
        &Runtime::Button::StaticType(),
        &Runtime::ScrollRect::StaticType(),
        &Runtime::ContentFit::StaticType(),
        &Runtime::InputField::StaticType(),
        &Runtime::Dropdown::StaticType(),
        &Runtime::UIWindow::StaticType(),
    };
    return types;
}

namespace
{
    /// <summary>
    /// 이 프로세스에서 등록에 성공한 컴포넌트 타입들이다. 등록 순서를 지킨다.
    ///
    /// 팩토리 표는 이름 → 만드는 법만 쥐므로 "이 프로세스가 아는 타입이 무엇인가"에 답할 수
    /// 없다. 컴포넌트 스키마 산출이 그 질문을 하고, 그 답이 프로세스 경계를 넘어 에디터에게
    /// 게임 컴포넌트를 보여 준다.
    /// </summary>
    std::vector<const Runtime::ComponentType*>& RegisteredTypeList()
    {
        static std::vector<const Runtime::ComponentType*> types;
        return types;
    }
}

std::span<const Runtime::ComponentType* const> RegisteredComponentTypes()
{
    return RegisteredTypeList();
}

bool RegisterComponentType(const Runtime::ComponentType& type)
{
    const bool registered = ComponentFactory::Register(
        std::string(type.GetName()),
        [&type](const Core::Json& json, Runtime::GameObject& gameObject)
        {
            // Transform은 객체의 일부라 만들지 않고, 이미 있는 것을 채운다.
            if (&type == &Runtime::Transform::StaticType())
            {
                if (const Core::Json* children = json.Find("children"); children &&
                    (!children->IsArray() || children->Size() != 0))
                {
                    throw Core::JsonError(
                        "Transform.children is not supported; use GameObject id/parent references");
                }
                ApplyProperties(json, type, gameObject.GetTransform());
                gameObject.GetTransform().ReadExtraSerializedState(json);
                return true;
            }

            std::unique_ptr<Runtime::Component> component = type.CreateInstance();
            if (!component)
            {
                return false;
            }
            ApplyProperties(json, type, *component);
            component->ReadExtraSerializedState(json);
            return gameObject.AddComponent(std::move(component)) != nullptr;
        });
    if (registered)
    {
        RegisteredTypeList().push_back(&type);
    }
    return registered;
}

bool RegisterRuntimeComponentFactories()
{
    bool registered = true;
    for (const Runtime::ComponentType* const type : EngineComponentTypes())
    {
        registered = RegisterComponentType(*type) && registered;
    }
    return registered;
}

}
