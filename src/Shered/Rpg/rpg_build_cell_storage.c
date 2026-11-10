// 役割: 圧縮方式と全マス個別フォルダ方式を選択・保存し、同じ呼び出し口で実行する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_compact.h, rpg_build_cell_folders.h
#include "rpg_build_cell_storage.h"

#include "rpg_build_cell_compact.h"
#include "rpg_build_cell_folders.h"
#include "rpg_block_inventory.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

enum { RPG_BUILD_GENERATION_WORK_MIN = 1, RPG_BUILD_GENERATION_WORK_MAX = 64,
       RPG_BUILD_GENERATION_WORK_DEFAULT = 8 };

static RpgBuildCellStorageMode currentMode = RPG_BUILD_CELL_STORAGE_COMPACT;
static int generationWorkPerUpdate = RPG_BUILD_GENERATION_WORK_DEFAULT;

static bool GetConfigPath(char *path, size_t pathSize)
{
    return snprintf(path, pathSize, "%s../assets/Settings/rpg_build_cell_storage.cfg", GetApplicationDirectory()) > 0;
}

RpgBuildCellStorageMode RpgBuildCellStorage_LoadMode(void)
{
    char path[1200]; FILE *file; int mode, work = RPG_BUILD_GENERATION_WORK_DEFAULT;
    currentMode = RPG_BUILD_CELL_STORAGE_COMPACT;
    generationWorkPerUpdate = RPG_BUILD_GENERATION_WORK_DEFAULT;
    if (!GetConfigPath(path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return currentMode;
    (void)fscanf(file, "%d %d", &mode, &work);
    fclose(file);
    if (work < RPG_BUILD_GENERATION_WORK_MIN) work = RPG_BUILD_GENERATION_WORK_MIN;
    if (work > RPG_BUILD_GENERATION_WORK_MAX) work = RPG_BUILD_GENERATION_WORK_MAX;
    generationWorkPerUpdate = work;
    /* 全マスを個別フォルダ化する旧方式は残すが、進行度保存では使用しない。 */
    return currentMode;
}

bool RpgBuildCellStorage_SaveMode(RpgBuildCellStorageMode mode)
{
    char path[1200]; FILE *file;
    if (mode != RPG_BUILD_CELL_STORAGE_COMPACT ||
        !GetConfigPath(path, sizeof(path))) return false;
    file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(file, "%d %d\n", (int)mode, generationWorkPerUpdate);
    if (fclose(file) != 0) return false;
    currentMode = mode;
    return true;
}

void RpgBuildCellStorage_SetMode(RpgBuildCellStorageMode mode)
{
    (void)mode;
    currentMode = RPG_BUILD_CELL_STORAGE_COMPACT;
}

RpgBuildCellStorageMode RpgBuildCellStorage_GetMode(void) { return currentMode; }

const char *RpgBuildCellStorage_GetModeName(RpgBuildCellStorageMode mode)
{
    return mode == RPG_BUILD_CELL_STORAGE_FOLDERS ? "All folders" : "Compact cells";
}

int RpgBuildCellStorage_GetGenerationWorkPerUpdate(void)
{
    return generationWorkPerUpdate;
}

void RpgBuildCellStorage_SetGenerationWorkPerUpdate(int count)
{
    if (count < RPG_BUILD_GENERATION_WORK_MIN) count = RPG_BUILD_GENERATION_WORK_MIN;
    if (count > RPG_BUILD_GENERATION_WORK_MAX) count = RPG_BUILD_GENERATION_WORK_MAX;
    generationWorkPerUpdate = count;
}

bool RpgBuildCellStorage_SaveGenerationWorkPerUpdate(int count)
{
    RpgBuildCellStorage_SetGenerationWorkPerUpdate(count);
    return RpgBuildCellStorage_SaveMode(currentMode);
}

bool RpgBuildCellStorage_Create(const RpgStage *stage, int startMapIndex,
                                const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_Create(stage, startMapIndex, backend) : RpgBuildCellCompact_Create(stage, backend);
}

bool RpgBuildCellStorage_CreatePreview(const RpgStage *stage, int startMapIndex,
                                       const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_CreatePreview(stage, startMapIndex, backend) :
           RpgBuildCellCompact_CreatePreview(stage, startMapIndex, backend);
}

void RpgBuildCellStorage_Update(const RpgBuildCellStorageBackend *backend)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS)
        RpgBuildCellFolders_Update(backend, generationWorkPerUpdate);
    else RpgBuildCellCompact_Update(backend, generationWorkPerUpdate);
}

bool RpgBuildCellStorage_QueueMap(const RpgStage *stage, int mapIndex,
                                  const RpgBuildCellStorageBackend *backend)
{
    /* Compact storage is the active mode.  Preserve legacy fallback behavior
       without allowing it to alter the compact editor path. */
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_EnsureMap(stage, mapIndex, backend) :
           RpgBuildCellCompact_QueueMap(stage, mapIndex);
}

