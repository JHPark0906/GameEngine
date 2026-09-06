# The editor is a game project with exactly the shape of any other: content, its own code, no main.

# The game project whose components the editor can run.
#
# A game project is its own repository with its own CMakeLists.txt, and the engine reaches it by a
# path on disk. Nothing is fetched: the person building this is working in a checkout they already
# have, and a configure that goes to the network is a configure that fails on a train.
#
# Projects are selected explicitly; the engine and editor do not choose a sibling checkout.
set(GAMEEDITOR_PROJECT_DIRECTORY "" CACHE PATH
    "Game project built and linked into the editor for Play mode. Empty builds without project components.")

# Empty, wrong, and incomplete are three different answers and get three different ones.
#
# Empty means no game project was selected. That is a working
# editor and says so once.
#
# A path that names nothing is a typo or a directory that moved. It would otherwise configure an
# editor with no project components and nothing saying so until Play mode did nothing.
#
# A project directory must provide its own CMakeLists.txt so its sources enter the build.
# Reject an incomplete directory with an actionable diagnostic.
if(GAMEEDITOR_PROJECT_DIRECTORY)
    if(NOT IS_DIRECTORY "${GAMEEDITOR_PROJECT_DIRECTORY}")
        message(FATAL_ERROR
            "GAMEEDITOR_PROJECT_DIRECTORY does not name a directory: ${GAMEEDITOR_PROJECT_DIRECTORY}")
    endif()
    if(NOT EXISTS "${GAMEEDITOR_PROJECT_DIRECTORY}/CMakeLists.txt")
        # 무엇을 고쳐야 하는지까지 말한다. 이 자리에서 사람이 아는 것은 "빌드가 거부했다"뿐이고,
        # 고칠 곳은 프로젝트 파일의 한 필드다 — 그 이름을 말하지 않으면 찾을 방법이 없다.
        message(FATAL_ERROR
            "The game project has no CMakeLists.txt: ${GAMEEDITOR_PROJECT_DIRECTORY}\n"
            "That directory came from the project's .gameproject: its sourceRootPath field says "
            "where the code lives, relative to the .gameproject itself, and defaults to '.' when "
            "the field is absent. If the code is somewhere else, set that field; if the project "
            "has no C++ of its own it needs no CMakeLists.txt and no entry here - the editor "
            "opens it at run time like any other .gameproject.\n"
            "A game project declares itself with gameengine_add_game_project(<name> SOURCES ...).")
    endif()

    # The project is built by this build, wherever it lives. Adding it by path is what makes it an
    # ordinary project rather than a subdirectory of the engine: it declares its own executable, its
    # own content staging and its own component objects, and it does so with the same function
    # whether it sits inside this checkout or beside it.
    add_subdirectory("${GAMEEDITOR_PROJECT_DIRECTORY}" "${CMAKE_BINARY_DIR}/GameProject")
    get_property(GAMEEDITOR_PROJECT_TARGET GLOBAL PROPERTY GAMEENGINE_LAST_GAME_PROJECT)
    if(NOT GAMEEDITOR_PROJECT_TARGET)
        message(FATAL_ERROR
            "The game project's CMakeLists.txt did not declare a project: "
            "${GAMEEDITOR_PROJECT_DIRECTORY}\n"
            "It is expected to call gameengine_add_game_project.")
    endif()
    message(STATUS "GameEditor: Play mode runs the components of ${GAMEEDITOR_PROJECT_TARGET}.")
else()
    set(GAMEEDITOR_PROJECT_TARGET "")
    message(STATUS
        "GameEditor: no game project directory, so Play mode runs without project components.")
endif()

