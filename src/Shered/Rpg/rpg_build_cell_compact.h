// 役割: 空気・通常ブロックを一つのメタデータへ圧縮して build に出力する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_storage.h
#ifndef RPG_BUILD_CELL_COMPACT_H
#define RPG_BUILD_CELL_COMPACT_H

#include "rpg_build_cell_storage.h"

bool RpgBuildCellCompact_Create(const RpgStage *stage, const RpgBuildCellStorageBackend *backend);
/* Preview builds create their local neighbourhood first, then use Update to
   finish remaining areas without blocking editor input. */
bool RpgBuildCellCompact_CreatePreview(const RpgStage *stage, int startMapIndex,
                                       const RpgBuildCellStorageBackend *backend);
void RpgBuildCellCompact_Update(const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_EnsureMap(const RpgStage *stage, int mapIndex,
                                   const RpgBuildCellStorageBackend *backend);
/* Applies a completed preview's ordinary-cell changes in one metadata write.
   It intentionally does not create/remove special cell folders. */
bool RpgBuildCellCompact_RewriteGeneratedMetadata(const RpgStage *stage,
                                                  const RpgBuildCellStorageBackend *backend);
/* Several areas can be generated as one operation.  Normal-cell metadata is
   then written once at the end instead of opening cells.csv for each
   area/cell. */
void RpgBuildCellCompact_BeginMetadataBatch(void);
bool RpgBuildCellCompact_EndMetadataBatch(const RpgBuildCellStorageBackend *backend);
void RpgBuildCellCompact_GetGenerationProgress(const RpgStage *stage, int *generatedCells,
                                               int *totalCells, bool *isPending);
bool RpgBuildCellCompact_UsesMetadataForBlock(int blockType);
bool RpgBuildCellCompact_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_Extract(RpgGridCell cell, int *blockType,
                                 const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_Restore(RpgGridCell cell, int blockType,
                                 const RpgBuildCellStorageBackend *backend);

#endif
