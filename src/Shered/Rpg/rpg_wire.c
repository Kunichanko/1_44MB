// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_grid_path.h, rpg_wire.h, rpg_stage.h
#include "rpg_wire.h"

#include "rpg_block_inventory.h"
#include "rpg_data_shot.h"
#include "raymath.h"

#define RPG_TEXT_ROUTE_RAYLIB_CALLS
#include "../game_font.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static bool RpgWires_IsCellInStage(int row, int column)
{
    return row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS;
}

static bool RpgWires_IsBlockCell(const RpgStage *stage, int row, int column)
{
    // 導線はブロック上のマス列だけを保存する見た目・接続用データであり、物理衝突には参加しない。
    return RpgWires_IsCellInStage(row, column) && stage->blocks[row][column] != 0;
}

RpgWires RpgWires_Default(void)
{
    return (RpgWires){ 0 };
}

bool RpgWires_Load(const char *filePath, RpgWires *wires)
{
    FILE *file = fopen(filePath, "r");
    RpgWires loaded = RpgWires_Default();
    char format[16];
    bool isV2Format = false;
    bool isV3Format = false;
    bool isV4Format = false;
    bool isV5Format = false;
    bool isV6Format = false;
    if (file == NULL) return false;
    if (fscanf(file, "%15s", format) != 1) {
        fclose(file);
        return false;
    }
    isV2Format = strcmp(format, "v2") == 0;
    isV3Format = strcmp(format, "v3") == 0;
    isV4Format = strcmp(format, "v4") == 0;
    isV5Format = strcmp(format, "v5") == 0;
    isV6Format = strcmp(format, "v6") == 0;
    if (((isV2Format || isV3Format || isV4Format || isV5Format || isV6Format) ? fscanf(file, "%d", &loaded.count) :
                           sscanf(format, "%d", &loaded.count)) != 1 || loaded.count < 0 ||
        loaded.count > RPG_WIRE_MAX_COUNT) {
        fclose(file);
        return false;
    }
    for (int wireIndex = 0; wireIndex < loaded.count; wireIndex++) {
        RpgWire *wire = &loaded.entries[wireIndex];
        int v4PathValues[1 + RPG_WIRE_MAX_CELLS * 2];
        int v4PathValueCount = 0;
        int sourceSide = RPG_GRID_SIDE_TOP;
        int receiverSource = 0;
        int conveyorHasFloor = 1;
        int kind = RPG_WIRE_KIND_ELECTRIC;
        wire->kind = RPG_WIRE_KIND_ELECTRIC;
        wire->conveyorSpeed = 96.0f;
        wire->conveyorDirection = 1;
        wire->conveyorHasFloor = true;
        wire->conveyorSlideAngleDegrees = 15.0f;
        wire->ownerCell = (RpgGridCell){ -1, -1 };
        if (isV6Format &&
            fscanf(file, "%d %f %d %f %d %d %d %d %d %d %d", &kind,
                   &wire->conveyorSpeed, &wire->conveyorDirection,
                   &wire->conveyorSlideAngleDegrees, &conveyorHasFloor,
                   &wire->ownerCell.row, &wire->ownerCell.column, &receiverSource,
                   &wire->receiverCell.row, &wire->receiverCell.column, &sourceSide) != 11) {
            fclose(file);
            return false;
        }
        if (isV5Format &&
            fscanf(file, "%d %f %d %f %d %d %d %d %d", &kind,
                   &wire->conveyorSpeed, &wire->conveyorDirection,
                   &wire->conveyorSlideAngleDegrees, &conveyorHasFloor, &receiverSource,
                   &wire->receiverCell.row, &wire->receiverCell.column, &sourceSide) != 9) {
            fclose(file);
            return false;
        }
        if (isV4Format &&
            fscanf(file, "%d %f %d %f %d %d %d %d", &kind,
                   &wire->conveyorSpeed, &wire->conveyorDirection,
                   &wire->conveyorSlideAngleDegrees, &receiverSource,
                   &wire->receiverCell.row, &wire->receiverCell.column, &sourceSide) != 8) {
            fclose(file);
            return false;
        }
        if (isV3Format &&
            fscanf(file, "%d %f %d %d %d %d %d", &kind,
                   &wire->conveyorSpeed, &wire->conveyorDirection, &receiverSource,
                   &wire->receiverCell.row, &wire->receiverCell.column, &sourceSide) != 7) {
            fclose(file);
            return false;
        }
        if (isV4Format || isV5Format || isV6Format) {
            char pathLine[4096];
            char *cursor;
            char *next;
            if (fgets(pathLine, sizeof(pathLine), file) == NULL) {
                fclose(file);
                return false;
            }
            cursor = pathLine;
            while (v4PathValueCount < (int)(sizeof(v4PathValues) / sizeof(v4PathValues[0]))) {
                long value = strtol(cursor, &next, 10);
                if (next == cursor) break;
                v4PathValues[v4PathValueCount++] = (int)value;
                cursor = next;
            }
            /* v4 normally starts this tail with cellCount.  The first v4
               build omitted that one format token; recover those already
               written files by treating the whole even-length tail as pairs. */
            if (v4PathValueCount >= 3 && v4PathValues[0] >= 1 &&
                v4PathValues[0] <= RPG_WIRE_MAX_CELLS &&
                v4PathValueCount == 1 + v4PathValues[0] * 2) {
                wire->path.cellCount = v4PathValues[0];
                for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++)
                    wire->path.cells[cellIndex] = (RpgWireCell){ v4PathValues[1 + cellIndex * 2],
                                                                  v4PathValues[2 + cellIndex * 2] };
            } else if (v4PathValueCount >= 2 && v4PathValueCount % 2 == 0 &&
                       v4PathValueCount / 2 <= RPG_WIRE_MAX_CELLS) {
                wire->path.cellCount = v4PathValueCount / 2;
                for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++)
                    wire->path.cells[cellIndex] = (RpgWireCell){ v4PathValues[cellIndex * 2],
                                                                  v4PathValues[cellIndex * 2 + 1] };
            } else {
                fclose(file);
                return false;
            }
        }
        wire->kind = (RpgWireKind)kind;
        wire->conveyorHasFloor = conveyorHasFloor != 0;
        if (isV2Format &&
            fscanf(file, "%d %d %d %d", &receiverSource,
                   &wire->receiverCell.row, &wire->receiverCell.column, &sourceSide) != 4) {
            fclose(file);
            return false;
        }
        if ((wire->kind != RPG_WIRE_KIND_ELECTRIC && wire->kind != RPG_WIRE_KIND_CONVEYOR) ||
            (wire->kind == RPG_WIRE_KIND_CONVEYOR &&
             (wire->conveyorSpeed < 16.0f || wire->conveyorSpeed > 960.0f ||
              (wire->conveyorDirection != -1 && wire->conveyorDirection != 1) ||
              (conveyorHasFloor != 0 && conveyorHasFloor != 1) ||
              wire->conveyorSlideAngleDegrees < 0.0f ||
              wire->conveyorSlideAngleDegrees > 89.0f)) ||
            (receiverSource != 0 && receiverSource != 1)) {
            fclose(file);
            return false;
        }
        wire->hasReceiverSource = receiverSource != 0;
        wire->receiverSide = (RpgGridSide)sourceSide;
        if (wire->receiverSide < RPG_GRID_SIDE_TOP || wire->receiverSide > RPG_GRID_SIDE_LEFT ||
            (!(isV4Format || isV5Format || isV6Format) && fscanf(file, "%d", &wire->path.cellCount) != 1) ||
            wire->path.cellCount < 1 ||
            wire->path.cellCount > RPG_WIRE_MAX_CELLS) {
            fclose(file);
            return false;
        }
        if (wire->kind == RPG_WIRE_KIND_CONVEYOR && wire->hasReceiverSource) {
            fclose(file);
            return false;
        }
        if (wire->hasReceiverSource && !RpgWires_IsCellInStage(wire->receiverCell.row,
                                                                wire->receiverCell.column)) {
            fclose(file);
            return false;
        }
        if (!wire->hasReceiverSource && wire->path.cellCount < 2) {
            fclose(file);
            return false;
        }
        for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++) {
            RpgWireCell *cell = &wire->path.cells[cellIndex];
            if ((!(isV4Format || isV5Format || isV6Format) && fscanf(file, "%d %d", &cell->row, &cell->column) != 2) ||
                !RpgWires_IsCellInStage(cell->row, cell->column)) {
                fclose(file);
                return false;
            }
        }
        /* v5 and older did not state ownership.  Recover the one meaningful
           owner deterministically, then discard legacy standalone paths in
           RpgWires_RemoveBroken() after the stage is available. */
        if (!isV6Format)
            wire->ownerCell = wire->hasReceiverSource ? wire->receiverCell : wire->path.cells[0];
        if (!RpgWires_IsCellInStage(wire->ownerCell.row, wire->ownerCell.column)) {
            fclose(file);
            return false;
        }
    }
    if (fclose(file) != 0) return false;
    *wires = loaded;
    return true;
}

