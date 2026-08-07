# 依存: src/main.c、src/player.c、src/stage.c
cmake_minimum_required(VERSION 3.20)
project(side_scroller_starter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(raylib 5.0 REQUIRED)

add_executable(side_scroller
    src/main.c
    src/player.c
    src/stage.c
)

target_compile_options(side_scroller PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(side_scroller PRIVATE raylib)

add_executable(side_scroller_editor
    src/editor_main.c
    src/editor_ui.c
    src/file_dialog.c
    src/player.c
    src/stage.c
)

target_compile_options(side_scroller_editor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(side_scroller_editor PRIVATE raylib comdlg32)

set_target_properties(side_scroller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

set_target_properties(side_scroller_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
