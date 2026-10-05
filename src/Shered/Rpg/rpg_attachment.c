// 依存する自プロジェクト内ファイル: rpg_attachment.h
#include "rpg_attachment.h"

#include "rpg_object_folder.h"

#include "rpg_light_source.h"

#include "rpg_stage_background.h"
#include "rpg_gimic_sprites.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RPG_SHOOTER_ANIMATION_MILLISECONDS = 360 };
static const float RPG_SHOOTER_ANIMATION_DURATION =
    (float)RPG_SHOOTER_ANIMATION_MILLISECONDS / 1000.0f;
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

static bool RpgAttachments_IsStoredType(int type)
{
    return type == RPG_ATTACHMENT_TYPE_RECEIVER || RpgBlockInventory_IsAttachment(type);
}

bool RpgAttachments_IsReceiver(const RpgAttachment *attachment)
{
    return attachment != NULL && attachment->type == RPG_ATTACHMENT_TYPE_RECEIVER;
}

static bool RpgAttachments_ReserveModule(void **items, int *capacity, int required,
                                         size_t itemSize)
{
    int next;
    void *resized;
    if (items == NULL || capacity == NULL || required < 0) return false;
    if (required <= *capacity) return true;
    next = *capacity > 0 ? *capacity : RPG_ATTACHMENT_INITIAL_CAPACITY;
    while (next < required) next *= 2;
    resized = realloc(*items, (size_t)next * itemSize);
    if (resized == NULL) return false;
    memset((unsigned char *)resized + (size_t)*capacity * itemSize, 0,
           (size_t)(next - *capacity) * itemSize);
    *items = resized;
    *capacity = next;
    return true;
}

RpgShooterConfig *RpgAttachments_FindShooter(RpgAttachments *attachments, int attachmentId)
{
    if (attachments == NULL) return NULL;
    for (int i = 0; i < attachments->shooterCount; i++)
        if (attachments->shooters[i].attachmentId == attachmentId) return &attachments->shooters[i];
    return NULL;
}
const RpgShooterConfig *RpgAttachments_FindShooterConst(const RpgAttachments *attachments, int attachmentId)
{ return RpgAttachments_FindShooter((RpgAttachments *)attachments, attachmentId); }
RpgFlagConfig *RpgAttachments_FindFlag(RpgAttachments *attachments, int attachmentId)
{
    if (attachments == NULL) return NULL;
    for (int i = 0; i < attachments->flagCount; i++)
        if (attachments->flags[i].attachmentId == attachmentId) return &attachments->flags[i];
    return NULL;
}
const RpgFlagConfig *RpgAttachments_FindFlagConst(const RpgAttachments *attachments, int attachmentId)
{ return RpgAttachments_FindFlag((RpgAttachments *)attachments, attachmentId); }
RpgSocketConfig *RpgAttachments_FindSocket(RpgAttachments *attachments, int attachmentId)
{
    if (attachments == NULL) return NULL;
    for (int i = 0; i < attachments->socketCount; i++)
        if (attachments->sockets[i].attachmentId == attachmentId) return &attachments->sockets[i];
    return NULL;
}
const RpgSocketConfig *RpgAttachments_FindSocketConst(const RpgAttachments *attachments, int attachmentId)
{ return RpgAttachments_FindSocket((RpgAttachments *)attachments, attachmentId); }

static RpgShooterConfig RpgAttachments_DefaultShooter(int attachmentId, const RpgAttachment *attachment)
{
    RpgShooterConfig config = { .attachmentId = attachmentId, .dataSize = 8.0f,
        .dataSpeed = 120.0f, .dataInterval = 1.0f, .sizePerFile = RPG_STAGE_TILE_SIZE * 0.25f,
        .speedPerKilobyte = 1.0f / 64.0f, .previewFileCount = 2 };
    RpgGridCell body = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
    RpgGridCell front = RpgGridPath_GetSideNeighbor(body, attachment->side);
    config.path.cells[0] = body; config.path.cellCount = 1;
    if (RpgAttachments_IsCellInStage(front)) config.path.cells[config.path.cellCount++] = front;
    return config;
}

static bool RpgAttachments_EnsureModules(RpgAttachments *attachments, const RpgAttachment *attachment)
{
    if (attachment->type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER &&
        RpgAttachments_FindShooter(attachments, attachment->folderId) == NULL) {
        if (!RpgAttachments_ReserveModule((void **)&attachments->shooters, &attachments->shooterCapacity,
                                          attachments->shooterCount + 1, sizeof(*attachments->shooters))) return false;
        attachments->shooters[attachments->shooterCount++] =
            RpgAttachments_DefaultShooter(attachment->folderId, attachment);
    }
    if (attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG &&
        RpgAttachments_FindFlag(attachments, attachment->folderId) == NULL) {
        if (!RpgAttachments_ReserveModule((void **)&attachments->flags, &attachments->flagCapacity,
                                          attachments->flagCount + 1, sizeof(*attachments->flags))) return false;
        attachments->flags[attachments->flagCount++] =
            (RpgFlagConfig){ .attachmentId = attachment->folderId, .startZipperConnected = true };
    }
    if (attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET &&
        RpgAttachments_FindSocket(attachments, attachment->folderId) == NULL) {
        if (!RpgAttachments_ReserveModule((void **)&attachments->sockets, &attachments->socketCapacity,
                                          attachments->socketCount + 1, sizeof(*attachments->sockets))) return false;
        attachments->sockets[attachments->socketCount++] =
            (RpgSocketConfig){ .attachmentId = attachment->folderId, .rightLightAngle = 18.0f, .lightOpacity = 0.45f };
    }
    return true;
}

void RpgAttachments_RefreshEditorFacades(RpgAttachments *attachments)
{
    if (attachments == NULL) return;
    for (int i = 0; i < attachments->count; i++) {
        RpgAttachment *a = &attachments->entries[i];
        RpgShooterConfig *shooter = RpgAttachments_FindShooter(attachments, a->folderId);
        RpgFlagConfig *flag = RpgAttachments_FindFlag(attachments, a->folderId);
        RpgSocketConfig *socket = RpgAttachments_FindSocket(attachments, a->folderId);
        if (shooter != NULL) {
            a->dataSize = shooter->dataSize; a->dataSpeed = shooter->dataSpeed;
            a->dataInterval = shooter->dataInterval; a->dataPreviewEnabled = shooter->dataPreviewEnabled;
            a->sizePerFile = shooter->sizePerFile; a->speedPerKilobyte = shooter->speedPerKilobyte;
            a->previewFileCount = shooter->previewFileCount; a->previewTotalBytes = shooter->previewTotalBytes;
            a->dataPath = shooter->path; a->shooterAnimationElapsed = shooter->animationElapsed;
        }
        if (flag != NULL) { a->flagStartZipperConnected = flag->startZipperConnected; a->flagRaised = flag->raised; }
        if (socket != NULL) { a->socketRightLightAngle = socket->rightLightAngle; a->socketLightOpacity = socket->lightOpacity; }
    }
}

void RpgAttachments_SynchronizeModuleConfigs(RpgAttachments *attachments)
{
    if (attachments == NULL) return;
    for (int i = 0; i < attachments->count; i++) {
        RpgAttachment *a = &attachments->entries[i];
        RpgShooterConfig *shooter = RpgAttachments_FindShooter(attachments, a->folderId);
        RpgFlagConfig *flag = RpgAttachments_FindFlag(attachments, a->folderId);
        RpgSocketConfig *socket = RpgAttachments_FindSocket(attachments, a->folderId);
        if (shooter != NULL) {
            shooter->dataSize = a->dataSize; shooter->dataSpeed = a->dataSpeed;
            shooter->dataInterval = a->dataInterval; shooter->dataPreviewEnabled = a->dataPreviewEnabled;
            shooter->sizePerFile = a->sizePerFile; shooter->speedPerKilobyte = a->speedPerKilobyte;
            shooter->previewFileCount = a->previewFileCount; shooter->previewTotalBytes = a->previewTotalBytes;
            shooter->path = a->dataPath; shooter->animationElapsed = a->shooterAnimationElapsed;
        }
        if (flag != NULL) { flag->startZipperConnected = a->flagStartZipperConnected; flag->raised = a->flagRaised; }
        if (socket != NULL) { socket->rightLightAngle = a->socketRightLightAngle; socket->lightOpacity = a->socketLightOpacity; }
    }
}

/* A shooter lives in its outer cell.  Its default path begins there and ends
   one cell ahead, unless the stage boundary leaves no forward cell. */
static RpgGridCell RpgAttachments_GetShooterFrontCell(const RpgAttachment *attachment)
{
    RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
    RpgGridCell frontCell = RpgGridPath_GetSideNeighbor(outerCell, attachment->side);
    return RpgAttachments_IsCellInStage(frontCell) ? frontCell : outerCell;
}

static void RpgAttachments_SetDefaultShooterPath(const RpgAttachment *attachment, RpgShooterConfig *config)
{
    RpgGridCell bodyCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
    RpgGridCell frontCell = RpgAttachments_GetShooterFrontCell(attachment);
    if (attachment == NULL || config == NULL) return;
    config->path.cells[0] = bodyCell;
    config->path.cellCount = 1;
    if (frontCell.row != bodyCell.row || frontCell.column != bodyCell.column) {
        config->path.cells[1] = frontCell;
        config->path.cellCount = 2;
    }
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

/* A socket is embedded in its owner block.  Its neighbouring cell is only a
 * possible location for a movable block, never an attachment-owned cell or
 * a placement reservation. */
static bool RpgAttachments_RequiresEmptyOuterCell(int type)
{
    return type != RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET;
}

bool RpgAttachments_GetOccupiedCell(const RpgAttachment *attachment, RpgGridCell *cell)
{
    RpgGridCell occupiedCell;
    if (attachment == NULL || cell == NULL || !RpgBlockInventory_IsAttachmentBlock(attachment->type)) return false;
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
        if (RpgAttachments_IsRuntimeUnavailable(attachment)) continue;
        if (!RpgAttachments_GetOccupiedCell(attachment, &occupiedCell)) continue;
        if (occupiedCell.row == cell.row && occupiedCell.column == cell.column) return true;
    }
    return false;
}