bool RpgWires_Save(const char *filePath, const RpgWires *wires)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "v6 %d\n", wires->count);
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        fprintf(file, "%d %.2f %d %.1f %d %d %d %d %d %d %d %d", wire->kind, wire->conveyorSpeed,
                wire->conveyorDirection, wire->conveyorSlideAngleDegrees,
                wire->conveyorHasFloor ? 1 : 0,
                wire->ownerCell.row, wire->ownerCell.column,
                wire->hasReceiverSource ? 1 : 0,
                wire->receiverCell.row, wire->receiverCell.column, wire->receiverSide,
                wire->path.cellCount);
        for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++)
            fprintf(file, " %d %d", wire->path.cells[cellIndex].row,
                    wire->path.cells[cellIndex].column);
        fputc('\n', file);
    }
    return fclose(file) == 0;
}

bool RpgWires_AddAdjacentConveyor(RpgWires *wires, const RpgStage *stage, int row, int column)
{
    static const int columnOffsets[] = { 1, -1, 0, 0 };
    static const int rowOffsets[] = { 0, 0, 1, -1 };
    if (wires == NULL || stage == NULL || wires->count >= RPG_WIRE_MAX_COUNT ||
        !RpgWires_IsBlockCell(stage, row, column)) return false;
    for (int direction = 0; direction < 4; direction++) {
        int adjacentRow = row + rowOffsets[direction];
        int adjacentColumn = column + columnOffsets[direction];
        if (!RpgWires_IsBlockCell(stage, adjacentRow, adjacentColumn)) continue;
        wires->entries[wires->count++] = (RpgWire){
            .ownerCell = { row, column },
            .path = RpgGridPath_Create((RpgWireCell){ row, column },
                                       (RpgWireCell){ adjacentRow, adjacentColumn }),
            .kind = RPG_WIRE_KIND_CONVEYOR,
            .conveyorSpeed = 96.0f,
            .conveyorDirection = 1,
            .conveyorHasFloor = true,
            .conveyorSlideAngleDegrees = 15.0f
        };
        return true;
    }
    return false;
}

bool RpgWires_AddFromReceiver(RpgWires *wires, const RpgStage *stage, RpgWireCell cell,
                              RpgGridSide side)
{
    if (wires->count >= RPG_WIRE_MAX_COUNT || !RpgWires_IsBlockCell(stage, cell.row, cell.column))
        return false;
    wires->entries[wires->count++] = (RpgWire){
        .ownerCell = cell,
        .path = { .cellCount = 1, .cells = { cell } },
        .kind = RPG_WIRE_KIND_ELECTRIC,
        .conveyorSpeed = 96.0f,
        .conveyorDirection = 1,
        .hasReceiverSource = true,
        .receiverCell = cell,
        .receiverSide = side
    };
    return true;
}

bool RpgWires_FindEndpoint(const RpgWires *wires, int row, int column, int *wireIndex,
                           bool *isStart)
{
    for (int index = wires->count - 1; index >= 0; index--) {
        const RpgWire *wire = &wires->entries[index];
        if (RpgGridPath_IsEndpoint(&wire->path, (RpgWireCell){ row, column }, isStart)) {
            if (wire->hasReceiverSource && *isStart) {
                if (wire->path.cellCount == 1) *isStart = false;
                else continue;
            }
            *wireIndex = index;
            return true;
        }
    }
    return false;
}

bool RpgWires_MoveEndpoint(RpgWires *wires, const RpgStage *stage, int wireIndex,
                           bool isStart, int row, int column)
{
    if (wireIndex < 0 || wireIndex >= wires->count || !RpgWires_IsBlockCell(stage, row, column))
        return false;
    const RpgWire *wire = &wires->entries[wireIndex];
    // 受容体由来の導線だけは始点まで縮め、最短の1マス経路へ戻せる。
    int minimumCellCount = wire->hasReceiverSource && !isStart ? 1 : 2;
    return RpgGridPath_MoveEndpoint(&wires->entries[wireIndex].path, isStart,
                                    (RpgWireCell){ row, column }, minimumCellCount);
}

void RpgWires_RemoveBroken(RpgWires *wires, const RpgStage *stage)
{
    for (int wireIndex = 0; wireIndex < wires->count;) {
        const RpgWire *wire = &wires->entries[wireIndex];
        /* There is no independent electric-wire object any more.  A legacy
           path without a receiver owner is intentionally removed. */
        bool isBroken = wire->kind == RPG_WIRE_KIND_ELECTRIC && !wire->hasReceiverSource;
        if (!isBroken && !RpgWires_IsBlockCell(stage, wire->ownerCell.row, wire->ownerCell.column))
            isBroken = true;
        for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++) {
            if (!RpgWires_IsBlockCell(stage, wire->path.cells[cellIndex].row,
                                      wire->path.cells[cellIndex].column)) {
                isBroken = true;
                break;
            }
        }
        if (!isBroken) {
            wireIndex++;
            continue;
        }
        for (int next = wireIndex; next < wires->count - 1; next++)
            wires->entries[next] = wires->entries[next + 1];
        wires->count--;
    }
}
static bool RpgWires_IsDoorCell(const RpgStage *stage, RpgWireCell cell)
{
    return RpgWires_IsCellInStage(cell.row, cell.column) &&
           RpgBlockInventory_IsDoorBlock(stage->blocks[cell.row][cell.column]);
}

