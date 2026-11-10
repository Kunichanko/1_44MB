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
enum { RPG_COMPACT_FLUSH_CELL_INTERVAL = 120 };

static const RpgStage *pendingStage = NULL;
static bool generatedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
/* The editor displays this value every frame.  Recount only when the
   materialized-cell state changes, rather than scanning the whole world after
   preview generation has completed. */
static const RpgStage *progressStage = NULL;
static bool progressCacheDirty = true;
static int cachedGeneratedCount = 0;
static int cachedTotalCount = 0;
static int pendingCursor = 0;
static int startingMap = 0;
/* Ordinary cells have no per-cell filesystem representation.  They can be
   marked generated in the same update; only these deferred cells still need
   an object-folder operation and therefore consume the frame work budget. */
static bool pendingFolderCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
static int pendingFolderCellCount = 0;
/* A terrain paint operation normally changes one or a small number of cells.
   Keep those requests apart from the area generator so they never restart a
   scan of all 4,080 stage cells. */
static bool queuedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
static int queuedCellPreviousBlockTypes[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { 0 } };
static int queuedCellCount = 0;
static int queuedCellCursor = 0;
/* These counters describe one targeted editor edit batch.  A refresh first
   clears generatedCells for the chosen cell and then recreates it, so the
   ordinary generated/total counter intentionally cannot show this work. */
static int queuedCellUpdateCompleted = 0;
static int queuedCellUpdateTotal = 0;
static bool metadataCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
/* A blank CSV field is the canonical missing/error cell.  Numeric zero is
   ordinary air, so there is no sentinel block value in this format. */
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
            if (metadataBrokenCells[row][column]) fputc(',', file);
            else if (metadataCells[row][column]) fprintf(file, ",%d", type);
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
    if (RpgStage_IsCellError(stage, cell.row, cell.column)) {
        metadataCells[cell.row][cell.column] = false;
        metadataBrokenCells[cell.row][cell.column] = true;
        metadataBlockTypes[cell.row][cell.column] = 0;
        metadataDirty = true;
        metadataWritesSinceFlush++;
        generatedCells[cell.row][cell.column] = true;
        progressCacheDirty = true;
        return true;
    }
    /* An unknown block type is only a runtime diagnostic.  Never turn it
       into a build folder or durable cells.csv record. */
    if (RpgStage_IsCellRuntimeError(stage, cell.row, cell.column)) {
        metadataCells[cell.row][cell.column] = false;
        metadataBrokenCells[cell.row][cell.column] = false;
        metadataBlockTypes[cell.row][cell.column] = 0;
        generatedCells[cell.row][cell.column] = true;
        progressCacheDirty = true;
        return true;
    }
    if (RpgBuildCellCompact_UsesMetadataForBlock(blockType)) {
        metadataCells[cell.row][cell.column] = true;
        metadataBrokenCells[cell.row][cell.column] = false;
        metadataBlockTypes[cell.row][cell.column] = blockType;
        metadataDirty = true;
        metadataWritesSinceFlush++;
    } else {
        if (backend->writeCellFolder == NULL ||
            !backend->writeCellFolder(backend->context, cell, blockType)) return false;
        /* cells.csv is also the complete stage grid.  A folder-backed local
           cell therefore replaces any former compact value in the same CSV
           batch, instead of leaving a stale ordinary block behind. */
        metadataCells[cell.row][cell.column] = false;
        metadataBrokenCells[cell.row][cell.column] = false;
        metadataBlockTypes[cell.row][cell.column] = blockType;
        metadataDirty = true;
        metadataWritesSinceFlush++;
    }
    generatedCells[cell.row][cell.column] = true;
    progressCacheDirty = true;
    return true;
}

static bool CellRequiresFolderGeneration(const RpgStage *stage, RpgGridCell cell)
{
    if (stage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS ||
        RpgStage_IsCellError(stage, cell.row, cell.column) ||
        RpgStage_IsCellRuntimeError(stage, cell.row, cell.column)) return false;
    return !RpgBuildCellCompact_UsesMetadataForBlock(stage->blocks[cell.row][cell.column]);
}

