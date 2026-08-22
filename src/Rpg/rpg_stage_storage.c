// 依存する自プロジェクト内ファイル: rpg_stage_storage.h
// 役割: Settings/Stage/Stage番号の実フォルダを扱い、各ステージ設定を読み書きする。
#include "rpg_stage_storage.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

static bool GetStageRootPath(char *path, int size)
{
    return snprintf(path, (size_t)size, "%s../assets/Settings/Stage", GetApplicationDirectory()) > 0;
}

static bool GetCatalogPath(char *path, int size)
{
    char root[RPG_STAGE_PATH_LENGTH];
    return GetStageRootPath(root, (int)sizeof(root)) &&
           snprintf(path, (size_t)size, "%s\\stage_catalog.cfg", root) > 0;
}

static bool CreateDirectoryPath(const char *path)
{
#ifdef _WIN32
    wchar_t wide[RPG_STAGE_PATH_LENGTH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, RPG_STAGE_PATH_LENGTH) <= 0) return false;
    if (CreateDirectoryW(wide, NULL) != 0) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
#else
    (void)path;
    return false;
#endif
}

static void RemoveDirectoryTree(const char *directory)
{
#ifdef _WIN32
    char search[RPG_STAGE_PATH_LENGTH];
    wchar_t wideSearch[RPG_STAGE_PATH_LENGTH], widePath[RPG_STAGE_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, search, -1, wideSearch, RPG_STAGE_PATH_LENGTH) <= 0) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            char name[512], child[RPG_STAGE_PATH_LENGTH];
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
                WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, (int)sizeof(name), NULL, NULL) <= 0 ||
                snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) RemoveDirectoryTree(child);
            else if (MultiByteToWideChar(CP_UTF8, 0, child, -1, widePath, RPG_STAGE_PATH_LENGTH) > 0) DeleteFileW(widePath);
        } while (FindNextFileW(handle, &data) != 0);
        FindClose(handle);
    }
    if (MultiByteToWideChar(CP_UTF8, 0, directory, -1, widePath, RPG_STAGE_PATH_LENGTH) > 0)
        RemoveDirectoryW(widePath);
#else
    (void)directory;
#endif
}

void RpgStageCatalog_GetName(int stageNumber, char *name, int size)
{
    if (name != NULL && size > 0) snprintf(name, (size_t)size, "Stage%d", stageNumber);
}

static bool GetStageDirectoryPath(int stageNumber, char *path, int size)
{
    char root[RPG_STAGE_PATH_LENGTH], name[RPG_STAGE_NAME_LENGTH];
    if (stageNumber <= 0 || path == NULL || size <= 0 || !GetStageRootPath(root, (int)sizeof(root))) return false;
    RpgStageCatalog_GetName(stageNumber, name, (int)sizeof(name));
    return snprintf(path, (size_t)size, "%s\\%s", root, name) > 0;
}

bool RpgStageStorage_GetFilePath(int stageNumber, const char *fileName, char *path, int size)
{
    char directory[RPG_STAGE_PATH_LENGTH];
    return fileName != NULL && GetStageDirectoryPath(stageNumber, directory, (int)sizeof(directory)) &&
           snprintf(path, (size_t)size, "%s\\%s", directory, fileName) > 0;
}

bool RpgStageStorage_EnsureStageDirectory(int stageNumber)
{
    char root[RPG_STAGE_PATH_LENGTH], directory[RPG_STAGE_PATH_LENGTH];
    return GetStageRootPath(root, (int)sizeof(root)) && CreateDirectoryPath(root) &&
           GetStageDirectoryPath(stageNumber, directory, (int)sizeof(directory)) &&
           CreateDirectoryPath(directory);
}

static bool GetLegacyFilePath(const char *fileName, char *path, int size)
{
    char root[RPG_STAGE_PATH_LENGTH];
    return GetStageRootPath(root, (int)sizeof(root)) &&
           snprintf(path, (size_t)size, "%s\\%s", root, fileName) > 0;
}

static void SetCatalogSavedState(RpgStageCatalog *catalog)
{
    catalog->savedCount = catalog->count;
    catalog->savedCurrentNumber = catalog->currentNumber;
    memcpy(catalog->savedNumbers, catalog->numbers, sizeof(catalog->numbers));
    catalog->deletedCount = 0;
}

bool RpgStageCatalog_Load(RpgStageCatalog *catalog)
{
    char path[RPG_STAGE_PATH_LENGTH], line[128];
    FILE *file;
    if (catalog == NULL) return false;
    memset(catalog, 0, sizeof(*catalog));
    if (!GetCatalogPath(path, (int)sizeof(path)) || (file = fopen(path, "r")) == NULL) {
        catalog->numbers[0] = 1;
        catalog->count = 1;
        catalog->currentNumber = 1;
        SetCatalogSavedState(catalog);
        return true;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        int number;
        if (sscanf(line, "stage %d", &number) == 1 && number > 0 &&
            catalog->count < RPG_STAGE_CATALOG_MAX_COUNT)
            catalog->numbers[catalog->count++] = number;
    }
    fclose(file);
    if (catalog->count == 0) { catalog->numbers[0] = 1; catalog->count = 1; }
    /* 選択中のステージはエディターの一時状態であり、保存データからは復元しない。 */
    catalog->currentNumber = catalog->numbers[0];
    SetCatalogSavedState(catalog);
    return true;
}