static void RpgWires_DrawSegment(Vector2 start, Vector2 end, bool startInDoor, bool endInDoor)
{
    Color wireColor = Fade(SKYBLUE, 0.92f);
    Color coreColor = RAYWHITE;
    if (!startInDoor && !endInDoor) {
        DrawLineEx(start, end, 5.0f, wireColor);
        DrawLineEx(start, end, 1.5f, coreColor);
        return;
    }
    if (startInDoor && endInDoor) {
        DrawLineEx(start, end, 5.0f, Fade(wireColor, 0.32f));
        DrawLineEx(start, end, 1.5f, Fade(coreColor, 0.38f));
        return;
    }
    // ドアの中心から半分だけを薄くし、ドア外の導線の見やすさは維持する。
    Vector2 midpoint = { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f };
    if (startInDoor) {
        DrawLineEx(start, endInDoor ? end : midpoint, 5.0f, Fade(wireColor, 0.32f));
        DrawLineEx(start, endInDoor ? end : midpoint, 1.5f, Fade(coreColor, 0.38f));
    }
    if (endInDoor) {
        DrawLineEx(startInDoor ? start : midpoint, end, 5.0f, Fade(wireColor, 0.32f));
        DrawLineEx(startInDoor ? start : midpoint, end, 1.5f, Fade(coreColor, 0.38f));
    }
    if (startInDoor && !endInDoor) {
        DrawLineEx(midpoint, end, 5.0f, wireColor);
        DrawLineEx(midpoint, end, 1.5f, coreColor);
    }
    if (!startInDoor && endInDoor) {
        DrawLineEx(start, midpoint, 5.0f, wireColor);
        DrawLineEx(start, midpoint, 1.5f, coreColor);
    }
    // ドアに入る境界だけを強調し、導線そのものの点には追加装飾を置かない。
    if (start.y == end.y)
        DrawLineEx((Vector2){ midpoint.x, midpoint.y - 14.0f },
                   (Vector2){ midpoint.x, midpoint.y + 14.0f }, 3.0f, GOLD);
    else
        DrawLineEx((Vector2){ midpoint.x - 14.0f, midpoint.y },
                   (Vector2){ midpoint.x + 14.0f, midpoint.y }, 3.0f, GOLD);
    DrawCircleV(midpoint, 5.0f, Fade(GOLD, 0.92f));
    DrawCircleLines((int)midpoint.x, (int)midpoint.y, 5.0f, RAYWHITE);
}

static void RpgWires_DrawEndpoint(Vector2 position, Color color, const char *label, bool isInDoor)
{
    Color endpointColor = isInDoor ? Fade(color, 0.38f) : color;
    Color outlineColor = isInDoor ? Fade(RAYWHITE, 0.45f) : RAYWHITE;
    DrawCircleV(position, 9.0f, endpointColor);
    DrawCircleLines((int)position.x, (int)position.y, 9.0f, outlineColor);
    DrawText(label, (int)position.x - 5, (int)position.y - 8, 15, outlineColor);
}

static Vector2 RpgWires_GetCellCenter(RpgWireCell cell, int firstColumn)
{
    return (Vector2){ (cell.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                      cell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f };
}

static Vector2 RpgWires_GetReceiverAnchor(RpgWireCell cell, RpgGridSide side, int firstColumn)
{
    Vector2 anchor = RpgWires_GetCellCenter(cell, firstColumn);
    if (side == RPG_GRID_SIDE_TOP) anchor.y -= RPG_STAGE_TILE_SIZE * 0.5f;
    else if (side == RPG_GRID_SIDE_RIGHT) anchor.x += RPG_STAGE_TILE_SIZE * 0.5f;
    else if (side == RPG_GRID_SIDE_BOTTOM) anchor.y += RPG_STAGE_TILE_SIZE * 0.5f;
    else anchor.x -= RPG_STAGE_TILE_SIZE * 0.5f;
    return anchor;
}

// 受容体のくぼみ部分を避ける位置から導線を描き、辺を変えても見た目が重ならないようにする。
static Vector2 RpgWires_GetReceiverWireStart(Vector2 anchor, Vector2 destination)
{
    float dx = destination.x - anchor.x;
    float dy = destination.y - anchor.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length <= 8.0f) return anchor;
    return (Vector2){ anchor.x + dx / length * 8.0f, anchor.y + dy / length * 8.0f };
}

bool RpgWires_IsConveyor(const RpgWire *wire)
{
    return wire != NULL && wire->kind == RPG_WIRE_KIND_CONVEYOR;
}

RpgGridCell RpgWires_GetOwnerCell(const RpgWire *wire)
{
    return wire != NULL ? wire->ownerCell : (RpgGridCell){ -1, -1 };
}

static bool RpgWires_AreAdjacent(RpgWireCell first, RpgWireCell second)
{
    return abs(first.row - second.row) + abs(first.column - second.column) == 1;
}

bool RpgWires_IsConveyorClosed(const RpgWire *wire)
{
    return RpgWires_IsConveyor(wire) && wire->path.cellCount >= 3 &&
           RpgWires_AreAdjacent(wire->path.cells[0],
                                 wire->path.cells[wire->path.cellCount - 1]);
}

static Vector2 RpgWires_GetConveyorNextCell(const RpgWire *wire, int cellIndex)
{
    int nextIndex = cellIndex + (wire->conveyorDirection >= 0 ? 1 : -1);
    if (nextIndex >= 0 && nextIndex < wire->path.cellCount)
        return (Vector2){ (float)wire->path.cells[nextIndex].column,
                          (float)wire->path.cells[nextIndex].row };
    if (RpgWires_IsConveyorClosed(wire)) {
        int wrapIndex = nextIndex < 0 ? wire->path.cellCount - 1 : 0;
        return (Vector2){ (float)wire->path.cells[wrapIndex].column,
                          (float)wire->path.cells[wrapIndex].row };
    }
    return (Vector2){ (float)wire->path.cells[cellIndex].column,
                      (float)wire->path.cells[cellIndex].row };
}

static void RpgWires_DrawConveyorCell(const RpgWire *wire, int cellIndex, int firstColumn)
{
    RpgWireCell cell = wire->path.cells[cellIndex];
    Vector2 center = RpgWires_GetCellCenter(cell, firstColumn);
    Vector2 next = RpgWires_GetConveyorNextCell(wire, cellIndex);
    Vector2 direction = Vector2Normalize((Vector2){ next.x - cell.column, next.y - cell.row });
    Rectangle belt = { center.x - 14.0f, center.y - 14.0f, 28.0f, 28.0f };
    DrawRectangleRec(belt, (Color){ 54, 66, 74, 255 });
    DrawRectangleLinesEx(belt, 1.5f, (Color){ 15, 18, 21, 255 });
    if (Vector2LengthSqr(direction) <= 0.0f) return;
    Vector2 arrowCenter = Vector2Add(center, Vector2Scale(direction,
        fmodf((float)GetTime() * wire->conveyorSpeed * (wire->conveyorDirection >= 0 ? 1.0f : -1.0f), 12.0f) - 6.0f));
    Vector2 tip = Vector2Add(arrowCenter, Vector2Scale(direction, 7.0f));
    Vector2 tail = Vector2Subtract(arrowCenter, Vector2Scale(direction, 6.0f));
    Vector2 side = { -direction.y, direction.x };
    DrawLineEx(tail, tip, 2.0f, Fade(GOLD, 0.82f));
    DrawTriangle(tip, Vector2Add(tail, Vector2Scale(side, 5.0f)),
                 Vector2Subtract(tail, Vector2Scale(side, 5.0f)), Fade(GOLD, 0.82f));
}

typedef struct RpgConveyorBoundaryEdge { Vector2 from; Vector2 to; } RpgConveyorBoundaryEdge;

static bool RpgWires_ConveyorContainsCell(const RpgWire *wire, int row, int column,
                                          int firstColumn, int lastColumn)
{
    if (column < firstColumn || column >= lastColumn) return false;
    for (int index = 0; index < wire->path.cellCount; index++)
        if (wire->path.cells[index].row == row && wire->path.cells[index].column == column)
            return true;
    return false;
}

