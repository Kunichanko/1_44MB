// 依存する自プロジェクト内ファイル: rpg_grid_path.h, rpg_stage.h
#ifndef RPG_WIRE_H
#define RPG_WIRE_H

#include <stdbool.h>
#include <stdio.h>

#include "rpg_grid_path.h"
#include "rpg_physics.h"
#include "rpg_stage.h"

enum { RPG_WIRE_MAX_COUNT = 32, RPG_WIRE_MAX_CELLS = RPG_GRID_PATH_MAX_CELLS };

typedef RpgGridCell RpgWireCell;
typedef enum RpgWireKind {
    RPG_WIRE_KIND_ELECTRIC = 0,
    RPG_WIRE_KIND_CONVEYOR
} RpgWireKind;
typedef struct RpgWire {
    /* The representative block that owns this path.  A receiver owns its
       electric path; a conveyor start block owns its conveyor path. */
    RpgGridCell ownerCell;
    RpgGridPath path;
    RpgWireKind kind;
    /* Shared by every block on one conveyor path.  Positive speed flows from
       path start to path end; direction -1 reverses the whole path. */
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
bool RpgWires_AddAdjacentConveyor(RpgWires *wires, const RpgStage *stage, int row, int column);
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
bool RpgWires_IsConveyor(const RpgWire *wire);
RpgGridCell RpgWires_GetOwnerCell(const RpgWire *wire);
bool RpgWires_IsConveyorClosed(const RpgWire *wire);
Vector2 RpgWires_GetConveyorVelocityBelow(const RpgWires *wires, Rectangle bounds);
/* Every second conveyor cell exposes a raised, one-way floor.  It only catches
   a body crossing its upper edge while falling; ascent passes through it. */
bool RpgWires_FindConveyorPlatformLanding(const RpgWires *wires, Rectangle previousBounds,
                                          Rectangle candidateBounds, float *landingY);
/* Retrieves the current diagonal conveyor floor under a body.  The wire
   module exposes geometry only; body movement is resolved by rpg_physics. */
bool RpgWires_FindConveyorSlopeBelow(const RpgWires *wires, Rectangle bounds,
                                     RpgPhysicsSlope *slope);
/* Returns the movement of the currently supporting conveyor blade.  Both
   rectangles are derived from the same timed outer-boundary blade that is
   drawn, so callers can reuse normal moving-solid rider handling. */
bool RpgWires_FindConveyorFloorMotion(const RpgWires *wires, Rectangle riderBounds,
                                      float previousElapsed, float currentElapsed,
                                      Rectangle *previousBounds, Rectangle *currentBounds);
/* A fully vertical conveyor blade is a one-way wall.  The returned movement
   keeps a body on its blocking side while allowing travel in blade direction. */
bool RpgWires_FindConveyorWallPush(const RpgWires *wires, Rectangle previousBounds,
                                   Rectangle candidateBounds, float *pushX);

#endif
// 役割: 導線データの構造と編集・描画 API を宣言する。