static bool BlockTypeRequiresFolderGeneration(int blockType)
{
    return blockType > 0 && !RpgBuildCellCompact_UsesMetadataForBlock(blockType);
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
    memset(queuedCells, 0, sizeof(queuedCells));
    memset(queuedCellPreviousBlockTypes, 0, sizeof(queuedCellPreviousBlockTypes));
    queuedCellCount = 0;
    queuedCellCursor = 0;
    queuedCellUpdateCompleted = 0;
    queuedCellUpdateTotal = 0;
    memset(pendingFolderCells, 0, sizeof(pendingFolderCells));
    pendingFolderCellCount = 0;
    progressStage = NULL;
    progressCacheDirty = true;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        metadataCells[row][column] = !RpgStage_IsCellRuntimeError(stage, row, column) &&
                                      RpgBuildCellCompact_UsesMetadataForBlock(stage->blocks[row][column]);
        metadataBrokenCells[row][column] = RpgStage_IsCellError(stage, row, column);
        metadataBlockTypes[row][column] = RpgStage_IsCellRuntimeError(stage, row, column) ?
                                           0 : stage->blocks[row][column];
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
            metadataCells[row][column] = !RpgStage_IsCellRuntimeError(stage, row, column) &&
                                          RpgBuildCellCompact_UsesMetadataForBlock(blockType);
            metadataBrokenCells[row][column] = RpgStage_IsCellError(stage, row, column);
            metadataBlockTypes[row][column] = RpgStage_IsCellRuntimeError(stage, row, column) ?
                                               0 : blockType;
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
    memset(queuedCells, 0, sizeof(queuedCells));
    memset(queuedCellPreviousBlockTypes, 0, sizeof(queuedCellPreviousBlockTypes));
    queuedCellCount = 0;
    queuedCellCursor = 0;
    queuedCellUpdateCompleted = 0;
    queuedCellUpdateTotal = 0;
    memset(pendingFolderCells, 0, sizeof(pendingFolderCells));
    pendingFolderCellCount = 0;
    memset(generatedCells, 0, sizeof(generatedCells));
    progressStage = NULL;
    progressCacheDirty = true;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        metadataCells[row][column] = !RpgStage_IsCellRuntimeError(stage, row, column) &&
                                      RpgBuildCellCompact_UsesMetadataForBlock(stage->blocks[row][column]);
        metadataBrokenCells[row][column] = RpgStage_IsCellError(stage, row, column);
        metadataBlockTypes[row][column] = RpgStage_IsCellRuntimeError(stage, row, column) ?
                                           0 : stage->blocks[row][column];
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

void RpgBuildCellCompact_Update(const RpgBuildCellStorageBackend *backend, int workBudget)
{
    int folderWork = 0;
    bool wroteOrdinaryCells = false;
    if (pendingStage == NULL) return;
    if (workBudget < 1) workBudget = 1;
    if (workBudget > 64) workBudget = 64;
    /* Exact-cell edits are always scanned as one batch.  A normal cell only
       changes the shared CSV state, so delaying it behind an unrelated
       folder-backed object makes brush editing feel blocked for no benefit. */
    if (queuedCellCount > 0) {
        const int total = RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS;
        for (int scanned = 0; scanned < total && queuedCellCount > 0; scanned++) {
            int cursor = queuedCellCursor++ % (RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS);
            int row = cursor / RPG_STAGE_WORLD_COLUMNS;
            int column = cursor % RPG_STAGE_WORLD_COLUMNS;
            RpgGridCell cell = { row, column };
            bool requiresFolder;
            if (!queuedCells[row][column]) continue;
            requiresFolder = CellRequiresFolderGeneration(pendingStage, cell) ||
                             BlockTypeRequiresFolderGeneration(queuedCellPreviousBlockTypes[row][column]);
            if (requiresFolder && folderWork >= workBudget) continue;
            queuedCells[row][column] = false;
            queuedCellCount--;
            if (!RpgBuildCellCompact_UsesMetadataForBlock(queuedCellPreviousBlockTypes[row][column]) &&
                backend->removeCellFolder != NULL &&
                !backend->removeCellFolder(backend->context, cell,
                                           queuedCellPreviousBlockTypes[row][column])) {
                pendingStage = NULL;
                return;
            }
            /* A direct refresh intentionally overwrites an already generated
               compact cell.  For folder-backed, self-contained cells the old
               folder above is removed first, then WriteCell creates the new
               object_info record from the final stage value. */
            generatedCells[row][column] = false;
            if (!WriteCell(pendingStage, cell, backend)) { pendingStage = NULL; return; }
            queuedCellUpdateCompleted++;
            if (requiresFolder) folderWork++;
            else wroteOrdinaryCells = true;
        }
        /* One normal edit is durable in this update, not after a later
           unrelated folder batch reaches the old 120-cell threshold. */
        if (metadataDirty && (wroteOrdinaryCells ||
                              metadataWritesSinceFlush >= RPG_COMPACT_FLUSH_CELL_INTERVAL ||
                              queuedCellCount == 0) && !FlushMetadata(backend)) {
            pendingStage = NULL;
            return;
        }
        if (queuedCellCount > 0) return;
    }
    /* Sweep ordinary terrain in one memory-only pass.  Defer only cells that
       need individual object folders, then serve those at the configured
       per-frame budget below. */
    while (pendingCursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS) {
        int mapIndex = pendingCursor / (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        int cellInMap = pendingCursor % (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        RpgGridCell cell = { cellInMap / RPG_STAGE_COLUMNS,
                             mapIndex * RPG_STAGE_COLUMNS + cellInMap % RPG_STAGE_COLUMNS };
        pendingCursor++;
        if (mapIndex == startingMap || generatedCells[cell.row][cell.column]) continue;
        if (CellRequiresFolderGeneration(pendingStage, cell)) {
            if (!pendingFolderCells[cell.row][cell.column]) {
                pendingFolderCells[cell.row][cell.column] = true;
                pendingFolderCellCount++;
            }
            continue;
        }
        if (!WriteCell(pendingStage, cell, backend)) { pendingStage = NULL; return; }
        wroteOrdinaryCells = true;
    }
    for (int cursor = 0; cursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS &&
         pendingFolderCellCount > 0 && folderWork < workBudget; cursor++) {
        const int row = cursor / RPG_STAGE_WORLD_COLUMNS;
        const int column = cursor % RPG_STAGE_WORLD_COLUMNS;
        const RpgGridCell cell = { row, column };
        if (!pendingFolderCells[row][column]) continue;
        pendingFolderCells[row][column] = false;
        pendingFolderCellCount--;
        if (!WriteCell(pendingStage, cell, backend)) { pendingStage = NULL; return; }
        folderWork++;
    }
    if (metadataDirty && (wroteOrdinaryCells ||
        metadataWritesSinceFlush >= RPG_COMPACT_FLUSH_CELL_INTERVAL ||
        (pendingCursor >= RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS && pendingFolderCellCount == 0))) {
        if (!FlushMetadata(backend)) { pendingStage = NULL; return; }
    }
    if (pendingCursor >= RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS && pendingFolderCellCount == 0)
        pendingStage = NULL;
}

bool RpgBuildCellCompact_QueueMap(const RpgStage *stage, int mapIndex)
{
    if (stage == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT ||
        !RpgStage_IsMapActive(stage, mapIndex)) return false;
    /* The caller invalidates only this map.  Preserve valid areas and let the
       existing generator revisit the invalid cells a few at a time. */
    pendingStage = stage;
    pendingCursor = 0;
    startingMap = -1;
    progressCacheDirty = true;
    return true;
}

bool RpgBuildCellCompact_QueueCell(const RpgStage *stage, RpgGridCell cell)
{
    if (stage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    pendingStage = stage;
    if (!queuedCells[cell.row][cell.column]) {
        if (queuedCellCount == 0 && queuedCellUpdateCompleted >= queuedCellUpdateTotal) {
            queuedCellUpdateCompleted = 0;
            queuedCellUpdateTotal = 0;
        }
        queuedCells[cell.row][cell.column] = true;
        queuedCellPreviousBlockTypes[cell.row][cell.column] = stage->blocks[cell.row][cell.column];
        queuedCellCount++;
        queuedCellUpdateTotal++;
    }
    progressCacheDirty = true;
    return true;
}

bool RpgBuildCellCompact_QueueCellUpdate(const RpgStage *stage, RpgGridCell cell,
                                         int previousBlockType)
{
    if (stage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    pendingStage = stage;
    if (!queuedCells[cell.row][cell.column]) {
        if (queuedCellCount == 0 && queuedCellUpdateCompleted >= queuedCellUpdateTotal) {
            queuedCellUpdateCompleted = 0;
            queuedCellUpdateTotal = 0;
        }
        queuedCells[cell.row][cell.column] = true;
        queuedCellPreviousBlockTypes[cell.row][cell.column] = previousBlockType;
        queuedCellCount++;
        queuedCellUpdateTotal++;
    }
    progressCacheDirty = true;
    return true;
}

bool RpgBuildCellCompact_EnsureMap(const RpgStage *stage, int mapIndex,
                                   const RpgBuildCellStorageBackend *backend)
{
    return WriteMap(stage, mapIndex, backend);
}

bool RpgBuildCellCompact_IsMapGenerated(const RpgStage *stage, int mapIndex)
{
    if (stage == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT ||
        !RpgStage_IsMapActive(stage, mapIndex)) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int localColumn = 0; localColumn < RPG_STAGE_COLUMNS; localColumn++) {
            if (!generatedCells[row][mapIndex * RPG_STAGE_COLUMNS + localColumn]) return false;
        }
    }
    return true;
}

void RpgBuildCellCompact_InvalidateMap(int mapIndex)
{
    if (mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT) return;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int localColumn = 0; localColumn < RPG_STAGE_COLUMNS; localColumn++) {
            int column = mapIndex * RPG_STAGE_COLUMNS + localColumn;
            generatedCells[row][column] = false;
            if (pendingFolderCells[row][column]) {
                pendingFolderCells[row][column] = false;
                pendingFolderCellCount--;
            }
            if (queuedCells[row][column]) {
                queuedCells[row][column] = false;
                queuedCellPreviousBlockTypes[row][column] = 0;
                queuedCellCount--;
            }
        }
    }
    /* A map rebuild supersedes any targeted refresh progress that touched
       cells in that map; the general generation counter becomes authoritative. */
    queuedCellUpdateCompleted = 0;
    queuedCellUpdateTotal = 0;
    progressCacheDirty = true;
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
        metadataCells[row][column] = !RpgStage_IsCellRuntimeError(stage, row, column) &&
                                      RpgBuildCellCompact_UsesMetadataForBlock(blockType);
        metadataBrokenCells[row][column] = RpgStage_IsCellError(stage, row, column);
        metadataBlockTypes[row][column] = RpgStage_IsCellRuntimeError(stage, row, column) ?
                                           0 : blockType;
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
    if (stage != progressStage || progressCacheDirty) {
        int generated = 0, total = 0;
        if (stage != NULL) for (int row = 0; row < RPG_STAGE_ROWS; row++) {
            for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
                int mapIndex = column / RPG_STAGE_COLUMNS;
                if (!RpgStage_IsMapActive(stage, mapIndex)) continue;
                total++;
                if (generatedCells[row][column]) generated++;
            }
        }
        progressStage = stage;
        cachedGeneratedCount = generated;
        cachedTotalCount = total;
        progressCacheDirty = false;
    }
    if (generatedCount != NULL) *generatedCount = cachedGeneratedCount;
    if (totalCount != NULL) *totalCount = cachedTotalCount;
    if (isPending != NULL) *isPending = pendingStage != NULL &&
                                         cachedGeneratedCount < cachedTotalCount;
}

