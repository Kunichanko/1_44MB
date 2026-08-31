// 依存する自プロジェクト内ファイル: rpg_stage_storage.h
// 役割: Settings/Stage/Stage番号の実フォルダを扱い、各ステージ設定を読み書きする。
#include "rpg_stage_storage.h"
#include "rpg_block_inventory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

static bool GetStageRootPath(char *path, int size)
{
    const char *suffix = RpgStageStorage_GetDomain() == RPG_STAGE_STORAGE_GAME_PACKAGE ? "Stage" : "../assets/Settings/Stage";
    return snprintf(path, (size_t)size, "%s%s", GetApplicationDirectory(), suffix) > 0;
}

static RpgStageStorageDomain storageDomain = RPG_STAGE_STORAGE_SETTINGS;
static bool GetStageDirectoryPath(int stageNumber, char *path, int size);

void RpgStageStorage_SetDomain(RpgStageStorageDomain domain)
{ storageDomain = domain; }

RpgStageStorageDomain RpgStageStorage_GetDomain(void)
{ return storageDomain; }

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

/* UTF-8 のステージ内パスを Windows のワイド文字 API 経由で開く。日本語名の参照ファイルも
   パッケージ化・展開できるよう、アーカイブ入出力はこの入口を必ず使う。 */
static FILE *OpenUtf8File(const char *path, const wchar_t *mode)
{
#ifdef _WIN32
    wchar_t widePath[RPG_STAGE_PATH_LENGTH];
    if (path == NULL || mode == NULL ||
        MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, RPG_STAGE_PATH_LENGTH) <= 0) return NULL;
    return _wfopen(widePath, mode);
#else
    (void)path;
    (void)mode;
    return NULL;
#endif
}

static bool IsSafeFolderName(const char *name)
{
    if (name == NULL || name[0] == '\0' || strlen(name) >= 120) return false;
    for (const unsigned char *cursor = (const unsigned char *)name; *cursor != '\0'; cursor++)
        if (*cursor < 0x20 || strchr("\\/:*?\"<>|", *cursor) != NULL) return false;
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
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

/* 静的な設計データだけを実行用パッケージへ複製する。旧 build は実行中データなので除外する。 */
static bool WriteStaticPackageTree(const char *source, const char *relativePath, FILE *package)
{
#ifdef _WIN32
    char search[RPG_STAGE_PATH_LENGTH];
    wchar_t wideSearch[RPG_STAGE_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (package == NULL || snprintf(search, sizeof(search), "%s\\*", source) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, search, -1, wideSearch, RPG_STAGE_PATH_LENGTH) <= 0) return false;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return true;
    do {
        char name[512], child[RPG_STAGE_PATH_LENGTH], childRelative[RPG_STAGE_PATH_LENGTH];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, (int)sizeof(name), NULL, NULL) <= 0 ||
            _stricmp(name, "build") == 0 ||
            snprintf(child, sizeof(child), "%s\\%s", source, name) <= 0 ||
            snprintf(childRelative, sizeof(childRelative), "%s%s%s", relativePath,
                     relativePath[0] == '\0' ? "" : "\\", name) <= 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!WriteStaticPackageTree(child, childRelative, package)) { FindClose(handle); return false; }
        } else {
            FILE *input = OpenUtf8File(child, L"rb");
            long length;
            char buffer[4096];
            size_t readCount;
            if (input == NULL || fseek(input, 0, SEEK_END) != 0 || (length = ftell(input)) < 0 ||
                fseek(input, 0, SEEK_SET) != 0 || fprintf(package, "FILE %u %ld\n", (unsigned)strlen(childRelative), length) < 0 ||
                fwrite(childRelative, 1, strlen(childRelative), package) != strlen(childRelative)) { if (input != NULL) fclose(input); FindClose(handle); return false; }
            while ((readCount = fread(buffer, 1, sizeof(buffer), input)) > 0)
                if (fwrite(buffer, 1, readCount, package) != readCount) { fclose(input); FindClose(handle); return false; }
            fclose(input);
        }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    return true;
#else
    (void)source; (void)relativePath; (void)package;
    return false;
#endif
}

static bool EnsurePackageParentDirectories(const char *path)
{
    char parent[RPG_STAGE_PATH_LENGTH];
    if (snprintf(parent, sizeof(parent), "%s", path) <= 0) return false;
    for (char *cursor = parent; *cursor != '\0'; cursor++) {
        if (*cursor != '\\') continue;
        *cursor = '\0';
        if (!CreateDirectoryPath(parent)) { *cursor = '\\'; return false; }
        *cursor = '\\';
    }
    return true;
}

static bool ExtractStaticPackage(int stageNumber)
{
    char root[RPG_STAGE_PATH_LENGTH], name[RPG_STAGE_NAME_LENGTH], packagePath[RPG_STAGE_PATH_LENGTH], target[RPG_STAGE_PATH_LENGTH];
    FILE *package;
    if (snprintf(root, sizeof(root), "%sStage\\game", GetApplicationDirectory()) <= 0) return false;
    RpgStageCatalog_GetName(stageNumber, name, (int)sizeof(name));
    if (snprintf(packagePath, sizeof(packagePath), "%s\\%s\\stage.package", root, name) <= 0 ||
        snprintf(target, sizeof(target), "%s\\%s\\static", root, name) <= 0 ||
        (package = OpenUtf8File(packagePath, L"rb")) == NULL) return false;
    RemoveDirectoryTree(target);
    if (!CreateDirectoryPath(target)) { fclose(package); return false; }
    for (;;) {
        unsigned pathLength = 0;
        long length = 0;
        int first = fgetc(package);
        if (first == EOF || first == 'E') break;
        ungetc(first, package);
        if (fscanf(package, "FILE %u %ld\n", &pathLength, &length) != 2 || pathLength == 0 || pathLength >= RPG_STAGE_PATH_LENGTH || length < 0) { fclose(package); return false; }
        char relative[RPG_STAGE_PATH_LENGTH], destination[RPG_STAGE_PATH_LENGTH], buffer[4096];
        FILE *output;
        if (fread(relative, 1, pathLength, package) != pathLength) { fclose(package); return false; }
        relative[pathLength] = '\0';
        if (snprintf(destination, sizeof(destination), "%s\\%s", target, relative) <= 0 || !EnsurePackageParentDirectories(destination) ||
            (output = OpenUtf8File(destination, L"wb")) == NULL) { fclose(package); return false; }
        long remaining = length;
        while (remaining > 0) {
            size_t chunk = remaining > (long)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
            if (fread(buffer, 1, chunk, package) != chunk || fwrite(buffer, 1, chunk, output) != chunk) { fclose(output); fclose(package); return false; }
            remaining -= (long)chunk;
        }
        fclose(output);
    }
    fclose(package);
    return true;
}

