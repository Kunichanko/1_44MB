// 依存する自プロジェクト内ファイル: rpg_attachment.h
#include "rpg_attachment.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* データ弾のファイルごとの増分は、32pxマスの1/4（8px）単位だけを許可する。 */
static float RpgAttachments_NormalizeShotSizePerFile(float sizePerFile)
{
    const float sizeStep = RPG_STAGE_TILE_SIZE * 0.25f;
    int stepCount = (int)roundf(sizePerFile / sizeStep);
    if (stepCount < 1) stepCount = 1;
    if (stepCount > 8) stepCount = 8;
    return sizeStep * (float)stepCount;
}

static bool RpgAttachments_IsCellInStage(RpgGridCell cell)
{
    return cell.row >= 0 && cell.row < RPG_STAGE_ROWS && cell.column >= 0 &&
           cell.column < RPG_STAGE_WORLD_COLUMNS;
}

static bool RpgAttachments_AreSame(const RpgAttachment *first, const RpgAttachment *second)
{
    return first->type == second->type && first->cell.row == second->cell.row &&
           first->cell.column == second->cell.column && first->side == second->side;
}

// 取付先の辺の外側に、装置を収めるための空きマスがあるか確認する。
static bool RpgAttachments_HasOuterEmptyCell(const RpgStage *stage, RpgGridCell cell,
                                             RpgGridSide side)
{
    RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(cell, side);
    return RpgAttachments_IsCellInStage(outerCell) && stage->blocks[outerCell.row][outerCell.column] == 0;
}

bool RpgAttachments_GetOccupiedCell(const RpgAttachment *attachment, RpgGridCell *cell)
{
    RpgGridCell occupiedCell;
    if (attachment == NULL || cell == NULL || !RpgBlockInventory_IsCellAttachment(attachment->type)) return false;
    occupiedCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
    if (!RpgAttachments_IsCellInStage(occupiedCell)) return false;
    *cell = occupiedCell;
    return true;
}

bool RpgAttachments_IsCellOccupied(const RpgAttachments *attachments, RpgGridCell cell)
{
    if (attachments == NULL) return false;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell occupiedCell;
        if (!RpgAttachments_GetOccupiedCell(attachment, &occupiedCell)) continue;
        if (occupiedCell.row == cell.row && occupiedCell.column == cell.column) return true;
    }
    return false;
}

RpgAttachments RpgAttachments_Default(void) { return (RpgAttachments){ 0 }; }

// 保存済みの添付物と重複しないフォルダ識別子を返す。
static int RpgAttachments_GetNextFolderId(const RpgAttachments *attachments)
{
    int nextFolderId = 1;
    for (int index = 0; index < attachments->count; index++)
        if (attachments->entries[index].folderId >= nextFolderId)
            nextFolderId = attachments->entries[index].folderId + 1;
    return nextFolderId;
}

// 旧保存形式の識別子欠落・重複を、読み込み時に安全な値へ補正する。
static int RpgAttachments_RepairFolderId(const RpgAttachments *attachments, int folderId)
{
    if (folderId <= 0) return RpgAttachments_GetNextFolderId(attachments);
    for (int index = 0; index < attachments->count; index++)
        if (attachments->entries[index].folderId == folderId)
            return RpgAttachments_GetNextFolderId(attachments);
    return folderId;
}

