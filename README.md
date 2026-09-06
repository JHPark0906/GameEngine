# GameEngine

**C++20으로 작성한 Windows용 2D·3D 게임 엔진**입니다. 렌더링, 객체 수명, 에셋, 입력, 물리, UI와 배포가 하나의 게임 실행 과정으로 연결되는 구조를 학습하기 위해 만들었습니다. 함께 제공하는 GameEditor로 콘텐츠를 편집하고 GameBuilder로 게임을 패키징합니다.

개발 과정에서 생성형 AI의 도움을 받은 프로젝트입니다. 학습을 위해 엔진 구조와 구현을 살펴보고 자유롭게 활용할 수 있도록 제공합니다.

엔진은 `GameEngine` 정적 라이브러리로 빌드됩니다. 게임별 캐릭터·채팅·네트워크 규칙은 소비 프로젝트가 구현하며, 엔진은 재사용할 수 있는 실행 기반과 프로젝트 빌드·콘텐츠 패키징 기능을 제공합니다.

## 프로젝트 관계

| 프로젝트 | 역할 | 주요 의존성 |
| --- | --- | --- |
| **GameEngine** | 게임 실행, 렌더링, 에셋·입력·물리·UI, 공통 빌드 서비스 | Windows SDK |
| **GameEditor** | 프로젝트·씬·에셋 편집과 Play 모드 | GameEngine, 선택한 게임의 C++ 컴포넌트 |
| **GameBuilder** | 프로젝트 컴파일·배포 패키징 CLI | GameEngine의 Build 서비스, CMake와 컴파일러 |

이 저장소에는 **GameEngine, GameEditor, GameBuilder**와 공통 테스트를 포함합니다. SampleGame과 외부 게임·서버 프로젝트는 포함하지 않으며, Summit·ServerCore·SummitServer 없이 기본 엔진과 도구를 빌드할 수 있습니다.

GameEditor와 GameBuilder는 GameEngine 안의 하위 프로젝트이며, 공통 CMake·엔진 API·테스트를 한 저장소에서 같은 버전으로 관리합니다. 도구별 사용법은 [GameEditor README](GameEditor/README.md)와 [GameBuilder README](GameBuilder/README.md)에 있습니다.

## 주요 기능

| 영역 | 구현 내용 |
| --- | --- |
| 런타임 | `Game`·`Scene`·`GameObject`·`Component`, 계층 Transform, 객체 등록과 수명 관리, 컴포넌트 콜백 |
| 렌더링 | Direct3D 11/12, 공통 `RenderFrame`, 스프라이트·타일맵·정적/스킨드 메시·텍스트, 카메라와 가시 영역 컬링 |
| 애니메이션 | 스프라이트 시트와 일반/왕복 재생, 골격·포즈·클립과 `Animator`, rigid FBX 애니메이션 |
| 에셋 | GUID와 파일 내 local ID 참조, `.meta`, 임포터 레지스트리, 참조 기반 로딩과 공유 페이로드 |
| 물리 | 고정 스텝 2D/3D Rigidbody와 박스 충돌, 2D 타일맵 충돌과 위에서 내려올 때만 밟는 발판 |
| 입력·UI | 키보드·마우스, 한글 IME, 캐럿과 Backspace 반복, Canvas·RectTransform·레이아웃·버튼·입력 필드·스크롤·마스킹 |
| 텍스트 | TrueType/OpenType 폰트 처리, 글리프 아틀라스·측정 캐시, 월드/화면 텍스트와 배경 |
| 오디오 | WAV·MP3, `AudioSource`·`AudioListener`, 재생·반복·볼륨 제어와 재생 길이 조회 |
| 프로젝트·배포 | `.gameproject`·씬 직렬화, 컴포넌트 속성/스키마, 프로젝트 아이콘, 디렉터리 또는 실행 파일 내 콘텐츠 패키징 |

Scene View는 편집용 카메라로 장면을 수집하므로 게임 카메라의 컬링 범위에 묶이지 않습니다. 렌더링 정책은 공통 계층에서 결정하고 D3D11/12는 같은 프레임 데이터를 각 API로 제출합니다.