RpgAttachments RpgAttachments_Default(void) { return (RpgAttachments){ 0 }; }

void RpgAttachments_Destroy(RpgAttachments *attachments)
{
    if (attachments == NULL) return;
    free(attachments->entries);
    free(attachments->routes);
    free(attachments->shooters);
    free(attachments->flags);
    free(attachments->sockets);
    *attachments = RpgAttachments_Default();
}

bool RpgAttachments_Reserve(RpgAttachments *attachments, int requiredCapacity)
{
    RpgAttachment *resized;
    int newCapacity;
    if (attachments == NULL || requiredCapacity < 0 || requiredCapacity > RPG_ATTACHMENT_LIMIT) return false;
    if (requiredCapacity <= attachments->capacity) return true;
    newCapacity = attachments->capacity > 0 ? attachments->capacity : RPG_ATTACHMENT_INITIAL_CAPACITY;
    while (newCapacity < requiredCapacity) {
        if (newCapacity >= RPG_ATTACHMENT_LIMIT / 2) { newCapacity = RPG_ATTACHMENT_LIMIT; break; }
        newCapacity *= 2;
    }
    resized = (RpgAttachment *)realloc(attachments->entries, (size_t)newCapacity * sizeof(*resized));
    if (resized == NULL) return false;
    memset(resized + attachments->capacity, 0,
           (size_t)(newCapacity - attachments->capacity) * sizeof(*resized));
    attachments->entries = resized;
    attachments->capacity = newCapacity;
    return true;
}

bool RpgAttachments_Append(RpgAttachments *attachments, const RpgAttachment *attachment)
{
    if (attachments == NULL || attachment == NULL ||
        !RpgAttachments_Reserve(attachments, attachments->count + 1)) return false;
    attachments->entries[attachments->count++] = *attachment;
    if (!RpgAttachments_EnsureModules(attachments, attachment)) {
        attachments->count--;
        return false;
    }
    RpgAttachments_RefreshEditorFacades(attachments);
    return true;
}

bool RpgAttachments_Clone(RpgAttachments *destination, const RpgAttachments *source)
{
    RpgAttachments clone = RpgAttachments_Default();
    if (destination == NULL || source == NULL || source->count < 0 ||
        source->count > RPG_ATTACHMENT_LIMIT || source->routeCount < 0 ||
        source->routeCount > RPG_WIRE_MAX_COUNT ||
        (source->count > 0 && source->entries == NULL) ||
        (source->routeCount > 0 && source->routes == NULL)) return false;
    if (source->count > 0 && !RpgAttachments_Reserve(&clone, source->count)) return false;
    if (source->count > 0)
        memcpy(clone.entries, source->entries, (size_t)source->count * sizeof(*clone.entries));
    clone.count = source->count;
    if (source->routeCount > 0) {
        clone.routes = (RpgAttachmentRoute *)calloc((size_t)source->routeCount, sizeof(*clone.routes));
        if (clone.routes == NULL) { RpgAttachments_Destroy(&clone); return false; }
        memcpy(clone.routes, source->routes, (size_t)source->routeCount * sizeof(*clone.routes));
        clone.routeCount = clone.routeCapacity = source->routeCount;
    }
    if (source->shooterCount > 0) {
        clone.shooters = (RpgShooterConfig *)calloc((size_t)source->shooterCount, sizeof(*clone.shooters));
        if (clone.shooters == NULL) { RpgAttachments_Destroy(&clone); return false; }
        memcpy(clone.shooters, source->shooters, (size_t)source->shooterCount * sizeof(*clone.shooters));
        clone.shooterCount = clone.shooterCapacity = source->shooterCount;
    }
    if (source->flagCount > 0) {
        clone.flags = (RpgFlagConfig *)calloc((size_t)source->flagCount, sizeof(*clone.flags));
        if (clone.flags == NULL) { RpgAttachments_Destroy(&clone); return false; }
        memcpy(clone.flags, source->flags, (size_t)source->flagCount * sizeof(*clone.flags));
        clone.flagCount = clone.flagCapacity = source->flagCount;
    }
    if (source->socketCount > 0) {
        clone.sockets = (RpgSocketConfig *)calloc((size_t)source->socketCount, sizeof(*clone.sockets));
        if (clone.sockets == NULL) { RpgAttachments_Destroy(&clone); return false; }
        memcpy(clone.sockets, source->sockets, (size_t)source->socketCount * sizeof(*clone.sockets));
        clone.socketCount = clone.socketCapacity = source->socketCount;
    }
    RpgAttachments_Destroy(destination);
    *destination = clone;
    RpgAttachments_RefreshEditorFacades(destination);
    return true;
}

static int RpgAttachments_FindReceiverIdAtCell(const RpgAttachments *attachments, RpgGridCell cell,
                                                RpgGridSide side)
{
    if (attachments == NULL) return 0;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (RpgAttachments_IsReceiver(attachment) && attachment->cell.row == cell.row &&
            attachment->cell.column == cell.column && attachment->side == side)
            return attachment->folderId;
    }
    return 0;
}

static const RpgAttachment *RpgAttachments_FindReceiverById(const RpgAttachments *attachments, int id)
{
    if (attachments == NULL || id <= 0) return NULL;
    for (int index = 0; index < attachments->count; index++)
        if (RpgAttachments_IsReceiver(&attachments->entries[index]) &&
            attachments->entries[index].folderId == id) return &attachments->entries[index];
    return NULL;
}

void RpgAttachments_ImportWires(RpgAttachments *attachments, const RpgWires *wires)
{
    if (attachments == NULL) return;
    free(attachments->routes);
    attachments->routes = NULL;
    attachments->routeCount = attachments->routeCapacity = 0;
    if (wires == NULL || wires->count <= 0) return;
    attachments->routes = (RpgAttachmentRoute *)calloc((size_t)wires->count, sizeof(*attachments->routes));
    if (attachments->routes == NULL) return;
    attachments->routeCapacity = wires->count;
    for (int index = 0; index < wires->count; index++) {
        const RpgWire *wire = &wires->entries[index];
        RpgAttachmentRoute *route = &attachments->routes[attachments->routeCount++];
        route->id = index + 1;
        route->kind = wire->kind;
        route->ownerCell = wire->ownerCell;
        route->cells = wire->path;
        route->conveyorSpeed = wire->conveyorSpeed;
        route->conveyorDirection = wire->conveyorDirection;
        route->conveyorHasFloor = wire->conveyorHasFloor;
        route->conveyorSlideAngleDegrees = wire->conveyorSlideAngleDegrees;
        if (wire->kind == RPG_WIRE_KIND_ELECTRIC && wire->hasReceiverSource)
            route->ownerAttachmentId = RpgAttachments_FindReceiverIdAtCell(
                attachments, wire->receiverCell, wire->receiverSide);
    }
}

void RpgAttachments_BuildRuntimeWires(const RpgAttachments *attachments, RpgWires *wires)
{
    if (wires == NULL) return;
    *wires = RpgWires_Default();
    if (attachments == NULL) return;
    for (int index = 0; index < attachments->routeCount && wires->count < RPG_WIRE_MAX_COUNT; index++) {
        const RpgAttachmentRoute *route = &attachments->routes[index];
        RpgWire wire = { .ownerCell = route->ownerCell, .path = route->cells, .kind = route->kind,
                         .conveyorSpeed = route->conveyorSpeed,
                         .conveyorDirection = route->conveyorDirection,
                         .conveyorHasFloor = route->conveyorHasFloor,
                         .conveyorSlideAngleDegrees = route->conveyorSlideAngleDegrees };
        if (wire.kind == RPG_WIRE_KIND_ELECTRIC) {
            const RpgAttachment *receiver = RpgAttachments_FindReceiverById(attachments, route->ownerAttachmentId);
            if (receiver == NULL) continue;
            wire.hasReceiverSource = true;
            wire.receiverCell = receiver->cell;
            wire.receiverSide = receiver->side;
            wire.ownerCell = receiver->cell;
        }
        wires->entries[wires->count++] = wire;
    }
}

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

#if 0 /* Pre-v9 standalone format reader/writer: retained as reference only.
          Unified v9-v11 is imported by the static loader below. */
