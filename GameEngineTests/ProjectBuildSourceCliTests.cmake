cmake_minimum_required(VERSION 3.28)

foreach(required BUILDER RUNTIME_ROOT ENGINE_ROOT TEST_ROOT TEST_GENERATOR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required} for the GameBuilder source provenance test")
    endif()
endforeach()
if(NOT EXISTS "${BUILDER}" OR NOT IS_DIRECTORY "${RUNTIME_ROOT}/Rendering")
    message(FATAL_ERROR "Build GameBuilder and GameEngineTests runtime artifacts before this test")
endif()

# Every run owns a new directory, including when two configurations are tested concurrently.
string(RANDOM LENGTH 20 ALPHABET 0123456789abcdef nonce)
set(workspace "${TEST_ROOT}-${nonce}")
set(first "${workspace}/first checkout/SameGame")
set(second "${workspace}/second checkout/SameGame")
set(tree "${workspace}/build")
set(output "${workspace}/package")
set(marker "${tree}/build-ran")
file(MAKE_DIRECTORY "${workspace}/engine" "${first}/Content/Scenes" "${second}/Content/Scenes" "${output}")
file(WRITE "${output}/Keep.txt" "previous package")

set(top_source [=[
cmake_minimum_required(VERSION 3.28)
project(BuilderSourceFixture NONE)
include("@ENGINE_ROOT@/cmake/GameEngineProject.cmake")
set(GAMEENGINE_OUTPUT_ROOT "@workspace@/products" CACHE PATH "" FORCE)
if(INCLUDE_GAME)
    add_subdirectory("${GAME_SOURCE}" "${CMAKE_BINARY_DIR}/game")
endif()
]=])
string(CONFIGURE "${top_source}" top_source @ONLY)
file(WRITE "${workspace}/engine/CMakeLists.txt" "${top_source}")
set(game_source [=[
gameengine_record_project_source(SameGame)
file(MAKE_DIRECTORY "${GAMEENGINE_OUTPUT_ROOT}/Debug/SameGame")
file(WRITE "${GAMEENGINE_OUTPUT_ROOT}/Debug/SameGame/SameGame.exe"
    "player from ${CMAKE_CURRENT_SOURCE_DIR}, output ${GAMEENGINE_OUTPUT_ROOT}")
add_custom_target(SameGame
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/build-ran"
    VERBATIM)
]=])
foreach(project IN ITEMS "${first}" "${second}")
    file(WRITE "${project}/CMakeLists.txt" "${game_source}")
    file(WRITE "${project}/Content/SameGame.gameproject"
        "{\"projectName\":\"SameGame\",\"initialSceneId\":0,\"window\":{\"width\":320,\"height\":240},"
        "\"graphicsApi\":\"D3D11\",\"scenes\":[{\"id\":0,\"path\":\"Scenes/Main.scene\"}]}")
    file(WRITE "${project}/Content/Scenes/Main.scene" "{\"sceneName\":\"Main\",\"gameObjects\":[]}")
endforeach()

function(configure_game enabled)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${workspace}/engine" -B "${tree}"
        -G "${TEST_GENERATOR}" "-DGAME_SOURCE=${first}" "-DINCLUDE_GAME=${enabled}"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Fixture configure failed: ${result}\n${stdout}\n${stderr}")
    endif()
endfunction()

function(wait_for_source_timestamp)
    # Filesystems with two-second modification-time resolution must observe the source edit
    # after the generated build files, or CMake legitimately sees no regeneration to perform.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2.1 COMMAND_ERROR_IS_FATAL ANY)
endfunction()

function(run_builder project expected_success expected_message expect_build)
    execute_process(COMMAND "${BUILDER}"
        --project "${project}" --build-dir "${tree}" --configuration Debug
        --target-name SameGame --output "${output}" --cmake "${CMAKE_COMMAND}" --issue-identities
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(expected_success)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "Matching project must package successfully: ${result}\n${stdout}\n${stderr}")
        endif()
    else()
        if("${result}" STREQUAL "0" OR NOT "${stdout}\n${stderr}" MATCHES "${expected_message}")
            message(FATAL_ERROR "Expected provenance rejection: ${result}\n${stdout}\n${stderr}")
        endif()
        file(READ "${output}/Keep.txt" kept)
        if(NOT kept STREQUAL "previous package")
            message(FATAL_ERROR "Rejected source selection changed the previous package")
        endif()
    endif()
    if(expect_build AND NOT EXISTS "${marker}")
        message(FATAL_ERROR "The accepted source did not reach the compiler target")
    elseif(NOT expect_build AND EXISTS "${marker}")
        message(FATAL_ERROR "The compiler target ran before rejecting source provenance")
    endif()
