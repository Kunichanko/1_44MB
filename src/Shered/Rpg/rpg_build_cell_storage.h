// 役割: build の通常マス保存方式を選択し、方式ごとの生成処理を差し替える。
// 依存する自プロジェクト内ファイル: rpg_stage.h
#ifndef RPG_BUILD_CELL_STORAGE_H
#define RPG_BUILD_CELL_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "rpg_stage.h"
#include "rpg_grid_path.h"

typedef enum RpgBuildCellStorageMode {
    RPG_BUILD_CELL_STORAGE_COMPACT,
    RPG_BUILD_CELL_STORAGE_FOLDERS
} RpgBuildCellStorageMode;

/* 実ファイルの場所と通知は object_folder 側に残し、保存方式はこの小さな境界だけを使う。 */
typedef struct RpgBuildCellStorageBackend {
    void *context;
    bool (*writeCellFolder)(void *context, RpgGridCell cell, int blockType);
    /* Removes the former cell-owned folder before a local update creates its
       replacement.  Composite/reference owners intentionally do not use the
       local-cell route. */
    bool (*removeCellFolder)(void *context, RpgGridCell cell, int blockType);
    bool (*getBuildFilePath)(void *context, const char *fileName, char *path, size_t pathSize);
    void (*notifyBuildFileChanged)(void *context, const char *path);
} RpgBuildCellStorageBackend;

RpgBuildCellStorageMode RpgBuildCellStorage_LoadMode(void);
bool RpgBuildCellStorage_SaveMode(RpgBuildCellStorageMode mode);
void RpgBuildCellStorage_SetMode(RpgBuildCellStorageMode mode);
RpgBuildCellStorageMode RpgBuildCellStorage_GetMode(void);
const char *RpgBuildCellStorage_GetModeName(RpgBuildCellStorageMode mode);
/* Number of preview cells processed by one background build tick.  This is a
 * global editor setting: increasing it shortens Play preparation, while a
 * lower value keeps each editor frame lighter. */
int RpgBuildCellStorage_GetGenerationWorkPerUpdate(void);
void RpgBuildCellStorage_SetGenerationWorkPerUpdate(int count);
bool RpgBuildCellStorage_SaveGenerationWorkPerUpdate(int count);

bool RpgBuildCellStorage_Create(const RpgStage *stage, int startMapIndex,
                                const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_CreatePreview(const RpgStage *stage, int startMapIndex,
                                       const RpgBuildCellStorageBackend *backend);
void RpgBuildCellStorage_Update(const RpgBuildCellStorageBackend *backend);
/* Queue a targeted preview repair without filesystem work in the editor
   input frame. */
bool RpgBuildCellStorage_QueueMap(const RpgStage *stage, int mapIndex,
                                  const RpgBuildCellStorageBackend *backend);
/* The operation-level editor dispatcher uses this for edits proven to affect
   only one compact ordinary cell. */
bool RpgBuildCellStorage_QueueCell(const RpgStage *stage, RpgGridCell cell,
                                   const RpgBuildCellStorageBackend *backend);
/* Queue a self-contained cell replacement.  The old type is retained until
   the worker runs, so a special single-cell folder can be removed before the
   final stage value is materialized. */
bool RpgBuildCellStorage_QueueCellUpdate(const RpgStage *stage, RpgGridCell cell,
                                         int previousBlockType,
                                         const RpgBuildCellStorageBackend *backend);
/* Generate all cells belonging to one connected-world area immediately.
   Used only when the player is about to enter an area that has not yet been
   completed by the incremental preview builder. */
bool RpgBuildCellStorage_EnsureMap(const RpgStage *stage, int mapIndex,
                                   const RpgBuildCellStorageBackend *backend);
/* True only when every cell belonging to this active area has already been
   materialized in the current build session. */
bool RpgBuildCellStorage_IsMapGenerated(const RpgStage *stage, int mapIndex);
/* A targeted preview repair invalidates only the affected area, then calls
   EnsureMap.  This deliberately does not discard other generated areas. */
void RpgBuildCellStorage_InvalidateMap(int mapIndex);
bool RpgBuildCellStorage_RewriteGeneratedMetadata(const RpgStage *stage,
                                                   const RpgBuildCellStorageBackend *backend);
void RpgBuildCellStorage_BeginMetadataBatch(void);
bool RpgBuildCellStorage_EndMetadataBatch(const RpgBuildCellStorageBackend *backend);
void RpgBuildCellStorage_GetGenerationProgress(const RpgStage *stage, int *generatedCells,
                                               int *totalCells, bool *isPending);
/* Targeted compact-cell refresh progress.  This differs from generation
   progress because replacing an already generated cell leaves its generated
   count unchanged. */
bool RpgBuildCellStorage_GetQueuedCellUpdateProgress(int *completedCells, int *totalCells);
void RpgBuildCellStorage_ClearQueuedCellUpdateProgress(void);
/* Per-frame build work only needs this cheap state check; it does not recount
   every cell merely to learn that generation already completed. */
bool RpgBuildCellStorage_IsGenerationPending(void);
/* A preview runtime was detached or discarded.  Do not leave its worker
   pointing at a stage whose output path no longer exists. */
void RpgBuildCellStorage_CancelGeneration(void);
/* Returns the next cell that the background preview builder will process. */
bool RpgBuildCellStorage_GetGenerationTarget(RpgGridCell *cell);
bool RpgBuildCellStorage_EnsureCell(RpgGridCell cell, int blockType,
                                    const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_UsesMetadataForBlock(int blockType);
bool RpgBuildCellStorage_IsMetadataFile(const char *fileName);
bool RpgBuildCellStorage_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend);
/* build 側に残っているセル情報を読み、取り込まれて存在しないセルだけをエラー状態へ反映する。 */
bool RpgBuildCellStorage_ApplyProgressState(RpgStage *stage,
                                            const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_Extract(RpgGridCell cell, int *blockType,
                                 const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_Restore(RpgGridCell cell, int blockType,
                                 const RpgBuildCellStorageBackend *backend);

#endif