bool RpgStageStorage_PublishStage(int stageNumber)
{
    char source[RPG_STAGE_PATH_LENGTH], packageRoot[RPG_STAGE_PATH_LENGTH], packageStage[RPG_STAGE_PATH_LENGTH];
    char target[RPG_STAGE_PATH_LENGTH], temporary[RPG_STAGE_PATH_LENGTH];
    RpgStageStorageDomain previous = storageDomain;
    bool result;
    storageDomain = RPG_STAGE_STORAGE_SETTINGS;
    if (!GetStageDirectoryPath(stageNumber, source, (int)sizeof(source))) { storageDomain = previous; return false; }
    storageDomain = RPG_STAGE_STORAGE_GAME_PACKAGE;
    if (!GetStageRootPath(packageRoot, (int)sizeof(packageRoot))) { storageDomain = previous; return false; }
    RpgStageCatalog_GetName(stageNumber, packageStage, (int)sizeof(packageStage));
    if (!CreateDirectoryPath(packageRoot) ||
        snprintf(target, sizeof(target), "%s\\game", packageRoot) <= 0 ||
        !CreateDirectoryPath(target) ||
        snprintf(target, sizeof(target), "%s\\game\\%s", packageRoot, packageStage) <= 0 ||
        !CreateDirectoryPath(target) || snprintf(target, sizeof(target), "%s\\game\\%s\\stage.package", packageRoot, packageStage) <= 0 ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", target) <= 0) {
        storageDomain = previous; return false;
    }
    {
        wchar_t wideTemporary[RPG_STAGE_PATH_LENGTH], wideTarget[RPG_STAGE_PATH_LENGTH];
        FILE *package;
        if (MultiByteToWideChar(CP_UTF8, 0, temporary, -1, wideTemporary, RPG_STAGE_PATH_LENGTH) <= 0 ||
            MultiByteToWideChar(CP_UTF8, 0, target, -1, wideTarget, RPG_STAGE_PATH_LENGTH) <= 0) {
            storageDomain = previous;
            return false;
        }
        DeleteFileW(wideTemporary);
        package = OpenUtf8File(temporary, L"wb");
        if (package == NULL) result = false;
        else {
            result = WriteStaticPackageTree(source, "", package) && fputs("END\n", package) >= 0;
            if (fclose(package) != 0) result = false;
        }
        /* 完成済みの一時パッケージだけを置換する。失敗しても前回の本編用パッケージは壊さない。 */
        if (result) result = MoveFileExW(wideTemporary, wideTarget,
                                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!result) DeleteFileW(wideTemporary);
    }
    RpgStageStorage_ClearPackagedStaticStage(stageNumber);
    storageDomain = previous;
    return result;
}

bool RpgStageStorage_PublishCatalog(const RpgStageCatalog *catalog)
{
    char source[RPG_STAGE_PATH_LENGTH], destination[RPG_STAGE_PATH_LENGTH];
    RpgStageStorageDomain previous = storageDomain;
    bool result = false;
    (void)catalog;
    storageDomain = RPG_STAGE_STORAGE_SETTINGS;
    if (!GetCatalogPath(source, (int)sizeof(source))) goto finish;
    storageDomain = RPG_STAGE_STORAGE_GAME_PACKAGE;
    if (!GetCatalogPath(destination, (int)sizeof(destination))) goto finish;
#ifdef _WIN32
    {
        wchar_t wideSource[RPG_STAGE_PATH_LENGTH], wideDestination[RPG_STAGE_PATH_LENGTH], wideTemporary[RPG_STAGE_PATH_LENGTH];
        char temporary[RPG_STAGE_PATH_LENGTH];
        char root[RPG_STAGE_PATH_LENGTH];
        if (GetStageRootPath(root, (int)sizeof(root)) && CreateDirectoryPath(root) &&
            MultiByteToWideChar(CP_UTF8, 0, source, -1, wideSource, RPG_STAGE_PATH_LENGTH) > 0 &&
            MultiByteToWideChar(CP_UTF8, 0, destination, -1, wideDestination, RPG_STAGE_PATH_LENGTH) > 0 &&
            snprintf(temporary, sizeof(temporary), "%s.tmp", destination) > 0 &&
            MultiByteToWideChar(CP_UTF8, 0, temporary, -1, wideTemporary, RPG_STAGE_PATH_LENGTH) > 0) {
            DeleteFileW(wideTemporary);
            result = CopyFileW(wideSource, wideTemporary, FALSE) != 0 &&
                     MoveFileExW(wideTemporary, wideDestination,
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!result) DeleteFileW(wideTemporary);
        }
    }
#endif
finish:
    storageDomain = previous;
    return result;
}

void RpgStageStorage_ClearPackagedStaticStage(int stageNumber)
{
    char root[RPG_STAGE_PATH_LENGTH], name[RPG_STAGE_NAME_LENGTH], path[RPG_STAGE_PATH_LENGTH];
    if (stageNumber <= 0 || snprintf(root, sizeof(root), "%sStage\\game", GetApplicationDirectory()) <= 0) return;
    RpgStageCatalog_GetName(stageNumber, name, (int)sizeof(name));
    if (snprintf(path, sizeof(path), "%s\\%s\\static", root, name) > 0) RemoveDirectoryTree(path);
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
    return RpgStageStorage_GetDomain() == RPG_STAGE_STORAGE_GAME_PACKAGE ?
           snprintf(path, (size_t)size, "%s\\game\\%s\\static", root, name) > 0 :
           snprintf(path, (size_t)size, "%s\\%s", root, name) > 0;
}

bool RpgStageStorage_GetRuntimePath(int stageNumber, RpgStageRuntimeKind kind, char *path, int size)
{
    char name[RPG_STAGE_NAME_LENGTH];
    const char *folder = kind == RPG_STAGE_RUNTIME_EDITOR ? "editor" : "game";
    if (stageNumber <= 0 || path == NULL || size <= 0) return false;
    RpgStageCatalog_GetName(stageNumber, name, (int)sizeof(name));
    return snprintf(path, (size_t)size, "%sStage\\%s\\%s", GetApplicationDirectory(), folder, name) > 0;
}

/* 指定フォルダのファイルだけを使う共通の StageData 入出力。静的ストレージとは切り離して使える。 */
static bool GetDirectoryFilePath(const char *directory, const char *name, char *path, int size)
{
    return directory != NULL && name != NULL && path != NULL && size > 0 &&
           snprintf(path, (size_t)size, "%s\\%s", directory, name) > 0;
}

/* Version 0 was a single monolithic stage file set.  Keep this reader solely
   for the one-time migration below; new saves are never written this way. */