static int RpgWires_CollectConveyorBoundary(const RpgWire *wire, int firstColumn, int lastColumn,
                                            RpgConveyorBoundaryEdge edges[], int capacity)
{
    int count = 0;
    for (int index = 0; index < wire->path.cellCount; index++) {
        const RpgWireCell cell = wire->path.cells[index];
        /* A map is rendered in its own local coordinate space.  Never let a
           conveyor cell owned by another storage slot contribute an outer
           roller or floor paddle to this map. */
        if (cell.column < firstColumn || cell.column >= lastColumn) continue;
        float left = (cell.column - firstColumn) * RPG_STAGE_TILE_SIZE;
        float top = cell.row * RPG_STAGE_TILE_SIZE;
        float right = left + RPG_STAGE_TILE_SIZE;
        float bottom = top + RPG_STAGE_TILE_SIZE;
        /* Edges are clockwise: the block interior is always on their right,
           so the left normal points to the outside of the whole track. */
        if (!RpgWires_ConveyorContainsCell(wire, cell.row - 1, cell.column,
                                            firstColumn, lastColumn) && count < capacity)
            edges[count++] = (RpgConveyorBoundaryEdge){ { left, top }, { right, top } };
        if (!RpgWires_ConveyorContainsCell(wire, cell.row, cell.column + 1,
                                            firstColumn, lastColumn) && count < capacity)
            edges[count++] = (RpgConveyorBoundaryEdge){ { right, top }, { right, bottom } };
        if (!RpgWires_ConveyorContainsCell(wire, cell.row + 1, cell.column,
                                            firstColumn, lastColumn) && count < capacity)
            edges[count++] = (RpgConveyorBoundaryEdge){ { right, bottom }, { left, bottom } };
        if (!RpgWires_ConveyorContainsCell(wire, cell.row, cell.column - 1,
                                            firstColumn, lastColumn) && count < capacity)
            edges[count++] = (RpgConveyorBoundaryEdge){ { left, bottom }, { left, top } };
    }
    return count;
}

static bool RpgWires_SamePoint(Vector2 first, Vector2 second)
{
    return first.x == second.x && first.y == second.y;
}

static int RpgWires_FindBoundarySuccessor(const RpgConveyorBoundaryEdge edges[], int edgeCount,
                                          const bool used[], Vector2 point)
{
    for (int index = 0; index < edgeCount; index++)
        if (!used[index] && RpgWires_SamePoint(edges[index].from, point)) return index;
    return -1;
}

static void RpgWires_DrawConveyorBoundaryPaddles(const RpgWire *wire,
                                                 const RpgConveyorBoundaryEdge edges[],
                                                 const int orderedEdges[], int edgeCount);

static void RpgWires_DrawConveyorBoundaryLoop(const RpgWire *wire,
                                              const RpgConveyorBoundaryEdge edges[],
                                              const int orderedEdges[], int edgeCount)
{
    const float rollerSpacing = 6.0f;
    float perimeter = 0.0f;
    for (int index = 0; index < edgeCount; index++)
        perimeter += Vector2Distance(edges[orderedEdges[index]].from, edges[orderedEdges[index]].to);
    if (perimeter <= 0.0f) return;
    /* Boundary edges are traced clockwise, which is opposite to the
       conveyor's path-direction convention.  Reverse only the visual phase;
       the conveyor's physical velocity remains unchanged. */
    float travel = fmodf((float)GetTime() * wire->conveyorSpeed *
                         (wire->conveyorDirection >= 0 ? -1.0f : 1.0f), perimeter);
    int rollerCount = (int)ceilf(perimeter / rollerSpacing);
    for (int rollerIndex = 0; rollerIndex < rollerCount; rollerIndex++) {
        float distance = (float)rollerIndex * rollerSpacing + travel;
        if (distance < 0.0f) distance += perimeter;
        distance = fmodf(distance, perimeter);
        for (int edgeIndex = 0; edgeIndex < edgeCount; edgeIndex++) {
            const RpgConveyorBoundaryEdge edge = edges[orderedEdges[edgeIndex]];
            Vector2 delta = Vector2Subtract(edge.to, edge.from);
            float length = Vector2Length(delta);
            if (distance > length && edgeIndex + 1 < edgeCount) { distance -= length; continue; }
            Vector2 direction = Vector2Scale(delta, 1.0f / length);
            Vector2 outside = { -direction.y, direction.x };
            Vector2 center = Vector2Add(Vector2Add(edge.from, Vector2Scale(direction, distance)),
                                        Vector2Scale(outside, 1.5f));
            Color roller = (rollerIndex & 1) == 0 ? (Color){ 16, 19, 22, 255 } :
                                                     (Color){ 123, 130, 137, 255 };
            DrawRectangle((int)roundf(center.x - 2.0f), (int)roundf(center.y - 2.0f), 4, 4, roller);
            break;
        }
    }
}

static void RpgWires_DrawConveyorOuterRollers(const RpgWire *wire, int firstColumn, int lastColumn)
{
    enum { MAX_BOUNDARY_EDGES = RPG_WIRE_MAX_CELLS * 4 };
    RpgConveyorBoundaryEdge edges[MAX_BOUNDARY_EDGES];
    bool used[MAX_BOUNDARY_EDGES] = { 0 };
    int edgeCount = RpgWires_CollectConveyorBoundary(wire, firstColumn, lastColumn,
                                                      edges, MAX_BOUNDARY_EDGES);
    for (int firstEdge = 0; firstEdge < edgeCount; firstEdge++) {
        int orderedEdges[MAX_BOUNDARY_EDGES];
        int orderedCount = 0;
        int edge = firstEdge;
        if (used[edge]) continue;
        while (edge >= 0 && !used[edge] && orderedCount < MAX_BOUNDARY_EDGES) {
            orderedEdges[orderedCount++] = edge;
            used[edge] = true;
            edge = RpgWires_FindBoundarySuccessor(edges, edgeCount, used, edges[edge].to);
        }
        RpgWires_DrawConveyorBoundaryLoop(wire, edges, orderedEdges, orderedCount);
        if (wire->conveyorHasFloor)
            RpgWires_DrawConveyorBoundaryPaddles(wire, edges, orderedEdges, orderedCount);
    }
}

typedef struct RpgConveyorBoundaryPaddle {
    Vector2 pivot;
    /* Travel controls one-way wall behaviour.  Outward is separate and is
       derived from the boundary's clockwise winding, so reversing the belt
       never makes a floor blade flip into the inside of the conveyor. */
    Vector2 direction;
    Vector2 outward;
    Vector2 visualOutward;
} RpgConveyorBoundaryPaddle;

static float RpgWires_GetBoundaryLoopLength(const RpgConveyorBoundaryEdge edges[],
                                            const int orderedEdges[], int edgeCount)
{
    float length = 0.0f;
    for (int index = 0; index < edgeCount; index++)
        length += Vector2Distance(edges[orderedEdges[index]].from, edges[orderedEdges[index]].to);
    return length;
}

static RpgConveyorBoundaryEdge RpgWires_GetDirectedBoundaryEdge(
    const RpgConveyorBoundaryEdge edges[], const int orderedEdges[], int edgeCount,
    int edgeIndex, bool reverse)
{
    RpgConveyorBoundaryEdge edge = edges[orderedEdges[reverse ? edgeCount - 1 - edgeIndex : edgeIndex]];
    if (reverse) {
        Vector2 from = edge.from;
        edge.from = edge.to;
        edge.to = from;
    }
    return edge;
}

static bool RpgWires_BoundaryDirectionsTurn(RpgConveyorBoundaryEdge first,
                                             RpgConveyorBoundaryEdge second)
{
    Vector2 firstDirection = Vector2Normalize(Vector2Subtract(first.to, first.from));
    Vector2 secondDirection = Vector2Normalize(Vector2Subtract(second.to, second.from));
    return fabsf(firstDirection.x - secondDirection.x) > 0.001f ||
           fabsf(firstDirection.y - secondDirection.y) > 0.001f;
}