int RpgStageCatalog_FindIndex(const RpgStageCatalog *catalog, int stageNumber)
{
    if (catalog == NULL) return -1;
    for (int index = 0; index < catalog->count; index++)
        if (catalog->numbers[index] == stageNumber) return index;
    return -1;
}

int RpgStageCatalog_GetCurrentNumber(const RpgStageCatalog *catalog)
{ return catalog == NULL ? 1 : catalog->currentNumber; }

int RpgStageCatalog_GetNumberAt(const RpgStageCatalog *catalog, int index)
{ return catalog == NULL || index < 0 || index >= catalog->count ? 0 : catalog->numbers[index]; }

bool RpgStageCatalog_Select(RpgStageCatalog *catalog, int stageNumber)
{
    if (catalog == NULL || RpgStageCatalog_FindIndex(catalog, stageNumber) < 0) return false;
    catalog->currentNumber = stageNumber;
    return true;
}

int RpgStageCatalog_Add(RpgStageCatalog *catalog)
{
    int number = 1;
    if (catalog == NULL || catalog->count >= RPG_STAGE_CATALOG_MAX_COUNT) return 0;
    for (int index = 0; index < catalog->count; index++)
        if (catalog->numbers[index] >= number) number = catalog->numbers[index] + 1;
    catalog->numbers[catalog->count++] = number;
    catalog->currentNumber = number;
    return number;
}

bool RpgStageCatalog_DeleteCurrent(RpgStageCatalog *catalog)
{
    int index;
    if (catalog == NULL || catalog->count <= 1 ||
        (index = RpgStageCatalog_FindIndex(catalog, catalog->currentNumber)) < 0) return false;
    if (catalog->deletedCount < RPG_STAGE_CATALOG_MAX_COUNT)
        catalog->deletedNumbers[catalog->deletedCount++] = catalog->currentNumber;
    memmove(&catalog->numbers[index], &catalog->numbers[index + 1],
            (size_t)(catalog->count - index - 1) * sizeof(catalog->numbers[0]));
    catalog->count--;
    catalog->currentNumber = catalog->numbers[index < catalog->count ? index : catalog->count - 1];
    return true;
}

void RpgStageCatalog_Revert(RpgStageCatalog *catalog)
{
    int selectedNumber;
    if (catalog == NULL) return;
    selectedNumber = catalog->currentNumber;
    catalog->count = catalog->savedCount;
    memcpy(catalog->numbers, catalog->savedNumbers, sizeof(catalog->numbers));
    /* Revert は追加・削除だけを戻し、閲覧中のステージは可能な限り維持する。 */
    catalog->currentNumber = RpgStageCatalog_FindIndex(catalog, selectedNumber) >= 0 ?
                             selectedNumber : catalog->numbers[0];
    catalog->deletedCount = 0;
}

bool RpgStageCatalog_IsDirty(const RpgStageCatalog *catalog)
{
    return catalog != NULL && (catalog->count != catalog->savedCount ||
        memcmp(catalog->numbers, catalog->savedNumbers,
               (size_t)catalog->count * sizeof(catalog->numbers[0])) != 0);
}

bool RpgStageCatalog_Save(RpgStageCatalog *catalog)
{
    char root[RPG_STAGE_PATH_LENGTH], path[RPG_STAGE_PATH_LENGTH];
    FILE *file;
    if (catalog == NULL || !GetStageRootPath(root, (int)sizeof(root)) || !CreateDirectoryPath(root) ||
        !GetCatalogPath(path, (int)sizeof(path))) return false;
    /* Stage1 は旧形式の設定を初回保存時に移行する。空フォルダを先に作らない。 */
    for (int index = 0; index < catalog->count; index++) {
        char marker[RPG_STAGE_PATH_LENGTH];
        if (catalog->numbers[index] == 1) continue;
        if (!GetStageDirectoryPath(catalog->numbers[index], marker, (int)sizeof(marker)) ||
            !CreateDirectoryPath(marker)) return false;
    }
    file = fopen(path, "w");
    if (file == NULL) return false;
    /* current は永続設定にしない。既存形式との互換のため固定値だけを書き出す。 */
    fprintf(file, "current 1\n");
    for (int index = 0; index < catalog->count; index++) fprintf(file, "stage %d\n", catalog->numbers[index]);
    if (fclose(file) != 0) return false;
    for (int index = 0; index < catalog->deletedCount; index++) {
        char folder[RPG_STAGE_PATH_LENGTH];
        if (GetStageDirectoryPath(catalog->deletedNumbers[index], folder, (int)sizeof(folder)))
            RemoveDirectoryTree(folder);
    }
    SetCatalogSavedState(catalog);
    return true;
}

