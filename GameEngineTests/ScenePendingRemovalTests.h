#pragma once

/// <summary>장면의 대기 중 부모·자손 삭제와 파괴 콜백의 추가·삭제 재진입을 검사한다.</summary>
[[nodiscard]] bool RunScenePendingRemovalTests();
