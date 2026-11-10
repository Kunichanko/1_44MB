#include "rpg_reference_folder_materialization.h"

#include "rpg_block_inventory.h"
#include "rpg_folder_travel_visual.h"
#include "rpg_object_folder.h"
#include "rpg_object_folder_monitor.h"
#include "rpg_reference_folder_monitor_trace.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

#include "raymath.h"

typedef struct RpgReferenceFolderWatch {
    int objectId;
    int monitorHandle;
    bool dirty;
} RpgReferenceFolderWatch;

typedef struct RpgReferenceFolderMaterialization {
    RpgStage *stage;
    RpgReferenceObjects *objects;
    RpgMagnetRuntime *magnetRuntime;
    RpgReferenceFolderWatch watches[RPG_REFERENCE_OBJECT_MAX_COUNT];
    bool active;
    int objectId;
    int blockType;
    bool restoresAir;
    bool restoresMovable;
    RpgGridCell destinationCell;
    Vector2 start;
    Vector2 destination;
    float elapsed;
    float duration;
    char returnSource[RPG_STAGE_REFERENCE_PATH_LENGTH];
    RpgReferenceObject originalObject;
    bool failureAnimating;
    bool folderStaged;
} RpgReferenceFolderMaterialization;

typedef struct RpgReferenceFolderMonitorTask {
    bool active;
    int objectId;
    int monitorHandle;
    unsigned int nativeSerialAtStart;
    float elapsed;
} RpgReferenceFolderMonitorTask;

/* A return/restore changes the directory too.  Remember the exact request
   that just failed so those self-generated notifications cannot launch the
   same travel again.  A later metadata or destination-cell change produces a
   different signature and is allowed to retry. */
typedef struct RpgRejectedMaterialization {
    int objectId;
    unsigned long long signature;
} RpgRejectedMaterialization;

static RpgReferenceFolderMaterialization materialization = { 0 };
static RpgReferenceFolderMonitorTask monitorTask = { .objectId = -1,
                                                      .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE };
static RpgRejectedMaterialization rejectedMaterialization = { .objectId = -1 };
static char pendingMonitorTaskMessage[160] = { 0 };
static bool materializationInitialized = false;

enum { RPG_REFERENCE_FOLDER_MONITOR_TASK_TIMEOUT_MS = 350 };

static void MarkFolderDirty(void *context, const char *directory)
{
    RpgReferenceFolderWatch *watch = context;
    (void)directory;
    if (watch != NULL) watch->dirty = true;
}

static void SetMonitorTaskMessage(const char *text)
{
    if (text == NULL) return;
    snprintf(pendingMonitorTaskMessage, sizeof(pendingMonitorTaskMessage), "%s", text);
}

static void ResetWatch(RpgReferenceFolderWatch *watch)
{
    if (watch == NULL) return;
    RpgObjectFolderMonitor_Disconnect(&watch->monitorHandle);
    *watch = (RpgReferenceFolderWatch){ .objectId = -1,
                                        .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE };
}

void RpgReferenceFolderMaterialization_Reset(void)
{
    if (materializationInitialized)
        for (int index = 0; index < RPG_REFERENCE_OBJECT_MAX_COUNT; index++) ResetWatch(&materialization.watches[index]);
    materialization = (RpgReferenceFolderMaterialization){ 0 };
    monitorTask = (RpgReferenceFolderMonitorTask){ .objectId = -1,
                                                     .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE };
    rejectedMaterialization = (RpgRejectedMaterialization){ .objectId = -1 };
    pendingMonitorTaskMessage[0] = '\0';
    for (int index = 0; index < RPG_REFERENCE_OBJECT_MAX_COUNT; index++)
        materialization.watches[index].monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
    materializationInitialized = true;
    RpgReferenceFolderMonitorTrace_Reset();
}

static RpgReferenceFolderWatch *FindWatch(int objectId)
{
    RpgReferenceFolderWatch *empty = NULL;
    for (int index = 0; index < RPG_REFERENCE_OBJECT_MAX_COUNT; index++) {
        RpgReferenceFolderWatch *watch = &materialization.watches[index];
        if (watch->objectId == objectId) return watch;
        if (empty == NULL && watch->objectId < 0) empty = watch;
    }
    return empty;
}

static int FindObjectIndex(const RpgReferenceObjects *objects, int objectId)
{
    if (objects == NULL) return -1;
    for (int index = 0; index < objects->count; index++)
        if (objects->entries[index].id == objectId) return index;
    return -1;
}

