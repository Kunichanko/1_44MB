// 依存する自プロジェクト内ファイル: rpg_explorer_filesystem.h, rpg_explorer_shell.h, rpg_explorer_theme.h, rpg_explorer_ui.h, rpg_explorer_window.h。
// 役割: Zipper 専用の自作 Explorer UI を別ウィンドウとして起動し、実フォルダの操作を受け付ける。
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "rpg_explorer_filesystem.h"
#include "rpg_explorer_shell.h"
#include "rpg_explorer_theme.h"
#include "rpg_explorer_ui.h"
#include "rpg_explorer_window.h"

int main(int argumentCount, char **arguments)
{
    const char *root = ".";
    void *ownerWindow = NULL;
    int argumentIndex;
    RpgExplorerFilesystem filesystem = { 0 };
    RpgExplorerShellCache shellCache = { 0 };
    RpgExplorerUiState ui;
    RpgExplorerTheme theme;

    for (argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
        const char *argument = arguments[argumentIndex];
        if (strncmp(argument, "--owner=", 8) == 0) {
            ownerWindow = (void *)(uintptr_t)strtoull(argument + 8, NULL, 16);
        } else {
            root = argument;
        }
    }
    shellCache.folderIconSlot = -1;
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 760, "Zipper - File Explorer");
    SetTargetFPS(60);
    RpgExplorerTheme_Load(&theme, GetWindowHandle());
    RpgExplorerWindow_SetOwner(ownerWindow);
    if (!RpgExplorerWindow_Install(GetWindowHandle(), theme.metrics.tabBarHeight)) {
        RpgExplorerTheme_Unload(&theme);
        CloseWindow();
        return 1;
    }
    /* Register before filesystem initialization so a second open request for
       the same Folder reuses this window instead of creating a duplicate. */
    RpgExplorerWindow_SetRootPath(root);
    RpgExplorerWindow_SetTabBounds(RpgExplorerUi_GetTabBounds());
    // 96-DPI の 1280x760 論理サイズを維持し、125% / 150% では物理ウィンドウも同じ比率で広げる。
    if (!RpgExplorerFilesystem_Initialize(&filesystem, root)) {
        RpgExplorerWindow_Uninstall();
        CloseWindow();
        return 1;
    }
    RpgExplorerShell_ResolveEntries(&shellCache, &filesystem);
    RpgExplorerUi_Initialize(&ui);
    while (!WindowShouldClose()) RpgExplorerUi_UpdateAndDraw(&ui, &filesystem, &shellCache, &theme);
    RpgExplorerFilesystem_Shutdown(&filesystem);
    RpgExplorerShell_Unload(&shellCache);
    RpgExplorerWindow_Uninstall();
    RpgExplorerTheme_Unload(&theme);
    CloseWindow();
    return 0;
}
