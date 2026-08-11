// 依存する自プロジェクト内ファイル: rpg_stage.h
#include "rpg_stage.h"

#include <stdio.h>

RpgStage RpgStage_Default(void)
{
    RpgStage stage = {0};
    for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        stage.blocks[8][column] = 1;
        stage.blocks[9][column] = 1;
    }
    return stage;
}

bool RpgStage_Load(const char *filePath, RpgStage *stage)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            if (fscanf(file, "%d", &stage->blocks[row][column]) != 1) {
                fclose(file); return false;
            }
        }
    }
    fclose(file); return true;
}

bool RpgStage_Save(const char *filePath, const RpgStage *stage)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) fprintf(file, "%d ", stage->blocks[row][column]);
        fputc('\n', file);
    }
    return fclose(file) == 0;
}

bool RpgStage_SetBlockAtPosition(RpgStage *stage, Vector2 position, bool isBlock)
{
    int column = (int)(position.x / RPG_STAGE_TILE_SIZE);
    int row = (int)(position.y / RPG_STAGE_TILE_SIZE);
    if (column < 0 || column >= RPG_STAGE_WORLD_COLUMNS || row < 0 || row >= RPG_STAGE_ROWS) return false;
    stage->blocks[row][column] = isBlock ? 1 : 0;
    return true;
}

void RpgStage_Draw(const RpgStage *stage, bool showGrid)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        if (stage->blocks[row][column]) DrawRectangleRec(cell, (Color){ 116, 78, 48, 255 });
        if (showGrid) DrawRectangleLinesEx(cell, 1.0f, Fade(DARKGRAY, 0.45f));
    }
}

void RpgStage_DrawMap(const RpgStage *stage, int mapIndex, bool showGrid)
{
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        if (stage->blocks[row][startColumn + column]) DrawRectangleRec(cell, (Color){ 116, 78, 48, 255 });
        if (showGrid) DrawRectangleLinesEx(cell, 1.0f, Fade(DARKGRAY, 0.45f));
    }
}