bool RpgBuildCellCompact_GetQueuedCellUpdateProgress(int *completedCells, int *totalCells)
{
    if (completedCells != NULL) *completedCells = queuedCellUpdateCompleted;
    if (totalCells != NULL) *totalCells = queuedCellUpdateTotal;
    return queuedCellUpdateTotal > 0;
}

void RpgBuildCellCompact_ClearQueuedCellUpdateProgress(void)
{
    if (queuedCellCount != 0) return;
    queuedCellUpdateCompleted = 0;
    queuedCellUpdateTotal = 0;
}

bool RpgBuildCellCompact_IsGenerationPending(void)
{
    return pendingStage != NULL;
}

void RpgBuildCellCompact_CancelGeneration(void)
{
    /* The preview owner may deliberately detach its runtime directory while
       a low-priority cell scan is still queued.  Keeping pendingStage set in
       that situation leaves the editor progress UI pointing at an immutable
       target forever: Update cannot write without an active build path.
       Clear only deferred work here.  A subsequent CreatePreview starts a
       new session and resets generated/metadata state explicitly. */
    pendingStage = NULL;
    pendingCursor = 0;
    startingMap = 0;
    memset(queuedCells, 0, sizeof(queuedCells));
    memset(pendingFolderCells, 0, sizeof(pendingFolderCells));
    pendingFolderCellCount = 0;
    memset(queuedCellPreviousBlockTypes, 0, sizeof(queuedCellPreviousBlockTypes));
    queuedCellCount = 0;
    queuedCellCursor = 0;
    queuedCellUpdateCompleted = 0;
    queuedCellUpdateTotal = 0;
    progressCacheDirty = true;
}