file(GLOB_RECURSE GAMEEDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

# 아이콘은 에셋이다 — Content/Icons/GameEditor.ico와 그 옆의 .meta가 진실이고, guid는
# 여기 손으로 적은 것과 그 .meta가 대조된다(ICON_GUID). GameEditor.gameproject의 "icon"
# 필드도 같은 guid를 적어 둔다: 두 자리가 어긋나면 사람이 옮겨야 한다. CMake 부팅 전이라
# 이 자리를 대신 풀어 줄 살아있는 에셋 데이터베이스가 없어서 손으로 적는다 — WriteBuildScript가
# 다른 프로젝트를 위해 하는 일을 여기서는 사람이 한다. ICON_GUID가 그 위험을 상쇄한다: 경로나
# guid 어느 한쪽만 고치면 configure가 실패하고 어느 파일의 어느 값이 어긋났는지 말한다.
gameengine_add_game_project(GameEditor SOURCES ${GAMEEDITOR_SOURCES}
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/Content/Icons/GameEditor.ico"
    ICON_GUID "41096bbda0724c60a6c61c897fb51656")

# Source는 층(Rules/Document/Views/Shell)별 하위 디렉토리로 나뉘어 있고, 파일들은 서로를
# "Views/EditorConsolePanel.h"처럼 그 하위 디렉토리를 밝혀 include한다 — 엔진 헤더가
# "Runtime/Camera.h"로 자기를 부르는 것과 같은 관례다. 그 include가 풀리려면 Source/ 자체가
# 검색 경로에 있어야 한다: 컴파일러는 인용부호 include를 컴파일 중인 파일 자신의 디렉토리
# 기준으로만 찾고, 다른 층의 파일을 부르는 순간 그 기본 검색으로는 닿지 않는다.
target_include_directories(GameEditor_Components PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

# The tree this editor came out of, so its Build button can configure and compile the project the
# user has open. Nothing at run time can work these out: where the build directory sits is the
# generator's and the preset's decision, and the configuration name appears nowhere in the
# executable. An editor built some other way gets empty strings and says it cannot build, which is
# the truth rather than a wrong guess. The definitions go on GameEditor_Components because that
# is where the editor's own sources compile; the executable itself compiles nothing of it.
target_compile_definitions(GameEditor_Components PRIVATE
    GAMEEDITOR_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    GAMEEDITOR_BINARY_ROOT="${CMAKE_BINARY_DIR}"
    GAMEEDITOR_BUILD_CONFIGURATION="$<CONFIG>")

# The project's components are linked into the editor, so they exist in the editor process and run
# when Play mode ticks the scene. The project registers its own types exactly as it does in its own
# executable: a static initializer among its sources calls RegisterComponentType, and the editor
# adds nothing of its own. Changing the project's components therefore requires rebuilding
# the editor because project code is linked into its executable.
#
# The link is to the object library rather than to a static one, and gameengine_add_game_project
# says why: a static library lets the linker discard the registration initializer, and the build
# that results has no project components with nothing reporting it.
# A project with no C++ of its own declares no component library, and there is nothing to link.
# That is a project the editor opens rather than a build that failed.
if(GAMEEDITOR_PROJECT_TARGET AND TARGET ${GAMEEDITOR_PROJECT_TARGET}_Components)
    target_link_libraries(GameEditor PRIVATE ${GAMEEDITOR_PROJECT_TARGET}_Components)
    target_include_directories(GameEditor PRIVATE "${GAMEEDITOR_PROJECT_DIRECTORY}/Source")
endif()

# The link test must compare the project selected above, including its configuration-specific
# output directory. A schema from an unrelated target cannot identify this editor's components.
# An empty definition explicitly identifies an editor configured without a game project.
if(TARGET GameEngineTests)
    get_target_property(editor_source_schema GameEditor GAMEENGINE_COMPONENT_SCHEMA_SOURCE)
    target_compile_definitions(GameEngineTests PRIVATE
        GAMEENGINE_TEST_EDITOR_SCHEMA="$<TARGET_FILE_DIR:GameEditor>/Components.schema.json"
        GAMEENGINE_TEST_EDITOR_SOURCE_SCHEMA="${editor_source_schema}")
    add_dependencies(GameEngineTests GameEditor)
    if(GAMEEDITOR_PROJECT_TARGET)
        get_target_property(project_source_schema ${GAMEEDITOR_PROJECT_TARGET} GAMEENGINE_COMPONENT_SCHEMA_SOURCE)
        target_compile_definitions(GameEngineTests PRIVATE
            GAMEENGINE_TEST_PROJECT_SCHEMA="$<TARGET_FILE_DIR:${GAMEEDITOR_PROJECT_TARGET}>/Components.schema.json"
            GAMEENGINE_TEST_PROJECT_SOURCE_SCHEMA="${project_source_schema}")
        add_dependencies(GameEngineTests ${GAMEEDITOR_PROJECT_TARGET})
    else()
        target_compile_definitions(GameEngineTests PRIVATE
            GAMEENGINE_TEST_PROJECT_SCHEMA="" GAMEENGINE_TEST_PROJECT_SOURCE_SCHEMA="")
    endif()
endif()
