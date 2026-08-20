// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_ITEM_H
#define RPG_ITEM_H

#include <stdbool.h>
#include "raylib.h"
#include "rpg_stage.h"

enum { RPG_ITEM_MAX_COUNT = 32, RPG_ITEM_NAME_LENGTH = 64 };
typedef struct RpgItem { Vector2 position; char name[RPG_ITEM_NAME_LENGTH]; bool collected; } RpgItem;
typedef struct RpgItems { int count; RpgItem entries[RPG_ITEM_MAX_COUNT]; } RpgItems;

RpgItems RpgItems_Default(void);
bool RpgItems_Load(const char *filePath, RpgItems *items);
bool RpgItems_Save(const char *filePath, const RpgItems *items);
bool RpgItems_Add(RpgItems *items, Vector2 position);
int RpgItems_FindAtPosition(const RpgItems *items, Vector2 position, float distance);
bool RpgItems_RemoveAtPosition(RpgItems *items, Vector2 position, float distance);
void RpgItems_Draw(const RpgItems *items);
#endif
// 役割: 通常アイテムと File.png ドロップの API を宣言する。
