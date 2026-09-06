# GameBuilder

GameBuilder는 [GameEngine](../README.md) 프로젝트의 C++ 타깃을 컴파일하고 실행 파일·콘텐츠·런타임 파일을 배포 폴더로 구성하는 Windows 명령줄 도구입니다. 엔진 저장소 안의 `GameBuilder` 타깃으로 관리합니다.

[BuildToolMain.cpp](BuildToolMain.cpp)는 인자와 경로를 검증하고 CMake 빌드를 실행한 뒤, 엔진의 [Build::ProjectBuilder](../GameEngine/Build/ProjectBuilder.h)에 패키징을 요청합니다. 컴파일 순서를 정하는 CLI와 에셋·매니페스트·배포 폴더를 검증하는 엔진 서비스를 구분합니다. `Build::ProjectBuilder` 자체는 C++을 컴파일하지 않습니다.

## 도구 빌드

명령은 **GameEngine 저장소 루트**에서 실행합니다. 현재 [루트 CMake](../CMakeLists.txt)가 엔진, Editor, Builder, 테스트를 함께 등록합니다. 이 디렉터리의 [CMakeLists.txt](CMakeLists.txt)는 상위의 `GameEngine` 타깃과 공통 함수를 사용하므로 디렉터리만 떼어 독립적으로 configure할 수 없습니다.

Windows, MSVC C++20 도구와 Windows SDK가 필요합니다. 루트 CMake 최소 버전은 3.28이며 `vs` 프리셋은 `Visual Studio 18 2026` 생성기를 지원하는 CMake를 요구합니다. Visual Studio 2026에 포함된 CMake를 사용할 수 있습니다.

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=
cmake --build --preset debug --target GameBuilder
cmake --build --preset release --target GameBuilder
.\x64\Release\Tools\GameBuilder.exe --help
```

새 `vs` 구성의 빌드 트리는 `build/vs`, 도구의 기본 출력은 `x64/<Debug|Release>/Tools/GameBuilder.exe`입니다. 기존 캐시의 `GAMEENGINE_OUTPUT_ROOT`를 변경했다면 실제 출력도 그 경로를 따릅니다. 위의 빈 프로젝트 설정은 게임 고유 컴포넌트를 구성에 추가하지 않는다는 뜻입니다. 패키징할 게임은 아래 예처럼 빌드 트리에 먼저 등록해야 합니다.

## 게임 패키징

다음 예는 GameEngine과 같은 상위 디렉터리에 외부 게임 `MyGame/`을 별도로 준비하고 **GameEngine 루트**에서 실행하는 경우입니다. 게임 디렉터리에는 공통 함수로 `MyGame` 타깃을 선언하는 `CMakeLists.txt`와 유효한 프로젝트 콘텐츠가 있어야 합니다. [루트 README](../README.md#게임-프로젝트-연결)의 프로젝트 선언을 참고할 수 있습니다. GameBuilder 자체의 빌드에는 외부 게임이나 서버 저장소가 필요하지 않습니다.

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=../MyGame
cmake --build --preset release --target GameBuilder
.\x64\Release\Tools\GameBuilder.exe --project ..\MyGame --build-dir .\build\vs --configuration Release --target-name MyGame --output .\Builds\MyGame-Windows-x64
```

이 명령은 구성된 트리에서 `MyGame` 타깃을 빌드하고 `Builds/MyGame-Windows-x64/`에 패키징합니다. 도구는 configure를 대신 수행하지 않습니다. 생성기·프로젝트 선택·출력 경로를 결정한 `CMakeCache.txt`가 미리 있어야 합니다.

`--project`는 **게임의 `CMakeLists.txt`가 있는 소스 디렉터리**입니다. `.gameproject` 파일을 직접 받지 않습니다. 콘텐츠는 그 디렉터리의 `Content/`, 이어서 소스 디렉터리 자체에서 유일한 `.gameproject`가 있는 곳을 찾습니다. Editor의 Build와 스크립트 생성이 해석하는 `sourceRootPath`와 달리 이 CLI에는 소스 디렉터리를 직접 전달합니다.

### 인자

| 인자 | 의미와 기본값 |
| --- | --- |
| `--project <directory>` | 필수. 게임의 `CMakeLists.txt`가 있는 디렉터리 |
| `--output <directory>` | 필수. 게임 소스 디렉터리 밖의 배포 출력 폴더 |
| `--configuration <name>` | 기본 `Debug`. 보통 `Debug` 또는 `Release`; 사용한 CMake 트리에 있는 구성이어야 함 |
| `--build-dir <directory>` | 구성된 CMake 빌드 트리. 생략 시 **게임 소스 디렉터리의 상위 디렉터리 / `build/vs`** |
| `--target-name <name>` | CMake 타깃과 실행 파일 이름. 기본은 게임 소스 디렉터리의 마지막 이름 |
| `--cmake <path>` | 사용할 CMake 실행 파일을 명시 |
| `--single-file` | 검증된 콘텐츠를 실행 파일에 덧붙이는 패킹 모드. 기본은 폴더 배포 |
| `--issue-identities` | 식별자가 없는 에셋에 식별자를 발급하여 원본 에셋 옆에 `.meta` 기록. 기본은 꺼짐 |
| `--help`, `-h` | 사용법 표시 |