static float RpgWires_GetBoundaryPaddleCycleDuration(const RpgWire *wire,
                                                      const RpgConveyorBoundaryEdge edges[],
                                                      const int orderedEdges[], int edgeCount,
                                                      bool reverse)
{
    float speed = fmaxf(1.0f, wire->conveyorSpeed);
    float duration = 0.0f;
    for (int index = 0; index < edgeCount; index++) {
        RpgConveyorBoundaryEdge edge = RpgWires_GetDirectedBoundaryEdge(edges, orderedEdges,
                                                                          edgeCount, index, reverse);
        RpgConveyorBoundaryEdge next = RpgWires_GetDirectedBoundaryEdge(edges, orderedEdges,
                                                                          edgeCount, (index + 1) % edgeCount,
                                                                          reverse);
        duration += Vector2Distance(edge.from, edge.to) / speed;
        if (RpgWires_BoundaryDirectionsTurn(edge, next))
            duration += RPG_STAGE_TILE_SIZE / speed;
    }
    return duration;
}

static bool RpgWires_SampleTimedBoundaryPaddle(const RpgWire *wire,
                                                const RpgConveyorBoundaryEdge edges[],
                                                const int orderedEdges[], int edgeCount,
                                                float elapsed, bool reverse,
                                                RpgConveyorBoundaryPaddle *paddle)
{
    float speed = fmaxf(1.0f, wire->conveyorSpeed);
    float cycleDuration = RpgWires_GetBoundaryPaddleCycleDuration(wire, edges, orderedEdges,
                                                                   edgeCount, reverse);
    if (paddle == NULL || cycleDuration <= 0.0f) return false;
    elapsed = fmodf(elapsed, cycleDuration);
    if (elapsed < 0.0f) elapsed += cycleDuration;
    for (int index = 0; index < edgeCount; index++) {
        RpgConveyorBoundaryEdge edge = RpgWires_GetDirectedBoundaryEdge(edges, orderedEdges,
                                                                          edgeCount, index, reverse);
        RpgConveyorBoundaryEdge next = RpgWires_GetDirectedBoundaryEdge(edges, orderedEdges,
                                                                          edgeCount, (index + 1) % edgeCount,
                                                                          reverse);
        Vector2 direction = Vector2Normalize(Vector2Subtract(edge.to, edge.from));
        /* GetDirectedBoundaryEdge() reverses the edge only for belt travel.
           The source boundary is clockwise, whose left normal is always the
           geometric outside of the connected conveyor footprint. */
        Vector2 canonicalDirection = reverse ? Vector2Negate(direction) : direction;
        /* The authored conveyor footprint uses the opposite winding from the
           initial assumption above: its geometric exterior is the right
           normal of the canonical boundary direction. */
        Vector2 outward = { canonicalDirection.y, -canonicalDirection.x };
        float edgeDuration = Vector2Distance(edge.from, edge.to) / speed;
        if (elapsed < edgeDuration) {
            Vector2 point = Vector2Add(edge.from, Vector2Scale(direction, elapsed * speed));
            paddle->pivot = Vector2Add(point, Vector2Scale(outward, 1.5f));
            paddle->direction = direction;
            paddle->outward = outward;
            paddle->visualOutward = outward;
            return true;
        }
        elapsed -= edgeDuration;
        if (RpgWires_BoundaryDirectionsTurn(edge, next)) {
            float turnDuration = RPG_STAGE_TILE_SIZE / speed;
            if (elapsed < turnDuration) {
                Vector2 nextDirection = Vector2Normalize(Vector2Subtract(next.to, next.from));
                Vector2 nextCanonicalDirection = reverse ? Vector2Negate(nextDirection) : nextDirection;
                Vector2 nextOutward = { nextCanonicalDirection.y, -nextCanonicalDirection.x };
                float turnProgress = elapsed / turnDuration;
                paddle->pivot = Vector2Add(edge.to, Vector2Scale(outward, 1.5f));
                paddle->direction = direction;
                paddle->outward = outward;
                paddle->visualOutward = Vector2Normalize(Vector2Add(
                    Vector2Scale(outward, 1.0f - turnProgress),
                    Vector2Scale(nextOutward, turnProgress)));
                return true;
            }
            elapsed -= turnDuration;
        }
    }
    return false;
}

/* The collision code deliberately samples this same outer trajectory as the
   renderer.  It prevents the floor/wall collider from drifting back to the
   old cell-centre path when the visual conveyor is changed. */
static int RpgWires_CollectTimedBoundaryPaddles(const RpgWire *wire, float elapsed,
                                                RpgConveyorBoundaryPaddle paddles[], int capacity)
{
    enum { MAX_BOUNDARY_EDGES = RPG_WIRE_MAX_CELLS * 4 };
    RpgConveyorBoundaryEdge edges[MAX_BOUNDARY_EDGES];
    bool used[MAX_BOUNDARY_EDGES] = { 0 };
    int edgeCount;
    int count = 0;
    if (wire == NULL || paddles == NULL || capacity <= 0 || !RpgWires_IsConveyor(wire)) return 0;
    edgeCount = RpgWires_CollectConveyorBoundary(wire, 0, RPG_STAGE_WORLD_COLUMNS,
                                                  edges, MAX_BOUNDARY_EDGES);
    for (int start = 0; start < edgeCount && count < capacity; start++) {
        int orderedEdges[MAX_BOUNDARY_EDGES];
        int orderedCount = 0;
        int edge;
        bool reverse;
        float paddleInterval;
        float cycleDuration;
        int paddleCount;
        if (used[start]) continue;
        edge = start;
        while (edge >= 0 && !used[edge] && orderedCount < MAX_BOUNDARY_EDGES) {
            orderedEdges[orderedCount++] = edge;
            used[edge] = true;
            edge = RpgWires_FindBoundarySuccessor(edges, edgeCount, used, edges[edge].to);
        }
        if (orderedCount <= 0) continue;
        reverse = wire->conveyorDirection >= 0;
        paddleInterval = (RPG_STAGE_TILE_SIZE * 2.0f) / fmaxf(1.0f, wire->conveyorSpeed);
        cycleDuration = RpgWires_GetBoundaryPaddleCycleDuration(wire, edges, orderedEdges,
                                                                 orderedCount, reverse);
        paddleCount = (int)ceilf(cycleDuration / paddleInterval);
        for (int index = 0; index < paddleCount && count < capacity; index++) {
            if (RpgWires_SampleTimedBoundaryPaddle(wire, edges, orderedEdges, orderedCount,
                                                    elapsed + index * paddleInterval, reverse,
                                                    &paddles[count]))
                count++;
        }
    }
    return count;
}

static Rectangle RpgWires_GetBoundaryPaddleBounds(const RpgConveyorBoundaryPaddle *paddle,
                                                  bool *isVertical)
{
    const float paddleLength = RPG_STAGE_TILE_SIZE + 4.0f;
    const float paddleThickness = 5.0f;
    Vector2 outward = paddle->visualOutward;
    Vector2 end = Vector2Add(paddle->pivot, Vector2Scale(outward, paddleLength));
    float left = fminf(paddle->pivot.x, end.x) - paddleThickness * 0.5f;
    float top = fminf(paddle->pivot.y, end.y) - paddleThickness * 0.5f;
    if (isVertical != NULL) *isVertical = fabsf(outward.x) < 0.001f;
    return (Rectangle){ left, top, fabsf(end.x - paddle->pivot.x) + paddleThickness,
                        fabsf(end.y - paddle->pivot.y) + paddleThickness };
}

static RpgPhysicsSlope RpgWires_GetBoundaryPaddleSurface(const RpgConveyorBoundaryPaddle *paddle)
{
    const float paddleLength = RPG_STAGE_TILE_SIZE + 4.0f;
    Vector2 outward = paddle->visualOutward;
    return (RpgPhysicsSlope){ .start = paddle->pivot,
                              .end = Vector2Add(paddle->pivot,
                                                Vector2Scale(outward, paddleLength)),
                              .thickness = 5.0f };
}

