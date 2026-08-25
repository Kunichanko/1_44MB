// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_GRID_PATH_H
#define RPG_GRID_PATH_H

#include <stdbool.h>

enum { RPG_GRID_PATH_MAX_CELLS = 128 };

typedef struct RpgGridCell { int row; int column; } RpgGridCell;
typedef enum RpgGridSide {
    RPG_GRID_SIDE_TOP,
    RPG_GRID_SIDE_RIGHT,
    RPG_GRID_SIDE_BOTTOM,
    RPG_GRID_SIDE_LEFT
} RpgGridSide;
typedef struct RpgGridPath {
    int cellCount;
    RpgGridCell cells[RPG_GRID_PATH_MAX_CELLS];
} RpgGridPath;

RpgGridPath RpgGridPath_Create(RpgGridCell start, RpgGridCell end);
bool RpgGridPath_IsEndpoint(const RpgGridPath *path, RpgGridCell cell, bool *isStart);
bool RpgGridPath_MoveEndpoint(RpgGridPath *path, bool isStart, RpgGridCell destination,
                              int minimumCellCount);
RpgGridCell RpgGridPath_GetSideNeighbor(RpgGridCell cell, RpgGridSide side);

#endif
// 役割: グリッド上の再利用可能な経路データ API を宣言する。
