// 役割: すべてのマスを個別フォルダとして build に出力する従来方式を保持する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_storage.h
#ifndef RPG_BUILD_CELL_FOLDERS_H
#define RPG_BUILD_CELL_FOLDERS_H

#include "rpg_build_cell_storage.h"

bool RpgBuildCellFolders_Create(const RpgStage *stage, int startMapIndex,
                                const RpgBuildCellStorageBackend *backend);
void RpgBuildCellFolders_Update(const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellFolders_EnsureCell(RpgGridCell cell, int blockType,
                                    const RpgBuildCellStorageBackend *backend);

#endif
