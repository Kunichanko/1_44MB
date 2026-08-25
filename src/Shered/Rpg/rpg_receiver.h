// 依存する自プロジェクト内ファイル: rpg_grid_path.h, rpg_stage.h
#ifndef RPG_RECEIVER_H
#define RPG_RECEIVER_H

#include <stdbool.h>

#include "rpg_grid_path.h"
#include "rpg_stage.h"

// 受容体は1ブロックにつき1個のため、ステージのマス数を上限にする。
enum { RPG_RECEIVER_MAX_COUNT = RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS };

typedef struct RpgReceiver { RpgGridCell cell; RpgGridSide side; } RpgReceiver;
typedef struct RpgReceivers { int count; RpgReceiver entries[RPG_RECEIVER_MAX_COUNT]; } RpgReceivers;

RpgReceivers RpgReceivers_Default(void);
bool RpgReceivers_Load(const char *filePath, RpgReceivers *receivers);
bool RpgReceivers_Save(const char *filePath, const RpgReceivers *receivers);
bool RpgReceivers_Add(RpgReceivers *receivers, const RpgStage *stage, RpgGridCell cell);
int RpgReceivers_FindAtCell(const RpgReceivers *receivers, RpgGridCell cell);
int RpgReceivers_FindAtPosition(const RpgReceivers *receivers, Vector2 position, float distance);
bool RpgReceivers_CycleSide(RpgReceivers *receivers, int receiverIndex);
void RpgReceivers_RemoveBroken(RpgReceivers *receivers, const RpgStage *stage);
void RpgReceivers_Draw(const RpgReceivers *receivers);
void RpgReceivers_DrawMap(const RpgReceivers *receivers, int mapIndex);

#endif
// 役割: 導線受容体の構造と操作 API を宣言する。
