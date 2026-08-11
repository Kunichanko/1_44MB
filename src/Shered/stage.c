// 依存: stage.h
#include "stage.h"

#include <stdio.h>
#include <string.h>

Stage Stage_Create(void)
{
    Stage stage = {0};
    stage.width = (float)(STAGE_GRID_COLUMNS * STAGE_GRID_TILE_SIZE);
    stage.groundY = 8.0f * STAGE_GRID_TILE_SIZE;

    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            stage.mergeOwnerColumn[row][column] = column;
            stage.mergeOwnerRow[row][column] = row;
            stage.mergeSize[row][column] = 1;
        }
    }

    // 下4行を地面・壁ブロックとして初期化し、空白・ブロック・敵を同じグリッドで管理する。
    for (int row = 8; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            stage.tiles[row][column] = STAGE_TILE_BLOCK;
        }
    }

    stage.tiles[7][15] = STAGE_TILE_ENEMY;
    stage.tiles[7][23] = STAGE_TILE_ENEMY;
    stage.tiles[7][31] = STAGE_TILE_ENEMY;

    return stage;
}

void Stage_Draw(const Stage *stage)
{
    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            if (stage->mergeOwnerColumn[row][column] != column ||
                stage->mergeOwnerRow[row][column] != row) {
                continue;
            }
            int size = stage->mergeSize[row][column];
            Rectangle cell = {
                (float)(column * STAGE_GRID_TILE_SIZE),
                (float)(row * STAGE_GRID_TILE_SIZE),
                size * STAGE_GRID_TILE_SIZE,
                size * STAGE_GRID_TILE_SIZE,
            };
            int tile = stage->tiles[row][column];
            if (tile == STAGE_TILE_BLOCK) {
                DrawRectangleRec(cell, DARKGREEN);
            } else if (tile == STAGE_TILE_ENEMY || stage->enemyTiles[row][column]) {
                DrawRectangleRec(cell, Fade(PURPLE, 0.18f));
            }
        }
    }

    // スクロールしていることが分かるよう、地面に一定間隔で目印を配置する。
}

void Stage_DrawGridOverlay(const Stage *stage, float opacity)
{
    Rectangle stageArea = { 0.0f, 0.0f, stage->width,
                            (float)(STAGE_GRID_ROWS * STAGE_GRID_TILE_SIZE) };
    DrawRectangleRec(stageArea, Fade(RAYWHITE, opacity));

    // グリッド確認中だけマス線と値を最前面に描画し、通常のゲーム画面には表示しない。
    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            if (stage->mergeOwnerColumn[row][column] != column ||
                stage->mergeOwnerRow[row][column] != row) {
                continue;
            }
            int size = stage->mergeSize[row][column];
            Rectangle cell = {
                (float)(column * STAGE_GRID_TILE_SIZE),
                (float)(row * STAGE_GRID_TILE_SIZE),
                size * STAGE_GRID_TILE_SIZE,
                size * STAGE_GRID_TILE_SIZE,
            };
            DrawRectangleLinesEx(cell, 1.0f, Fade(DARKGRAY, 0.75f));
            int displayedTile = stage->enemyTiles[row][column] ? STAGE_TILE_ENEMY :
                                stage->tiles[row][column];
            DrawText(TextFormat("%d", displayedTile),
                     (int)(cell.x + cell.width / 2) - 5,
                     (int)(cell.y + cell.height / 2) - 8, 16, DARKGRAY);
        }
    }
}

void Stage_ClearEnemyTiles(Stage *stage)
{
    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            stage->enemyTiles[row][column] = false;
        }
    }
}

void Stage_SetEnemyTile(Stage *stage, Vector2 worldPosition)
{
    int column = (int)(worldPosition.x / STAGE_GRID_TILE_SIZE);
    int row = (int)(worldPosition.y / STAGE_GRID_TILE_SIZE) - 1;
    if (column < 0 || column >= STAGE_GRID_COLUMNS || row < 0 || row >= STAGE_GRID_ROWS) {
        return;
    }
    stage->enemyTiles[row][column] = true;
}

