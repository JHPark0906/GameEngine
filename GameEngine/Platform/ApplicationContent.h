#pragma once

#include "IContentSource.h"

namespace GameEngine::Platform
{

/// <summary>
/// 이 실행 파일과 함께 배포된 파일들이다 — 셰이더와 실행할 프로젝트.
///
/// 프로세스당 실행 파일은 하나이므로 PlatformServices를 통해 접근한다.
/// 실행 파일의 콘텐츠 팩을 우선하고, 팩이 없으면 실행 파일 옆 디렉터리를 사용한다.
/// 소비자는 바이트만 요청하므로 저장 방식에 의존하지 않는다.
/// </summary>
[[nodiscard]] const IContentSource& GetApplicationContent();

/// <summary>
/// 이 애플리케이션의 콘텐츠가 옆에 놓이는 대신 실행 파일 안에 packed되어 있는지 여부이다.
///
/// 조립 루트만 묻는다. packed 애플리케이션은 그 자체가 프로젝트라서 그 아래 해석할 에셋 루트가
/// 없고, packed가 아니면 프로젝트에 설정된 루트를 실행 파일이 있는 디렉터리에 대해 해석한다.
/// 그 결정 아래의 모든 것은 소스를 읽을 뿐 상관하지 않는다.
/// </summary>
[[nodiscard]] bool IsApplicationContentPacked();

}
