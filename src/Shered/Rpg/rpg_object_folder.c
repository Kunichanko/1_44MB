// 依存する自プロジェクト内ファイル: rpg_object_folder.h
#include "rpg_object_folder.h"
#include "rpg_build_cell_storage.h"
#include "rpg_explorer_launcher.h"
#include "rpg_object_folder_monitor.h"
#include "rpg_stage_storage.h"

// 依存関係: build の通常マス生成は rpg_build_cell_storage の選択方式へ委譲する。

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <limits.h>

#if defined(__GNUC__)
#define RPG_OBJECT_FOLDER_UNUSED __attribute__((unused))
#else
#define RPG_OBJECT_FOLDER_UNUSED
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <shellapi.h>

/* shlobj.h は無効化済みのWin32 UI型を要求するため、変更通知APIだけを局所宣言する。 */
#define RPG_SHCNE_CREATE       ((LONG)0x00000002)
#define RPG_SHCNE_DELETE       ((LONG)0x00000004)
#define RPG_SHCNE_MKDIR        ((LONG)0x00000008)
#define RPG_SHCNE_RMDIR        ((LONG)0x00000010)
#define RPG_SHCNE_UPDATEDIR    ((LONG)0x00001000)
#define RPG_SHCNE_UPDATEITEM   ((LONG)0x00002000)
#define RPG_SHCNE_RENAMEITEM   ((LONG)0x00000001)
#define RPG_SHCNE_RENAMEFOLDER ((LONG)0x00020000)
#define RPG_SHCNF_PATHW        0x0005U
#define RPG_SHCNF_FLUSHNOWAIT  0x3000U
extern void WINAPI SHChangeNotify(LONG eventId, UINT flags, LPCVOID firstItem, LPCVOID secondItem);

static int dataShotFolderSerials[RPG_DATA_SHOT_MAX_COUNT] = { 0 };
static int nextFileDropId = 1;
/* 同じ要求ファイルが削除待ちで残っても、同じアニメーションを毎フレーム再生しない。 */
/* cmd が書き込む一意の内容で要求を識別する。更新時刻だけに依存しないため、短時間の連続実行も取りこぼさない。 */
static char dispatchedZipperRequestToken[256] = { 0 };
static char pendingZipperRequestToken[256] = { 0 };
static bool hasDispatchedZipperRequest = false;
static bool hasPendingZipperRequest = false;
static RpgZipperCommandRequest pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
/* 本編の「ビルドする」で選択した StageN/build。エディターは空のまま従来の一時領域を使う。 */
static char activeBuildPath[1200] = { 0 };
static int activeBuildStageNumber = 0;
/* 現在の Zipper 構造のルート。build/Zipper 固定ではなく、cmd を実行した Folder へ切り替わる。 */
static char activeZipperPath[1200] = { 0 };
static bool activeBuildPersists = false;
/* Editor Play may freely change object-folder contents, but those changes
 * must never become authoring data. */
static char editorPreviewBaselinePath[1200] = { 0 };
static bool editorPreviewBaselineReady = false;
static bool editorPreviewRuntimeTrackingActive = false;
/* 全マス生成中は数千件の Shell 通知をまとめ、ビルド処理が Explorer 更新で詰まらないようにする。 */
static bool isBulkBuildOperation = false;
/* A preview may add one cell per editor frame.  Explorer only needs a
   coalesced directory refresh, not sixty notifications per second. */
static unsigned int generationShellNotifyFrameCount = 0;
/* 描画中はファイルシステムへ触れず、build の変更通知を受けたマスだけ更新する。 */
static bool buildCellLinkedFiles[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
/* An untouched editor Play does not alter its static preview folders.  Keep a
 * narrow runtime flag so Stop need not re-audit every special cell merely to
 * prove that fact.  Game builds retain their own progress semantics. */
static bool editorPreviewStaticMutated = true;
static bool editorPreviewDirtyMaps[RPG_STAGE_MAP_COUNT] = { false };
/* Folder identities use authored area coordinates.  Keep a tiny immutable
   snapshot for the active build instead of leaking storage-slot columns into
   build/ or Zipper paths. */
static bool buildMapActive[RPG_STAGE_MAP_COUNT] = { false };
static int buildMapGridX[RPG_STAGE_MAP_COUNT] = { 0 };
static int buildMapGridY[RPG_STAGE_MAP_COUNT] = { 0 };
/* Multi-cell effects have one persistent object: their root cell.  Keep the
 * owner lookup beside the active build so every filesystem operation resolves
 * a component cell to that same root folder. */
static RpgGridCell buildCellOwners[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
/* Folder identity for multi-cell objects.  Unlike buildCellOwners this never
 * names a component tile: it is the stage-persisted composite object ID. */
static int buildCompositeIds[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { 0 } };
// データ弾ごとに配下を含む変更通知を持ち、毎フレームの再帰走査を避ける。
/* Zipper は毎フレーム走査せず、Windows の変更通知を受けた時だけ容量を再集計する。 */
static int zipperStorageMonitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
static char zipperStorageMonitorPath[1200] = { 0 };
static bool zipperStorageMonitorDirty = true;
static unsigned long long zipperStorageBytes = 0;

static void DiscardEditorPreviewBaseline(void);
static bool CopyTree(const char *source, const char *destination);
static int GetBuildCompositeId(RpgGridCell cell);

bool RpgObjectFolders_BeginEditorPreviewRuntimeTracking(void)
{
    if (activeBuildPersists) return true;
    editorPreviewStaticMutated = false;
    memset(editorPreviewDirtyMaps, 0, sizeof(editorPreviewDirtyMaps));
    /* A full blocks/movables snapshot used to be made here for every Play.
       That makes even an unchanged preview perform a recursive copy before
       its first frame.  Runtime tracking only needs to distinguish a clean
       Play from one that changed the preview's static folder structure. */
    DiscardEditorPreviewBaseline(); /* remove a legacy snapshot, if any */
    editorPreviewRuntimeTrackingActive = true;
    return true;
}

static int FindBuildMapByGridPosition(int gridX, int gridY)
{
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
        if (buildMapActive[index] && buildMapGridX[index] == gridX && buildMapGridY[index] == gridY)
            return index;
    return -1;
}

void RpgObjectFolders_MarkEditorPreviewMapMutation(int mapIndex)
{
    if (activeBuildPersists || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT || !buildMapActive[mapIndex]) return;
    editorPreviewStaticMutated = true;
    /* Record only the actual owner area.  Repair expands this set later only
       when a real composite spans an area boundary. */
    editorPreviewDirtyMaps[mapIndex] = true;
}

static void MarkEditorPreviewPositionMutation(Vector2 position)
{
    const float mapWidth = (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
    const float mapHeight = (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE);
    int gridX = (int)floorf(position.x / mapWidth);
    int gridY = -(int)floorf(position.y / mapHeight);
    int mapIndex = FindBuildMapByGridPosition(gridX, gridY);
    if (mapIndex >= 0) RpgObjectFolders_MarkEditorPreviewMapMutation(mapIndex);
    else RpgObjectFolders_MarkEditorPreviewStaticMutation();
}

static void MarkEditorPreviewPathMutation(const char *path)
{
    const char *area;
    int gridX, gridY, mapIndex;
    if (path == NULL || (area = strstr(path, "area_x")) == NULL ||
        sscanf(area, "area_x%d_y%d_cell_", &gridX, &gridY) != 2) {
        RpgObjectFolders_MarkEditorPreviewStaticMutation();
        return;
    }
    mapIndex = FindBuildMapByGridPosition(gridX, gridY);
    if (mapIndex >= 0) RpgObjectFolders_MarkEditorPreviewMapMutation(mapIndex);
    else RpgObjectFolders_MarkEditorPreviewStaticMutation();
}

bool RpgObjectFolders_HasEditorPreviewStaticMutations(void)
{
    return !activeBuildPersists && editorPreviewStaticMutated;
}

void RpgObjectFolders_MarkEditorPreviewStaticMutation(void)
{
    if (!activeBuildPersists) {
        editorPreviewStaticMutated = true;
        /* This legacy no-location entry point is used only by old callers.
           Keep it safe until they can provide an owning map explicitly. */
        for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
            if (buildMapActive[index]) editorPreviewDirtyMaps[index] = true;
    }
}

void RpgObjectFolders_MarkEditorPreviewCellMutation(RpgGridCell cell, int blockType)
{
    int mapIndex;
    if (activeBuildPersists || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return;
    mapIndex = cell.column / RPG_STAGE_COLUMNS;
    (void)blockType;
    RpgObjectFolders_MarkEditorPreviewMapMutation(mapIndex);
}

static bool ToWide(const char *path, wchar_t *wide, int count)
{ return path != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) > 0; }

/* Shell通知では、実Explorerが監視している絶対パスへ正規化して渡す。 */
static bool ToAbsoluteWide(const char *path, wchar_t *wide, int count)
{
    wchar_t input[1200];
    DWORD length;
    if (!ToWide(path, input, 1200)) return false;
    length = GetFullPathNameW(input, (DWORD)count, wide, NULL);
    return length > 0 && length < (DWORD)count;
}

/* 実Explorerが開いているフォルダの内容を、再入場なしで更新させるためのShell通知。 */
static void NotifyShellChange(LONG eventId, const char *firstPath, const char *secondPath)
{
    wchar_t wideFirst[1200], wideSecond[1200];
    if (isBulkBuildOperation) return;
    if (!ToAbsoluteWide(firstPath, wideFirst, 1200)) return;
    if (secondPath != NULL && !ToAbsoluteWide(secondPath, wideSecond, 1200)) return;
    /* Explorerの更新は通知して委譲する。同期完了待ちはゲームフレームを止めるため行わない。 */
    SHChangeNotify(eventId, RPG_SHCNF_PATHW | RPG_SHCNF_FLUSHNOWAIT,
                   wideFirst, secondPath == NULL ? NULL : wideSecond);
}

static void NotifyShellPathHierarchyChanged(const char *path);

static void NotifyShellParentChanged(const char *path)
{
    const char *separator;
    char parent[1200], name[256];
    size_t length;
    if (path == NULL || (separator = strrchr(path, '\\')) == NULL) return;
    length = (size_t)(separator - path);
    if (length == 0 || length >= sizeof(parent)) return;
    memcpy(parent, path, length);
    parent[length] = '\0';
    NotifyShellPathHierarchyChanged(parent);
}

/* Explorerは表示中の階層ごとに別の一覧を保持する。ファイル操作の完了時は、
   操作対象からZipperルートまでの各一覧を同じ共通経路で同期更新する。 */
static void NotifyShellPathHierarchyChanged(const char *path)
{
    char current[1200];
    if (path == NULL || snprintf(current, sizeof(current), "%s", path) <= 0) return;
    for (;;) {
        const char *separator;
        NotifyShellChange(RPG_SHCNE_UPDATEDIR, current, NULL);
        separator = strrchr(current, '\\');
        if (separator == NULL || separator == current) break;
        current[separator - current] = '\0';
    }
}

// GetApplicationDirectory() を起点にした ".." を解決し、Explorer が表示中の実パスへ通知する。
bool RpgObjectFolder_OpenZipperDirectory(void)
{
    /* 起動先の選択は独立モジュールへ委譲し、ゲーム本編側は従来どおりこの入口だけを呼ぶ。 */
    return RpgExplorerLauncher_OpenZipperDirectory();
}

static bool CreateFolderUtf8(const char *path)
{
    wchar_t wide[1200];
    if (!ToWide(path, wide, 1200)) return false;
    if (CreateDirectoryW(wide, NULL) != 0) {
        NotifyShellChange(RPG_SHCNE_MKDIR, path, NULL);
        NotifyShellParentChanged(path);
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

/* 実行用 StageN の用途別親フォルダ（game / editor）を先に作る。 */
static bool EnsureRuntimeParentFolder(const char *path)
{
    char parent[1200];
    char *separator;
    if (path == NULL || snprintf(parent, sizeof(parent), "%s", path) <= 0 ||
        (separator = strrchr(parent, '\\')) == NULL) return false;
    *separator = '\0';
    return CreateFolderUtf8(parent);
}

static bool FolderExistsUtf8(const char *path)
{
    wchar_t wide[1200];
    DWORD attributes;
    if (!ToWide(path, wide, 1200)) return false;
    attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool RpgObjectFolder_DirectoryExists(const char *path)
{
    return FolderExistsUtf8(path);
}

static void ResetBuildCellOwners(void)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++)
        buildCellOwners[row][column] = (RpgGridCell){ row, column };
    memset(buildCompositeIds, 0, sizeof(buildCompositeIds));
}

static void SetBuildCellOwners(const RpgStage *stage)
{
    ResetBuildCellOwners();
    memset(buildMapActive, 0, sizeof(buildMapActive));
    memset(buildMapGridX, 0, sizeof(buildMapGridX));
    memset(buildMapGridY, 0, sizeof(buildMapGridY));
    if (stage == NULL) return;
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        buildMapActive[mapIndex] = stage->mapActive[mapIndex];
        buildMapGridX[mapIndex] = stage->mapGridX[mapIndex];
        buildMapGridY[mapIndex] = stage->mapGridY[mapIndex];
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(stage->blocks[row][column]);
        if (shape == NULL || shape->rootType != stage->blocks[row][column]) continue;
        for (int index = 0; index < shape->cellCount; index++) {
            const RpgEffectShapeCell *part = &shape->cells[index];
            int partRow = row + part->offsetY;
            int partColumn = column + part->offsetX;
            if (partRow >= 0 && partRow < RPG_STAGE_ROWS && partColumn >= 0 &&
                partColumn < RPG_STAGE_WORLD_COLUMNS &&
                stage->blocks[partRow][partColumn] == part->blockType)
                buildCellOwners[partRow][partColumn] = (RpgGridCell){ row, column };
        }
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++)
        buildCompositeIds[row][column] = RpgStage_GetCompositeObjectIdAtCell(stage, row, column);
}

static RpgGridCell GetBuildCellOwner(RpgGridCell cell)
{
    if (activeBuildPath[0] == '\0' || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return cell;
    return buildCellOwners[cell.row][cell.column];
}

bool RpgObjectFolders_HaveSameBlockOwner(RpgGridCell first, RpgGridCell second)
{
    const int firstComposite = GetBuildCompositeId(first);
    const int secondComposite = GetBuildCompositeId(second);
    if (firstComposite > 0 || secondComposite > 0)
        return firstComposite > 0 && firstComposite == secondComposite;
    return first.row == second.row && first.column == second.column;
}

static int GetBuildCompositeId(RpgGridCell cell)
{
    if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 ||
        cell.column >= RPG_STAGE_WORLD_COLUMNS) return 0;
    return buildCompositeIds[cell.row][cell.column];
}

/* Q で開く実 Zipper フォルダそのものを容量対象にし、Explorer のプロパティと一致させる。 */
static bool GetZipperStorageDirectory(char *path, size_t size)
{
    return RpgExplorerLauncher_GetZipperDirectory(path, size);
}

static void CloseZipperStorageWatcher(void)
{
    RpgObjectFolderMonitor_Disconnect(&zipperStorageMonitorHandle);
    zipperStorageMonitorPath[0] = '\0';
    zipperStorageMonitorDirty = true;
}

static void OnZipperStorageFolderChanged(void *context, const char *directory)
{
    (void)context;
    (void)directory;
    zipperStorageMonitorDirty = true;
}

static unsigned long long MeasureDirectoryBytes(const wchar_t *directory)
{
    WIN32_FIND_DATAW found;
    wchar_t search[1200];
    HANDLE handle;
    unsigned long long total = 0;
    if (directory == NULL || swprintf(search, 1200, L"%ls\\*", directory) < 0) return 0;
    handle = FindFirstFileW(search, &found);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t child[1200];
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* リパースポイントをたどると Zipper 外まで数えるため、実ディレクトリだけを辿る。 */
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                swprintf(child, 1200, L"%ls\\%ls", directory, found.cFileName) >= 0)
                total += MeasureDirectoryBytes(child);
        } else {
            total += ((unsigned long long)found.nFileSizeHigh << 32) | found.nFileSizeLow;
        }
    } while (FindNextFileW(handle, &found) != 0);
    FindClose(handle);
    return total;
}

unsigned long long RpgObjectFolder_GetZipperStorageBytes(void)
{
    char storageDirectory[1200];
    wchar_t widePath[1200];
    if (!GetZipperStorageDirectory(storageDirectory, sizeof(storageDirectory)) ||
        !FolderExistsUtf8(storageDirectory)) {
        CloseZipperStorageWatcher();
        zipperStorageBytes = 0;
        return 0;
    }
    if (strcmp(zipperStorageMonitorPath, storageDirectory) != 0) {
        CloseZipperStorageWatcher();
        if (!ToWide(storageDirectory, widePath, 1200)) return zipperStorageBytes = 0;
        zipperStorageMonitorHandle = RpgObjectFolderMonitor_Connect(
            RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE, storageDirectory, true,
            OnZipperStorageFolderChanged, NULL);
        snprintf(zipperStorageMonitorPath, sizeof(zipperStorageMonitorPath), "%s", storageDirectory);
        zipperStorageBytes = MeasureDirectoryBytes(widePath);
        zipperStorageMonitorDirty = false;
        return zipperStorageBytes;
    }
    RpgObjectFolderMonitor_PollAll();
    if (zipperStorageMonitorDirty) {
        if (!ToWide(storageDirectory, widePath, 1200)) return zipperStorageBytes;
        zipperStorageBytes = MeasureDirectoryBytes(widePath);
        zipperStorageMonitorDirty = false;
    }
    return zipperStorageBytes;
}

// データ弾フォルダの変更を軽量に検出し、変更時だけ内容を再集計する。
static bool GetStagePath(const char *suffix, char *path, size_t size)
{ return snprintf(path, size, "%s../assets/Settings/Stage/%s", GetApplicationDirectory(), suffix) > 0; }
static bool GetObjectsPath(char *path, size_t size)
{
    if (activeBuildPath[0] != '\0') return snprintf(path, size, "%s\\movables", activeBuildPath) > 0;
    return GetStagePath("Movables", path, size);
}
static bool GetCellsPath(char *path, size_t size)
{
    if (activeBuildPath[0] != '\0') return snprintf(path, size, "%s\\blocks", activeBuildPath) > 0;
    return GetStagePath("Blocks", path, size);
}
static bool RPG_OBJECT_FOLDER_UNUSED GetCompactCellMetadataPath(char *path, size_t size)
{
    char blocks[1200];
    return activeBuildPath[0] != '\0' && GetCellsPath(blocks, sizeof(blocks)) &&
           snprintf(path, size, "%s\\cells.csv", blocks) > 0;
}
static bool GetDropsPath(char *path, size_t size)
{
    char movables[1200];
    if (activeBuildPath[0] != '\0' && GetObjectsPath(movables, sizeof(movables)))
        return snprintf(path, size, "%s\\drops", movables) > 0;
    return GetStagePath("Drops", path, size);
}

static bool GetReferenceFilesPath(char *path, size_t size)
{
    char movables[1200];
    return GetObjectsPath(movables, sizeof(movables)) &&
           snprintf(path, size, "%s\\reference_files", movables) > 0;
}

static bool GetReferenceFoldersPath(char *path, size_t size)
{
    char movables[1200];
    return GetObjectsPath(movables, sizeof(movables)) &&
           snprintf(path, size, "%s\\reference_folders", movables) > 0;
}
static bool GetInboxPath(char *path, size_t size)
{
    /* Zipper itself is the storage root.  Keep this private helper name so the
       object-transfer code remains shared, but do not create an Inbox layer. */
    return activeZipperPath[0] != '\0' && snprintf(path, size, "%s", activeZipperPath) > 0;
}

static void MigrateLegacyInboxContents(void);

static bool SetActiveZipperPath(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    if (snprintf(activeZipperPath, sizeof(activeZipperPath), "%s", path) <= 0) return false;
    RpgExplorerLauncher_SetZipperDirectory(activeZipperPath);
    return true;
}

