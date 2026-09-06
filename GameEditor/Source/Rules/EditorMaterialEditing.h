#pragma once

// editor-layer: 0 (Rules)

#include "Assets/MaterialData.h"

namespace GameEngine::Assets
{
class Material;
}

namespace GameEditor
{

/// <summary>
/// 머티리얼의 texture·tint를 그 <c>.material</c> 파일에 그대로 써 넣는다.
///
/// <c>WriteSpriteSheet</c>와 달리 사이드카를 찾지 않는다 — 머티리얼은 자기 소스 파일 자체가
/// 값의 저장소이고, 그 파일은 이미 스캔된 에셋의 것이므로 언제나 있다. 그래도 있는 내용을
/// 객체로 파싱해 아는 두 필드만 바꾸고 되쓰는 것은 같다: 사람이 손으로 다른 필드를 더해 뒀다면
/// 그것을 지우지 않는다.
/// </summary>
/// <param name="material">쓸 대상 머티리얼이다.</param>
/// <param name="data">새로 쓸 texture·tint다.</param>
/// <returns>파일에 놓였으면 참이다. 있던 내용이 깨진 JSON이면(사람이 손으로 망가뜨렸다면)
/// 되쓰지 않고 거절한다 — 고쳐 쓰면 그 자리에 있던 것을 잃는다.</returns>
[[nodiscard]] bool WriteMaterial(
    const GameEngine::Assets::Material& material, const GameEngine::Assets::MaterialData& data);

}
