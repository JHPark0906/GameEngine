#pragma once

#include <filesystem>

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEngine::Runtime
{

class GameObject;
class Scene;

/// <summary>
/// 모델 파일이 담은 모든 메시를 장면에 넣는다. 메시마다 자식 하나를 둔 객체 하나로.
///
/// 모델 파일은 합쳐진 메시 하나가 아니라 geometry마다 메시 에셋 하나를 내놓는데, 여러 개를 담은
/// 파일을 Unity가 모델링하는 방식이 그렇다. `MeshRenderer`는 정확히 메시 하나를 가리키므로, 이
/// 연산이 없으면 다섯 부품짜리 모델은 손으로 객체 다섯을 만들고 각 부품의 로컬 id를 알아야
/// 한다.
///
/// 메시 하나짜리 파일은 감싸는 것 없이 객체 하나가 된다. 자식 하나를 쥐는 것이 유일한 목적인
/// 루트는 계층의 군더더기이기 때문이다.
///
/// 자식들은 항등 변환에 놓인다: 임포터가 파일 안에서의 각 메시 배치를 정점에 구워 넣으므로
/// 부품들은 이미 서로 올바르게 놓여 있고, 자식에게 자기 변환을 주면 그 배치를 두 번 적용하는
/// 셈이 된다. 반환된 객체를 움직이면 모델 전체가 움직이는데, 그것이 루트의 존재 이유다.
///
/// 이제 모델을 대표하는 객체를 반환하며, 참조가 모델을 가리키지 않거나 파일에 메시가 없거나
/// 장면이 객체를 거부하면 null이다. 각 실패는 로그된다.
/// </summary>
[[nodiscard]] GameObject* InstantiateModel(
    Scene& scene,
    const Assets::AssetDatabase& assetDatabase,
    const std::filesystem::path& modelPath);

}