bool RpgObjectFolder_EnsureRuntimeZipperDirectory(void)
{
#ifdef _WIN32
    char foldersPath[1200], zipperPath[1200];
    if (activeBuildPath[0] == '\0' ||
        !GetObjectsPath(foldersPath, sizeof(foldersPath)) ||
        snprintf(zipperPath, sizeof(zipperPath), "%s\\Zipper", foldersPath) <= 0 ||
        !CreateFolderUtf8(foldersPath) || !CreateFolderUtf8(zipperPath) ||
        !SetActiveZipperPath(zipperPath)) return false;

    MigrateLegacyInboxContents();

    /* Never carry command tokens across builds or editor previews. */
    hasDispatchedZipperRequest = false;
    hasPendingZipperRequest = false;
    pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
    dispatchedZipperRequestToken[0] = '\0';
    pendingZipperRequestToken[0] = '\0';
    RpgObjectFolder_PrepareZipperAnimationCommand();
    return true;
#else
    return false;
#endif
}

/* cmd を実行した Folder 自身を Zipper 構造へ昇格する。固定の build/Zipper を複製・置換せず、
   元の内容を保ったまま名前を Zipper にし、以後の Inbox と Explorer が同じ実体を参照する。 */
bool RpgObjectFolder_ActivateReferenceFolderAsZipper(RpgStage *stage, RpgGridCell cell)
{
#ifdef _WIN32
    const char *source;
    const char *separator;
    char destination[1200];
    wchar_t wideSource[1200], wideDestination[1200];
    if (stage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 ||
        cell.column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsReferenceFolder(stage->blocks[cell.row][cell.column])) return false;
    source = RpgStage_GetReferencePathAtCell(stage, cell.row, cell.column);
    separator = strrchr(source, '\\');
    if (source[0] == '\0' || separator == NULL || separator == source) return false;
    if (_stricmp(separator + 1, "Zipper") == 0) {
        if (snprintf(destination, sizeof(destination), "%s", source) <= 0) return false;
    } else if (snprintf(destination, sizeof(destination), "%.*s\\Zipper",
                        (int)(separator - source), source) <= 0 ||
               !ToWide(source, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0]))) ||
               !ToWide(destination, wideDestination, (int)(sizeof(wideDestination) / sizeof(wideDestination[0]))) ||
               GetFileAttributesW(wideDestination) != INVALID_FILE_ATTRIBUTES ||
               MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) == 0) return false;
    if (!SetActiveZipperPath(destination)) return false;
    MigrateLegacyInboxContents();
    if (!RpgStage_SetReferencePathAtCell(stage, cell.row, cell.column, destination)) return false;
    RpgObjectFolder_PrepareZipperAnimationCommand();
    NotifyShellChange(RPG_SHCNE_RENAMEFOLDER, source, destination);
    NotifyShellPathHierarchyChanged(destination);
    return true;
#else
    (void)stage;
    (void)cell;
    return false;
#endif
}

static bool RpgObjectFolders_UsesCompactCellMetadata(int blockType)
{
    /* 地形の基本色と空気だけを一つの一覧へ圧縮し、穴・効果・参照物などは個別フォルダに残す。 */
    return RpgBuildCellStorage_UsesMetadataForBlock(blockType);
}

static bool DataShotName(const RpgDataShot *shot, char *name, size_t size)
{
    return shot != NULL && shot->folderSerial > 0 &&
           snprintf(name, size, "data_shot_%03d_%06d_a%06d", 0, shot->folderSerial, shot->attachmentIndex + 1) > 0;
}

/* PNG配置物はセルフォルダではなく、弾・設置物と同じ非占有オブジェクト名で区別する。 */
static bool ImageObjectName(const RpgImageObject *object, char *name, size_t size)
{
    return object != NULL && object->id > 0 &&
           snprintf(name, size, "image_object_%03d_%06u", (int)object->appearance, object->id) > 0;
}

static bool BuildAreaCellName(RpgGridCell cell, char *name, size_t size)
{
    int areaId;
    int localColumn;
    if (name == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    areaId = cell.column / RPG_STAGE_COLUMNS;
    localColumn = cell.column % RPG_STAGE_COLUMNS;
    if (areaId < 0 || areaId >= RPG_STAGE_MAP_COUNT || !buildMapActive[areaId]) return false;
    return snprintf(name, size, "area_x%02d_y%02d_cell_x%02d_y%02d",
                    buildMapGridX[areaId], buildMapGridY[areaId], localColumn, cell.row) > 0;
}

static bool BlockName(const RpgObjectFolder *folder, int blockType, char *name, size_t size)
{
    if (folder == NULL) return false;
    (void)blockType;
    /* 種類を名前へ埋め込まず、マスごとに同じフォルダ名を保つ。種類は object_info.txt に記録する。 */
    return BuildAreaCellName(folder->cell, name, size);
}

static bool ObjectPath(const char *name, bool inInbox, char *path, size_t size)
{
    char parent[1200];
    if (name == NULL || !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetObjectsPath(parent, sizeof(parent)))) return false;
    return snprintf(path, size, "%s\\%s", parent, name) > 0;
}

static bool BlockPath(const RpgObjectFolder *folder, int blockType, bool inInbox,
                      char *path, size_t size);

/* Attachments do not own folders.  Their metadata sits directly in the
   owning block folder, so capturing that block captures its attachments too. */
static bool AttachmentPath(const RpgAttachment *attachment, bool inInbox, char *path, size_t size)
{
    RpgObjectFolder owner;
    char blockPath[1200];
    if (attachment == NULL) return false;
    if (!RpgAttachments_GetOwnerBlockCell(attachment, &owner.cell)) return false;
    return BlockPath(&owner, 0, inInbox, blockPath, sizeof(blockPath)) &&
           snprintf(path, size, "%s\\attachment_%06d_t%03d.meta", blockPath,
                    attachment->folderId, attachment->type) > 0;
}

static bool AttachmentOwnerBlockPath(const RpgAttachment *attachment, bool inInbox,
                                     char *path, size_t size)
{
    RpgObjectFolder owner;
    if (attachment == NULL) return false;
    if (!RpgAttachments_GetOwnerBlockCell(attachment, &owner.cell)) return false;
    return BlockPath(&owner, 0, inInbox, path, size);
}

/* Legacy receiver-only records are removed on the next metadata refresh.
   A receiver is now an ordinary attachment_<id>_t204.meta record. */
static bool ReceiverPath(const RpgReceiver *receiver, bool inInbox, char *path, size_t size)
{
    RpgObjectFolder owner;
    char blockPath[1200];
    if (receiver == NULL) return false;
    owner.cell = receiver->cell;
    return BlockPath(&owner, 0, inInbox, blockPath, sizeof(blockPath)) &&
           snprintf(path, size, "%s\\receiver_r%03d_c%03d.meta", blockPath,
                    receiver->cell.row, receiver->cell.column) > 0;
}

static bool ReceiverWirePath(const RpgReceiver *receiver, bool inInbox, char *path, size_t size)
{
    RpgObjectFolder owner;
    char blockPath[1200];
    if (receiver == NULL) return false;
    owner.cell = receiver->cell;
    return BlockPath(&owner, 0, inInbox, blockPath, sizeof(blockPath)) &&
           snprintf(path, size, "%s\\wire_receiver_r%03d_c%03d.meta", blockPath,
                    receiver->cell.row, receiver->cell.column) > 0;
}

static bool DataShotPath(const RpgDataShot *shot, bool inInbox, char *path, size_t size)
{
    char name[256];
    return DataShotName(shot, name, sizeof(name)) && ObjectPath(name, inInbox, path, size);
}

static bool ImageObjectPath(const RpgImageObject *object, bool inInbox, char *path, size_t size)
{
    char name[256];
    return ImageObjectName(object, name, sizeof(name)) && ObjectPath(name, inInbox, path, size);
}

// サブディレクトリを含む変更通知。通知を受けたデータ弾だけが再帰集計を実行する。
static bool CompositePath(int compositeId, bool inInbox, char *path, size_t size)
{
    char parent[1200];
    if (compositeId <= 0 ||
        !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetObjectsPath(parent, sizeof(parent)))) return false;
    return snprintf(path, size, "%s\\object_%08d", parent, compositeId) > 0;
}

static bool BlockPath(const RpgObjectFolder *folder, int blockType, bool inInbox, char *path, size_t size)
{
    char name[256], parent[1200];
    RpgObjectFolder owner;
    if (folder == NULL) return false;
    const int compositeId = GetBuildCompositeId(folder->cell);
    /* A group directory, not a component-cell folder, is the only movable
       owner of a multi-cell object. */
    if (compositeId > 0) return CompositePath(compositeId, inInbox, path, size);
    owner.cell = folder->cell;
    if (!BlockName(&owner, blockType, name, sizeof(name)) ||
        !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetCellsPath(parent, sizeof(parent)))) return false;
    return snprintf(path, size, "%s\\%s", parent, name) > 0;
}

/* Build creation must first materialize every grid cell in the same direct
 * form.  Multi-cell ownership is applied afterwards by moving only component
 * folders below their representative. */
static bool BlockPathDirect(const RpgObjectFolder *folder, int blockType, bool inInbox,
                            char *path, size_t size)
{
    char name[256], parent[1200];
    if (!BlockName(folder, blockType, name, sizeof(name)) ||
        !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetCellsPath(parent, sizeof(parent)))) return false;
    return snprintf(path, size, "%s\\%s", parent, name) > 0;
}

static bool DynamicBlockPath(RpgGridCell identityCell, int blockType, bool inInbox,
                             char *path, size_t size)
{
    char parent[1200], name[256];
    if (identityCell.row < 0 || identityCell.row >= RPG_STAGE_ROWS ||
        identityCell.column < 0 || identityCell.column >= RPG_STAGE_WORLD_COLUMNS ||
        !BuildAreaCellName(identityCell, name, sizeof(name)) ||
        !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetObjectsPath(parent, sizeof(parent)))) return false;
    (void)blockType;
    return snprintf(path, size, "%s\\%s", parent, name) > 0;
}

bool RpgObjectFolder_GetBlockDirectory(const RpgObjectFolder *folder, int blockType,
                                       char *path, size_t pathSize)
{
    if (!BlockPath(folder, blockType, false, path, pathSize)) return false;
    return CreateFolderUtf8(path);
}

bool RpgObjectFolder_GetBlockDirectoryPath(const RpgObjectFolder *folder, int blockType,
                                           char *path, size_t pathSize)
{
    return BlockPath(folder, blockType, false, path, pathSize);
}

static void RemoveTree(const char *directory)
{
    char search[1200];
    wchar_t wideSearch[1200], wideChild[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 || !ToWide(search, wideSearch, 1200)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            char name[1024], child[1200];
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
                WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
                snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) RemoveTree(child);
            else if (ToWide(child, wideChild, 1200)) DeleteFileW(wideChild);
        } while (FindNextFileW(handle, &data) != 0);
        FindClose(handle);
    }
    if (ToWide(directory, wideChild, 1200) && RemoveDirectoryW(wideChild) != 0) {
        NotifyShellChange(RPG_SHCNE_RMDIR, directory, NULL);
        NotifyShellParentChanged(directory);
    }
}

static void DiscardEditorPreviewBaseline(void)
{
    if (editorPreviewBaselinePath[0] != '\0') RemoveTree(editorPreviewBaselinePath);
    editorPreviewBaselinePath[0] = '\0';
    editorPreviewBaselineReady = false;
}

/* 実Explorerが開いているZipper/Inboxの実体を消すと、Shellビューが古いフォルダを保持して
   以後の移動通知を受け取れなくなる。セッション掃除ではルートを保持して内容だけを消す。 */
static void RPG_OBJECT_FOLDER_UNUSED ClearFolderContents(const char *directory)
{
    char search[1200];
    wchar_t wideSearch[1200], wideChild[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL) return;
    if (!FolderExistsUtf8(directory)) {
        (void)CreateFolderUtf8(directory);
        return;
    }
    if (snprintf(search, sizeof(search), "%s\\*", directory) <= 0 || !ToWide(search, wideSearch, 1200)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[1024], child[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            RemoveTree(child);
        } else if (ToWide(child, wideChild, 1200) && DeleteFileW(wideChild) != 0) {
            NotifyShellChange(RPG_SHCNE_DELETE, child, NULL);
        }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, directory, NULL);
}

static bool MoveDirectory(const char *source, const char *destination)
{
    wchar_t wideSource[1200], wideDestination[1200];
    bool moved;
    if (!FolderExistsUtf8(source) || !ToWide(source, wideSource, 1200) || !ToWide(destination, wideDestination, 1200)) return false;
    RemoveTree(destination);
    moved = MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) != 0;
    if (moved) {
        /* Rename通知だけでは表示中の移動先一覧が更新されないExplorerがあるため、
           移動先を新規フォルダとしても通知し、親・対象ディレクトリを同期更新する。 */
        NotifyShellChange(RPG_SHCNE_RENAMEFOLDER, source, destination);
        NotifyShellChange(RPG_SHCNE_DELETE, source, NULL);
        NotifyShellChange(RPG_SHCNE_MKDIR, destination, NULL);
        NotifyShellPathHierarchyChanged(source);
        NotifyShellPathHierarchyChanged(destination);
        /* Our own move is known synchronously.  Do not make runtime objects
           wait for ReadDirectoryChangesW before they can refresh state. */
        RpgObjectFolderMonitor_NotifyLocalChange(source);
        RpgObjectFolderMonitor_NotifyLocalChange(destination);
    }
    return moved;
}

/* Older builds stored captured objects under Zipper\\Inbox.  Flatten that
   generated container once without discarding captured objects on upgrade. */
static void MigrateLegacyInboxContents(void)
{
    char legacy[1200], search[1200];
    wchar_t wideSearch[1200], wideLegacy[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (activeZipperPath[0] == '\0' ||
        snprintf(legacy, sizeof(legacy), "%s\\Inbox", activeZipperPath) <= 0 ||
        !FolderExistsUtf8(legacy) ||
        snprintf(search, sizeof(search), "%s\\*", legacy) <= 0 ||
        !ToWide(search, wideSearch, (int)(sizeof(wideSearch) / sizeof(wideSearch[0])))) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            char name[1024], source[1200], destination[1200];
            wchar_t wideSource[1200], wideDestination[1200];
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
                WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
                snprintf(source, sizeof(source), "%s\\%s", legacy, name) <= 0) continue;
            /* Regenerate these obsolete command files as eat.* below. */
            if (_stricmp(name, "assyuku.cmd") == 0 || _stricmp(name, "assyuku.vbs") == 0 ||
                _stricmp(name, "assyuku.request") == 0) {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) RemoveTree(source);
                else if (ToWide(source, wideSource, 1200)) DeleteFileW(wideSource);
                continue;
            }
            if (snprintf(destination, sizeof(destination), "%s\\%s", activeZipperPath, name) <= 0 ||
                !ToWide(source, wideSource, 1200) || !ToWide(destination, wideDestination, 1200)) continue;
            if (GetFileAttributesW(wideDestination) == INVALID_FILE_ATTRIBUTES &&
                MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) != 0) {
                NotifyShellChange((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ?
                                  RPG_SHCNE_RENAMEFOLDER : RPG_SHCNE_RENAMEITEM, source, destination);
            } else if (GetFileAttributesW(wideDestination) != INVALID_FILE_ATTRIBUTES) {
                /* A prior partial migration already placed this generated item
                   in the root; discard only its duplicate Inbox copy. */
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) RemoveTree(source);
                else (void)DeleteFileW(wideSource);
            }
        } while (FindNextFileW(handle, &data) != 0);
        FindClose(handle);
    }
    if (ToWide(legacy, wideLegacy, 1200)) (void)RemoveDirectoryW(wideLegacy);
    NotifyShellPathHierarchyChanged(activeZipperPath);
}

/* 返却演出中だけ、対象フォルダを build の一つ上の StageN で待機させる。
   フォルダ名は一意なので、演出中も別オブジェクトのフォルダと衝突しない。 */
static bool GetBuildParentPath(char *path, size_t size)
{
    const char *separator;
    size_t length;
    if (activeBuildPath[0] == '\0' || path == NULL || size == 0 ||
        (separator = strrchr(activeBuildPath, '\\')) == NULL) return false;
    length = (size_t)(separator - activeBuildPath);
    if (length == 0 || length >= size) return false;
    memcpy(path, activeBuildPath, length);
    path[length] = '\0';
    return true;
}

static bool GetReturnStagingPath(const char *buildObjectPath, char *path, size_t size)
{
    char parent[1200];
    const char *name;
    if (buildObjectPath == NULL || (name = strrchr(buildObjectPath, '\\')) == NULL || name[1] == '\0' ||
        !GetBuildParentPath(parent, sizeof(parent))) return false;
    return snprintf(path, size, "%s\\%s", parent, name + 1) > 0;
}

static bool BeginFolderReturn(const char *inboxPath, const char *buildObjectPath)
{
    char staging[1200];
    if (!FolderExistsUtf8(inboxPath) || !GetReturnStagingPath(buildObjectPath, staging, sizeof(staging))) return false;
    return MoveDirectory(inboxPath, staging);
}

static bool CompleteFolderReturn(const char *inboxPath, const char *buildObjectPath)
{
    char staging[1200];
    if (!GetReturnStagingPath(buildObjectPath, staging, sizeof(staging))) return false;
    if (FolderExistsUtf8(staging)) return MoveDirectory(staging, buildObjectPath);
    /* 旧セッションで演出待機がなかった場合も、Inbox から安全に復帰できる。 */
    return !FolderExistsUtf8(inboxPath) || MoveDirectory(inboxPath, buildObjectPath);
}

// 所有元のフォルダ内容を弾の parent へ固定時点のスナップショットとして複製する。
static bool CopyTree(const char *source, const char *destination)
{
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (!FolderExistsUtf8(source) || !CreateFolderUtf8(destination) ||
        snprintf(search, sizeof(search), "%s\\*", source) <= 0 || !ToWide(search, wideSearch, 1200)) return false;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool succeeded = true;
    do {
        char name[1024], childSource[1200], childDestination[1200];
        wchar_t wideSource[1200], wideDestination[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        if (WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(childSource, sizeof(childSource), "%s\\%s", source, name) <= 0 ||
            snprintf(childDestination, sizeof(childDestination), "%s\\%s", destination, name) <= 0) {
            succeeded = false; continue;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!CopyTree(childSource, childDestination)) succeeded = false;
        } else if (!ToWide(childSource, wideSource, 1200) || !ToWide(childDestination, wideDestination, 1200) ||
                   CopyFileW(wideSource, wideDestination, FALSE) == 0) succeeded = false;
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    return succeeded;
}

static bool HasExternalFiles(const char *directory)
{
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (!FolderExistsUtf8(directory) || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, 1200)) return false;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        char name[1024], child[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            wcscmp(data.cFileName, L"object_info.txt") == 0 ||
            wcscmp(data.cFileName, L"reference_info.txt") == 0 ||
            wcscmp(data.cFileName, L"data_shot_info.txt") == 0 ||
            wcscmp(data.cFileName, L"image_info.txt") == 0 ||
            wcscmp(data.cFileName, L"zipper.request") == 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) > 0 &&
            snprintf(child, sizeof(child), "%s\\%s", directory, name) > 0) {
            if (HasExternalFiles(child)) { FindClose(handle); return true; }
        } else { FindClose(handle); return true; }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    return false;
}

static bool DirectoryContainsFileNamed(const char *directory, const char *fileName)
{
#ifdef _WIN32
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL || fileName == NULL || fileName[0] == '\0' ||
        !FolderExistsUtf8(directory) || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, 1200)) return false;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        char name[1024], child[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0)
            continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (snprintf(child, sizeof(child), "%s\\%s", directory, name) > 0 &&
                DirectoryContainsFileNamed(child, fileName)) {
                FindClose(handle);
                return true;
            }
        } else if (strcmp(name, fileName) == 0) {
            FindClose(handle);
            return true;
        }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
#else
    (void)directory;
    (void)fileName;
#endif
    return false;
}

bool RpgObjectFolder_DirectoryContainsFileNamed(const char *directory, const char *fileName)
{
    return DirectoryContainsFileNamed(directory, fileName);
}

/* Every build object has this deliberately small, common base record.  Its
   folder location identifies the object class; grid position, IDs and area
   are derived instead of being serialized redundantly here. */
static bool WriteObjectInfo(const char *folder, int type, Vector2 position)
{
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    if (!CreateFolderUtf8(folder) || snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "type=%d\nx=%.1f\ny=%.1f\n", type, position.x, position.y);
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, infoPath, NULL);
    NotifyShellParentChanged(infoPath);
    return true;
}

/* Composite grouping runs when a preview is resumed as well as when it is
   first created.  Do not turn that idempotent check into write traffic: the
   existing base record is authoritative when it already names the expected
   type and centre. */