완성된 프레임이 참조하는 동적 UI 픽셀은 후속 갱신으로 바뀌지 않습니다. 마지막 공유 참조가 해제된 CPU 저장 공간만 제한된 풀에서 재사용하고, GPU 업로드 자원은 해당 제출의 완료까지 유지합니다. Canvas·창 계층의 겹침 순서는 입력과 그리기가 함께 사용합니다.

## 빌드 환경

- Windows x64와 Windows SDK.
- MSVC C++20 컴파일러. 기본 `vs` 프리셋은 **Visual Studio 2026**을 지정하므로 해당 생성기를 지원하는 CMake가 필요합니다.
- 소스의 CMake 최소 버전은 **3.28**입니다. Visual Studio 프리셋 대신 MSVC 개발자 PowerShell에서 Ninja 프리셋을 사용할 수도 있습니다.
- 그래픽 실행에는 선택한 Direct3D 백엔드를 지원하는 GPU·드라이버가 필요합니다. Debug 그래픽 검증에는 Direct3D 디버그 레이어가 필요합니다.

소스 구성 단계에서 외부 저장소나 의존성을 자동으로 내려받지 않습니다. 게임 프로젝트는 로컬 경로로 지정합니다.

### 엔진과 도구 빌드

GameEngine 저장소 루트에서 실행합니다. 게임 고유 C++ 컴포넌트를 연결하지 않는 빈 Editor 구성입니다.

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=
cmake --build --preset debug
cmake --build --preset release
```

MSVC 개발자 PowerShell과 Ninja를 사용하는 경우:

```powershell
cmake --preset ninja-debug -DGAMEEDITOR_PROJECT_DIRECTORY=
cmake --build --preset ninja-debug
```

기본 Visual Studio 빌드 결과는 다음 위치에 생성됩니다. Ninja는 `x64-ninja`, no-PCH 구성은 `x64-no-pch`를 사용합니다.

| 대상 | 기본 출력 |
| --- | --- |
| 엔진 라이브러리 | `x64/<Config>/Engine/GameEngine.lib` |
| 에디터 | `x64/<Config>/GameEditor/GameEditor.exe` |
| 빌드 도구 | `x64/<Config>/Tools/GameBuilder.exe` |
| 엔진 테스트 | `x64/<Config>/Tests/GameEngineTests.exe` |
| 선택한 게임 | `x64/<Config>/<게임 이름>/<게임 이름>.exe` |

`<Config>`는 `Debug` 또는 `Release`입니다. `GAMEENGINE_OUTPUT_ROOT`로 출력 루트를 지정할 수 있습니다. 엔진·컴포넌트·도구·게임에는 같은 MSVC 정적 런타임 설정(`/MT`, Debug는 `/MTd`)을 적용합니다. Release 배포본은 별도의 Visual C++ 런타임 설치 의존성을 줄이지만 Windows와 그래픽·미디어 시스템 의존성은 유지합니다.

기존 빌드 트리는 캐시의 출력 경로를 유지합니다. 아래로 확인할 수 있으며, 이후 예제의 `x64`는 그 값으로 바꿔 실행합니다.

```powershell
Select-String -Path build/vs/CMakeCache.txt -Pattern '^GAMEENGINE_OUTPUT_ROOT:'
```

## 게임 프로젝트 연결

게임은 자신의 `Content/`, C++ 소스와 `CMakeLists.txt`를 갖습니다. 엔진 루트 빌드에서 `GAMEEDITOR_PROJECT_DIRECTORY`로 선택한 프로젝트를 추가하고, 프로젝트는 공통 함수 `gameengine_add_game_project`를 호출합니다.

예를 들어 엔진과 같은 부모 디렉터리에 별도로 준비한 `MyGame/`이 있으면:

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=../MyGame
cmake --build --preset debug --target MyGame GameEditor
```