bool RpgAttachments_Load(const char *filePath, RpgAttachments *attachments)
{
    FILE *file = fopen(filePath, "r");
    RpgAttachments loaded = RpgAttachments_Default();
    if (file == NULL) return false;
    char format[16];
    bool currentFormat = false;
    bool previewFormat = false;
    if (fscanf(file, "%15s", format) != 1) { fclose(file); return false; }
    currentFormat = strcmp(format, "v2") == 0 || strcmp(format, "v3") == 0 || strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 || strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 || strcmp(format, "v8") == 0;
    previewFormat = strcmp(format, "v3") == 0 || strcmp(format, "v4") == 0;
    if ((currentFormat ? fscanf(file, "%d", &loaded.count) : sscanf(format, "%d", &loaded.count)) != 1 || loaded.count < 0 ||
        loaded.count > RPG_ATTACHMENT_LIMIT || !RpgAttachments_Reserve(&loaded, loaded.count)) {
        fclose(file);
        return false;
    }
    for (int index = 0; index < loaded.count; index++) {
        RpgAttachment *attachment = &loaded.entries[index];
        int side = 0;
        int folderId = index + 1;
        /* v8 still starts each attachment with its stable folderId.  Treating
           it as the pre-v4 layout shifts every remaining field and makes the
           whole area attachment file fail to load. */
        bool folderIdFormat = strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 ||
                              strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 ||
                              strcmp(format, "v8") == 0;
        int fieldCount = folderIdFormat ?
            fscanf(file, "%d %d %d %d %d", &attachment->type, &folderId, &attachment->cell.row,
                   &attachment->cell.column, &side) :
            fscanf(file, "%d %d %d %d", &attachment->type, &attachment->cell.row,
                   &attachment->cell.column, &side);
        if (fieldCount != (folderIdFormat ? 5 : 4) ||
            !RpgAttachments_IsStoredType(attachment->type) ||
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
        attachment->socketRightLightAngle = 18.0f;
        attachment->socketLightOpacity = 0.45f;
        /* v7以前の旗開始は常に接続済みだったため、既存ステージも同じ挙動で移行する。 */
        attachment->flagStartZipperConnected = true;
        attachment->dataPath = (RpgGridPath){ .cellCount = 1, .cells = { outerCell } };
        if (attachment->type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER)
            RpgAttachments_SetDefaultShooterPath(attachment);
        if (currentFormat) {
            int previewEnabled = 0;
            float ignoredLegacyPreviewSize = 0.0f;
            float ignoredLegacyPreviewSpeed = 0.0f;
            bool currentSettingsFormat = strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 || strcmp(format, "v8") == 0;
            bool socketLightSettingsFormat = strcmp(format, "v7") == 0;
            bool flagStartSettingsFormat = strcmp(format, "v8") == 0;
            bool legacyPreviewSettingsFormat = strcmp(format, "v5") == 0;
            int flagStartConnected = 1;
            int readCount = flagStartSettingsFormat ?
                fscanf(file, "%f %f %f %d %f %f %d %llu %d %f %f %d", &attachment->dataSize, &attachment->dataSpeed,
                       &attachment->dataInterval, &previewEnabled, &attachment->sizePerFile,
                       &attachment->speedPerKilobyte, &attachment->previewFileCount,
                       &attachment->previewTotalBytes, &attachment->dataPath.cellCount,
                       &attachment->socketRightLightAngle, &attachment->socketLightOpacity, &flagStartConnected) : socketLightSettingsFormat ?
                fscanf(file, "%f %f %f %d %f %f %d %llu %d %f %f", &attachment->dataSize, &attachment->dataSpeed,
                       &attachment->dataInterval, &previewEnabled, &attachment->sizePerFile,
                       &attachment->speedPerKilobyte, &attachment->previewFileCount,
                       &attachment->previewTotalBytes, &attachment->dataPath.cellCount,
                       &attachment->socketRightLightAngle, &attachment->socketLightOpacity) : currentSettingsFormat ?
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
            if (readCount != (flagStartSettingsFormat ? 12 : socketLightSettingsFormat ? 11 : currentSettingsFormat ? 9 : legacyPreviewSettingsFormat ? 7 : previewFormat ? 5 : 4) ||
                attachment->dataSize < 2.0f || attachment->dataSize > 24.0f ||
                attachment->dataSpeed < 20.0f || attachment->dataSpeed > 480.0f ||
                attachment->dataInterval < 0.1f || attachment->dataInterval > 10.0f ||
                attachment->sizePerFile < 1.0f || attachment->sizePerFile > 64.0f ||
                attachment->speedPerKilobyte < 0.0001f || attachment->speedPerKilobyte > 1.0f ||
                attachment->previewFileCount < 1 || attachment->previewFileCount > 9999 ||
                attachment->dataPath.cellCount < 1 || attachment->dataPath.cellCount > RPG_GRID_PATH_MAX_CELLS) {
                fclose(file); return false;
            }
            attachment->socketRightLightAngle = Clamp(attachment->socketRightLightAngle, 0.0f, 80.0f);
            attachment->socketLightOpacity = Clamp(attachment->socketLightOpacity, 0.05f, 0.95f);
            attachment->flagStartZipperConnected = flagStartConnected != 0;
            attachment->sizePerFile = RpgAttachments_NormalizeShotSizePerFile(attachment->sizePerFile);
            attachment->dataPreviewEnabled = previewEnabled != 0;
            for (int pathIndex = 0; pathIndex < attachment->dataPath.cellCount; pathIndex++)
                if (fscanf(file, "%d %d", &attachment->dataPath.cells[pathIndex].row,
                           &attachment->dataPath.cells[pathIndex].column) != 2 ||
                    !RpgAttachments_IsCellInStage(attachment->dataPath.cells[pathIndex])) {
                    fclose(file); return false;
                }
            if (attachment->type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER &&
                attachment->dataPath.cellCount == 1) {
                RpgGridCell frontCell = RpgAttachments_GetShooterFrontCell(attachment);
                if ((attachment->dataPath.cells[0].row == outerCell.row &&
                     attachment->dataPath.cells[0].column == outerCell.column) ||
                    (attachment->dataPath.cells[0].row == frontCell.row &&
                     attachment->dataPath.cells[0].column == frontCell.column))
                    RpgAttachments_SetDefaultShooterPath(attachment);
            }
        }
    }
    if (fclose(file) != 0) { RpgAttachments_Destroy(&loaded); return false; }
    RpgAttachments_Destroy(attachments);
    *attachments = loaded;
    return true;
}

bool RpgAttachments_Save(const char *filePath, const RpgAttachments *attachments)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "v8 %d\n", attachments->count);
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        fprintf(file, "%d %d %d %d %d %.2f %.2f %.2f %d %.2f %.6f %d %llu %d %.1f %.3f %d", attachment->type, attachment->folderId, attachment->cell.row,
                attachment->cell.column, attachment->side, attachment->dataSize,
                attachment->dataSpeed, attachment->dataInterval, attachment->dataPreviewEnabled ? 1 : 0,
                attachment->sizePerFile, attachment->speedPerKilobyte, attachment->previewFileCount,
                attachment->previewTotalBytes, attachment->dataPath.cellCount,
                attachment->socketRightLightAngle, attachment->socketLightOpacity,
                attachment->flagStartZipperConnected ? 1 : 0);
        for (int pathIndex = 0; pathIndex < attachment->dataPath.cellCount; pathIndex++)
            fprintf(file, " %d %d", attachment->dataPath.cells[pathIndex].row,
                    attachment->dataPath.cells[pathIndex].column);
        fputc('\n', file);
    }
    return fclose(file) == 0;
}
#endif

/* The current area tree was written while attachments still used the
 * standalone v2-v8 file.  Those records contain the same editable values as
 * the newer per-kind modules, so import them once into the in-memory v12
 * model.  Do not mark this as unified: the caller must still read the
 * companion legacy wire/receiver files before the next save publishes v12. */