typedef enum RpgDropOrigin {
    RPG_DROP_ORIGIN_LEGACY_BLOCK,
    RPG_DROP_ORIGIN_BLOCK,
    RPG_DROP_ORIGIN_AIR,
    RPG_DROP_ORIGIN_MOVABLE
} RpgDropOrigin;

static bool ReadObjectInfo(const char *directory, int *type, Vector2 *position,
                           RpgGridCell *serializedCell, RpgDropOrigin *origin)
{
    char path[RPG_STAGE_REFERENCE_PATH_LENGTH + 64];
    char line[256];
    FILE *file;
    bool haveType = false, haveX = false, haveY = false;
    if (directory == NULL || type == NULL || position == NULL ||
        snprintf(path, sizeof(path), "%s\\object_info.txt", directory) <= 0) return false;
#ifdef _WIN32
    wchar_t widePath[RPG_STAGE_REFERENCE_PATH_LENGTH + 64];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath,
                            (int)(sizeof(widePath) / sizeof(widePath[0]))) <= 0 ||
        (file = _wfopen(widePath, L"rb")) == NULL) return false;
#else
    if ((file = fopen(path, "rb")) == NULL) return false;
#endif
    *type = 0;
    *position = (Vector2){ 0 };
    if (serializedCell != NULL) *serializedCell = (RpgGridCell){ -1, -1 };
    if (origin != NULL) *origin = RPG_DROP_ORIGIN_LEGACY_BLOCK;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "type=%d", type) == 1) { haveType = true; continue; }
        if (serializedCell != NULL && sscanf(line, "cell_row=%d", &serializedCell->row) == 1) continue;
        if (serializedCell != NULL && sscanf(line, "cell_column=%d", &serializedCell->column) == 1) continue;
        if (sscanf(line, "x=%f", &position->x) == 1) { haveX = true; continue; }
        if (sscanf(line, "y=%f", &position->y) == 1) { haveY = true; continue; }
        if (origin != NULL && strncmp(line, "drop_origin=air", 15) == 0)
            *origin = RPG_DROP_ORIGIN_AIR;
        else if (origin != NULL && strncmp(line, "drop_origin=movable", 19) == 0)
            *origin = RPG_DROP_ORIGIN_MOVABLE;
        else if (origin != NULL && strncmp(line, "drop_origin=block", 17) == 0)
            *origin = RPG_DROP_ORIGIN_BLOCK;
    }
    fclose(file);
    return haveType && haveX && haveY;
}

static bool GetMaterializationIntent(const RpgStage *stage, const char *directory, int *blockType,
                                     RpgGridCell *cell, Vector2 *position, bool *restoresAir,
                                     bool *restoresMovable)
{
    Vector2 serializedPosition;
    RpgGridCell serializedCell;
    RpgDropOrigin origin;
    if (stage == NULL || blockType == NULL || cell == NULL ||
        !ReadObjectInfo(directory, blockType, &serializedPosition, &serializedCell, &origin) ||
        /* A Folder can reconstruct a self-contained one-cell normal block.
           Composite and attachment configurations need their own complete
           metadata validators and therefore remain intentionally untouched. */
        (origin != RPG_DROP_ORIGIN_AIR &&
         (!RpgBlockInventory_IsOrdinaryBlock(*blockType) ||
          RpgBlockInventory_IsAttachmentBlock(*blockType) ||
          RpgBlockInventory_IsSocketSignalBlock(*blockType)))) return false;
    if (origin == RPG_DROP_ORIGIN_MOVABLE &&
        !RpgBlockInventory_IsMetalBlock(*blockType) && !RpgBlockInventory_IsPushBlock(*blockType)) return false;
    if (origin != RPG_DROP_ORIGIN_AIR && origin != RPG_DROP_ORIGIN_MOVABLE &&
        (RpgBlockInventory_IsMetalBlock(*blockType) || RpgBlockInventory_IsPushBlock(*blockType))) return false;
    if (serializedCell.row >= 0 && serializedCell.column >= 0) *cell = serializedCell;
    else if (!RpgStage_GetWorldCellAtPosition(stage, serializedPosition, &cell->row, &cell->column)) return false;
    if (cell->row < 0 || cell->row >= RPG_STAGE_ROWS || cell->column < 0 ||
        cell->column >= RPG_STAGE_WORLD_COLUMNS) return false;
    if (position != NULL) *position = serializedPosition;
    if (restoresAir != NULL) *restoresAir = origin == RPG_DROP_ORIGIN_AIR;
    if (restoresMovable != NULL) *restoresMovable = origin == RPG_DROP_ORIGIN_MOVABLE;
    return true;
}

