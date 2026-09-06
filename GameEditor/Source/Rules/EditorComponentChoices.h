#pragma once

// editor-layer: 0 (Rules)

#include <span>
#include <string>
#include <vector>

#include "Core/Json.h"

namespace GameEngine::Serialization
{
struct ComponentSchema;
}

namespace GameEditor
{

struct EditorComponentChoice
{
    std::string typeName;
    /// <summary>null이면 등록 타입의 기본값을, 그 외에는 스키마 기본값을 쓴다.</summary>
    GameEngine::Core::Json prototype;
    bool isGameComponent = false;
};

/// <summary>
/// 현재 생성 가능한 등록 타입과 프로젝트 스키마를 이름별로 한 번씩 나열한다.
/// 등록 타입을 우선하며, 취소된 팩토리와 직접 만들 수 없는 필수 컴포넌트는 제외한다.
/// </summary>
[[nodiscard]] std::vector<EditorComponentChoice> BuildEditorComponentChoices(
    std::span<const GameEngine::Serialization::ComponentSchema> schemas);

}