static void RpgWires_DrawConveyorBoundaryPaddles(const RpgWire *wire,
                                                 const RpgConveyorBoundaryEdge edges[],
                                                 const int orderedEdges[], int edgeCount)
{
    const float paddleLength = RPG_STAGE_TILE_SIZE + 4.0f;
    const float paddleThickness = 5.0f;
    const float spacing = RPG_STAGE_TILE_SIZE * 2.0f;
    float perimeter = RpgWires_GetBoundaryLoopLength(edges, orderedEdges, edgeCount);
    float elapsed;
    int paddleCount;
    if (perimeter <= 0.0f) return;
    bool reverse = wire->conveyorDirection >= 0;
    float paddleInterval = spacing / fmaxf(1.0f, wire->conveyorSpeed);
    float cycleDuration = RpgWires_GetBoundaryPaddleCycleDuration(wire, edges, orderedEdges,
                                                                   edgeCount, reverse);
    elapsed = (float)GetTime();
    /* Paddles are separated by two tiles' worth of *time*.  Corner pauses
       therefore occupy a slot too, instead of making the whole loop sparse. */
    paddleCount = (int)ceilf(cycleDuration / paddleInterval);
    for (int index = 0; index < paddleCount; index++) {
        RpgConveyorBoundaryPaddle paddle;
        Vector2 visualOutward;
        if (!RpgWires_SampleTimedBoundaryPaddle(wire, edges, orderedEdges, edgeCount,
                                                 elapsed + index * paddleInterval, reverse, &paddle)) continue;
        visualOutward = paddle.visualOutward;
        float angle = atan2f(visualOutward.y, visualOutward.x) * RAD2DEG;
        Rectangle outline = { paddle.pivot.x,
                              paddle.pivot.y - (paddleThickness + 2.0f) * 0.5f,
                              paddleLength + 2.0f, paddleThickness + 2.0f };
        Rectangle blade = { paddle.pivot.x,
                            paddle.pivot.y - paddleThickness * 0.5f,
                            paddleLength, paddleThickness };
        DrawRectanglePro(outline, (Vector2){ 0.0f, outline.height * 0.5f }, angle,
                         (Color){ 13, 17, 20, 255 });
        DrawRectanglePro(blade, (Vector2){ 0.0f, blade.height * 0.5f }, angle,
                         (Color){ 211, 146, 60, 255 });
        DrawCircleV(paddle.pivot, 5.0f, (Color){ 28, 47, 62, 255 });
        DrawCircleV(paddle.pivot, 3.0f, (Color){ 70, 183, 216, 255 });
        DrawCircleLines((int)roundf(paddle.pivot.x), (int)roundf(paddle.pivot.y), 5.0f,
                        (Color){ 11, 16, 20, 255 });
    }
}

static void RpgWires_DrawConveyor(const RpgWire *wire, int firstColumn, int lastColumn)
{
    for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++) {
        RpgWireCell cell = wire->path.cells[cellIndex];
        if (cell.column >= firstColumn && cell.column < lastColumn)
            RpgWires_DrawConveyorCell(wire, cellIndex, firstColumn);
    }
    /* The rollers belong to the entire connected path, not to each tile. */
    RpgWires_DrawConveyorOuterRollers(wire, firstColumn, lastColumn);
    /* Adjacent endpoints form one continuous loop.  The short outer connector
       deliberately replaces start/end markers, so the seam is visually closed. */
    if (RpgWires_IsConveyorClosed(wire)) {
        RpgWireCell last = wire->path.cells[wire->path.cellCount - 1];
        RpgWireCell first = wire->path.cells[0];
        if (last.column >= firstColumn && last.column < lastColumn &&
            first.column >= firstColumn && first.column < lastColumn)
        {
            Vector2 from = RpgWires_GetCellCenter(last, firstColumn);
            Vector2 to = RpgWires_GetCellCenter(first, firstColumn);
            /* Keep the return leg outside the two tiles, rather than drawing
               another line through their shared interior edge. */
            Vector2 outside = fabsf(to.x - from.x) > fabsf(to.y - from.y) ?
                (Vector2){ 0.0f, -16.0f } : (Vector2){ -16.0f, 0.0f };
            DrawLineEx(from, Vector2Add(from, outside), 3.0f, GOLD);
            DrawLineEx(Vector2Add(from, outside), Vector2Add(to, outside), 3.0f, GOLD);
            DrawLineEx(Vector2Add(to, outside), to, 3.0f, GOLD);
        }
    }
}

Vector2 RpgWires_GetConveyorVelocityBelow(const RpgWires *wires, Rectangle bounds)
{
    if (wires == NULL || bounds.width <= 0.0f || bounds.height <= 0.0f) return (Vector2){ 0.0f, 0.0f };
    int row = (int)floorf((bounds.y + bounds.height + 0.5f) / RPG_STAGE_TILE_SIZE);
    int firstColumn = (int)floorf((bounds.x + 1.0f) / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((bounds.x + bounds.width - 1.0f) / RPG_STAGE_TILE_SIZE);
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        if (!RpgWires_IsConveyor(wire)) continue;
        for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++) {
            RpgWireCell cell = wire->path.cells[cellIndex];
            if (cell.row != row || cell.column < firstColumn || cell.column > lastColumn) continue;
            Vector2 next = RpgWires_GetConveyorNextCell(wire, cellIndex);
            Vector2 direction = Vector2Normalize((Vector2){ next.x - cell.column, next.y - cell.row });
            /* The visible outer rollers travel opposite this path-edge lookup
               (the boundary loop is ordered clockwise).  Bodies must follow
               the rendered belt, not the storage-order arrow. */
            return Vector2Scale(direction, -wire->conveyorSpeed);
        }
    }
    return (Vector2){ 0.0f, 0.0f };
}

bool RpgWires_FindConveyorPlatformLanding(const RpgWires *wires, Rectangle previousBounds,
                                          Rectangle candidateBounds, float *landingY)
{
    float nearestLandingY = 0.0f;
    bool found = false;
    if (wires == NULL || candidateBounds.y + candidateBounds.height <=
        previousBounds.y + previousBounds.height) return false;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        RpgConveyorBoundaryPaddle paddles[RPG_WIRE_MAX_CELLS * 4];
        int paddleCount;
        if (!RpgWires_IsConveyor(wire) || !wire->conveyorHasFloor) continue;
        paddleCount = RpgWires_CollectTimedBoundaryPaddles(wire, (float)GetTime(), paddles,
                                                            RPG_WIRE_MAX_CELLS * 4);
        for (int paddleIndex = 0; paddleIndex < paddleCount; paddleIndex++) {
            bool isVertical;
            RpgPhysicsSlope surface;
            float platformY;
            Rectangle paddleBounds = RpgWires_GetBoundaryPaddleBounds(&paddles[paddleIndex], &isVertical);
            if (isVertical) continue;
            /* Keep an exact axis-aligned floor path.  It is deliberately not
               reduced to the body's centre point: a body whose edge crosses
               the visible blade must land even when its centre has not yet
               entered the blade span. */
            if (fabsf(paddles[paddleIndex].visualOutward.y) < 0.001f) {
                bool overlapsX = candidateBounds.x + candidateBounds.width > paddleBounds.x &&
                                 candidateBounds.x < paddleBounds.x + paddleBounds.width;
                float previousBottom = previousBounds.y + previousBounds.height;
                float candidateBottom = candidateBounds.y + candidateBounds.height;
                if (!overlapsX || previousBottom > paddleBounds.y + 0.001f ||
                    candidateBottom < paddleBounds.y) continue;
                platformY = paddleBounds.y;
            } else {
                surface = RpgWires_GetBoundaryPaddleSurface(&paddles[paddleIndex]);
                if (!RpgPhysics_FindOneWaySlopeLanding(surface, previousBounds, candidateBounds,
                                                        &platformY)) continue;
            }
            if (!found || platformY < nearestLandingY) {
                nearestLandingY = platformY;
                found = true;
            }
        }
    }
    if (found && landingY != NULL) *landingY = nearestLandingY;
    return found;
}