`--build-dir`의 기본 경로는 GameBuilder 실행 파일 위치나 엔진 저장소 위치로부터 추론하지 않습니다. 따라서 위처럼 외부 게임 저장소를 구성한 경우 명시적으로 전달하는 것이 정확합니다. 타깃 이름과 구성 이름은 문자·숫자·밑줄·하이픈으로 제한됩니다.

CMake는 명시한 `--cmake`, 빌드 캐시의 `CMAKE_COMMAND`, Visual Studio에 포함된 CMake, `PATH` 순서로 찾습니다. 컴파일 결과는 캐시의 `GAMEENGINE_OUTPUT_ROOT/<configuration>/<target-name>/<target-name>.exe`에서 찾습니다. 사용자 정의 출력 규칙을 쓰는 타깃이라면 이 도구의 출력 계약과 맞아야 합니다.

### 빌드 타깃의 소스 확인

공통 CMake 함수는 타깃을 선언한 실제 소스 디렉터리를 `GAMEENGINE_PROJECT_SOURCE_<target>:INTERNAL`로 기록합니다. configure마다 이 메타데이터를 다시 구성하므로 제거된 타깃의 항목은 남지 않습니다. Builder는 빌드 전과 완료 후에 이 경로를 `--project`와 대조합니다. 같은 타깃 이름의 다른 작업 사본, 누락되거나 잘못된 메타데이터는 패키징하지 않으며, 해당 프로젝트를 선택해 트리를 다시 configure해야 합니다. 엔진 루트일 수 있는 `CMAKE_HOME_DIRECTORY`만으로는 출처를 판단하지 않습니다.

두 디렉터리가 존재하고 canonical 경로가 같으면 파일 ID 조회 없이 허용합니다. 따라서 파일 ID 조회를 지원하지 않는 exFAT 등의 볼륨에서도 같은 canonical 소스 경로를 사용할 수 있습니다. canonical 경로가 다르면 파일 시스템 identity 비교로 동일 디렉터리임을 확인해야 하며, 이 비교가 다르거나 지원되지 않으면 거절합니다. 경로 별칭은 이 두 방법 중 하나로 같은 디렉터리임이 확인될 때 허용됩니다.

빌드가 CMake 자동 재구성을 일으킬 수 있으므로 완료 후 소스 검증을 반복하고 `GAMEENGINE_OUTPUT_ROOT`도 다시 읽습니다. 소스 선택이 바뀌면 기존 패키지를 유지하고 중단하며, 출력 루트만 바뀌었다면 완료된 빌드의 새 위치에서 실행 파일을 찾습니다.

## 패키징 흐름과 결과

1. 소스·콘텐츠·빌드 트리·출력 경로와 타깃의 소스 메타데이터를 확인합니다.
2. `cmake --build … --config … --target … --parallel`로 게임을 컴파일합니다. 실패하면 해당 종료 코드를 반환합니다.
3. 완료된 트리의 소스 메타데이터를 다시 대조하고 출력 루트를 다시 읽습니다.
4. 엔진 `Build::ProjectBuilder`가 임시 배포 폴더에 실행 파일, 프로젝트 콘텐츠와 필요한 런타임 파일을 조립하고 에셋 참조·매니페스트를 검증합니다.
5. 검증된 결과로 출력 폴더를 교체합니다. 실패하면 기존의 성공한 배포 결과를 보존하도록 처리합니다.

CLI 출력은 게임 소스 디렉터리 밖이어야 하며, 패키징 서비스는 콘텐츠·컴파일된 실행 파일·런타임 파일 경로와의 겹침도 거절합니다. 지정한 출력 폴더는 게시 시 교체되는 배포 전용 위치로 사용합니다.

일반 모드에서는 `.gameproject`와 콘텐츠·렌더링 파일이 실행 파일 옆에 배치됩니다. 원본의 `Content/` 디렉터리를 통째로 한 단계 더 감싸는 출력 구조가 아닙니다. 패킹 모드는 같은 스테이징·검증을 거친 후 콘텐츠를 실행 파일에 넣습니다.

`--single-file`에서도 Windows 로더가 읽어야 하는 네이티브 DLL과 디버그 심볼은 별도 파일입니다. 실행 파일 하나만 복사해도 된다는 보장으로 해석하지 말고 최종 출력 폴더를 확인해야 합니다. 도구가 성공하면 출력 위치와 에셋·씬·런타임 파일 개수를 표시하고 `0`으로 종료합니다. 입력 검증이나 패키징 실패는 실패 코드로 종료합니다.