static bool LoadLegacyStageDataFromDirectory(const char *directory, RpgStageData *data)
{
    char path[RPG_STAGE_PATH_LENGTH];
    bool areaEntryEventsLoaded = false;
    /* 実行用構成が無い場合に既定ステージへ化けないよう、基幹ステージ定義の存在を必須にする。 */
    if (directory == NULL || data == NULL ||
        !GetDirectoryFilePath(directory, "rpg_stage.cfg", path, (int)sizeof(path)) || !FileExists(path)) return false;
    data->layout = RpgLayout_Default();
    RpgStage_Initialize(&data->stage);
    data->dialogue = RpgDialogue_Default();
    data->stage3Event = RpgStage3Event_Default();
    RpgAreaEntryEvents_Initialize(&data->areaEntryEvents);
    data->npcInspectData = RpgInspect_Default("Inspect", "Nothing unusual here.");
    data->items = RpgItems_Default(); data->wires = RpgWires_Default();
    data->receivers = RpgReceivers_Default(); data->attachments = RpgAttachments_Default();
    data->signalBlocks = RpgSignalBlocks_Default(); data->mapEvents = RpgMapEvents_Default();
#define LOAD_DIRECTORY_FILE(name, function, target) do { \
    if (GetDirectoryFilePath(directory, (name), path, (int)sizeof(path)) && FileExists(path)) function(path, (target)); \
} while (0)
    LOAD_DIRECTORY_FILE("rpg_layout.cfg", RpgLayout_Load, &data->layout);
    LOAD_DIRECTORY_FILE("rpg_stage.cfg", RpgStage_Load, &data->stage);
    LOAD_DIRECTORY_FILE("rpg_dialogue.txt", RpgDialogue_Load, &data->dialogue);
    LOAD_DIRECTORY_FILE("rpg_stage_entry_event.cfg", RpgStage3Event_Load, &data->stage3Event);
    if (GetDirectoryFilePath(directory, "rpg_area_entry_events.cfg", path, (int)sizeof(path)) && FileExists(path))
        areaEntryEventsLoaded = RpgAreaEntryEvents_Load(path, &data->stage, &data->areaEntryEvents);
    if (!areaEntryEventsLoaded && GetDirectoryFilePath(directory, "rpg_stage3_event.cfg", path, (int)sizeof(path)) && FileExists(path)) {
        RpgStage3Event legacyAreaEvent = RpgStage3Event_Default();
        int legacyAreaIndex = RpgStage_GetMapAtGrid(&data->stage, 2, 0);
        if (legacyAreaIndex >= 0 && RpgStage3Event_Load(path, &legacyAreaEvent))
            data->areaEntryEvents.entries[legacyAreaIndex] = legacyAreaEvent;
    }
    LOAD_DIRECTORY_FILE("rpg_inspect.cfg", RpgInspect_Load, &data->npcInspectData);
    LOAD_DIRECTORY_FILE("rpg_items.cfg", RpgItems_Load, &data->items);
    LOAD_DIRECTORY_FILE("rpg_wires.cfg", RpgWires_Load, &data->wires);
    LOAD_DIRECTORY_FILE("rpg_receivers.cfg", RpgReceivers_Load, &data->receivers);
    LOAD_DIRECTORY_FILE("rpg_attachments.cfg", RpgAttachments_Load, &data->attachments);
    LOAD_DIRECTORY_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Load, &data->signalBlocks);
    LOAD_DIRECTORY_FILE("rpg_map_events.cfg", RpgMapEvents_Load, &data->mapEvents);
#undef LOAD_DIRECTORY_FILE
    RpgWires_RemoveBroken(&data->wires, &data->stage);
    RpgReceivers_RemoveBroken(&data->receivers, &data->stage);
    RpgAttachments_MigrateLegacyButtons(&data->attachments, &data->stage);
    RpgAttachments_RemoveBroken(&data->attachments, &data->stage);
    RpgSignalBlocks_RemoveBroken(&data->signalBlocks, &data->stage);
    return true;
}

#if 0 /* The old writer is intentionally retained as source history, never built. */
static bool SaveLegacyStageDataToDirectory(const char *directory, const RpgStageData *data)
{
    char path[RPG_STAGE_PATH_LENGTH];
    if (directory == NULL || data == NULL || !CreateDirectoryPath(directory)) return false;
#define SAVE_DIRECTORY_FILE(name, function, source) \
    (GetDirectoryFilePath(directory, (name), path, (int)sizeof(path)) && function(path, (source)))
    return SAVE_DIRECTORY_FILE("rpg_layout.cfg", RpgLayout_Save, &data->layout) &&
           SAVE_DIRECTORY_FILE("rpg_stage.cfg", RpgStage_Save, &data->stage) &&
           SAVE_DIRECTORY_FILE("rpg_dialogue.txt", RpgDialogue_Save, &data->dialogue) &&
           SAVE_DIRECTORY_FILE("rpg_stage_entry_event.cfg", RpgStage3Event_Save, &data->stage3Event) &&
           (GetDirectoryFilePath(directory, "rpg_area_entry_events.cfg", path, (int)sizeof(path)) &&
            RpgAreaEntryEvents_Save(path, &data->stage, &data->areaEntryEvents)) &&
           SAVE_DIRECTORY_FILE("rpg_inspect.cfg", RpgInspect_Save, &data->npcInspectData) &&
           SAVE_DIRECTORY_FILE("rpg_items.cfg", RpgItems_Save, &data->items) &&
           SAVE_DIRECTORY_FILE("rpg_wires.cfg", RpgWires_Save, &data->wires) &&
           SAVE_DIRECTORY_FILE("rpg_receivers.cfg", RpgReceivers_Save, &data->receivers) &&
           SAVE_DIRECTORY_FILE("rpg_attachments.cfg", RpgAttachments_Save, &data->attachments) &&
           SAVE_DIRECTORY_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Save, &data->signalBlocks) &&
           SAVE_DIRECTORY_FILE("rpg_map_events.cfg", RpgMapEvents_Save, &data->mapEvents);
#undef SAVE_DIRECTORY_FILE
}
#endif

/*
 * Current storage layout
 * ----------------------
 * rpg_stage_layout.cfg owns only the topology: Area ID -> grid coordinate.
 * Areas/Area_<id>/ owns the data whose cell/position belongs to that Area ID.
 * Runtime still receives one assembled RpgStageData, so game systems do not
 * need a second coordinate system.  A move changes only the layout file;
 * editing an Area changes only that Area folder.
 */
static void InitializeStageData(RpgStageData *data)
{
    data->layout = RpgLayout_Default();
    RpgStage_Initialize(&data->stage);
    data->dialogue = RpgDialogue_Default();
    data->stage3Event = RpgStage3Event_Default();
    RpgAreaEntryEvents_Initialize(&data->areaEntryEvents);
    data->npcInspectData = RpgInspect_Default("Inspect", "Nothing unusual here.");
    data->items = RpgItems_Default();
    data->wires = RpgWires_Default();
    data->receivers = RpgReceivers_Default();
    data->attachments = RpgAttachments_Default();
    data->signalBlocks = RpgSignalBlocks_Default();
    data->mapEvents = RpgMapEvents_Default();
}

static bool GetLayoutPath(const char *directory, char *path, int size)
{ return GetDirectoryFilePath(directory, "rpg_stage_layout.cfg", path, size); }

static bool GetAreasPath(const char *directory, char *path, int size)
{ return GetDirectoryFilePath(directory, "Areas", path, size); }

static bool GetAreaDirectoryPath(const char *directory, int areaId, char *path, int size)
{
    char areas[RPG_STAGE_PATH_LENGTH];
    return areaId >= 0 && areaId < RPG_STAGE_MAP_COUNT && GetAreasPath(directory, areas, (int)sizeof(areas)) &&
           snprintf(path, (size_t)size, "%s\\Area_%d", areas, areaId) > 0;
}

static bool CellBelongsToArea(RpgGridCell cell, int areaId)
{ return cell.column >= 0 && cell.column / RPG_STAGE_COLUMNS == areaId; }

static bool PathBelongsToArea(const RpgGridPath *path, int areaId)
{
    if (path == NULL || path->cellCount <= 0) return false;
    for (int index = 0; index < path->cellCount; index++)
        if (!CellBelongsToArea(path->cells[index], areaId)) return false;
    return true;
}

static bool PositionBelongsToArea(const RpgStage *stage, Vector2 position, int areaId)
{ return stage != NULL && RpgStage_GetMapAtWorldPosition(stage, position) == areaId; }

