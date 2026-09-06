# 이 모듈이 있는 디렉터리다. 옆의 StageFile.cmake를 부르는 자리들이 이것으로 찾는다.
set(GAMEENGINE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
include("${GAMEENGINE_CMAKE_DIR}/ResolveProjectIcon.cmake")

# Source provenance belongs to the targets registered by this configure. Removed targets must
# not leave a cache entry that could validate an unrelated executable with the same name.
get_property(_gameengine_project_sources_initialized GLOBAL PROPERTY GAMEENGINE_PROJECT_SOURCES_INITIALIZED)
if(NOT _gameengine_project_sources_initialized)
    get_cmake_property(_gameengine_cache_variables CACHE_VARIABLES)
    foreach(_gameengine_variable IN LISTS _gameengine_cache_variables)
        if(_gameengine_variable MATCHES "^GAMEENGINE_PROJECT_SOURCE_")
            unset("${_gameengine_variable}" CACHE)
        endif()
    endforeach()
    set_property(GLOBAL PROPERTY GAMEENGINE_PROJECT_SOURCES_INITIALIZED TRUE)
endif()

# The source is the directory declaring the target, not the engine's top-level source tree.
function(gameengine_record_project_source target)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" project_source)
    set("GAMEENGINE_PROJECT_SOURCE_${target}" "${project_source}" CACHE INTERNAL
        "Source directory compiled into this game target" FORCE)
endfunction()

# Shared build rules for everything in this repository.
#
# A game project is an executable that links the engine, has no entry point of its own, and carries
# a Content directory that must sit beside the executable. Those three facts are stated here once,
# so SampleGame and GameEditor differ only in what they contain.

# Compiler settings shared by every target and the location of its products. The optional
# second argument names the output directory when it differs from the target name:
# the engine library lands in Engine/, the tests in Tests/, and the tools in Tools/.
function(gameengine_apply_common_options target)
    set(output_name "${target}")
    if(ARGC GREATER 1)
        set(output_name "${ARGV1}")
    endif()
    set_target_properties(${target} PROPERTIES GAMEENGINE_OUTPUT_DIRECTORY "${output_name}")
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    if(MSVC)
        # Embed the C/C++ runtime so a distributed game needs no separate VC++ runtime
        # installation. Every linked engine, component, tool and executable uses the same
        # choice; changing only the player would mix /MT and /MD object files.
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        # /sdl enables additional security checks, /permissive- enforces standard conformance,
        # and /utf-8 fixes the source and execution character sets. /MP lets MSBuild generators
        # compile a target's files in parallel. /W4 is the warning level; new warnings need review.
        target_compile_options(${target} PRIVATE /W4 /sdl /permissive- /utf-8 /MP)
    endif()
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}"
        ARCHIVE_OUTPUT_DIRECTORY "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}"
        LIBRARY_OUTPUT_DIRECTORY "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}")
endfunction()