Vector2 Stage_GetCellCenter(int column, int row)
{
    return (Vector2){
        (float)(column * STAGE_GRID_TILE_SIZE + STAGE_GRID_TILE_SIZE / 2),
        (float)(row * STAGE_GRID_TILE_SIZE + STAGE_GRID_TILE_SIZE / 2),
    };
}

bool Stage_SetTileAtWorldPosition(Stage *stage, Vector2 worldPosition, int tile)
{
    int column = (int)(worldPosition.x / STAGE_GRID_TILE_SIZE);
    int row = (int)(worldPosition.y / STAGE_GRID_TILE_SIZE);
    if (tile < STAGE_TILE_EMPTY || tile > STAGE_TILE_ENEMY ||
        column < 0 || column >= STAGE_GRID_COLUMNS ||
        row < 0 || row >= STAGE_GRID_ROWS) {
        return false;
    }

    stage->tiles[row][column] = tile;
    return true;
}

bool Stage_GetCellAtWorldPosition(Vector2 worldPosition, int *column, int *row)
{
    int foundColumn = (int)(worldPosition.x / STAGE_GRID_TILE_SIZE);
    int foundRow = (int)(worldPosition.y / STAGE_GRID_TILE_SIZE);
    if (foundColumn < 0 || foundColumn >= STAGE_GRID_COLUMNS ||
        foundRow < 0 || foundRow >= STAGE_GRID_ROWS) {
        return false;
    }
    *column = foundColumn;
    *row = foundRow;
    return true;
}

bool Stage_MergeSquare(Stage *stage, int column, int row, int size)
{
    if (size < 1 || column < 0 || row < 0 ||
        column + size > STAGE_GRID_COLUMNS || row + size > STAGE_GRID_ROWS) {
        return false;
    }

    int total = 0;
    for (int currentRow = row; currentRow < row + size; currentRow++) {
        for (int currentColumn = column; currentColumn < column + size; currentColumn++) {
            total += stage->tiles[currentRow][currentColumn];
        }
    }
    int average = (total + (size * size) / 2) / (size * size);
    for (int currentRow = row; currentRow < row + size; currentRow++) {
        for (int currentColumn = column; currentColumn < column + size; currentColumn++) {
            stage->tiles[currentRow][currentColumn] = average;
            stage->mergeOwnerColumn[currentRow][currentColumn] = column;
            stage->mergeOwnerRow[currentRow][currentColumn] = row;
            stage->mergeSize[currentRow][currentColumn] = 0;
        }
    }
    stage->mergeSize[row][column] = size;
    return true;
}

int Stage_GetEnemySpawnPositions(const Stage *stage, Vector2 *positions, int maximumCount)
{
    int count = 0;
    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            if (stage->tiles[row][column] == STAGE_TILE_ENEMY && count < maximumCount) {
                positions[count++] = Stage_GetCellCenter(column, row);
            }
        }
    }
    return count;
}

bool Stage_Load(const char *filePath, Stage *stage)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        return false;
    }

    int loadedTiles[STAGE_GRID_ROWS][STAGE_GRID_COLUMNS];
    char line[STAGE_GRID_COLUMNS + 8];
    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        if (fgets(line, sizeof(line), file) == NULL ||
            (int)strcspn(line, "\r\n") < STAGE_GRID_COLUMNS) {
            fclose(file);
            return false;
        }
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            if (line[column] < '0' || line[column] > '2') {
                fclose(file);
                return false;
            }
            loadedTiles[row][column] = line[column] - '0';
        }
    }
    fclose(file);
    memcpy(stage->tiles, loadedTiles, sizeof(loadedTiles));
    return true;
}

bool Stage_Save(const Stage *stage, const char *filePath)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) {
        return false;
    }

    for (int row = 0; row < STAGE_GRID_ROWS; row++) {
        for (int column = 0; column < STAGE_GRID_COLUMNS; column++) {
            fputc('0' + stage->tiles[row][column], file);
        }
        fputc('\n', file);
    }
    return fclose(file) == 0;
}
