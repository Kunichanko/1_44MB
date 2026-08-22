// 役割: 個別フォルダ方式で、空気を含むすべてのマスの object_info.txt を生成する。
// 依存する自プロジェクト内ファイル: rpg_build_cell_folders.h
#include "rpg_build_cell_folders.h"

#include <string.h>

enum { RPG_BUILD_CELL_FOLDER_BATCH_SIZE = 16 };

static const RpgStage *pendingStage = NULL;
static bool generatedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
static int pendingCursor = 0;
static int startingMap = 0;

static bool WriteCell(const RpgStage *stage, RpgGridCell cell, const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || backend == NULL || backend->writeCellFolder == NULL ||
        cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS)
        return false;
    if (generatedCells[cell.row][cell.column]) return true;
    if (!backend->writeCellFolder(backend->context, cell, stage->blocks[cell.row][cell.column])) return false;
    generatedCells[cell.row][cell.column] = true;
    return true;
}

bool RpgBuildCellFolders_Create(const RpgStage *stage, int startMapIndex,
                                const RpgBuildCellStorageBackend *backend)
{
    if (stage == NULL || startMapIndex < 0 || startMapIndex >= RPG_STAGE_MAP_COUNT) return false;
    pendingStage = stage;
    startingMap = startMapIndex;
    pendingCursor = 0;
    memset(generatedCells, 0, sizeof(generatedCells));
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int localColumn = 0; localColumn < RPG_STAGE_COLUMNS; localColumn++) {
        if (!WriteCell(stage, (RpgGridCell){ row, startMapIndex * RPG_STAGE_COLUMNS + localColumn }, backend))
            return false;
    }
    return true;
}

void RpgBuildCellFolders_Update(const RpgBuildCellStorageBackend *backend)
{
    int created = 0;
    if (pendingStage == NULL) return;
    while (pendingCursor < RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS && created < RPG_BUILD_CELL_FOLDER_BATCH_SIZE) {
        int mapIndex = pendingCursor / (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        int cellInMap = pendingCursor % (RPG_STAGE_ROWS * RPG_STAGE_COLUMNS);
        RpgGridCell cell = { cellInMap / RPG_STAGE_COLUMNS,
                             mapIndex * RPG_STAGE_COLUMNS + cellInMap % RPG_STAGE_COLUMNS };
        pendingCursor++;
        if (mapIndex == startingMap || generatedCells[cell.row][cell.column]) continue;
        if (!WriteCell(pendingStage, cell, backend)) { pendingStage = NULL; return; }
        created++;
    }
    if (pendingCursor >= RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS) pendingStage = NULL;
}

bool RpgBuildCellFolders_EnsureCell(RpgGridCell cell, int blockType,
                                    const RpgBuildCellStorageBackend *backend)
{
    if (pendingStage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS || backend == NULL ||
        backend->writeCellFolder == NULL) return false;
    if (generatedCells[cell.row][cell.column]) return true;
    if (!backend->writeCellFolder(backend->context, cell, blockType)) return false;
    generatedCells[cell.row][cell.column] = true;
    return true;
}