static bool RpgAttachments_LoadLegacyStatic(const char *filePath, RpgAttachments *attachments)
{
    FILE *file;
    char format[16];
    RpgAttachments loaded = RpgAttachments_Default();
    int count;
    bool versioned;
    bool previewFormat;
    if (filePath == NULL || attachments == NULL || (file = fopen(filePath, "r")) == NULL) return false;
    if (fscanf(file, "%15s", format) != 1) goto fail;
    versioned = strcmp(format, "v2") == 0 || strcmp(format, "v3") == 0 ||
                strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 ||
                strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 ||
                strcmp(format, "v8") == 0;
    previewFormat = strcmp(format, "v3") == 0 || strcmp(format, "v4") == 0;
    if ((versioned ? fscanf(file, "%d", &count) : sscanf(format, "%d", &count)) != 1 ||
        count < 0 || count > RPG_ATTACHMENT_LIMIT) goto fail;
    for (int index = 0; index < count; index++) {
        RpgAttachment attachment = { 0 };
        RpgShooterConfig shooter = { 0 };
        RpgGridCell outerCell;
        int folderId = index + 1;
        int side = 0;
        int previewEnabled = 0;
        int flagStartConnected = 1;
        int pathCount = 1;
        bool folderIdFormat = strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 ||
                              strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 ||
                              strcmp(format, "v8") == 0;
        bool settingsFormat = strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 ||
                              strcmp(format, "v8") == 0;
        bool socketSettingsFormat = strcmp(format, "v7") == 0 || strcmp(format, "v8") == 0;
        bool flagSettingsFormat = strcmp(format, "v8") == 0;
        float legacyPreviewSize = 0.0f, legacyPreviewSpeed = 0.0f;
        if ((folderIdFormat ?
             fscanf(file, "%d %d %d %d %d", &attachment.type, &folderId,
                    &attachment.cell.row, &attachment.cell.column, &side) :
             fscanf(file, "%d %d %d %d", &attachment.type, &attachment.cell.row,
                    &attachment.cell.column, &side)) != (folderIdFormat ? 5 : 4) ||
            !RpgAttachments_IsStoredType(attachment.type) ||
            !RpgAttachments_IsCellInStage(attachment.cell) ||
            side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) goto fail;
        attachment.folderId = RpgAttachments_RepairFolderId(&loaded, folderId);
        attachment.side = (RpgGridSide)side;
        outerCell = RpgGridPath_GetSideNeighbor(attachment.cell, attachment.side);
        shooter = (RpgShooterConfig){
            .attachmentId = attachment.folderId, .dataSize = 8.0f, .dataSpeed = 120.0f,
            .dataInterval = 1.0f, .sizePerFile = RPG_STAGE_TILE_SIZE * 0.25f,
            .speedPerKilobyte = 1.0f / 64.0f, .previewFileCount = 2,
            .path = { .cellCount = 1, .cells = { outerCell } }
        };
        attachment.socketRightLightAngle = 18.0f;
        attachment.socketLightOpacity = 0.45f;
        if (versioned) {
            int readCount;
            if (flagSettingsFormat) {
                readCount = fscanf(file, "%f %f %f %d %f %f %d %llu %d %f %f %d",
                    &shooter.dataSize, &shooter.dataSpeed, &shooter.dataInterval, &previewEnabled,
                    &shooter.sizePerFile, &shooter.speedPerKilobyte, &shooter.previewFileCount,
                    &shooter.previewTotalBytes, &pathCount, &attachment.socketRightLightAngle,
                    &attachment.socketLightOpacity, &flagStartConnected);
            } else if (socketSettingsFormat) {
                readCount = fscanf(file, "%f %f %f %d %f %f %d %llu %d %f %f",
                    &shooter.dataSize, &shooter.dataSpeed, &shooter.dataInterval, &previewEnabled,
                    &shooter.sizePerFile, &shooter.speedPerKilobyte, &shooter.previewFileCount,
                    &shooter.previewTotalBytes, &pathCount, &attachment.socketRightLightAngle,
                    &attachment.socketLightOpacity);
            } else if (settingsFormat) {
                readCount = fscanf(file, "%f %f %f %d %f %f %d %llu %d",
                    &shooter.dataSize, &shooter.dataSpeed, &shooter.dataInterval, &previewEnabled,
                    &shooter.sizePerFile, &shooter.speedPerKilobyte, &shooter.previewFileCount,
                    &shooter.previewTotalBytes, &pathCount);
            } else if (strcmp(format, "v5") == 0) {
                readCount = fscanf(file, "%f %f %f %d %f %f %d",
                    &shooter.dataSize, &shooter.dataSpeed, &shooter.dataInterval, &previewEnabled,
                    &legacyPreviewSize, &legacyPreviewSpeed, &pathCount);
            } else if (previewFormat) {
                readCount = fscanf(file, "%f %f %f %d %d", &shooter.dataSize,
                    &shooter.dataSpeed, &shooter.dataInterval, &previewEnabled, &pathCount);
            } else {
                readCount = fscanf(file, "%f %f %f %d", &shooter.dataSize,
                    &shooter.dataSpeed, &shooter.dataInterval, &pathCount);
            }
            if (readCount != (flagSettingsFormat ? 12 : socketSettingsFormat ? 11 :
                              settingsFormat ? 9 : strcmp(format, "v5") == 0 ? 7 :
                              previewFormat ? 5 : 4) || shooter.dataSize < 2.0f ||
                shooter.dataSize > 24.0f || shooter.dataSpeed < 20.0f ||
                shooter.dataSpeed > 480.0f || shooter.dataInterval < 0.1f ||
                shooter.dataInterval > 10.0f || shooter.sizePerFile < 1.0f ||
                shooter.sizePerFile > 64.0f || shooter.speedPerKilobyte < 0.0001f ||
                shooter.speedPerKilobyte > 1.0f || shooter.previewFileCount < 1 ||
                shooter.previewFileCount > 9999 || pathCount < 1 ||
                pathCount > RPG_GRID_PATH_MAX_CELLS) goto fail;
            shooter.dataPreviewEnabled = previewEnabled != 0;
            shooter.sizePerFile = RpgAttachments_NormalizeShotSizePerFile(shooter.sizePerFile);
            shooter.path.cellCount = pathCount;
            attachment.socketRightLightAngle = Clamp(attachment.socketRightLightAngle, 0.0f, 80.0f);
            attachment.socketLightOpacity = Clamp(attachment.socketLightOpacity, 0.05f, 0.95f);
            for (int pathIndex = 0; pathIndex < shooter.path.cellCount; pathIndex++)
                if (fscanf(file, "%d %d", &shooter.path.cells[pathIndex].row,
                           &shooter.path.cells[pathIndex].column) != 2 ||
                    !RpgAttachments_IsCellInStage(shooter.path.cells[pathIndex])) goto fail;
        }
        if (!RpgAttachments_Append(&loaded, &attachment)) goto fail;
        if (attachment.type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
            RpgShooterConfig *target = RpgAttachments_FindShooter(&loaded, attachment.folderId);
            if (target == NULL) goto fail;
            *target = shooter;
            if (target->path.cellCount == 1 &&
                ((target->path.cells[0].row == outerCell.row && target->path.cells[0].column == outerCell.column) ||
                 (target->path.cells[0].row == RpgAttachments_GetShooterFrontCell(&attachment).row &&
                  target->path.cells[0].column == RpgAttachments_GetShooterFrontCell(&attachment).column)))
                RpgAttachments_SetDefaultShooterPath(&attachment, target);
        } else if (attachment.type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
            RpgFlagConfig *target = RpgAttachments_FindFlag(&loaded, attachment.folderId);
            if (target == NULL) goto fail;
            target->startZipperConnected = flagStartConnected != 0;
        } else if (attachment.type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
            RpgSocketConfig *target = RpgAttachments_FindSocket(&loaded, attachment.folderId);
            if (target == NULL) goto fail;
            target->rightLightAngle = attachment.socketRightLightAngle;
            target->lightOpacity = attachment.socketLightOpacity;
        }
    }
    if (fclose(file) != 0) { RpgAttachments_Destroy(&loaded); return false; }
    RpgAttachments_RefreshEditorFacades(&loaded);
    RpgAttachments_Destroy(attachments);
    *attachments = loaded;
    return true;
fail:
    fclose(file);
    RpgAttachments_Destroy(&loaded);
    return false;
}

/* v9 is the canonical static format.  Keep the old v2-v8 reader above for
   one-way migration only; runtime objects may still use specialized arrays,
   but their editable source now has one attachment-owned file. */
static bool RpgAttachments_ReadStaticRecord(FILE *file, RpgAttachments *attachments)
{
    RpgAttachment attachment = { 0 };
    RpgShooterConfig shooter = { 0 };
    RpgFlagConfig flag = { 0 };
    RpgSocketConfig socket = { 0 };
    int side, previewEnabled, startConnected;
    if (file == NULL || attachments == NULL || attachments->count >= RPG_ATTACHMENT_LIMIT ||
        fscanf(file, "%d %d %d %d %d %f %f %f %d %f %f %d %llu %d %f %f %d",
               &attachment.type, &attachment.folderId, &attachment.cell.row,
               &attachment.cell.column, &side, &shooter.dataSize,
               &shooter.dataSpeed, &shooter.dataInterval, &previewEnabled,
               &shooter.sizePerFile, &shooter.speedPerKilobyte,
               &shooter.previewFileCount, &shooter.previewTotalBytes,
               &shooter.path.cellCount, &socket.rightLightAngle,
               &socket.lightOpacity, &startConnected) != 17 ||
        !RpgAttachments_IsStoredType(attachment.type) ||
        !RpgAttachments_IsCellInStage(attachment.cell) ||
        side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT ||
        shooter.dataSize < 2.0f || shooter.dataSize > 24.0f ||
        shooter.dataSpeed < 20.0f || shooter.dataSpeed > 480.0f ||
        shooter.dataInterval < 0.1f || shooter.dataInterval > 10.0f ||
        shooter.sizePerFile < 1.0f || shooter.sizePerFile > 64.0f ||
        shooter.speedPerKilobyte < 0.0001f || shooter.speedPerKilobyte > 1.0f ||
        shooter.previewFileCount < 1 || shooter.previewFileCount > 9999 ||
        shooter.path.cellCount < 1 || shooter.path.cellCount > RPG_GRID_PATH_MAX_CELLS)
        return false;
    attachment.folderId = RpgAttachments_RepairFolderId(attachments, attachment.folderId);
    attachment.side = (RpgGridSide)side;
    shooter.attachmentId = attachment.folderId;
    shooter.dataPreviewEnabled = previewEnabled != 0;
    shooter.sizePerFile = RpgAttachments_NormalizeShotSizePerFile(shooter.sizePerFile);
    socket.attachmentId = attachment.folderId;
    socket.rightLightAngle = Clamp(socket.rightLightAngle, 0.0f, 80.0f);
    socket.lightOpacity = Clamp(socket.lightOpacity, 0.05f, 0.95f);
    flag.attachmentId = attachment.folderId; flag.startZipperConnected = startConnected != 0;
    for (int index = 0; index < shooter.path.cellCount; index++)
        if (fscanf(file, "%d %d", &shooter.path.cells[index].row,
                   &shooter.path.cells[index].column) != 2 ||
            !RpgAttachments_IsCellInStage(shooter.path.cells[index])) return false;
    if (!RpgAttachments_Append(attachments, &attachment)) return false;
    if (attachment.type == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
        RpgShooterConfig *target = RpgAttachments_FindShooter(attachments, attachment.folderId);
        if (target != NULL) { *target = shooter; if (target->path.cellCount == 1) RpgAttachments_SetDefaultShooterPath(&attachment, target); }
    } else if (attachment.type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
        RpgFlagConfig *target = RpgAttachments_FindFlag(attachments, attachment.folderId); if (target != NULL) *target = flag;
    } else if (attachment.type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
        RpgSocketConfig *target = RpgAttachments_FindSocket(attachments, attachment.folderId); if (target != NULL) *target = socket;
    }
    return true;
}

#if 0 /* v11 writer retained only to document legacy on-disk records. */
static bool RpgAttachments_WriteStaticRecord(FILE *file, const RpgAttachment *attachment)
{
    if (file == NULL || attachment == NULL || attachment->dataPath.cellCount < 1 ||
        attachment->dataPath.cellCount > RPG_GRID_PATH_MAX_CELLS ||
        fprintf(file, "%d %d %d %d %d %.2f %.2f %.2f %d %.2f %.6f %d %llu %d %.1f %.3f %d",
                attachment->type, attachment->folderId, attachment->cell.row,
                attachment->cell.column, attachment->side, attachment->dataSize,
                attachment->dataSpeed, attachment->dataInterval,
                attachment->dataPreviewEnabled ? 1 : 0, attachment->sizePerFile,
                attachment->speedPerKilobyte, attachment->previewFileCount,
                attachment->previewTotalBytes, attachment->dataPath.cellCount,
                attachment->socketRightLightAngle, attachment->socketLightOpacity,
                attachment->flagStartZipperConnected ? 1 : 0) < 0) return false;
    for (int index = 0; index < attachment->dataPath.cellCount; index++)
        if (fprintf(file, " %d %d", attachment->dataPath.cells[index].row,
                    attachment->dataPath.cells[index].column) < 0) return false;
    return fputc('\n', file) != EOF;
}
#endif

