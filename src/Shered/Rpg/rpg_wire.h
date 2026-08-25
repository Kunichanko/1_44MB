// 依存する自プロジェクト内ファイル: rpg_grid_path.h, rpg_stage.h
#ifndef RPG_WIRE_H
#define RPG_WIRE_H

#include <stdbool.h>

#include "rpg_grid_path.h"
#include "rpg_stage.h"

enum { RPG_WIRE_MAX_COUNT = 32, RPG_WIRE_MAX_CELLS = RPG_GRID_PATH_MAX_CELLS };

typedef RpgGridCell RpgWireCell;
typedef struct RpgWire {
    RpgGridPath path;
    bool hasReceiverSource;
    RpgWireCell receiverCell;
    RpgGridSide receiverSide;
} RpgWire;
typedef struct RpgWires { int count; RpgWire entries[RPG_WIRE_MAX_COUNT]; } RpgWires;
struct RpgDataShots;

RpgWires RpgWires_Default(void);
bool RpgWires_Load(const char *filePath, RpgWires *wires);
bool RpgWires_Save(const char *filePath, const RpgWires *wires);
bool RpgWires_AddAdjacent(RpgWires *wires, const RpgStage *stage, int row, int column);
bool RpgWires_AddFromReceiver(RpgWires *wires, const RpgStage *stage, RpgWireCell cell,
                              RpgGridSide side);
bool RpgWires_FindEndpoint(const RpgWires *wires, int row, int column, int *wireIndex,
                           bool *isStart);
bool RpgWires_MoveEndpoint(RpgWires *wires, const RpgStage *stage, int wireIndex,
                           bool isStart, int row, int column);
void RpgWires_RemoveBroken(RpgWires *wires, const RpgStage *stage);
void RpgWires_Draw(const RpgWires *wires, const RpgStage *stage);
void RpgWires_DrawMap(const RpgWires *wires, const RpgStage *stage, int mapIndex);
// 電気化したデータ弾が同じマスにいる導線だけを発光させる。進行状態はデータ弾側に持つ。
void RpgWires_DrawElectric(const RpgWires *wires, const struct RpgDataShots *dataShots,
                           int firstColumn, int columnCount);

#endif
// 役割: 導線データの構造と編集・描画 API を宣言する。