static unsigned long long HashMaterializationBytes(unsigned long long hash, const unsigned char *bytes,
                                                   size_t byteCount)
{
    for (size_t index = 0; index < byteCount; index++) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static unsigned long long GetMaterializationRequestSignature(const RpgStage *stage, const char *directory)
{
    char path[RPG_STAGE_REFERENCE_PATH_LENGTH + 64];
    unsigned char bytes[256];
    unsigned long long hash = 1469598103934665603ULL;
    FILE *file = NULL;
    size_t byteCount;
    int blockType;
    RpgGridCell cell;
    bool restoresAir;
    bool restoresMovable;
    if (directory == NULL) return hash;
    hash = HashMaterializationBytes(hash, (const unsigned char *)directory, strlen(directory));
    if (snprintf(path, sizeof(path), "%s\\object_info.txt", directory) > 0) {
#ifdef _WIN32
        wchar_t widePath[RPG_STAGE_REFERENCE_PATH_LENGTH + 64];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath,
                                (int)(sizeof(widePath) / sizeof(widePath[0]))) > 0)
            file = _wfopen(widePath, L"rb");
#else
        file = fopen(path, "rb");
#endif
        if (file != NULL) {
            while ((byteCount = fread(bytes, 1, sizeof(bytes), file)) != 0)
                hash = HashMaterializationBytes(hash, bytes, byteCount);
            fclose(file);
        }
    }
    /* The destination state is part of the request.  For example, changing
       the target cell back to a red error is a meaningful new retry even if
       object_info itself did not change. */
    if (GetMaterializationIntent(stage, directory, &blockType, &cell, NULL,
                                 &restoresAir, &restoresMovable)) {
        const int destinationState = (int)RpgStage_GetCellState(stage, cell.row, cell.column);
        hash = HashMaterializationBytes(hash, (const unsigned char *)&blockType, sizeof(blockType));
        hash = HashMaterializationBytes(hash, (const unsigned char *)&cell, sizeof(cell));
        hash = HashMaterializationBytes(hash, (const unsigned char *)&destinationState, sizeof(destinationState));
    }
    return hash;
}

static void RememberRejectedMaterialization(const RpgStage *stage, int objectId, const char *directory)
{
    rejectedMaterialization.objectId = objectId;
    rejectedMaterialization.signature = GetMaterializationRequestSignature(stage, directory);
}

static bool IsUnchangedRejectedMaterialization(const RpgStage *stage, const RpgReferenceObject *object)
{
    unsigned long long signature;
    if (object == NULL || rejectedMaterialization.objectId != object->id) return false;
    signature = GetMaterializationRequestSignature(stage, object->path);
    if (signature == rejectedMaterialization.signature) return true;
    rejectedMaterialization = (RpgRejectedMaterialization){ .objectId = -1 };
    return false;
}

static bool IsActiveMonitorTaskForFolder(int objectId)
{
    return monitorTask.active && monitorTask.objectId == objectId;
}

static void BeginInPlaceMaterializationFailure(RpgReferenceObject *object)
{
    if (object == NULL) return;
    RememberRejectedMaterialization(materialization.stage, object->id, object->path);
    materialization.active = true;
    materialization.objectId = object->id;
    materialization.start = object->position;
    materialization.destination = object->position;
    materialization.elapsed = 0.0f;
    materialization.duration = 0.35f;
    materialization.failureAnimating = true;
    materialization.folderStaged = false;
    snprintf(materialization.returnSource, sizeof(materialization.returnSource), "%s", object->path);
    RpgReferenceFolderMonitorTrace_Log("materialization rejected before travel: folder=%d", object->id);
}