static bool ObjectInfoMatches(const char *folder, int expectedType, Vector2 expectedPosition)
{
    char infoPath[1200], line[128];
    wchar_t wideInfoPath[1200];
    FILE *file;
    int type = INT_MIN;
    float x = NAN, y = NAN;
    if (folder == NULL || snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200) || (file = _wfopen(wideInfoPath, L"rb")) == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        (void)sscanf(line, "type=%d", &type);
        (void)sscanf(line, "x=%f", &x);
        (void)sscanf(line, "y=%f", &y);
    }
    fclose(file);
    return type == expectedType && fabsf(x - expectedPosition.x) < 0.01f &&
           fabsf(y - expectedPosition.y) < 0.01f;
}

static bool CompositeInfoMatches(const char *path, int objectId,
                                 const RpgGridCell *cells, int count)
{
    char line[128], expected[128];
    wchar_t widePath[1200];
    FILE *file;
    if (path == NULL || cells == NULL || count <= 0 || !ToWide(path, widePath, 1200) ||
        (file = _wfopen(widePath, L"rb")) == NULL) return false;
    snprintf(expected, sizeof(expected), "id=%d\n", objectId);
    if (fgets(line, sizeof(line), file) == NULL || strcmp(line, expected) != 0) {
        fclose(file);
        return false;
    }
    snprintf(expected, sizeof(expected), "component_count=%d\n", count);
    if (fgets(line, sizeof(line), file) == NULL || strcmp(line, expected) != 0) {
        fclose(file);
        return false;
    }
    for (int index = 0; index < count; index++) {
        snprintf(expected, sizeof(expected), "component=%d,%d\n", cells[index].row, cells[index].column);
        if (fgets(line, sizeof(line), file) == NULL || strcmp(line, expected) != 0) {
            fclose(file);
            return false;
        }
    }
    /* An old record with stale trailing components is not a valid group. */
    const bool hasNoTrailingData = fgets(line, sizeof(line), file) == NULL;
    fclose(file);
    return hasNoTrailingData;
}

static bool WriteCompositeInfo(const char *path, int objectId,
                               const RpgGridCell *cells, int count)
{
    wchar_t widePath[1200];
    FILE *file;
    if (path == NULL || cells == NULL || count <= 0 || !ToWide(path, widePath, 1200) ||
        (file = _wfopen(widePath, L"wb")) == NULL) return false;
    fprintf(file, "id=%d\ncomponent_count=%d\n", objectId, count);
    for (int index = 0; index < count; index++)
        fprintf(file, "component=%d,%d\n", cells[index].row, cells[index].column);
    return fclose(file) == 0;
}

/* Folderを外部CMDから操作しても、どのステージ・マスの配置物か再現できるように記録する。 */
static bool WriteReferenceFolderInfo(const char *folder, RpgGridCell cell)
{
#ifdef _WIN32
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    Vector2 position = { (cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                         (cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    /* A legacy Folder record can contain only a display name such as
       "Folder_9".  CreateDirectoryW resolves that against the executable's
       current directory (build/), which must never become an object store.
       All current callers resolve movable folders to an absolute Stage runtime
       path first; reject anything else as a final safety net. */
    if (folder == NULL || folder[0] == '\0' ||
        !((folder[0] != '\0' && folder[1] == ':') ||
          (folder[0] == '\\' && folder[1] == '\\')) ||
        !WriteObjectInfo(folder,
        RPG_BLOCK_REFERENCE_FOLDER, position) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\reference_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, (int)(sizeof(wideInfoPath) / sizeof(wideInfoPath[0])))) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    /* Stage and cell are derived from this folder's active build and x/y. */
    fprintf(file, "zipper_command=zipper.cmd\n");
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, infoPath, NULL);
    NotifyShellParentChanged(infoPath);
    return true;
#else
    (void)folder; (void)cell;
    return false;
#endif
}

/* 保存方式はパスを知らず、ここから渡す最小のファイル操作だけを利用する。 */
static bool WriteBuildCellFolder(void *context, RpgGridCell cell, int blockType)
{
    RpgObjectFolder folder = { .cell = cell };
    char cellPath[1200];
    Vector2 position = { (cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                         (cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    (void)context;
    /* File/Folder は地形セルではない。専用の外部オブジェクト構成だけに保存し、cells には作らない。 */
    if (RpgBlockInventory_IsReferenceObject(blockType)) return true;
    return BlockPathDirect(&folder, blockType, false, cellPath, sizeof(cellPath)) &&
           WriteObjectInfo(cellPath, blockType, position);
}

/* `requestedMaps == NULL` means every active map.  The targeted form is used
   by editor-preview repair so a composite spanning several dirty areas is
   collected once, rather than once for every area it touches. */
static bool CollectEffectComponentFoldersForMaps(const RpgStage *stage,
                                                 const bool requestedMaps[RPG_STAGE_MAP_COUNT])
{
    int collected[RPG_COMPOSITE_OBJECT_MAX_COUNT + RPG_MAGNET_OBJECT_MAX_COUNT] = { 0 };
    int collectedCount = 0;
    if (stage == NULL) return false;
    /* Every composite owns one ID directory.  The direct cell folders are
       created first, then all of them -- including the placement-origin cell
       -- are moved beneath that directory. */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgGridCell cells[RPG_STAGE_ROWS * RPG_STAGE_COLUMNS];
        int count = 0;
        const int objectId = RpgStage_GetCompositeObjectIdAtCell(stage, row, column);
        bool alreadyCollected = false;
        for (int seen = 0; seen < collectedCount; seen++)
            if (collected[seen] == objectId) { alreadyCollected = true; break; }
        if (objectId <= 0 || alreadyCollected || !RpgStage_GetCompositeCellsAtCell(stage, row, column, cells,
                RPG_STAGE_ROWS * RPG_STAGE_COLUMNS, &count)) continue;
        if (collectedCount >= (int)(sizeof(collected) / sizeof(collected[0]))) return false;
        collected[collectedCount++] = objectId;
        bool touchesRequestedMap = requestedMaps == NULL;
        for (int index = 0; index < count; index++)
            if (requestedMaps != NULL &&
                requestedMaps[cells[index].column / RPG_STAGE_COLUMNS])
                touchesRequestedMap = true;
        if (!touchesRequestedMap) continue;
        char groupPath[1200], componentsPath[1200], infoPath[1200];
        if (!CompositePath(objectId, false, groupPath, sizeof(groupPath)) ||
            snprintf(componentsPath, sizeof(componentsPath), "%s\\components", groupPath) <= 0 ||
            snprintf(infoPath, sizeof(infoPath), "%s\\composite_info.txt", groupPath) <= 0)
            return false;
        if ((!FolderExistsUtf8(groupPath) && !CreateFolderUtf8(groupPath)) ||
            (!FolderExistsUtf8(componentsPath) && !CreateFolderUtf8(componentsPath))) return false;
        if (!CompositeInfoMatches(infoPath, objectId, cells, count) &&
            !WriteCompositeInfo(infoPath, objectId, cells, count)) return false;
        for (int index = 0; index < count; index++) {
            RpgObjectFolder component = { .cell = cells[index] };
            int componentType = stage->blocks[cells[index].row][cells[index].column];
            char source[1200], name[256], destination[1200];
            if (componentType == RPG_BLOCK_BUILD_MISSING)
                componentType = stage->missingBlockTypes[cells[index].row][cells[index].column];
            if (!BlockPathDirect(&component, componentType, false, source, sizeof(source)) ||
                !BlockName(&component, componentType, name, sizeof(name)) ||
                snprintf(destination, sizeof(destination), "%s\\%s", componentsPath, name) <= 0)
                return false;
            if (!FolderExistsUtf8(destination) && FolderExistsUtf8(source) &&
                !MoveDirectory(source, destination)) return false;
            if (!FolderExistsUtf8(destination)) continue; /* incremental preview: try next pass */
            const Vector2 componentPosition = { (cells[index].column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                                (cells[index].row + 0.5f) * RPG_STAGE_TILE_SIZE };
            if (!ObjectInfoMatches(destination, componentType, componentPosition) &&
                !WriteObjectInfo(destination, componentType, componentPosition)) return false;
        }
        /* object_info belongs to the group, never to a particular component. */
        const Vector2 groupPosition = { (column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                        (row + 0.5f) * RPG_STAGE_TILE_SIZE };
        if (!ObjectInfoMatches(groupPath, 0, groupPosition) &&
            !WriteObjectInfo(groupPath, 0, groupPosition)) return false;
    }
    return true;
}

static bool CollectEffectComponentFolders(const RpgStage *stage, int onlyMapIndex)
{
    bool requestedMaps[RPG_STAGE_MAP_COUNT] = { false };
    if (onlyMapIndex < 0) return CollectEffectComponentFoldersForMaps(stage, NULL);
    if (onlyMapIndex >= RPG_STAGE_MAP_COUNT) return false;
    requestedMaps[onlyMapIndex] = true;
    return CollectEffectComponentFoldersForMaps(stage, requestedMaps);
}

static bool GetBuildCellFilePath(void *context, const char *fileName, char *path, size_t pathSize)
{
    char blocks[1200];
    (void)context;
    return activeBuildPath[0] != '\0' && fileName != NULL &&
           GetCellsPath(blocks, sizeof(blocks)) &&
           snprintf(path, pathSize, "%s\\%s", blocks, fileName) > 0;
}

static void NotifyBuildCellFileChanged(void *context, const char *path)
{
    (void)context;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, path, NULL);
    NotifyShellParentChanged(path);
}

static RpgBuildCellStorageBackend GetBuildCellStorageBackend(void)
{
    return (RpgBuildCellStorageBackend){ .context = NULL, .writeCellFolder = WriteBuildCellFolder,
                                         .getBuildFilePath = GetBuildCellFilePath,
                                         .notifyBuildFileChanged = NotifyBuildCellFileChanged };
}

static bool OpenWideFile(const char *path, const wchar_t *mode, FILE **file)
{
    wchar_t widePath[1200];
    if (file == NULL || !ToWide(path, widePath, 1200)) return false;
    *file = _wfopen(widePath, mode);
    return *file != NULL;
}

static bool RPG_OBJECT_FOLDER_UNUSED WriteCompactCellMetadata(RpgStage *stage)
{
    char metadataPath[1200];
    FILE *file;
    if (stage == NULL || !GetCompactCellMetadataPath(metadataPath, sizeof(metadataPath)) ||
        !OpenWideFile(metadataPath, L"wb", &file)) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        int blockType = stage->blocks[row][column];
        if (RpgObjectFolders_UsesCompactCellMetadata(blockType))
            fprintf(file, "%d %d %d\n", row, column, blockType);
    }
    /* File の静的コピーも実行ディレクトリへ展開する。本編の開始後に static は参照しない。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const char *source;
        const char *name;
        char folderPath[1200], targetPath[1200], referenceRoot[1200];
        wchar_t wideSource[1200], wideTarget[1200];
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        source = RpgStage_GetReferencePathAtCell(stage, row, column);
        name = strrchr(source, '\\');
        if (name == NULL) name = strrchr(source, '/');
        name = name == NULL ? source : name + 1;
        if (name[0] == '\0' || !GetReferenceFilesPath(referenceRoot, sizeof(referenceRoot)) ||
            snprintf(folderPath, sizeof(folderPath), "%s\\reference_r%02d_c%03d", referenceRoot, row, column) <= 0 ||
            snprintf(targetPath, sizeof(targetPath), "%s\\%s", folderPath, name) <= 0 ||
            !CreateFolderUtf8(referenceRoot) ||
            !CreateFolderUtf8(folderPath) || !ToWide(source, wideSource, 1200) || !ToWide(targetPath, wideTarget, 1200) ||
            CopyFileW(wideSource, wideTarget, FALSE) == 0 || !RpgStage_SetReferencePathAtCell(stage, row, column, targetPath)) {
            RpgObjectFolders_EndStageBuild(); isBulkBuildOperation = false; return false;
        }
    }
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, metadataPath, NULL);
    NotifyShellParentChanged(metadataPath);
    return true;
}

/* static パッケージを消さずに、プレイごとに生成する成果物だけを初期化する。 */
static bool ClearStageRuntimeArtifacts(const char *buildPath)
{
#ifdef _WIN32
    static const char *folders[] = { "blocks", "movables", "cells", "objects", "drops", "Zipper", "folders", "reference_files", "runtime_state" };
    char path[1200];
    wchar_t widePath[1200];
    if (buildPath == NULL || buildPath[0] == '\0') return false;
    for (int index = 0; index < (int)(sizeof(folders) / sizeof(folders[0])); index++) {
        if (snprintf(path, sizeof(path), "%s\\%s", buildPath, folders[index]) <= 0) return false;
        RemoveTree(path);
        if (FolderExistsUtf8(path)) return false;
    }
    if (snprintf(path, sizeof(path), "%s\\blocks\\cells.csv", buildPath) <= 0 || !ToWide(path, widePath, 1200)) return false;
    /* blocks may already have been removed above.  Both a missing file and a
       missing parent mean the old compact metadata is cleanly absent. */
    if (DeleteFileW(widePath) == 0 && GetLastError() != ERROR_FILE_NOT_FOUND &&
        GetLastError() != ERROR_PATH_NOT_FOUND) return false;
    if (snprintf(path, sizeof(path), "%s\\cells.csv", buildPath) <= 0 || !ToWide(path, widePath, 1200)) return false;
    if (DeleteFileW(widePath) == 0 && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    return true;
#else
    (void)buildPath;
    return false;
#endif
}

/* Recreate one movable FILE's current Play copy. sourcePath is static editor
   data; object->path is strictly a disposable location under activeBuildPath.
   In particular, a legacy/static path must never be accepted as a live copy:
   moving it into a key door would consume the user's selected source. */
/* Old stages stored selected-file copies below movables/reference_files using
   cell coordinates.  Look there only as a compatibility fallback; current
   files use reference_sources/reference_<object id>. */
static bool FindLegacyReferenceSource(const char *root, const char *name, char *path, size_t pathSize)
{
#ifdef _WIN32
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW found;
    HANDLE handle;
    if (root == NULL || name == NULL || name[0] == '\0' || path == NULL || pathSize == 0 ||
        snprintf(search, sizeof(search), "%s\\*", root) <= 0 || !ToWide(search, wideSearch, 1200)) return false;
    handle = FindFirstFileW(wideSearch, &found);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        char child[1200], candidate[1200];
        wchar_t wideCandidate[1200];
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, child, (int)sizeof(child), NULL, NULL) <= 0 ||
            snprintf(candidate, sizeof(candidate), "%s\\%s\\%s", root, child, name) <= 0 ||
            !ToWide(candidate, wideCandidate, 1200)) continue;
        if (GetFileAttributesW(wideCandidate) != INVALID_FILE_ATTRIBUTES) {
            snprintf(path, pathSize, "%s", candidate);
            FindClose(handle);
            return true;
        }
    } while (FindNextFileW(handle, &found) != 0);
    FindClose(handle);
#else
    (void)root; (void)name; (void)path; (void)pathSize;
#endif
    return false;
}

static bool FindStaticReferenceSource(int objectId, const char *name, char *path, size_t pathSize)
{
#ifdef _WIN32
    char relativeSnapshot[1200], snapshot[1200], legacyRoot[1200];
    wchar_t wideSnapshot[1200];
    if (objectId <= 0 || name == NULL || name[0] == '\0' || path == NULL || pathSize == 0 ||
        activeBuildStageNumber <= 0 ||
        snprintf(relativeSnapshot, sizeof(relativeSnapshot),
                 "movables\\reference_sources\\reference_%04d\\%s", objectId, name) <= 0) return false;
    if (RpgStageStorage_GetFilePath(activeBuildStageNumber, relativeSnapshot, snapshot, (int)sizeof(snapshot)) &&
        ToWide(snapshot, wideSnapshot, 1200) && GetFileAttributesW(wideSnapshot) != INVALID_FILE_ATTRIBUTES) {
        snprintf(path, pathSize, "%s", snapshot);
        return true;
    }
    return RpgStageStorage_GetFilePath(activeBuildStageNumber, "movables\\reference_files", legacyRoot,
                                       (int)sizeof(legacyRoot)) &&
           FindLegacyReferenceSource(legacyRoot, name, path, pathSize);
#else
    (void)objectId; (void)name; (void)path; (void)pathSize;
    return false;
#endif
}

/* A reference-source folder is owned by one movable object ID and stores one
 * authored file.  Do not use a stale runtime/display filename to decide that
 * the source is unavailable: older runtime records can contain File.txt even
 * though the static object identity is intact. */
static bool FindStaticReferenceSourceByObjectId(int objectId, char *path, size_t pathSize)
{
#ifdef _WIN32
    char relativeSnapshot[1200], directory[1200], search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW found;
    HANDLE handle;
    if (objectId <= 0 || path == NULL || pathSize == 0 || activeBuildStageNumber <= 0 ||
        snprintf(relativeSnapshot, sizeof(relativeSnapshot),
                 "movables\\reference_sources\\reference_%04d", objectId) <= 0 ||
        !RpgStageStorage_GetFilePath(activeBuildStageNumber, relativeSnapshot, directory,
                                     (int)sizeof(directory)) ||
        snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, (int)(sizeof(wideSearch) / sizeof(wideSearch[0])))) return false;
    handle = FindFirstFileW(wideSearch, &found);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        char name[1024];
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, name, (int)sizeof(name), NULL, NULL) <= 0 ||
            snprintf(path, pathSize, "%s\\%s", directory, name) <= 0) continue;
        FindClose(handle);
        return true;
    } while (FindNextFileW(handle, &found) != 0);
    FindClose(handle);
#else
    (void)objectId; (void)path; (void)pathSize;
#endif
    return false;
}

static const char *ReferenceSourceLeafName(const char *path)
{
    const char *backslash = path == NULL ? NULL : strrchr(path, '\\');
    const char *slash = path == NULL ? NULL : strrchr(path, '/');
    const char *name = backslash != NULL ? backslash + 1 : path;
    return slash != NULL && slash + 1 > name ? slash + 1 : name;
}

/* Reference folders are presented to Explorer by their authored folder name.
   `path` changes to this runtime location during Play; `sourcePath` is the
   durable label captured from the editor/static stage record. */
static bool GetReferenceObjectFolderRuntimePath(const RpgReferenceObject *object,
                                                char *path, size_t pathSize)
{
#ifdef _WIN32
    char referenceFolders[1200];
    const char *source;
    const char *name;
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
        path == NULL || pathSize == 0 || !GetReferenceFoldersPath(referenceFolders, sizeof(referenceFolders)))
        return false;
    source = object->sourcePath[0] != '\0' ? object->sourcePath : object->path;
    name = ReferenceSourceLeafName(source);
    if (name == NULL || name[0] == '\0' || strpbrk(name, "\\/:*?\"<>|") != NULL)
        return snprintf(path, pathSize, "%s\\Folder_%04d", referenceFolders, object->id) > 0;
    return snprintf(path, pathSize, "%s\\%s", referenceFolders, name) > 0;
#else
    (void)object; (void)path; (void)pathSize;
    return false;
#endif
}

