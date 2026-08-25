// 依存する自プロジェクト内ファイル: rpg_data_shot.h
#include "rpg_data_shot.h"

#include "raymath.h"

#include <stddef.h>

static Vector2 RpgDataShots_GetCellCenter(RpgGridCell cell)
{
    return (Vector2){ cell.column * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                      cell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f };
}

RpgDataShots RpgDataShots_Default(void) { return (RpgDataShots){ .nextFolderSerial = 1 }; }

void RpgDataShot_SetFileProperties(RpgDataShot *shot, const RpgAttachment *attachment,
                                   int fileCount, unsigned long long totalBytes)
{
    if (shot == NULL || attachment == NULL) return;
    shot->fileCount = fileCount;
    shot->totalBytes = totalBytes;
    // 保存データが古くても、実弾の増分は必ず8px（1マスの1/4）刻みにそろえる。
    const float sizeStep = RPG_STAGE_TILE_SIZE * 0.25f;
    int stepCount = (int)(attachment->sizePerFile / sizeStep + 0.5f);
    if (stepCount < 1) stepCount = 1;
    if (stepCount > 8) stepCount = 8;
    shot->size = (fileCount - 1) * sizeStep * (float)stepCount;
    if (shot->size < 1.0f) shot->size = 1.0f;
    // 100B時の基準速度を容量の100B単位数で割る。容量が大きいほど必ず遅くなる。
    float capacityUnits = (float)totalBytes / RPG_DATA_SPEED_BASE_BYTES;
    if (capacityUnits < 1.0f) capacityUnits = 1.0f;
    shot->speed = attachment->dataSpeed / capacityUnits;
    if (shot->speed < 1.0f) shot->speed = 1.0f;
    if (shot->speed > 480.0f) shot->speed = 480.0f;
}

static bool RpgDataShots_FindWallImpactAlongSegment(const RpgStage *stage, Vector2 start, Vector2 end,
                                                    float radius, Vector2 *impactPosition)
{
    // 高速な弾でも壁を飛び越えないよう、移動線分を小刻みに半径込みで調べる。
    Vector2 movement = Vector2Subtract(end, start);
    int steps = (int)ceilf(Vector2Length(movement) / 4.0f);
    if (steps < 1) steps = 1;
    for (int step = 1; step <= steps; step++) {
        float progress = (float)step / (float)steps;
        Vector2 position = Vector2Add(start, Vector2Scale(movement, progress));
        if (RpgStage_FindSolidCircleCollisionCenter(stage, position, radius, impactPosition)) return true;
        if (position.x - radius < 0.0f || position.x + radius > RPG_STAGE_WORLD_WIDTH ||
            position.y - radius < 0.0f || position.y + radius > RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) {
            *impactPosition = position;
            return true;
        }
    }
    return false;
}

static int RpgDataShots_FindReceiverAlongSegment(const RpgReceivers *receivers, Vector2 start,
                                                 Vector2 end, float radius, Vector2 *impactPosition)
{
    if (receivers == NULL) return -1;
    Vector2 movement = Vector2Subtract(end, start);
    int steps = (int)ceilf(Vector2Length(movement) / 4.0f);
    if (steps < 1) steps = 1;
    for (int step = 1; step <= steps; step++) {
        Vector2 position = Vector2Add(start, Vector2Scale(movement, (float)step / (float)steps));
        int receiverIndex = RpgReceivers_FindAtPosition(receivers, position, radius + 10.0f);
        if (receiverIndex >= 0) { *impactPosition = position; return receiverIndex; }
    }
    return -1;
}

static int RpgDataShots_FindWireFromReceiver(const RpgWires *wires, RpgGridCell receiverCell,
                                             RpgGridSide receiverSide)
{
    if (wires == NULL) return -1;
    for (int wireIndex = 0; wireIndex < wires->count; wireIndex++) {
        const RpgWire *wire = &wires->entries[wireIndex];
        if (wire->hasReceiverSource && wire->receiverCell.row == receiverCell.row &&
            wire->receiverCell.column == receiverCell.column && wire->receiverSide == receiverSide)
            return wireIndex;
    }
    return -1;
}

static bool RpgDataShots_UpdateElectric(RpgDataShot *shot, const RpgWires *wires,
                                        float cellDelay, float deltaTime)
{
    if (wires == NULL || shot->electricWireIndex < 0 || shot->electricWireIndex >= wires->count)
        return false;
    const RpgWire *wire = &wires->entries[shot->electricWireIndex];
    if (wire->path.cellCount <= 0 || shot->electricCellIndex < 0 ||
        shot->electricCellIndex >= wire->path.cellCount) return false;
    if (cellDelay < 0.01f) cellDelay = 0.01f;
    shot->electricDelayElapsed += deltaTime;
    while (shot->electricDelayElapsed >= cellDelay) {
        shot->electricDelayElapsed -= cellDelay;
        shot->electricCellIndex++;
        if (shot->electricCellIndex >= wire->path.cellCount) return false;
    }
    shot->position = RpgDataShots_GetCellCenter(wire->path.cells[shot->electricCellIndex]);
    return true;
}