static bool RpgAttachments_ReadRouteRecord(FILE *file, RpgAttachmentRoute *route)
{
    int kind, hasFloor, pathCount;
    RpgAttachmentRoute loaded = { 0 };
    if (file == NULL || route == NULL ||
        fscanf(file, "%d %d %f %d %f %d %d %d %d %d", &loaded.id, &kind,
               &loaded.conveyorSpeed, &loaded.conveyorDirection,
               &loaded.conveyorSlideAngleDegrees, &hasFloor, &loaded.ownerCell.row,
               &loaded.ownerCell.column, &loaded.ownerAttachmentId, &pathCount) != 10 ||
        loaded.id <= 0 || (kind != RPG_WIRE_KIND_ELECTRIC && kind != RPG_WIRE_KIND_CONVEYOR) ||
        (hasFloor != 0 && hasFloor != 1) || pathCount < 1 || pathCount > RPG_GRID_PATH_MAX_CELLS ||
        !RpgAttachments_IsCellInStage(loaded.ownerCell)) return false;
    loaded.kind = (RpgWireKind)kind;
    loaded.conveyorHasFloor = hasFloor != 0;
    loaded.cells.cellCount = pathCount;
    if (loaded.kind == RPG_WIRE_KIND_CONVEYOR &&
        (loaded.conveyorSpeed < 16.0f || loaded.conveyorSpeed > 960.0f ||
         (loaded.conveyorDirection != -1 && loaded.conveyorDirection != 1) ||
         loaded.conveyorSlideAngleDegrees < 0.0f || loaded.conveyorSlideAngleDegrees > 89.0f)) return false;
    for (int index = 0; index < pathCount; index++)
        if (fscanf(file, "%d %d", &loaded.cells.cells[index].row, &loaded.cells.cells[index].column) != 2 ||
            !RpgAttachments_IsCellInStage(loaded.cells.cells[index])) return false;
    *route = loaded;
    return true;
}

static bool RpgAttachments_WriteRouteRecord(FILE *file, const RpgAttachmentRoute *route)
{
    if (file == NULL || route == NULL || route->id <= 0 || route->cells.cellCount < 1 ||
        route->cells.cellCount > RPG_GRID_PATH_MAX_CELLS) return false;
    if (fprintf(file, "%d %d %.2f %d %.1f %d %d %d %d %d", route->id, route->kind,
                route->conveyorSpeed, route->conveyorDirection,
                route->conveyorSlideAngleDegrees, route->conveyorHasFloor ? 1 : 0,
                route->ownerCell.row, route->ownerCell.column, route->ownerAttachmentId,
                route->cells.cellCount) < 0) return false;
    for (int index = 0; index < route->cells.cellCount; index++)
        if (fprintf(file, " %d %d", route->cells.cells[index].row,
                    route->cells.cells[index].column) < 0) return false;
    return fputc('\n', file) != EOF;
}

bool RpgAttachments_LoadStatic(const char *filePath, RpgAttachments *attachments,
                               RpgWires *wires, bool *isUnifiedFormat)
{
    FILE *file;
    char format[16];
    int attachmentCount, receiverCount = 0, routeCount = 0, wireCount = 0;
    bool isV9, isV11;
    RpgAttachments loadedAttachments = RpgAttachments_Default();
    RpgWires loadedWires = RpgWires_Default();
    if (isUnifiedFormat != NULL) *isUnifiedFormat = false;
    if (filePath == NULL || attachments == NULL || wires == NULL ||
        (file = fopen(filePath, "r")) == NULL) return false;
    if (fscanf(file, "%15s", format) != 1) { fclose(file); return false; }
    if (strcmp(format, "v2") == 0 || strcmp(format, "v3") == 0 ||
        strcmp(format, "v4") == 0 || strcmp(format, "v5") == 0 ||
        strcmp(format, "v6") == 0 || strcmp(format, "v7") == 0 ||
        strcmp(format, "v8") == 0) {
        fclose(file);
        /* Keep isUnifiedFormat false: v2-v8 routes and receivers are still
           loaded from their companion legacy files by stage storage. */
        return RpgAttachments_LoadLegacyStatic(filePath, attachments);
    }
    if (strcmp(format, "v12") == 0) {
        int shooters, flags, sockets;
        if (fscanf(file, "%d %d %d %d %d", &attachmentCount, &shooters, &flags, &sockets, &routeCount) != 5 ||
            attachmentCount < 0 || attachmentCount > RPG_ATTACHMENT_LIMIT || shooters < 0 || flags < 0 ||
            sockets < 0 || routeCount < 0 || routeCount > RPG_WIRE_MAX_COUNT) { fclose(file); return false; }
        for (int i = 0; i < attachmentCount; i++) {
            RpgAttachment item = { 0 }; int side;
            if (fscanf(file, "%d %d %d %d %d", &item.type, &item.folderId, &item.cell.row,
                       &item.cell.column, &side) != 5 || !RpgAttachments_IsStoredType(item.type) ||
                !RpgAttachments_IsCellInStage(item.cell) || side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) {
                fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false;
            }
            item.side = (RpgGridSide)side;
            if (!RpgAttachments_Append(&loadedAttachments, &item)) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
        }
        for (int i = 0; i < shooters; i++) {
            RpgShooterConfig item = { 0 }; int preview, count;
            if (fscanf(file, "%d %f %f %f %d %f %f %d %llu %d", &item.attachmentId, &item.dataSize,
                       &item.dataSpeed, &item.dataInterval, &preview, &item.sizePerFile, &item.speedPerKilobyte,
                       &item.previewFileCount, &item.previewTotalBytes, &count) != 10 || count < 1 || count > RPG_GRID_PATH_MAX_CELLS) {
                fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false;
            }
            item.dataPreviewEnabled = preview != 0; item.path.cellCount = count;
            for (int n = 0; n < count; n++) if (fscanf(file, "%d %d", &item.path.cells[n].row, &item.path.cells[n].column) != 2) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
            RpgShooterConfig *target = RpgAttachments_FindShooter(&loadedAttachments, item.attachmentId);
            if (target == NULL) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; } *target = item;
        }
        for (int i = 0; i < flags; i++) { int connected; RpgFlagConfig item = { 0 };
            if (fscanf(file, "%d %d", &item.attachmentId, &connected) != 2 ||
                RpgAttachments_FindFlag(&loadedAttachments, item.attachmentId) == NULL) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
            RpgAttachments_FindFlag(&loadedAttachments, item.attachmentId)->startZipperConnected = connected != 0;
        }
        for (int i = 0; i < sockets; i++) { RpgSocketConfig item = { 0 };
            if (fscanf(file, "%d %f %f", &item.attachmentId, &item.rightLightAngle, &item.lightOpacity) != 3 ||
                RpgAttachments_FindSocket(&loadedAttachments, item.attachmentId) == NULL) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
            *RpgAttachments_FindSocket(&loadedAttachments, item.attachmentId) = item;
        }
        if (routeCount > 0 && !(loadedAttachments.routes = calloc((size_t)routeCount, sizeof(*loadedAttachments.routes)))) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
        loadedAttachments.routeCapacity = routeCount;
        for (int i = 0; i < routeCount; i++) if (!RpgAttachments_ReadRouteRecord(file, &loadedAttachments.routes[i])) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
        loadedAttachments.routeCount = routeCount;
        if (fclose(file) != 0) { RpgAttachments_Destroy(&loadedAttachments); return false; }
        RpgAttachments_RefreshEditorFacades(&loadedAttachments);
        RpgAttachments_Destroy(attachments); *attachments = loadedAttachments;
        RpgAttachments_BuildRuntimeWires(attachments, wires); if (isUnifiedFormat != NULL) *isUnifiedFormat = true;
        return true;
    }
    isV9 = strcmp(format, "v9") == 0;
    isV11 = strcmp(format, "v11") == 0;
    if (!isV9 && !isV11 && strcmp(format, "v10") != 0) {
        fclose(file);
        return false;
    }
    if (isUnifiedFormat != NULL) *isUnifiedFormat = true;
    if ((isV9 ? fscanf(file, "%d %d %d", &attachmentCount, &receiverCount, &wireCount) :
         isV11 ? fscanf(file, "%d %d", &attachmentCount, &routeCount) :
                 fscanf(file, "%d %d", &attachmentCount, &wireCount)) != (isV9 ? 3 : 2) ||
        attachmentCount < 0 || attachmentCount > RPG_ATTACHMENT_LIMIT ||
        receiverCount < 0 || receiverCount > RPG_ATTACHMENT_LIMIT ||
        routeCount < 0 || routeCount > RPG_WIRE_MAX_COUNT ||
        wireCount < 0 || wireCount > RPG_WIRE_MAX_COUNT) {
        fclose(file);
        return false;
    }
    for (int index = 0; index < attachmentCount; index++)
        if (!RpgAttachments_ReadStaticRecord(file, &loadedAttachments)) {
            fclose(file); return false;
        }
    for (int index = 0; index < receiverCount; index++) {
        RpgAttachment receiver = { .type = RPG_ATTACHMENT_TYPE_RECEIVER,
                                    .folderId = RpgAttachments_GetNextFolderId(&loadedAttachments) };
        int side;
        if (fscanf(file, "%d %d %d", &receiver.cell.row, &receiver.cell.column, &side) != 3 ||
            !RpgAttachments_IsCellInStage(receiver.cell) ||
            side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) {
            fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false;
        }
        receiver.side = (RpgGridSide)side;
        for (int attachmentIndex = 0; attachmentIndex < loadedAttachments.count; attachmentIndex++)
            if (RpgAttachments_IsReceiver(&loadedAttachments.entries[attachmentIndex]) &&
                loadedAttachments.entries[attachmentIndex].cell.row == receiver.cell.row &&
                loadedAttachments.entries[attachmentIndex].cell.column == receiver.cell.column) {
                fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false;
            }
        if (!RpgAttachments_Append(&loadedAttachments, &receiver)) {
            fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false;
        }
    }
    if (isV11 && routeCount > 0) {
        loadedAttachments.routes = (RpgAttachmentRoute *)calloc((size_t)routeCount,
                                                                  sizeof(*loadedAttachments.routes));
        if (loadedAttachments.routes == NULL) { fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false; }
        loadedAttachments.routeCapacity = routeCount;
        for (int index = 0; index < routeCount; index++)
            if (!RpgAttachments_ReadRouteRecord(file, &loadedAttachments.routes[index])) {
                fclose(file); RpgAttachments_Destroy(&loadedAttachments); return false;
            }
        loadedAttachments.routeCount = routeCount;
    }
    for (int index = 0; index < wireCount; index++)
        if (!RpgWires_ReadRecord(file, &loadedWires.entries[loadedWires.count])) {
            fclose(file); return false;
        } else loadedWires.count++;
    if (fclose(file) != 0) { RpgAttachments_Destroy(&loadedAttachments); return false; }
    RpgAttachments_RefreshEditorFacades(&loadedAttachments);
    RpgAttachments_Destroy(attachments);
    if (!isV11) RpgAttachments_ImportWires(&loadedAttachments, &loadedWires);
    *attachments = loadedAttachments;
    RpgAttachments_BuildRuntimeWires(attachments, wires);
    return true;
}