static void ClearStageForLayout(RpgStage *stage)
{
    memset(stage, 0, sizeof(*stage));
    stage->spatialReferenceMap = -1;
    stage->imageObjects = RpgImageObjects_Default();
}

static bool SaveStageLayout(const char *directory, const RpgStage *stage)
{
    char path[RPG_STAGE_PATH_LENGTH];
    FILE *file;
    if (stage == NULL || !GetLayoutPath(directory, path, (int)sizeof(path)) ||
        (file = OpenUtf8File(path, L"wb")) == NULL) return false;
    fputs("rpg_stage_layout_v1\n", file);
    for (int areaId = 0; areaId < RPG_STAGE_MAP_COUNT; areaId++) if (stage->mapActive[areaId])
        fprintf(file, "area %d %d %d\n", areaId, stage->mapGridX[areaId], stage->mapGridY[areaId]);
    fputs("end\n", file);
    return fclose(file) == 0;
}

static bool LoadStageLayout(const char *directory, RpgStage *stage)
{
    char path[RPG_STAGE_PATH_LENGTH], token[32];
    FILE *file;
    bool used[RPG_STAGE_MAP_COUNT] = { false };
    if (stage == NULL || !GetLayoutPath(directory, path, (int)sizeof(path)) ||
        (file = OpenUtf8File(path, L"rb")) == NULL) return false;
    if (fscanf(file, "%31s", token) != 1 || strcmp(token, "rpg_stage_layout_v1") != 0) { fclose(file); return false; }
    ClearStageForLayout(stage);
    while (fscanf(file, "%31s", token) == 1) {
        int areaId, gridX, gridY;
        if (strcmp(token, "end") == 0) break;
        if (strcmp(token, "area") != 0 || fscanf(file, "%d %d %d", &areaId, &gridX, &gridY) != 3 ||
            areaId < 0 || areaId >= RPG_STAGE_MAP_COUNT || used[areaId]) { fclose(file); return false; }
        for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
            if (stage->mapActive[index] && stage->mapGridX[index] == gridX && stage->mapGridY[index] == gridY) { fclose(file); return false; }
        used[areaId] = true;
        stage->mapActive[areaId] = true;
        stage->mapGridX[areaId] = gridX;
        stage->mapGridY[areaId] = gridY;
    }
    fclose(file);
    return true;
}

static void ExtractAreaData(const RpgStageData *source, int areaId, RpgStageData *area)
{
    int firstColumn = areaId * RPG_STAGE_COLUMNS;
    InitializeStageData(area);
    ClearStageForLayout(&area->stage);
    area->stage.mapActive[areaId] = true;
    area->stage.mapGridX[areaId] = source->stage.mapGridX[areaId];
    area->stage.mapGridY[areaId] = source->stage.mapGridY[areaId];
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int local = 0; local < RPG_STAGE_COLUMNS; local++) {
        int column = firstColumn + local;
        area->stage.blocks[row][column] = source->stage.blocks[row][column];
        memcpy(area->stage.referencePaths[row][column], source->stage.referencePaths[row][column],
               RPG_STAGE_REFERENCE_PATH_LENGTH);
    }
    for (int index = 0; index < source->stage.keyDoorCount; index++) {
        const RpgKeyDoor *door = &source->stage.keyDoors[index];
        if (door->rootColumn / RPG_STAGE_COLUMNS == areaId && area->stage.keyDoorCount < RPG_KEY_DOOR_MAX_COUNT)
            area->stage.keyDoors[area->stage.keyDoorCount++] = *door;
    }
    for (int index = 0; index < source->stage.imageObjects.count; index++) {
        const RpgImageObject *object = &source->stage.imageObjects.entries[index];
        if (object->column / RPG_STAGE_COLUMNS == areaId && area->stage.imageObjects.count < RPG_IMAGE_OBJECT_MAX_COUNT)
            area->stage.imageObjects.entries[area->stage.imageObjects.count++] = *object;
    }
    for (int index = 0; index < area->stage.imageObjects.count; index++)
        if (area->stage.imageObjects.entries[index].id >= area->stage.imageObjects.nextId)
            area->stage.imageObjects.nextId = area->stage.imageObjects.entries[index].id + 1;
    for (int index = 0; index < source->attachments.count; index++)
        if (CellBelongsToArea(source->attachments.entries[index].cell, areaId) && area->attachments.count < RPG_ATTACHMENT_MAX_COUNT)
            area->attachments.entries[area->attachments.count++] = source->attachments.entries[index];
    for (int index = 0; index < source->wires.count; index++) {
        const RpgWire *wire = &source->wires.entries[index];
        if (PathBelongsToArea(&wire->path, areaId) && (!wire->hasReceiverSource || CellBelongsToArea(wire->receiverCell, areaId)) &&
            area->wires.count < RPG_WIRE_MAX_COUNT) area->wires.entries[area->wires.count++] = *wire;
    }
    for (int index = 0; index < source->receivers.count; index++)
        if (CellBelongsToArea(source->receivers.entries[index].cell, areaId) && area->receivers.count < RPG_RECEIVER_MAX_COUNT)
            area->receivers.entries[area->receivers.count++] = source->receivers.entries[index];
    for (int index = 0; index < source->signalBlocks.count; index++)
        if (source->signalBlocks.entries[index].column / RPG_STAGE_COLUMNS == areaId && area->signalBlocks.count < RPG_SIGNAL_BLOCK_MAX_COUNT)
            area->signalBlocks.entries[area->signalBlocks.count++] = source->signalBlocks.entries[index];
    for (int index = 0; index < source->items.count; index++)
        if (PositionBelongsToArea(&source->stage, source->items.entries[index].position, areaId) && area->items.count < RPG_ITEM_MAX_COUNT)
            area->items.entries[area->items.count++] = source->items.entries[index];
    for (int index = 0; index < source->mapEvents.count; index++)
        if (PositionBelongsToArea(&source->stage, source->mapEvents.entries[index].position, areaId) && area->mapEvents.count < RPG_MAP_EVENT_MAX_COUNT)
            area->mapEvents.entries[area->mapEvents.count++] = source->mapEvents.entries[index];
}

static bool SaveAreaData(const char *directory, const RpgStageData *source, int areaId)
{
    char areas[RPG_STAGE_PATH_LENGTH], areaPath[RPG_STAGE_PATH_LENGTH], path[RPG_STAGE_PATH_LENGTH];
    RpgStageData *area;
    bool result;
    if (!GetAreasPath(directory, areas, (int)sizeof(areas)) || !CreateDirectoryPath(areas) ||
        !GetAreaDirectoryPath(directory, areaId, areaPath, (int)sizeof(areaPath)) || !CreateDirectoryPath(areaPath) ||
        (area = (RpgStageData *)calloc(1, sizeof(*area))) == NULL) return false;
    ExtractAreaData(source, areaId, area);
#define SAVE_AREA_FILE(name, function, sourceValue) \
    (GetDirectoryFilePath(areaPath, (name), path, (int)sizeof(path)) && function(path, (sourceValue)))
    result = SAVE_AREA_FILE("rpg_area_stage.cfg", RpgStage_Save, &area->stage) &&
             SAVE_AREA_FILE("rpg_items.cfg", RpgItems_Save, &area->items) &&
             SAVE_AREA_FILE("rpg_wires.cfg", RpgWires_Save, &area->wires) &&
             SAVE_AREA_FILE("rpg_receivers.cfg", RpgReceivers_Save, &area->receivers) &&
             SAVE_AREA_FILE("rpg_attachments.cfg", RpgAttachments_Save, &area->attachments) &&
             SAVE_AREA_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Save, &area->signalBlocks) &&
             SAVE_AREA_FILE("rpg_map_events.cfg", RpgMapEvents_Save, &area->mapEvents) &&
             (GetDirectoryFilePath(areaPath, "rpg_area_entry_event.cfg", path, (int)sizeof(path)) &&
              RpgStage3Event_Save(path, &source->areaEntryEvents.entries[areaId]));
