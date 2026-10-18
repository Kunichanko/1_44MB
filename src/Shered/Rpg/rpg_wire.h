// 依存する自プロジェクト内ファイル: rpg_grid_path.h, rpg_stage.h
#ifndef RPG_WIRE_H
#define RPG_WIRE_H

#include <stdbool.h>
#include <stdio.h>

#include "rpg_grid_path.h"
#include "rpg_stage.h"

enum { RPG_WIRE_MAX_COUNT = 32, RPG_WIRE_MAX_CELLS = RPG_GRID_PATH_MAX_CELLS };

typedef RpgGridCell RpgWireCell;
typedef enum RpgWireKind {
    RPG_WIRE_KIND_ELECTRIC = 0,
    /* Legacy on-disk value.  It is accepted while loading old stages, then
       discarded; no conveyor runtime remains. */
    RPG_WIRE_KIND_CONVEYOR = 1
} RpgWireKind;
typedef struct RpgWire {
    /* The representative block that owns this path.  A receiver owns its
       electric path; a conveyor start block owns its conveyor path. */
    RpgGridCell ownerCell;
    RpgGridPath path;
    RpgWireKind kind;
    /* Retained only so pre-removal v6 route records can be consumed safely.
       These values are never used by a runtime system. */
    float conveyorSpeed;
    int conveyorDirection;
    /* The optional outer moving floor/wall blades.  The belt body and its
       rollers remain active when this is false. */
    bool conveyorHasFloor;
    /* The smallest visible floor angle (degrees from horizontal) that makes
       a moving body slide downhill.  Shared by the entire conveyor path. */
    float conveyorSlideAngleDegrees;
    /* Editor-only visual preview state; deliberately not serialized. */
    float conveyorPreviewElapsed;
    bool hasReceiverSource;
    RpgWireCell receiverCell;
    RpgGridSide receiverSide;
} RpgWire;
typedef struct RpgWires { int count; RpgWire entries[RPG_WIRE_MAX_COUNT]; } RpgWires;
struct RpgDataShots;

RpgWires RpgWires_Default(void);
bool RpgWires_Load(const char *filePath, RpgWires *wires);
bool RpgWires_Save(const char *filePath, const RpgWires *wires);
/* One v6-format path record. Shared by rpg_wires.cfg legacy import and the
   unified rpg_attachments.cfg static format. */
bool RpgWires_ReadRecord(FILE *file, RpgWire *wire);
bool RpgWires_WriteRecord(FILE *file, const RpgWire *wire);
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
