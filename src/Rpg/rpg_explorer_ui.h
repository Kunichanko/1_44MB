// 依存する自プロジェクト内ファイル: rpg_explorer_filesystem.h, rpg_explorer_shell.h, rpg_explorer_theme.h。
// 役割: Windows 11 Explorer 風の自作表示・入力を担当し、ファイル処理を filesystem モジュールへ委譲する。
#ifndef RPG_EXPLORER_UI_H
#define RPG_EXPLORER_UI_H

#include "rpg_explorer_filesystem.h"
#include "rpg_explorer_shell.h"
#include "rpg_explorer_theme.h"

typedef struct RpgExplorerUiState {
    int selectedEntry;
    int draggedEntry;
    int listScroll;
    int treeScroll;
    bool isDragging;
    Vector2 dragStart;
    double lastClickTime;
    int lastClickEntry;
    char status[128];
} RpgExplorerUiState;

void RpgExplorerUi_Initialize(RpgExplorerUiState *state);
void RpgExplorerUi_UpdateAndDraw(RpgExplorerUiState *state, RpgExplorerFilesystem *filesystem,
                                 RpgExplorerShellCache *shellCache, RpgExplorerTheme *theme);

#endif
