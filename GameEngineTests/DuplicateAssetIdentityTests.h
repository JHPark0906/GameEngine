#pragma once

/// <summary>
/// 같은 GUID의 배포 파일이 둘이면 충돌 로그가 GUID·양쪽 경로·해결 방법을 포함해야 한다.
/// 창이 없는 실행에서도 진단을 읽을 수 있도록 실행 파일 옆 로그 파일에 기록하는지 확인한다.
/// </summary>
[[nodiscard]] bool RunDuplicateAssetIdentityTests();
