file(GLOB GAMEENGINETESTS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable(GameEngineTests ${GAMEENGINETESTS_SOURCES})
gameengine_apply_common_options(GameEngineTests Tests)
target_link_libraries(GameEngineTests PRIVATE GameEngine)
add_dependencies(GameEngineTests GameBuilder)

# EditorDockLayout의 슬롯 사각형은 창 크기에서 계산되므로 창 없이 검사한다.
# EditorContext의 문서 상태 전이도 창 없이 검사한다. 저장되지 않은 편집을 다루는
# 규칙이 틀리면 작업물을 잃을 수 있으므로, 필요한 에디터 번역 단위를 이 타깃에 연결한다.
target_sources(GameEngineTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorAssetMaintenance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorSettingsStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorPlaySession.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorProjectWatch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorUndoService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorSceneDocument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorFileWrite.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorComponentChoices.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorDockLayout.cpp"
    # 떠 있는 창을 화면 안으로 물리는 계산이다. 도킹 슬롯과 같은 성질이라 같은 자리에 둔다 —
    # 창 없이 재어야, 화면 크기가 바뀔 때 창이 어디로 가는지를 눈이 아니라 수로 답한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorFloatingWindowRect.cpp"
    # 메뉴 막대의 표다. 무엇이 어느 메뉴에 어떤 이름으로 놓이는지만 아는 데이터라 창 없이
    # 돌고, 그 표가 화면의 유일한 근거이므로 표 자체가 재어져야 한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorMenuModel.cpp"
    # 메뉴가 열리고 닫히는 규칙이다. 자리도 폭도 모르므로 창 없이 돌고, 배치가 바뀌어도
    # 그대로 남는다 — 특히 "밖을 누르면 닫힌다"가 모달과 갈리는 자리다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorMenuBarState.cpp"
    # 메뉴 막대와 펼친 목록을 세우는 조립이다. 폭이 글자에서 올라오게 하는 컴포넌트가 여기서
    # 붙으므로, 에디터가 쓰는 이 함수를 시험이 그대로 불러야 조립이 틀린 것이 걸린다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorMenuBarView.cpp"
    # 펼친 목록을 창 안 어디에 놓을지다. 폭을 만들지 않고 배치가 낸 폭을 받아 하한을 씌우고
    # 가장자리로 물리는 일만 하므로 창 없이 재며, 그 하한이 배율을 타는지가 여기서 갈린다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorMenuBarLayout.cpp"
    # 떠 있는 창의 몸통·제목줄 그림·Dock 글자가 그리기 목록에 포함되는지 창 없이 확인한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorFloatingPanelView.cpp"
    # 장면 명령 표다. UI를 모르고 표 하나뿐이라 창 없이 볼 수 있으며, 이것을 보는 시험이
    # 있어야 명령이 파일과 함께 조용히 사라지지 않는다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorSceneCommands.cpp"
    # 컴포넌트 하나를 프로젝트에 써 넣는 템플릿이다. 파일을 쓰는 일뿐이라 창 없이 돌고,
    # 그 결과가 실제로 컴파일되는지는 시험이 configure로 확인한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorScriptTemplate.cpp"
    # 최소 머티리얼 파일 하나를 프로젝트에 써 넣는 템플릿이다. 파일을 쓰는 일뿐이라 창 없이
    # 돈다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorMaterialTemplate.cpp"
    # 장면의 에셋 참조를 세고 정체성으로 옮기는 규칙이다. 파일도 창도 모르고 EditorContext가
    # 이것을 부르므로, 이관 판정 전부가 헤드리스로 재어진다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/SceneReferenceMigration.cpp"
    # 종료해도 되는지 정하는 규칙이다. 창도 UI도 모르므로 창 없이 시험할 수 있고, 이것이
    # 틀리면 사용자가 저장하지 않은 작업을 잃는다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorCloseDecision.cpp"
    # 편집 커맨드다. 저장이 실제로 파일을 쓰는지 재려면 사람이 만드는 것과 같은 편집을
    # 만들어야 하고, 그 길이 이 커맨드다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Document/EditorCommands.cpp"
    # 주인 없는 사이드카를 가려내는 규칙이다. 파일도 창도 모르고 이름만 보므로, 무엇을
    # 치우면 안 되는가를 창 없이 재는 자리가 여기다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/OrphanSidecars.cpp"
    # 에디터의 Build 버튼이 cmake에게 시키는 명령이다. 명령을 만드는 것뿐이라 창도 빌드도
    # 없이 볼 수 있고, 어느 트리를 어느 구성으로 짓는지가 틀리면 빌드는 성공하면서 사람이
    # 연 프로젝트가 아닌 것을 컴파일한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorProjectBuild.cpp"
    # 툴바 버튼 하나를 세우는 조립이다. 에디터가 쓰는 그 함수를 시험이 그대로 불러야, 재는
    # 컴포넌트가 엉뚱한 오브젝트에 붙어 아무것도 재지 못하는 것이 시험에 걸린다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorToolbarButton.cpp"
    # 툴바가 버튼을 줄에 늘어놓고 넘치면 접는 계산이다. 폭 하나에서 사각형이 나오는 순수한
    # 산술이라 창 없이 재고, 그래야 "버튼이 창 밖으로 밀려 사라졌다"를 화면이 아니라 시험이
    # 먼저 말한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorToolbarLayout.cpp"
    # 복구 사본의 경로와 목록 규칙이다. 파일만 알고 창을 모르므로, 쓰는 쪽과 읽는 쪽이 같은
    # 규칙을 보는지를 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorRecovery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorRecoveryPrompt.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorFileDialogs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorProjectCommands.cpp"
    # Play 중 입력을 누가 받는지 정하는 규칙이다. 창도 입력 장치도 모르는 순수 함수라, 잡힌 채
    # 갇히는 상태가 없다는 것을 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorPlayInputFocus.cpp"
    # 묻는 물음들을 한 줄로 세우는 규칙이다. 창도 그리기도 모르므로, 한 번에 하나만 서고 답이
    # 정확히 한 번 온다는 계약을 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorConfirmation.cpp"
    # 물음이 서는 자리다. 셸도 즉시 모드도 모르고 엔진의 UI 컴포넌트만 쓰므로 창 없이 세울 수
    # 있고, 버튼이 겹치거나 창 밖으로 나가는지를 사각형으로 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorConfirmationView.cpp"
    # 툴바가 묻는 글들이다. 창도 버튼도 모르고 글만 만드므로, 무엇을 지우는지 이름으로 적는지를
    # 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorToolbarQuestions.cpp"
    # 다른 장면을 열지 묻는 글이다. 시도가 어떻게 끝났는지에 따라 갈리는지를 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorSceneSwitchQuestions.cpp"
    # 스프라이트 시트를 사이드카에 쓰는 규칙이다. 파일과 JSON만 다루고 창을 모르므로, 왕복이
    # 다른 항목을 보존하는지를 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorSpriteSheetEditing.cpp"
    # 머티리얼의 texture·tint를 그 파일에 쓰는 규칙이다. 위와 같은 이유로 창 없이 잰다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorMaterialEditing.cpp"

    # 패널은 UI 문맥과 좁은 셸 호스트 인터페이스만으로 구성해 창 없이 검사한다.
    # 새 패널 시험은 이 묶음 끝에 추가한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorConsolePanel.cpp"
    # 동봉 폰트를 싣는 로더다. 편집기가 부르는 그 함수를 시험이 그대로 불러야, 「편집기가
    # 청하는 것」과 「시험이 확인하는 것」이 갈리지 않는다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Rules/EditorFonts.cpp"
    # 씬 뷰가 월드 경계를 UI 선으로 옮기는 실제 경로다. 셸이나 창 없이도 패널 호스트와
    # UI 문맥만으로 세울 수 있으므로, 기즈모가 투영한 draw를 RenderFrame에서 확인한다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorSceneViewPanel.cpp"
    # 인스펙터 패널이다. 스프라이트 시트 칸에 가짜 입력으로 타이핑하는 시험이 실제
    # Draw 호출을 불러야, "칸이 그 값을 그렸다"와 "타이핑이 실제로 그 칸의 apply를 부른다"가
    # 갈리지 않는다 — 후자를 흉내 낸 텍스트 필드로 재면 배선 자체가 끊긴 결함을 놓친다.
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source/Views/EditorInspectorPanel.cpp")
target_include_directories(GameEngineTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../GameEditor/Source")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${GAMEENGINETESTS_SOURCES})

# Staged like a real application so RunRuntimeArtifactTests can check the deployment contract
# against this executable's own directory, instead of depending on another project having built.
gameengine_stage_runtime_files(GameEngineTests)

# One CTest test per suite, so a failure names the area that broke and suites can run in
# parallel. The names must match the registry in TestMain.cpp; the SuiteList test below hands
# this list to the executable, which fails if the two disagree — a suite added in C++ but not
# here would otherwise silently never run under CTest.
set(GAMEENGINETESTS_SUITES
    AssetDatabase RenderFrame Physics RenderCache ShaderContract ContentSource
    RuntimeObject Animator Audio AppTiming EditorSettings EditorDocument Diagnostics Math PoseSampler Core Layering UIContext UILayout UIEvent
    ProjectBuilder BackendImage SkinnedMeshRender PlayerStartup FbxSkeletalImport)
foreach(suite IN LISTS GAMEENGINETESTS_SUITES)
    add_test(NAME "GameEngineTests.${suite}"
        COMMAND GameEngineTests --suite "${suite}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:GameEngineTests>")
endforeach()
# ShaderContract precompiles the deployed shader artifacts that BackendImage's and
# SkinnedMeshRender's headless devices load, so the comparison also proves the precompiled
# shaders draw the same image as source compilation.
set_tests_properties("GameEngineTests.BackendImage" "GameEngineTests.SkinnedMeshRender"
    PROPERTIES DEPENDS "GameEngineTests.ShaderContract")
add_test(NAME "GameEngineTests.SuiteList"
    COMMAND GameEngineTests --expect-suites ${GAMEENGINETESTS_SUITES}
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:GameEngineTests>")

add_test(NAME "GameEngineTests.ProjectBuildSourceCli"
    COMMAND "${CMAKE_COMMAND}"
        "-DBUILDER=$<TARGET_FILE:GameBuilder>"
        "-DRUNTIME_ROOT=$<TARGET_FILE_DIR:GameEngineTests>"
        "-DENGINE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ProjectBuildSourceCli-$<CONFIG>"
        "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ProjectBuildSourceCliTests.cmake")

# External reference models are optional integration inputs, never an implicit default dependency.
set(GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX "" CACHE FILEPATH
    "Optional external reference FBX with exactly 34 bones and five clips for integration testing.")
if(GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX)
    get_filename_component(external_skeletal_fixture "${GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX}"
        ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${external_skeletal_fixture}" OR
       IS_DIRECTORY "${external_skeletal_fixture}")
        message(FATAL_ERROR
            "GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX must name a readable FBX file: "
            "${external_skeletal_fixture}")
    endif()
    file(SIZE "${external_skeletal_fixture}" external_skeletal_bytes)
    if(external_skeletal_bytes EQUAL 0)
        message(FATAL_ERROR "GAMEENGINE_TEST_EXTERNAL_SKELETAL_FBX must not be empty.")
    endif()
    add_test(NAME "GameEngineTests.ExternalSkeletalAsset"
        COMMAND GameEngineTests --external-skeletal-fbx "${external_skeletal_fixture}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:GameEngineTests>")
    set_tests_properties("GameEngineTests.ExternalSkeletalAsset" PROPERTIES LABELS "ExternalAssets")
else()
    message(STATUS "GameEngineTests: external skeletal model not configured; generated FBX regressions remain enabled.")
endif()
