# GameEditor

GameEditor는 [GameEngine](../README.md)으로 게임의 프로젝트·씬·에셋을 편집하고 Play 모드로 실행하는 Windows 데스크톱 도구입니다. 엔진과 같은 저장소에서 함께 빌드하며, 엔진의 런타임·렌더링·UI·직렬화를 사용합니다.

개발 과정에서 생성형 AI의 도움을 받은 프로젝트입니다.

Editor 자체도 `gameengine_add_game_project`로 선언한 게임 프로젝트입니다. 별도의 `main`을 두지 않고 엔진의 플레이어 진입점에서 시작한 뒤, [EditorRegistration.cpp](Source/Shell/EditorRegistration.cpp)가 등록한 부트스트랩을 실행합니다. 편집 기능은 이 디렉터리에 있으며 엔진 라이브러리가 Editor를 참조하지 않습니다.

## 할 수 있는 일

- 프로젝트와 씬 생성·열기·저장, 오브젝트 계층 편집, Inspector에서 컴포넌트와 속성 편집
- 전용 카메라를 사용하는 Scene View와 실제 게임 카메라를 사용하는 Game View 확인
- 타일 팔레트와 타일맵 도구, 콘텐츠 브라우저, 머티리얼·스프라이트 시트 편집
- 편집 명령의 Undo/Redo, 로컬 설정과 마지막 작업 위치 복원, 복구 사본 처리
- Play/Stop 전환과 게임 입력 전달, 콘솔에서 로그 확인
- C++ 컴포넌트 템플릿 생성과 열린 프로젝트의 CMake 빌드 요청

Play는 편집 씬 하나만 활성인 상태에서 시작하며, 진입할 때 미저장 편집을 포함한 스냅샷을 보관합니다. 다른 활성 씬이 이미 있으면 그 씬을 버리지 않고 진입을 거절합니다. Stop은 원본 씬이 Play 중 언로드되었더라도 스냅샷을 복원하고, 실행 중 추가로 로드한 씬까지 정리합니다. 종료 콜백은 이 정리 도중 씬을 추가하거나 복원 씬을 삭제할 수 없습니다. 내려간 씬의 오디오와 미사용 에셋도 정리합니다.

복원 준비에 실패하면 Play 상태와 스냅샷을 유지하므로 Stop을 다시 시도하거나 편집 상태의 복구 사본을 저장할 수 있습니다. 정상 Stop도 진입 전의 미저장 표시를 지우지 않습니다. Stop의 재생성으로 객체 ID가 바뀌므로 Play 진입과 정상 복원에서 Undo 이력을 비웁니다. 이 흐름은 [EditorSceneDocument](Source/Document/EditorSceneDocument.cpp)와 [EditorPlaySession](Source/Document/EditorPlaySession.cpp)이 관리합니다.

[UndoStack과 IEditCommand](Source/Document/UndoStack.h)는 Editor의 Document 계층에서 편집 이력을 관리합니다. 엔진 라이브러리는 이 편집 명령 계약에 의존하지 않습니다. Edit 모드의 삭제·재부모화 Undo/Redo는 부모뿐 아니라 형제 위치와 저장한 로컬 변환도 복원합니다. UI의 겹침 순서는 [공통 UIStack](../GameEngine/Runtime/UIStackOrder.h)을 통해 그리기와 입력에 함께 적용됩니다. 창을 앞으로 옮기면 Sprite·텍스트·드롭다운과 입력 대상이 함께 이동하고, 중첩 창은 부모의 일반 콘텐츠 위에, 최상위 모달은 다른 창 위에 놓입니다.

## 빌드와 실행

모든 명령은 **이 디렉터리의 상위인 GameEngine 저장소 루트**에서 실행합니다. 현재 [루트 CMake](../CMakeLists.txt)가 엔진, Editor, Builder, 테스트를 함께 등록하므로 `cmake -S GameEditor`만으로 구성하는 독립 빌드는 지원하지 않습니다.

개발 환경은 Windows, MSVC C++20 도구와 Windows SDK입니다. 루트 CMake 최소 버전은 3.28이며, `vs` 프리셋을 사용할 CMake는 `Visual Studio 18 2026` 생성기를 지원해야 합니다. Visual Studio 2026에 포함된 CMake를 사용할 수 있습니다.

