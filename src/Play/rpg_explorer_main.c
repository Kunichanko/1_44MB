// 依存する自プロジェクト内ファイル: rpg_explorer_filesystem.h, rpg_explorer_shell.h, rpg_explorer_theme.h, rpg_explorer_ui.h。
// 役割: Zipper 専用の自作 Explorer UI を別ウィンドウとして起動し、実フォルダの操作を受け付ける。
#include <stdio.h>

#include "raylib.h"

#include "rpg_explorer_filesystem.h"
#include "rpg_explorer_shell.h"
#include "rpg_explorer_theme.h"
#include "rpg_explorer_ui.h"

int main(int argumentCount, char **arguments)
{
    const char *root = argumentCount > 1 ? arguments[1] : ".";
    RpgExplorerFilesystem filesystem = { 0 };
    RpgExplorerShellCache shellCache = { 0 };
    RpgExplorerUiState ui;
    RpgExplorerTheme theme;
    shellCache.folderIconSlot = -1;
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 760, "Zipper - File Explorer");
    SetTargetFPS(60);
    RpgExplorerTheme_Load(&theme, GetWindowHandle());
    // 96-DPI の 1280x760 論理サイズを維持し、125% / 150% では物理ウィンドウも同じ比率で広げる。
    SetWindowSize((int)(1280.0f * theme.dpiScale), (int)(760.0f * theme.dpiScale));
    if (!RpgExplorerFilesystem_Initialize(&filesystem, root)) {
        CloseWindow();
        return 1;
    }
    RpgExplorerShell_ResolveEntries(&shellCache, &filesystem);
    RpgExplorerUi_Initialize(&ui);
    while (!WindowShouldClose()) RpgExplorerUi_UpdateAndDraw(&ui, &filesystem, &shellCache, &theme);
    RpgExplorerShell_Unload(&shellCache);
    RpgExplorerTheme_Unload(&theme);
    CloseWindow();
    return 0;
}
