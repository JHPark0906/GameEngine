#pragma once

#include <filesystem>

#include <d3dcommon.h>
#include <wrl/client.h>

#include "../ShaderProgram.h"

namespace GameEngine::Rendering::Direct3D
{

/// <summary>
/// 셰이더 프로그램의 진입점 하나를 blob으로 가져온다.
///
/// 배포된 컴파일 아티팩트(.cso)가 있으면 그것을 로드하고, 없으면 HLSL 소스를 컴파일한다. 둘 다
/// 애플리케이션의 콘텐츠를 통해 읽으므로, 배포가 실행 파일 옆에 두든 안에 넣든 같은 방식으로
/// 찾아진다. 개발 빌드에는 아티팩트가 없어 소스에서 컴파일된다 — 셰이더를 고치고 다시 실행하면
/// 그대로 반영된다. 패키징된 빌드는 빌더가 만들어 둔 아티팩트를 로드해서, 실행 시 컴파일러가
/// 필요 없고 시작이 소스 컴파일만큼 걸리지 않는다.
///
/// 소스 컴파일 시 프로그램은 두 가지를 더 결정한다: 어느 파일이 자기를 구현하는지, 그리고
/// 소스가 전처리 정의로 받을 바인딩 슬롯이 무엇인지 — 그래서 HLSL은 슬롯을 되풀이해 적는 대신
/// 엔진의 슬롯을 받는다. 컴파일 오류는 디버거 출력으로 가고, 실패 메시지가 프로그램과 진입점의
/// 이름을 말하므로, 깨진 셰이더는 어느 파이프라인이 요청했는지 추측하지 않고도 식별된다.
/// </summary>
/// <param name="program">컴파일할 셰이더 프로그램이다.</param>
/// <param name="entryPoint">진입점 함수 이름이다. 예: "VS", "PS".</param>
/// <param name="target">셰이더 모델 타깃이다. 예: "vs_5_0".</param>
/// <param name="blob">성공 시 컴파일된 바이트코드를 받는다.</param>
[[nodiscard]] bool CompileShader(
    ShaderProgram program,
    const char* entryPoint,
    const char* target,
    Microsoft::WRL::ComPtr<ID3DBlob>& blob);

/// <summary>
/// 배포된 런타임 루트 안의 Direct3D 셰이더 소스를 전부 컴파일해, 실행 시 로드되는 아티팩트를
/// 소스 옆에 쓴다. 백엔드 서술자의 CompileRuntimeArtifacts가 이것을 가리킨다.
///
/// 같은 번역 단위가 같은 슬롯 정의로 컴파일하므로, 빌드 시점 아티팩트와 실행 시점 소스 컴파일이
/// 다른 셰이더를 만들 수 없다 — 슬롯을 빌드 스크립트에 다시 적었다면 생겼을 어긋남이다.
/// </summary>
/// <param name="runtimeRootPath">셰이더 소스가 스테이징된 배포 루트이다. 아티팩트도 여기 쓰인다.</param>
[[nodiscard]] bool CompileRuntimeArtifacts(const std::filesystem::path& runtimeRootPath);

}