게임 고유 C++ 컴포넌트를 포함하지 않는 Editor 빌드:

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=
cmake --build --preset debug --target GameEditor
cmake --build --preset release --target GameEditor
.\x64\Release\GameEditor\GameEditor.exe
```

새 `vs` 구성의 기본 빌드 트리는 `build/vs`, 출력은 `x64/<Debug|Release>/GameEditor/`입니다. 기존 캐시에서 `GAMEENGINE_OUTPUT_ROOT`를 변경했다면 실행 경로도 그 값을 따릅니다. 빌드가 Editor의 콘텐츠와 렌더링 파일을 실행 파일 옆에 배치하므로 실행할 때 해당 폴더를 함께 유지합니다.

외부 게임의 C++ 컴포넌트를 포함하려면 그 게임을 명시적으로 연결합니다. 다음 예는 GameEngine과 같은 상위 디렉터리에 `CMakeLists.txt`와 `Content/MyGame.gameproject`를 가진 `MyGame/`을 별도로 준비한 경우입니다. 프로젝트 선언 예시는 [루트 README](../README.md#게임-프로젝트-연결)에 있습니다.

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=../MyGame
cmake --build --preset release --target GameEditor
.\x64\Release\GameEditor\GameEditor.exe ..\MyGame\Content\MyGame.gameproject
```

시작 시 명령줄에서 첫 `.gameproject` 경로를 엽니다. 경로가 없거나 열기에 실패하면 저장된 마지막 프로젝트와 씬을 복원하려고 시도합니다. 실행 후 File 메뉴로도 프로젝트를 열 수 있습니다.

### 프로젝트 코드 연결

`GAMEEDITOR_PROJECT_DIRECTORY`는 `.gameproject` 파일이 아니라 게임의 `CMakeLists.txt`가 있는 디렉터리입니다. 해당 CMake는 공통 함수 `gameengine_add_game_project`로 게임을 선언해야 합니다. 빈 값은 게임 고유 컴포넌트 없이 동작하는 Editor를 뜻하며, 잘못된 경로나 CMake 파일이 없는 경로를 지정하면 configure가 실패합니다. 공개 저장소는 게임 프로젝트를 포함하지 않으므로 기본 빌드에는 외부 게임이나 서버 저장소가 필요하지 않습니다. 기존 빌드 트리에서 게임 연결을 해제할 때도 빈 값을 명시합니다.

선택한 게임의 `<Target>_Components` 오브젝트 라이브러리를 Editor에 링크하여 타입 등록과 Play 동작을 제공합니다. 프로젝트의 C++ 코드를 바꾸거나 연결할 프로젝트를 바꾸면 Editor를 다시 빌드해야 합니다. 실행 중인 Editor에 코드를 다시 로드하는 기능은 없습니다. C++ 코드가 없는 콘텐츠 프로젝트는 이 설정에 넣지 않고 실행 중 `.gameproject`를 열 수 있습니다.

Inspector의 Add Component는 생성 훅과 현재 팩토리가 있는 등록 타입을 엔진·게임 구분 없이 표시하고, 프로젝트의 `Components.schema.json`으로 나머지를 보완합니다. 같은 타입 이름은 한 번만 표시하며 Transform처럼 따로 생성할 수 없는 타입은 제외합니다. 연결된 게임 타입은 실제 컴포넌트로, 코드가 없는 타입은 스키마 기본값을 담은 보존 데이터로 추가합니다. 목록 규칙은 [EditorComponentChoices](Source/Rules/EditorComponentChoices.cpp)에 있습니다. UIWindow도 엔진 등록 타입이므로 저장 후 실제 창 컴포넌트로 복원됩니다. 일시적인 modal 상태는 저장하지 않습니다.

### Editor의 Build 메뉴

[EditorProjectBuild](Source/Rules/EditorProjectBuild.cpp)는 Editor를 만들 때 기록한 엔진 소스 루트·빌드 트리·구성을 사용합니다. 열린 프로젝트의 `sourceRootPath`를 따라 소스 디렉터리를 구하고, 그 프로젝트를 지정하여 엔진 트리를 다시 configure한 다음 게임 타깃을 컴파일합니다. `sourceRootPath`는 `.gameproject` 기준 상대 경로이며 생략하면 `.`입니다. 설명 파일이 `Content/`에 있고 CMake가 한 단계 위에 있다면 `"sourceRootPath": ".."`를 사용합니다.

Editor의 스크립트 생성도 이 소스 경로를 사용합니다. 콘텐츠 루트는 계속 `.gameproject`가 있는 디렉터리이며, GameBuilder는 `sourceRootPath`를 해석하지 않고 `--project`로 소스 디렉터리를 직접 받습니다.

