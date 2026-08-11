// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_STAGE_H
#define RPG_STAGE_H

#include "raylib.h"

enum { RPG_STAGE_TILE_SIZE = 48, RPG_STAGE_COLUMNS = 20, RPG_STAGE_ROWS = 10,
       RPG_STAGE_MAP_COUNT = 3, RPG_STAGE_WORLD_COLUMNS = RPG_STAGE_COLUMNS * RPG_STAGE_MAP_COUNT,
       RPG_STAGE_WORLD_WIDTH = RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE };

typedef struct RpgStage { int blocks[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS]; } RpgStage;

RpgStage RpgStage_Default(void);
bool RpgStage_Load(const char *filePath, RpgStage *stage);
bool RpgStage_Save(const char *filePath, const RpgStage *stage);
bool RpgStage_SetBlockAtPosition(RpgStage *stage, Vector2 position, bool isBlock);
void RpgStage_Draw(const RpgStage *stage, bool showGrid);
void RpgStage_DrawMap(const RpgStage *stage, int mapIndex, bool showGrid);

#endif
