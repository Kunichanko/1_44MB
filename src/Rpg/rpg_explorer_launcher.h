// 依存する自プロジェクト内ファイル: なし
// 役割: Zipper フォルダを仮想 Explorer または Windows Explorer で開く起動先設定を管理する。
#ifndef RPG_EXPLORER_LAUNCHER_H
#define RPG_EXPLORER_LAUNCHER_H

#include <stdbool.h>

typedef enum RpgExplorerMode {
    RPG_EXPLORER_MODE_VIRTUAL = 0,
    RPG_EXPLORER_MODE_WINDOWS = 1
} RpgExplorerMode;

RpgExplorerMode RpgExplorerLauncher_LoadMode(void);
bool RpgExplorerLauncher_SaveMode(RpgExplorerMode mode);
bool RpgExplorerLauncher_OpenZipperDirectory(void);

#endif
