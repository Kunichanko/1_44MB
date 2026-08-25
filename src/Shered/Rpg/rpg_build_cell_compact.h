// 役割: 空気・通常ブロックを一つのメタデータへ圧縮して build に出力する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_storage.h
#ifndef RPG_BUILD_CELL_COMPACT_H
#define RPG_BUILD_CELL_COMPACT_H

#include "rpg_build_cell_storage.h"

bool RpgBuildCellCompact_Create(const RpgStage *stage, const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_UsesMetadataForBlock(int blockType);
bool RpgBuildCellCompact_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_Extract(RpgGridCell cell, int *blockType,
                                 const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_Restore(RpgGridCell cell, int blockType,
                                 const RpgBuildCellStorageBackend *backend);

#endif