bool RpgObjectFolders_EnsureReferenceObjectFile(RpgReferenceObject *object)
{
#ifdef _WIN32
    char referenceRoot[1200], sourcePath[1200], fallbackPath[1200], folderPath[1200], targetPath[1200];
    wchar_t wideSource[1200], wideTarget[1200];
    const char *name, *identityPath;
    DWORD attributes;
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FILE || object->id <= 0 ||
        activeBuildPath[0] == '\0' ||
        !GetReferenceFilesPath(referenceRoot, sizeof(referenceRoot)) || !CreateFolderUtf8(referenceRoot)) return false;

    identityPath = object->sourcePath[0] != '\0' ? object->sourcePath : object->path;
    name = ReferenceSourceLeafName(identityPath);

    /* The ID-addressed static snapshot is the authoring source of truth.
       A Play copy is intentionally disposable, and assets/Files can be
       replaced or temporarily unavailable while the editor is open.  Looking
       at the snapshot first keeps a FILE object's identity and bytes stable
       across preview rebuilds, Continue, and a fresh application launch. */
    if (name != NULL && name[0] != '\0' &&
        FindStaticReferenceSource(object->id, name, sourcePath, sizeof(sourcePath)))
        ;
    /* A generic or empty runtime name must not prevent recovery.  One source
       folder belongs to exactly one object ID, so its actual leaf name is the
       durable identity to restore before key comparison. */
    else if (FindStaticReferenceSourceByObjectId(object->id, sourcePath, sizeof(sourcePath)))
        ;
    /* A stage made before snapshots existed can still be recovered from its
       selected relative source. */
    else if (object->sourcePath[0] != '\0' &&
        snprintf(fallbackPath, sizeof(fallbackPath), "%s../assets/Files/%s",
                 GetApplicationDirectory(), object->sourcePath) > 0 &&
        ToWide(fallbackPath, wideSource, 1200) &&
        GetFileAttributesW(wideSource) != INVALID_FILE_ATTRIBUTES)
        snprintf(sourcePath, sizeof(sourcePath), "%s", fallbackPath);
    else if (name[0] != '\0' &&
             snprintf(fallbackPath, sizeof(fallbackPath), "%s../assets/Files/%s",
                      GetApplicationDirectory(), name) > 0 &&
             ToWide(fallbackPath, wideSource, 1200) &&
             GetFileAttributesW(wideSource) != INVALID_FILE_ATTRIBUTES)
        snprintf(sourcePath, sizeof(sourcePath), "%s", fallbackPath);
    else return false;

    name = ReferenceSourceLeafName(sourcePath);
    if (name == NULL || name[0] == '\0' ||
        snprintf(folderPath, sizeof(folderPath), "%s\\reference_%04d", referenceRoot, object->id) <= 0 ||
        snprintf(targetPath, sizeof(targetPath), "%s\\%s", folderPath, name) <= 0 ||
        !ToWide(targetPath, wideTarget, 1200)) return false;

    /* sourcePath is identity, not a runtime location.  Rewrite only this
       in-memory runtime copy from the ID-owned snapshot; static configuration
       remains unchanged. */
    snprintf(object->sourcePath, sizeof(object->sourcePath), "%s", name);

    /* Only the canonical path for this active build is allowed to count as
       an already-prepared runtime file. */
    attributes = GetFileAttributesW(wideTarget);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
        snprintf(object->path, sizeof(object->path), "%s", targetPath);
        return true;
    }

    /* Promote an old cell-addressed copy (or an assets/Files fallback) once
       while editing.  Thereafter this movable has an ID-addressed immutable
       source, independent of the inspector's last selection or preview
       cache.  Game packages are read-only here and simply use their packaged
       snapshot/fallback. */
    if (RpgStageStorage_GetDomain() == RPG_STAGE_STORAGE_SETTINGS)
        (void)RpgStageStorage_CopyReferenceFileToStaticSource(activeBuildStageNumber,
                                                              object->id, sourcePath);

    if (!CreateFolderUtf8(folderPath) || !ToWide(sourcePath, wideSource, 1200) ||
        (CopyFileW(wideSource, wideTarget, FALSE) == 0 && GetLastError() != ERROR_FILE_EXISTS)) return false;
    snprintf(object->path, sizeof(object->path), "%s", targetPath);
    return true;
#else
    (void)object;
    return false;
#endif
}

static bool EnsureReferenceObjectFolder(RpgReferenceObject *object)
{
#ifdef _WIN32
    char referenceFolders[1200], folderPath[1200];
    const char *name;
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER || object->id <= 0 ||
        activeBuildPath[0] == '\0' || !GetReferenceFoldersPath(referenceFolders, sizeof(referenceFolders)) ||
        !GetReferenceObjectFolderRuntimePath(object, folderPath, sizeof(folderPath)) ||
        !CreateFolderUtf8(referenceFolders) || !CreateFolderUtf8(folderPath)) return false;
    if (object->sourcePath[0] == '\0') {
        name = ReferenceSourceLeafName(object->path);
        if (name != NULL && name[0] != '\0')
            snprintf(object->sourcePath, sizeof(object->sourcePath), "%s", name);
    }
    snprintf(object->path, sizeof(object->path), "%s", folderPath);
    return true;
#else
    (void)object;
    return false;
#endif
}

bool RpgObjectFolders_EnsureReferenceObject(RpgReferenceObject *object)
{
    if (object == NULL) return false;
    if (object->objectKind == RPG_REFERENCE_OBJECT_FILE)
        return RpgObjectFolders_EnsureReferenceObjectFile(object);
    if (object->objectKind == RPG_REFERENCE_OBJECT_FOLDER)
        return EnsureReferenceObjectFolder(object);
    return false;
}

/* Folder object metadata is runtime data.  In particular, do not use
   object->path directly here: during an editor preview it can still hold a
   static legacy display name while the runtime folder has not been eagerly
   materialized. */
static bool WriteRuntimeReferenceObjectFolderInfo(const RpgReferenceObject *object,
                                                  RpgGridCell cell)
{
#ifdef _WIN32
    char referenceFolders[1200], runtimeFolder[1200];
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
        activeBuildPath[0] == '\0' ||
        !GetReferenceFoldersPath(referenceFolders, sizeof(referenceFolders)) ||
        !GetReferenceObjectFolderRuntimePath(object, runtimeFolder, sizeof(runtimeFolder)) ||
        !CreateFolderUtf8(referenceFolders) || !CreateFolderUtf8(runtimeFolder)) return false;
    return WriteReferenceFolderInfo(runtimeFolder, cell);
#else
    (void)object;
    (void)cell;
    return false;
#endif
}

bool RpgObjectFolders_ResolveReferenceObjectSource(RpgReferenceObject *object)
{
    return RpgObjectFolders_EnsureReferenceObjectFile(object);
}

/* static の参照コピーを実行用へ直接複製する。Fileの不備はそのマスだけを除外し、Playは継続する。 */
static void PrepareRuntimeReferenceFiles(RpgStage *stage, int onlyMapIndex)
{
#ifdef _WIN32
    char referenceRoot[1200], sourcePath[1200], fallbackPath[1200], folderPath[1200], targetPath[1200];
    wchar_t wideSource[1200], wideTarget[1200];
    int firstColumn = 0;
    int lastColumn = RPG_STAGE_WORLD_COLUMNS;
    if (stage == NULL ||
        !GetReferenceFilesPath(referenceRoot, sizeof(referenceRoot)) ||
        !CreateFolderUtf8(referenceRoot)) return;
    if (onlyMapIndex >= 0) {
        if (onlyMapIndex >= RPG_STAGE_MAP_COUNT) return;
        firstColumn = onlyMapIndex * RPG_STAGE_COLUMNS;
        lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
         column < lastColumn; column++) {
        const char *source;
        const char *name;
        DWORD attributes;
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE ||
            (onlyMapIndex >= 0 && column / RPG_STAGE_COLUMNS != onlyMapIndex)) continue;
        source = RpgStage_GetReferencePathAtCell(stage, row, column);
        if (snprintf(sourcePath, sizeof(sourcePath), "%s", source) <= 0 || !ToWide(sourcePath, wideSource, 1200)) goto unavailable;
        attributes = GetFileAttributesW(wideSource);
        name = strrchr(sourcePath, '\\');
        if (name == NULL) name = strrchr(sourcePath, '/');
        name = name == NULL ? sourcePath : name + 1;
        if ((attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) && name[0] != '\0' &&
            snprintf(fallbackPath, sizeof(fallbackPath), "%s../assets/Files/%s", GetApplicationDirectory(), name) > 0 &&
            ToWide(fallbackPath, wideSource, 1200) && GetFileAttributesW(wideSource) != INVALID_FILE_ATTRIBUTES)
            snprintf(sourcePath, sizeof(sourcePath), "%s", fallbackPath);
        else if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || name[0] == '\0') goto unavailable;
        name = strrchr(sourcePath, '\\');
        if (name == NULL) name = strrchr(sourcePath, '/');
        name = name == NULL ? sourcePath : name + 1;
        if (name[0] == '\0' || snprintf(folderPath, sizeof(folderPath), "%s\\reference_r%02d_c%03d",
                                           referenceRoot, row, column) <= 0 ||
            snprintf(targetPath, sizeof(targetPath), "%s\\%s", folderPath, name) <= 0) goto unavailable;
        if (_stricmp(sourcePath, targetPath) == 0) continue;
        if (!CreateFolderUtf8(folderPath) || !ToWide(sourcePath, wideSource, 1200) ||
            !ToWide(targetPath, wideTarget, 1200) ||
            (CopyFileW(wideSource, wideTarget, FALSE) == 0 && GetLastError() != ERROR_FILE_EXISTS) ||
            !RpgStage_SetReferencePathAtCell(stage, row, column, targetPath)) goto unavailable;
        continue;
unavailable:
        /* A missing source can be transient: the static package may have just
           been extracted, or a reference copy may be repaired on the next
           build.  Never turn that temporary lookup failure into a deletion of
           the stage object (and then persist it into runtime_state). */
        continue;
    }
    /* New reference objects are position based.  Their backing files use the
       stable movable id rather than a terrain-cell coordinate. */
    for (int index = 0; index < stage->referenceObjects.count; index++) {
        RpgReferenceObject *object = &stage->referenceObjects.entries[index];
        if (object->objectKind != RPG_REFERENCE_OBJECT_FILE ||
            (object->path[0] == '\0' && object->sourcePath[0] == '\0') ||
            (onlyMapIndex >= 0 && RpgStage_GetMapAtWorldPosition(stage, object->position) != onlyMapIndex)) continue;
        (void)RpgObjectFolders_EnsureReferenceObjectFile(object);
    }
#else
    (void)stage; (void)onlyMapIndex;
#endif
}

/* Reference folders are area-owned just like terrain cells.  In a preview,
   create their runtime directories only for the area being made available. */
static bool PrepareRuntimeReferenceFolders(RpgStage *stage, int onlyMapIndex)
{
#ifdef _WIN32
    int firstColumn = 0;
    int lastColumn = RPG_STAGE_WORLD_COLUMNS;
    if (stage == NULL) return false;
    if (onlyMapIndex >= 0) {
        if (onlyMapIndex >= RPG_STAGE_MAP_COUNT) return false;
        firstColumn = onlyMapIndex * RPG_STAGE_COLUMNS;
        lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
         column < lastColumn; column++) {
        const char *oldPath;
        const char *name;
        char folderPath[1200], referenceFolders[1200];
        if (!RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column]) ||
            (onlyMapIndex >= 0 && column / RPG_STAGE_COLUMNS != onlyMapIndex)) continue;
        oldPath = RpgStage_GetReferencePathAtCell(stage, row, column);
        name = strrchr(oldPath, '\\');
        if (name == NULL) name = strrchr(oldPath, '/');
        name = name == NULL ? oldPath : name + 1;
        if (!GetReferenceFoldersPath(referenceFolders, sizeof(referenceFolders)) || name[0] == '\0' ||
            snprintf(folderPath, sizeof(folderPath), "%s\\%s", referenceFolders, name) <= 0 ||
            !CreateFolderUtf8(referenceFolders) || !CreateFolderUtf8(folderPath) ||
            !RpgStage_SetReferencePathAtCell(stage, row, column, folderPath)) return false;
    }
    for (int index = 0; index < stage->referenceObjects.count; index++) {
        RpgReferenceObject *object = &stage->referenceObjects.entries[index];
        char referenceFolders[1200], folderPath[1200];
        if (object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
            (onlyMapIndex >= 0 && RpgStage_GetMapAtWorldPosition(stage, object->position) != onlyMapIndex)) continue;
        (void)referenceFolders;
        (void)folderPath;
        if (!EnsureReferenceObjectFolder(object)) return false;
    }
    return true;
#else
    (void)stage; (void)onlyMapIndex;
    return false;
#endif
}

bool RpgObjectFolders_ReadCompactCellAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS])
{
    RpgBuildCellStorageBackend backend = GetBuildCellStorageBackend();
    bool couldRead = RpgBuildCellStorage_ReadAvailability(available, &backend);
    if (!couldRead) return false;
    /* CompactでInboxから返されたマスは再圧縮せずcells内の実フォルダとして残る。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgObjectFolder folder = { .cell = { row, column } };
        char cellPath[1200];
        if (BlockPath(&folder, 0, false, cellPath, sizeof(cellPath)) && FolderExistsUtf8(cellPath))
            available[row][column] = true;
    }
    return true;
}

static bool ExtractCompactCellMetadata(const RpgObjectFolder *folder, int *blockType)
{
    RpgBuildCellStorageBackend backend = GetBuildCellStorageBackend();
    return folder != NULL && RpgBuildCellStorage_Extract(folder->cell, blockType, &backend);
}

static unsigned long long HashPath(const char *path)
{
    unsigned long long hash = 1469598103934665603ULL;
    while (*path != '\0') { hash ^= (unsigned char)*path++; hash *= 1099511628211ULL; }
    return hash;
}

static bool IsDataShotBaselinePath(const RpgDataShot *shot, unsigned long long hash)
{
    for (int index = 0; index < shot->baselinePathCount; index++)
        if (shot->baselinePathHashes[index] == hash) return true;
    return false;
}

// ファイル数・容量と、発射後に追加されたファイルの有無を同じ再帰走査で求める。
static void GetDataShotFolderStats(const char *directory, const char *root, RpgDataShot *shot,
                                   bool recordBaseline, int *fileCount, unsigned long long *totalBytes,
                                   bool *hasAddedFiles)
{
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (!FolderExistsUtf8(directory) || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, 1200)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[1024], child[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            GetDataShotFolderStats(child, root, shot, recordBaseline, fileCount, totalBytes, hasAddedFiles);
            continue;
        }
        (*fileCount)++;
        *totalBytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        const char *relativePath = child + strlen(root);
        while (*relativePath == '\\' || *relativePath == '/') relativePath++;
        unsigned long long pathHash = HashPath(relativePath);
        if (recordBaseline) {
            if (shot->baselinePathCount < RPG_DATA_SHOT_BASELINE_FILE_MAX)
                shot->baselinePathHashes[shot->baselinePathCount++] = pathHash;
        } else if (!IsDataShotBaselinePath(shot, pathHash)) *hasAddedFiles = true;
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
}

static bool EnsureAttachment(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char metadataPath[1200], ownerPath[1200], blocks[1200];
    wchar_t wideMetadataPath[1200];
    RpgGridCell ownerCell;
    FILE *file;
    if (attachment == NULL || !RpgAttachments_GetOwnerBlockCell(attachment, &ownerCell) ||
        !AttachmentPath(attachment, false, metadataPath, sizeof(metadataPath)) ||
        !AttachmentOwnerBlockPath(attachment, false, ownerPath, sizeof(ownerPath)) ||
        !GetCellsPath(blocks, sizeof(blocks)) || !CreateFolderUtf8(blocks) ||
        !CreateFolderUtf8(ownerPath) || !ToWide(metadataPath, wideMetadataPath, 1200)) return false;
    file = _wfopen(wideMetadataPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "kind=attachment\ntype=%d\nid=%d\nowner_row=%d\nowner_column=%d\n"
            "mount_row=%d\nmount_column=%d\nside=%d\n",
            attachment->type, attachment->folderId, ownerCell.row, ownerCell.column,
            attachment->cell.row, attachment->cell.column, attachment->side);
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, metadataPath, NULL);
    NotifyShellParentChanged(metadataPath);
    return true;
#else
    (void)attachment;
    return false;
#endif
}

static bool EnsureReceiverWire(const RpgReceiver *receiver, const RpgWires *wires)
{
#ifdef _WIN32
    char metadataPath[1200];
    wchar_t wideMetadataPath[1200];
    const RpgWire *wire = NULL;
    FILE *file;
    if (receiver == NULL || wires == NULL ||
        !ReceiverWirePath(receiver, false, metadataPath, sizeof(metadataPath)) ||
        !ToWide(metadataPath, wideMetadataPath, 1200)) return false;
    for (int index = 0; index < wires->count; index++) {
        const RpgWire *candidate = &wires->entries[index];
        if (candidate->kind == RPG_WIRE_KIND_ELECTRIC && candidate->hasReceiverSource &&
            candidate->receiverCell.row == receiver->cell.row &&
            candidate->receiverCell.column == receiver->cell.column &&
            candidate->receiverSide == receiver->side) {
            wire = candidate;
            break;
        }
    }
    /* No path is a valid receiver state.  Remove stale derived metadata rather
       than inventing a standalone wire object. */
    if (wire == NULL) {
        DeleteFileW(wideMetadataPath);
        return true;
    }
    file = _wfopen(wideMetadataPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "kind=receiver_wire\\nowner_row=%d\\nowner_column=%d\\nside=%d\\npath_count=%d\\n",
            receiver->cell.row, receiver->cell.column, receiver->side, wire->path.cellCount);
    for (int index = 0; index < wire->path.cellCount; index++)
        fprintf(file, "path=%d,%d\\n", wire->path.cells[index].row, wire->path.cells[index].column);
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, metadataPath, NULL);
    NotifyShellParentChanged(metadataPath);
    return true;
#else
    (void)receiver; (void)wires;
    return false;
#endif
}

static bool EnsureReceiver(const RpgReceiver *receiver, const RpgWires *wires)
{
#ifdef _WIN32
    char metadataPath[1200];
    wchar_t wideMetadataPath[1200];
    if (receiver == NULL) return false;
    /* Remove the retired split receiver record.  The corresponding generic
       attachment record is emitted by EnsureAttachment before this call. */
    if (ReceiverPath(receiver, false, metadataPath, sizeof(metadataPath)) &&
        ToWide(metadataPath, wideMetadataPath, 1200)) (void)DeleteFileW(wideMetadataPath);
    return EnsureReceiverWire(receiver, wires);
#else
    (void)receiver; (void)wires;
    return false;
#endif
}

/* Data-shot routing is not derivable from the common record, so it belongs in
   a separate type-specific file rather than expanding object_info.txt. */
static bool WriteDataShotSpecificInfo(const char *folder, const RpgDataShot *shot)
{
#ifdef _WIN32
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    if (folder == NULL || shot == NULL ||
        snprintf(infoPath, sizeof(infoPath), "%s\\data_shot_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, (int)(sizeof(wideInfoPath) / sizeof(wideInfoPath[0])))) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "attachment_index=%d\npath_cell_index=%d\n",
            shot->attachmentIndex, shot->pathCellIndex);
    return fclose(file) == 0;
#else
    (void)folder; (void)shot;
    return false;
#endif
}

static bool EnsureDataShot(const RpgDataShot *shot, const RpgAttachments *attachments)
{
    char source[1200], inbox[1200], objects[1200], parent[1200], attachmentSource[1200], attachmentInbox[1200];
    const RpgAttachment *attachment;
    if (!DataShotPath(shot, false, source, sizeof(source)) || !DataShotPath(shot, true, inbox, sizeof(inbox)) ||
        !GetObjectsPath(objects, sizeof(objects))) return false;
    if (FolderExistsUtf8(source) || FolderExistsUtf8(inbox)) return true;
    if (attachments == NULL || shot->attachmentIndex < 0 || shot->attachmentIndex >= attachments->count) return false;
    attachment = &attachments->entries[shot->attachmentIndex];
    if (!EnsureAttachment(attachment) || !AttachmentOwnerBlockPath(attachment, false, attachmentSource, sizeof(attachmentSource)) ||
        !AttachmentOwnerBlockPath(attachment, true, attachmentInbox, sizeof(attachmentInbox)) ||
        snprintf(parent, sizeof(parent), "%s\\parent", source) <= 0) return false;
    // 発射時点の装置フォルダ全体を parent に入れ、弾自身の object_info.txt は root に残す。
    const char *parentSource = FolderExistsUtf8(attachmentSource) ? attachmentSource : attachmentInbox;
    bool created = CreateFolderUtf8(objects) &&
                   WriteObjectInfo(source, 0, shot->position) &&
                   WriteDataShotSpecificInfo(source, shot) &&
                   CopyTree(parentSource, parent);
    if (created) NotifyShellPathHierarchyChanged(source);
    return created;
}

/* PNGのメタ情報は独立した所有フォルダに書き、セルの存在や当たり判定とは結び付けない。 */
static bool EnsureImageObject(const RpgImageObject *object)
{
    char source[1200], inbox[1200], objects[1200], infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    Vector2 position;
    if (object == NULL || !ImageObjectPath(object, false, source, sizeof(source)) ||
        !ImageObjectPath(object, true, inbox, sizeof(inbox)) || !GetObjectsPath(objects, sizeof(objects))) return false;
    if (FolderExistsUtf8(source) || FolderExistsUtf8(inbox)) return true;
    position = (Vector2){ RpgImageObjects_GetWorldCenterX(object, RPG_STAGE_TILE_SIZE),
                          RpgImageObjects_GetWorldCenterY(object, RPG_STAGE_TILE_SIZE) };
    if (!CreateFolderUtf8(objects) || !WriteObjectInfo(source, object->appearance, position) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\image_info.txt", source) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "layer=%d\nscale=%.3f\npath=%s\n", (int)object->layer, object->scale,
            object->path[0] != '\0' ? object->path : "-");
    if (fclose(file) != 0) return false;
    NotifyShellPathHierarchyChanged(source);
    return true;
}

/* Playerは実行中のStage buildだけに存在する一時オブジェクトとして、objects/Playerへメタ情報を置く。 */
static bool EnsurePlayerFolder(Vector2 position)
{
    char objects[1200], playerFolder[1200];
    if (activeBuildPath[0] == '\0' || !GetObjectsPath(objects, sizeof(objects)) ||
        snprintf(playerFolder, sizeof(playerFolder), "%s\\Player", objects) <= 0) return false;
    return CreateFolderUtf8(objects) && WriteObjectInfo(playerFolder, 0, position);
}

static void RemovePlayerFolder(void)
{
    char objects[1200], playerFolder[1200];
    if (activeBuildPath[0] == '\0' || !GetObjectsPath(objects, sizeof(objects)) ||
        snprintf(playerFolder, sizeof(playerFolder), "%s\\Player", objects) <= 0) return;
    RemoveTree(playerFolder);
}

/* データ弾は再開対象にしない一時オブジェクト。進行度フォルダを残す終了時にもこれだけは掃除する。 */
static void RemoveTransientDataShotFolders(void)
{
#ifdef _WIN32
    char objects[1200], search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (!GetObjectsPath(objects, sizeof(objects)) || snprintf(search, sizeof(search), "%s\\data_shot_*", objects) <= 0 ||
        !ToWide(search, wideSearch, (int)(sizeof(wideSearch) / sizeof(wideSearch[0])))) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[512], path[1200];
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, (int)sizeof(name), NULL, NULL) <= 0 ||
            snprintf(path, sizeof(path), "%s\\%s", objects, name) <= 0) continue;
        RemoveTree(path);
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
#endif
}

