// 依存: なし（raylib は外部ライブラリ）
#ifndef STAGE_H
#define STAGE_H

#include "raylib.h"

#include <stdbool.h>

enum {
    STAGE_TILE_EMPTY = 0,
    STAGE_TILE_BLOCK = 1,
    STAGE_TILE_ENEMY = 2,
    STAGE_GRID_TILE_SIZE = 48,
    STAGE_GRID_COLUMNS = 72,
    STAGE_GRID_ROWS = 16,
};

typedef struct Stage {
    float width;
    float groundY;
    int tiles[STAGE_GRID_ROWS][STAGE_GRID_COLUMNS];
    bool enemyTiles[STAGE_GRID_ROWS][STAGE_GRID_COLUMNS];
    int mergeOwnerColumn[STAGE_GRID_ROWS][STAGE_GRID_COLUMNS];
    int mergeOwnerRow[STAGE_GRID_ROWS][STAGE_GRID_COLUMNS];
    int mergeSize[STAGE_GRID_ROWS][STAGE_GRID_COLUMNS];
} Stage;

Stage Stage_Create(void);
void Stage_Draw(const Stage *stage);
void Stage_DrawGridOverlay(const Stage *stage, float opacity);
void Stage_ClearEnemyTiles(Stage *stage);
void Stage_SetEnemyTile(Stage *stage, Vector2 worldPosition);
Vector2 Stage_GetCellCenter(int column, int row);
bool Stage_SetTileAtWorldPosition(Stage *stage, Vector2 worldPosition, int tile);
bool Stage_GetCellAtWorldPosition(Vector2 worldPosition, int *column, int *row);
bool Stage_MergeSquare(Stage *stage, int column, int row, int size);
int Stage_GetEnemySpawnPositions(const Stage *stage, Vector2 *positions, int maximumCount);
bool Stage_Load(const char *filePath, Stage *stage);
bool Stage_Save(const Stage *stage, const char *filePath);

#endif
// 役割: 横スクロールステージの状態と衝突 API を宣言する。