bool RpgAttachments_Load(const char *filePath, RpgAttachments *attachments)
{
    FILE *file = fopen(filePath, "r");
    RpgAttachments loaded = RpgAttachments_Default();
    if (file == NULL) return false;
    char format[16];
    bool currentFormat = false;
    bool previewFormat = false;
    if (fscanf(file, "%15s", format) != 1) { fclose(file); return false; }
    currentFormat = strcmp(format, "v2") == 0 || strcmp(format, "v3") == 0 || strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 || strcmp(format, "v6") == 0;
    previewFormat = strcmp(format, "v3") == 0 || strcmp(format, "v4") == 0;
    if ((currentFormat ? fscanf(file, "%d", &loaded.count) : sscanf(format, "%d", &loaded.count)) != 1 || loaded.count < 0 ||
        loaded.count > RPG_ATTACHMENT_MAX_COUNT) {
        fclose(file);
        return false;
    }
    for (int index = 0; index < loaded.count; index++) {
        RpgAttachment *attachment = &loaded.entries[index];
        int side = 0;
        int folderId = index + 1;
        bool folderIdFormat = strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 || strcmp(format, "v6") == 0;
        int fieldCount = folderIdFormat ?
            fscanf(file, "%d %d %d %d %d", &attachment->type, &folderId, &attachment->cell.row,
                   &attachment->cell.column, &side) :
            fscanf(file, "%d %d %d %d", &attachment->type, &attachment->cell.row,
                   &attachment->cell.column, &side);
        if (fieldCount != (folderIdFormat ? 5 : 4) ||
            !RpgBlockInventory_IsAttachment(attachment->type) ||
            !RpgAttachments_IsCellInStage(attachment->cell) ||
            side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) {
            fclose(file);
            return false;
        }
        attachment->folderId = RpgAttachments_RepairFolderId(&loaded, folderId);
        attachment->side = (RpgGridSide)side;
        RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
        attachment->dataSize = 8.0f;
        attachment->dataSpeed = 120.0f;
        attachment->dataInterval = 1.0f;
        attachment->dataPreviewEnabled = false;
        attachment->sizePerFile = RPG_STAGE_TILE_SIZE * 0.25f;
        attachment->speedPerKilobyte = 1.0f / 64.0f;
        attachment->previewFileCount = 2;
        attachment->previewTotalBytes = 0;
        attachment->dataPath = (RpgGridPath){ .cellCount = 1, .cells = { outerCell } };
        if (currentFormat) {
            int previewEnabled = 0;
            float ignoredLegacyPreviewSize = 0.0f;
            float ignoredLegacyPreviewSpeed = 0.0f;
            bool currentSettingsFormat = strcmp(format, "v6") == 0;
            bool legacyPreviewSettingsFormat = strcmp(format, "v5") == 0;
            int readCount = currentSettingsFormat ?
                fscanf(file, "%f %f %f %d %f %f %d %llu %d", &attachment->dataSize, &attachment->dataSpeed,
                       &attachment->dataInterval, &previewEnabled, &attachment->sizePerFile,
                       &attachment->speedPerKilobyte, &attachment->previewFileCount,
                       &attachment->previewTotalBytes, &attachment->dataPath.cellCount) : legacyPreviewSettingsFormat ?
                fscanf(file, "%f %f %f %d %f %f %d", &attachment->dataSize, &attachment->dataSpeed,
                       &attachment->dataInterval, &previewEnabled, &ignoredLegacyPreviewSize,
                       &ignoredLegacyPreviewSpeed, &attachment->dataPath.cellCount) : previewFormat ?
                fscanf(file, "%f %f %f %d %d", &attachment->dataSize, &attachment->dataSpeed,
                       &attachment->dataInterval, &previewEnabled, &attachment->dataPath.cellCount) :
                fscanf(file, "%f %f %f %d", &attachment->dataSize, &attachment->dataSpeed,
                       &attachment->dataInterval, &attachment->dataPath.cellCount);
            if (readCount != (currentSettingsFormat ? 9 : legacyPreviewSettingsFormat ? 7 : previewFormat ? 5 : 4) ||
                attachment->dataSize < 2.0f || attachment->dataSize > 24.0f ||
                attachment->dataSpeed < 20.0f || attachment->dataSpeed > 480.0f ||
                attachment->dataInterval < 0.1f || attachment->dataInterval > 10.0f ||
                attachment->sizePerFile < 1.0f || attachment->sizePerFile > 64.0f ||
                attachment->speedPerKilobyte < 0.0001f || attachment->speedPerKilobyte > 1.0f ||
                attachment->previewFileCount < 1 || attachment->previewFileCount > 9999 ||
                attachment->dataPath.cellCount < 1 || attachment->dataPath.cellCount > RPG_GRID_PATH_MAX_CELLS) {
                fclose(file); return false;
            }
            attachment->sizePerFile = RpgAttachments_NormalizeShotSizePerFile(attachment->sizePerFile);
            attachment->dataPreviewEnabled = previewEnabled != 0;
            for (int pathIndex = 0; pathIndex < attachment->dataPath.cellCount; pathIndex++)
                if (fscanf(file, "%d %d", &attachment->dataPath.cells[pathIndex].row,
                           &attachment->dataPath.cells[pathIndex].column) != 2 ||
                    !RpgAttachments_IsCellInStage(attachment->dataPath.cells[pathIndex])) {
                    fclose(file); return false;
                }
        }
    }
    if (fclose(file) != 0) return false;
    *attachments = loaded;
    return true;
}