static void RpgDataShots_Spawn(RpgDataShots *shots, const RpgAttachments *attachments,
                               int attachmentIndex, bool isPreview)
{
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        if (shots->entries[index].active) continue;
        const RpgAttachment *attachment = &attachments->entries[attachmentIndex];
        shots->entries[index] = (RpgDataShot){ .active = true, .isPreview = isPreview,
            .folderSerial = shots->nextFolderSerial++, .attachmentIndex = attachmentIndex,
            .position = RpgDataShots_GetCellCenter(attachment->dataPath.cells[0]),
            .metadataCell = { -1, -1 } };
        RpgDataShot_SetFileProperties(&shots->entries[index], attachment,
            isPreview ? attachment->previewFileCount : 0,
            isPreview ? attachment->previewTotalBytes : 0);
        if (shots->nextFolderSerial <= 0) shots->nextFolderSerial = 1;
        return;
    }
}

void RpgDataShots_Trigger(RpgDataShots *shots, const RpgAttachments *attachments, int attachmentIndex)
{
    if (attachmentIndex >= 0 && attachmentIndex < attachments->count &&
        !attachments->entries[attachmentIndex].isZipperHeld &&
        attachments->entries[attachmentIndex].type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER)
        RpgDataShots_Spawn(shots, attachments, attachmentIndex, false);
}

void RpgDataShots_TriggerAll(RpgDataShots *shots, const RpgAttachments *attachments)
{
    for (int index = 0; index < attachments->count; index++)
        RpgDataShots_Trigger(shots, attachments, index);
}

void RpgDataShots_ConsumeButtonEvent(RpgDataShots *shots, const RpgAttachments *attachments,
                                     const RpgButtonEvent *buttonEvent)
{
    if (RpgButtonEvent_Consume(buttonEvent, &shots->lastButtonEventSequence))
        RpgDataShots_TriggerAll(shots, attachments);
}

void RpgDataShots_ConsumePreviewEvent(RpgDataShots *shots, const RpgAttachments *attachments,
                                      const RpgPreviewEvent *previewEvent)
{
    if (shots == NULL || attachments == NULL ||
        !RpgPreviewEvent_Consume(previewEvent, &shots->lastPreviewEventSequence)) return;
    RpgDataShots_TriggerPreview(shots, attachments, previewEvent->target);
}

void RpgDataShots_TriggerPreview(RpgDataShots *shots, const RpgAttachments *attachments, int target)
{
    if (shots == NULL || attachments == NULL) return;
    // 共通プレビュー通知では全装置、個別通知では指定装置だけを見た目専用で反応させる。
    for (int index = 0; index < attachments->count; index++)
        if (!attachments->entries[index].isZipperHeld && (target == -1 || target == index) &&
            attachments->entries[index].type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER)
            RpgDataShots_Spawn(shots, attachments, index, true);
}