static void TryStartMaterialization(RpgStage *stage, RpgReferenceObjects *objects,
                                    RpgReferenceObject *object, float duration)
{
    int blockType;
    int objectIndex;
    RpgGridCell cell;
    bool restoresAir;
    bool restoresMovable;
    if (materialization.active || object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
        object->isMaterializing) return;
    if (!GetMaterializationIntent(stage, object->path, &blockType, &cell,
                                  NULL, &restoresAir, &restoresMovable)) {
        if (IsActiveMonitorTaskForFolder(object->id)) BeginInPlaceMaterializationFailure(object);
        return;
    }
    object->isMaterializing = true;
    materialization.active = true;
    materialization.objectId = object->id;
    materialization.blockType = blockType;
    materialization.restoresAir = restoresAir;
    materialization.restoresMovable = restoresMovable;
    materialization.destinationCell = cell;
    materialization.start = object->position;
    /* object_info's x/y is the persistent cell identity for a static Drop,
       not the live position of a movable solid.  Both return animations end
       at the original cell centre; the movable representation itself is
       registered at that cell's top-left after the animation. */
    materialization.destination = RpgStage_GetWorldPositionForCell(stage, cell.row, cell.column);
    materialization.elapsed = 0.0f;
    materialization.duration = Clamp(duration, 0.10f, 5.0f);
    snprintf(materialization.returnSource, sizeof(materialization.returnSource), "%s", object->path);
    materialization.originalObject = *object;
    materialization.originalObject.isMaterializing = false;
    materialization.folderStaged = false;
    /* A directory notification may arrive while gameplay is updating.  Do
       not move a potentially large Folder in that notification frame: begin
       the visual now, keep its on-disk directory in place, and commit the
       same direct Folder-to-build move only after the travel visual reaches
       its destination.  The runtime Folder object still disappears now, so
       the visible behaviour remains the same. */
    objectIndex = FindObjectIndex(objects, materialization.objectId);
    if (objectIndex < 0 || !RpgReferenceObjects_RemoveTarget(
            stage, objects, (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_DROP,
                                                   .dropIndex = objectIndex })) {
        object->isMaterializing = false;
        materialization.active = false;
        materialization.objectId = -1;
        return;
    }
    ResetWatch(FindWatch(materialization.objectId));
}

static bool RestoreOriginalFolderObject(void)
{
    RpgReferenceObjects *objects = materialization.objects;
    if (objects == NULL || objects->count >= RPG_REFERENCE_OBJECT_MAX_COUNT) return false;
    objects->entries[objects->count++] = materialization.originalObject;
    return true;
}

static void BeginMaterializationFailure(void)
{
    RpgObjectFolder folder = { .cell = materialization.destinationCell };
    bool returned = !materialization.folderStaged || RpgObjectFolder_CancelReturnReferenceFolderAsBlock(
        materialization.returnSource, &folder, materialization.blockType);
    RememberRejectedMaterialization(materialization.stage, materialization.objectId,
                                    materialization.returnSource);
    RpgReferenceFolderMonitorTrace_Log("materialization failed: folder=%d return=%d", materialization.objectId, returned);
    materialization.failureAnimating = true;
    materialization.elapsed = 0.0f;
    materialization.duration = 0.35f;
}

void RpgReferenceFolderMaterialization_QueueFolderMonitorTask(RpgStage *stage,
                                                               RpgReferenceObjects *objects,
                                                               int folderObjectId)
{
    int index;
    RpgReferenceObject *object;
    RpgReferenceFolderWatch *watch;
    if (stage == NULL || objects == NULL || folderObjectId <= 0) return;
    if (materialization.stage != stage || materialization.objects != objects) {
        RpgReferenceFolderMaterialization_Reset();
        materialization.stage = stage;
        materialization.objects = objects;
    }
    index = FindObjectIndex(objects, folderObjectId);
    if (index < 0 || objects->entries[index].objectKind != RPG_REFERENCE_OBJECT_FOLDER) return;
    object = &objects->entries[index];
    /* This is an explicit user operation, so it deliberately re-arms an
       otherwise unchanged Folder after a previous failed return. */
    if (rejectedMaterialization.objectId == object->id)
        rejectedMaterialization = (RpgRejectedMaterialization){ .objectId = -1 };
    watch = FindWatch(object->id);
    if (watch == NULL || object->path[0] == '\0') {
        RpgReferenceFolderMonitorTrace_Log("task rejected: folder=%d path unavailable", folderObjectId);
        SetMonitorTaskMessage(u8"Folder監視: 対象を監視できません");
        return;
    }
    if (watch->objectId != object->id) {
        ResetWatch(watch);
        watch->objectId = object->id;
    }
    watch->monitorHandle = RpgObjectFolderMonitor_Connect(watch->monitorHandle, object->path,
                                                           true, MarkFolderDirty, watch);
    /* A successful local move is authoritative, even if Win32 cannot create
       a notification handle.  Update() consumes this through the same path
       as an external notification. */
    watch->dirty = true;
    monitorTask = (RpgReferenceFolderMonitorTask){
        .active = true,
        .objectId = object->id,
        .monitorHandle = watch->monitorHandle,
        .nativeSerialAtStart = RpgObjectFolderMonitor_GetNativeChangeSerial(watch->monitorHandle),
        .elapsed = 0.0f
    };
    RpgReferenceFolderMonitorTrace_Log("task queued: folder=%d watch=%d native=%d path='%s'",
                                       object->id, watch->monitorHandle,
                                       RpgObjectFolderMonitor_IsWatching(watch->monitorHandle), object->path);
    SetMonitorTaskMessage(RpgObjectFolderMonitor_IsWatching(watch->monitorHandle) ?
                          u8"Folder監視: 変更を確認中" :
                          u8"Folder監視: Windows監視なし。ローカル確認を使用");
}