bool RpgAttachments_Save(const char *filePath, const RpgAttachments *attachments)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "v6 %d\n", attachments->count);
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        fprintf(file, "%d %d %d %d %d %.2f %.2f %.2f %d %.2f %.6f %d %llu %d", attachment->type, attachment->folderId, attachment->cell.row,
                attachment->cell.column, attachment->side, attachment->dataSize,
                attachment->dataSpeed, attachment->dataInterval, attachment->dataPreviewEnabled ? 1 : 0,
                attachment->sizePerFile, attachment->speedPerKilobyte, attachment->previewFileCount,
                attachment->previewTotalBytes, attachment->dataPath.cellCount);
        for (int pathIndex = 0; pathIndex < attachment->dataPath.cellCount; pathIndex++)
            fprintf(file, " %d %d", attachment->dataPath.cells[pathIndex].row,
                    attachment->dataPath.cells[pathIndex].column);
        fputc('\n', file);
    }
    return fclose(file) == 0;
}

bool RpgAttachments_Add(RpgAttachments *attachments, const RpgStage *stage, int type,
                        RpgGridCell cell, RpgGridSide side)
{
    RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(cell, side);
    RpgAttachment attachment = { .type = type, .folderId = RpgAttachments_GetNextFolderId(attachments), .cell = cell, .side = side, .dataSize = 8.0f,
                                 .dataSpeed = 120.0f, .dataInterval = 1.0f,
                                 .sizePerFile = RPG_STAGE_TILE_SIZE * 0.25f, .speedPerKilobyte = 1.0f / 64.0f,
                                 .previewFileCount = 2, .previewTotalBytes = 0,
                                 .dataPath = { .cellCount = 1, .cells = { outerCell } } };
    if (attachments->count >= RPG_ATTACHMENT_MAX_COUNT || !RpgBlockInventory_IsAttachment(type) ||
        !RpgAttachments_IsCellInStage(cell) || stage->blocks[cell.row][cell.column] == 0 ||
        !RpgAttachments_HasOuterEmptyCell(stage, cell, side) ||
        side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) return false;
    if (RpgBlockInventory_IsCellAttachment(type) && RpgAttachments_IsCellOccupied(attachments, outerCell))
        return false;
    for (int index = 0; index < attachments->count; index++)
        if (RpgAttachments_AreSame(&attachments->entries[index], &attachment)) return false;
    attachments->entries[attachments->count++] = attachment;
    return true;
}

bool RpgAttachments_MoveDataPathEndpoint(RpgAttachments *attachments, const RpgStage *stage,
                                          int attachmentIndex, int row, int column)
{
    (void)stage;
    if (attachmentIndex < 0 || attachmentIndex >= attachments->count ||
        attachments->entries[attachmentIndex].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER ||
        row < 0 || row >= RPG_STAGE_ROWS ||
        column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    return RpgGridPath_MoveEndpoint(&attachments->entries[attachmentIndex].dataPath, false,
                                    (RpgGridCell){ row, column }, 1);
}

bool RpgAttachments_FindDataPathEndpoint(const RpgAttachments *attachments, int row, int column,
                                         int *attachmentIndex)
{
    for (int index = attachments->count - 1; index >= 0; index--) {
        if (attachments->entries[index].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) continue;
        const RpgGridPath *path = &attachments->entries[index].dataPath;
        RpgGridCell end = path->cells[path->cellCount - 1];
        if (end.row == row && end.column == column) { *attachmentIndex = index; return true; }
    }
    return false;
}

bool RpgAttachments_Remove(RpgAttachments *attachments, RpgAttachment attachment)
{
    for (int index = 0; index < attachments->count; index++) {
        if (!RpgAttachments_AreSame(&attachments->entries[index], &attachment)) continue;
        for (int next = index; next < attachments->count - 1; next++)
            attachments->entries[next] = attachments->entries[next + 1];
        attachments->count--;
        return true;
    }
    return false;
}

void RpgAttachments_MigrateLegacyButtons(RpgAttachments *attachments, RpgStage *stage)
{
    // 旧来の単独ボタンを、同じ位置の隣にあるブロックへ取り付ける形式へ変換する。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (stage->blocks[row][column] != RPG_BLOCK_EFFECT_BUTTON) continue;
        stage->blocks[row][column] = 0;
        const RpgGridCell bases[] = {
            { row + 1, column }, { row, column - 1 }, { row - 1, column }, { row, column + 1 }
        };
        const RpgGridSide sides[] = {
            RPG_GRID_SIDE_TOP, RPG_GRID_SIDE_RIGHT, RPG_GRID_SIDE_BOTTOM, RPG_GRID_SIDE_LEFT
        };
        for (int index = 0; index < 4; index++) {
            RpgGridCell base = bases[index];
            if (base.row < 0 || base.row >= RPG_STAGE_ROWS || base.column < 0 ||
                base.column >= RPG_STAGE_WORLD_COLUMNS || stage->blocks[base.row][base.column] == 0) continue;
            if (RpgAttachments_Add(attachments, stage, RPG_BLOCK_ATTACHMENT_DATA_BUTTON,
                                   base, sides[index])) break;
        }
    }
}