에셋 식별자가 없으면 기본 패키징은 실패합니다. `--issue-identities`는 원본 에셋에 메타데이터를 기록해도 되는 경우에만 사용합니다. 이 옵션과 별개로 CMake 빌드는 컴포넌트 스키마를 프로젝트 `Content/Components.schema.json`에 생성할 수 있으므로 전체 명령이 소스 디렉터리에 아무 파일도 쓰지 않는 작업은 아닙니다.

### 폴더 ZIP 배포

ZIP 생성은 GameBuilder의 CLI 기능에 포함되어 있지 않습니다. 일반 모드의 완성된 출력 폴더를 별도 도구로 압축할 수 있습니다.

```powershell
Compress-Archive -LiteralPath .\Builds\MyGame-Windows-x64 -DestinationPath .\Builds\MyGame-Windows-x64.zip -Force
```

수신자는 폴더 전체를 압축 해제하고 게임 실행 파일을 실행합니다. 게임을 받는 사람에게 CMake·Visual Studio나 소스 컴파일은 필요하지 않습니다. 저장소의 [공통 MSVC 설정](../cmake/GameEngineProject.cmake)은 Release `/MT`, Debug `/MTd`를 적용하므로 이 설정으로 만든 Release 게임은 Visual C++ 런타임을 별도로 설치하지 않아도 됩니다. Windows 시스템 DLL, 그래픽 드라이버와 프로젝트별 네이티브 DLL 의존성은 여전히 필요합니다.

## 주요 코드와 검증

| 위치 | 책임 |
| --- | --- |
| [BuildToolMain.cpp](BuildToolMain.cpp) | `wmain`, CLI·경로 검증, CMake 빌드와 패키징 연결 |
| [CMakeLists.txt](CMakeLists.txt) | 엔진에 링크하는 콘솔 타깃과 `Tools/` 출력 설정 |
| [엔진 Build/ProjectBuilder.cpp](../GameEngine/Build/ProjectBuilder.cpp) | 패키지 조립·검증·패킹·출력 교체 |
| [엔진 Build/CMakeLocation.cpp](../GameEngine/Build/CMakeLocation.cpp) | CMake 캐시 읽기와 실행 파일 탐색 |
| [엔진 Build/ProjectBuildSource.cpp](../GameEngine/Build/ProjectBuildSource.cpp) | 타깃 소스 메타데이터와 요청 프로젝트의 canonical 경로·identity 대조 |
| [엔진 App/ProjectFile.cpp](../GameEngine/App/ProjectFile.cpp) | 프로젝트 설명 파일과 콘텐츠 루트, 프로젝트 CMake 생성 |
| [공통 프로젝트 CMake](../cmake/GameEngineProject.cmake) | 컴파일 옵션, 타깃·콘텐츠·스키마·런타임 파일 배치 |

관련 회귀 검사는 [GameEngineTests](../GameEngineTests/CMakeLists.txt)의 `ProjectBuilder` 스위트와 별도 CTest `ProjectBuildSourceCli`에 있습니다. [ProjectBuildSourceTests](../GameEngineTests/ProjectBuildSourceTests.cpp)는 소스 경로·메타데이터 판정을 검사하며, [CLI 회귀](../GameEngineTests/ProjectBuildSourceCliTests.cmake)는 실제 Builder로 다른 작업 사본의 거부, 정상 패키징, 타깃 제거와 빌드 중 소스·출력 경로 변경을 검사합니다. `ProjectBuilder`는 패키징, 매니페스트와 콘텐츠 팩, 실패 시 출력 보존, 빌드 요청과 스키마 배치도 확인합니다.
공개 기본 구성의 실제 확인 상태와 전체 절차는 [검증 안내](../Docs/VALIDATION.md)에 있습니다.

```powershell
cmake --build --preset debug
cmake --build --preset release
ctest --test-dir build/vs -C Debug -R "GameEngineTests\.(ProjectBuilder|ProjectBuildSourceCli)$" --output-on-failure
ctest --test-dir build/vs -C Release -R "GameEngineTests\.(ProjectBuilder|ProjectBuildSourceCli)$" --output-on-failure
```

모든 구성의 빌드가 끝난 뒤 CTest를 실행합니다. 검사 입력에 생성·배치된 콘텐츠와 스키마가 포함되므로 빌드, 다른 GameBuilder 실행, Editor의 Build 메뉴를 동시에 실행하지 않습니다. 현재 도구는 엔진 저장소의 CMake 타깃·출력·패키징 계약을 사용하는 Windows 도구이며, 임의의 CMake 프로젝트를 패키징하는 범용 빌더는 아닙니다.
