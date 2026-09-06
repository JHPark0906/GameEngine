#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace GameEngine::Runtime
{

class RuntimeContext;
class Scene;

/// <summary>
/// 장면 파일의 바이트를 Scene으로 바꾸는 함수다. 런타임은 파일 형식을 모른다 — 형식은 위
/// 계층(Serialization)의 것이고, 런타임을 세우는 쪽이 로더를 넘긴다. 그래서 Runtime은
/// Serialization을 포함하지 않고, 테스트는 파일 없이 장면을 만드는 로더를 끼울 수 있다.
/// </summary>
using SceneLoader = std::function<std::unique_ptr<Scene>(
    const std::vector<std::byte>& bytes,
    const std::filesystem::path& relativePath,
    RuntimeContext& runtimeContext)>;

}