# Removes files absent from the current staging manifest after staging completes.
# Content and RuntimeFiles use separate manifests because their relative-path spaces differ.
# Manifests live in the binary directory, outside STAGED_ROOT, and are configuration-specific.
# RELATIVE_PATHS is recorded at configure time from the staging destination list; pruning
# compares it with the preceding manifest without rescanning source globs.
function(gameengine_prune_stale_staged_files target kind relative_paths output_directory)
    set(manifest_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_${kind}Manifest.txt")
    set(manifest_contents "")
    foreach(relative IN LISTS relative_paths)
        string(APPEND manifest_contents "${relative}\n")
    endforeach()
    file(WRITE "${manifest_file}" "${manifest_contents}")

    # 이전 매니페스트는 구성별로 갈린다: Debug를 한 번도 짓지 않은 트리에서 Release만 지었다면
    # Debug의 스테이징 디렉터리에는 지울 것이 아직 없고, 그 사실은 두 구성이 서로 다른 기억을
    # 가질 때만 참으로 남는다.
    set(previous_manifest
        "${CMAKE_CURRENT_BINARY_DIR}/${target}_${kind}Manifest.previous.$<CONFIG>.txt")

    add_custom_command(TARGET ${target}_${kind} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                -DOUTPUT_DIR=${output_directory}
                -DCURRENT_MANIFEST=${manifest_file}
                -DPREVIOUS_MANIFEST=${previous_manifest}
                -P "${GAMEENGINE_CMAKE_DIR}/PruneStaleFiles.cmake"
        COMMENT "Removing ${kind} staged for ${target} that no longer has a source"
        VERBATIM)
endfunction()

# Stages the shader files an engine-based executable needs beside itself.
# Discover Shaders directories under Rendering and preserve their relative paths so adding
# a backend family's shaders requires no duplicate file list here. GameEngineTests checks
# that every artifact declared by GraphicsBackendDescriptor::GetRuntimeArtifacts is staged.
function(gameengine_stage_runtime_files target)
    get_target_property(output_name ${target} GAMEENGINE_OUTPUT_DIRECTORY)
    set(rendering_root "${GAMEENGINE_SOURCE_DIR}/Rendering")
    file(GLOB_RECURSE shader_files CONFIGURE_DEPENDS "${rendering_root}/*/Shaders/*.*")
    set(staged_files)
    set(relative_paths)
    foreach(shader IN LISTS shader_files)
        file(RELATIVE_PATH relative "${rendering_root}" "${shader}")
        list(APPEND relative_paths "Rendering/${relative}")
        set(staged "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}/Rendering/${relative}")
        add_custom_command(
            OUTPUT "${staged}"
            COMMAND "${CMAKE_COMMAND}" -DSOURCE=${shader} -DDESTINATION=${staged}
                    -P "${GAMEENGINE_CMAKE_DIR}/StageFile.cmake"
            DEPENDS "${shader}"
            COMMENT "Staging ${relative} beside ${target}"
            VERBATIM)
        list(APPEND staged_files "${staged}")
    endforeach()
    add_custom_target(${target}_RuntimeFiles DEPENDS ${staged_files})
    add_dependencies(${target} ${target}_RuntimeFiles)
    gameengine_prune_stale_staged_files(${target} RuntimeFiles "${relative_paths}"
        "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}")
endfunction()

# A project's Content directory, copied beside its executable with the "Content" prefix removed:
# Content/Scenes/Main.scene becomes <exe dir>/Scenes/Main.scene, which is the layout a shipped
# package has. The directory is the truth about what a project contains; nothing lists its files.
function(gameengine_stage_content target content_dir)
    get_target_property(output_name ${target} GAMEENGINE_OUTPUT_DIRECTORY)
    file(GLOB_RECURSE content_files CONFIGURE_DEPENDS "${content_dir}/*")
    set(staged_files)
    set(relative_paths)
    foreach(source IN LISTS content_files)
        file(RELATIVE_PATH relative "${content_dir}" "${source}")
        list(APPEND relative_paths "${relative}")
        set(staged "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}/${relative}")
        add_custom_command(
            OUTPUT "${staged}"
            COMMAND "${CMAKE_COMMAND}" -DSOURCE=${source} -DDESTINATION=${staged}
                    -P "${GAMEENGINE_CMAKE_DIR}/StageFile.cmake"
            DEPENDS "${source}"
            COMMENT "Staging ${relative} beside ${target}"
            VERBATIM)
        list(APPEND staged_files "${staged}")
    endforeach()
    add_custom_target(${target}_Content DEPENDS ${staged_files})
    add_dependencies(${target} ${target}_Content)
    gameengine_prune_stale_staged_files(${target} Content "${relative_paths}"
        "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}")
endfunction()

# Writes the component schema known to the project's executable into its source Content.
# The executable owns the property descriptors; the editor reads the schema when opening
# the project. Staging carries that schema beside the executable in the same build.
# Run the newly linked executable in schema mode before window or graphics initialization,
# so the schema describes the current binary and requires no interactive application.
function(gameengine_emit_component_schema target content_dir)
    get_target_property(output_name ${target} GAMEENGINE_OUTPUT_DIRECTORY)
    set(source_schema "${CMAKE_CURRENT_SOURCE_DIR}/${content_dir}/Components.schema.json")
    set(staged_schema "${GAMEENGINE_OUTPUT_ROOT}/$<CONFIG>/${output_name}/Components.schema.json")
    set_property(TARGET ${target} PROPERTY GAMEENGINE_COMPONENT_SCHEMA_SOURCE "${source_schema}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "$<TARGET_FILE:${target}>" --emit-component-schema "${source_schema}"
        COMMAND "${CMAKE_COMMAND}" -DSOURCE=${source_schema} -DDESTINATION=${staged_schema}
                -P "${GAMEENGINE_CMAKE_DIR}/StageFile.cmake"
        COMMENT "Writing the component schema into ${target}'s content"
        VERBATIM)
endfunction()


# A game project: content plus optional C++ components, never a main.
#
# The player entry point lives in the engine library. The linker only infers wWinMain when it can
# already see it among the project's own objects, so the startup symbol is named explicitly; without
# it the link looks for WinMain instead and fails.
#
# CONTENT_DIR names the project's content directory relative to the project, and defaults to
# Content. It is an argument rather than a fixed name because a project is its own repository and
# decides its own shape: one keeps its scenes under Content/, another keeps them at its root beside
# the .gameproject. The engine reads whichever it is told and imposes neither.
#
# ICON names a .ico file the executable and its window use as their face. When omitted, the
# .gameproject's ProjectSettings.icon GUID is resolved against .ico.meta assets in CONTENT_DIR.
# A project with neither setting builds with whatever icon the platform lends an unmarked
# executable. GameEditor.rc is the shape a project would otherwise have to write by hand for itself
# — a one-line .rc declaring GAMEENGINE_APPLICATION_ICON — so ICON generates that file instead of
# asking every project to carry a near-identical one.
#
# ICON_GUID is the icon asset's own guid, checked against the .meta beside the path ICON names
# (<ICON>.meta's own "guid" field) at configure time. A caller that resolves ICON from a live asset
# database (WriteBuildScript's generated CMakeLists.txt) always has both and gets the check for
# free. GameEditor writes both by hand instead — nothing resolves them for it, since it is what
# runs before there is a CMake build to resolve anything — and ICON_GUID is the reason that hand
# wiring is safe: if GameEditor.ico is ever replaced without updating the guid to match, or the
# guid is edited without updating the path, configure fails and says which file and which value is
# wrong, rather than silently shipping the wrong icon or an icon that quietly stopped being an
# asset. ICON_GUID is optional; without it ICON is trusted as given.
#
# The sources become an OBJECT library as well as the executable, and this is not an arrangement
# detail - it is what keeps component registration working. A project registers its types from a
# static initializer in its own translation unit, and the editor compiles that same code so those
# types exist in the editor process. Object libraries put their objects into whatever links them,
# exactly as compiling the sources directly would. A STATIC library would not: the linker discards
# an object no symbol refers to, and nothing refers to a registration initializer. That build would
# compile and link and simply have no project components in Play mode, which is the kind of failure
# nothing reports.
#
# The name is recorded in a global property so that a build which added a project by path can find
# out what that project called itself.
function(gameengine_add_game_project target)
    cmake_parse_arguments(ARG "" "CONTENT_DIR;ICON;ICON_GUID" "SOURCES" ${ARGN})
    gameengine_record_project_source("${target}")
    if(NOT ARG_CONTENT_DIR)
        set(ARG_CONTENT_DIR "Content")
    endif()
    if(NOT ARG_ICON)
        gameengine_resolve_project_icon(
            "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_CONTENT_DIR}" ARG_ICON ARG_ICON_GUID)
    endif()

    # A content-only project still needs an executable for playing and packaging.
    # CMake requires at least one source, so the target carries an empty translation unit.
    # The engine library provides the entry point; the explicit startup symbol selects it.
    set_property(GLOBAL PROPERTY GAMEENGINE_LAST_GAME_PROJECT "${target}")
    if(NOT ARG_SOURCES)
        message(STATUS "${target}: no sources, so it builds no components of its own.")
    endif()

    set(GAMEENGINE_PLACEHOLDER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${target}_NoProjectCode.cpp")
    file(WRITE "${GAMEENGINE_PLACEHOLDER_SOURCE}"
        "// Generated. A game executable needs one translation unit to exist; everything it runs\n"
        "// lives in the engine library, and a project's own code -- if it has any -- arrives as\n"
        "// an object library linked beside this.\n")

    if(ARG_SOURCES)
        add_library(${target}_Components OBJECT ${ARG_SOURCES})
        gameengine_apply_common_options(${target}_Components "${target}")
        target_link_libraries(${target}_Components PUBLIC GameEngine)
    endif()

    add_executable(${target} WIN32 "${GAMEENGINE_PLACEHOLDER_SOURCE}")
    gameengine_apply_common_options(${target})
    target_link_libraries(${target} PRIVATE GameEngine)
    if(ARG_SOURCES)
        target_link_libraries(${target} PRIVATE ${target}_Components)
    endif()
    if(MSVC)
        target_link_options(${target} PRIVATE /ENTRY:wWinMainCRTStartup)
    endif()

    # ICON은 컴포넌트가 아니라 실행 파일 자신의 얼굴이므로 ARG_SOURCES가 아니라 실행 파일에
    # 직접 붙인다. ARG_SOURCES에 섞으면 "컴포넌트가 있는가"와 "아이콘이 있는가"가 같은 조건을
    # 나눠 쓰게 되는데, 둘은 서로 다른 것을 묻는다 — 컴포넌트 없이 아이콘만 있는 프로젝트가
    # 위의 "no sources" 알림을 잃는 것은 그 알림이 말하려던 사실과 무관하다.
    if(ARG_ICON)
        if(NOT EXISTS "${ARG_ICON}")
            message(FATAL_ERROR "${target}: ICON does not name a file: ${ARG_ICON}")
        endif()

        # ICON_GUID가 있으면 ICON이 실제로 그 guid의 에셋인지 대조한다. 이것이 GameEditor의
        # 손 배선(경로와 guid를 사람이 나란히 적는다)이 안전한 이유다 — 둘 중 하나만 고치면
        # 여기서 configure가 실패하고, 어느 파일의 어느 값이 어긋났는지 말한다.
        if(ARG_ICON_GUID)
            set(icon_meta "${ARG_ICON}.meta")
            if(NOT EXISTS "${icon_meta}")
                message(FATAL_ERROR
                    "${target}: ICON_GUID was given but the icon has no sidecar to check it "
                    "against: ${icon_meta}")
            endif()
            file(READ "${icon_meta}" icon_meta_contents)
            string(JSON icon_meta_guid ERROR_VARIABLE icon_meta_guid_error GET
                "${icon_meta_contents}" guid)
            if(icon_meta_guid_error)
                message(FATAL_ERROR
                    "${target}: could not read \"guid\" from ${icon_meta}: ${icon_meta_guid_error}")
            endif()
            if(NOT icon_meta_guid STREQUAL ARG_ICON_GUID)
                message(FATAL_ERROR
                    "${target}: ICON_GUID does not match the icon's own .meta.\n"
                    "  ICON      = ${ARG_ICON}\n"
                    "  ICON_GUID = ${ARG_ICON_GUID}\n"
                    "  ${icon_meta} says guid = ${icon_meta_guid}\n"
                    "Fix whichever one is stale: the .gameproject's \"icon\" field, or the icon "
                    "file at that path.")
            endif()
        endif()

        set(icon_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_Icon.rc")
        file(WRITE "${icon_rc}"
"#include \"${GAMEENGINE_SOURCE_DIR}/Platform/Win32/Win32IconResource.h\"

// Generated by gameengine_add_game_project's ICON argument. Explorer picks the first icon an .rc
// declares as the executable's face, so this file declares exactly one -- the shape GameEditor.rc
// carries by hand for itself.
GAMEENGINE_APPLICATION_ICON ICON \"${ARG_ICON}\"
")
        target_sources(${target} PRIVATE "${icon_rc}")
        source_group("Generated" FILES "${icon_rc}")
    endif()

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ARG_SOURCES})
    gameengine_stage_runtime_files(${target})
    gameengine_stage_content(${target} "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_CONTENT_DIR}")
    gameengine_emit_component_schema(${target} "${ARG_CONTENT_DIR}")
endfunction()