bool RpgAttachments_SaveStatic(const char *filePath, const RpgAttachments *attachments,
                               const RpgWires *wires)
{
    FILE *file;
    RpgAttachments stored = RpgAttachments_Default();
    if (filePath == NULL || attachments == NULL || wires == NULL ||
        (file = fopen(filePath, "w")) == NULL) return false;
    if (!RpgAttachments_Clone(&stored, attachments)) { fclose(file); return false; }
    RpgAttachments_SynchronizeModuleConfigs(&stored);
    RpgAttachments_ImportWires(&stored, wires);
    if (fprintf(file, "v12 %d %d %d %d %d\n", stored.count, stored.shooterCount,
                stored.flagCount, stored.socketCount, stored.routeCount) < 0) {
        RpgAttachments_Destroy(&stored); fclose(file); return false;
    }
    for (int index = 0; index < stored.count; index++) {
        const RpgAttachment *item = &stored.entries[index];
        if (fprintf(file, "%d %d %d %d %d\n", item->type, item->folderId, item->cell.row,
                    item->cell.column, item->side) < 0) {
            RpgAttachments_Destroy(&stored); fclose(file); return false;
        }
    }
    for (int index = 0; index < stored.shooterCount; index++) {
        const RpgShooterConfig *item = &stored.shooters[index];
        if (fprintf(file, "%d %.2f %.2f %.2f %d %.2f %.6f %d %llu %d", item->attachmentId,
                    item->dataSize, item->dataSpeed, item->dataInterval, item->dataPreviewEnabled ? 1 : 0,
                    item->sizePerFile, item->speedPerKilobyte, item->previewFileCount,
                    item->previewTotalBytes, item->path.cellCount) < 0) { RpgAttachments_Destroy(&stored); fclose(file); return false; }
        for (int cell = 0; cell < item->path.cellCount; cell++)
            if (fprintf(file, " %d %d", item->path.cells[cell].row, item->path.cells[cell].column) < 0) { RpgAttachments_Destroy(&stored); fclose(file); return false; }
        if (fputc('\n', file) == EOF) { RpgAttachments_Destroy(&stored); fclose(file); return false; }
    }
    for (int index = 0; index < stored.flagCount; index++)
        if (fprintf(file, "%d %d\n", stored.flags[index].attachmentId,
                    stored.flags[index].startZipperConnected ? 1 : 0) < 0) { RpgAttachments_Destroy(&stored); fclose(file); return false; }
    for (int index = 0; index < stored.socketCount; index++)
        if (fprintf(file, "%d %.1f %.3f\n", stored.sockets[index].attachmentId,
                    stored.sockets[index].rightLightAngle, stored.sockets[index].lightOpacity) < 0) { RpgAttachments_Destroy(&stored); fclose(file); return false; }
    for (int index = 0; index < stored.routeCount; index++)
        if (!RpgAttachments_WriteRouteRecord(file, &stored.routes[index])) {
            RpgAttachments_Destroy(&stored); fclose(file); return false;
        }
    RpgAttachments_Destroy(&stored);
    return fclose(file) == 0;
}

bool RpgAttachments_Add(RpgAttachments *attachments, RpgStage *stage, int type,
                        RpgGridCell cell, RpgGridSide side)
{
    RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(cell, side);
    RpgAttachment attachment = { .type = type,
        .folderId = RpgAttachments_GetNextFolderId(attachments), .cell = cell, .side = side };
    if (!RpgBlockInventory_IsAttachment(type) ||
        !RpgAttachments_IsCellInStage(cell) || stage->blocks[cell.row][cell.column] == 0 ||
        side < RPG_GRID_SIDE_TOP || side > RPG_GRID_SIDE_LEFT) return false;
    /* A block socket has a gravity-facing recess and is deliberately only
       mountable on the top face of its supporting block. */
    if (type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET && side != RPG_GRID_SIDE_TOP) return false;
    if (RpgAttachments_RequiresEmptyOuterCell(type) &&
        !RpgAttachments_HasOuterEmptyCell(stage, cell, side)) return false;
    if (RpgBlockInventory_IsAttachmentBlock(type) && RpgAttachments_IsCellOccupied(attachments, outerCell))
        return false;
    for (int index = 0; index < attachments->count; index++)
        if (RpgAttachments_AreSame(&attachments->entries[index], &attachment)) return false;
    if (!RpgAttachments_Append(attachments, &attachment)) return false;
    if (RpgBlockInventory_IsAttachmentBlock(type))
        stage->blocks[outerCell.row][outerCell.column] = type;
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
    RpgShooterConfig *shooter = RpgAttachments_FindShooter(attachments,
        attachments->entries[attachmentIndex].folderId);
    if (shooter == NULL) return false;
    return RpgGridPath_MoveEndpoint(&shooter->path, false,
                                    (RpgGridCell){ row, column }, 1);
}

bool RpgAttachments_FindDataPathEndpoint(const RpgAttachments *attachments, int row, int column,
                                         int *attachmentIndex)
{
    for (int index = attachments->count - 1; index >= 0; index--) {
        if (attachments->entries[index].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) continue;
        const RpgShooterConfig *shooter = RpgAttachments_FindShooterConst(attachments,
            attachments->entries[index].folderId);
        if (shooter == NULL) continue;
        const RpgGridPath *path = &shooter->path;
        RpgGridCell end = path->cells[path->cellCount - 1];
        if (end.row == row && end.column == column) { *attachmentIndex = index; return true; }
    }
    return false;
}

bool RpgAttachments_Remove(RpgAttachments *attachments, RpgStage *stage, RpgAttachment attachment)
{
    for (int index = 0; index < attachments->count; index++) {
        if (!RpgAttachments_AreSame(&attachments->entries[index], &attachment)) continue;
        RpgGridCell outerCell;
        if (stage != NULL && RpgBlockInventory_IsAttachmentBlock(attachments->entries[index].type) &&
            RpgAttachments_GetOccupiedCell(&attachments->entries[index], &outerCell) &&
            stage->blocks[outerCell.row][outerCell.column] == attachments->entries[index].type)
            stage->blocks[outerCell.row][outerCell.column] = 0;
        for (int next = index; next < attachments->count - 1; next++)
            attachments->entries[next] = attachments->entries[next + 1];
        attachments->count--;
        return true;
    }
    return false;
}

bool RpgAttachments_Replace(RpgAttachments *attachments, RpgStage *stage, int index,
                            RpgAttachment replacement)
{
    RpgGridCell oldOuter, newOuter;
    RpgAttachment old;
    if (attachments == NULL || stage == NULL || index < 0 || index >= attachments->count ||
        !RpgAttachments_IsCellInStage(replacement.cell)) return false;
    old = attachments->entries[index];
    if (RpgBlockInventory_IsAttachmentBlock(old.type) &&
        RpgAttachments_GetOccupiedCell(&old, &oldOuter) &&
        stage->blocks[oldOuter.row][oldOuter.column] == old.type)
        stage->blocks[oldOuter.row][oldOuter.column] = 0;
    if (RpgBlockInventory_IsAttachmentBlock(replacement.type)) {
        if (!RpgAttachments_GetOccupiedCell(&replacement, &newOuter) ||
            stage->blocks[newOuter.row][newOuter.column] != 0) {
            if (RpgBlockInventory_IsAttachmentBlock(old.type) &&
                RpgAttachments_GetOccupiedCell(&old, &oldOuter))
                stage->blocks[oldOuter.row][oldOuter.column] = old.type;
            return false;
        }
        stage->blocks[newOuter.row][newOuter.column] = replacement.type;
    }
    attachments->entries[index] = replacement;
    return true;
}

bool RpgAttachments_GetOwnerBlockCell(const RpgAttachment *attachment, RpgGridCell *cell)
{
    RpgGridCell outerCell;
    if (attachment == NULL || cell == NULL) return false;
    if (RpgBlockInventory_IsAttachmentBlock(attachment->type) &&
        RpgAttachments_GetOccupiedCell(attachment, &outerCell)) {
        *cell = outerCell;
        return true;
    }
    *cell = attachment->cell;
    return true;
}

bool RpgAttachments_IsOwnedByBlock(const RpgAttachment *attachment, RpgGridCell blockCell)
{
    RpgGridCell owner;
    return RpgAttachments_GetOwnerBlockCell(attachment, &owner) &&
           owner.row == blockCell.row && owner.column == blockCell.column;
}

void RpgAttachments_MaterializeBlockCells(RpgAttachments *attachments, RpgStage *stage)
{
    if (attachments == NULL || stage == NULL) return;
    for (int index = 0; index < attachments->count; index++) {
        RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell outerCell;
        if (!RpgAttachments_GetOccupiedCell(attachment, &outerCell)) continue;
        /* Save flags used to be block-backed.  On loading an old stage, remove
           only the matching legacy flag cell; never erase user terrain that
           happens to occupy the same cell. */
        if (!RpgBlockInventory_IsAttachmentBlock(attachment->type)) {
            if (stage->blocks[outerCell.row][outerCell.column] == attachment->type)
                stage->blocks[outerCell.row][outerCell.column] = 0;
            continue;
        }
        /* Existing stage data stored these only in rpg_attachments.cfg.  A
           conflicting terrain cell wins; RemoveBroken will discard the stale
           attachment rather than overwriting user-authored terrain. */
        if (stage->blocks[outerCell.row][outerCell.column] == 0)
            stage->blocks[outerCell.row][outerCell.column] = attachment->type;
    }
}

bool RpgAttachments_IsRuntimeUnavailable(const RpgAttachment *attachment)
{
    return attachment == NULL || attachment->isZipperHeld || attachment->isOwnerBlockZipperHeld;
}