bool RpgWires_FindConveyorSlopeBelow(const RpgWires *wires, Rectangle bounds,
                                     RpgPhysicsSlope *slope)
{
    if (wires == NULL || slope == NULL) return false;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        RpgConveyorBoundaryPaddle paddles[RPG_WIRE_MAX_CELLS * 4];
        int paddleCount;
        if (!RpgWires_IsConveyor(wire) || !wire->conveyorHasFloor) continue;
        paddleCount = RpgWires_CollectTimedBoundaryPaddles(wire, (float)GetTime(), paddles,
                                                            RPG_WIRE_MAX_CELLS * 4);
        for (int index = 0; index < paddleCount; index++) {
            bool isVertical;
            RpgPhysicsSlope candidate = RpgWires_GetBoundaryPaddleSurface(&paddles[index]);
            float angleDegrees;
            (void)RpgWires_GetBoundaryPaddleBounds(&paddles[index], &isVertical);
            angleDegrees = atan2f(fabsf(candidate.end.y - candidate.start.y),
                                  fabsf(candidate.end.x - candidate.start.x)) * RAD2DEG;
            if (isVertical || angleDegrees < wire->conveyorSlideAngleDegrees ||
                !RpgPhysics_IsBodyOnSlopeTop(candidate, bounds, 2.0f)) continue;
            *slope = candidate;
            return true;
        }
    }
    return false;
}

bool RpgWires_FindConveyorFloorMotion(const RpgWires *wires, Rectangle riderBounds,
                                      float previousElapsed, float currentElapsed,
                                      Rectangle *previousBounds, Rectangle *currentBounds)
{
    const float riderBottom = riderBounds.y + riderBounds.height;
    if (wires == NULL || previousBounds == NULL || currentBounds == NULL) return false;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        RpgConveyorBoundaryPaddle before[RPG_WIRE_MAX_CELLS * 4];
        RpgConveyorBoundaryPaddle after[RPG_WIRE_MAX_CELLS * 4];
        int count;
        if (!RpgWires_IsConveyor(wire) || !wire->conveyorHasFloor) continue;
        count = RpgWires_CollectTimedBoundaryPaddles(wire, previousElapsed, before,
                                                     RPG_WIRE_MAX_CELLS * 4);
        if (RpgWires_CollectTimedBoundaryPaddles(wire, currentElapsed, after,
                                                 RPG_WIRE_MAX_CELLS * 4) != count) continue;
        for (int index = 0; index < count; index++) {
            bool beforeVertical, afterVertical;
            Rectangle oldBlade = RpgWires_GetBoundaryPaddleBounds(&before[index], &beforeVertical);
            Rectangle newBlade = RpgWires_GetBoundaryPaddleBounds(&after[index], &afterVertical);
            if (beforeVertical || afterVertical ||
                riderBounds.x >= oldBlade.x + oldBlade.width ||
                riderBounds.x + riderBounds.width <= oldBlade.x ||
                /* A moving one-way floor can carry only a rider already on
                   its upper face.  Never attach a body rising from below. */
                riderBounds.y >= oldBlade.y || riderBottom > oldBlade.y + 2.0f ||
                riderBottom < oldBlade.y - 2.0f) continue;
            *previousBounds = oldBlade;
            *currentBounds = newBlade;
            return true;
        }
    }
    return false;
}

bool RpgWires_FindConveyorWallPush(const RpgWires *wires, Rectangle previousBounds,
                                   Rectangle candidateBounds, float *pushX)
{
    float bestPush = 0.0f;
    bool found = false;
    if (wires == NULL) return false;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        RpgConveyorBoundaryPaddle paddles[RPG_WIRE_MAX_CELLS * 4];
        int paddleCount;
        if (!RpgWires_IsConveyor(wire) || !wire->conveyorHasFloor) continue;
        paddleCount = RpgWires_CollectTimedBoundaryPaddles(wire, (float)GetTime(), paddles,
                                                            RPG_WIRE_MAX_CELLS * 4);
        for (int paddleIndex = 0; paddleIndex < paddleCount; paddleIndex++) {
            Rectangle wall;
            float pushLeft, pushRight, push;
            float bodyMovementX;
            bool isVertical;
            wall = RpgWires_GetBoundaryPaddleBounds(&paddles[paddleIndex], &isVertical);
            /* A blade only becomes a wall while the conveyor travels fully
               horizontally (the drawn blade is therefore vertical). */
            if (!isVertical || fabsf(paddles[paddleIndex].direction.x) < 0.999f) continue;
            bodyMovementX = candidateBounds.x - previousBounds.x;
            /* Detect both an overlap and a complete one-frame crossing of the
               5px blade.  This keeps the directional wall solid at normal
               gameplay speeds instead of allowing it to be skipped. */
            bool overlapsY = candidateBounds.y + candidateBounds.height > wall.y &&
                             candidateBounds.y < wall.y + wall.height;
            bool crossesWall = false;
            if (paddles[paddleIndex].direction.x > 0.0f)
                crossesWall = previousBounds.x >= wall.x + wall.width - 0.001f &&
                              candidateBounds.x < wall.x + wall.width;
            else
                crossesWall = previousBounds.x + previousBounds.width <= wall.x + 0.001f &&
                              candidateBounds.x + candidateBounds.width > wall.x;
            if (!overlapsY || (!CheckCollisionRecs(candidateBounds, wall) && !crossesWall)) continue;
            /* The blade is a one-way wall: pass through in its travel direction,
               block from the opposite side.  Checking the body's actual movement
               also keeps this true when a frame begins with a tiny overlap. */
            if ((paddles[paddleIndex].direction.x > 0.0f &&
                 (previousBounds.x + previousBounds.width <= wall.x + 0.001f ||
                  bodyMovementX > 0.0001f)) ||
                (paddles[paddleIndex].direction.x < 0.0f &&
                 (previousBounds.x >= wall.x + wall.width - 0.001f ||
                  bodyMovementX < -0.0001f))) continue;
            pushLeft = wall.x - (candidateBounds.x + candidateBounds.width);
            pushRight = wall.x + wall.width - candidateBounds.x;
            if (previousBounds.x + previousBounds.width <= wall.x) push = pushLeft;
            else if (previousBounds.x >= wall.x + wall.width) push = pushRight;
            else push = fabsf(pushLeft) <= fabsf(pushRight) ? pushLeft : pushRight;
            if (!found || fabsf(push) < fabsf(bestPush)) {
                bestPush = push;
                found = true;
            }
        }
    }
    if (found && pushX != NULL) *pushX = bestPush;
    return found;
}

