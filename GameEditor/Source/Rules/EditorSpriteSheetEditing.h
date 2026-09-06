#pragma once

// editor-layer: 0 (Rules)

#include "Assets/Asset.h"

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEditor
{

/// <summary>
/// 스프라이트의 시트(columns·rows·frameCount·frameRate)를 사이드카(<c>.meta</c>, 옛 이름은
/// <c>.sprite.json</c>)에 써 넣는다. 사람이 손으로 적은 다른 항목 — pixelsPerUnit, border,
/// 정체성 guid — 은 있으면 그대로 둔다: 이 함수가 아는 것은 시트뿐이고, 모르는 것을 지우면
/// 사람이 적어 둔 설정을 잃는다.
///
/// <c>AssetIdentityIssue.cpp</c>의 <c>IssueMissingIdentities</c>가 정체성을 더할 때 쓰는 것과
/// 같은 틀이다: 있는 사이드카를 객체로 파싱해 필요한 멤버만 바꾸고 되쓴다. 사이드카가 아직
/// 없으면 최소한의 것(format과 sheet)만 담아 새로 놓는다 — guid는 여기서 발급하지 않는다.
/// 발급은 <c>IssueMissingIdentities</c> 한 자리여야 하고(되돌릴 수 없는 동작이라서), 다음에
/// 그 함수가 이 파일을 볼 때 이미 있는 사이드카로 다뤄져 guid만 더해진다.
///
/// <b>런타임의 자산 데이터베이스는 여기서 갱신하지 않는다.</b> 프로젝트 디렉터리 감시가 이미
/// 조용해진 뒤 다시 스캔하는 것과 같은 경로로 이 파일도 잡히므로 — <c>WriteMissingSidecars</c>가
/// 쓴 파일이 그렇듯 — 별도의 즉시 반영 경로를 새로 만들 필요가 없다.
/// </summary>
/// <param name="database">이 스프라이트가 등록된 데이터베이스다. 사이드카 경로를 얻는 데 쓴다.</param>
/// <param name="sprite">시트를 바꿀 스프라이트다.</param>
/// <param name="sheet">
/// 새로 쓸 시트다. columns·rows가 양수가 아니거나, frameCount가 음수이거나 격자보다 크거나,
/// frameRate가 양수가 아니면 아무것도 쓰지 않고 거짓을 돌려준다 — 사이드카는 사람이 손으로도
/// 고치는 파일이라, 다음에 읽을 때 던져질 값을 이 함수 스스로 먼저 거절한다.
/// </param>
/// <returns>사이드카가 디스크에 놓였으면 참이다.</returns>
[[nodiscard]] bool WriteSpriteSheet(
    const GameEngine::Assets::AssetDatabase& database,
    const GameEngine::Assets::Sprite& sprite,
    const GameEngine::Assets::Sprite::Sheet& sheet);

}