#undef SAVE_AREA_FILE
    free(area);
    return result;
}

static void MergeAreaData(RpgStageData *target, const RpgStageData *area, int areaId)
{
    int firstColumn = areaId * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int local = 0; local < RPG_STAGE_COLUMNS; local++) {
        int column = firstColumn + local;
        target->stage.blocks[row][column] = area->stage.blocks[row][column];
        memcpy(target->stage.referencePaths[row][column], area->stage.referencePaths[row][column], RPG_STAGE_REFERENCE_PATH_LENGTH);
    }
#define APPEND_ALL(targetCollection, sourceCollection, maxCount) do { \
    for (int index = 0; index < (sourceCollection).count && (targetCollection).count < (maxCount); index++) \
        (targetCollection).entries[(targetCollection).count++] = (sourceCollection).entries[index]; \
} while (0)
    APPEND_ALL(target->attachments, area->attachments, RPG_ATTACHMENT_MAX_COUNT);
    APPEND_ALL(target->wires, area->wires, RPG_WIRE_MAX_COUNT);
    APPEND_ALL(target->receivers, area->receivers, RPG_RECEIVER_MAX_COUNT);
    APPEND_ALL(target->signalBlocks, area->signalBlocks, RPG_SIGNAL_BLOCK_MAX_COUNT);
    APPEND_ALL(target->items, area->items, RPG_ITEM_MAX_COUNT);
    APPEND_ALL(target->mapEvents, area->mapEvents, RPG_MAP_EVENT_MAX_COUNT);
    APPEND_ALL(target->stage.imageObjects, area->stage.imageObjects, RPG_IMAGE_OBJECT_MAX_COUNT);
#undef APPEND_ALL
    for (int index = 0; index < area->stage.keyDoorCount && target->stage.keyDoorCount < RPG_KEY_DOOR_MAX_COUNT; index++)
        target->stage.keyDoors[target->stage.keyDoorCount++] = area->stage.keyDoors[index];
    if (target->stage.imageObjects.nextId < area->stage.imageObjects.nextId)
        target->stage.imageObjects.nextId = area->stage.imageObjects.nextId;
}

static bool LoadAreaData(const char *directory, RpgStageData *target, int areaId)
{
    char areaPath[RPG_STAGE_PATH_LENGTH], path[RPG_STAGE_PATH_LENGTH];
    RpgStageData *area;
    bool result = false;
    if (!GetAreaDirectoryPath(directory, areaId, areaPath, (int)sizeof(areaPath)) ||
        (area = (RpgStageData *)calloc(1, sizeof(*area))) == NULL) return false;
    InitializeStageData(area);
    if (!GetDirectoryFilePath(areaPath, "rpg_area_stage.cfg", path, (int)sizeof(path)) || !FileExists(path) ||
        !RpgStage_Load(path, &area->stage)) goto finish;
#define LOAD_AREA_FILE(name, function, targetValue) do { \
    if (GetDirectoryFilePath(areaPath, (name), path, (int)sizeof(path)) && FileExists(path)) function(path, (targetValue)); \
} while (0)
    LOAD_AREA_FILE("rpg_items.cfg", RpgItems_Load, &area->items);
    LOAD_AREA_FILE("rpg_wires.cfg", RpgWires_Load, &area->wires);
    LOAD_AREA_FILE("rpg_receivers.cfg", RpgReceivers_Load, &area->receivers);
    LOAD_AREA_FILE("rpg_attachments.cfg", RpgAttachments_Load, &area->attachments);
    LOAD_AREA_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Load, &area->signalBlocks);
    LOAD_AREA_FILE("rpg_map_events.cfg", RpgMapEvents_Load, &area->mapEvents);
    if (GetDirectoryFilePath(areaPath, "rpg_area_entry_event.cfg", path, (int)sizeof(path)) && FileExists(path))
        (void)RpgStage3Event_Load(path, &target->areaEntryEvents.entries[areaId]);
#undef LOAD_AREA_FILE
    MergeAreaData(target, area, areaId);
    result = true;
finish:
    free(area);
    return result;
}

static void PruneInactiveAreaDirectories(const char *directory, const RpgStage *stage)
{
    char path[RPG_STAGE_PATH_LENGTH];
    if (stage == NULL) return;
    for (int areaId = 0; areaId < RPG_STAGE_MAP_COUNT; areaId++)
        if (!stage->mapActive[areaId] && GetAreaDirectoryPath(directory, areaId, path, (int)sizeof(path)))
            RemoveDirectoryTree(path);
}

static void RemoveLegacyAreaFiles(const char *directory)
{
#ifdef _WIN32
    static const char *names[] = {
        "rpg_stage.cfg", "rpg_area_entry_events.cfg", "rpg_stage3_event.cfg",
        "rpg_items.cfg", "rpg_wires.cfg", "rpg_receivers.cfg", "rpg_attachments.cfg",
        "rpg_signal_blocks.cfg", "rpg_map_events.cfg"
    };
    char path[RPG_STAGE_PATH_LENGTH];
    wchar_t widePath[RPG_STAGE_PATH_LENGTH];
    for (int index = 0; index < (int)(sizeof(names) / sizeof(names[0])); index++)
        if (GetDirectoryFilePath(directory, names[index], path, (int)sizeof(path)) &&
            MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, RPG_STAGE_PATH_LENGTH) > 0)
            (void)DeleteFileW(widePath);
#else
    (void)directory;
#endif
}

static bool SaveStageDataToDirectory(const char *directory, const RpgStageData *data);

static bool LoadStageDataFromDirectory(const char *directory, RpgStageData *data)
{
    char path[RPG_STAGE_PATH_LENGTH];
    bool legacy;
    if (directory == NULL || data == NULL || !GetLayoutPath(directory, path, (int)sizeof(path))) return false;
    legacy = !FileExists(path);
    if (legacy) {
        if (!LoadLegacyStageDataFromDirectory(directory, data)) return false;
        /* Settings becomes canonical immediately. Packaged extraction remains
           read-only and is upgraded on its next editor save/publish. */
        if (RpgStageStorage_GetDomain() == RPG_STAGE_STORAGE_SETTINGS) {
            bool migrated = SaveStageDataToDirectory(directory, data);
            if (migrated) RemoveLegacyAreaFiles(directory);
            return migrated;
        }
        return true;
    }
    InitializeStageData(data);
    if (!LoadStageLayout(directory, &data->stage)) return false;
#define LOAD_GLOBAL_FILE(name, function, target) do { \
    if (GetDirectoryFilePath(directory, (name), path, (int)sizeof(path)) && FileExists(path)) function(path, (target)); \
} while (0)
    LOAD_GLOBAL_FILE("rpg_layout.cfg", RpgLayout_Load, &data->layout);
    LOAD_GLOBAL_FILE("rpg_dialogue.txt", RpgDialogue_Load, &data->dialogue);
    LOAD_GLOBAL_FILE("rpg_stage_entry_event.cfg", RpgStage3Event_Load, &data->stage3Event);
    LOAD_GLOBAL_FILE("rpg_inspect.cfg", RpgInspect_Load, &data->npcInspectData);