bool RpgAttachments_IsButtonPressed(const RpgAttachments *attachments, Vector2 playerPosition)
{
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (attachment->isZipperHeld || attachment->type != RPG_BLOCK_ATTACHMENT_DATA_BUTTON) continue;
        Vector2 position = RpgAttachments_GetPosition(attachment, 0);
        if (fabsf(playerPosition.x - position.x) <= 22.0f && fabsf(playerPosition.y - position.y) <= 24.0f)
            return true;
    }
    return false;
}

int RpgAttachments_FindTouchedSaveFlag(const RpgAttachments *attachments, Vector2 playerPosition)
{
    if (attachments == NULL) return -1;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (attachment->isZipperHeld || attachment->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG) continue;
        if (Vector2Distance(playerPosition, RpgAttachments_GetPosition(attachment, 0)) <= 28.0f) return index;
    }
    return -1;
}

bool RpgAttachments_SetRaisedSaveFlag(RpgAttachments *attachments, int flagId)
{
    bool found = false;
    if (attachments == NULL || flagId <= 0) return false;
    for (int index = 0; index < attachments->count; index++) {
        RpgAttachment *attachment = &attachments->entries[index];
        if (attachment->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG) continue;
        attachment->flagRaised = attachment->folderId == flagId;
        if (attachment->flagRaised) found = true;
    }
    return found;
}

