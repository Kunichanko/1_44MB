// 役割: 通常マスをステージ全体の cells.csv にまとめ、取り込み時だけ個別フォルダへ展開する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_compact.h
#include "rpg_build_cell_compact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RPG_COMPACT_NORMAL_BLOCK_MAX = 10 };
/* Normal cells live together in one metadata file.  Keep preview work small
   enough for the editor, but do not turn one file into thousands of open /
   append / close operations. */
enum { RPG_COMPACT_CELL_BATCH_SIZE = 24, RPG_COMPACT_FLUSH_CELL_INTERVAL = 120 };

static const RpgStage *pendingStage = NULL;
static bool generatedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
static int pendingCursor = 0;
static int startingMap = 0;
static bool metadataCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
/* False metadataCells has two meanings: an intentionally absent captured
   normal cell (blank CSV field), or a broken cell (-1).  Keep them distinct. */
static bool metadataBrokenCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
static int metadataBlockTypes[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { 0 } };
static bool metadataCacheActive = false;
static bool metadataDirty = false;
static int metadataWritesSinceFlush = 0;
static unsigned int metadataBatchDepth = 0;

static bool GetMetadataPath(const RpgBuildCellStorageBackend *backend, char *path, size_t pathSize)
{
    return backend != NULL && backend->getBuildFilePath != NULL &&
           backend->getBuildFilePath(backend->context, "cells.csv", path, pathSize);
}

bool RpgBuildCellCompact_UsesMetadataForBlock(int blockType)
{
    return blockType >= 0 && blockType <= RPG_COMPACT_NORMAL_BLOCK_MAX;
}

static void NotifyMetadataChanged(const RpgBuildCellStorageBackend *backend);

static bool FlushMetadata(const RpgBuildCellStorageBackend *backend)
{
    char path[1200], legacyPath[1200];
    FILE *file;
    if (!metadataDirty) return true;
    if (!GetMetadataPath(backend, path, sizeof(path))) return false;
    file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs("y/x", file);
    for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) fprintf(file, ",%d", column);
    fputc('\n', file);
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        fprintf(file, "%d", row);
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            const int type = metadataBlockTypes[row][column];
            if (metadataCells[row][column]) fprintf(file, ",%d", type);
            else if (metadataBrokenCells[row][column] || type == RPG_BLOCK_BUILD_MISSING) fputs(",-1", file);
            else if (type > RPG_COMPACT_NORMAL_BLOCK_MAX) fprintf(file, ",%d", type);
            else fputc(',', file); /* Captured normal cell: deliberately blank. */
        }
        fputc('\n', file);
    }
    if (fclose(file) != 0) return false;
    /* The former sparse list must not remain beside the canonical stage grid. */
    if (backend->getBuildFilePath != NULL &&
        backend->getBuildFilePath(backend->context, "cells_metadata.txt", legacyPath, sizeof(legacyPath)))
        (void)remove(legacyPath);
    metadataDirty = false;
    metadataWritesSinceFlush = 0;
    NotifyMetadataChanged(backend);
    return true;
}

static void NotifyMetadataChanged(const RpgBuildCellStorageBackend *backend)
{
    char path[1200];
    if (backend != NULL && backend->notifyBuildFileChanged != NULL &&
        GetMetadataPath(backend, path, sizeof(path)))
        backend->notifyBuildFileChanged(backend->context, path);
}

static bool WriteCell(const RpgStage *stage, RpgGridCell cell,
                      const RpgBuildCellStorageBackend *backend)
{
    int blockType;
    if (stage == NULL || backend == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    if (generatedCells[cell.row][cell.column]) return true;
    blockType = stage->blocks[cell.row][cell.column];
    if (RpgBuildCellCompact_UsesMetadataForBlock(blockType)) {
        metadataCells[cell.row][cell.column] = true;
        metadataBrokenCells[cell.row][cell.column] = false;
        metadataBlockTypes[cell.row][cell.column] = blockType;
        metadataDirty = true;
        metadataWritesSinceFlush++;
    } else if (backend->writeCellFolder == NULL ||
               !backend->writeCellFolder(backend->context, cell, blockType)) return false;
    generatedCells[cell.row][cell.column] = true;
    return true;
}

static bool WriteMap(const RpgStage *stage, int mapIndex,
                     const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int localColumn = 0; localColumn < RPG_STAGE_COLUMNS; localColumn++) {
            if (!WriteCell(stage, (RpgGridCell){ row, mapIndex * RPG_STAGE_COLUMNS + localColumn }, backend))
                return false;
        }
    }
    return metadataBatchDepth != 0 || FlushMetadata(backend);
}

