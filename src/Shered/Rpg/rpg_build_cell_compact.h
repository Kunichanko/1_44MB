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
void RpgBuildCellCompact_Update(const RpgBuildCellStorageBackend *backend, int workBudget);
/* Add one invalidated area to the existing incremental preview queue.  This
   deliberately does not materialise all 240 cells in one editor frame. */
bool RpgBuildCellCompact_QueueMap(const RpgStage *stage, int mapIndex);
/* Queue one already-existing ordinary cell for a compact CSV refresh.  This
   does not invalidate or touch the other cells in its area. */
bool RpgBuildCellCompact_QueueCell(const RpgStage *stage, RpgGridCell cell);
bool RpgBuildCellCompact_QueueCellUpdate(const RpgStage *stage, RpgGridCell cell,
                                         int previousBlockType);
bool RpgBuildCellCompact_EnsureMap(const RpgStage *stage, int mapIndex,
                                   const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_IsMapGenerated(const RpgStage *stage, int mapIndex);
/* Forget only one area's materialization state.  The shared CSV cache stays
   alive; the next EnsureMap republishes that area's physical folders. */
void RpgBuildCellCompact_InvalidateMap(int mapIndex);
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
/* A targeted editor edit rewrites an already materialized cell, so it does
   not change the generated-cell total above.  Expose that short-lived batch
   separately for the progress UI. */
bool RpgBuildCellCompact_GetQueuedCellUpdateProgress(int *completedCells, int *totalCells);
void RpgBuildCellCompact_ClearQueuedCellUpdateProgress(void);
bool RpgBuildCellCompact_IsGenerationPending(void);
/* Detach a discarded preview from its deferred work.  This intentionally
   leaves the generated-state cache alone; Create/CreatePreview initialise it
   before a new build session starts. */
void RpgBuildCellCompact_CancelGeneration(void);
/* Returns the next cell scheduled for incremental preview generation. */
bool RpgBuildCellCompact_GetGenerationTarget(RpgGridCell *cell);
bool RpgBuildCellCompact_UsesMetadataForBlock(int blockType);
bool RpgBuildCellCompact_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_Extract(RpgGridCell cell, int *blockType,
                                 const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellCompact_Restore(RpgGridCell cell, int blockType,
                                 const RpgBuildCellStorageBackend *backend);

#endif