endfunction()

configure_game(ON)
file(COPY "${RUNTIME_ROOT}/Rendering" DESTINATION "${workspace}/products/Debug/SameGame")
run_builder("${second}" FALSE "different directory|file identity comparison failed" FALSE)
if(EXISTS "${second}/Content/SameGame.gameproject.meta")
    message(FATAL_ERROR "Rejected worktree selection must not issue source asset identities")
endif()

# The top-level CMake source is engine/, but the target belongs to first checkout/SameGame.
# A spelling alias still identifies that same directory and must pass both provenance checks.
run_builder("${first}/../SameGame/." TRUE "" TRUE)
if(NOT EXISTS "${marker}" OR EXISTS "${output}/Keep.txt" OR
   NOT EXISTS "${output}/SameGame.gameproject" OR NOT EXISTS "${output}/Scenes/Main.scene")
    message(FATAL_ERROR "The matching source did not compile and publish the complete package")
endif()
file(READ "${output}/SameGame.exe" player)
if(NOT player STREQUAL "player from ${first}, output ${workspace}/products")
    message(FATAL_ERROR "The published executable came from a different source directory")
endif()
file(REMOVE "${marker}")
string(TOUPPER "${first}" uppercase_first)
run_builder("${uppercase_first}" TRUE "" TRUE)
file(REMOVE "${marker}")
file(WRITE "${output}/Keep.txt" "previous package")

# Reconfiguration removes the game. A stale source entry and executable must not validate it.
configure_game(OFF)
run_builder("${first}" FALSE "no source metadata" FALSE)

# A cache written without target metadata also fails before build or publication.
configure_game(ON)
file(READ "${tree}/CMakeCache.txt" complete_cache)
# Rename only the key so its help block, value and line endings remain a valid cache entry.
string(REPLACE "GAMEENGINE_PROJECT_SOURCE_SameGame:INTERNAL="
    "TEST_REMOVED_PROJECT_SOURCE_SameGame:INTERNAL=" cache "${complete_cache}")
if(cache STREQUAL complete_cache)
    message(FATAL_ERROR "The fixture could not rename the target source metadata")
endif()
file(WRITE "${tree}/CMakeCache.txt" "${cache}")
run_builder("${first}" FALSE "no source metadata" FALSE)

# Regeneration may change only the output layout. Read the completed build's output root,
# even when an executable from the same source is still present at the preceding location.
configure_game(ON)
wait_for_source_timestamp()
string(REPLACE "${workspace}/products" "${workspace}/new-products" moved_source "${top_source}")
file(WRITE "${workspace}/engine/CMakeLists.txt" "${moved_source}")
file(MAKE_DIRECTORY "${workspace}/new-products/Debug/SameGame")
file(COPY "${RUNTIME_ROOT}/Rendering" DESTINATION "${workspace}/new-products/Debug/SameGame")
run_builder("${first}" TRUE "" TRUE)
file(READ "${output}/SameGame.exe" moved_player)
if(NOT moved_player STREQUAL "player from ${first}, output ${workspace}/new-products")
    message(FATAL_ERROR "The builder packaged the stale executable after the output root changed")
endif()
file(REMOVE "${marker}")
file(WRITE "${output}/Keep.txt" "previous package")

# The build can trigger CMake's regeneration after the preflight check. Changing the source
# then must still be rejected before the newly built executable replaces the previous package.
configure_game(ON)
wait_for_source_timestamp()
string(REPLACE "if(INCLUDE_GAME)" "set(GAME_SOURCE \"${second}\")\nif(INCLUDE_GAME)"
    switched_source "${top_source}")
file(WRITE "${workspace}/engine/CMakeLists.txt" "${switched_source}")
run_builder("${first}" FALSE "different directory|file identity comparison failed" TRUE)

file(REMOVE_RECURSE "${workspace}")
message(STATUS "GameBuilder source provenance CLI regression passed")
