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
/* 実行中の Zipper 構造体のルートを指定する。NULL で既定の設定フォルダへ戻す。 */
void RpgExplorerLauncher_SetZipperDirectory(const char *path);
bool RpgExplorerLauncher_OpenZipperDirectory(void);
bool RpgExplorerLauncher_OpenDirectory(const char *path);

#endif
