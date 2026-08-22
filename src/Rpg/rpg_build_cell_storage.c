// 役割: 圧縮方式と全マス個別フォルダ方式を選択・保存し、同じ呼び出し口で実行する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_compact.h, rpg_build_cell_folders.h
#include "rpg_build_cell_storage.h"

#include "rpg_build_cell_compact.h"
#include "rpg_build_cell_folders.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

static RpgBuildCellStorageMode currentMode = RPG_BUILD_CELL_STORAGE_COMPACT;

static bool GetConfigPath(char *path, size_t pathSize)
{
    return snprintf(path, pathSize, "%s../assets/Settings/rpg_build_cell_storage.cfg", GetApplicationDirectory()) > 0;
}

RpgBuildCellStorageMode RpgBuildCellStorage_LoadMode(void)
{
    char path[1200]; FILE *file; int mode;
    currentMode = RPG_BUILD_CELL_STORAGE_COMPACT;
    if (!GetConfigPath(path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return currentMode;
    if (fscanf(file, "%d", &mode) == 1 && mode >= RPG_BUILD_CELL_STORAGE_COMPACT && mode <= RPG_BUILD_CELL_STORAGE_FOLDERS)
        currentMode = (RpgBuildCellStorageMode)mode;
    fclose(file);
    return currentMode;
}

bool RpgBuildCellStorage_SaveMode(RpgBuildCellStorageMode mode)
{
    char path[1200]; FILE *file;
    if (mode < RPG_BUILD_CELL_STORAGE_COMPACT || mode > RPG_BUILD_CELL_STORAGE_FOLDERS ||
        !GetConfigPath(path, sizeof(path))) return false;
    file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(file, "%d\n", (int)mode);
    if (fclose(file) != 0) return false;
    currentMode = mode;
    return true;
}

void RpgBuildCellStorage_SetMode(RpgBuildCellStorageMode mode)
{
    if (mode >= RPG_BUILD_CELL_STORAGE_COMPACT && mode <= RPG_BUILD_CELL_STORAGE_FOLDERS) currentMode = mode;
}

RpgBuildCellStorageMode RpgBuildCellStorage_GetMode(void) { return currentMode; }

const char *RpgBuildCellStorage_GetModeName(RpgBuildCellStorageMode mode)
{
    return mode == RPG_BUILD_CELL_STORAGE_FOLDERS ? "All folders" : "Compact cells";
}

bool RpgBuildCellStorage_Create(const RpgStage *stage, int startMapIndex,
                                const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS ?
           RpgBuildCellFolders_Create(stage, startMapIndex, backend) : RpgBuildCellCompact_Create(stage, backend);
}

void RpgBuildCellStorage_Update(const RpgBuildCellStorageBackend *backend)
{
    if (currentMode == RPG_BUILD_CELL_STORAGE_FOLDERS) RpgBuildCellFolders_Update(backend);
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
           strcmp(fileName, "cells_metadata.txt") == 0;
}

bool RpgBuildCellStorage_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_ReadAvailability(available, backend);
}

bool RpgBuildCellStorage_Extract(RpgGridCell cell, int *blockType, const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_Extract(cell, blockType, backend);
}

bool RpgBuildCellStorage_Restore(RpgGridCell cell, int blockType, const RpgBuildCellStorageBackend *backend)
{
    return currentMode == RPG_BUILD_CELL_STORAGE_COMPACT && RpgBuildCellCompact_Restore(cell, blockType, backend);
}
