// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_grid_path.h, rpg_wire.h, rpg_stage.h
#include "rpg_wire.h"

#include "rpg_block_inventory.h"
#include "rpg_data_shot.h"

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
    bool isCurrentFormat = false;
    if (file == NULL) return false;
    if (fscanf(file, "%15s", format) != 1) {
        fclose(file);
        return false;
    }
    isCurrentFormat = strcmp(format, "v2") == 0;
    if ((isCurrentFormat ? fscanf(file, "%d", &loaded.count) :
                           sscanf(format, "%d", &loaded.count)) != 1 || loaded.count < 0 ||
        loaded.count > RPG_WIRE_MAX_COUNT) {
        fclose(file);
        return false;
    }
    for (int wireIndex = 0; wireIndex < loaded.count; wireIndex++) {
        RpgWire *wire = &loaded.entries[wireIndex];
        int sourceSide = RPG_GRID_SIDE_TOP;
        int receiverSource = 0;
        if (isCurrentFormat &&
            fscanf(file, "%d %d %d %d", &receiverSource,
                   &wire->receiverCell.row, &wire->receiverCell.column, &sourceSide) != 4) {
            fclose(file);
            return false;
        }
        if (receiverSource != 0 && receiverSource != 1) {
            fclose(file);
            return false;
        }
        wire->hasReceiverSource = receiverSource != 0;
        wire->receiverSide = (RpgGridSide)sourceSide;
        if (wire->receiverSide < RPG_GRID_SIDE_TOP || wire->receiverSide > RPG_GRID_SIDE_LEFT ||
            fscanf(file, "%d", &wire->path.cellCount) != 1 || wire->path.cellCount < 1 ||
            wire->path.cellCount > RPG_WIRE_MAX_CELLS) {
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
            if (fscanf(file, "%d %d", &cell->row, &cell->column) != 2 ||
                !RpgWires_IsCellInStage(cell->row, cell->column)) {
                fclose(file);
                return false;
            }
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
    fprintf(file, "v2 %d\n", wires->count);
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        fprintf(file, "%d %d %d %d %d", wire->hasReceiverSource ? 1 : 0,
                wire->receiverCell.row, wire->receiverCell.column, wire->receiverSide,
                wire->path.cellCount);
        for (int cellIndex = 0; cellIndex < wire->path.cellCount; cellIndex++)
            fprintf(file, " %d %d", wire->path.cells[cellIndex].row,
                    wire->path.cells[cellIndex].column);
        fputc('\n', file);
    }
    return fclose(file) == 0;
}

bool RpgWires_AddAdjacent(RpgWires *wires, const RpgStage *stage, int row, int column)
{
    static const int columnOffsets[] = { 1, -1, 0, 0 };
    static const int rowOffsets[] = { 0, 0, 1, -1 };
    if (wires->count >= RPG_WIRE_MAX_COUNT || !RpgWires_IsBlockCell(stage, row, column)) return false;
    for (int direction = 0; direction < 4; direction++) {
        int adjacentRow = row + rowOffsets[direction];
        int adjacentColumn = column + columnOffsets[direction];
        if (!RpgWires_IsBlockCell(stage, adjacentRow, adjacentColumn)) continue;
        wires->entries[wires->count++] = (RpgWire){
            .path = RpgGridPath_Create((RpgWireCell){ row, column },
                                       (RpgWireCell){ adjacentRow, adjacentColumn })
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
        .path = { .cellCount = 1, .cells = { cell } },
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
        bool isBroken = false;
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

static void RpgWires_DrawWithOffset(const RpgWires *wires, const RpgStage *stage,
                                    int firstColumn, int columnCount)
{
    int lastColumn = firstColumn + columnCount;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
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
