# The engine static library.
#
# Sources are discovered from the tree. A file in the source tree is part of the engine;
# the layering test and IWYU build validate its placement and dependencies.
file(GLOB_RECURSE GAMEENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
# Build output directly under the engine directory is never a source.
# (Platform/Win32 is a source directory; only the top-level x64/ and Win32/ are excluded.)
list(FILTER GAMEENGINE_SOURCES EXCLUDE REGEX "^${CMAKE_CURRENT_SOURCE_DIR}/(x64|Win32)/")
file(GLOB_RECURSE GAMEENGINE_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/*/Shaders/*.hlsl")
file(GLOB GAMEENGINE_DOCUMENTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/*/*.md")

add_library(GameEngine STATIC ${GAMEENGINE_SOURCES} ${GAMEENGINE_SHADERS} ${GAMEENGINE_DOCUMENTS})
set_source_files_properties(${GAMEENGINE_SHADERS} ${GAMEENGINE_DOCUMENTS} PROPERTIES HEADER_FILE_ONLY ON)
gameengine_apply_common_options(GameEngine Engine)

# Includes are written from the engine root ("App/Application.h", "Rendering/RenderFrame.h"), for
# the engine itself and for everything that links it.
target_include_directories(GameEngine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The precompiled header is a cache of standard-library headers and nothing else: emptying it must
# not break the build, and -DGAMEENGINE_USE_PCH=OFF is how that is checked.
if(GAMEENGINE_USE_PCH)
    target_precompile_headers(GameEngine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${GAMEENGINE_SOURCES} ${GAMEENGINE_SHADERS} ${GAMEENGINE_DOCUMENTS})

# Where the engine's own sources live, for the staging rules in cmake/GameEngineProject.cmake.
set(GAMEENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Engine source directory")
