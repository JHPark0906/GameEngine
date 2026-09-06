# Removes staged files that a previous build put beside an executable but this build no longer
# stages, and forgets nothing else.
#
# Run with `cmake -DOUTPUT_DIR=<dir> -DCURRENT_MANIFEST=<file> -DPREVIOUS_MANIFEST=<file>
# -P PruneStaleFiles.cmake`.
#
# gameengine_stage_content and gameengine_stage_runtime_files each add or update one staged file
# per source, through StageFile.cmake — never remove one. Delete or rename a source file and its
# old staged copy is orphaned beside the executable forever, surviving every later rebuild, because
# nothing looks at "what's there now that shouldn't be". That is what this script is for.
#
# It does not do that by mirroring OUTPUT_DIR against the current source glob: OUTPUT_DIR holds
# files this project never staged and must never touch — a settings file and a log the running
# executable writes for itself, per-sprite metadata the editor writes when it imports an asset.
# None of those come from a source glob, so a "delete what the glob doesn't name" pass would delete
# them the first time it ran. What it does instead is remember, in PREVIOUS_MANIFEST, exactly which
# relative paths *this staging step* put there last time, and delete only the ones that dropped out
# of CURRENT_MANIFEST since. A file this script never staged is never in that memory, so it is
# never a candidate for removal, no matter what else lives beside it in OUTPUT_DIR.
#
# CURRENT_MANIFEST and PREVIOUS_MANIFEST are text files, one relative path per line (the same
# relative paths gameengine_stage_content/gameengine_stage_runtime_files hand to StageFile.cmake as
# DESTINATION, made relative to OUTPUT_DIR). CURRENT_MANIFEST is written by the caller before this
# runs; PREVIOUS_MANIFEST is this script's own bookkeeping; it does not exist yet on a clean build,
# which is not a failure - there is simply nothing yet to prune.
if(NOT DEFINED OUTPUT_DIR OR NOT DEFINED CURRENT_MANIFEST OR NOT DEFINED PREVIOUS_MANIFEST)
    message(FATAL_ERROR
        "PruneStaleFiles.cmake needs OUTPUT_DIR, CURRENT_MANIFEST and PREVIOUS_MANIFEST.")
endif()

if(NOT EXISTS "${CURRENT_MANIFEST}")
    message(FATAL_ERROR "PruneStaleFiles.cmake needs an existing CURRENT_MANIFEST: "
        "${CURRENT_MANIFEST} does not exist.")
endif()

set(current_paths)
file(STRINGS "${CURRENT_MANIFEST}" current_paths)

get_filename_component(output_dir_abs "${OUTPUT_DIR}" ABSOLUTE)
string(TOLOWER "${output_dir_abs}" output_dir_lower)
string(LENGTH "${output_dir_lower}" output_dir_lower_length)

if(EXISTS "${PREVIOUS_MANIFEST}")
    set(previous_paths)
    file(STRINGS "${PREVIOUS_MANIFEST}" previous_paths)

    foreach(relative IN LISTS previous_paths)
        if(NOT relative STREQUAL "" AND NOT relative IN_LIST current_paths)
            set(stale "${output_dir_abs}/${relative}")
            if(EXISTS "${stale}")
                file(REMOVE "${stale}")
                message(STATUS "Pruned stale staged file: ${relative}")
            endif()

            # 지운 파일이 남긴 빈 디렉터리를 위로 걷으며 치운다. OUTPUT_DIR 자신은 결코 지우지
            # 않는다 - 그 상위 표현은 이 스크립트가 만든 것이 아니고, 이 프로젝트가 그 자리에
            # 아무것도 스테이징하지 않은 빌드 구성일 수도 있다(예: 아직 한 번도 짓지 않은 구성).
            get_filename_component(walk "${stale}" DIRECTORY)
            while(NOT walk STREQUAL "")
                get_filename_component(walk_abs "${walk}" ABSOLUTE)
                string(TOLOWER "${walk_abs}" walk_lower)
                if(walk_lower STREQUAL output_dir_lower)
                    break()
                endif()
                string(LENGTH "${walk_lower}" walk_lower_length)
                if(walk_lower_length LESS output_dir_lower_length)
                    break()
                endif()
                string(SUBSTRING "${walk_lower}" 0 ${output_dir_lower_length} walk_prefix)
                if(NOT walk_prefix STREQUAL output_dir_lower)
                    # OUTPUT_DIR 밖이다: 다른 매니페스트가 관리하는 트리이거나 아예 무관한
                    # 자리이므로, 여기서 멈춘다.
                    break()
                endif()
                if(NOT IS_DIRECTORY "${walk_abs}")
                    break()
                endif()
                file(GLOB walk_children "${walk_abs}/*")
                if(walk_children)
                    break()
                endif()
                file(REMOVE_RECURSE "${walk_abs}")
                get_filename_component(walk "${walk_abs}" DIRECTORY)
            endwhile()
        endif()
    endforeach()
endif()

# 다음 빌드가 견줄 상대를 남긴다. StageFile.cmake와 같은 이유로 임시 이름에 쓰고 이름을
# 바꾼다 - 이 파일을 읽는 다음 빌드와, 쓰는 이번 빌드가 겹칠 수 있는 것은 아니지만(같은 타깃을
# 두 번 빌드하는 프로세스는 하나뿐이다), 절반만 쓰인 매니페스트를 남기고 끝나는 것보다는 그 편이
# 값싸고 한결같다.
set(staging_path "${PREVIOUS_MANIFEST}.staging")
file(WRITE "${staging_path}" "")
foreach(relative IN LISTS current_paths)
    file(APPEND "${staging_path}" "${relative}\n")
endforeach()
file(RENAME "${staging_path}" "${PREVIOUS_MANIFEST}" RESULT rename_failure)
if(rename_failure)
    file(REMOVE "${staging_path}")
    message(FATAL_ERROR "Could not put ${PREVIOUS_MANIFEST} in place: ${rename_failure}")
endif()