Vector2 RpgAttachments_GetPosition(const RpgAttachment *attachment, int firstColumn)
{
    RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
    float x = (outerCell.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    float y = outerCell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    // 外側1マスのうち、土台だけを取付先ブロック側の辺へ寄せる。
    // 支持ブロックと空気マスの境界を唯一の取付基準にする。描画・当たり判定・ドラッグはすべてこの値を使う。
    float offset = RPG_STAGE_TILE_SIZE * 0.5f;
    if (attachment->side == RPG_GRID_SIDE_TOP) y += offset;
    else if (attachment->side == RPG_GRID_SIDE_RIGHT) x -= offset;
    else if (attachment->side == RPG_GRID_SIDE_BOTTOM) y -= offset;
    else x += offset;
    return (Vector2){ x, y };
}

Vector2 RpgAttachments_GetSaveFlagRespawnPosition(const RpgAttachment *attachment)
{
    if (attachment == NULL) return (Vector2){ 0.0f, 0.0f };
    /* キャラクター座標は足元基準なので、旗を支えるブロックの上辺へ置く。 */
    return (Vector2){ (attachment->cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                      attachment->cell.row * RPG_STAGE_TILE_SIZE };
}

int RpgAttachments_FindAtPosition(const RpgAttachments *attachments, Vector2 position, float distance)
{
    for (int index = attachments->count - 1; index >= 0; index--) {
        Vector2 attachmentPosition = RpgAttachments_GetPosition(&attachments->entries[index], 0);
        if (Vector2Distance(attachmentPosition, position) <= distance) return index;
    }
    return -1;
}

bool RpgAttachments_FindSnap(const RpgAttachments *attachments, const RpgStage *stage, int type,
                             Vector2 position, int ignoredAttachmentIndex, RpgAttachment *attachment)
{
    float nearestDistance = RPG_STAGE_TILE_SIZE;
    bool found = false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (stage->blocks[row][column] == 0) continue;
        for (int side = RPG_GRID_SIDE_TOP; side <= RPG_GRID_SIDE_LEFT; side++) {
            RpgAttachment candidate = { .type = type, .cell = { row, column },
                                        .side = (RpgGridSide)side };
            if (!RpgAttachments_HasOuterEmptyCell(stage, candidate.cell, candidate.side)) continue;
            RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(candidate.cell, candidate.side);
            if (RpgBlockInventory_IsCellAttachment(type)) {
                bool occupied = false;
                for (int attachmentIndex = 0; attachments != NULL && attachmentIndex < attachments->count; attachmentIndex++) {
                    const RpgAttachment *existing = &attachments->entries[attachmentIndex];
                    RpgGridCell occupiedCell;
                    if (attachmentIndex == ignoredAttachmentIndex ||
                        !RpgBlockInventory_IsCellAttachment(existing->type)) continue;
                    occupiedCell = RpgGridPath_GetSideNeighbor(existing->cell, existing->side);
                    if (occupiedCell.row == outerCell.row && occupiedCell.column == outerCell.column)
                        occupied = true;
                }
                if (occupied) continue;
            }
            float distance = Vector2Distance(position, RpgAttachments_GetPosition(&candidate, 0));
            if (distance > nearestDistance) continue;
            nearestDistance = distance;
            *attachment = candidate;
            found = true;
        }
    }
    return found;
}

void RpgAttachments_RemoveBroken(RpgAttachments *attachments, const RpgStage *stage)
{
    for (int index = 0; index < attachments->count;) {
        RpgGridCell cell = attachments->entries[index].cell;
        if (RpgAttachments_IsCellInStage(cell) && stage->blocks[cell.row][cell.column] != 0 &&
            RpgAttachments_HasOuterEmptyCell(stage, cell, attachments->entries[index].side)) {
            index++;
            continue;
        }
        for (int next = index; next < attachments->count - 1; next++)
            attachments->entries[next] = attachments->entries[next + 1];
        attachments->count--;
    }
}

static Vector2 RpgAttachments_GetOutwardDirection(RpgGridSide side)
{
    if (side == RPG_GRID_SIDE_RIGHT) return (Vector2){ 1.0f, 0.0f };
    if (side == RPG_GRID_SIDE_BOTTOM) return (Vector2){ 0.0f, 1.0f };
    if (side == RPG_GRID_SIDE_LEFT) return (Vector2){ -1.0f, 0.0f };
    return (Vector2){ 0.0f, -1.0f };
}

static void RpgAttachments_DrawIcon(int type, Vector2 position, RpgGridSide side, float alpha)
{
    position = RpgStage_SnapRenderPoint(position);
    Vector2 direction = RpgAttachments_GetOutwardDirection(side);
    Vector2 perpendicular = { -direction.y, direction.x };
    if (type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
        // 保存旗は常に地面の上へ正立させる。取付辺の回転で旗形状を崩さない。
        Vector2 tip = { position.x, position.y - 23.0f };
        DrawLineEx(position, tip, 2.5f, Fade(DARKBROWN, alpha));
        DrawCircleV(tip, 2.5f, Fade(GOLD, alpha));
        return;
    }
    if (type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON) {
        // 取付面へ台座が触れるよう、押し部は支持面から3pxだけ空気側へ置く。
        Vector2 center = Vector2Add(position, Vector2Scale(direction, 3.0f));
        bool horizontalDirection = direction.x != 0.0f;
        Rectangle base = horizontalDirection ? (Rectangle){ center.x - 3.5f, center.y - 10.0f, 7.0f, 20.0f } :
                                               (Rectangle){ center.x - 10.0f, center.y - 3.5f, 20.0f, 7.0f };
        DrawRectangleRec(base, Fade(DARKGRAY, alpha));
        DrawRectangleLinesEx(base, 1.0f, Fade(RAYWHITE, alpha));
        DrawCircleV(center, 4.5f, Fade(RED, alpha));
        DrawCircleLines((int)center.x, (int)center.y, 4.5f, Fade(MAROON, alpha));
        return;
    }
    if (type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) return;
    Vector2 base = Vector2Add(position, Vector2Scale(direction, 3.0f));
    Vector2 coil = Vector2Add(position, Vector2Scale(direction, 12.0f));
    Vector2 sphere = Vector2Add(position, Vector2Scale(direction, 21.0f));
    // 1マスの外側セルからはみ出さないよう、土台・コイル・発生球を32px内へ収める。
    DrawLineEx((Vector2){ base.x - perpendicular.x * 8.0f, base.y - perpendicular.y * 8.0f },
               (Vector2){ base.x + perpendicular.x * 8.0f, base.y + perpendicular.y * 8.0f },
               5.0f, Fade(DARKGRAY, alpha));
    DrawLineEx(position, coil, 2.0f, Fade(GOLD, alpha));
    DrawCircleV(coil, 5.5f, Fade(DARKBLUE, alpha));
    DrawCircleLines((int)coil.x, (int)coil.y, 5.5f, Fade(SKYBLUE, alpha));
    DrawCircleLines((int)coil.x, (int)coil.y, 3.0f, Fade(RAYWHITE, alpha));
    DrawLineEx(coil, sphere, 2.0f, Fade(SKYBLUE, alpha));
    DrawCircleV(sphere, 4.5f, Fade((Color){ 98, 221, 255, 255 }, alpha));
    DrawCircleLines((int)sphere.x, (int)sphere.y, 4.5f, Fade(RAYWHITE, alpha));
    DrawCircleLines((int)sphere.x, (int)sphere.y, 6.5f, Fade(SKYBLUE, alpha * 0.65f));
}

static void RpgAttachments_DrawSaveFlag(const RpgAttachment *attachment, Vector2 position, float alpha)
{
    position = RpgStage_SnapRenderPoint(position);
    RpgAttachments_DrawIcon(attachment->type, position, attachment->side, alpha);
    if (attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG && attachment->flagRaised) {
        Vector2 poleTop = { position.x, position.y - 22.0f };
        Vector2 flagBottom = { poleTop.x + 1.0f, poleTop.y + 11.0f };
        Vector2 flagTip = { poleTop.x + 13.0f, poleTop.y + 6.0f };
        DrawTriangle((Vector2){ poleTop.x + 1.0f, poleTop.y + 2.0f }, flagTip, flagBottom,
                     Fade(RED, alpha));
        DrawTriangleLines((Vector2){ poleTop.x + 1.0f, poleTop.y + 2.0f }, flagTip, flagBottom,
                          Fade(RAYWHITE, alpha));
    }
}

static void RpgAttachments_DrawWithOffset(const RpgAttachments *attachments, int firstColumn,
                                          int columnCount, int excludedIndex)
{
    int lastColumn = firstColumn + columnCount;
    for (int index = 0; index < attachments->count; index++) {
        if (index == excludedIndex) continue;
        const RpgAttachment *attachment = &attachments->entries[index];
        if (attachment->isZipperHeld) continue;
        RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
        if (outerCell.column < firstColumn || outerCell.column >= lastColumn) continue;
        Vector2 position = RpgAttachments_GetPosition(attachment, firstColumn);
        RpgAttachments_DrawSaveFlag(attachment, position, 0.94f);
    }
}

void RpgAttachments_Draw(const RpgAttachments *attachments)
{
    RpgAttachments_DrawWithOffset(attachments, 0, RPG_STAGE_WORLD_COLUMNS, -1);
}

void RpgAttachments_DrawMap(const RpgAttachments *attachments, int mapIndex)
{
    RpgAttachments_DrawWithOffset(attachments, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS, -1);
}

void RpgAttachments_DrawMapExcept(const RpgAttachments *attachments, int mapIndex, int excludedIndex)
{
    RpgAttachments_DrawWithOffset(attachments, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS, excludedIndex);
}

void RpgAttachments_DrawGhost(int type, Vector2 position, RpgGridSide side, bool isSnapped)
{
    RpgAttachments_DrawIcon(type, position, side, isSnapped ? 0.74f : 0.42f);
}

void RpgAttachments_DrawDataPaths(const RpgAttachments *attachments, int mapIndex)
{
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    int lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    for (int index = 0; index < attachments->count; index++) {
        if (attachments->entries[index].isZipperHeld ||
            attachments->entries[index].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) continue;
        const RpgGridPath *path = &attachments->entries[index].dataPath;
        for (int cellIndex = 0; cellIndex < path->cellCount - 1; cellIndex++) {
            RpgGridCell first = path->cells[cellIndex];
            RpgGridCell second = path->cells[cellIndex + 1];
            if (first.column < firstColumn || first.column >= lastColumn ||
                second.column < firstColumn || second.column >= lastColumn) continue;
            Vector2 start = RpgStage_SnapRenderPoint((Vector2){
                (first.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                first.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f });
            Vector2 end = RpgStage_SnapRenderPoint((Vector2){
                (second.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                second.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f });
            DrawLineEx(start, end, 3.0f, Fade(YELLOW, 0.75f));
        }
        RpgGridCell endCell = path->cells[path->cellCount - 1];
        if (endCell.column >= firstColumn && endCell.column < lastColumn) {
            Vector2 end = RpgStage_SnapRenderPoint((Vector2){
                (endCell.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                endCell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f });
            DrawCircleLines((int)end.x, (int)end.y, 8.0f, YELLOW);
        }
    }
}
// 役割: ブロックに付与する電波装置・ボタンなどの設置物を管理する。
