#pragma once

/// <summary>
/// JSON 문서가 바이트 순서 표식으로 시작해도 읽히는지다.
///
/// 편집기가 UTF-8로 저장하며 붙이는 세 바이트라, 사람이 손으로 쓰는 파일 — 에셋 사이드카가
/// 그렇다 — 이 그 길로 들어온다. 파서 자체와, 그 파일을 실제로 읽는 에셋 등록 양쪽에서 본다.
/// </summary>
[[nodiscard]] bool RunJsonByteOrderMarkTests();