static void UpdateMonitorTask(const RpgReferenceObjects *objects, float deltaTime)
{
    int objectIndex;
    unsigned int nativeSerial;
    if (!monitorTask.active) return;
    monitorTask.elapsed += deltaTime;
    objectIndex = FindObjectIndex(objects, monitorTask.objectId);
    nativeSerial = RpgObjectFolderMonitor_GetNativeChangeSerial(monitorTask.monitorHandle);
    if (nativeSerial != monitorTask.nativeSerialAtStart) {
        RpgReferenceFolderMonitorTrace_Log("task complete: folder=%d Windows notification serial=%u",
                                           monitorTask.objectId, nativeSerial);
        SetMonitorTaskMessage(u8"Folder監視: Windows通知を確認");
        monitorTask.active = false;
    } else if (monitorTask.elapsed * 1000.0f >= RPG_REFERENCE_FOLDER_MONITOR_TASK_TIMEOUT_MS) {
        RpgReferenceFolderMonitorTrace_Log(
            "task fallback complete: folder=%d Windows notification unavailable; local validation dispatched; objectIndex=%d",
            monitorTask.objectId, objectIndex);
        SetMonitorTaskMessage(u8"Folder監視: 通知なし。ローカル確認で完了");
        monitorTask.active = false;
    }
}

static void CompleteMaterialization(void)
{
    RpgObjectFolder folder;
    Rectangle destinationBounds;
    if (materialization.stage == NULL) { BeginMaterializationFailure(); return; }
    folder.cell = materialization.destinationCell;
    if (materialization.restoresAir) {
        /* Air has no block folder or physics record to rebuild.  It still
           uses the same Folder return travel, then consumes that transfer
           object and clears the red missing-cell marker back to authored
           air.  This keeps an air Drop visible in transit without turning it
           into the type stored in a stale object_info.txt. */
        if (!RpgStage_IsCellError(materialization.stage, folder.cell.row, folder.cell.column) ||
            !RpgObjectFolder_DiscardReferenceFolder(&materialization.originalObject)) {
            BeginMaterializationFailure();
            return;
        }
        RpgStage_SetCellBlock(materialization.stage, folder.cell.row, folder.cell.column, 0);
        materialization.active = false;
        materialization.objectId = -1;
        return;
    }
    if (materialization.restoresMovable) {
        /* A dropped metal/push block deliberately left its old terrain cell
           as air.  Reconstructing it as a terrain block here loses its
           gravity and movement system.  Return the exact folder to the
           movable-object location, then register the normal runtime solid. */
        if (materialization.magnetRuntime == NULL ||
            RpgStage_GetCellState(materialization.stage, folder.cell.row, folder.cell.column) != RPG_CELL_AIR ||
            materialization.magnetRuntime->metalCount >= RPG_MAGNET_MAX_METALS) {
            BeginMaterializationFailure();
            return;
        }
        destinationBounds = RpgStage_GetWorldBoundsForCell(materialization.stage,
                                                             folder.cell.row, folder.cell.column);
        if (!RpgObjectFolder_ReturnReferenceFolderToDynamicBlock(
                &materialization.originalObject, folder.cell, materialization.blockType,
                (Vector2){ destinationBounds.x, destinationBounds.y }) ||
            !RpgMagnets_AddRuntimeBlock(materialization.magnetRuntime, materialization.stage,
                                        folder.cell, materialization.blockType,
                                        (Vector2){ destinationBounds.x, destinationBounds.y })) {
            BeginMaterializationFailure();
            return;
        }
        materialization.active = false;
        materialization.objectId = -1;
        return;
    }
    if (!RpgStage_IsCellError(materialization.stage, folder.cell.row, folder.cell.column) ||
        !RpgObjectFolder_ReturnBlockFolderToBuild(materialization.returnSource, &folder,
                                                  materialization.blockType)) {
        BeginMaterializationFailure();
        return;
    }
    RpgStage_SetCellBlock(materialization.stage, folder.cell.row, folder.cell.column,
                          materialization.blockType);
    materialization.active = false;
    materialization.objectId = -1;
}

