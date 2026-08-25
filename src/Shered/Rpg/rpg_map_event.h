// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_MAP_EVENT_H
#define RPG_MAP_EVENT_H

#include <stdbool.h>
#include "raylib.h"

enum { RPG_MAP_EVENT_MAX_COUNT = 32, RPG_MAP_EVENT_NAME_LENGTH = 64 };
typedef struct RpgMapEvent { Vector2 position; char name[RPG_MAP_EVENT_NAME_LENGTH]; bool triggered; } RpgMapEvent;
typedef struct RpgMapEvents { int count; RpgMapEvent entries[RPG_MAP_EVENT_MAX_COUNT]; } RpgMapEvents;
RpgMapEvents RpgMapEvents_Default(void);
bool RpgMapEvents_Load(const char *filePath, RpgMapEvents *events);
bool RpgMapEvents_Save(const char *filePath, const RpgMapEvents *events);
/* イベントの配置・移動で共有する、マス中央へのスナップ処理。 */
Vector2 RpgMapEvents_SnapToCell(Vector2 position);
bool RpgMapEvents_Add(RpgMapEvents *events, Vector2 position);
/* 既存イベントをマス中央へ移動し、同じマスの重複を防ぐ。 */
bool RpgMapEvents_Move(RpgMapEvents *events, int index, Vector2 position);
int RpgMapEvents_FindAtPosition(const RpgMapEvents *events, Vector2 position, float distance);
bool RpgMapEvents_RemoveAtPosition(RpgMapEvents *events, Vector2 position, float distance);
void RpgMapEvents_Draw(const RpgMapEvents *events);
#endif
// 役割: マップイベントの構造と操作 API を宣言する。