bool RpgBuildCellCompact_GetGenerationTarget(RpgGridCell *cell)
{
    int cursor;
    if (cell == NULL || pendingStage == NULL) return false;
    if (queuedCellCount > 0) {
        for (cursor = 0; cursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS; cursor++) {
            int index = (queuedCellCursor + cursor) % (RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS);
            int row = index / RPG_STAGE_WORLD_COLUMNS;
            int column = index % RPG_STAGE_WORLD_COLUMNS;
            if (!queuedCells[row][column]) continue;
            *cell = (RpgGridCell){ row, column };
            return true;
        }
    }
    for (cursor = 0; cursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS; cursor++) {
        const int row = cursor / RPG_STAGE_WORLD_COLUMNS;
        const int column = cursor % RPG_STAGE_WORLD_COLUMNS;
        if (!pendingFolderCells[row][column]) continue;
        *cell = (RpgGridCell){ row, column };
        return true;
    }
    for (cursor = pendingCursor; cursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS; cursor++) {
        int mapIndex = cursor / (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        int cellInMap = cursor % (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        RpgGridCell candidate = { cellInMap / RPG_STAGE_COLUMNS,
                                  mapIndex * RPG_STAGE_COLUMNS + cellInMap % RPG_STAGE_COLUMNS };
        if (!RpgStage_IsMapActive(pendingStage, mapIndex) || mapIndex == startingMap ||
            generatedCells[candidate.row][candidate.column]) continue;
        *cell = candidate;
        return true;
    }
    return false;
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
            if (*cursor == ',' || *cursor == '\r' || *cursor == '\n' || *cursor == '\0') {
                metadataBrokenCells[row][column] = true;
                continue;
            }
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
            if (blockType == -1) metadataCells[row][column] = false;
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
