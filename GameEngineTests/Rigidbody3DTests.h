#pragma once

/// <summary>
/// Rigidbody3D가 고정 스텝 중력, 정적 상자 충돌, 접촉 상태를 3D 콜라이더 계층과 함께 처리하는지
/// 창이나 렌더러 없이 임시 장면에서 확인한다.
/// </summary>
[[nodiscard]] bool RunRigidbody3DTests();