static bool CreateInitialMap(const RpgStage *stage, int startMapIndex,
                             const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || startMapIndex < 0 || startMapIndex >= RPG_STAGE_MAP_COUNT) return false;
    memset(generatedCells, 0, sizeof(generatedCells));
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        metadataCells[row][column] = RpgBuildCellCompact_UsesMetadataForBlock(stage->blocks[row][column]);
        metadataBrokenCells[row][column] = stage->blocks[row][column] == RPG_BLOCK_BUILD_MISSING;
        metadataBlockTypes[row][column] = stage->blocks[row][column];
    }
    metadataCacheActive = true;
    metadataDirty = true;
    metadataWritesSinceFlush = 0;
    startingMap = startMapIndex;
    pendingCursor = 0;
    pendingStage = stage;
    return WriteMap(stage, startMapIndex, backend);
}

/* The compact file describes ordinary terrain for the whole connected stage,
   not merely the currently materialized preview areas.  Keep it complete from
   the first preview frame, while generatedCells continues to track only the
   per-area physical-folder work that may be deferred. */
static bool PublishAllNormalCellMetadata(const RpgStage *stage,
                                         const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || backend == NULL || !metadataCacheActive) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            int blockType = stage->blocks[row][column];
            metadataCells[row][column] = RpgBuildCellCompact_UsesMetadataForBlock(blockType);
            metadataBrokenCells[row][column] = blockType == RPG_BLOCK_BUILD_MISSING;
            metadataBlockTypes[row][column] = blockType;
        }
    }
    metadataDirty = true;
    return FlushMetadata(backend);
}

bool RpgBuildCellCompact_Create(const RpgStage *stage, const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || backend == NULL) return false;
    pendingStage = NULL;
    pendingCursor = 0;
    memset(generatedCells, 0, sizeof(generatedCells));
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        metadataCells[row][column] = RpgBuildCellCompact_UsesMetadataForBlock(stage->blocks[row][column]);
        metadataBrokenCells[row][column] = stage->blocks[row][column] == RPG_BLOCK_BUILD_MISSING;
        metadataBlockTypes[row][column] = stage->blocks[row][column];
    }
    metadataCacheActive = true;
    metadataDirty = true;
    metadataWritesSinceFlush = 0;
    RpgBuildCellCompact_BeginMetadataBatch();
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!WriteCell(stage, (RpgGridCell){ row, column }, backend)) {
            metadataBatchDepth--;
            return false;
        }
    }
    return RpgBuildCellCompact_EndMetadataBatch(backend);
}

bool RpgBuildCellCompact_CreatePreview(const RpgStage *stage, int startMapIndex,
                                       const RpgBuildCellStorageBackend *backend)
{
    return CreateInitialMap(stage, startMapIndex, backend) &&
           PublishAllNormalCellMetadata(stage, backend);
}

void RpgBuildCellCompact_Update(const RpgBuildCellStorageBackend *backend)
{
    int created = 0;
    if (pendingStage == NULL) return;
    while (pendingCursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS && created < RPG_COMPACT_CELL_BATCH_SIZE) {
        int mapIndex = pendingCursor / (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        int cellInMap = pendingCursor % (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        RpgGridCell cell = { cellInMap / RPG_STAGE_COLUMNS,
                             mapIndex * RPG_STAGE_COLUMNS + cellInMap % RPG_STAGE_COLUMNS };
        pendingCursor++;
        if (mapIndex == startingMap || generatedCells[cell.row][cell.column]) continue;
        if (!WriteCell(pendingStage, cell, backend)) { pendingStage = NULL; return; }
        created++;
    }
    if (metadataDirty && (metadataWritesSinceFlush >= RPG_COMPACT_FLUSH_CELL_INTERVAL ||
        pendingCursor >= RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS)) {
        if (!FlushMetadata(backend)) { pendingStage = NULL; return; }
    }
    if (pendingCursor >= RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS) pendingStage = NULL;
}

bool RpgBuildCellCompact_EnsureMap(const RpgStage *stage, int mapIndex,
                                   const RpgBuildCellStorageBackend *backend)
{
    return WriteMap(stage, mapIndex, backend);
}

bool RpgBuildCellCompact_RewriteGeneratedMetadata(const RpgStage *stage,
                                                  const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || backend == NULL || !metadataCacheActive || pendingStage != NULL) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        int blockType;
        if (!generatedCells[row][column]) continue;
        blockType = stage->blocks[row][column];
        metadataCells[row][column] = RpgBuildCellCompact_UsesMetadataForBlock(blockType);
        metadataBrokenCells[row][column] = blockType == RPG_BLOCK_BUILD_MISSING;
        metadataBlockTypes[row][column] = blockType;
    }
    metadataDirty = true;
    return metadataBatchDepth != 0 || FlushMetadata(backend);
}

