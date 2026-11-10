// Responsibility: bind key doors to the generic object-folder monitor.
#include "rpg_key_door_folder_monitor.h"

#include "rpg_object_folder.h"
#include "rpg_object_folder_monitor.h"
#include "rpg_key_door_trace.h"

#include <stdio.h>
#include <string.h>

typedef struct RpgKeyDoorFolderWatch {
    int monitorHandle;
    /* A missing object folder cannot own a change-notification handle.  Keep
       one non-recursive watch on its parent instead, so its return is an
       event rather than a DirectoryExists() poll every frame. */
    int parentMonitorHandle;
    int rootRow;
    int rootColumn;
    char keyPath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    bool dirty;
    bool directoryPresent;
} RpgKeyDoorFolderWatch;

static RpgStage *watchedStage = NULL;
static RpgKeyDoorFolderWatch watches[RPG_KEY_DOOR_MAX_COUNT];
static bool watchesInitialized = false;

static const char *GetLeafName(const char *path)
{
    const char *slash;
    if (path == NULL) return "";
    slash = strrchr(path, '\\');
    if (slash == NULL) slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static void OnKeyDoorFolderChanged(void *context, const char *directory)
{
    RpgKeyDoorFolderWatch *watch = context;
    (void)directory;
    if (watch != NULL) watch->dirty = true;
}

void RpgKeyDoorFolderMonitor_Reset(void)
{
    for (int index = 0; index < RPG_KEY_DOOR_MAX_COUNT; index++) {
        if (watchesInitialized)
            RpgObjectFolderMonitor_Disconnect(&watches[index].monitorHandle);
        watches[index] = (RpgKeyDoorFolderWatch){
            .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE,
            .parentMonitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE,
            .rootRow = -1,
            .rootColumn = -1
        };
    }
    watchesInitialized = true;
    watchedStage = NULL;
}

static void PrepareWatch(RpgKeyDoorFolderWatch *watch, const RpgKeyDoor *door)
{
    if (watch == NULL || door == NULL) return;
    if (watch->rootRow != door->rootRow || watch->rootColumn != door->rootColumn ||
        strcmp(watch->keyPath, door->keyPath) != 0) {
        RpgObjectFolderMonitor_Disconnect(&watch->monitorHandle);
        RpgObjectFolderMonitor_Disconnect(&watch->parentMonitorHandle);
        *watch = (RpgKeyDoorFolderWatch){
            .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE,
            .parentMonitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE,
            .rootRow = door->rootRow,
            .rootColumn = door->rootColumn,
            .dirty = true
        };
        snprintf(watch->keyPath, sizeof(watch->keyPath), "%s", door->keyPath);
    }
}

static bool GetParentDirectory(const char *directory, char *parent, size_t parentSize)
{
    char *separator;
    if (directory == NULL || parent == NULL || parentSize == 0 ||
        snprintf(parent, parentSize, "%s", directory) <= 0) return false;
    separator = strrchr(parent, '\\');
    if (separator == NULL) separator = strrchr(parent, '/');
    if (separator == NULL || separator == parent) return false;
    *separator = '\0';
    return parent[0] != '\0';
}

void RpgKeyDoorFolderMonitor_Update(RpgStage *stage)
{
    if (stage == NULL) return;
    if (watchedStage != stage) {
        RpgKeyDoorFolderMonitor_Reset();
        watchedStage = stage;
    }
    /* RpgObjectFolders_UpdateDataShotLifetimes owns the once-per-frame poll
       for the shared monitor set.  StoreFileInDirectory also immediately
       calls NotifyLocalChange, so this consumer only needs to consume dirty
       state and must not repeat the global poll for every key-door update. */
    for (int index = 0; index < stage->keyDoorCount; index++) {
        RpgKeyDoor *door = &stage->keyDoors[index];
        RpgKeyDoorFolderWatch *watch = &watches[index];
        RpgObjectFolder folder = { .cell = { door->rootRow, door->rootColumn } };
        char directory[1200];
        char parentDirectory[1200];
        int blockType;
        const char *keyName;
        bool hasKey;
        if (door->rootRow < 0 || door->rootRow >= RPG_STAGE_ROWS || door->rootColumn < 0 ||
            door->rootColumn >= RPG_STAGE_WORLD_COLUMNS) continue;
        blockType = stage->blocks[door->rootRow][door->rootColumn];
        if (!RpgBlockInventory_IsKeyDoorBlock(blockType)) continue;
        PrepareWatch(watch, door);

        /* The retained type makes an eaten key door recognisable for a
           future spit, but its object folder and lock state are absent while
           the root is a red-error cell.  Do not recreate/watch/open it until
           the whole composite has returned. */
        if (RpgStage_IsCellUnavailable(stage, door->rootRow, door->rootColumn)) {
            RpgObjectFolderMonitor_Disconnect(&watch->monitorHandle);
            RpgObjectFolderMonitor_Disconnect(&watch->parentMonitorHandle);
            watch->directoryPresent = false;
            watch->dirty = true;
            continue;
        }

        /* After the initial sync, only a content/parent notification may make
           this watch dirty.  In particular, an eaten door's absent folder is
           not stat'ed every frame. */
        if (!watch->dirty) continue;

        if (!RpgObjectFolder_GetBlockDirectoryPath(&folder, blockType, directory, sizeof(directory))) {
            RpgObjectFolderMonitor_Disconnect(&watch->monitorHandle);
            RpgObjectFolderMonitor_Disconnect(&watch->parentMonitorHandle);
            hasKey = false;
            watch->directoryPresent = false;
        } else if (!RpgObjectFolder_DirectoryExists(directory)) {
            RpgObjectFolderMonitor_Disconnect(&watch->monitorHandle);
            if (GetParentDirectory(directory, parentDirectory, sizeof(parentDirectory))) {
                watch->parentMonitorHandle = RpgObjectFolderMonitor_Connect(
                    watch->parentMonitorHandle, parentDirectory, false,
                    OnKeyDoorFolderChanged, watch);
            }
            hasKey = false;
            watch->directoryPresent = false;
        } else {
            RpgObjectFolderMonitor_Disconnect(&watch->parentMonitorHandle);
            watch->monitorHandle = RpgObjectFolderMonitor_Connect(
                watch->monitorHandle, directory, true, OnKeyDoorFolderChanged, watch);
            keyName = GetLeafName(door->keyPath);
            hasKey = keyName[0] != '\0' &&
                     RpgObjectFolder_DirectoryContainsFileNamed(directory, keyName);
            watch->directoryPresent = true;
        }
        if (RpgBlockInventory_IsKeyDoorOpen(blockType) != hasKey) {
            bool changed = RpgStage_SetDoorOpenAtCell(stage, door->rootRow, door->rootColumn, hasKey);
            RpgKeyDoorTrace_Log("door monitor: state update open=%d result=%d", hasKey, changed);
        }
        watch->dirty = false;
    }
}