void RpgAttachments_SetOwnerBlockZipperHeld(RpgAttachments *attachments, RpgGridCell blockCell,
                                             bool isHeld)
{
    if (attachments == NULL) return;
    for (int index = 0; index < attachments->count; index++) {
        RpgAttachment *attachment = &attachments->entries[index];
        if (RpgAttachments_IsOwnedByBlock(attachment, blockCell))
            attachment->isOwnerBlockZipperHeld = isHeld;
    }
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
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_DATA_BUTTON) continue;
        Vector2 position = RpgAttachments_GetPosition(attachment, 0);
        if (fabsf(playerPosition.x - position.x) <= 22.0f && fabsf(playerPosition.y - position.y) <= 24.0f)
            return true;
    }
    return false;
}

bool RpgAttachments_IsButtonPressedWorld(const RpgAttachments *attachments, const RpgStage *stage,
                                         Vector2 playerPosition)
{
    int playerMap;
    if (attachments == NULL || stage == NULL) return false;
    /* Buttons are Area-owned.  The assembled stage may contain many Areas,
       but a player can only trigger buttons belonging to the Area containing
       their centre.  This also avoids scanning unrelated button paths. */
    playerMap = RpgStage_GetMapAtWorldPosition(stage, playerPosition);
    if (playerMap < 0) return false;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell outerCell;
        Vector2 position;
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_DATA_BUTTON) continue;
        if (attachment->cell.column / RPG_STAGE_COLUMNS != playerMap) continue;
        outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
        position = RpgStage_GetWorldPositionForCell(stage, outerCell.row, outerCell.column);
        if (fabsf(playerPosition.x - position.x) <= 22.0f && fabsf(playerPosition.y - position.y) <= 24.0f)
            return true;
    }
    return false;
}

int RpgAttachments_FindBlockSocketAtBoundsWorld(const RpgAttachments *attachments,
                                                const RpgStage *stage, Rectangle blockBounds)
{
    const float horizontalTolerance = (float)RPG_BLOCK_SOCKET_HORIZONTAL_SNAP_TOLERANCE;
    /* A fixed block visibly settles into the 4px recess.  It still fully
       covers the source, so occupancy must tolerate only that intentional
       vertical descent, never a loose side-to-side overlap. */
    const float verticalTolerance = (float)(RPG_BLOCK_SOCKET_RECESS_DEPTH +
                                            RPG_BLOCK_SOCKET_VERTICAL_SNAP_TOLERANCE);
    if (attachments == NULL || stage == NULL) return -1;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell socketCell;
        Rectangle socketBounds;
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET ||
            attachment->side != RPG_GRID_SIDE_TOP) continue;
        socketCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
        if (!RpgAttachments_IsCellInStage(socketCell)) continue;
        socketBounds = RpgStage_GetWorldBoundsForCell(stage, socketCell.row, socketCell.column);
        /* The blue beam is blocked only when the whole 32px moving block is
           seated in this exact cell, not merely overlapping the rim. */
        if (fabsf(blockBounds.x - socketBounds.x) <= horizontalTolerance &&
            fabsf(blockBounds.y - socketBounds.y) <= verticalTolerance &&
            fabsf(blockBounds.width - socketBounds.width) <= horizontalTolerance &&
            fabsf(blockBounds.height - socketBounds.height) <= horizontalTolerance)
            return index;
    }
    return -1;
}

bool RpgAttachments_HasBlockSocketAtBaseCell(const RpgAttachments *attachments, int row, int column)
{
    if (attachments == NULL) return false;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (!RpgAttachments_IsRuntimeUnavailable(attachment) && attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET &&
            attachment->side == RPG_GRID_SIDE_TOP && attachment->cell.row == row &&
            attachment->cell.column == column) return true;
    }
    return false;
}

void RpgAttachments_DrawBlockSocketRecesses(const RpgAttachments *attachments, int mapIndex,
                                            const struct RpgStageBackground *background,
                                            Rectangle mapBounds, float brightness)
{
    if (attachments == NULL || background == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT)
        return;
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    int lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET ||
            attachment->side != RPG_GRID_SIDE_TOP || attachment->cell.row < 0 ||
            attachment->cell.row >= RPG_STAGE_ROWS || attachment->cell.column < firstColumn ||
            attachment->cell.column >= lastColumn) continue;
        Rectangle recess = {
            (attachment->cell.column - firstColumn) * RPG_STAGE_TILE_SIZE,
            attachment->cell.row * RPG_STAGE_TILE_SIZE,
            RPG_STAGE_TILE_SIZE, RPG_BLOCK_SOCKET_RECESS_DEPTH
        };
        RpgStageBackground_DrawRegion(background, mapBounds, recess, brightness);
    }
}

typedef struct RpgSocketLightOcclusionContext {
    const RpgStage *stage;
    const RpgMovingSolidSet *movingSolids;
    Vector2 localToWorld;
} RpgSocketLightOcclusionContext;

static bool RpgAttachments_IsSocketLightOccludedByMovingSolid(
    const RpgMovingSolidSet *movingSolids, Rectangle probe)
{
    if (movingSolids == NULL || movingSolids->entries == NULL) return false;
    for (int index = 0; index < movingSolids->count; index++)
        if (movingSolids->entries[index].bounds.width > 0.0f &&
            movingSolids->entries[index].bounds.height > 0.0f &&
            CheckCollisionRecs(probe, movingSolids->entries[index].bounds)) return true;
    return false;
}

static float RpgAttachments_RaycastSocketLight(void *rawContext, Vector2 localOrigin,
                                                Vector2 direction, float maximumLength)
{
    const RpgSocketLightOcclusionContext *context = rawContext;
    if (context == NULL || context->stage == NULL) return maximumLength;
    /* The 4px recess is intentionally empty; start testing after it so the
       supporting terrain does not self-occlude the embedded source. */
    const float sampleStep = 0.5f;
    for (float distance = (float)RPG_BLOCK_SOCKET_RECESS_DEPTH + sampleStep;
         distance <= maximumLength; distance += sampleStep) {
        Vector2 localPoint = Vector2Add(localOrigin, Vector2Scale(direction, distance));
        Vector2 worldPoint = Vector2Add(localPoint, context->localToWorld);
        Rectangle probe = { worldPoint.x - 0.15f, worldPoint.y - 0.15f, 0.30f, 0.30f };
        if (RpgStage_CheckSolidCollision(context->stage, probe) ||
            RpgAttachments_IsSocketLightOccludedByMovingSolid(context->movingSolids, probe))
            return fmaxf(0.0f, distance - sampleStep);
    }
    return maximumLength;
}

void RpgAttachments_DrawSocketLightsMap(const RpgAttachments *attachments, const RpgStage *stage,
                                        int mapIndex, const RpgMovingSolidSet *movingSolids)
{
    int firstColumn;
    int lastColumn;
    if (attachments == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT) return;
    firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        const RpgSocketConfig *socket = RpgAttachments_FindSocketConst(attachments, attachment->folderId);
        float left;
        float bottom;
        RpgLightSourceSegment source;
        RpgSocketLightOcclusionContext occlusion;
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET || socket == NULL ||
            attachment->side != RPG_GRID_SIDE_TOP || attachment->cell.row < 0 ||
            attachment->cell.row >= RPG_STAGE_ROWS || attachment->cell.column < firstColumn ||
            attachment->cell.column >= lastColumn) continue;
        left = (attachment->cell.column - firstColumn) * RPG_STAGE_TILE_SIZE;
        bottom = attachment->cell.row * RPG_STAGE_TILE_SIZE + RPG_BLOCK_SOCKET_RECESS_DEPTH - 1.0f;
        Rectangle worldCell = RpgStage_GetWorldBoundsForCell(stage, attachment->cell.row,
                                                             attachment->cell.column);
        occlusion = (RpgSocketLightOcclusionContext){
            .stage = stage,
            .movingSolids = movingSolids,
            .localToWorld = { worldCell.x - left,
                              worldCell.y - attachment->cell.row * RPG_STAGE_TILE_SIZE }
        };
        source = (RpgLightSourceSegment){
            .first = { left, bottom }, .second = { left + RPG_STAGE_TILE_SIZE, bottom },
            .firstDirectionDegrees = -socket->rightLightAngle,
            .secondDirectionDegrees = socket->rightLightAngle,
            .rayLength = RPG_STAGE_TILE_SIZE, .opacity = socket->lightOpacity,
            .color = { 70, 197, 255, 255 }
        };
        RpgLightSource_DrawSegmentFanOccluded(&source, RpgAttachments_RaycastSocketLight, &occlusion);
    }
}

int RpgAttachments_FindTouchedSaveFlag(const RpgAttachments *attachments, Vector2 playerPosition)
{
    if (attachments == NULL) return -1;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG) continue;
        if (Vector2Distance(playerPosition, RpgAttachments_GetPosition(attachment, 0)) <= 28.0f) return index;
    }
    return -1;
}

int RpgAttachments_FindTouchedSaveFlagWorld(const RpgAttachments *attachments, const RpgStage *stage,
                                            Vector2 playerPosition)
{
    if (attachments == NULL || stage == NULL) return -1;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (RpgAttachments_IsRuntimeUnavailable(attachment) || attachment->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG) continue;
        /* A flag belongs to its owner block.  Keep its world trigger at the
           shared attachment position rather than the former outer-cell centre. */
        if (Vector2Distance(playerPosition, RpgAttachments_GetPosition(attachment, 0)) <= 28.0f)
            return index;
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
        /* A flag follows its parent block into Zipper.  It cannot be the
           active respawn point until that parent has returned. */
        RpgFlagConfig *flag = RpgAttachments_FindFlag(attachments, attachment->folderId);
        if (flag == NULL) continue;
        flag->raised = !RpgAttachments_IsRuntimeUnavailable(attachment) && attachment->folderId == flagId;
        attachment->flagRaised = flag->raised;
        if (flag->raised) found = true;
    }
    return found;
}

bool RpgAttachments_StartShooterAnimation(RpgAttachments *attachments, int attachmentIndex)
{
    if (attachments == NULL || attachmentIndex < 0 || attachmentIndex >= attachments->count)
        return false;
    RpgAttachment *attachment = &attachments->entries[attachmentIndex];
    RpgShooterConfig *shooter = RpgAttachments_FindShooter(attachments, attachment->folderId);
    if (attachment->type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER || shooter == NULL ||
        shooter->animationElapsed > 0.0f)
        return false;
    shooter->animationElapsed = RPG_SHOOTER_ANIMATION_DURATION;
    attachment->shooterAnimationElapsed = shooter->animationElapsed;
    return true;
}