static void RpgWires_DrawWithOffset(const RpgWires *wires, const RpgStage *stage,
                                    int firstColumn, int columnCount)
{
    int lastColumn = firstColumn + columnCount;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        if (RpgWires_IsConveyor(wire)) {
            RpgWires_DrawConveyor(wire, firstColumn, lastColumn);
            continue;
        }
        for (int cellIndex = 0; cellIndex < wire->path.cellCount - 1; cellIndex++) {
            RpgWireCell first = wire->path.cells[cellIndex];
            RpgWireCell second = wire->path.cells[cellIndex + 1];
            if (first.column < firstColumn || first.column >= lastColumn ||
                second.column < firstColumn || second.column >= lastColumn) continue;
            RpgWires_DrawSegment(RpgWires_GetCellCenter(first, firstColumn),
                                 RpgWires_GetCellCenter(second, firstColumn),
                                 RpgWires_IsDoorCell(stage, first),
                                 RpgWires_IsDoorCell(stage, second));
        }
        RpgWireCell startCell = wire->path.cells[0];
        RpgWireCell endCell = wire->path.cells[wire->path.cellCount - 1];
        if (startCell.column >= firstColumn && startCell.column < lastColumn) {
            Vector2 start = RpgWires_GetCellCenter(startCell, firstColumn);
            if (wire->hasReceiverSource) {
                Vector2 anchor = RpgWires_GetReceiverAnchor(wire->receiverCell, wire->receiverSide,
                                                            firstColumn);
                Vector2 wireStart = RpgWires_GetReceiverWireStart(anchor, start);
                RpgWires_DrawSegment(wireStart, start, RpgWires_IsDoorCell(stage, startCell),
                                     RpgWires_IsDoorCell(stage, startCell));
            } else RpgWires_DrawEndpoint(start, DARKGREEN, "S", RpgWires_IsDoorCell(stage, startCell));
        }
        if (endCell.column >= firstColumn && endCell.column < lastColumn) {
            Vector2 end = RpgWires_GetCellCenter(endCell, firstColumn);
            RpgWires_DrawEndpoint(end, MAROON, "E", RpgWires_IsDoorCell(stage, endCell));
        }
    }
}

void RpgWires_Draw(const RpgWires *wires, const RpgStage *stage)
{
    RpgWires_DrawWithOffset(wires, stage, 0, RPG_STAGE_WORLD_COLUMNS);
}

void RpgWires_DrawMap(const RpgWires *wires, const RpgStage *stage, int mapIndex)
{
    RpgWires_DrawWithOffset(wires, stage, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
}

#if 0
RpgWireSignals RpgWireSignals_Default(void) { return (RpgWireSignals){ 0 }; }

void RpgWireSignals_TriggerFromReceiver(RpgWireSignals *signals, const RpgWires *wires,
                                        RpgGridCell receiverCell, RpgGridSide receiverSide)
{
    if (signals == NULL || wires == NULL) return;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        if (RpgWires_IsConveyor(wire)) continue;
        if (!wire->hasReceiverSource || wire->receiverCell.row != receiverCell.row ||
            wire->receiverCell.column != receiverCell.column || wire->receiverSide != receiverSide) continue;
        // 同じ導線へ再入力された場合は、先頭から流れ直す。
        signals->entries[wireIndex] = (RpgWireSignal){ .active = true, .wireIndex = wireIndex,
                                                        .reachedCellCount = 1 };
    }
}

void RpgWireSignals_Update(RpgWireSignals *signals, const RpgWires *wires,
                           float deltaTime, float cellDelay)
{
    if (signals == NULL || wires == NULL) return;
    if (cellDelay < 0.01f) cellDelay = 0.01f;
    for (int index = 0; index < RPG_WIRE_MAX_COUNT; index++) {
        RpgWireSignal *signal = &signals->entries[index];
        if (!signal->active) continue;
        if (signal->wireIndex < 0 || signal->wireIndex >= wires->count) { signal->active = false; continue; }
        const RpgWire *wire = &wires->entries[signal->wireIndex];
        signal->delayElapsed += deltaTime;
        while (signal->delayElapsed >= cellDelay) {
            signal->delayElapsed -= cellDelay;
            signal->reachedCellCount++;
            // 末端の発光を1区間だけ維持した後に消し、導線に残光を残さない。
            if (signal->reachedCellCount > wire->path.cellCount) { signal->active = false; break; }
        }
    }
}

static void RpgWireSignals_DrawGlowSegment(Vector2 start, Vector2 end)
{
    DrawLineEx(start, end, 11.0f, Fade(YELLOW, 0.18f));
    DrawLineEx(start, end, 6.0f, Fade(GOLD, 0.68f));
    DrawLineEx(start, end, 2.0f, RAYWHITE);
}

void RpgWireSignals_Draw(const RpgWireSignals *signals, const RpgWires *wires,
                         int firstColumn, int columnCount)
{
    if (signals == NULL || wires == NULL) return;
    int lastColumn = firstColumn + columnCount;
    for (int signalIndex = 0; signalIndex < RPG_WIRE_MAX_COUNT; signalIndex++) {
        const RpgWireSignal *signal = &signals->entries[signalIndex];
        if (!signal->active || signal->wireIndex < 0 || signal->wireIndex >= wires->count) continue;
        const RpgWire *wire = &wires->entries[signal->wireIndex];
        int reachedCount = signal->reachedCellCount;
        if (reachedCount > wire->path.cellCount) reachedCount = wire->path.cellCount;
        if (reachedCount <= 0) continue;
        if (wire->hasReceiverSource) {
            RpgWireCell firstCell = wire->path.cells[0];
            if (firstCell.column >= firstColumn && firstCell.column < lastColumn) {
                Vector2 anchor = RpgWires_GetReceiverAnchor(wire->receiverCell, wire->receiverSide, firstColumn);
                RpgWireSignals_DrawGlowSegment(anchor, RpgWires_GetCellCenter(firstCell, firstColumn));
            }
        }
        for (int cellIndex = 0; cellIndex < reachedCount; cellIndex++) {
            RpgWireCell cell = wire->path.cells[cellIndex];
            if (cell.column < firstColumn || cell.column >= lastColumn) continue;
            Vector2 center = RpgWires_GetCellCenter(cell, firstColumn);
            DrawCircleV(center, 13.0f, Fade(YELLOW, 0.18f));
            DrawCircleV(center, 5.0f, Fade(GOLD, 0.92f));
            if (cellIndex > 0) {
                RpgWireCell previous = wire->path.cells[cellIndex - 1];
                if (previous.column >= firstColumn && previous.column < lastColumn)
                    RpgWireSignals_DrawGlowSegment(RpgWires_GetCellCenter(previous, firstColumn), center);
            }
        }
    }
}
#endif

static bool RpgWires_HasElectricDataShot(const RpgDataShots *dataShots, RpgWireCell cell)
{
    for (int shotIndex = 0; shotIndex < RPG_DATA_SHOT_MAX_COUNT; shotIndex++) {
        const RpgDataShot *shot = &dataShots->entries[shotIndex];
        if (!shot->active || !shot->isElectric) continue;
        int row = (int)floorf(shot->position.y / RPG_STAGE_TILE_SIZE);
        int column = (int)floorf(shot->position.x / RPG_STAGE_TILE_SIZE);
        if (row == cell.row && column == cell.column) return true;
    }
    return false;
}

static void RpgWires_DrawElectricGlow(Vector2 position)
{
    DrawCircleV(position, 18.0f, Fade(YELLOW, 0.14f));
    DrawCircleV(position, 11.0f, Fade(GOLD, 0.42f));
    DrawCircleV(position, 5.0f, RAYWHITE);
}

void RpgWires_DrawElectric(const RpgWires *wires, const RpgDataShots *dataShots,
                           int firstColumn, int columnCount)
{
    if (wires == NULL || dataShots == NULL) return;
    int lastColumn = firstColumn + columnCount;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++) {
            RpgWireCell cell = wire->path.cells[cellIndex];
            if (cell.column < firstColumn || cell.column >= lastColumn ||
                !RpgWires_HasElectricDataShot(dataShots, cell)) continue;
            RpgWires_DrawElectricGlow(RpgWires_GetCellCenter(cell, firstColumn));
        }
    }
}
// 役割: 受容体・導線・端点の接続情報と描画を管理する。
