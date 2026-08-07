# 依存: src/Play/main.c、src/Editor/main.c、src/Editor/editor_ui.c、src/Editor/file_dialog.c、src/Shered/enemy.c、src/Shered/enemy_group.c、src/Shered/enemy_settings.c、src/Shered/game_font.c、src/Shered/player.c、src/Shered/stage.c
cmake_minimum_required(VERSION 3.20)
project(side_scroller_starter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(raylib 5.0 REQUIRED)

add_executable(side_scroller
    src/Play/main.c
    src/Shered/enemy.c
    src/Shered/enemy_group.c
    src/Shered/enemy_settings.c
    src/Shered/game_font.c
    src/Shered/player.c
    src/Shered/stage.c
)

target_compile_options(side_scroller PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(side_scroller PRIVATE raylib)
target_include_directories(side_scroller PRIVATE src/Shered)

add_executable(side_scroller_editor
    src/Editor/main.c
    src/Editor/editor_ui.c
    src/Editor/file_dialog.c
    src/Shered/enemy.c
    src/Shered/enemy_group.c
    src/Shered/enemy_settings.c
    src/Shered/game_font.c
    src/Shered/player.c
    src/Shered/stage.c
)

target_compile_options(side_scroller_editor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(side_scroller_editor PRIVATE raylib comdlg32)
target_include_directories(side_scroller_editor PRIVATE src/Editor src/Shered)

set_target_properties(side_scroller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)

set_target_properties(side_scroller_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)
