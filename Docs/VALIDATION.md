# 검증

이 문서는 공개 소스 스냅샷의 빌드·테스트 절차와 실제 확인 상태를 기록한다. 기본 구성은 `GAMEEDITOR_PROJECT_DIRECTORY`가 빈 값이고 외부 골격 FBX 입력도 지정하지 않은 엔진·도구 구성이다. 외부 게임이나 SampleGame 없이 검증하도록 구성한다.

## 현재 확인 상태

2026-09-06 AABB를 Math로, UndoStack을 GameEditor로 이동한 소스를 SampleGame과 기존 Git 이력이 없는 별도 사본에서 새로 구성·빌드·검증했다. 게임 프로젝트 및 외부 FBX 옵션은 모두 빈 값이었다. 공개 기본 구성은 **27개 CTest**를 등록한다. 엔진 실행 파일의 25개 suite와 `SuiteList`, 별도 CMake·CLI 회귀인 `ProjectBuildSourceCli`의 합계다.

| 구성 | 전체 빌드 | CTest | 테스트 실행 시간 |
| --- | --- | --- | --- |
| Debug | 통과, 컴파일러 경고 없음 | 27/27 통과 | 95.98초 |
| Release | 통과, 컴파일러 경고 없음 | 27/27 통과 | 89.74초 |
| Debug, PCH 비활성 | 통과, 컴파일러 경고 없음 | 27/27 통과 | 97.35초 |

Windows 11 Pro 10.0.26200, Windows SDK 10.0.26100.0, MSVC 19.50.35728.0, CMake 4.2.3-msvc3에서 실행했다. GPU 환경은 NVIDIA GeForce RTX 5060 Ti(드라이버 32.0.15.9597)와 AMD Radeon Graphics(32.0.21043.5001)다. Debug 그래픽 검사에는 Direct3D 디버그 레이어를 사용했다. 소스 사본 바깥의 별도 출력 루트로 세 구성을 모두 빌드한 뒤 CTest를 실행했다. 위 시간은 회귀 테스트 소요 시간이며 렌더링 성능 수치가 아니다.

같은 날짜의 초기 공개 준비에서는 실제 Debug Editor도 D3D11/D3D12 각각 빈 설정에서 시작하고, 창 응답과 정상 종료 코드 0을 확인했다. 종료 시 오류 로그는 없었다. 승인된 닫기 요청은 렌더링 정리가 끝날 때까지 창 표면을 유지한다. `PlayerStartup`에는 별도 스레드·비활성 데스크톱에서 중복 닫기, 미소비 종료, 다음 창에 남는 메시지, 외부 창 파괴와 종료 코드 전달을 검사하는 회귀가 포함된다.

MP3 검사에는 자체 합성한 음원을, 애니메이션·프로젝트 검사에는 자체 생성한 데이터를 사용했다. 초기 공개 준비 때 기존 외부 골격 모델의 선택적 검사도 별도로 통과했으며 그 모델은 공개 소스에 포함하지 않는다.

이번 경계 정리에서는 AABB와 UndoStack의 구현을 유지하고 경로·네임스페이스·소비자 연결을 변경했다. AABB 순수 기하 회귀는 `Math`, UndoStack 회귀는 `EditorDocument` suite에서 검사한다. 엔진은 Editor의 UndoStack 구현을 링크하지 않으며, 에디터와 통합 테스트가 해당 소스를 직접 빌드한다.

## 기본 빌드와 CTest

Windows x64, MSVC C++20 도구와 Windows SDK가 필요하다. `vs` 프리셋은 Visual Studio 2026 생성기를 지원하는 CMake를 사용한다. 루트 CMake의 최소 버전은 3.28이다. 모든 명령은 저장소 루트에서 실행한다.

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY= -DGAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX=
cmake --preset vs-no-pch -DGAMEEDITOR_PROJECT_DIRECTORY= -DGAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX=
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset no-pch