#undef LOAD_GLOBAL_FILE
    for (int areaId = 0; areaId < RPG_STAGE_MAP_COUNT; areaId++)
        if (data->stage.mapActive[areaId] && !LoadAreaData(directory, data, areaId)) return false;
    RpgWires_RemoveBroken(&data->wires, &data->stage);
    RpgReceivers_RemoveBroken(&data->receivers, &data->stage);
    RpgAttachments_MigrateLegacyButtons(&data->attachments, &data->stage);
    RpgAttachments_RemoveBroken(&data->attachments, &data->stage);
    RpgSignalBlocks_RemoveBroken(&data->signalBlocks, &data->stage);
    return true;
}

static bool SaveStageDataToDirectory(const char *directory, const RpgStageData *data)
{
    char path[RPG_STAGE_PATH_LENGTH];
    bool result;
    if (directory == NULL || data == NULL || !CreateDirectoryPath(directory)) return false;
#define SAVE_GLOBAL_FILE(name, function, source) \
    (GetDirectoryFilePath(directory, (name), path, (int)sizeof(path)) && function(path, (source)))
    result = SaveStageLayout(directory, &data->stage) &&
             SAVE_GLOBAL_FILE("rpg_layout.cfg", RpgLayout_Save, &data->layout) &&
             SAVE_GLOBAL_FILE("rpg_dialogue.txt", RpgDialogue_Save, &data->dialogue) &&
             SAVE_GLOBAL_FILE("rpg_stage_entry_event.cfg", RpgStage3Event_Save, &data->stage3Event) &&
             SAVE_GLOBAL_FILE("rpg_inspect.cfg", RpgInspect_Save, &data->npcInspectData);
#undef SAVE_GLOBAL_FILE
    for (int areaId = 0; result && areaId < RPG_STAGE_MAP_COUNT; areaId++)
        if (data->stage.mapActive[areaId]) result = SaveAreaData(directory, data, areaId);
    if (result) PruneInactiveAreaDirectories(directory, &data->stage);
    return result;
}

bool RpgStageStorage_SaveRuntimeState(int stageNumber, const RpgStageData *data)
{
    char buildPath[RPG_STAGE_PATH_LENGTH], statePath[RPG_STAGE_PATH_LENGTH];
    return data != NULL && RpgStageStorage_GetRuntimePath(stageNumber, RPG_STAGE_RUNTIME_GAME,
        buildPath, (int)sizeof(buildPath)) && CreateDirectoryPath(buildPath) &&
        snprintf(statePath, sizeof(statePath), "%s\\runtime_state", buildPath) > 0 &&
        SaveStageDataToDirectory(statePath, data);
}

bool RpgStageStorage_LoadRuntimeState(int stageNumber, RpgStageData *data)
{
    char buildPath[RPG_STAGE_PATH_LENGTH], statePath[RPG_STAGE_PATH_LENGTH];
    return data != NULL && RpgStageStorage_GetRuntimePath(stageNumber, RPG_STAGE_RUNTIME_GAME,
        buildPath, (int)sizeof(buildPath)) &&
        snprintf(statePath, sizeof(statePath), "%s\\runtime_state", buildPath) > 0 &&
        LoadStageDataFromDirectory(statePath, data);
}

bool RpgStageStorage_ClearRuntimeState(int stageNumber)
{
    char buildPath[RPG_STAGE_PATH_LENGTH], statePath[RPG_STAGE_PATH_LENGTH];
#ifdef _WIN32
    wchar_t widePath[RPG_STAGE_PATH_LENGTH];
    DWORD attributes;
#endif
    if (!RpgStageStorage_GetRuntimePath(stageNumber, RPG_STAGE_RUNTIME_GAME,
                                        buildPath, (int)sizeof(buildPath)) ||
        snprintf(statePath, sizeof(statePath), "%s\\runtime_state", buildPath) <= 0)
        return false;
#ifdef _WIN32
    if (MultiByteToWideChar(CP_UTF8, 0, statePath, -1, widePath, RPG_STAGE_PATH_LENGTH) <= 0)
        return false;
    attributes = GetFileAttributesW(widePath);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND ||
                                                     GetLastError() == ERROR_PATH_NOT_FOUND;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return DeleteFileW(widePath) != 0;
    RemoveDirectoryTree(statePath);
    return GetFileAttributesW(widePath) == INVALID_FILE_ATTRIBUTES;
#else
    (void)statePath;
    return true;
#endif
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

bool RpgStageStorage_CreateBuildFolder(int stageNumber, const char *name, char *path, int size)
{
    char buildPath[RPG_STAGE_PATH_LENGTH];
    if (path == NULL || size <= 0 || !IsSafeFolderName(name) || !RpgStageStorage_EnsureStageDirectory(stageNumber) ||
        !RpgStageStorage_GetFilePath(stageNumber, "folder_defs", buildPath, (int)sizeof(buildPath)) ||
        !CreateDirectoryPath(buildPath) || snprintf(path, (size_t)size, "%s\\%s", buildPath, name) <= 0) return false;
    return CreateDirectoryPath(path);
}

bool RpgStageStorage_RenameBuildFolder(int stageNumber, const char *oldPath, const char *name,
                                       char *newPath, int newPathSize)
{
#ifdef _WIN32
    char buildPath[RPG_STAGE_PATH_LENGTH], destination[RPG_STAGE_PATH_LENGTH];
    wchar_t oldWide[RPG_STAGE_PATH_LENGTH], newWide[RPG_STAGE_PATH_LENGTH];
    if (oldPath == NULL || oldPath[0] == '\0' || !IsSafeFolderName(name) ||
        !RpgStageStorage_EnsureStageDirectory(stageNumber) ||
        !RpgStageStorage_GetFilePath(stageNumber, "folder_defs", buildPath, (int)sizeof(buildPath)) ||
        !CreateDirectoryPath(buildPath) ||
        snprintf(destination, sizeof(destination), "%s\\%s", buildPath, name) <= 0) return false;
    /* 同名の既存フォルダを誤って上書きしない。作成済みなら元と同一パスだけを許可する。 */
    if (strcmp(oldPath, destination) == 0) {
        if (newPath != NULL && newPathSize > 0) snprintf(newPath, (size_t)newPathSize, "%s", destination);
        return true;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, oldPath, -1, oldWide, RPG_STAGE_PATH_LENGTH) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, destination, -1, newWide, RPG_STAGE_PATH_LENGTH) <= 0) return false;
    /* CreateBuildFolderが作った空フォルダを消してから、MoveFileで名前を変更する。 */
    if (GetFileAttributesW(newWide) != INVALID_FILE_ATTRIBUTES) return false;
    if (MoveFileW(oldWide, newWide) == 0) return false;
    if (newPath != NULL && newPathSize > 0) snprintf(newPath, (size_t)newPathSize, "%s", destination);
    return true;