void RpgReferenceFolderMaterialization_Update(RpgStage *stage, RpgReferenceObjects *objects,
                                              RpgMagnetRuntime *magnetRuntime,
                                              float deltaTime, float travelDuration)
{
    if (stage == NULL || objects == NULL) return;
    if (materialization.stage != stage || materialization.objects != objects ||
        materialization.magnetRuntime != magnetRuntime) {
        RpgReferenceFolderMaterialization_Reset();
        materialization.stage = stage;
        materialization.objects = objects;
        materialization.magnetRuntime = magnetRuntime;
    }
    for (int watchIndex = 0; watchIndex < RPG_REFERENCE_OBJECT_MAX_COUNT; watchIndex++) {
        RpgReferenceFolderWatch *watch = &materialization.watches[watchIndex];
        if (watch->objectId >= 0 && FindObjectIndex(objects, watch->objectId) < 0)
            ResetWatch(watch);
    }
    /* The runtime owns the single shared non-blocking monitor poll through
       RpgKeyDoorFolderMonitor_Update().  This module only registers its
       Folder-specific callbacks, avoiding a second full watcher sweep. */
    if (!materialization.active) for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *object = &objects->entries[index];
        RpgReferenceFolderWatch *watch;
        if (object->objectKind != RPG_REFERENCE_OBJECT_FOLDER || object->id <= 0 || object->path[0] == '\0') continue;
        watch = FindWatch(object->id);
        if (watch == NULL) continue;
        if (watch->objectId != object->id) {
            ResetWatch(watch);
            watch->objectId = object->id;
            /* Merely reconnecting a watcher must not be interpreted as a
               folder-content change.  In particular, a failed return puts
               the original Folder back into the reference list; marking the
               new watch dirty here immediately retried the same return and
               looped the spit/failure visuals forever.  Explicit local
               operations queue their own one-shot task above, while real
               filesystem changes arrive through MarkFolderDirty(). */
            watch->dirty = false;
        }
        watch->monitorHandle = RpgObjectFolderMonitor_Connect(watch->monitorHandle, object->path,
                                                               true, MarkFolderDirty, watch);
        if (watch->dirty) {
            watch->dirty = false;
            if (IsUnchangedRejectedMaterialization(stage, object)) {
                RpgReferenceFolderMonitorTrace_Log(
                    "materialization ignored: folder=%d unchanged after previous failure", object->id);
            } else {
                TryStartMaterialization(stage, objects, object, travelDuration);
            }
            if (materialization.active) break;
        }
    }
    UpdateMonitorTask(objects, deltaTime);
    if (!materialization.active) return;
    materialization.elapsed += deltaTime;
    if (materialization.elapsed < materialization.duration) return;
    if (materialization.failureAnimating) {
        /* The Folder runtime object was removed at travel start even though
           the physical directory was deliberately left in place.  Always
           restore that runtime object when a return cannot be committed. */
        (void)RestoreOriginalFolderObject();
        materialization.active = false;
        materialization.failureAnimating = false;
        materialization.objectId = -1;
    } else CompleteMaterialization();
}

bool RpgReferenceFolderMaterialization_ConsumeMonitorTaskMessage(char *text, size_t textSize)
{
    if (text == NULL || textSize == 0 || pendingMonitorTaskMessage[0] == '\0') return false;
    snprintf(text, textSize, "%s", pendingMonitorTaskMessage);
    pendingMonitorTaskMessage[0] = '\0';
    return true;
}

void RpgReferenceFolderMaterialization_Draw(void)
{
    if (!materialization.active) return;
    if (materialization.failureAnimating) {
        RpgFolderTravelVisual_DrawFailure(materialization.start,
                                          materialization.elapsed, materialization.duration);
        return;
    }
    RpgFolderTravelVisual_Draw(materialization.start, materialization.destination,
                               materialization.elapsed, materialization.duration);
}
