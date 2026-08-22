// 依存する自プロジェクト内ファイル: rpg_explorer_filesystem.h。
// 役割: Windows Shell のファイル種別・アイコンを一度だけ取得し、raylib Texture としてキャッシュする。
#ifndef RPG_EXPLORER_SHELL_H
#define RPG_EXPLORER_SHELL_H

#include "raylib.h"
#include "rpg_explorer_filesystem.h"

enum { RPG_EXPLORER_ICON_CACHE_CAPACITY = 64 };

typedef struct RpgExplorerShellIcon {
    char key[RPG_EXPLORER_TYPE_LENGTH];
    Texture2D texture;
} RpgExplorerShellIcon;

typedef struct RpgExplorerShellCache {
    RpgExplorerShellIcon icons[RPG_EXPLORER_ICON_CACHE_CAPACITY];
    int count;
    int folderIconSlot;
} RpgExplorerShellCache;

void RpgExplorerShell_ResolveEntries(RpgExplorerShellCache *cache, RpgExplorerFilesystem *filesystem);
Texture2D RpgExplorerShell_GetTexture(const RpgExplorerShellCache *cache, int slot);
Texture2D RpgExplorerShell_GetFolderTexture(const RpgExplorerShellCache *cache);
// Windows Shellの実フォルダアイコンをraylib Textureとして取得する。呼び出し側でキャッシュして使う。
Texture2D RpgExplorerShell_LoadFolderIconTexture(void);
void RpgExplorerShell_Unload(RpgExplorerShellCache *cache);

#endif