선택한 게임의 C++ 컴포넌트는 게임 실행 파일과 에디터에 정적으로 연결됩니다. 코드를 바꾸면 에디터도 다시 빌드해야 Play 모드에 반영됩니다. 런타임 DLL 핫 리로드는 사용하지 않습니다. 선택 값이 비어 있으면 프로젝트 컴포넌트 없이 에디터를 빌드합니다. 콘텐츠만 있는 프로젝트는 별도 CMake 연결 없이 실행 중 `.gameproject`를 열어 편집할 수 있습니다.

게임 프로젝트 선언의 최소 형태는 다음과 같습니다. 이 파일은 엔진이 `add_subdirectory`로 읽는 파일이며 독립적인 최상위 CMake 구성은 아닙니다.

```cmake
file(GLOB_RECURSE MYGAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

gameengine_add_game_project(MyGame
    CONTENT_DIR "Content"
    SOURCES ${MYGAME_SOURCES})
```

플레이어 진입점은 엔진이 제공합니다. 게임 컴포넌트는 타입과 속성을 선언하고 등록하며, 동일한 정보가 씬 직렬화와 에디터 Inspector에 사용됩니다. [공통 CMake 규칙](cmake/GameEngineProject.cmake)과 [아키텍처](Docs/ARCHITECTURE.md)에 연결 계약을 설명했습니다.

## 콘텐츠와 배포

- `.gameproject`는 프로젝트 이름, 초기 씬, 그래픽 백엔드와 아이콘 등 실행 설정을 담습니다.
- `.scene`은 객체와 컴포넌트를 직렬화합니다. 에셋은 GUID와 필요한 경우 local ID로 참조하며 `.meta`는 그 정체성과 임포트 설정을 보관합니다.
- 콘텐츠는 `IContentSource`를 통해 읽습니다. 디렉터리 배포와 실행 파일에 포함한 콘텐츠가 같은 런타임 경로를 사용합니다.
- 에디터의 Build 명령은 CMake를 다시 구성하고 게임 타깃을 컴파일합니다. GameBuilder는 컴파일한 실행 파일과 콘텐츠를 엔진의 `Build/ProjectBuilder` 서비스에 전달해 배포 패키지를 만듭니다.

GameBuilder는 `--project`의 소스 디렉터리가 CMake에 기록된 게임 타깃의 소스와 일치하는지 빌드 전후에 확인합니다. 다른 작업 사본이거나 메타데이터가 없는 빌드 트리는 패키징하지 않으며, 자동 재구성 후에는 출력 루트도 다시 읽습니다. 자세한 선택·복구 절차는 [GameBuilder 사용법](GameBuilder/README.md)에 있습니다.

일반 빌드는 실행 파일 옆에 콘텐츠와 백엔드 런타임 파일을 배치합니다. 배포할 때는 패키징 결과 전체를 전달합니다. 생성한 패키지, 빌드 트리, 실행 파일과 디버그 심볼은 소스 업로드 대상에서 제외하며 `.gitignore`의 기존 규칙을 따릅니다.

## 검증

해당 구성의 전체 빌드가 끝난 뒤 회귀를 실행합니다. 테스트 중 같은 출력 디렉터리를 쓰는 빌드를 병행하지 않습니다.

```powershell
# 모든 구성을 먼저 빌드합니다. 새 소스가 추가되었다면 두 트리를 모두 configure합니다.
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=
cmake --preset vs-no-pch -DGAMEEDITOR_PROJECT_DIRECTORY=
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset no-pch

ctest --test-dir build/vs -C Debug --output-on-failure -j 4
ctest --test-dir build/vs -C Release --output-on-failure -j 4
ctest --test-dir build/vs-no-pch -C Debug --output-on-failure -j 4

# 필요한 suite만 실행
.\x64\Debug\Tests\GameEngineTests.exe --suite UIEvent
```

테스트는 객체 수명·씬·에셋·물리·UI·애니메이션·오디오·패키징·레이어 경계를 포함합니다. 일부 그래픽 suite는 실제 D3D 장치를 사용합니다. 렌더링 변경은 D3D11/12의 이미지와 디버그 로그를 함께 확인합니다.