bool RpgBuildCellStorage_QueueCell(const RpgStage *stage, RpgGridCell cell,
                                   const RpgBuildCellStorageBackend *backend)
{
    /* Folder storage has no incremental single-cell writer.  It is retained
       solely as a compatibility mode, so use its established immediate path.
       Compact storage—the normal editor mode—keeps the work asynchronous. */
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_EnsureCell(cell,
                                          stage == NULL ? 0 : stage->blocks[cell.row][cell.column],
                                          backend) :
           RpgBuildCellCompact_QueueCell(stage, cell);
}

bool RpgBuildCellStorage_QueueCellUpdate(const RpgStage *stage, RpgGridCell cell,
                                         int previousBlockType,
                                         const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    /* Legacy all-folder storage has no deferred replacement primitive. */
    if (currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS) {
        return RpgBuildCellFolders_EnsureCell(cell, stage->blocks[cell.row][cell.column], backend);
    }
    return RpgBuildCellCompact_QueueCellUpdate(stage, cell, previousBlockType);
}

bool RpgBuildCellStorage_EnsureMap(const RpgStage *stage, int mapIndex,
                                   const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_EnsureMap(stage, mapIndex, backend) :
           RpgBuildCellCompact_EnsureMap(stage, mapIndex, backend);
}

bool RpgBuildCellStorage_IsMapGenerated(const RpgStage *stage, int mapIndex)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_IsMapGenerated(stage, mapIndex) :
           RpgBuildCellCompact_IsMapGenerated(stage, mapIndex);
}

void RpgBuildCellStorage_InvalidateMap(int mapIndex)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS)
        RpgBuildCellFolders_InvalidateMap(mapIndex);
    else
        RpgBuildCellCompact_InvalidateMap(mapIndex);
}

bool RpgBuildCellStorage_RewriteGeneratedMetadata(const RpgStage *stage,
                                                   const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT &&
           RpgBuildCellCompact_RewriteGeneratedMetadata(stage, backend);
}

void RpgBuildCellStorage_BeginMetadataBatch(void)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_COMPACT) RpgBuildCellCompact_BeginMetadataBatch();
}

bool RpgBuildCellStorage_EndMetadataBatch(const RpgBuildCellStorageBackend *backend)
{
    return currentMode != RPG_BUILD_CELL_STORAGE_COMPACT ||
           RpgBuildCellCompact_EndMetadataBatch(backend);
}

void RpgBuildCellStorage_GetGenerationProgress(const RpgStage *stage, int *generatedCells,
                                               int *totalCells, bool *isPending)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS)
        RpgBuildCellFolders_GetGenerationProgress(stage, generatedCells, totalCells, isPending);
    else RpgBuildCellCompact_GetGenerationProgress(stage, generatedCells, totalCells, isPending);
}

bool RpgBuildCellStorage_GetQueuedCellUpdateProgress(int *completedCells, int *totalCells)
{
    if (completedCells != NULL) *completedCells = 0;
    if (totalCells != NULL) *totalCells = 0;
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT &&
           RpgBuildCellCompact_GetQueuedCellUpdateProgress(completedCells, totalCells);
}

void RpgBuildCellStorage_ClearQueuedCellUpdateProgress(void)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_COMPACT)
        RpgBuildCellCompact_ClearQueuedCellUpdateProgress();
}

bool RpgBuildCellStorage_IsGenerationPending(void)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_IsGenerationPending() :
           RpgBuildCellCompact_IsGenerationPending();
}

void RpgBuildCellStorage_CancelGeneration(void)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS)
        RpgBuildCellFolders_CancelGeneration();
    else
        RpgBuildCellCompact_CancelGeneration();
}

bool RpgBuildCellStorage_GetGenerationTarget(RpgGridCell *cell)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_GetGenerationTarget(cell) :
           RpgBuildCellCompact_GetGenerationTarget(cell);
}

bool RpgBuildCellStorage_EnsureCell(RpgGridCell cell, int blockType,
                                    const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS &&
           RpgBuildCellFolders_EnsureCell(cell, blockType, backend);
}

bool RpgBuildCellStorage_UsesMetadataForBlock(int blockType)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_UsesMetadataForBlock(blockType);
}

bool RpgBuildCellStorage_IsMetadataFile(const char *fileName)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && fileName != NULL &&
           strcmp(fileName, "cells.csv") == 0;
}

bool RpgBuildCellStorage_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_ReadAvailability(available, backend);
}

bool RpgBuildCellStorage_ApplyProgressState(RpgStage *stage,
                                            const RpgBuildCellStorageBackend *backend)
{
    bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    if (stage == NULL || currentMode != RPG_BUILD_CELL_STORAGE_COMPACT ||
        !RpgBuildCellCompact_ReadAvailability(available, backend)) return false;
    /* 静的なステージ定義は維持し、build から取り出されて不在のマスだけを実行時の欠損状態にする。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (RpgBuildCellCompact_UsesMetadataForBlock(stage->blocks[row][column]) &&
            !available[row][column])
            RpgStage_SetCellError(stage, row, column, stage->blocks[row][column]);
    }
    return true;
}

bool RpgBuildCellStorage_Extract(RpgGridCell cell, int *blockType, const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_Extract(cell, blockType, backend);
}

bool RpgBuildCellStorage_Restore(RpgGridCell cell, int blockType, const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_Restore(cell, blockType, backend);
}
