#pragma once

/// <summary>
/// 스테이징은 이전 소유 매니페스트에 있고 현재 목록에는 없는 파일만 지워야 한다.
/// 소유 목록에 없는 설정이나 sprite JSON은 보존해 관련 없는 사용자 파일을 지우지 않는다.
/// </summary>
[[nodiscard]] bool RunPruneStaleFilesScriptTests();

/// <summary>
/// 위 시험이 스크립트 하나만 격리해 재는 것과 달리, 이 시험은 실제 배선 - gameengine_stage_content
/// 가 만드는 <c>&lt;target&gt;_Content</c> 타깃과, 그 POST_BUILD로 붙는 프루닝 - 을 실제 configure와
/// build로 지나간다: 콘텐츠를 스테이징하고, 그중 하나를 소스에서 지우고, 다시 configure와 build를
/// 돌려 그 사본만 사라지고 나머지와 실행 중에나 생기는 파일은 그대로인지 본다.
/// </summary>
[[nodiscard]] bool RunPruneStaleFilesBuildIntegrationTests();
