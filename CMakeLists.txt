cmake_minimum_required(VERSION 3.20)
project(rpg_version C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(raylib 5.0 REQUIRED)

# 依存方向: Play / Editor -> Shered/Rpg -> Shered/Explorer。
# Explorer補助は本編でもShellアイコン取得に再利用する。
file(GLOB RPG_EXPLORER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/Shered/Explorer/*.c")
add_library(rpg_explorer_shared STATIC ${RPG_EXPLORER_SOURCES})
target_include_directories(rpg_explorer_shared PUBLIC
    src/Shered/Explorer
    src/Shered)
target_compile_options(rpg_explorer_shared PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rpg_explorer_shared PUBLIC raylib shell32 gdi32)

# 共有実装は実行形式ごとに重複させず、ここで一度だけまとめる。
file(GLOB RPG_SHARED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/Shered/Rpg/*.c")

add_library(rpg_shared STATIC
    ${RPG_SHARED_SOURCES}
    src/Shered/game_font.c)
target_include_directories(rpg_shared PUBLIC
    src/Shered/Rpg
    src/Shered
    src/Shered/Explorer)
target_compile_options(rpg_shared PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rpg_shared PUBLIC rpg_explorer_shared)

add_executable(rpg_version WIN32 src/Play/rpg_main.c)
target_compile_options(rpg_version PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(rpg_version PRIVATE "-Wl,--stack,8388608")
target_link_libraries(rpg_version PRIVATE rpg_shared)

add_executable(rpg_editor WIN32
    src/Editor/rpg_editor_main.c
    src/Editor/rpg_editor_drag.c
    src/Editor/rpg_editor_play.c
    src/Editor/rpg_editor_text.c
    src/Editor/file_dialog.c)
target_include_directories(rpg_editor PRIVATE src/Editor)
target_compile_options(rpg_editor PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(rpg_editor PRIVATE "-Wl,--stack,8388608")
target_link_libraries(rpg_editor PRIVATE rpg_shared imm32 comdlg32)

# Explorer風UIはPlayの補助実行形式として分離する。
add_executable(rpg_explorer WIN32
    src/Play/rpg_explorer_main.c)
target_include_directories(rpg_explorer PRIVATE
    src/Shered/Explorer
    src/Shered/Rpg
    src/Shered)
target_compile_options(rpg_explorer PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rpg_explorer PRIVATE rpg_explorer_shared)

set_target_properties(rpg_version rpg_editor rpg_explorer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build")
