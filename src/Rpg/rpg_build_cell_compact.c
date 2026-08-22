// 役割: 通常マスを cells_metadata.txt にまとめ、取り込み時だけ個別フォルダへ展開する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_compact.h
#include "rpg_build_cell_compact.h"

#include <stdio.h>
#include <string.h>

enum { RPG_COMPACT_NORMAL_BLOCK_MAX = 10 };

static bool GetMetadataPath(const RpgBuildCellStorageBackend *backend, char *path, size_t pathSize)
{
    return backend != NULL && backend->getBuildFilePath != NULL &&
           backend->getBuildFilePath(backend->context, "cells_metadata.txt", path, pathSize);
}

bool RpgBuildCellCompact_UsesMetadataForBlock(int blockType)
{
    return blockType >= 0 && blockType <= RPG_COMPACT_NORMAL_BLOCK_MAX;
}

bool RpgBuildCellCompact_Create(const RpgStage *stage, const RpgBuildCellStorageBackend *backend)
{
    char path[1200];
    FILE *file;
    if (stage == NULL || !GetMetadataPath(backend, path, sizeof(path))) return false;
    file = fopen(path, "wb");
    if (file == NULL) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        int blockType = stage->blocks[row][column];
        if (RpgBuildCellCompact_UsesMetadataForBlock(blockType)) fprintf(file, "%d %d %d\n", row, column, blockType);
        else if (backend->writeCellFolder == NULL ||
                 !backend->writeCellFolder(backend->context, (RpgGridCell){ row, column }, blockType)) {
            fclose(file);
            return false;
        }
    }
    if (fclose(file) != 0) return false;
    if (backend->notifyBuildFileChanged != NULL) backend->notifyBuildFileChanged(backend->context, path);
    return true;
}

bool RpgBuildCellCompact_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend)
{
    char path[1200], line[96];
    FILE *file;
    if (available == NULL || !GetMetadataPath(backend, path, sizeof(path))) return false;
    memset(available, 0, sizeof(bool) * RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS);
    file = fopen(path, "rb");
    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        int row, column, blockType;
        if (sscanf(line, "%d %d %d", &row, &column, &blockType) == 3 &&
            row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
            RpgBuildCellCompact_UsesMetadataForBlock(blockType)) available[row][column] = true;
    }
    fclose(file);
    return true;
}

bool RpgBuildCellCompact_Extract(RpgGridCell cell, int *blockType, const RpgBuildCellStorageBackend *backend)
{
    char path[1200], temporaryPath[1200], line[96];
    FILE *source, *temporary;
    bool extracted = false;
    if (blockType == NULL || !GetMetadataPath(backend, path, sizeof(path)) ||
        snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", path) <= 0) return false;
    source = fopen(path, "rb");
    temporary = fopen(temporaryPath, "wb");
    if (source == NULL || temporary == NULL) { if (source != NULL) fclose(source); if (temporary != NULL) fclose(temporary); return false; }
    while (fgets(line, sizeof(line), source) != NULL) {
        int row, column, parsedType;
        if (sscanf(line, "%d %d %d", &row, &column, &parsedType) == 3 && row == cell.row && column == cell.column) {
            *blockType = parsedType; extracted = true; continue;
        }
        fputs(line, temporary);
    }
    fclose(source);
    if (fclose(temporary) != 0 || !extracted || remove(path) != 0 || rename(temporaryPath, path) != 0) {
        remove(temporaryPath);
        return false;
    }
    if (backend->notifyBuildFileChanged != NULL) backend->notifyBuildFileChanged(backend->context, path);
    return true;
}

bool RpgBuildCellCompact_Restore(RpgGridCell cell, int blockType, const RpgBuildCellStorageBackend *backend)
{
    char path[1200];
    FILE *file;
    if (!RpgBuildCellCompact_UsesMetadataForBlock(blockType) || !GetMetadataPath(backend, path, sizeof(path))) return false;
    file = fopen(path, "ab");
    if (file == NULL) return false;
    fprintf(file, "%d %d %d\n", cell.row, cell.column, blockType);
    if (fclose(file) != 0) return false;
    if (backend->notifyBuildFileChanged != NULL) backend->notifyBuildFileChanged(backend->context, path);
    return true;
}