/* Object-specific endpoint for the shared folder monitor.  The callback only
 * marks the object dirty; this module performs the reload on its normal tick. */
static void OnDataShotFolderChanged(void *context, const char *directory)
{
    RpgDataShot *shot = context;
    (void)directory;
    if (shot != NULL) shot->folderMonitorDirty = true;
}

static void UpdateDataShotProperties(int shotIndex, RpgDataShot *shot, const RpgAttachments *attachments)
{
    char source[1200], inbox[1200];
    int fileCount = 0;
    unsigned long long totalBytes = 0;
    bool hasAddedFiles = false;
    bool recordBaseline;
    (void)shotIndex;
    if (!DataShotPath(shot, false, source, sizeof(source)) || !DataShotPath(shot, true, inbox, sizeof(inbox))) return;
    const char *folder = FolderExistsUtf8(source) ? source : inbox;
    recordBaseline = !shot->folderStatsInitialized;
    shot->folderMonitorHandle = RpgObjectFolderMonitor_Connect(
        shot->folderMonitorHandle, folder, true, OnDataShotFolderChanged, shot);
    if (recordBaseline) shot->folderMonitorDirty = true;
    else RpgObjectFolderMonitor_PollAll();
    if (!shot->folderMonitorDirty) return;
    // 初回も監視を開始する。以後は配下の変更通知が届いた時だけ集計する。
    GetDataShotFolderStats(folder, folder, shot, recordBaseline, &fileCount, &totalBytes, &hasAddedFiles);
    if (recordBaseline) shot->folderStatsInitialized = true;
    shot->folderMonitorDirty = false;
    if (!recordBaseline && fileCount == shot->fileCount && totalBytes == shot->totalBytes &&
        hasAddedFiles == shot->hasAddedFiles) return;
    if (shot->attachmentIndex < 0 || shot->attachmentIndex >= attachments->count) return;
    shot->hasAddedFiles = hasAddedFiles;
    // 容量が増えるほど緩やかに減速し、極端な容量でも停止しない最低速度を残す。
    shot->speed = 360.0f / (1.0f + (float)totalBytes / (64.0f * 1024.0f));
    if (shot->speed < 30.0f) shot->speed = 30.0f;
    if (shot->speed > 360.0f) shot->speed = 360.0f;
    const RpgShooterConfig *shooter = RpgAttachments_FindShooterConst(attachments,
        attachments->entries[shot->attachmentIndex].folderId);
    if (shooter != NULL) RpgDataShot_SetFileProperties(shot, shooter, fileCount, totalBytes);
}

/* データ弾はマスを占有しないため、フォルダに最後のグリッド位置と親装置上の軌道位置だけを残す。 */
static bool WriteDataShotRuntimeMetadata(RpgDataShot *shot)
{
    char source[1200], inbox[1200], staging[1200];
    RpgGridCell cell;
    const char *folder = NULL;
    if (shot == NULL || !shot->active || shot->isPreview ||
        !DataShotPath(shot, false, source, sizeof(source)) ||
        !DataShotPath(shot, true, inbox, sizeof(inbox))) return false;
    cell = (RpgGridCell){ (int)(shot->position.y / RPG_STAGE_TILE_SIZE),
                          (int)(shot->position.x / RPG_STAGE_TILE_SIZE) };
    if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS ||
        (cell.row == shot->metadataCell.row && cell.column == shot->metadataCell.column)) return true;
    if (FolderExistsUtf8(source)) folder = source;
    else if (FolderExistsUtf8(inbox)) folder = inbox;
    else if (GetReturnStagingPath(source, staging, sizeof(staging)) && FolderExistsUtf8(staging)) folder = staging;
    if (folder == NULL || !WriteObjectInfo(folder, 0, shot->position) ||
        !WriteDataShotSpecificInfo(folder, shot)) return false;
    shot->metadataCell = cell;
    return true;
}

static bool GetFirstAddedDataShotFilePath(const char *directory, const char *root,
                                          const RpgDataShot *shot, char *path, size_t pathSize)
{
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (snprintf(search, sizeof(search), "%s\\*", directory) <= 0 || !ToWide(search, wideSearch, 1200)) return false;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        char name[1024], child[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (GetFirstAddedDataShotFilePath(child, root, shot, path, pathSize)) { FindClose(handle); return true; }
            continue;
        }
        const char *relativePath = child + strlen(root);
        while (*relativePath == '\\' || *relativePath == '/') relativePath++;
        if (!IsDataShotBaselinePath(shot, HashPath(relativePath)) &&
            snprintf(path, pathSize, "%s", child) > 0) { FindClose(handle); return true; }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    return false;
}