#else
    (void)stageNumber; (void)oldPath; (void)name; (void)newPath; (void)newPathSize;
    return false;
#endif
}

/* Fileオブジェクトのコピー先はマスごとの専用フォルダに固定する。
   コピーに成功してから旧フォルダを差し替えるため、選択元のファイルには一切変更を加えない。 */
bool RpgStageStorage_CopyReferenceFileToBuild(int stageNumber, int row, int column,
                                              const char *sourcePath, char *copiedPath, int copiedPathSize)
{
#ifdef _WIN32
    char referenceRoot[RPG_STAGE_PATH_LENGTH];
    char targetDirectory[RPG_STAGE_PATH_LENGTH];
    char temporaryPath[RPG_STAGE_PATH_LENGTH];
    char destinationPath[RPG_STAGE_PATH_LENGTH];
    const char *fileName;
    wchar_t wideSource[RPG_STAGE_PATH_LENGTH];
    wchar_t wideTemporary[RPG_STAGE_PATH_LENGTH];
    wchar_t wideDestination[RPG_STAGE_PATH_LENGTH];
    DWORD attributes;

    if (copiedPath == NULL || copiedPathSize <= 0 || sourcePath == NULL || sourcePath[0] == '\0' ||
        row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgStageStorage_EnsureStageDirectory(stageNumber) ||
        !RpgStageStorage_GetFilePath(stageNumber, "reference_files", referenceRoot, (int)sizeof(referenceRoot)) ||
        !CreateDirectoryPath(referenceRoot)) return false;

    if (MultiByteToWideChar(CP_UTF8, 0, sourcePath, -1, wideSource, RPG_STAGE_PATH_LENGTH) <= 0) return false;
    attributes = GetFileAttributesW(wideSource);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;

    fileName = strrchr(sourcePath, '\\');
    if (fileName == NULL) fileName = strrchr(sourcePath, '/');
    fileName = fileName == NULL ? sourcePath : fileName + 1;
    if (fileName[0] == '\0') return false;

    if (snprintf(targetDirectory, sizeof(targetDirectory), "%s\\reference_r%02d_c%03d",
                 referenceRoot, row, column) <= 0 ||
        snprintf(temporaryPath, sizeof(temporaryPath), "%s\\.reference_r%02d_c%03d.tmp",
                 referenceRoot, row, column) <= 0 ||
        snprintf(destinationPath, sizeof(destinationPath), "%s\\%s", targetDirectory, fileName) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, temporaryPath, -1, wideTemporary, RPG_STAGE_PATH_LENGTH) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, destinationPath, -1, wideDestination, RPG_STAGE_PATH_LENGTH) <= 0) return false;

    /* 一時ファイルへのコピーが成功した場合だけ、既存のコピーを置換する。 */
    DeleteFileW(wideTemporary);
    if (CopyFileW(wideSource, wideTemporary, FALSE) == 0) return false;
    RemoveDirectoryTree(targetDirectory);
    if (!CreateDirectoryPath(targetDirectory) || MoveFileExW(wideTemporary, wideDestination,
                                                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        DeleteFileW(wideTemporary);
        return false;
    }
    snprintf(copiedPath, (size_t)copiedPathSize, "%s", destinationPath);
    return true;
#else
    (void)stageNumber; (void)row; (void)column; (void)sourcePath; (void)copiedPath; (void)copiedPathSize;
    return false;
#endif
}

/* Play停止時はマップ状態だけ戻るため、格納済みFileの元コピーが消える場合がある。
   File選択元であるassets/Filesから、不足した実行用コピーだけをPlay開始前に復元する。 */
bool RpgStageStorage_RepairReferenceFileCopies(int stageNumber, RpgStage *stage)
{
#ifdef _WIN32
    char fallbackPath[RPG_STAGE_PATH_LENGTH];
    char repairedPath[RPG_STAGE_PATH_LENGTH];
    if (stage == NULL || stageNumber <= 0) return true;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const char *currentPath;
        const char *fileName;
        wchar_t wideCurrent[RPG_STAGE_PATH_LENGTH];
        DWORD attributes;
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        currentPath = RpgStage_GetReferencePathAtCell(stage, row, column);
        if (currentPath[0] == '\0' ||
            MultiByteToWideChar(CP_UTF8, 0, currentPath, -1, wideCurrent,
                                RPG_STAGE_PATH_LENGTH) <= 0) continue;
        attributes = GetFileAttributesW(wideCurrent);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        fileName = strrchr(currentPath, '\\');
        if (fileName == NULL) fileName = strrchr(currentPath, '/');
        fileName = fileName == NULL ? currentPath : fileName + 1;
        if (fileName[0] == '\0' ||
            snprintf(fallbackPath, sizeof(fallbackPath), "%s../assets/Files/%s",
                     GetApplicationDirectory(), fileName) <= 0 ||
            !RpgStageStorage_CopyReferenceFileToBuild(stageNumber, row, column, fallbackPath,
                                                       repairedPath, (int)sizeof(repairedPath)) ||
            !RpgStage_SetReferencePathAtCell(stage, row, column, repairedPath)) continue;
    }
    return true;
#else
    (void)stageNumber;
    (void)stage;
    return true;
#endif
}

void RpgStageStorage_RemoveReferenceFileCopy(int stageNumber, const char *copiedPath)
{
#ifdef _WIN32
    char buildPath[RPG_STAGE_PATH_LENGTH];
    char referenceRoot[RPG_STAGE_PATH_LENGTH];
    char directory[RPG_STAGE_PATH_LENGTH];
    char *separator;
    size_t rootLength;
    if (copiedPath == NULL || copiedPath[0] == '\0' ||
        !RpgStageStorage_GetFilePath(stageNumber, "reference_files", buildPath, (int)sizeof(buildPath)) ||
        snprintf(referenceRoot, sizeof(referenceRoot), "%s\\reference_files\\reference_r", buildPath) <= 0) return;
    rootLength = strlen(referenceRoot);
    /* 生成済みコピー専用の接頭辞以外には触れず、外部ファイルを削除しない。 */
    if (_strnicmp(copiedPath, referenceRoot, rootLength) != 0) return;
    if (snprintf(directory, sizeof(directory), "%s", copiedPath) <= 0 ||
        (separator = strrchr(directory, '\\')) == NULL ||
        _strnicmp(directory, referenceRoot, rootLength) != 0) return;
    *separator = '\0';
    RemoveDirectoryTree(directory);
#else
    (void)stageNumber; (void)copiedPath;
#endif
}

#if 0 /* Used only by the retired in-place loader below. */
static bool GetLegacyFilePath(const char *fileName, char *path, int size)
{
    char root[RPG_STAGE_PATH_LENGTH];
    return GetStageRootPath(root, (int)sizeof(root)) &&
           snprintf(path, (size_t)size, "%s\\%s", root, fileName) > 0;
}
#endif

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
    return RpgStageStorage_GetDomain() != RPG_STAGE_STORAGE_SETTINGS || RpgStageStorage_PublishCatalog(catalog);
}

#if 0 /* Legacy in-place load is retained below RpgStageStorage_LoadStage only. */
static bool LoadFilePath(int stageNumber, const char *fileName, char *path, int size)
{
    if (RpgStageStorage_GetFilePath(stageNumber, fileName, path, size) && FileExists(path)) return true;
    return stageNumber == 1 && GetLegacyFilePath(fileName, path, size);
}
#endif