컴파일 출력은 콘솔로 전달되고 실행 중 빌드를 취소할 수 있습니다. 이 메뉴는 배포 폴더를 만들지 않습니다. 배포 패키징은 [GameBuilder](../GameBuilder/README.md)의 역할입니다. Build 메뉴도 개발용 CMake·컴파일러와 기록된 소스/빌드 트리가 필요하므로, Editor 실행 폴더만 다른 PC로 옮겨 게임 컴파일까지 할 수 있는 구조는 아닙니다.

## 주요 구조

| 위치 | 책임 |
| --- | --- |
| [CMakeLists.txt](CMakeLists.txt) | 게임 프로젝트 선택, 컴포넌트 링크, Editor 콘텐츠와 스키마 배치 |
| [Source/Shell](Source/Shell) | 부트스트랩, 패널 조립, 입력·명령·렌더링 연결 |
| [Source/Document](Source/Document) | 편집 프로젝트와 씬, Undo, Play 스냅샷, 설정·에셋 관리 |
| [Source/Views](Source/Views) | Hierarchy, Inspector, Scene/Game View, 콘텐츠·타일·콘솔 패널 |
| [Source/Rules](Source/Rules) | 편집 판단과 도구 규칙, 템플릿, 복구·파일 쓰기·빌드 요청 |
| [Content](Content) | Editor 자신의 `.gameproject`, 씬, 아이콘과 UI 에셋 |

엔진의 플레이어 진입점은 [Win32PlayerEntry.cpp](../GameEngine/Player/Win32PlayerEntry.cpp), 공통 타깃·스테이징 규칙은 [GameEngineProject.cmake](../cmake/GameEngineProject.cmake)에 있습니다. 프로젝트 에셋과 컴포넌트는 편집 대상 게임이 소유합니다.

## 검증과 한계

Editor 관련 검사는 엔진의 [GameEngineTests](../GameEngineTests/CMakeLists.txt)에 포함됩니다. `EditorSettings`, `EditorDocument`는 설정·Play 복원·컴포넌트 목록·Undo를, `RuntimeObject`는 씬 종료와 UIWindow 직렬화를, `UIEvent`는 UI 그리기·입력 순서를 검사합니다. `ProjectBuilder`와 별도 CTest `ProjectBuildSourceCli`는 패키징·빌드 요청·타깃 출처를 검사합니다. 스키마 배치 검사는 현재 configure에서 선택한 게임 및 Editor의 소스·출력 경로를 사용합니다.
공개 기본 구성의 실제 확인 상태와 전체 절차는 [검증 안내](../Docs/VALIDATION.md)에 있습니다.

```powershell
cmake --build --preset debug
cmake --build --preset release
ctest --test-dir build/vs -C Debug -R "GameEngineTests\.(EditorSettings|EditorDocument|RuntimeObject|UIEvent|ProjectBuilder|ProjectBuildSourceCli)$" --output-on-failure
ctest --test-dir build/vs -C Release -R "GameEngineTests\.(EditorSettings|EditorDocument|RuntimeObject|UIEvent|ProjectBuilder|ProjectBuildSourceCli)$" --output-on-failure
```

모든 구성의 빌드를 끝낸 후 CTest를 실행합니다. 빌드가 콘텐츠와 `Components.schema.json`을 생성·배치하므로 다른 구성의 빌드나 Editor의 Build 메뉴를 동시에 실행하지 않습니다. 소스·헤더 변경 시에는 `cmake --preset vs-no-pch` 다음 `cmake --build --preset no-pch`로 PCH 없이도 빌드되는지 확인할 수 있습니다. 이 구성은 `build/vs-no-pch`에서 빌드하고 `x64-no-pch/Debug/`로 출력하며, 검사는 `ctest --test-dir build/vs-no-pch -C Debug --output-on-failure`로 실행합니다. Ninja 프리셋은 `build/ninja-debug` 또는 `build/ninja-release`, 출력은 `x64-ninja/<Debug|Release>/`를 사용합니다. 소스 파일이 추가되면 사용할 빌드 트리를 다시 configure합니다.

현재 지원 플랫폼은 Windows이며 렌더링은 엔진의 D3D11/D3D12 구현을 사용합니다. 공통 MSVC 설정은 Release `/MT`, Debug `/MTd`입니다. Release 실행에 Visual C++ 런타임을 별도로 설치할 필요가 없도록 빌드하지만 Windows 시스템 DLL과 그래픽 드라이버, 실행 폴더의 콘텐츠는 여전히 필요합니다. 엔진 API, Editor와 테스트가 함께 발전하는 구조이므로 별도 버전의 엔진과 호환된다는 보장은 없습니다.
