// 依存する自プロジェクト内ファイル: rpg_receiver.h, rpg_grid_path.h, rpg_stage.h
#include "rpg_receiver.h"

#include "rpg_gimic_sprites.h"

#include <stdio.h>

static bool RpgReceivers_IsCellInStage(RpgGridCell cell)
{
    return cell.row >= 0 && cell.row < RPG_STAGE_ROWS && cell.column >= 0 &&
           cell.column < RPG_STAGE_WORLD_COLUMNS;
}

static Vector2 RpgReceivers_GetAnchor(const RpgReceiver *receiver, int firstColumn)
{
    float x = (receiver->cell.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    float y = receiver->cell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    if (receiver->side == RPG_GRID_SIDE_TOP) y -= RPG_STAGE_TILE_SIZE * 0.5f;
    else if (receiver->side == RPG_GRID_SIDE_RIGHT) x += RPG_STAGE_TILE_SIZE * 0.5f;
    else if (receiver->side == RPG_GRID_SIDE_BOTTOM) y += RPG_STAGE_TILE_SIZE * 0.5f;
    else x -= RPG_STAGE_TILE_SIZE * 0.5f;
    return (Vector2){ x, y };
}

static float RpgReceivers_GetRotation(RpgGridSide side)
{
    /* Attachment art uses down as its zero-degree orientation. */
    if (side == RPG_GRID_SIDE_RIGHT) return 270.0f;
    if (side == RPG_GRID_SIDE_BOTTOM) return 0.0f;
    if (side == RPG_GRID_SIDE_LEFT) return 90.0f;
    return 180.0f;
}

int RpgReceivers_FindAtCell(const RpgReceivers *receivers, RpgGridCell cell)
{
    for (int index = receivers->count - 1; index >= 0; index--) {
        const RpgReceiver *receiver = &receivers->entries[index];
        if (receiver->cell.row == cell.row && receiver->cell.column == cell.column) return index;
    }
    return -1;
}

RpgReceivers RpgReceivers_Default(void) { return (RpgReceivers){ 0 }; }

bool RpgReceivers_Load(const char *filePath, RpgReceivers *receivers)
{
    FILE *file = fopen(filePath, "r");
    RpgReceivers loaded = RpgReceivers_Default();
    if (file == NULL) return false;
    int storedCount = 0;
    if (fscanf(file, "%d", &storedCount) != 1 || storedCount < 0 ||
        storedCount > RPG_RECEIVER_MAX_COUNT * 4) {
        fclose(file);
        return false;
    }
    for (int index = 0; index < storedCount; index++) {
        RpgReceiver receiver = { 0 };
        int side = 0;
        if (fscanf(file, "%d %d %d", &receiver.cell.row, &receiver.cell.column, &side) != 3 ||
            !RpgReceivers_IsCellInStage(receiver.cell) || side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) {
            fclose(file);
            return false;
        }
        receiver.side = (RpgGridSide)side;
        // 旧データに同一ブロックの受容体が複数あれば、先にある一つだけを残す。
        if (RpgReceivers_FindAtCell(&loaded, receiver.cell) < 0)
            loaded.entries[loaded.count++] = receiver;
    }
    if (fclose(file) != 0) return false;
    *receivers = loaded;
    return true;
}

bool RpgReceivers_Save(const char *filePath, const RpgReceivers *receivers)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "%d\n", receivers->count);
    for (int index = 0; index < receivers->count; index++) {
        const RpgReceiver *receiver = &receivers->entries[index];
        fprintf(file, "%d %d %d\n", receiver->cell.row, receiver->cell.column, receiver->side);
    }
    return fclose(file) == 0;
}

bool RpgReceivers_Add(RpgReceivers *receivers, const RpgStage *stage, RpgGridCell cell)
{
    if (receivers->count >= RPG_RECEIVER_MAX_COUNT || !RpgReceivers_IsCellInStage(cell) ||
        stage->blocks[cell.row][cell.column] == 0 || RpgReceivers_FindAtCell(receivers, cell) >= 0)
        return false;
    receivers->entries[receivers->count++] = (RpgReceiver){ cell, RPG_GRID_SIDE_TOP };
    return true;
}

int RpgReceivers_FindAtPosition(const RpgReceivers *receivers, Vector2 position, float distance)
{
    for (int index = receivers->count - 1; index >= 0; index--) {
        Vector2 anchor = RpgReceivers_GetAnchor(&receivers->entries[index], 0);
        if (position.x >= anchor.x - distance && position.x <= anchor.x + distance &&
            position.y >= anchor.y - distance && position.y <= anchor.y + distance) return index;
    }
    return -1;
}

bool RpgReceivers_CycleSide(RpgReceivers *receivers, int receiverIndex)
{
    if (receiverIndex < 0 || receiverIndex >= receivers->count) return false;
    RpgReceiver *receiver = &receivers->entries[receiverIndex];
    receiver->side = (RpgGridSide)((receiver->side + 1) % 4);
    return true;
}

void RpgReceivers_RemoveBroken(RpgReceivers *receivers, const RpgStage *stage)
{
    for (int index = 0; index < receivers->count;) {
        RpgGridCell cell = receivers->entries[index].cell;
        if (RpgReceivers_IsCellInStage(cell) && stage->blocks[cell.row][cell.column] != 0) {
            index++;
            continue;
        }
        for (int next = index; next < receivers->count - 1; next++)
            receivers->entries[next] = receivers->entries[next + 1];
        receivers->count--;
    }
}

static void RpgReceivers_DrawWithOffset(const RpgReceivers *receivers, int firstColumn, int columnCount)
{
    int lastColumn = firstColumn + columnCount;
    for (int index = 0; index < receivers->count; index++) {
        const RpgReceiver *receiver = &receivers->entries[index];
        if (receiver->cell.column < firstColumn || receiver->cell.column >= lastColumn) continue;
        Vector2 anchor = RpgStage_SnapRenderPoint(RpgReceivers_GetAnchor(receiver, firstColumn));
        Rectangle recess = RpgStage_SnapRenderRectangle(
            (Rectangle){ anchor.x - 8.0f, anchor.y - 8.0f, 16.0f, 16.0f });
        if (RpgGimicSprites_DrawRotated(RPG_GIMIC_SPRITE_DATA_RECEIVER, recess,
                                        RpgReceivers_GetRotation(receiver->side), WHITE))
            continue;
        // ブロックの縁を掘り込んだように見せるため、外枠より暗い内側を描く。
        DrawRectangleRec(recess, DARKBROWN);
        DrawRectangleLinesEx(recess, 2.0f, GOLD);
        DrawRectangle((int)recess.x + 3, (int)recess.y + 2,
                      (int)recess.width - 6, (int)recess.height - 4, Fade(BLACK, 0.72f));
    }
}

void RpgReceivers_Draw(const RpgReceivers *receivers)
{
    RpgReceivers_DrawWithOffset(receivers, 0, RPG_STAGE_WORLD_COLUMNS);
}

void RpgReceivers_DrawMap(const RpgReceivers *receivers, int mapIndex)
{
    RpgReceivers_DrawWithOffset(receivers, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
}
// 役割: ブロック辺に置く受容体と、その配置・保存・描画を管理する。