static bool MaterializeBlockInInbox(const RpgObjectFolder *folder, int blockType)
{
    char source[1200], inbox[1200], inboxParent[1200];
    Vector2 position;
    if (!BlockPath(folder, blockType, false, source, sizeof(source)) ||
        !BlockPath(folder, blockType, true, inbox, sizeof(inbox)) ||
        !GetInboxPath(inboxParent, sizeof(inboxParent)) || !CreateFolderUtf8(inboxParent)) return false;
    if (FolderExistsUtf8(inbox)) return true;
    if (FolderExistsUtf8(source)) return MoveDirectory(source, inbox);
    /* build で外部削除されたマスは赤壁として扱うため、取得操作でフォルダを再生成しない。 */
    if (activeBuildPath[0] != '\0') {
        int storedBlockType;
        RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
        if (RpgBuildCellStorage_EnsureCell(folder->cell, blockType, &storageBackend) &&
            FolderExistsUtf8(source)) return MoveDirectory(source, inbox);
        if (!ExtractCompactCellMetadata(folder, &storedBlockType)) return false;
        position = (Vector2){ (folder->cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                              (folder->cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
        return WriteObjectInfo(inbox, storedBlockType, position);
    }
    // 通常ブロックは普段は仮想的にだけ存在し、アニメーション時にメタ情報付きで実体化する。
    position = (Vector2){ (folder->cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                          (folder->cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    return WriteObjectInfo(inbox, blockType, position);
}

/* This is intentionally an eat-time operation, not a global pre-sync.  A
   block becomes a real folder only because Zipper is taking it, and that
   folder is complete before its single move into Zipper. */
static bool MaterializeBlockWithAttachmentsInInbox(const RpgObjectFolder *folder, int blockType,
                                                    const RpgAttachments *attachments,
                                                    const RpgReceivers *receivers,
                                                    const RpgWires *wires)
{
    char source[1200], inbox[1200], inboxParent[1200];
    Vector2 position;
    int storedBlockType = blockType;
    const int ownerComposite = GetBuildCompositeId(folder->cell);
    if (folder == NULL || !BlockPath(folder, blockType, false, source, sizeof(source)) ||
        !BlockPath(folder, blockType, true, inbox, sizeof(inbox)) ||
        !GetInboxPath(inboxParent, sizeof(inboxParent)) || !CreateFolderUtf8(inboxParent)) return false;
    if (FolderExistsUtf8(inbox)) return true;

    /* Keep the existing compact-cell behavior, but materialize the source
       folder first so child metadata and object_info always share one move. */
    if (!FolderExistsUtf8(source) && activeBuildPath[0] != '\0') {
        RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
        if (!RpgBuildCellStorage_EnsureCell(folder->cell, blockType, &storageBackend) &&
            !ExtractCompactCellMetadata(folder, &storedBlockType)) return false;
    }
    position = (Vector2){ (folder->cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                          (folder->cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    if (!WriteObjectInfo(source, storedBlockType, position)) return false;

    for (int index = 0; attachments != NULL && index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell attachmentOwner = { -1, -1 };
        const bool hasAttachmentOwner = RpgAttachments_GetOwnerBlockCell(attachment, &attachmentOwner);
        const int attachmentComposite = hasAttachmentOwner ? GetBuildCompositeId(attachmentOwner) : 0;
        if ((ownerComposite > 0 ? attachmentComposite == ownerComposite :
                                 hasAttachmentOwner && attachmentComposite == 0 && attachmentOwner.row == folder->cell.row &&
                                 attachmentOwner.column == folder->cell.column) &&
            !EnsureAttachment(attachment)) return false;
    }
    for (int index = 0; receivers != NULL && index < RpgReceivers_Count(receivers); index++) {
        RpgReceiver receiver;
        if (!RpgReceivers_Get(receivers, index, &receiver)) continue;
        const int receiverComposite = GetBuildCompositeId(receiver.cell);
        if ((ownerComposite > 0 ? receiverComposite == ownerComposite :
                                 receiverComposite == 0 && receiver.cell.row == folder->cell.row &&
                                 receiver.cell.column == folder->cell.column) &&
            !EnsureReceiver(&receiver, wires)) return false;
    }
    return MoveDirectory(source, inbox);
}

static void SetStageEffectFolderMissing(RpgStage *stage, RpgGridCell rootCell, int rootType, bool missing)
{
    const RpgEffectShape *shape;
    if (stage == NULL || rootCell.row < 0 || rootCell.row >= RPG_STAGE_ROWS ||
        rootCell.column < 0 || rootCell.column >= RPG_STAGE_WORLD_COLUMNS) return;
    shape = RpgBlockInventory_GetEffectShape(rootType);
    if (shape == NULL || shape->rootType != rootType) {
        if (missing) {
            stage->missingBlockTypes[rootCell.row][rootCell.column] = rootType;
            stage->blocks[rootCell.row][rootCell.column] = RPG_BLOCK_BUILD_MISSING;
        }
        return;
    }
    for (int index = 0; index < shape->cellCount; index++) {
        const RpgEffectShapeCell *part = &shape->cells[index];
        int row = rootCell.row + part->offsetY;
        int column = rootCell.column + part->offsetX;
        if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) continue;
        if (missing) {
            stage->missingBlockTypes[row][column] = part->blockType;
            stage->blocks[row][column] = RPG_BLOCK_BUILD_MISSING;
        } else if (stage->blocks[row][column] == RPG_BLOCK_BUILD_MISSING) {
            stage->blocks[row][column] = part->blockType;
            stage->missingBlockTypes[row][column] = 0;
        }
    }
}

static void ReleaseDestroyedDataShotFolder(const char *source, const char *inbox, Vector2 position,
                                           const RpgDataShot *shot, RpgReferenceObjects *referenceObjects)
{
    const char *owner = FolderExistsUtf8(source) ? source : inbox;
    char drops[1200], dropFolder[1200], target[1200], addedFilePath[1200];
    wchar_t wideAddedFilePath[1200], wideTarget[1200];
    if (!FolderExistsUtf8(owner)) return;
    // 初期 parent の内容だけなら弾と一緒に消す。発射後に追加されたファイルだけを落とす。
    if (shot == NULL || !shot->hasAddedFiles || referenceObjects == NULL || !GetDropsPath(drops, sizeof(drops)) ||
        !CreateFolderUtf8(drops) || !GetFirstAddedDataShotFilePath(owner, owner, shot, addedFilePath,
                                                                     sizeof(addedFilePath))) {
        RemoveTree(owner);
        return;
    }
    const char *name = strrchr(addedFilePath, '\\');
    name = name == NULL ? addedFilePath : name + 1;
    // ドロップごとのフォルダで重複を避け、ファイル本来の名前は変更しない。
    if (snprintf(dropFolder, sizeof(dropFolder), "%s\\drop_%06d", drops, nextFileDropId++) <= 0 ||
        !CreateFolderUtf8(dropFolder) || snprintf(target, sizeof(target), "%s\\%s", dropFolder, name) <= 0 ||
        !ToWide(addedFilePath, wideAddedFilePath, 1200) || !ToWide(target, wideTarget, 1200) ||
        MoveFileExW(wideAddedFilePath, wideTarget, MOVEFILE_WRITE_THROUGH) == 0 ||
        !RpgReferenceObjects_AddDrop(referenceObjects, position, target)) {
        RemoveTree(owner);
        return;
    }
    RemoveTree(owner);
}
#endif

bool RpgObjectFolder_GetZipperInboxDirectory(char *path, size_t pathSize)
{
#ifdef _WIN32
    if (path == NULL || pathSize == 0 || !GetInboxPath(path, pathSize)) return false;
    return path[0] != '\0';
#else
    (void)path;
    (void)pathSize;
    return false;
#endif
}

/* 格納前に比較し、同名でも内容が異なるファイルを失わないようにする。 */
static bool AreFilesEqual(const wchar_t *leftPath, const wchar_t *rightPath)
{
#ifdef _WIN32
    HANDLE left = INVALID_HANDLE_VALUE, right = INVALID_HANDLE_VALUE;
    LARGE_INTEGER leftSize, rightSize;
    BYTE leftBuffer[16384], rightBuffer[16384];
    DWORD leftRead, rightRead;
    bool equal = false;
    left = CreateFileW(leftPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    right = CreateFileW(rightPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (left == INVALID_HANDLE_VALUE || right == INVALID_HANDLE_VALUE ||
        !GetFileSizeEx(left, &leftSize) || !GetFileSizeEx(right, &rightSize) ||
        leftSize.QuadPart != rightSize.QuadPart) goto cleanup;
    for (;;) {
        if (!ReadFile(left, leftBuffer, sizeof(leftBuffer), &leftRead, NULL) ||
            !ReadFile(right, rightBuffer, sizeof(rightBuffer), &rightRead, NULL) || leftRead != rightRead)
            goto cleanup;
        if (leftRead == 0) { equal = true; break; }
        if (memcmp(leftBuffer, rightBuffer, leftRead) != 0) goto cleanup;
    }
cleanup:
    if (left != INVALID_HANDLE_VALUE) CloseHandle(left);
    if (right != INVALID_HANDLE_VALUE) CloseHandle(right);
    return equal;
#else
    (void)leftPath;
    (void)rightPath;
    return false;
#endif
}

/* runtime の File オブジェクトを格納した後、元オブジェクト用フォルダを残さない。 */
/* reference_files から格納した File は、対応するセル用オブジェクトフォルダも消す。 */
static void RemoveReferenceCellObjectFolder(const char *sourcePath)
{
#ifdef _WIN32
    const char *reference;
    int row, column;
    char path[1200];
    RpgObjectFolder folder;
    if (sourcePath == NULL || (reference = strstr(sourcePath, "\\reference_files\\reference_r")) == NULL ||
        sscanf(reference, "\\reference_files\\reference_r%d_c%d", &row, &column) != 2 ||
        row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return;
    folder.cell = (RpgGridCell){ row, column };
    if (BlockPath(&folder, 0, false, path, sizeof(path))) RemoveTree(path);
#else
    (void)sourcePath;
#endif
}

static void RemoveStoredSourceObjectFolder(const char *sourcePath)
{
#ifdef _WIN32
    char parent[1200];
    char *separator;
    if (sourcePath == NULL || activeBuildPath[0] == '\0' ||
        strncmp(sourcePath, activeBuildPath, strlen(activeBuildPath)) != 0 ||
        (strstr(sourcePath, "\\drops\\") == NULL && strstr(sourcePath, "\\reference_files\\") == NULL) ||
        snprintf(parent, sizeof(parent), "%s", sourcePath) <= 0 || (separator = strrchr(parent, '\\')) == NULL) return;
    RemoveReferenceCellObjectFolder(sourcePath);
    *separator = '\0';
    RemoveTree(parent);
#else
    (void)sourcePath;
#endif
}

/* 同名の別ファイルには Explorer と同じく番号を付け、既存ファイルを上書きしない。 */
static bool MakeStoredFilePath(const char *sourcePath, const char *destinationDirectory,
                               char *destination, size_t destinationSize)
{
#ifdef _WIN32
    const char *fileName, *extension;
    wchar_t wideSource[1200], wideDestination[1200];
    DWORD sourceAttributes;
    fileName = strrchr(sourcePath, '\\');
    if (fileName == NULL) fileName = strrchr(sourcePath, '/');
    fileName = fileName == NULL ? sourcePath : fileName + 1;
    extension = strrchr(fileName, '.');
    if (extension == fileName) extension = NULL;
    if (!ToWide(sourcePath, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0])))) return false;
    sourceAttributes = GetFileAttributesW(wideSource);
    if (sourceAttributes == INVALID_FILE_ATTRIBUTES || (sourceAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    for (int serial = 1; serial < 10000; serial++) {
        int written;
        if (serial == 1) written = snprintf(destination, destinationSize, "%s\\%s", destinationDirectory, fileName);
        else if (extension != NULL) written = snprintf(destination, destinationSize, "%s\\%.*s (%d)%s",
                                                       destinationDirectory, (int)(extension - fileName), fileName,
                                                       serial, extension);
        else written = snprintf(destination, destinationSize, "%s\\%s (%d)", destinationDirectory, fileName, serial);
        if (written <= 0 || (size_t)written >= destinationSize ||
            !ToWide(destination, wideDestination, (int)(sizeof(wideDestination) / sizeof(wideDestination[0])))) return false;
        if (GetFileAttributesW(wideDestination) == INVALID_FILE_ATTRIBUTES) return true;
        if (AreFilesEqual(wideSource, wideDestination)) return true;
    }
#else
    (void)sourcePath;
    (void)destinationDirectory;
    (void)destination;
    (void)destinationSize;
#endif
    return false;
}

/* 一時ファイルへコピーして検証してから改名するため、失敗時に中途半端な格納を残さない。 */
bool RpgObjectFolder_StoreFileInDirectory(const char *sourcePath, const char *destinationDirectory)
{
#ifdef _WIN32
    char destination[1200];
    wchar_t wideSource[1200], wideDestination[1200];
    if (sourcePath == NULL || destinationDirectory == NULL || sourcePath[0] == '\0' ||
        destinationDirectory[0] == '\0' || !EnsureRuntimeParentFolder(destinationDirectory) ||
        !CreateFolderUtf8(destinationDirectory) ||
        !MakeStoredFilePath(sourcePath, destinationDirectory, destination, sizeof(destination))) return false;
    if (strcmp(sourcePath, destination) == 0) return true;
    if (!ToWide(sourcePath, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0])) ) ||
        !ToWide(destination, wideDestination, (int)(sizeof(wideDestination) / sizeof(wideDestination[0])) )) return false;
    /* 同一内容が既にある場合だけ、コピーなしで完了として扱う。 */
    if (GetFileAttributesW(wideDestination) != INVALID_FILE_ATTRIBUTES) {
        if (!AreFilesEqual(wideSource, wideDestination) || DeleteFileW(wideSource) == 0) return false;
        RemoveStoredSourceObjectFolder(sourcePath);
        /* A key-door/file insertion can be followed immediately by Stop,
           before ReadDirectoryChangesW has delivered its notification. */
        MarkEditorPreviewPathMutation(destinationDirectory);
        RpgObjectFolderMonitor_NotifyLocalChange(destinationDirectory);
        return true;
    }
    if (MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) == 0) return false;
    RemoveStoredSourceObjectFolder(sourcePath);
    NotifyShellChange(RPG_SHCNE_RENAMEITEM, sourcePath, destination);
    NotifyShellParentChanged(sourcePath);
    NotifyShellParentChanged(destination);
    MarkEditorPreviewPathMutation(destinationDirectory);
    RpgObjectFolderMonitor_NotifyLocalChange(destinationDirectory);
    return true;
#else
    (void)sourcePath;
    (void)destinationDirectory;
    return false;
#endif
}

void RpgObjectFolder_PrepareZipperAnimationCommand(void)
{
#ifdef _WIN32
    char inbox[1200], command[1200], script[1200], legacy[1200];
    wchar_t wideCommand[1200], wideScript[1200], wideLegacy[1200]; FILE *file;
    if (!GetInboxPath(inbox, sizeof(inbox)) || !CreateFolderUtf8(inbox) ||
        snprintf(command, sizeof(command), "%s\\eat.cmd", inbox) <= 0 ||
        snprintf(script, sizeof(script), "%s\\eat.vbs", inbox) <= 0 ||
        !ToWide(command, wideCommand, 1200) || !ToWide(script, wideScript, 1200)) return;
    /* Remove direct leftovers from builds that used the old command name. */
    if (snprintf(legacy, sizeof(legacy), "%s\\assyuku.cmd", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    if (snprintf(legacy, sizeof(legacy), "%s\\assyuku.vbs", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    if (snprintf(legacy, sizeof(legacy), "%s\\assyuku.request", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    if (snprintf(legacy, sizeof(legacy), "%s\\eat.request", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    if (snprintf(legacy, sizeof(legacy), "%s\\spit.request", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    if (snprintf(legacy, sizeof(legacy), "%s\\zipper_command.request", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    file = _wfopen(wideCommand, L"wb");
    if (file != NULL) {
        /* cmdは既存操作との互換用。ダブルクリック時のコンソール表示を避けるには.vbsを使う。 */
        fputs("@echo off\r\ndel /f /q \"%~dp0zipper_command.request\" >nul 2>nul\r\n"
               "> \"%~dp0zipper_command.request\" echo eat %DATE%_%TIME%_%RANDOM%_%RANDOM%\r\n", file);
        if (fclose(file) == 0) {
            NotifyShellChange(RPG_SHCNE_CREATE, command, NULL);
            NotifyShellPathHierarchyChanged(command);
        }
    }
    file = _wfopen(wideScript, L"wb");
    if (file != NULL) {
        /* WScriptで実行されるため、同じ要求をコンソールなしで発行できる。 */
        fputs("Randomize\r\n"
              "Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n"
              "request = fso.BuildPath(fso.GetParentFolderName(WScript.ScriptFullName), \"zipper_command.request\")\r\n"
              "If fso.FileExists(request) Then fso.DeleteFile request, True\r\n"
              "Set output = fso.CreateTextFile(request, True)\r\n"
              "output.WriteLine \"eat \" & Timer & \"_\" & Rnd & \"_\" & Rnd\r\n"
              "output.Close\r\n", file);
        if (fclose(file) == 0) {
            NotifyShellChange(RPG_SHCNE_CREATE, script, NULL);
            NotifyShellPathHierarchyChanged(script);
        }
    }
    if (snprintf(command, sizeof(command), "%s\\spit.cmd", inbox) > 0 &&
        snprintf(script, sizeof(script), "%s\\spit.vbs", inbox) > 0 &&
        ToWide(command, wideCommand, 1200) && ToWide(script, wideScript, 1200)) {
        file = _wfopen(wideCommand, L"wb");
        if (file != NULL) {
            fputs("@echo off\r\ndel /f /q \"%~dp0zipper_command.request\" >nul 2>nul\r\n"
                   "> \"%~dp0zipper_command.request\" echo spit %DATE%_%TIME%_%RANDOM%_%RANDOM%\r\n", file);
            if (fclose(file) == 0) {
                NotifyShellChange(RPG_SHCNE_CREATE, command, NULL);
                NotifyShellPathHierarchyChanged(command);
            }
        }
        file = _wfopen(wideScript, L"wb");
        if (file != NULL) {
            fputs("Randomize\r\n"
                  "Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n"
                  "request = fso.BuildPath(fso.GetParentFolderName(WScript.ScriptFullName), \"zipper_command.request\")\r\n"
                  "If fso.FileExists(request) Then fso.DeleteFile request, True\r\n"
                  "Set output = fso.CreateTextFile(request, True)\r\n"
                  "output.WriteLine \"spit \" & Timer & \"_\" & Rnd & \"_\" & Rnd\r\n"
                  "output.Close\r\n", file);
            if (fclose(file) == 0) {
                NotifyShellChange(RPG_SHCNE_CREATE, script, NULL);
                NotifyShellPathHierarchyChanged(script);
            }
        }
    }
#endif
}

bool RpgObjectFolder_BeginZipperCommandRequest(void)
{
#ifdef _WIN32
    char inbox[1200], request[1200], token[256] = { 0 }, action[16] = { 0 };
    wchar_t wideRequest[1200]; FILE *file;
    if (!GetInboxPath(inbox, sizeof(inbox)) || snprintf(request, sizeof(request), "%s\\zipper_command.request", inbox) <= 0 ||
        !ToWide(request, wideRequest, 1200)) {
        hasDispatchedZipperRequest = false;
        hasPendingZipperRequest = false;
        pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
        return false;
    }
    /* 完了後に削除が遅れても、同一の書込み要求は再実行しない。 */
    file = _wfopen(wideRequest, L"rb");
    if (file == NULL) {
        hasPendingZipperRequest = false;
        pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
        return false;
    }
    (void)fgets(token, (int)sizeof(token), file);
    fclose(file);
    token[strcspn(token, "\r\n")] = '\0';
    if (sscanf(token, "%15s", action) != 1 ||
        (hasDispatchedZipperRequest && strcmp(token, dispatchedZipperRequestToken) == 0)) return false;
    if (_stricmp(action, "eat") == 0 || _stricmp(action, "animate") == 0)
        pendingZipperCommand = RPG_ZIPPER_COMMAND_EAT;
    else if (_stricmp(action, "spit") == 0)
        pendingZipperCommand = RPG_ZIPPER_COMMAND_SPIT;
    else return false;
    snprintf(pendingZipperRequestToken, sizeof(pendingZipperRequestToken), "%s", token);
    hasPendingZipperRequest = true;
    return true;
#else
    return false;
#endif
}

RpgZipperCommandRequest RpgObjectFolder_GetPendingZipperCommandRequest(void)
{
    return hasPendingZipperRequest ? pendingZipperCommand : RPG_ZIPPER_COMMAND_NONE;
}

bool RpgObjectFolder_CompleteZipperCommandRequest(void)
{
#ifdef _WIN32
    char inbox[1200], request[1200]; wchar_t wideRequest[1200];
    if (!hasPendingZipperRequest) return false;
    /* 取り込み成功を完了条件にする。削除失敗は次回cmd実行時に置換するため、
       Shellの一時ロックでアニメーションまで失われない。 */
    snprintf(dispatchedZipperRequestToken, sizeof(dispatchedZipperRequestToken), "%s", pendingZipperRequestToken);
    hasDispatchedZipperRequest = true;
    hasPendingZipperRequest = false;
    pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
    if (!GetInboxPath(inbox, sizeof(inbox)) || snprintf(request, sizeof(request), "%s\\zipper_command.request", inbox) <= 0 ||
        !ToWide(request, wideRequest, 1200)) return true;
    if (DeleteFileW(wideRequest) != 0) {
        /* 一時要求はExplorerへ見せる対象でないため、重い同期通知は送らない。 */
    }
    return true;
#else
    return false;
#endif
}

/* A movable file is represented on disk by the directory containing its
 * materialized file (movables/reference_files/reference_<id>).  Keep that
 * directory as the transfer unit; moving only the leaf file would lose the
 * object boundary and makes repeated eat/spit ambiguous. */
static bool ReferenceObjectDirectory(const RpgReferenceObject *object, bool inZipper,
                                     char *path, size_t pathSize)
{
#ifdef _WIN32
    char sourceDirectory[1200], zipperDirectory[1200];
    const char *separator;
    /* `object->path` is a disposable runtime cache.  Older/static records can
       contain only a display name (for example "Folder_9").  Resolving that
       relative name here makes Win32 use the executable's current directory
       and leaks the object beside build/.  Every transfer must instead begin
       at the active Stage runtime tree. */
    if (object == NULL || path == NULL || pathSize == 0 || activeBuildPath[0] == '\0') return false;
    if (object->objectKind == RPG_REFERENCE_OBJECT_FILE) {
        char referenceFiles[1200];
        if (object->id <= 0 || !GetReferenceFilesPath(referenceFiles, sizeof(referenceFiles)) ||
            snprintf(sourceDirectory, sizeof(sourceDirectory), "%s\\reference_%04d",
                     referenceFiles, object->id) <= 0) return false;
    } else if (object->objectKind == RPG_REFERENCE_OBJECT_FOLDER) {
        if (!GetReferenceObjectFolderRuntimePath(object, sourceDirectory,
                                                 sizeof(sourceDirectory))) return false;
    } else return false;
    if (!inZipper) return snprintf(path, pathSize, "%s", sourceDirectory) > 0;
    separator = strrchr(sourceDirectory, '\\');
    if (separator == NULL || separator[1] == '\0' || !GetInboxPath(zipperDirectory, sizeof(zipperDirectory)))
        return false;
    return snprintf(path, pathSize, "%s\\%s", zipperDirectory, separator + 1) > 0;
#else
    (void)object; (void)inZipper; (void)path; (void)pathSize;
    return false;
#endif
}

bool RpgObjectFolder_MoveReferenceObjectToZipper(const RpgReferenceObject *object)
{
#ifdef _WIN32
    char source[1200], zipperPath[1200], zipperDirectory[1200];
    bool moved = ReferenceObjectDirectory(object, false, source, sizeof(source)) &&
                 ReferenceObjectDirectory(object, true, zipperPath, sizeof(zipperPath)) &&
                 GetInboxPath(zipperDirectory, sizeof(zipperDirectory)) && CreateFolderUtf8(zipperDirectory) &&
                 (FolderExistsUtf8(zipperPath) || MoveDirectory(source, zipperPath));
    if (moved) MarkEditorPreviewPositionMutation(object->position);
    return moved;
#else
    (void)object;
    return false;
#endif
}

bool RpgObjectFolder_MoveReferenceFileToZipper(const RpgReferenceObject *object)
{
    return object != NULL && object->objectKind == RPG_REFERENCE_OBJECT_FILE &&
           RpgObjectFolder_MoveReferenceObjectToZipper(object);
}

bool RpgObjectFolder_MoveDataShotToZipper(RpgDataShot *shot)
{
#ifdef _WIN32
    char source[1200], inbox[1200], parent[1200];
    if (!EnsureDataShot(shot, NULL)) return false;
    (void)WriteDataShotRuntimeMetadata(shot);
    return DataShotPath(shot, false, source, sizeof(source)) &&
           DataShotPath(shot, true, inbox, sizeof(inbox)) && GetInboxPath(parent, sizeof(parent)) && CreateFolderUtf8(parent) &&
           (FolderExistsUtf8(inbox) || MoveDirectory(source, inbox));
#else
    (void)shot; return false;
#endif
}

bool RpgObjectFolder_MoveBlockToZipper(const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    bool moved = (blockType != 0 || activeBuildPath[0] != '\0') &&
                 MaterializeBlockInInbox(folder, blockType);
    if (moved && folder != NULL)
        RpgObjectFolders_MarkEditorPreviewCellMutation(folder->cell, blockType);
    return moved;
#else
    (void)folder; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_MoveBlockWithAttachmentsToZipper(const RpgObjectFolder *folder, int blockType,
                                                       const RpgAttachments *attachments,
                                                       const RpgReceivers *receivers,
                                                       const RpgWires *wires)
{
#ifdef _WIN32
    bool moved = (blockType != 0 || activeBuildPath[0] != '\0') &&
                 MaterializeBlockWithAttachmentsInInbox(folder, blockType, attachments, receivers, wires);
    if (moved && folder != NULL)
        RpgObjectFolders_MarkEditorPreviewCellMutation(folder->cell, blockType);
    return moved;
#else
    (void)folder; (void)blockType; (void)attachments; (void)receivers; (void)wires;
    return false;
#endif
}

bool RpgObjectFolder_BeginReturnDataShotFromZipper(const RpgDataShot *shot)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    return activeBuildPath[0] != '\0' && DataShotPath(shot, false, source, sizeof(source)) &&
           DataShotPath(shot, true, inbox, sizeof(inbox)) && BeginFolderReturn(inbox, source);
#else
    (void)shot; return false;
#endif
}

bool RpgObjectFolder_BeginReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    return activeBuildPath[0] != '\0' && BlockPath(folder, blockType, false, source, sizeof(source)) &&
           BlockPath(folder, blockType, true, inbox, sizeof(inbox)) && BeginFolderReturn(inbox, source);
#else
    (void)folder; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_BeginReturnReferenceFileFromZipper(const RpgReferenceObject *object)
{
    return object != NULL && object->objectKind == RPG_REFERENCE_OBJECT_FILE &&
           RpgObjectFolder_BeginReturnReferenceObjectFromZipper(object);
}

bool RpgObjectFolder_BeginReturnReferenceObjectFromZipper(const RpgReferenceObject *object)
{
#ifdef _WIN32
    char source[1200], zipperPath[1200];
    return activeBuildPath[0] != '\0' &&
           ReferenceObjectDirectory(object, false, source, sizeof(source)) &&
           ReferenceObjectDirectory(object, true, zipperPath, sizeof(zipperPath)) &&
           BeginFolderReturn(zipperPath, source);
#else
    (void)object;
    return false;
#endif
}

bool RpgObjectFolder_ReturnDataShotFromZipper(const RpgDataShot *shot)
{
#ifdef _WIN32
    char source[1200], inbox[1200], objects[1200];
    return activeBuildPath[0] != '\0' && DataShotPath(shot, false, source, sizeof(source)) && DataShotPath(shot, true, inbox, sizeof(inbox)) &&
           GetObjectsPath(objects, sizeof(objects)) && CreateFolderUtf8(objects) && CompleteFolderReturn(inbox, source);
#else
    (void)shot; return false;
#endif
}

bool RpgObjectFolder_RestoreDataShotFromMetadata(RpgDataShot *shot)
{
#ifdef _WIN32
    char source[1200], infoPath[1200], specificInfoPath[1200];
    wchar_t wideInfoPath[1200];
    int type = -1, attachmentIndex = -1, pathCellIndex = -1;
    float worldX = 0.0f, worldY = 0.0f;
    FILE *file;
    if (shot == NULL || !DataShotPath(shot, false, source, sizeof(source)) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", source) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"rb");
    if (file == NULL) return false;
    char line[256];
    bool haveX = false, haveY = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "type=%d", &type) == 1) continue;
        if (sscanf(line, "x=%f", &worldX) == 1) { haveX = true; continue; }
        if (sscanf(line, "y=%f", &worldY) == 1) { haveY = true; continue; }
    }
    fclose(file);
    if (!haveX || !haveY || type != 0 ||
        snprintf(specificInfoPath, sizeof(specificInfoPath), "%s\\data_shot_info.txt", source) <= 0 ||
        !ToWide(specificInfoPath, wideInfoPath, 1200) ||
        (file = _wfopen(wideInfoPath, L"rb")) == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        (void)sscanf(line, "attachment_index=%d", &attachmentIndex);
        (void)sscanf(line, "path_cell_index=%d", &pathCellIndex);
    }
    fclose(file);
    const int row = (int)(worldY / RPG_STAGE_TILE_SIZE);
    const int column = (int)(worldX / RPG_STAGE_TILE_SIZE);
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    shot->position = (Vector2){ worldX, worldY };
    shot->metadataCell = (RpgGridCell){ row, column };
    shot->attachmentIndex = attachmentIndex;
    shot->pathCellIndex = pathCellIndex;
    return true;
#else
    (void)shot; return false;
#endif
}

bool RpgObjectFolder_ReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    char source[1200], inbox[1200], cells[1200];
    if (activeBuildPath[0] == '\0' || !BlockPath(folder, blockType, false, source, sizeof(source)) ||
        !BlockPath(folder, blockType, true, inbox, sizeof(inbox))) return false;
    /* Compact方式でも、取得済みマスはcells内の実フォルダのまま返して読み取る。 */
    bool returned = GetCellsPath(cells, sizeof(cells)) && CreateFolderUtf8(cells) &&
                    CompleteFolderReturn(inbox, source);
    if (returned && folder != NULL)
        RpgObjectFolders_MarkEditorPreviewCellMutation(folder->cell, blockType);
    return returned;
#else
    (void)folder; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_ReturnReferenceFileFromZipper(const RpgReferenceObject *object)
{
    return object != NULL && object->objectKind == RPG_REFERENCE_OBJECT_FILE &&
           RpgObjectFolder_ReturnReferenceObjectFromZipper(object);
}

bool RpgObjectFolder_ReturnReferenceObjectFromZipper(const RpgReferenceObject *object)
{
#ifdef _WIN32
    char source[1200], zipperPath[1200], parent[1200];
    bool hasParent = object != NULL &&
        (object->objectKind == RPG_REFERENCE_OBJECT_FILE ?
             GetReferenceFilesPath(parent, sizeof(parent)) :
         object->objectKind == RPG_REFERENCE_OBJECT_FOLDER ?
             GetReferenceFoldersPath(parent, sizeof(parent)) : false);
    bool returned = activeBuildPath[0] != '\0' &&
                    hasParent && CreateFolderUtf8(parent) &&
                    ReferenceObjectDirectory(object, false, source, sizeof(source)) &&
                    ReferenceObjectDirectory(object, true, zipperPath, sizeof(zipperPath)) &&
                    CompleteFolderReturn(zipperPath, source);
    if (returned) MarkEditorPreviewPositionMutation(object->position);
    return returned;
#else
    (void)object;
    return false;
#endif
}

bool RpgObjectFolder_EnsureDynamicBlock(RpgGridCell identityCell, int blockType, Vector2 position)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    if (!DynamicBlockPath(identityCell, blockType, false, source, sizeof(source)) ||
        !DynamicBlockPath(identityCell, blockType, true, inbox, sizeof(inbox))) return false;
    if (FolderExistsUtf8(source) || FolderExistsUtf8(inbox)) return true;
    return WriteObjectInfo(source, blockType, position);
#else
    (void)identityCell; (void)blockType; (void)position; return false;
#endif
}

bool RpgObjectFolder_MoveDynamicBlockToZipper(RpgGridCell identityCell, int blockType, Vector2 position)
{
#ifdef _WIN32
    char source[1200], inbox[1200], parent[1200];
    bool moved;
    if (!RpgObjectFolder_EnsureDynamicBlock(identityCell, blockType, position) ||
        !DynamicBlockPath(identityCell, blockType, false, source, sizeof(source)) ||
        !DynamicBlockPath(identityCell, blockType, true, inbox, sizeof(inbox)) ||
        !GetInboxPath(parent, sizeof(parent)) || !CreateFolderUtf8(parent) ||
        !WriteObjectInfo(source, blockType, position)) return false;
    moved = FolderExistsUtf8(inbox) || MoveDirectory(source, inbox);
    /* Dynamic objects are not authoring data, but an unreturned captured
       folder still needs reconstruction before the editor preview is reused. */
    if (moved) RpgObjectFolders_MarkEditorPreviewCellMutation(identityCell, blockType);
    return moved;
#else
    (void)identityCell; (void)blockType; (void)position; return false;
#endif
}

bool RpgObjectFolder_BeginReturnDynamicBlockFromZipper(RpgGridCell identityCell, int blockType,
                                                        Vector2 position)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    if (activeBuildPath[0] == '\0' ||
        !DynamicBlockPath(identityCell, blockType, false, source, sizeof(source)) ||
        !DynamicBlockPath(identityCell, blockType, true, inbox, sizeof(inbox)) ||
        !WriteObjectInfo(inbox, blockType, position)) return false;
    return BeginFolderReturn(inbox, source);
#else
    (void)identityCell; (void)blockType; (void)position; return false;
#endif
}

bool RpgObjectFolder_ReturnDynamicBlockFromZipper(RpgGridCell identityCell, int blockType,
                                                   Vector2 position)
{
#ifdef _WIN32
    char source[1200], inbox[1200], staging[1200];
    bool returned;
    if (activeBuildPath[0] == '\0' ||
        !DynamicBlockPath(identityCell, blockType, false, source, sizeof(source)) ||
        !DynamicBlockPath(identityCell, blockType, true, inbox, sizeof(inbox)) ||
        !GetReturnStagingPath(source, staging, sizeof(staging)) ||
        !WriteObjectInfo(staging, blockType, position)) return false;
    returned = CompleteFolderReturn(inbox, source);
    if (returned) RpgObjectFolders_MarkEditorPreviewCellMutation(identityCell, blockType);
    return returned;
#else
    (void)identityCell; (void)blockType; (void)position; return false;
#endif
}

void RpgObjectFolders_PrepareBlockOwnedMetadata(const RpgAttachments *attachments,
                                                const RpgReceivers *receivers,
                                                const RpgWires *wires)
{
#ifdef _WIN32
    if (attachments != NULL) for (int index = 0; index < attachments->count; index++) {
        /* A parent held by Zipper owns this metadata in Inbox.  Never rebuild
           a second child file at the absent stage location. */
        if (!RpgAttachments_IsRuntimeUnavailable(&attachments->entries[index]))
            (void)EnsureAttachment(&attachments->entries[index]);
    }
    if (receivers != NULL) for (int index = 0; index < RpgReceivers_Count(receivers); index++) {
        RpgReceiver receiver;
        if (RpgReceivers_Get(receivers, index, &receiver) &&
            !RpgReceivers_IsRuntimeUnavailable(&receiver))
            (void)EnsureReceiver(&receiver, wires);
    }
#else
    (void)attachments; (void)receivers; (void)wires;
#endif
}

void RpgObjectFolders_PrepareReferenceFolderMetadata(const RpgStage *stage)
{
    if (stage == NULL) return;
    for (int index = 0; index < stage->referenceObjects.count; index++) {
        const RpgReferenceObject *object = &stage->referenceObjects.entries[index];
        int row, column;
        if (object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
            !RpgStage_GetWorldCellAtPosition(stage, object->position, &row, &column)) continue;
        (void)WriteRuntimeReferenceObjectFolderInfo(object, (RpgGridCell){ row, column });
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column])) continue;
        (void)WriteReferenceFolderInfo(RpgStage_GetReferencePathAtCell(stage, row, column),
                                       (RpgGridCell){ row, column });
    }
}

void RpgObjectFolders_PrepareImageObjectFolders(const RpgImageObjects *objects)
{
#ifdef _WIN32
    if (objects == NULL) return;
    for (int index = 0; index < objects->count; index++) (void)EnsureImageObject(&objects->entries[index]);
#else
    (void)objects;
#endif
}

void RpgObjectFolders_UpdateDataShotLifetimes(RpgDataShots *shots, const RpgAttachments *attachments,
                                              RpgReferenceObjects *referenceObjects)
{
#ifdef _WIN32
    if (shots == NULL) return;
    RpgObjectFolderMonitor_PollAll();
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        RpgDataShot *shot = &shots->entries[index];
        // プレビュー弾は設定確認専用であり、実フォルダや File.png を生成・参照しない。
        if (shot->isPreview || shot->isZipperHeld) continue;
        int serial = shot->active ? shot->folderSerial : 0;
        if (serial == dataShotFolderSerials[index]) {
            if (serial > 0 && EnsureDataShot(shot, attachments)) {
                (void)WriteDataShotRuntimeMetadata(shot);
                UpdateDataShotProperties(index, shot, attachments);
            }
            continue;
        }
        if (dataShotFolderSerials[index] > 0) {
            RpgDataShot expired = *shot;
            expired.folderSerial = dataShotFolderSerials[index];
            char source[1200], inbox[1200];
            if (DataShotPath(&expired, false, source, sizeof(source)) && DataShotPath(&expired, true, inbox, sizeof(inbox)))
                ReleaseDestroyedDataShotFolder(source, inbox, shot->hitWall ? shot->impactPosition : shot->position,
                                                &expired, referenceObjects);
        }
        dataShotFolderSerials[index] = serial;
        RpgObjectFolderMonitor_Disconnect(&shot->folderMonitorHandle);
        if (serial > 0 && EnsureDataShot(shot, attachments)) {
            (void)WriteDataShotRuntimeMetadata(shot);
            UpdateDataShotProperties(index, shot, attachments);
        }
    }
#else
    (void)shots; (void)attachments; (void)referenceObjects;
#endif
}

bool RpgObjectFolder_AttachmentHasLinkedFiles(const RpgAttachment *attachment)
{
    (void)attachment; return false;
}

bool RpgObjectFolder_DataShotHasLinkedFiles(const RpgDataShot *shot)
{
    return shot != NULL && shot->hasAddedFiles;
}

bool RpgObjectFolder_BlockHasLinkedFiles(const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    /* 圧縮済みの空気・通常ブロックには個別フォルダがないため、描画ごとのファイル照会を行わない。 */
    if (activeBuildPath[0] != '\0' && RpgObjectFolders_UsesCompactCellMetadata(blockType)) return false;
    if (activeBuildPath[0] != '\0' && folder != NULL && folder->cell.row >= 0 && folder->cell.row < RPG_STAGE_ROWS &&
        folder->cell.column >= 0 && folder->cell.column < RPG_STAGE_WORLD_COLUMNS)
        return buildCellLinkedFiles[folder->cell.row][folder->cell.column];
    return (blockType != 0 || activeBuildPath[0] != '\0') && ((BlockPath(folder, blockType, false, source, sizeof(source)) && HasExternalFiles(source)) ||
                              (BlockPath(folder, blockType, true, inbox, sizeof(inbox)) && HasExternalFiles(inbox)));
#else
    (void)folder; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_HasLinkedFiles(const RpgObjectFolder *folder) { (void)folder; return false; }

bool RpgObjectFolder_MoveAttachmentFolder(const RpgAttachment *from, const RpgAttachment *to)
{
#ifdef _WIN32
    char source[1200];
    wchar_t wideSource[1200];
    if (from == NULL || to == NULL || !AttachmentPath(from, false, source, sizeof(source))) return false;
    if (ToWide(source, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0]))))
        (void)DeleteFileW(wideSource);
    return EnsureAttachment(to);
#else
    (void)from; (void)to; return false;
#endif
}

void RpgObjectFolder_RemoveAttachmentFolder(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char source[1200];
    wchar_t wideSource[1200];
    if (AttachmentPath(attachment, false, source, sizeof(source)) &&
        ToWide(source, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0]))))
        (void)DeleteFileW(wideSource);
#else
    (void)attachment;
#endif
}

bool RpgObjectFolders_BeginStageBuild(int stageNumber, RpgStage *stage,
                                      const RpgAttachments *attachments,
                                      const RpgReceivers *receivers,
                                      const RpgWires *wires, Vector2 playerStartPosition,
                                      bool isSimpleBuild,
                                      char *buildPath, size_t buildPathSize)
{
#ifdef _WIN32
    char cells[1200], objects[1200], drops[1200];
    if (stageNumber <= 0 || stage == NULL || buildPath == NULL || buildPathSize == 0) return false;
    RpgStageRuntimeKind kind = isSimpleBuild ? RPG_STAGE_RUNTIME_EDITOR : RPG_STAGE_RUNTIME_GAME;
    if (!RpgStageStorage_GetRuntimePath(stageNumber, kind, buildPath, (int)buildPathSize)) return false;

    RpgObjectFolders_EndStageBuild();
    isBulkBuildOperation = true;
    RpgObjectFolders_ClearSessionStorage();
    /* build直下の選択済みReference File/Folderは保持し、生成物だけを再構築する。 */
    /* 前回の成果物が一つでも残った状態で初期化を続けると世代が混ざるため、完全削除できない場合は失敗にする。 */
    snprintf(activeBuildPath, sizeof(activeBuildPath), "%s", buildPath);
    activeBuildStageNumber = stageNumber;
    activeBuildPersists = !isSimpleBuild;
    generationShellNotifyFrameCount = 0;
    /* This is the final boundary before a disposable runtime path replaces
       object->path. Preserve/repair the static file identity first. */
    RpgReferenceObjects_EnsureSourcePaths(&stage->referenceObjects);
    RpgStage_NormalizeMagnets(stage);
    RpgStage_NormalizeCompositeObjects(stage);
    SetBuildCellOwners(stage);
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);
    if (!EnsureRuntimeParentFolder(activeBuildPath) || !CreateFolderUtf8(activeBuildPath) ||
        !GetCellsPath(cells, sizeof(cells)) ||
        !GetObjectsPath(objects, sizeof(objects)) || !GetDropsPath(drops, sizeof(drops))) {
        activeBuildPath[0] = '\0'; activeZipperPath[0] = '\0'; RpgExplorerLauncher_SetZipperDirectory(NULL);
        isBulkBuildOperation = false;
        return false;
    }
    if (!EnsureRuntimeParentFolder(activeBuildPath) || !CreateFolderUtf8(activeBuildPath) ||
        !ClearStageRuntimeArtifacts(activeBuildPath)) {
        activeBuildPath[0] = '\0'; activeZipperPath[0] = '\0'; RpgExplorerLauncher_SetZipperDirectory(NULL);
        isBulkBuildOperation = false; return false;
    }
    if (!CreateFolderUtf8(cells) || !CreateFolderUtf8(objects)) {
        activeBuildPath[0] = '\0'; activeZipperPath[0] = '\0'; RpgExplorerLauncher_SetZipperDirectory(NULL);
        isBulkBuildOperation = false;
        return false;
    }
    int playerStartMap = (int)(playerStartPosition.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
    if (playerStartMap < 0) playerStartMap = 0;
    if (playerStartMap >= RPG_STAGE_MAP_COUNT) playerStartMap = RPG_STAGE_MAP_COUNT - 1;
    RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
#if 0 /* Full synchronous editor-preview route: retain for an immediate rollback. */
    if (!RpgBuildCellStorage_Create(stage, playerStartMap, &storageBackend)) {
#else
    if (!(isSimpleBuild ? RpgBuildCellStorage_CreatePreview(stage, playerStartMap, &storageBackend) :
                          RpgBuildCellStorage_Create(stage, playerStartMap, &storageBackend))) {
#endif
        RpgObjectFolders_EndStageBuild();
        isBulkBuildOperation = false;
        return false;
    }
    if (isSimpleBuild) {
        /* Stop returns the editor quickly with the playable local cross ready.
           The queued maps are completed by editor frames afterwards. */
        if (!RpgObjectFolders_EnsurePreviewNeighborhood(stage, playerStartMap)) {
            RpgObjectFolders_EndStageBuild(); isBulkBuildOperation = false; return false;
        }
    } else {
        /* Every cell is generated in its normal direct form first.  Only after
           generation do representative folders absorb their non-root components. */
        if (!CollectEffectComponentFolders(stage, -1)) {
            RpgObjectFolders_EndStageBuild();
            isBulkBuildOperation = false;
            return false;
        }
        PrepareRuntimeReferenceFiles(stage, -1);
        /* Folder は設計側では名前だけを持ち、実行時の実体はこの動的ディレクトリへ作る。 */
        if (!PrepareRuntimeReferenceFolders(stage, -1)) {
            RpgObjectFolders_EndStageBuild(); isBulkBuildOperation = false; return false;
        }
    }
    RpgObjectFolders_PrepareBlockOwnedMetadata(attachments, receivers, wires);
    RpgObjectFolders_PrepareReferenceFolderMetadata(stage);
    RpgObjectFolders_PrepareImageObjectFolders(&stage->imageObjects);
    if (!EnsurePlayerFolder(playerStartPosition)) {
        RpgObjectFolders_EndStageBuild();
        isBulkBuildOperation = false;
        return false;
    }
    RpgObjectFolder_PrepareZipperAnimationCommand();
    isBulkBuildOperation = false;
    /* 生成後は build ルートと親だけを一度更新通知する。 */
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    NotifyShellParentChanged(activeBuildPath);
    return true;
#else
    (void)stageNumber; (void)stage; (void)attachments; (void)playerStartPosition; (void)isSimpleBuild; (void)buildPath; (void)buildPathSize;
    return false;
#endif
}

bool RpgObjectFolders_IsStageBuildActive(void) { return activeBuildPath[0] != '\0'; }

static bool ResumeStageBuildForKind(int stageNumber, RpgStage *stage, RpgStageRuntimeKind runtimeKind,
                                    char *buildPath, size_t buildPathSize)
{
#ifdef _WIN32
    char zipperPath[1200], movablesPath[1200];
    RpgBuildCellStorageBackend storageBackend;
    if (stageNumber <= 0 || stage == NULL || buildPath == NULL || buildPathSize == 0 ||
        !RpgStageStorage_GetRuntimePath(stageNumber, runtimeKind, buildPath, (int)buildPathSize) ||
        !FolderExistsUtf8(buildPath)) return false;
    snprintf(activeBuildPath, sizeof(activeBuildPath), "%s", buildPath);
    activeBuildStageNumber = stageNumber;
    activeBuildPersists = runtimeKind == RPG_STAGE_RUNTIME_GAME;
    RpgReferenceObjects_EnsureSourcePaths(&stage->referenceObjects);
    RpgStage_NormalizeMagnets(stage);
    RpgStage_NormalizeCompositeObjects(stage);
    SetBuildCellOwners(stage);
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);
    /* データ弾など、マスに定着しない実行時オブジェクトは再開時に持ち越さない。 */
    (void)RpgStageStorage_RepairReferenceFileCopies(stageNumber, stage);
    PrepareRuntimeReferenceFiles(stage, -1);
    RemoveTransientDataShotFolders();
    if (GetObjectsPath(movablesPath, sizeof(movablesPath)) &&
        snprintf(zipperPath, sizeof(zipperPath), "%s\\Zipper", movablesPath) > 0 &&
        FolderExistsUtf8(zipperPath)) (void)SetActiveZipperPath(zipperPath);
    /* フォルダを取り込んだセルは cells.csv から欠損として復元する。 */
    storageBackend = GetBuildCellStorageBackend();
    (void)RpgBuildCellStorage_ApplyProgressState(stage, &storageBackend);
    /* Builds created before composite ownership stored every component beside
       its representative.  Repair that legacy shape now, before the folder
       availability pass can turn a valid key door into a missing block. */
    if (!CollectEffectComponentFolders(stage, -1)) return false;
    /* 特殊ブロックはセルごとのオブジェクトフォルダが正本。格納済みでフォルダが無ければ復活させない。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgObjectFolder folder = { .cell = { row, column } };
        char objectPath[1200];
        int blockType = stage->blocks[row][column];
        const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
        if (RpgBlockInventory_IsReferenceObject(blockType) ||
            RpgBuildCellStorage_UsesMetadataForBlock(blockType) || blockType == RPG_BLOCK_BUILD_MISSING ||
            !BlockPath(&folder, blockType, false, objectPath, sizeof(objectPath))) continue;
        /* The root directory owns every component folder.  Do not load or
           validate component cells independently. */
        if (shape != NULL && shape->rootType != blockType) continue;
        if (!FolderExistsUtf8(objectPath)) {
            if (!RpgStage_SetCompositeMissingAtCell(stage, row, column, true))
                SetStageEffectFolderMissing(stage, (RpgGridCell){ row, column }, blockType, true);
        }
    }
    return true;
#else
    (void)stageNumber; (void)stage; (void)buildPath; (void)buildPathSize;
    return false;
#endif
}

bool RpgObjectFolders_ResumeStageBuild(int stageNumber, RpgStage *stage, char *buildPath, size_t buildPathSize)
{
    return ResumeStageBuildForKind(stageNumber, stage, RPG_STAGE_RUNTIME_GAME, buildPath, buildPathSize);
}

bool RpgObjectFolders_ResumeEditorPreviewBuild(int stageNumber, RpgStage *stage,
                                                char *buildPath, size_t buildPathSize)
{
    return ResumeStageBuildForKind(stageNumber, stage, RPG_STAGE_RUNTIME_EDITOR, buildPath, buildPathSize);
}

void RpgObjectFolders_LoadReferenceDrops(RpgReferenceObjects *objects)
{
#ifdef _WIN32
    char drops[1200], search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (objects == NULL || !GetDropsPath(drops, sizeof(drops)) ||
        snprintf(search, sizeof(search), "%s\\drop_*", drops) <= 0 || !ToWide(search, wideSearch, 1200)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char folderName[512], folderPath[1200], infoPath[1200], candidate[1200], childSearch[1200];
        wchar_t wideInfoPath[1200], wideChildSearch[1200];
        FILE *info;
        WIN32_FIND_DATAW child;
        HANDLE childHandle;
        float x = 0.0f, y = 0.0f;
        char line[256];
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, folderName, sizeof(folderName), NULL, NULL) <= 0 ||
            snprintf(folderPath, sizeof(folderPath), "%s\\%s", drops, folderName) <= 0 ||
            snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folderPath) <= 0 ||
            !ToWide(infoPath, wideInfoPath, 1200) || (info = _wfopen(wideInfoPath, L"rb")) == NULL) continue;
        while (fgets(line, sizeof(line), info) != NULL) {
            (void)sscanf(line, "x=%f", &x);
            (void)sscanf(line, "y=%f", &y);
        }
        fclose(info);
        if (snprintf(childSearch, sizeof(childSearch), "%s\\*", folderPath) <= 0 ||
            !ToWide(childSearch, wideChildSearch, 1200)) continue;
        childHandle = FindFirstFileW(wideChildSearch, &child);
        if (childHandle == INVALID_HANDLE_VALUE) continue;
        candidate[0] = '\0';
        do {
            char childName[512];
            if ((child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                wcscmp(child.cFileName, L"object_info.txt") == 0 ||
                WideCharToMultiByte(CP_UTF8, 0, child.cFileName, -1, childName, sizeof(childName), NULL, NULL) <= 0) continue;
            {
                size_t folderLength = strlen(folderPath);
                size_t childLength = strlen(childName);
                if (folderLength + 1 + childLength >= sizeof(candidate)) continue;
                memcpy(candidate, folderPath, folderLength);
                candidate[folderLength] = '\\';
                memcpy(candidate + folderLength + 1, childName, childLength + 1);
            }
            break;
        } while (FindNextFileW(childHandle, &child) != 0);
        FindClose(childHandle);
        if (candidate[0] != '\0') (void)RpgReferenceObjects_AddDrop(objects, (Vector2){ x, y }, candidate);
    } while (FindNextFileW(handle, &data) != 0 && objects->count < RPG_REFERENCE_OBJECT_MAX_COUNT);
    FindClose(handle);
#else
    (void)objects;
#endif
}

void RpgObjectFolders_UpdateBuildCellGeneration(const RpgStage *stage)
{
#ifdef _WIN32
    int generated = 0, total = 0;
    bool pending = false;
    if (activeBuildPath[0] == '\0' || stage == NULL) return;
    RpgBuildCellStorage_GetGenerationProgress(stage, &generated, &total, &pending);
    if (!pending) return;
    RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
    isBulkBuildOperation = true;
    RpgBuildCellStorage_Update(&storageBackend);
    isBulkBuildOperation = false;
    RpgBuildCellStorage_GetGenerationProgress(stage, &generated, &total, &pending);
    generationShellNotifyFrameCount++;
    if (!pending || generationShellNotifyFrameCount >= 15U) {
        NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
        generationShellNotifyFrameCount = 0;
    }
#endif
}

bool RpgObjectFolders_EnsureMapGenerated(RpgStage *stage, int mapIndex)
{
#ifdef _WIN32
    RpgBuildCellStorageBackend storageBackend;
    bool generated;
    if (activeBuildPath[0] == '\0' || stage == NULL) return false;
    storageBackend = GetBuildCellStorageBackend();
    isBulkBuildOperation = true;
    generated = RpgBuildCellStorage_EnsureMap(stage, mapIndex, &storageBackend);
    if (generated) {
        generated = CollectEffectComponentFolders(stage, mapIndex);
        if (generated) {
            PrepareRuntimeReferenceFiles(stage, mapIndex);
            generated = PrepareRuntimeReferenceFolders(stage, mapIndex);
        }
    }
    isBulkBuildOperation = false;
    if (generated) NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    return generated;
#else
    (void)stage; (void)mapIndex;
    return false;
#endif
}

bool RpgObjectFolders_EnsurePreviewNeighborhood(RpgStage *stage, int startMapIndex)
{
#ifdef _WIN32
    const RpgAreaDirection directions[] = { RPG_AREA_LEFT, RPG_AREA_RIGHT, RPG_AREA_UP, RPG_AREA_DOWN };
    if (stage == NULL || !RpgStage_IsMapActive(stage, startMapIndex) ||
        !RpgObjectFolders_EnsureMapGenerated(stage, startMapIndex)) return false;
    for (int direction = 0; direction < (int)(sizeof(directions) / sizeof(directions[0])); direction++) {
        int adjacentMap = RpgStage_GetAdjacentMap(stage, startMapIndex, directions[direction]);
        if (adjacentMap >= 0 && !RpgObjectFolders_EnsureMapGenerated(stage, adjacentMap)) return false;
    }
    return true;
#else
    (void)stage; (void)startMapIndex;
    return false;
#endif
}

bool RpgObjectFolders_EnsureAllMapsGenerated(RpgStage *stage)
{
#ifdef _WIN32
    RpgBuildCellStorageBackend storageBackend;
    bool generated = true;
    bool generatedMaps[RPG_STAGE_MAP_COUNT] = { false };
    bool hasUnfinishedMaps = false;
    if (stage == NULL) return false;
    storageBackend = GetBuildCellStorageBackend();
    /* Play requires every area before it starts, but a cached area must not be
       regenerated just because another area is still pending. */
    isBulkBuildOperation = true;
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        if (!RpgStage_IsMapActive(stage, mapIndex)) continue;
        if (RpgBuildCellStorage_IsMapGenerated(stage, mapIndex)) continue;
        if (!hasUnfinishedMaps) RpgBuildCellStorage_BeginMetadataBatch();
        hasUnfinishedMaps = true;
        generatedMaps[mapIndex] = true;
        if (!RpgBuildCellStorage_EnsureMap(stage, mapIndex, &storageBackend)) {
            generated = false;
            break;
        }
    }
    if (hasUnfinishedMaps && !RpgBuildCellStorage_EndMetadataBatch(&storageBackend)) generated = false;
    if (generated && hasUnfinishedMaps) {
        /* All pending maps are materialized above.  A composite belongs to
           one object-ID directory even when it crosses area boundaries, so
           grouping it once for this batch avoids repeating the same folder
           checks and moves once per generated area. */
        generated = CollectEffectComponentFolders(stage, -1);
        for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT && generated; mapIndex++) {
            if (!generatedMaps[mapIndex]) continue;
            PrepareRuntimeReferenceFiles(stage, mapIndex);
            generated = PrepareRuntimeReferenceFolders(stage, mapIndex);
        }
    }
    isBulkBuildOperation = false;
    if (generated && hasUnfinishedMaps)
        NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    return generated;
#else
    (void)stage;
    return false;
#endif
}

