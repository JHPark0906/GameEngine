#pragma once

/// <summary>WAV 디코더의 지원 경계와 거절 이유를 검증한다.</summary>
bool RunWavAudioTests();

/// <summary>AudioSystem의 수명 규칙 — 시작·정지·재시작·청소 — 을 가짜 출력으로 검증한다.</summary>
bool RunAudioSystemTests();

/// <summary>
/// AudioListener 거리 감쇠 — 승자 결정, minDistance/maxDistance 선형 보간, spatialBlend 혼합,
/// 매 동기화마다 다시 계산됨 — 을 가짜 출력으로 검증한다.
/// </summary>
bool RunAudioListenerTests();
