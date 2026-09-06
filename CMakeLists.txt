cmake_minimum_required(VERSION 3.28)

# The engine, its tests, its tools, and the game projects built on it. One build, one set of
# compiler settings, one place that knows how a project is staged beside its executable.
project(GameEngine LANGUAGES CXX)

# Every target compiles as C++20 without compiler extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build products use <output root>/<Config>/<Target>/, with content beside each executable.
# The configurable output root keeps independently compiled trees from overwriting one
# another. The Visual Studio preset uses x64; no-PCH and Ninja presets use separate roots.
set(GAMEENGINE_OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/x64" CACHE PATH
    "Directory that receives x64/<Config>/<Target>/ build products.")

option(GAMEENGINE_USE_PCH
    "Compile the engine with its precompiled header. Turn it off to check that every file includes what it uses."
    ON)

include(cmake/GameEngineProject.cmake)

# Enable testing before adding the subdirectories that declare CTest tests.
enable_testing()

add_subdirectory(GameEngine)
add_subdirectory(GameEngineTests)
add_subdirectory(GameBuilder)
# GameEditor adds an explicitly selected GAMEEDITOR_PROJECT_DIRECTORY by path. An empty value
# builds the engine, tools and editor without a separate game's content or components.
add_subdirectory(GameEditor)
