// Responsibility: bind key doors to the generic object-folder monitor.
#include "rpg_key_door_folder_monitor.h"

#include "rpg_object_folder.h"
#include "rpg_object_folder_monitor.h"

#include <stdio.h>
#include <string.h>

typedef struct RpgKeyDoorFolderWatch {
    int monitorHandle;
    int rootRow;
    int rootColumn;
    char keyPath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    bool dirty;
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
        *watch = (RpgKeyDoorFolderWatch){
            .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE,
            .rootRow = door->rootRow,
            .rootColumn = door->rootColumn,
            .dirty = true
        };
        snprintf(watch->keyPath, sizeof(watch->keyPath), "%s", door->keyPath);
    }
}

void RpgKeyDoorFolderMonitor_Update(RpgStage *stage)
{
    if (stage == NULL) return;
    if (watchedStage != stage) {
        RpgKeyDoorFolderMonitor_Reset();
        watchedStage = stage;
    }
    RpgObjectFolderMonitor_PollAll();
    for (int index = 0; index < stage->keyDoorCount; index++) {
        RpgKeyDoor *door = &stage->keyDoors[index];
        RpgKeyDoorFolderWatch *watch = &watches[index];
        RpgObjectFolder folder = { .cell = { door->rootRow, door->rootColumn } };
        char directory[1200];
        int blockType;
        const char *keyName;
        bool hasKey;
        if (door->rootRow < 0 || door->rootRow >= RPG_STAGE_ROWS || door->rootColumn < 0 ||
            door->rootColumn >= RPG_STAGE_WORLD_COLUMNS) continue;
        blockType = stage->blocks[door->rootRow][door->rootColumn];
        if (!RpgBlockInventory_IsKeyDoorBlock(blockType)) continue;
        PrepareWatch(watch, door);
        if (!RpgObjectFolder_GetBlockDirectoryPath(&folder, blockType, directory, sizeof(directory)) ||
            !RpgObjectFolder_DirectoryExists(directory)) {
            RpgObjectFolderMonitor_Disconnect(&watch->monitorHandle);
            watch->dirty = true;
            hasKey = false;
        } else {
            watch->monitorHandle = RpgObjectFolderMonitor_Connect(
                watch->monitorHandle, directory, true, OnKeyDoorFolderChanged, watch);
            if (!watch->dirty) continue;
            keyName = GetLeafName(door->keyPath);
            hasKey = keyName[0] != '\0' &&
                     RpgObjectFolder_DirectoryContainsFileNamed(directory, keyName);
        }
        if (RpgBlockInventory_IsKeyDoorOpen(blockType) != hasKey)
            (void)RpgStage_SetDoorOpenAtCell(stage, door->rootRow, door->rootColumn, hasKey);
        watch->dirty = false;
    }
}