void RpgBuildCellCompact_BeginMetadataBatch(void)
{
    metadataBatchDepth++;
}

bool RpgBuildCellCompact_EndMetadataBatch(const RpgBuildCellStorageBackend *backend)
{
    if (metadataBatchDepth > 0) metadataBatchDepth--;
    return metadataBatchDepth != 0 || FlushMetadata(backend);
}

void RpgBuildCellCompact_GetGenerationProgress(const RpgStage *stage, int *generatedCount,
                                               int *totalCount, bool *isPending)
{
    int generated = 0, total = 0;
    if (stage != NULL) for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            int mapIndex = column / RPG_STAGE_COLUMNS;
            if (!RpgStage_IsMapActive(stage, mapIndex)) continue;
            total++;
            if (generatedCells[row][column]) generated++;
        }
    }
    if (generatedCount != NULL) *generatedCount = generated;
    if (totalCount != NULL) *totalCount = total;
    if (isPending != NULL) *isPending = pendingStage != NULL && generated < total;
}

static bool LoadMetadataCsv(const RpgBuildCellStorageBackend *backend)
{
    char path[1200], line[8192];
    FILE *file;
    bool normalizedBrokenField = false;
    if (!GetMetadataPath(backend, path, sizeof(path))) return false;
    memset(metadataCells, 0, sizeof(metadataCells));
    memset(metadataBrokenCells, 0, sizeof(metadataBrokenCells));
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) metadataBlockTypes[row][column] = -1;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *cursor = line;
        char *end = NULL;
        long row = strtol(cursor, &end, 10);
        if (end == cursor || row < 0 || row >= RPG_STAGE_ROWS) continue; /* CSV header */
        cursor = end;
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            long blockType;
            if (*cursor != ',') break;
            cursor++;
            if (*cursor == ',' || *cursor == '\r' || *cursor == '\n' || *cursor == '\0') continue;
            blockType = strtol(cursor, &end, 10);
            if (end == cursor) {
                /* A non-empty non-number is corrupt, unlike an intentional blank. */
                metadataBlockTypes[row][column] = -1;
                metadataBrokenCells[row][column] = true;
                normalizedBrokenField = true;
                while (*cursor != '\0' && *cursor != ',' && *cursor != '\r' && *cursor != '\n') cursor++;
                continue;
            }
            metadataBlockTypes[row][column] = (int)blockType;
            metadataCells[row][column] = RpgBuildCellCompact_UsesMetadataForBlock((int)blockType);
            metadataBrokenCells[row][column] = blockType == -1;
            cursor = end;
        }
    }
    fclose(file);
    metadataCacheActive = true;
    metadataDirty = normalizedBrokenField;
    metadataWritesSinceFlush = 0;
    return !metadataDirty || FlushMetadata(backend);
}

bool RpgBuildCellCompact_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend)
{
    if (available == NULL || !LoadMetadataCsv(backend)) return false;
    memcpy(available, metadataCells, sizeof(metadataCells));
    return true;
}

bool RpgBuildCellCompact_Extract(RpgGridCell cell, int *blockType, const RpgBuildCellStorageBackend *backend)
{
    if (blockType == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS ||
        (!metadataCacheActive && !LoadMetadataCsv(backend)) ||
        !metadataCells[cell.row][cell.column]) return false;
    *blockType = metadataBlockTypes[cell.row][cell.column];
    metadataCells[cell.row][cell.column] = false;
    metadataBrokenCells[cell.row][cell.column] = false;
    metadataDirty = true;
    return FlushMetadata(backend);
}

bool RpgBuildCellCompact_Restore(RpgGridCell cell, int blockType, const RpgBuildCellStorageBackend *backend)
{
    if (!RpgBuildCellCompact_UsesMetadataForBlock(blockType)) return false;
    if ((!metadataCacheActive && !LoadMetadataCsv(backend))) return false;
    if (metadataCacheActive && cell.row >= 0 && cell.row < RPG_STAGE_ROWS &&
        cell.column >= 0 && cell.column < RPG_STAGE_WORLD_COLUMNS) {
        metadataCells[cell.row][cell.column] = true;
        metadataBrokenCells[cell.row][cell.column] = false;
        metadataBlockTypes[cell.row][cell.column] = blockType;
        metadataDirty = true;
        return FlushMetadata(backend);
    }
    return false;
}