void RpgAttachments_UpdateShooterAnimations(RpgAttachments *attachments, float deltaTime)
{
    if (attachments == NULL || deltaTime <= 0.0f) return;
    for (int index = 0; index < attachments->shooterCount; index++) {
        RpgShooterConfig *shooter = &attachments->shooters[index];
        if (shooter->animationElapsed <= 0.0f) continue;
        shooter->animationElapsed -= deltaTime;
        if (shooter->animationElapsed < 0.0f) shooter->animationElapsed = 0.0f;
        for (int attachmentIndex = 0; attachmentIndex < attachments->count; attachmentIndex++)
            if (attachments->entries[attachmentIndex].folderId == shooter->attachmentId) {
                attachments->entries[attachmentIndex].shooterAnimationElapsed = shooter->animationElapsed;
                break;
            }
    }
}

float RpgAttachments_GetShooterAnimationProgress(const RpgAttachments *attachments, int attachmentId)
{
    const RpgShooterConfig *shooter = RpgAttachments_FindShooterConst(attachments, attachmentId);
    if (shooter == NULL || shooter->animationElapsed <= 0.0f)
        return 0.0f;
    float progress = 1.0f - shooter->animationElapsed / RPG_SHOOTER_ANIMATION_DURATION;
    if (progress < 0.0f) return 0.0f;
    if (progress > 1.0f) return 1.0f;
    return progress;
}

Vector2 RpgAttachments_GetPosition(const RpgAttachment *attachment, int firstColumn)
{
    RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
    float x = (outerCell.column - firstColumn) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    float y = outerCell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    /* Cell attachments live in the adjacent empty cell, so both their visual and hit position
       are the center of that occupied cell rather than the supporting block's edge. */
    if (RpgBlockInventory_IsAttachmentBlock(attachment->type)) return (Vector2){ x, y };
    /* The socket reserves the empty cell above for a seated block, but its
       actual body is a recess cut into the supporting block. */
    if (attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET)
        return (Vector2){ (attachment->cell.column - firstColumn) * RPG_STAGE_TILE_SIZE +
                              RPG_STAGE_TILE_SIZE * 0.5f,
                          attachment->cell.row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f };
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

Vector2 RpgAttachments_GetSaveFlagRespawnPositionWorld(const RpgAttachment *attachment,
                                                        const RpgStage *stage)
{
    Vector2 position = RpgStage_GetWorldPositionForCell(stage, attachment == NULL ? -1 : attachment->cell.row,
                                                         attachment == NULL ? -1 : attachment->cell.column);
    position.y -= RPG_STAGE_TILE_SIZE * 0.5f;
    return position;
}

int RpgAttachments_FindAtPosition(const RpgAttachments *attachments, Vector2 position, float distance)
{
    for (int index = attachments->count - 1; index >= 0; index--) {
        Vector2 attachmentPosition = RpgAttachments_GetPosition(&attachments->entries[index], 0);
        if (Vector2Distance(attachmentPosition, position) <= distance) return index;
    }
    return -1;
}

int RpgAttachments_FindAtWorldPosition(const RpgAttachments *attachments, const RpgStage *stage,
                                       Vector2 position, float distance)
{
    if (attachments == NULL || stage == NULL) return -1;
    for (int index = attachments->count - 1; index >= 0; index--) {
        const RpgAttachment *attachment = &attachments->entries[index];
        Vector2 storedPosition = RpgAttachments_GetPosition(attachment, 0);
        Vector2 storedCellCenter = {
            (attachment->cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
            (attachment->cell.row + 0.5f) * RPG_STAGE_TILE_SIZE
        };
        Vector2 worldCellCenter = RpgStage_GetWorldPositionForCell(stage, attachment->cell.row,
                                                                    attachment->cell.column);
        /* Preserve the attachment's side/socket offset while replacing only
         * the packed-cell origin with the actual stage-world origin. */
        Vector2 worldPosition = Vector2Add(worldCellCenter,
                                           Vector2Subtract(storedPosition, storedCellCenter));
        if (Vector2Distance(worldPosition, position) <= distance) return index;
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
            if (type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET && side != RPG_GRID_SIDE_TOP) continue;
            RpgAttachment candidate = { .type = type, .cell = { row, column },
                                        .side = (RpgGridSide)side };
            if (RpgAttachments_RequiresEmptyOuterCell(type) &&
                !RpgAttachments_HasOuterEmptyCell(stage, candidate.cell, candidate.side)) continue;
            RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(candidate.cell, candidate.side);
            if (RpgBlockInventory_IsAttachmentBlock(type)) {
                bool occupied = false;
                for (int attachmentIndex = 0; attachments != NULL && attachmentIndex < attachments->count; attachmentIndex++) {
                    const RpgAttachment *existing = &attachments->entries[attachmentIndex];
                    RpgGridCell occupiedCell;
                    if (attachmentIndex == ignoredAttachmentIndex ||
                        !RpgBlockInventory_IsAttachmentBlock(existing->type)) continue;
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

void RpgAttachments_RemoveBroken(RpgAttachments *attachments, RpgStage *stage)
{
    for (int index = 0; index < attachments->count;) {
        RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell cell = attachment->cell;
        RpgGridCell outerCell;
        bool isReceiver = RpgAttachments_IsReceiver(attachment);
        bool isBlockAttachment = RpgBlockInventory_IsAttachmentBlock(attachment->type);
        bool hasValidOuterCell = isReceiver || attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET ? true : isBlockAttachment ?
            (RpgAttachments_GetOccupiedCell(attachment, &outerCell) &&
             stage->blocks[outerCell.row][outerCell.column] == attachment->type) :
            RpgAttachments_HasOuterEmptyCell(stage, cell, attachment->side);
        if (RpgAttachments_IsRuntimeUnavailable(attachment) ||
            (RpgAttachments_IsCellInStage(cell) && stage->blocks[cell.row][cell.column] != 0 &&
             hasValidOuterCell)) {
            index++;
            continue;
        }
        if (isBlockAttachment && RpgAttachments_GetOccupiedCell(attachment, &outerCell) &&
            stage->blocks[outerCell.row][outerCell.column] == attachment->type)
            stage->blocks[outerCell.row][outerCell.column] = 0;
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

static float RpgAttachments_GetIconRotation(RpgGridSide side)
{
    /* Attachment art uses down as its zero-degree orientation. */
    if (side == RPG_GRID_SIDE_RIGHT) return 270.0f;
    if (side == RPG_GRID_SIDE_BOTTOM) return 0.0f;
    if (side == RPG_GRID_SIDE_LEFT) return 90.0f;
    return 180.0f;
}

static void RpgAttachments_DrawIcon(int type, Vector2 position, RpgGridSide side, float alpha,
                                    float shooterAnimationProgress)
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
    if (type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
        /* Terrain leaves this recessed region entirely transparent.  The
           small blue bottom/side strips are the embedded sensor, not a fill. */
        Rectangle recess = { position.x - 16.0f, position.y - 16.0f,
                              RPG_STAGE_TILE_SIZE, RPG_BLOCK_SOCKET_RECESS_DEPTH };
        DrawRectangleRec((Rectangle){ recess.x, recess.y, 2.0f, recess.height },
                         Fade((Color){ 43, 166, 235, 255 }, alpha));
        DrawRectangleRec((Rectangle){ recess.x + recess.width - 2.0f, recess.y,
                                       2.0f, recess.height },
                         Fade((Color){ 43, 166, 235, 255 }, alpha));
        DrawRectangleGradientV((int)recess.x + 2, (int)(recess.y + recess.height - 1.0f),
                               (int)recess.width - 4, 1,
                               Fade((Color){ 28, 103, 165, 255 }, alpha),
                               Fade((Color){ 114, 229, 255, 255 }, alpha));
        return;
    }
    if (type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) return;
    Rectangle shooterBounds = { position.x - RPG_STAGE_TILE_SIZE * 0.5f,
                                position.y - RPG_STAGE_TILE_SIZE * 0.5f,
                                RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
    if (RpgGimicSprites_DrawShooter(shooterBounds, RpgAttachments_GetIconRotation(side),
                                    shooterAnimationProgress, Fade(WHITE, alpha)))
        return;
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

static void RpgAttachments_DrawSaveFlag(const RpgAttachments *attachments, const RpgAttachment *attachment,
                                        Vector2 position, float alpha)
{
    const RpgFlagConfig *flag = RpgAttachments_FindFlagConst(attachments, attachment->folderId);
    position = RpgStage_SnapRenderPoint(position);
    RpgAttachments_DrawIcon(attachment->type, position, attachment->side, alpha,
                             RpgAttachments_GetShooterAnimationProgress(attachments, attachment->folderId));
    if (attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG && flag != NULL && flag->raised) {
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
        /* Receivers are drawn by the receiver compatibility view. */
        if (RpgAttachments_IsReceiver(attachment)) continue;
        if (RpgAttachments_IsRuntimeUnavailable(attachment)) continue;
        RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachment->cell, attachment->side);
        if (outerCell.column < firstColumn || outerCell.column >= lastColumn) continue;
        Vector2 position = RpgAttachments_GetPosition(attachment, firstColumn);
        RpgAttachments_DrawSaveFlag(attachments, attachment, position, 0.94f);
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
    RpgAttachments_DrawIcon(type, position, side, isSnapped ? 0.74f : 0.42f, 0.0f);
}

void RpgAttachments_DrawDataPaths(const RpgAttachments *attachments, int mapIndex)
{
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    int lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    for (int index = 0; index < attachments->count; index++) {
        if (RpgAttachments_IsRuntimeUnavailable(&attachments->entries[index]) ||
            attachments->entries[index].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) continue;
        const RpgShooterConfig *shooter = RpgAttachments_FindShooterConst(attachments,
            attachments->entries[index].folderId);
        if (shooter == NULL) continue;
        const RpgGridPath *path = &shooter->path;
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
