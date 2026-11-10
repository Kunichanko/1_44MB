// 依存する自プロジェクト内ファイル: なし。
// 役割: raylib が作成した HWND を Windows/DWM の client-area title bar として統合する。
#ifndef RPG_EXPLORER_WINDOW_H
#define RPG_EXPLORER_WINDOW_H

#include <stdbool.h>

#include "raylib.h"

typedef enum RpgExplorerCaptionButton {
    RPG_EXPLORER_CAPTION_MINIMIZE,
    RPG_EXPLORER_CAPTION_MAXIMIZE,
    RPG_EXPLORER_CAPTION_CLOSE
} RpgExplorerCaptionButton;

bool RpgExplorerWindow_Install(void *nativeWindow, float titleBarHeight);
void RpgExplorerWindow_Uninstall(void);
/* Associates Explorer with the game for keyboard forwarding and conditional
   z-order upkeep.  It intentionally does not change Win32 ownership. */
void RpgExplorerWindow_SetOwner(void *nativeOwner);
/* Registers the Explorer process under its root directory.  The launcher
   uses this identity to reuse an existing virtual Explorer and to request a
   normal close before that directory itself is moved. */
void RpgExplorerWindow_SetRootPath(const char *rootPath);
void RpgExplorerWindow_UpdateOwnerZOrder(void);
void RpgExplorerWindow_SetTabBounds(Rectangle bounds);
void RpgExplorerWindow_SetCaptionButtonWidth(float logicalWidth);
void RpgExplorerWindow_SetCaptionButtonLayout(Rectangle bounds);
Rectangle RpgExplorerWindow_GetCaptionButtonBounds(void);
Rectangle RpgExplorerWindow_GetCaptionButtonRect(RpgExplorerCaptionButton button);
bool RpgExplorerWindow_IsCaptionButtonHovered(RpgExplorerCaptionButton button);
bool RpgExplorerWindow_IsCaptionButtonPressed(RpgExplorerCaptionButton button);
bool RpgExplorerWindow_IsMaximized(void);

#endif
