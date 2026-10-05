// Receiver compatibility facade. Receiver records are RpgAttachment entries.
#include "rpg_receiver.h"

#include "rpg_gimic_sprites.h"
#include "rpg_object_folder.h"

#include <string.h>
#include <stdio.h>

static bool IsCellInStage(RpgGridCell cell)
{
    return cell.row >= 0 && cell.row < RPG_STAGE_ROWS && cell.column >= 0 &&
           cell.column < RPG_STAGE_WORLD_COLUMNS;
}

static int AttachmentIndexAtReceiverIndex(const RpgReceivers *receivers, int receiverIndex)
{
    int found = 0;
    if (receivers == NULL || receivers->attachments == NULL || receiverIndex < 0) return -1;
    for (int index = 0; index < receivers->attachments->count; index++) {
        if (!RpgAttachments_IsReceiver(&receivers->attachments->entries[index])) continue;
        if (found++ == receiverIndex) return index;
    }
    return -1;
}

static Vector2 GetAnchor(const RpgReceiver *receiver, int firstColumn)
{
    float x = (receiver->cell.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    float y = receiver->cell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    if (receiver->side == RPG_GRID_SIDE_TOP) y -= RPG_STAGE_TILE_SIZE * 0.5f;
    else if (receiver->side == RPG_GRID_SIDE_RIGHT) x += RPG_STAGE_TILE_SIZE * 0.5f;
    else if (receiver->side == RPG_GRID_SIDE_BOTTOM) y += RPG_STAGE_TILE_SIZE * 0.5f;
    else x -= RPG_STAGE_TILE_SIZE * 0.5f;
    return (Vector2){ x, y };
}

static float GetRotation(RpgGridSide side)
{
    if (side == RPG_GRID_SIDE_RIGHT) return 270.0f;
    if (side == RPG_GRID_SIDE_BOTTOM) return 0.0f;
    if (side == RPG_GRID_SIDE_LEFT) return 90.0f;
    return 180.0f;
}

RpgReceivers RpgReceivers_Default(void) { return (RpgReceivers){ 0 }; }

void RpgReceivers_Bind(RpgReceivers *receivers, RpgAttachments *attachments)
{
    if (receivers != NULL) receivers->attachments = attachments;
}

int RpgReceivers_Count(const RpgReceivers *receivers)
{
    int count = 0;
    if (receivers == NULL || receivers->attachments == NULL) return 0;
    for (int index = 0; index < receivers->attachments->count; index++)
        if (RpgAttachments_IsReceiver(&receivers->attachments->entries[index])) count++;
    return count;
}

bool RpgReceivers_Get(const RpgReceivers *receivers, int receiverIndex, RpgReceiver *receiver)
{
    int attachmentIndex = AttachmentIndexAtReceiverIndex(receivers, receiverIndex);
    const RpgAttachment *attachment;
    if (receiver == NULL || attachmentIndex < 0) return false;
    attachment = &receivers->attachments->entries[attachmentIndex];
    *receiver = (RpgReceiver){ .cell = attachment->cell, .side = attachment->side,
                               .isOwnerBlockZipperHeld = attachment->isOwnerBlockZipperHeld };
    return true;
}

bool RpgReceivers_Set(RpgReceivers *receivers, int receiverIndex, RpgReceiver receiver)
{
    int attachmentIndex = AttachmentIndexAtReceiverIndex(receivers, receiverIndex);
    RpgAttachment *attachment;
    if (attachmentIndex < 0 || !IsCellInStage(receiver.cell) ||
        receiver.side < RPG_GRID_SIDE_TOP || receiver.side > RPG_GRID_SIDE_LEFT) return false;
    attachment = &receivers->attachments->entries[attachmentIndex];
    attachment->cell = receiver.cell;
    attachment->side = receiver.side;
    attachment->isOwnerBlockZipperHeld = receiver.isOwnerBlockZipperHeld;
    return true;
}

int RpgReceivers_FindAtCell(const RpgReceivers *receivers, RpgGridCell cell)
{
    for (int index = RpgReceivers_Count(receivers) - 1; index >= 0; index--) {
        RpgReceiver receiver;
        if (RpgReceivers_Get(receivers, index, &receiver) && !RpgReceivers_IsRuntimeUnavailable(&receiver) &&
            receiver.cell.row == cell.row && receiver.cell.column == cell.column) return index;
    }
    return -1;
}

bool RpgReceivers_Add(RpgReceivers *receivers, const RpgStage *stage, RpgGridCell cell)
{
    RpgAttachment attachment;
    if (receivers == NULL || receivers->attachments == NULL || stage == NULL || !IsCellInStage(cell) ||
        stage->blocks[cell.row][cell.column] == 0 || RpgReceivers_FindAtCell(receivers, cell) >= 0) return false;
    attachment = (RpgAttachment){ .type = RPG_ATTACHMENT_TYPE_RECEIVER, .folderId = 1,
        .cell = cell, .side = RPG_GRID_SIDE_TOP };
    for (int index = 0; index < receivers->attachments->count; index++)
        if (receivers->attachments->entries[index].folderId >= attachment.folderId)
            attachment.folderId = receivers->attachments->entries[index].folderId + 1;
    return RpgAttachments_Append(receivers->attachments, &attachment);
}

bool RpgReceivers_LoadLegacy(const char *filePath, RpgReceivers *receivers)
{
    FILE *file;
    int count;
    if (filePath == NULL || receivers == NULL || receivers->attachments == NULL ||
        (file = fopen(filePath, "r")) == NULL) return false;
    if (fscanf(file, "%d", &count) != 1 || count < 0) { fclose(file); return false; }
    for (int index = 0; index < count; index++) {
        int row, column, side;
        RpgAttachment attachment;
        if (fscanf(file, "%d %d %d", &row, &column, &side) != 3 ||
            !IsCellInStage((RpgGridCell){ row, column }) ||
            side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) { fclose(file); return false; }
        attachment = (RpgAttachment){ .type = RPG_ATTACHMENT_TYPE_RECEIVER, .folderId = 1,
            .cell = { row, column }, .side = (RpgGridSide)side };
        for (int existing = 0; existing < receivers->attachments->count; existing++)
            if (receivers->attachments->entries[existing].folderId >= attachment.folderId)
                attachment.folderId = receivers->attachments->entries[existing].folderId + 1;
        if (!RpgAttachments_Append(receivers->attachments, &attachment)) { fclose(file); return false; }
    }
    fclose(file);
    return true;
}

int RpgReceivers_FindAtPosition(const RpgReceivers *receivers, Vector2 position, float distance)
{
    for (int index = RpgReceivers_Count(receivers) - 1; index >= 0; index--) {
        RpgReceiver receiver;
        if (!RpgReceivers_Get(receivers, index, &receiver) || RpgReceivers_IsRuntimeUnavailable(&receiver)) continue;
        Vector2 anchor = GetAnchor(&receiver, 0);
        if (position.x >= anchor.x - distance && position.x <= anchor.x + distance &&
            position.y >= anchor.y - distance && position.y <= anchor.y + distance) return index;
    }
    return -1;
}

bool RpgReceivers_IsRuntimeUnavailable(const RpgReceiver *receiver)
{ return receiver == NULL || receiver->isOwnerBlockZipperHeld; }

void RpgReceivers_SetOwnerBlockZipperHeld(RpgReceivers *receivers, RpgGridCell blockCell, bool isHeld)
{
    if (receivers == NULL || receivers->attachments == NULL) return;
    for (int index = 0; index < receivers->attachments->count; index++) {
        RpgAttachment *attachment = &receivers->attachments->entries[index];
        if (RpgAttachments_IsReceiver(attachment) &&
            RpgObjectFolders_HaveSameBlockOwner(attachment->cell, blockCell))
            attachment->isOwnerBlockZipperHeld = isHeld;
    }
}

bool RpgReceivers_CycleSide(RpgReceivers *receivers, int receiverIndex)
{
    RpgReceiver receiver;
    if (!RpgReceivers_Get(receivers, receiverIndex, &receiver)) return false;
    receiver.side = (RpgGridSide)((receiver.side + 1) % 4);
    return RpgReceivers_Set(receivers, receiverIndex, receiver);
}

bool RpgReceivers_Remove(RpgReceivers *receivers, int receiverIndex)
{
    int attachmentIndex = AttachmentIndexAtReceiverIndex(receivers, receiverIndex);
    if (attachmentIndex < 0) return false;
    memmove(&receivers->attachments->entries[attachmentIndex],
            &receivers->attachments->entries[attachmentIndex + 1],
            (size_t)(receivers->attachments->count - attachmentIndex - 1) *
            sizeof(receivers->attachments->entries[0]));
    receivers->attachments->count--;
    return true;
}

void RpgReceivers_RemoveBroken(RpgReceivers *receivers, const RpgStage *stage)
{
    if (receivers == NULL || receivers->attachments == NULL || stage == NULL) return;
    for (int index = RpgReceivers_Count(receivers) - 1; index >= 0; index--) {
        RpgReceiver receiver;
        if (RpgReceivers_Get(receivers, index, &receiver) && IsCellInStage(receiver.cell) &&
            stage->blocks[receiver.cell.row][receiver.cell.column] != 0) continue;
        (void)RpgReceivers_Remove(receivers, index);
    }
}

static void DrawWithOffset(const RpgReceivers *receivers, int firstColumn, int columnCount)
{
    int lastColumn = firstColumn + columnCount;
    for (int index = 0; index < RpgReceivers_Count(receivers); index++) {
        RpgReceiver receiver;
        if (!RpgReceivers_Get(receivers, index, &receiver) || RpgReceivers_IsRuntimeUnavailable(&receiver) ||
            receiver.cell.column < firstColumn || receiver.cell.column >= lastColumn) continue;
        Vector2 anchor = RpgStage_SnapRenderPoint(GetAnchor(&receiver, firstColumn));
        Rectangle recess = RpgStage_SnapRenderRectangle((Rectangle){ anchor.x - 8.0f, anchor.y - 8.0f, 16.0f, 16.0f });
        if (RpgGimicSprites_DrawRotated(RPG_GIMIC_SPRITE_DATA_RECEIVER, recess, GetRotation(receiver.side), WHITE)) continue;
        DrawRectangleRec(recess, DARKBROWN);
        DrawRectangleLinesEx(recess, 2.0f, GOLD);
        DrawRectangle((int)recess.x + 3, (int)recess.y + 2, (int)recess.width - 6, (int)recess.height - 4, Fade(BLACK, 0.72f));
    }
}

void RpgReceivers_Draw(const RpgReceivers *receivers)
{ DrawWithOffset(receivers, 0, RPG_STAGE_WORLD_COLUMNS); }

void RpgReceivers_DrawMap(const RpgReceivers *receivers, int mapIndex)
{ DrawWithOffset(receivers, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS); }