bool RpgObjectFolders_RefreshEditorPreviewCompactCells(const RpgStage *stage)
{
#ifdef _WIN32
    RpgBuildCellStorageBackend storageBackend;
    int generated = 0, total = 0;
    bool pending = false;
    if (stage == NULL || activeBuildPath[0] == '\0' || activeBuildPersists) return false;
    RpgBuildCellStorage_GetGenerationProgress(stage, &generated, &total, &pending);
    if (pending || total == 0 || generated != total) return false;
    storageBackend = GetBuildCellStorageBackend();
    isBulkBuildOperation = true;
    bool updated = RpgBuildCellStorage_RewriteGeneratedMetadata(stage, &storageBackend);
    isBulkBuildOperation = false;
    if (updated) NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    return updated;
#else
    (void)stage;
    return false;
#endif
}

/* A targeted repair owns the complete runtime representation of one map.  It
 * removes only folders whose identity belongs to that map; cells and objects
 * in every other area are deliberately left untouched. */
static void RemovePreviewMapArtifacts(const RpgStage *stage, int mapIndex)
{
#ifdef _WIN32
    char root[1200];
    int removedCompositeIds[RPG_COMPOSITE_OBJECT_MAX_COUNT + RPG_MAGNET_OBJECT_MAX_COUNT] = { 0 };
    int removedCompositeCount = 0;
    const int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    const int lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    if (stage == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT) return;

    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
         column < lastColumn; column++) {
        RpgObjectFolder folder = { .cell = { row, column } };
        char path[1200];
        if (BlockPathDirect(&folder, stage->blocks[row][column], false, path, sizeof(path)))
            RemoveTree(path);
    }

    /* A composite is one object-ID folder, not a folder owned by one of its
       cells.  Removing only components/ left runtime-inserted key files at
       the group root, so a key door reopened on the next Preview Play.
       Discard each affected group once; the following map build recreates
       the static group and every component. */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
         column < lastColumn; column++) {
        const int objectId = RpgStage_GetCompositeObjectIdAtCell(stage, row, column);
        bool alreadyRemoved = false;
        if (objectId <= 0) continue;
        for (int index = 0; index < removedCompositeCount; index++)
            if (removedCompositeIds[index] == objectId) { alreadyRemoved = true; break; }
        if (alreadyRemoved) continue;
        if (removedCompositeCount >= (int)(sizeof(removedCompositeIds) / sizeof(removedCompositeIds[0]))) return;
        removedCompositeIds[removedCompositeCount++] = objectId;
        char groupPath[1200];
        if (CompositePath(objectId, false, groupPath, sizeof(groupPath))) RemoveTree(groupPath);
    }

    if (GetReferenceFilesPath(root, sizeof(root))) {
        for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
             column < lastColumn; column++) {
            char path[1200];
            if (snprintf(path, sizeof(path), "%s\\reference_r%02d_c%03d", root, row, column) > 0)
                RemoveTree(path);
        }
        for (int index = 0; index < stage->referenceObjects.count; index++) {
            const RpgReferenceObject *object = &stage->referenceObjects.entries[index];
            char path[1200];
            if (RpgStage_GetMapAtWorldPosition(stage, object->position) != mapIndex ||
                object->objectKind != RPG_REFERENCE_OBJECT_FILE) continue;
            if (snprintf(path, sizeof(path), "%s\\reference_%04d", root, object->id) > 0) RemoveTree(path);
        }
    }
    if (GetReferenceFoldersPath(root, sizeof(root))) {
        for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
             column < lastColumn; column++) {
            const char *oldPath;
            const char *name;
            char path[1200];
            if (!RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column])) continue;
            oldPath = RpgStage_GetReferencePathAtCell(stage, row, column);
            name = oldPath == NULL ? NULL : strrchr(oldPath, '\\');
            if (name == NULL && oldPath != NULL) name = strrchr(oldPath, '/');
            name = name == NULL ? oldPath : name + 1;
            /* oldPath may point at authored content; only delete the derived
               runtime directory below this preview's reference root. */
            if (name != NULL && name[0] != '\0' &&
                snprintf(path, sizeof(path), "%s\\%s", root, name) > 0) RemoveTree(path);
        }
        for (int index = 0; index < stage->referenceObjects.count; index++) {
            const RpgReferenceObject *object = &stage->referenceObjects.entries[index];
            char path[1200];
            if (RpgStage_GetMapAtWorldPosition(stage, object->position) != mapIndex ||
                object->objectKind != RPG_REFERENCE_OBJECT_FOLDER) continue;
            if (GetReferenceObjectFolderRuntimePath(object, path, sizeof(path))) RemoveTree(path);
        }
    }
