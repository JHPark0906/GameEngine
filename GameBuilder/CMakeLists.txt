# The command-line project compiler and packager. A console tool, not a game project.
add_executable(GameBuilder BuildToolMain.cpp)
gameengine_apply_common_options(GameBuilder Tools)
target_link_libraries(GameBuilder PRIVATE GameEngine)