ctest --test-dir build/vs -C Debug -N
ctest --test-dir build/vs -C Debug --output-on-failure -j 4
ctest --test-dir build/vs -C Release --output-on-failure -j 4
ctest --test-dir build/vs-no-pch -C Debug --output-on-failure -j 4
```

CTest는 소스를 빌드하지 않는다. 각 빌드를 끝낸 뒤 테스트를 실행하며, 테스트 중 다른 구성의 빌드·GameBuilder·Editor Build를 실행하지 않는다. 서로 다른 출력 폴더를 쓰더라도 원본 컴포넌트 스키마 등 같은 입력을 갱신할 수 있다. 소스 파일이 추가되면 사용할 모든 트리를 다시 configure한다.

새 VS 트리의 기본 출력은 `x64/<Configuration>/`, PCH 비활성 트리는 `x64-no-pch/Debug/`다. 기존 캐시에서 출력 루트를 바꿨다면 실제 경로를 사용한다.

```powershell
Select-String -Path build/vs/CMakeCache.txt -Pattern "^GAMEENGINE_OUTPUT_ROOT:"
.\x64\Debug\Tests\GameEngineTests.exe --list
.\x64\Debug\Tests\GameEngineTests.exe --suite EditorDocument
```

## 검사 범위와 조건

기본 suite는 씬·컴포넌트 수명, 에셋·직렬화, 물리·애니메이션·오디오, UI, Editor 문서·Undo·Play, 프로젝트 패키징, 셰이더 계약과 실제 D3D11/D3D12 이미지 렌더링을 검사한다. `SuiteList`는 CMake와 실행 파일의 suite 목록이 일치하는지 확인한다.

`ProjectBuildSourceCli`는 실제 CMake와 GameBuilder를 실행해 다른 소스 사본의 거부, 타깃 제거, 빌드 중 소스·출력 경로 변경을 검사한다. `BackendImage`와 `SkinnedMeshRender`는 `ShaderContract`의 셰이더 준비 뒤에 실행되도록 등록된다. 그래픽 검사에는 해당 API를 지원하는 GPU·드라이버가 필요하고, Debug 그래픽 환경에는 Direct3D 디버그 레이어를 준비한다.

UI 픽셀 스냅샷·GPU 업로드 수명·씬 종료 재진입·중첩 UI 순서·Play 복원 회귀는 자동 테스트에 포함된다. 이 검사는 Editor의 모든 메뉴·입력 장치·DPI·사용자 프로젝트를 수동으로 확인한 결과를 대신하지 않는다. 배포 대상 게임은 자신의 콘텐츠와 실제 실행 조건으로 별도 확인한다.

## 선택적 외부 골격 FBX 검사

`GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX`는 **정확히 34개 bone과 5개 clip을 가진 기준 모델**을 검사하는 선택적 통합 입력이다. 임의의 FBX를 주는 범용 성공 검사로 사용하지 않는다. 기본 회귀는 자체 생성 fixture를 사용하며 이 외부 파일을 요구하지 않는다. 기준 모델은 공개 저장소에 포함하거나 자동으로 내려받지 않는다.

해당 계약의 파일을 사용할 권한이 있고 별도로 준비한 경우에만 경로를 지정한다. 예시의 파일 경로는 실제 입력으로 바꾼다.

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY= -DGAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX="../Fixtures/reference-skeletal.fbx"
cmake --build --preset debug
ctest --test-dir build/vs -C Debug -R "^GameEngineTests\.ExternalSkeletalAsset$" --output-on-failure
```

옵션을 지정하면 기본 27개에 `ExternalSkeletalAsset` 한 항목을 추가한다. 경로가 없거나 비어 있는 파일이면 configure가 실패한다. 기본 구성으로 돌아갈 때는 옵션에 빈 값을 명시해 캐시를 지운다.

## 성능 확인

자동 회귀 테스트는 수행 시간의 상한을 성공 조건으로 삼지 않는다. 성능을 비교할 때는 같은 콘텐츠·프레임·백엔드·해상도·빌드 구성으로 측정하고, GPU 동기화와 이미지 읽기 비용의 포함 여부를 기록한다. 이 공개 문서에는 별도 환경이나 비공개 콘텐츠로 측정한 수치를 기본 구성의 결과로 싣지 않는다.