/* 実行版は Settings を参照しない。保存済みの参照先をパッケージ内の静的コピーへ差し替える。 */
static void RebasePackagedReferenceFiles(int stageNumber, RpgStage *stage)
{
    char root[RPG_STAGE_PATH_LENGTH];
    if (stage == NULL || RpgStageStorage_GetDomain() != RPG_STAGE_STORAGE_GAME_PACKAGE ||
        !RpgStageStorage_GetFilePath(stageNumber, "reference_files", root, (int)sizeof(root))) return;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const char *source;
        const char *fileName;
        char target[RPG_STAGE_REFERENCE_PATH_LENGTH];
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        source = RpgStage_GetReferencePathAtCell(stage, row, column);
        fileName = strrchr(source, '\\');
        if (fileName == NULL) fileName = strrchr(source, '/');
        fileName = fileName == NULL ? source : fileName + 1;
        if (fileName[0] == '\0') continue;
        if (snprintf(target, sizeof(target), "%s\\reference_r%02d_c%03d\\%s", root, row, column, fileName) > 0)
            RpgStage_SetReferencePathAtCell(stage, row, column, target);
    }
}

bool RpgStageStorage_LoadStage(int stageNumber, RpgStageData *data)
{
    char path[RPG_STAGE_PATH_LENGTH];
    if (data == NULL || stageNumber <= 0) return false;
    if (RpgStageStorage_GetDomain() == RPG_STAGE_STORAGE_GAME_PACKAGE)
        (void)ExtractStaticPackage(stageNumber);
    if (!GetStageDirectoryPath(stageNumber, path, (int)sizeof(path))) return false;
    if (!LoadStageDataFromDirectory(path, data)) return false;
    /* The editor reads Settings as the static source of truth.  Re-publish the
       saved source after a migration (and whenever an older package is opened)
       so the game path cannot retain the retired monolithic format. */
    if (RpgStageStorage_GetDomain() == RPG_STAGE_STORAGE_SETTINGS)
        (void)RpgStageStorage_PublishStage(stageNumber);
    RebasePackagedReferenceFiles(stageNumber, &data->stage);
    return true;
#if 0 /* legacy in-place load retained for reference; the directory loader owns migration. */
    data->layout = RpgLayout_Default();
    RpgStage_Initialize(&data->stage);
    data->dialogue = RpgDialogue_Default();
    data->stage3Event = RpgStage3Event_Default();
    RpgAreaEntryEvents_Initialize(&data->areaEntryEvents);
    data->npcInspectData = RpgInspect_Default("Inspect", "Nothing unusual here.");
    data->items = RpgItems_Default(); data->wires = RpgWires_Default();
    data->receivers = RpgReceivers_Default(); data->attachments = RpgAttachments_Default();
    data->signalBlocks = RpgSignalBlocks_Default(); data->mapEvents = RpgMapEvents_Default();
#define LOAD_STAGE_FILE(name, function, target) do { if (LoadFilePath(stageNumber, (name), path, (int)sizeof(path))) function(path, (target)); } while (0)
    LOAD_STAGE_FILE("rpg_layout.cfg", RpgLayout_Load, &data->layout);
    LOAD_STAGE_FILE("rpg_stage.cfg", RpgStage_Load, &data->stage);
    LOAD_STAGE_FILE("rpg_dialogue.txt", RpgDialogue_Load, &data->dialogue);
    LOAD_STAGE_FILE("rpg_stage_entry_event.cfg", RpgStage3Event_Load, &data->stage3Event);
    bool areaEntryEventsLoaded = false;
    if (LoadFilePath(stageNumber, "rpg_area_entry_events.cfg", path, (int)sizeof(path)))
        areaEntryEventsLoaded = RpgAreaEntryEvents_Load(path, &data->stage, &data->areaEntryEvents);
    /* 旧「Area 3 dialogue」は、初回エリア入場イベントへ一度だけ移行する。 */
    if (!areaEntryEventsLoaded && LoadFilePath(stageNumber, "rpg_stage3_event.cfg", path, (int)sizeof(path))) {
        RpgStage3Event legacyAreaEvent = RpgStage3Event_Default();
        int legacyAreaIndex = RpgStage_GetMapAtGrid(&data->stage, 2, 0);
        if (legacyAreaIndex >= 0 && RpgStage3Event_Load(path, &legacyAreaEvent))
            data->areaEntryEvents.entries[legacyAreaIndex] = legacyAreaEvent;
    }
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
    RebasePackagedReferenceFiles(stageNumber, &data->stage);
    return true;
#endif
}

bool RpgStageStorage_SaveStage(int stageNumber, const RpgStageData *data)
{
    char folder[RPG_STAGE_PATH_LENGTH];
    if (data == NULL || !GetStageDirectoryPath(stageNumber, folder, (int)sizeof(folder)) ||
        !RpgStageStorage_EnsureStageDirectory(stageNumber)) return false;
    bool saved = SaveStageDataToDirectory(folder, data);
    return saved && (RpgStageStorage_GetDomain() != RPG_STAGE_STORAGE_SETTINGS || RpgStageStorage_PublishStage(stageNumber));
#if 0 /* legacy monolithic writer retained only to document the pre-v1 format. */
#define SAVE_STAGE_FILE(name, function, source) \
    (RpgStageStorage_GetFilePath(stageNumber, (name), path, (int)sizeof(path)) && function(path, (source)))
    bool saved = SAVE_STAGE_FILE("rpg_layout.cfg", RpgLayout_Save, &data->layout) &&
           SAVE_STAGE_FILE("rpg_stage.cfg", RpgStage_Save, &data->stage) &&
           SAVE_STAGE_FILE("rpg_dialogue.txt", RpgDialogue_Save, &data->dialogue) &&
           SAVE_STAGE_FILE("rpg_stage_entry_event.cfg", RpgStage3Event_Save, &data->stage3Event) &&
           (RpgStageStorage_GetFilePath(stageNumber, "rpg_area_entry_events.cfg", path, (int)sizeof(path)) &&
            RpgAreaEntryEvents_Save(path, &data->stage, &data->areaEntryEvents)) &&
           SAVE_STAGE_FILE("rpg_inspect.cfg", RpgInspect_Save, &data->npcInspectData) &&
           SAVE_STAGE_FILE("rpg_items.cfg", RpgItems_Save, &data->items) &&
           SAVE_STAGE_FILE("rpg_wires.cfg", RpgWires_Save, &data->wires) &&
           SAVE_STAGE_FILE("rpg_receivers.cfg", RpgReceivers_Save, &data->receivers) &&
           SAVE_STAGE_FILE("rpg_attachments.cfg", RpgAttachments_Save, &data->attachments) &&
           SAVE_STAGE_FILE("rpg_signal_blocks.cfg", RpgSignalBlocks_Save, &data->signalBlocks) &&
           SAVE_STAGE_FILE("rpg_map_events.cfg", RpgMapEvents_Save, &data->mapEvents);
#undef SAVE_STAGE_FILE
    return saved && (RpgStageStorage_GetDomain() != RPG_STAGE_STORAGE_SETTINGS || RpgStageStorage_PublishStage(stageNumber));
#endif
}
