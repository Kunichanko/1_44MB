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

add_executable(rpg_version
    src/Rpg/main.c
    src/Rpg/rpg_attachment.c
    src/Rpg/rpg_button_event.c
    src/Rpg/rpg_block_inventory.c
    src/Rpg/rpg_character.c
    src/Rpg/rpg_data_shot.c
    src/Rpg/rpg_dialogue.c
    src/Rpg/rpg_grid_path.c
    src/Rpg/rpg_layout.c
    src/Rpg/rpg_inspect.c
    src/Rpg/rpg_item.c
    src/Rpg/rpg_map_event.c
    src/Rpg/rpg_object_folder.c
    src/Rpg/rpg_preview_event.c
    src/Rpg/rpg_preview_system.c
    src/Rpg/rpg_receiver.c
    src/Rpg/rpg_signal_block.c
    src/Rpg/rpg_stage3_event.c
    src/Rpg/rpg_stage.c
    src/Rpg/rpg_wire.c
    src/Rpg/rpg_zipper.c
    src/Shered/game_font.c
)

target_compile_options(rpg_version PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(rpg_version PRIVATE "-Wl,--stack,8388608")
target_link_libraries(rpg_version PRIVATE raylib shell32)
target_include_directories(rpg_version PRIVATE src/Rpg src/Shered)

set_target_properties(rpg_version PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)

add_executable(rpg_editor
    src/Rpg/editor_main.c
    src/Rpg/rpg_editor_drag.c
    src/Editor/file_dialog.c
    src/Rpg/rpg_attachment.c
    src/Rpg/rpg_button_event.c
    src/Rpg/rpg_block_inventory.c
    src/Rpg/rpg_character.c
    src/Rpg/rpg_data_shot.c
    src/Rpg/rpg_dialogue.c
    src/Rpg/rpg_editor_text.c
    src/Rpg/rpg_grid_path.c
    src/Rpg/rpg_layout.c
    src/Rpg/rpg_inspect.c
    src/Rpg/rpg_item.c
    src/Rpg/rpg_map_event.c
    src/Rpg/rpg_object_folder.c
    src/Rpg/rpg_preview_event.c
    src/Rpg/rpg_preview_system.c
    src/Rpg/rpg_receiver.c
    src/Rpg/rpg_signal_block.c
    src/Rpg/rpg_stage3_event.c
    src/Rpg/rpg_stage.c
    src/Rpg/rpg_wire.c
    src/Rpg/rpg_zipper.c
    src/Shered/game_font.c
)

target_compile_options(rpg_editor PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(rpg_editor PRIVATE "-Wl,--stack,8388608")
target_link_libraries(rpg_editor PRIVATE raylib imm32 comdlg32)
target_include_directories(rpg_editor PRIVATE src/Rpg src/Shered src/Editor)
set_target_properties(rpg_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)