#else
    (void)stage; (void)mapIndex;
#endif
}

static void PrepareBlockOwnedMetadataForMap(const RpgAttachments *attachments,
                                            const RpgReceivers *receivers,
                                            const RpgWires *wires, int mapIndex)
{
#ifdef _WIN32
    if (attachments != NULL) for (int index = 0; index < attachments->count; index++) {
        RpgGridCell owner;
        const RpgAttachment *attachment = &attachments->entries[index];
        if (!RpgAttachments_IsRuntimeUnavailable(attachment) &&
            RpgAttachments_GetOwnerBlockCell(attachment, &owner) &&
            owner.column / RPG_STAGE_COLUMNS == mapIndex)
            (void)EnsureAttachment(attachment);
    }
    if (receivers != NULL) for (int index = 0; index < RpgReceivers_Count(receivers); index++) {
        RpgReceiver receiver;
        if (RpgReceivers_Get(receivers, index, &receiver) &&
            !RpgReceivers_IsRuntimeUnavailable(&receiver) &&
            receiver.cell.column / RPG_STAGE_COLUMNS == mapIndex)
            (void)EnsureReceiver(&receiver, wires);
    }
#else
    (void)attachments; (void)receivers; (void)wires; (void)mapIndex;
#endif
}

static void PrepareReferenceFolderMetadataForMap(const RpgStage *stage, int mapIndex)
{
    if (stage == NULL) return;
    for (int index = 0; index < stage->referenceObjects.count; index++) {
        const RpgReferenceObject *object = &stage->referenceObjects.entries[index];
        int row, column;
        if (object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
            RpgStage_GetMapAtWorldPosition(stage, object->position) != mapIndex ||
            !RpgStage_GetWorldCellAtPosition(stage, object->position, &row, &column)) continue;
        (void)WriteRuntimeReferenceObjectFolderInfo(object, (RpgGridCell){ row, column });
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = mapIndex * RPG_STAGE_COLUMNS;
         column < (mapIndex + 1) * RPG_STAGE_COLUMNS; column++) {
        if (RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column]))
            (void)WriteReferenceFolderInfo(RpgStage_GetReferencePathAtCell(stage, row, column),
                                           (RpgGridCell){ row, column });
    }
}

/* A multi-cell object is represented by one root folder.  If a dirty area
 * contains any part of such an object, include only the other areas occupied
 * by that same shape; never add unrelated neighbouring areas. */
static void ExpandDirtyMapsForCompositeOwners(const RpgStage *stage,
                                              bool dirtyMaps[RPG_STAGE_MAP_COUNT])
{
    bool changed;
    if (stage == NULL || dirtyMaps == NULL) return;
    do {
        changed = false;
        for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
             column < RPG_STAGE_WORLD_COLUMNS; column++) {
            const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(stage->blocks[row][column]);
            bool touchesDirty = false;
            if (shape == NULL || shape->rootType != stage->blocks[row][column] || shape->cellCount <= 1) continue;
            for (int part = 0; part < shape->cellCount; part++) {
                int partColumn = column + shape->cells[part].offsetX;
                if (partColumn >= 0 && partColumn < RPG_STAGE_WORLD_COLUMNS &&
                    dirtyMaps[partColumn / RPG_STAGE_COLUMNS]) {
                    touchesDirty = true;
                    break;
                }
            }
            if (!touchesDirty) continue;
            for (int part = 0; part < shape->cellCount; part++) {
                int partColumn = column + shape->cells[part].offsetX;
                int mapIndex;
                if (partColumn < 0 || partColumn >= RPG_STAGE_WORLD_COLUMNS) continue;
                mapIndex = partColumn / RPG_STAGE_COLUMNS;
                if (!dirtyMaps[mapIndex]) { dirtyMaps[mapIndex] = true; changed = true; }
            }
        }
    } while (changed);
}

bool RpgObjectFolders_RepairEditorPreview(const RpgStage *stage,
                                          const RpgAttachments *attachments,
                                          const RpgReceivers *receivers,
                                          const RpgWires *wires,
                                          Vector2 playerStartPosition)
{
#ifdef _WIN32
    char zipperDirectory[1200];
    bool repaired = true;
    bool staticMutated;
    if (stage == NULL || attachments == NULL || activeBuildPath[0] == '\0' || activeBuildPersists)
        return false;
    (void)receivers;
    (void)wires;
    staticMutated = RpgObjectFolders_HasEditorPreviewStaticMutations();
    if (!staticMutated) {
        /* The common case: no Play operation moved or changed a static
           object folder.  Discard only runtime artifacts.  In particular,
           do not copy or replace the complete blocks/movables trees. */
        if (GetInboxPath(zipperDirectory, sizeof(zipperDirectory))) ClearFolderContents(zipperDirectory);
        RemoveTransientDataShotFolders();
        repaired = EnsurePlayerFolder(playerStartPosition);
        if (repaired) RpgObjectFolder_PrepareZipperAnimationCommand();
        editorPreviewRuntimeTrackingActive = false;
        editorPreviewStaticMutated = false;
        memset(editorPreviewDirtyMaps, 0, sizeof(editorPreviewDirtyMaps));
        if (repaired) {
            NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
            NotifyShellParentChanged(activeBuildPath);
        }
        return repaired;
    }

    /* Play changed one or more object folders.  First discard just those
       areas' generated representation, then rebuild the same areas from the
       still-authoritative editor stage.  No full preview tree is cleared or
       copied here. */
    {
        RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
        RpgStage runtimeStage = *stage;
        bool dirtyMaps[RPG_STAGE_MAP_COUNT];
        bool wasBulkBuildOperation = isBulkBuildOperation;
        memcpy(dirtyMaps, editorPreviewDirtyMaps, sizeof(dirtyMaps));
        ExpandDirtyMapsForCompositeOwners(stage, dirtyMaps);

        /* A targeted repair can still touch many folders in one area.  Do not
           make Explorer process a notification per cell; publish once below. */
        isBulkBuildOperation = true;
        for (int mapIndex = 0; repaired && mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
            if (!dirtyMaps[mapIndex] || !RpgStage_IsMapActive(stage, mapIndex)) continue;
            RemovePreviewMapArtifacts(stage, mapIndex);
            RpgBuildCellStorage_InvalidateMap(mapIndex);
        }

        /* cells.csv is one shared compact file, so batch its single rewrite
           while materialising every dirty map's folders. */
        RpgBuildCellStorage_BeginMetadataBatch();
        for (int mapIndex = 0; repaired && mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
            if (!dirtyMaps[mapIndex] || !RpgStage_IsMapActive(stage, mapIndex)) continue;
            repaired = RpgBuildCellStorage_EnsureMap(stage, mapIndex, &storageBackend);
        }
        if (!RpgBuildCellStorage_EndMetadataBatch(&storageBackend)) repaired = false;

        /* A single composite may cross several dirty maps.  Re-home its
           component folders once for the whole repair set before doing the
           per-map reference/metadata work below. */
        if (repaired) repaired = CollectEffectComponentFoldersForMaps(stage, dirtyMaps);
        for (int mapIndex = 0; repaired && mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
            if (!dirtyMaps[mapIndex] || !RpgStage_IsMapActive(stage, mapIndex)) continue;
            PrepareRuntimeReferenceFiles(&runtimeStage, mapIndex);
            repaired = PrepareRuntimeReferenceFolders(&runtimeStage, mapIndex);
            if (!repaired) break;
            PrepareBlockOwnedMetadataForMap(attachments, receivers, wires, mapIndex);
            PrepareReferenceFolderMetadataForMap(&runtimeStage, mapIndex);
        }
        if (repaired) {
            char zipperDirectory[1200];
            if (GetInboxPath(zipperDirectory, sizeof(zipperDirectory))) ClearFolderContents(zipperDirectory);
            RemoveTransientDataShotFolders();
            repaired = EnsurePlayerFolder(playerStartPosition);
            if (repaired) RpgObjectFolder_PrepareZipperAnimationCommand();
        }
        isBulkBuildOperation = wasBulkBuildOperation;
    }
    editorPreviewRuntimeTrackingActive = false;
    if (!repaired) return false;
    editorPreviewStaticMutated = false;
    memset(editorPreviewDirtyMaps, 0, sizeof(editorPreviewDirtyMaps));
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    NotifyShellParentChanged(activeBuildPath);
    return true;

#else
    (void)stage;
    (void)attachments;
    (void)playerStartPosition;
    return false;
#endif
}

bool RpgObjectFolders_IsBuildCellAvailable(RpgGridCell cell)
{
#ifdef _WIN32
    RpgObjectFolder folder = { .cell = cell };
    char source[1200];
    bool compactAvailable[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    if (RpgObjectFolders_ReadCompactCellAvailability(compactAvailable) && compactAvailable[cell.row][cell.column]) return true;
    /* A folder under Zipper is an explicitly captured object, not an
       available build cell.  Checking it here hid the missing-cell error when
       a once-returned physical cell folder was captured a second time. */
    return BlockPath(&folder, 0, false, source, sizeof(source)) && FolderExistsUtf8(source);
#else
    (void)cell;
    return false;
#endif
}

void RpgObjectFolders_RefreshBuildCellLinkedFiles(RpgGridCell cell)
{
#ifdef _WIN32
    RpgObjectFolder folder = { .cell = cell };
    char source[1200], inbox[1200];
    if (activeBuildPath[0] == '\0' || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return;
    buildCellLinkedFiles[cell.row][cell.column] =
        (BlockPath(&folder, 0, false, source, sizeof(source)) && HasExternalFiles(source)) ||
        (BlockPath(&folder, 0, true, inbox, sizeof(inbox)) && HasExternalFiles(inbox));
#else
    (void)cell;
#endif
}

void RpgObjectFolders_ClearBuildCellLinkedFiles(void)
{
    memset(buildCellLinkedFiles, 0, sizeof(buildCellLinkedFiles));
}

void RpgObjectFolders_EndStageBuild(void)
{
    /* ステージを離れた時点でPlayerは残さない。stage.package は同じ StageN にあるため、
       ここでは実行時成果物だけを消し、次回ビルド用の静的パッケージを削除しない。 */
    char completedBuildPath[1200];
    snprintf(completedBuildPath, sizeof(completedBuildPath), "%s", activeBuildPath);
    if (!activeBuildPersists) DiscardEditorPreviewBaseline();
    editorPreviewRuntimeTrackingActive = false;
    if (!activeBuildPersists) RemovePlayerFolder();
    else RemoveTransientDataShotFolders();
    activeBuildPath[0] = '\0';
    ResetBuildCellOwners();
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);
    activeBuildStageNumber = 0;
    bool preserveProgress = activeBuildPersists;
    activeBuildPersists = false;
    RpgObjectFolders_ClearBuildCellLinkedFiles();
    if (!preserveProgress && completedBuildPath[0] != '\0') (void)ClearStageRuntimeArtifacts(completedBuildPath);
}

void RpgObjectFolders_AbandonStageBuild(void)
{
    /* An editor preview has no persistent runtime state.  Removing every
       generated cell/object directory here can take noticeably longer than
       the Stop action itself, especially with the folder backend.  The next
       BeginStageBuild already clears this exact runtime directory before it
       generates the new preview, so just detach from it now. */
    activeBuildPath[0] = '\0';
    DiscardEditorPreviewBaseline();
    editorPreviewRuntimeTrackingActive = false;
    ResetBuildCellOwners();
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);
    activeBuildStageNumber = 0;
    activeBuildPersists = false;
    RpgObjectFolders_ClearBuildCellLinkedFiles();
}

void RpgObjectFolders_ClearSessionStorage(void)
{
#ifdef _WIN32
    char objects[1200], drops[1200];
    memset(dataShotFolderSerials, 0, sizeof(dataShotFolderSerials)); nextFileDropId = 1;
    hasDispatchedZipperRequest = false;
    hasPendingZipperRequest = false;
    pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
    dispatchedZipperRequestToken[0] = '\0';
    pendingZipperRequestToken[0] = '\0';
    RpgObjectFolderMonitor_DisconnectAll();
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    if (activeBuildPath[0] == '\0' && GetObjectsPath(objects, sizeof(objects))) RemoveTree(objects);
    if (activeBuildPath[0] == '\0' && GetDropsPath(drops, sizeof(drops))) RemoveTree(drops);
    /* Inboxは実Explorerの表示ルートなので、削除・再作成せず同じディレクトリ内を掃除する。 */
    /* Zipper is now the visible storage root.  It contains eat.cmd and any
       captured objects, so cleanup must not empty it as though it were a
       disposable Inbox. */
#endif
}
// 役割: オブジェクト所有フォルダ、Zipper への移動、外部ファイルの引き継ぎを管理する。