기본 구성은 `UIModel` suite를 포함한 엔진·도구의 CTest 28개 항목을 등록합니다. 공개 스냅샷의 실제 검증 결과와 실행 조건은 [검증 안내](Docs/VALIDATION.md)에 기록합니다. 특수한 외부 골격 FBX 검사는 `GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX`로 입력을 명시한 경우에만 추가되며, 기본 빌드에는 필요하지 않습니다.

## 소스 구조

| 경로 | 내용 |
| --- | --- |
| `GameEngine/App/`, `GameEngine/Runtime/` | 부트스트랩·메인 루프, 게임·씬·객체·컴포넌트 |
| `GameEngine/Core/`, `GameEngine/Math/`, `GameEngine/Diagnostics/` | GUID·JSON·문자 인코딩, 수학·plain-float 저장 타입·AABB, 진단 |
| `GameEngine/Assets/`, `GameEngine/Animation/`, `GameEngine/Text/` | 에셋 임포트·관리·리소스 ID·메시 정점, 골격·클립, 폰트 처리 |
| `GameEngine/Platform/` | 운영체제 인터페이스·파일 I/O·상대 경로 검사와 Win32 구현 |
| `GameEngine/Rendering/`, `GameEngine/SceneRendering/` | 프레임 계약·공통 렌더링 정책·D3D 백엔드, 씬에서 프레임으로 변환 |
| `GameEngine/Serialization/`, `GameEngine/Build/` | 씬 직렬화와 프로젝트 패키징 서비스 |
| `GameEngine/UIModel/` | Runtime과 즉시 모드 UI가 공유하는 선택·텍스트 편집 상태 모델 |
| `GameEngine/UI/` | 도구가 사용하는 공통 UI 기반과 텍스트 폭 맞춤 정책 |
| `cmake/` | 컴파일 옵션, 게임 프로젝트 선언, 콘텐츠·셰이더 스테이징 |
| `GameEngineTests/` | 엔진 및 현재 통합된 도구 회귀 |

모듈의 의존 방향과 실행 흐름은 [아키텍처](Docs/ARCHITECTURE.md), 빌드·테스트 절차와 검증 상태는 [검증 안내](Docs/VALIDATION.md)에 있습니다.

## 지원 범위와 개발 구조

현재 플랫폼 구현은 Windows와 D3D11/12입니다. 플랫폼 인터페이스가 있다고 해서 Linux·macOS나 다른 그래픽 API를 지원하는 것은 아닙니다. FBX는 Binary FBX 7.x의 지원하는 메시·애니메이션 범위를 읽으며 morph 등 모든 FBX 기능을 구현하지 않습니다. 물리는 이동하는 박스와 정적 지형 중심으로, 동적 몸체끼리의 반발·질량·마찰을 포함하는 범용 물리 시뮬레이터는 아닙니다.

엔진·에디터·빌더는 별도 CMake 타깃과 소스 디렉터리를 유지하면서 한 저장소에서 함께 변경·검증합니다. `GameEngineTests`에는 에디터의 문서·편집 규칙과 도구·게임 산출물의 연결 검사도 포함됩니다. 루트 CMake는 이 디렉터리 구성을 전제로 하며, 외부 게임은 로컬 소스 경로로 연결합니다. 현재 GameEngine에는 설치 후 `find_package(GameEngine)`로 사용하는 패키지 구성이 없습니다.

## 라이선스

이 저장소에서 제공하는 GameEngine·GameEditor·GameBuilder와 공통 테스트·빌드 스크립트·문서의 프로젝트 고유 부분에는 권리를 보유한 범위에서 [MIT No Attribution (MIT-0)](LICENSE)를 적용합니다. 상업적 이용·수정·재배포를 허용하며, 출처 표시나 수정한 소스의 공개를 요구하지 않습니다. 보증과 책임 제한은 라이선스 원문을 따릅니다.

포함된 제3자 글꼴에는 기존 SIL Open Font License 1.1이 적용됩니다. MIT-0는 이 글꼴의 라이선스를 대체하지 않으며, [제3자 고지](THIRD_PARTY_NOTICES.md)와 글꼴별 원문 고지를 함께 유지합니다.