static bool LoadFilePath(int stageNumber, const char *fileName, char *path, int size)
{
    if (RpgStageStorage_GetFilePath(stageNumber, fileName, path, size) && FileExists(path)) return true;
    return stageNumber == 1 && GetLegacyFilePath(fileName, path, size);
}

bool RpgStageStorage_LoadStage(int stageNumber, RpgStageData *data)
{
    char path[RPG_STAGE_PATH_LENGTH];
    if (data == NULL || stageNumber <= 0) return false;
    data->layout = RpgLayout_Default();
    data->stage = RpgStage_Default();
    data->dialogue = RpgDialogue_Default();
    data->stage3Event = RpgStage3Event_Default();
    data->npcInspectData = RpgInspect_Default("Inspect", "Nothing unusual here.");
    data->items = RpgItems_Default(); data->wires = RpgWires_Default();
    data->receivers = RpgReceivers_Default(); data->attachments = RpgAttachments_Default();
    data->signalBlocks = RpgSignalBlocks_Default(); data->mapEvents = RpgMapEvents_Default();
#define LOAD_STAGE_FILE(name, function, target) do { if (LoadFilePath(stageNumber, (name), path, (int)sizeof(path))) function(path, (target)); } while (0)
    LOAD_STAGE_FILE("rpg_layout.cfg", RpgLayout_Load, &data->layout);
    LOAD_STAGE_FILE("rpg_stage.cfg", RpgStage_Load, &data->stage);
    LOAD_STAGE_FILE("rpg_dialogue.txt", RpgDialogue_Load, &data->dialogue);
    LOAD_STAGE_FILE("rpg_stage3_event.cfg", RpgStage3Event_Load, &data->stage3Event);
    LOAD_STAGE_FILE("rpg_inspect.cfg", RpgInspect_Load, &data->npcInspectData);
    LOAD_STAGE_FILE("rpg_items.cfg", RpgItems_Load, &data->items);
    LOAD_STAGE_FILE("rpg_wires.cfg", RpgWires_Load, &data->wires);
    LOAD_STAGE_FILE("rpg_receivers.cfg", RpgReceivers_Load, &data->receivers);
    LOAD_STAGE_FILE("rpg_attachments.cfg", RpgAttachments_Load, &data->attachments);
    LOAD_STAGE_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Load, &data->signalBlocks);
    LOAD_STAGE_FILE("rpg_map_events.cfg", RpgMapEvents_Load, &data->mapEvents);
#undef LOAD_STAGE_FILE
    RpgWires_RemoveBroken(&data->wires, &data->stage);
    RpgReceivers_RemoveBroken(&data->receivers, &data->stage);
    RpgAttachments_MigrateLegacyButtons(&data->attachments, &data->stage);
    RpgAttachments_RemoveBroken(&data->attachments, &data->stage);
    RpgSignalBlocks_RemoveBroken(&data->signalBlocks, &data->stage);
    return true;
}

bool RpgStageStorage_SaveStage(int stageNumber, const RpgStageData *data)
{
    char folder[RPG_STAGE_PATH_LENGTH], path[RPG_STAGE_PATH_LENGTH];
    if (data == NULL || !GetStageDirectoryPath(stageNumber, folder, (int)sizeof(folder)) ||
        !RpgStageStorage_EnsureStageDirectory(stageNumber)) return false;
#define SAVE_STAGE_FILE(name, function, source) \
    (RpgStageStorage_GetFilePath(stageNumber, (name), path, (int)sizeof(path)) && function(path, (source)))
    return SAVE_STAGE_FILE("rpg_layout.cfg", RpgLayout_Save, &data->layout) &&
           SAVE_STAGE_FILE("rpg_stage.cfg", RpgStage_Save, &data->stage) &&
           SAVE_STAGE_FILE("rpg_dialogue.txt", RpgDialogue_Save, &data->dialogue) &&
           SAVE_STAGE_FILE("rpg_stage3_event.cfg", RpgStage3Event_Save, &data->stage3Event) &&
           SAVE_STAGE_FILE("rpg_inspect.cfg", RpgInspect_Save, &data->npcInspectData) &&
           SAVE_STAGE_FILE("rpg_items.cfg", RpgItems_Save, &data->items) &&
           SAVE_STAGE_FILE("rpg_wires.cfg", RpgWires_Save, &data->wires) &&
           SAVE_STAGE_FILE("rpg_receivers.cfg", RpgReceivers_Save, &data->receivers) &&
           SAVE_STAGE_FILE("rpg_attachments.cfg", RpgAttachments_Save, &data->attachments) &&
           SAVE_STAGE_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Save, &data->signalBlocks) &&
           SAVE_STAGE_FILE("rpg_map_events.cfg", RpgMapEvents_Save, &data->mapEvents);
#undef SAVE_STAGE_FILE
}
