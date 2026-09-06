#pragma once

/// <summary>
/// Rigidbody2D가 고정 스텝 중력, 정적 고체 sweep, 접촉 상태를 기존 콜라이더 계층과 함께
/// 처리하는지 확인한다. 창이나 렌더러 없이 임시 장면만 세운다.
/// </summary>
[[nodiscard]] bool RunRigidbody2DTests();