void RpgDataShots_Update(RpgDataShots *shots, const RpgAttachments *attachments,
                         RpgStage *stage, const RpgReceivers *receivers,
                         const RpgWires *wires, float electricCellDelay,
                         float deltaTime, bool previewsOnly)
{
    (void)previewsOnly;
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        RpgDataShot *shot = &shots->entries[index];
        if (!shot->active || shot->isZipperHeld || shot->attachmentIndex >= attachments->count) continue;
        if (shot->isElectric) {
            // 導線の進行は受容体ではなく、電気化したデータ弾自身が担当する。
            if (!RpgDataShots_UpdateElectric(shot, wires, electricCellDelay, deltaTime)) shot->active = false;
            // プレビュー弾は更新呼び出し側の用途に関わらず、ギミックへ影響しない。
            else if (!shot->isPreview && !previewsOnly) {
                int row = (int)(shot->position.y / RPG_STAGE_TILE_SIZE);
                int column = (int)(shot->position.x / RPG_STAGE_TILE_SIZE);
                // 導線上の電気化データ弾がドアのマスへ入った瞬間、ドア全体を開状態にする。
                RpgStage_SetDoorOpenAtCell(stage, row, column, true);
            }
            continue;
        }
        const RpgAttachment *attachment = &attachments->entries[shot->attachmentIndex];
        const RpgGridPath *path = &attachment->dataPath;
        Vector2 target;
        if (shot->pathCellIndex + 1 < path->cellCount)
            target = RpgDataShots_GetCellCenter(path->cells[shot->pathCellIndex + 1]);
        else {
            Vector2 direction = { 0.0f, -1.0f };
            if (attachment->side == RPG_GRID_SIDE_RIGHT) direction.x = 1.0f, direction.y = 0.0f;
            else if (attachment->side == RPG_GRID_SIDE_BOTTOM) direction.y = 1.0f;
            else if (attachment->side == RPG_GRID_SIDE_LEFT) direction.x = -1.0f, direction.y = 0.0f;
            target = Vector2Add(shot->position, Vector2Scale(direction, shot->speed * deltaTime));
        }
        Vector2 previousPosition = shot->position;
        Vector2 step = Vector2Subtract(target, shot->position);
        float distance = Vector2Length(step);
        float move = shot->speed * deltaTime;
        if (shot->pathCellIndex + 1 < path->cellCount && distance <= move) {
            shot->position = target;
            shot->pathCellIndex++;
        } else if (distance > 0.0f) shot->position = Vector2Add(shot->position, Vector2Scale(step, move / distance));
        Vector2 impactPosition;
        int receiverIndex = RpgDataShots_FindReceiverAlongSegment(receivers, previousPosition, shot->position,
                                                                   shot->size, &impactPosition);
        if (receiverIndex >= 0) {
            // 受容体へ届いた弾はそこで消費し、導線だけへ電気の進行状態を渡す。
            shot->position = impactPosition;
            shot->impactPosition = impactPosition;
            // 受容体への到達は弾本体にとって壁衝突。電気表現だけを残してフォルダ寿命を終える。
            shot->hitWall = true;
            shot->folderSerial = 0;
            shot->electricWireIndex = RpgDataShots_FindWireFromReceiver(wires,
                receivers->entries[receiverIndex].cell, receivers->entries[receiverIndex].side);
            if (shot->electricWireIndex < 0) shot->active = false;
            else {
                shot->isElectric = true;
                shot->electricCellIndex = 0;
                shot->electricDelayElapsed = 0.0f;
                shot->position = RpgDataShots_GetCellCenter(
                    wires->entries[shot->electricWireIndex].path.cells[0]);
            }
        } else if (RpgDataShots_FindWallImpactAlongSegment(stage, previousPosition, shot->position, shot->size,
                                                    &impactPosition)) {
            shot->impactPosition = impactPosition;
            shot->hitWall = true;
            shot->active = false;
        } else if (shot->pathCellIndex + 1 >= path->cellCount && path->cellCount > 1 && distance <= move)
            shot->active = false;
    }
}

void RpgDataShots_Draw(const RpgDataShots *shots)
{
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        const RpgDataShot *shot = &shots->entries[index];
        if (!shot->active || shot->isZipperHeld) continue;
        Vector2 drawPosition = RpgStage_SnapRenderPoint(shot->position);
        if (shot->isElectric) {
            DrawCircleV(drawPosition, 12.0f, Fade(YELLOW, 0.20f));
            DrawCircleV(drawPosition, 5.0f, GOLD);
            DrawCircleLines((int)drawPosition.x, (int)drawPosition.y, 5.0f, RAYWHITE);
            continue;
        }
        DrawCircleV(drawPosition, shot->size, Fade(SKYBLUE, 0.3f));
        DrawCircleV(drawPosition, shot->size * 0.62f, YELLOW);
        DrawCircleLines((int)drawPosition.x, (int)drawPosition.y, shot->size, RAYWHITE);
        // 数字だけを上に表示し、フォルダのファイル数と合計容量をその場で確認できるようにする。
        // 表記はファイル数とバイト数に統一する。100Bなら100と表示する。
        DrawText(TextFormat("%d %llu", shot->fileCount, shot->totalBytes),
                 (int)(drawPosition.x - 20.0f), (int)(drawPosition.y - shot->size - 18.0f),
                 12, RAYWHITE);
    }
}

void RpgDataShots_DrawMap(const RpgDataShots *shots, int mapIndex)
{
    float mapStartX = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    float mapEndX = mapStartX + RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        const RpgDataShot *shot = &shots->entries[index];
        if (!shot->active || shot->isZipperHeld || shot->position.x < mapStartX || shot->position.x >= mapEndX) continue;
        Vector2 localPosition = RpgStage_SnapRenderPoint(
            (Vector2){ shot->position.x - mapStartX, shot->position.y });
        if (shot->isElectric) {
            DrawCircleV(localPosition, 12.0f, Fade(YELLOW, 0.20f));
            DrawCircleV(localPosition, 5.0f, GOLD);
            DrawCircleLines((int)localPosition.x, (int)localPosition.y, 5.0f, RAYWHITE);
            continue;
        }
        DrawCircleV(localPosition, shot->size, Fade(SKYBLUE, 0.3f));
        DrawCircleV(localPosition, shot->size * 0.62f, YELLOW);
        DrawCircleLines((int)localPosition.x, (int)localPosition.y, shot->size, RAYWHITE);
        // エディターのローカル描画もゲーム本体と同じバイト表記を使う。
        DrawText(TextFormat("%d %llu", shot->fileCount, shot->totalBytes),
                 (int)(localPosition.x - 20.0f), (int)(localPosition.y - shot->size - 18.0f),
                 12, RAYWHITE);
    }
}
// 役割: 電波発生装置から射出されるデータ弾の生成、移動、衝突、描画を管理する。
