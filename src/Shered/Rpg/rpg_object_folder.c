// 依存する自プロジェクト内ファイル: rpg_object_folder.h
#include "rpg_object_folder.h"
#include "rpg_build_cell_storage.h"
#include "rpg_explorer_launcher.h"
#include "rpg_object_folder_monitor.h"
#include "rpg_stage_storage.h"
#include "rpg_zipper_ability.h"
#include "raylib.h"

// 依存関係: build の通常マス生成は rpg_build_cell_storage の選択方式へ委譲する。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>

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
typedef struct RpgShooterPayloadWatch {
    int attachmentId;
    int monitorHandle;
    bool dirty;
} RpgShooterPayloadWatch;
static RpgShooterPayloadWatch *shooterPayloadWatches = NULL;
static int shooterPayloadWatchCount = 0;
static int shooterPayloadWatchCapacity = 0;
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

/* Temporary Play-start diagnosis.  Reconnecting an existing editor preview
   does no cell generation here, so retain a per-phase trace to identify
   filesystem work that still blocks the first Play frame. */
static RpgObjectFolderResumeProfile lastResumeProfile = { 0 };
static bool hasLastResumeProfile = false;

static void WriteResumeProfile(const RpgObjectFolderResumeProfile *profile)
{
    char path[1200];
    FILE *file;
    if (profile == NULL ||
        snprintf(path, sizeof(path), "%splay_resume_profile.log", GetApplicationDirectory()) <= 0)
        return;
    file = fopen(path, "wb");
    if (file == NULL) return;
    fprintf(file, "Play preview resume profile (milliseconds)\n");
    fprintf(file, "result=%s\n", profile->succeeded ? "success" : "failed");
    fprintf(file, "stage_setup=%.3f\n", profile->stageSetupMs);
    fprintf(file, "repair_reference_copies=%.3f\n", profile->repairReferenceCopiesMs);
    fprintf(file, "prepare_runtime_reference_files=%.3f\n", profile->prepareReferenceFilesMs);
    fprintf(file, "remove_transient_data_shots=%.3f\n", profile->removeTransientShotsMs);
    fprintf(file, "apply_cell_progress=%.3f\n", profile->applyCellProgressMs);
    fprintf(file, "collect_composite_folders=%.3f\n", profile->collectCompositeFoldersMs);
    fprintf(file, "validate_special_folders=%.3f\n", profile->validateSpecialFoldersMs);
    fprintf(file, "total=%.3f\n", profile->totalMs);
    fflush(file);
    fclose(file);
}

bool RpgObjectFolders_GetLastResumeProfile(RpgObjectFolderResumeProfile *profile)
{
    if (profile == NULL || !hasLastResumeProfile) return false;
    *profile = lastResumeProfile;
    return true;
}
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
/* A topology change needs one old-identity cleanup before the build snapshot
   can move to the newly authored area coordinates.  It is deliberately
   separate from the legacy static mutation flag, which means "all maps". */
static bool editorPreviewTopologyMutated = false;
static bool editorPreviewRetiredMaps[RPG_STAGE_MAP_COUNT] = { false };
typedef enum RpgEditorPreviewRepairPhase {
    RPG_EDITOR_PREVIEW_REPAIR_IDLE = 0,
    RPG_EDITOR_PREVIEW_REPAIR_TOPOLOGY_CLEANUP,
    RPG_EDITOR_PREVIEW_REPAIR_MAPS,
    RPG_EDITOR_PREVIEW_REPAIR_COMPOSITES,
    RPG_EDITOR_PREVIEW_REPAIR_REFERENCES,
    RPG_EDITOR_PREVIEW_REPAIR_FINALIZE
} RpgEditorPreviewRepairPhase;
/* Keep repair work cooperative.  The old repair path rebuilt every marked
   map, its metadata and its reference folders in one editor frame. */
static struct {
    RpgEditorPreviewRepairPhase phase;
    bool maps[RPG_STAGE_MAP_COUNT];
    bool retiredMaps[RPG_STAGE_MAP_COUNT];
    bool topologyChanged;
    int cursor;
    int queuedMapIndex;
    bool metadataBatchOpen;
} editorPreviewRepairJob = { RPG_EDITOR_PREVIEW_REPAIR_IDLE, { false }, { false }, false, 0, -1, false };
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
/* Zipper の容量は、ゲームが成功させた入出庫操作の差分として保持する。
   Windows の変更通知は command 要求のためだけに使い、HUD 描画からの
   フォルダ再帰走査は絶対に行わない。 */
static int zipperStorageMonitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
static char zipperStorageMonitorPath[1200] = { 0 };
/* The same directory watcher that invalidates the capacity cache also gates
   zipper_command.request.  Never probe that request on every game frame. */
static bool zipperCommandRequestDirty = true;
static ULONGLONG zipperCommandFallbackPollAt = 0;
static unsigned long long zipperStorageBytes = 0;
/* Content consumers (abilities, later item types) share the one recursive
   Zipper watcher instead of adding filesystem work to the game loop. */
static unsigned int zipperStorageChangeSerial = 1;

/* Capacity is updated immediately by successful game-owned transfers.  The
 * audit below is deliberately independent: it walks the directory in small
 * slices and can repair the ledger after an external Explorer edit or an
 * accounting mistake without putting recursive I/O in the render path. */
enum { RPG_ZIPPER_AUDIT_MAX_DEPTH = 48, RPG_ZIPPER_AUDIT_ENTRIES_PER_FRAME = 24 };
typedef struct RpgZipperStorageAuditDirectory {
    HANDLE handle;
    wchar_t path[1200];
    WIN32_FIND_DATAW current;
    bool hasCurrent;
    bool isRoot;
} RpgZipperStorageAuditDirectory;
typedef struct RpgZipperStorageAudit {
    RpgZipperStorageAuditDirectory directories[RPG_ZIPPER_AUDIT_MAX_DEPTH];
    int depth;
    unsigned long long bytes;
    unsigned int generationAtStart;
    ULONGLONG requestedAt;
    ULONGLONG nextPeriodicAt;
    bool requested;
    bool active;
} RpgZipperStorageAudit;
static RpgZipperStorageAudit zipperStorageAudit = { 0 };
static unsigned int zipperStorageAuditGeneration = 1;

static void DiscardEditorPreviewBaseline(void);
static bool CopyTree(const char *source, const char *destination);
static int GetBuildCompositeId(RpgGridCell cell);
static void ResetZipperStorageAudit(void);

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

void RpgObjectFolders_ConfirmEditorPreviewBaseline(void)
{
    /* Never discard a real repair that is already processing an authoring or
       Play-side change.  Callers use this immediately after a successful
       Create/Repair publication, before an editor-frame repair job exists. */
    if (activeBuildPersists || activeBuildPath[0] == '\0' ||
        editorPreviewRepairJob.phase != RPG_EDITOR_PREVIEW_REPAIR_IDLE)
        return;
    editorPreviewStaticMutated = false;
    memset(editorPreviewDirtyMaps, 0, sizeof(editorPreviewDirtyMaps));
    editorPreviewRuntimeTrackingActive = false;
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
    /* This is invoked from the authoring-stage diff.  Do not reject a valid
       map merely because the previous preview's buildMapActive snapshot was
       taken before an area insert/move.  The cooperative repair receives the
       current stage and skips inactive maps there. */
    if (activeBuildPersists || activeBuildPath[0] == '\0' ||
        mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT) return;
    editorPreviewStaticMutated = true;
    /* Record only the actual owner area.  Repair expands this set later only
       when a real composite spans an area boundary. */
    editorPreviewDirtyMaps[mapIndex] = true;
}

void RpgObjectFolders_MarkEditorPreviewTopologyMutation(const RpgStage *before,
                                                        const RpgStage *after)
{
    if (activeBuildPersists || before == NULL || after == NULL) return;
    editorPreviewStaticMutated = true;
    editorPreviewTopologyMutated = true;
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        const bool wasActive = before->mapActive[mapIndex];
        const bool isActive = after->mapActive[mapIndex];
        const bool changed = wasActive != isActive ||
                             (wasActive && isActive &&
                              (before->mapGridX[mapIndex] != after->mapGridX[mapIndex] ||
                               before->mapGridY[mapIndex] != after->mapGridY[mapIndex]));
        if (!changed) continue;
        /* A new or relocated map is rebuilt at its new identity. */
        if (isActive) editorPreviewDirtyMaps[mapIndex] = true;
        /* A removed map has no current cells to queue, but its old
           coordinate-owned folders still need one cleanup pass. */
        if (wasActive && !isActive) editorPreviewRetiredMaps[mapIndex] = true;
    }
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
        sscanf(area, "area_x%d_y%d_cell_", &gridX, &gridY) != 2)
        /* A movable Folder or a composite root deliberately has no cell in
           its pathname.  This low-level file operation cannot infer which
           stage object owns it, so it must not pessimistically dirty every
           area.  Its caller supplies the owner cell once it knows the
           target (key door / Folder object). */
        return;
    mapIndex = FindBuildMapByGridPosition(gridX, gridY);
    if (mapIndex >= 0) RpgObjectFolders_MarkEditorPreviewMapMutation(mapIndex);
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
           Keep it safe until they can provide an owning map explicitly.  The
           current-stage repair filters inactive maps; using a stale preview
           snapshot here must not make an authored edit invisible. */
        for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
            editorPreviewDirtyMaps[index] = true;
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

/* Data-shot folders are runtime artifacts.  Creating one must never hold the
 * gameplay thread at the moment a shooter fires, so the worker receives an
 * immutable snapshot rather than a pointer into RpgDataShots. */
enum { RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY = RPG_DATA_SHOT_MAX_COUNT * 2 };
typedef struct RpgDataShotFolderWork {
    bool removeOnly;
    int slotIndex;
    int serial;
    int attachmentIndex;
    int pathCellIndex;
    Vector2 position;
    char folder[1200];
} RpgDataShotFolderWork;
typedef struct RpgDataShotFolderWorkResult {
    bool succeeded;
    int slotIndex;
    int serial;
    char folder[1200];
} RpgDataShotFolderWorkResult;
static INIT_ONCE dataShotFolderWorkInitOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION dataShotFolderWorkLock;
static HANDLE dataShotFolderWorkWakeEvent = NULL;
static HANDLE dataShotFolderWorkStopEvent = NULL;
static HANDLE dataShotFolderWorkThread = NULL;
static RpgDataShotFolderWork dataShotFolderWorkQueue[RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY];
static int dataShotFolderWorkHead = 0;
static int dataShotFolderWorkCount = 0;
static RpgDataShotFolderWorkResult dataShotFolderWorkResults[RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY];
static int dataShotFolderWorkResultHead = 0;
static int dataShotFolderWorkResultCount = 0;

static BOOL CALLBACK InitializeDataShotFolderWorker(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    InitializeCriticalSection(&dataShotFolderWorkLock);
    dataShotFolderWorkWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    dataShotFolderWorkStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    return dataShotFolderWorkWakeEvent != NULL && dataShotFolderWorkStopEvent != NULL;
}

static bool WorkerCreateDirectoryUtf8(const char *path)
{
    wchar_t widePath[1200];
    if (!ToWide(path, widePath, 1200)) return false;
    if (CreateDirectoryW(widePath, NULL) != 0) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool WorkerWriteTextFile(const char *path, const char *text)
{
    wchar_t widePath[1200];
    HANDLE file;
    DWORD written = 0;
    size_t length;
    if (!ToWide(path, widePath, 1200) || text == NULL) return false;
    file = CreateFileW(widePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    length = strlen(text);
    const bool wrote = length <= MAXDWORD &&
                       WriteFile(file, text, (DWORD)length, &written, NULL) != 0 &&
                       written == (DWORD)length;
    const bool closed = CloseHandle(file) != 0;
    return wrote && closed;
}

static void WorkerRemoveTreeUtf8(const char *directory)
{
    char search[1200];
    wchar_t wideSearch[1200], wideDirectory[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, 1200) || !ToWide(directory, wideDirectory, 1200)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            char name[1024], child[1200];
            wchar_t wideChild[1200];
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
                WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, (int)sizeof(name), NULL, NULL) <= 0 ||
                snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) WorkerRemoveTreeUtf8(child);
            else if (ToWide(child, wideChild, 1200)) (void)DeleteFileW(wideChild);
        } while (FindNextFileW(handle, &data) != 0);
        FindClose(handle);
    }
    (void)RemoveDirectoryW(wideDirectory);
}

static bool WorkerCreateDataShotFolder(const RpgDataShotFolderWork *work)
{
    char objectInfo[1200], shotInfo[1200], text[256];
    if (work == NULL || !WorkerCreateDirectoryUtf8(work->folder) ||
        snprintf(objectInfo, sizeof(objectInfo), "%s\\object_info.txt", work->folder) <= 0 ||
        snprintf(shotInfo, sizeof(shotInfo), "%s\\data_shot_info.txt", work->folder) <= 0 ||
        snprintf(text, sizeof(text), "type=0\nx=%.1f\ny=%.1f\n", work->position.x, work->position.y) <= 0 ||
        !WorkerWriteTextFile(objectInfo, text) ||
        snprintf(text, sizeof(text), "attachment_index=%d\npath_cell_index=%d\n",
                 work->attachmentIndex, work->pathCellIndex) <= 0 ||
        !WorkerWriteTextFile(shotInfo, text)) return false;
    return true;
}

static bool PopDataShotFolderWork(RpgDataShotFolderWork *work)
{
    if (work == NULL) return false;
    EnterCriticalSection(&dataShotFolderWorkLock);
    if (dataShotFolderWorkCount <= 0) {
        LeaveCriticalSection(&dataShotFolderWorkLock);
        return false;
    }
    *work = dataShotFolderWorkQueue[dataShotFolderWorkHead];
    dataShotFolderWorkHead = (dataShotFolderWorkHead + 1) % RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY;
    dataShotFolderWorkCount--;
    LeaveCriticalSection(&dataShotFolderWorkLock);
    return true;
}

static void PushDataShotFolderWorkResult(const RpgDataShotFolderWorkResult *result)
{
    if (result == NULL) return;
    EnterCriticalSection(&dataShotFolderWorkLock);
    if (dataShotFolderWorkResultCount < RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY) {
        const int slot = (dataShotFolderWorkResultHead + dataShotFolderWorkResultCount) %
                         RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY;
        dataShotFolderWorkResults[slot] = *result;
        dataShotFolderWorkResultCount++;
    }
    LeaveCriticalSection(&dataShotFolderWorkLock);
}

static DWORD WINAPI DataShotFolderWorkerMain(LPVOID context)
{
    HANDLE waits[2] = { dataShotFolderWorkStopEvent, dataShotFolderWorkWakeEvent };
    (void)context;
    for (;;) {
        const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult != WAIT_OBJECT_0 + 1) continue;
        for (;;) {
            RpgDataShotFolderWork work;
            if (!PopDataShotFolderWork(&work)) break;
            if (work.removeOnly) {
                WorkerRemoveTreeUtf8(work.folder);
                continue;
            }
            RpgDataShotFolderWorkResult result = {
                .succeeded = WorkerCreateDataShotFolder(&work),
                .slotIndex = work.slotIndex,
                .serial = work.serial
            };
            snprintf(result.folder, sizeof(result.folder), "%s", work.folder);
            PushDataShotFolderWorkResult(&result);
        }
    }
    return 0;
}

static bool QueueDataShotFolderWork(const RpgDataShotFolderWork *work)
{
    bool queued = false;
    if (work == NULL || !InitOnceExecuteOnce(&dataShotFolderWorkInitOnce,
                                              InitializeDataShotFolderWorker, NULL, NULL)) return false;
    EnterCriticalSection(&dataShotFolderWorkLock);
    if (dataShotFolderWorkThread == NULL) {
        ResetEvent(dataShotFolderWorkStopEvent);
        dataShotFolderWorkThread = CreateThread(NULL, 0, DataShotFolderWorkerMain, NULL, 0, NULL);
    }
    if (dataShotFolderWorkThread != NULL && dataShotFolderWorkCount < RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY) {
        const int slot = (dataShotFolderWorkHead + dataShotFolderWorkCount) %
                         RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY;
        dataShotFolderWorkQueue[slot] = *work;
        dataShotFolderWorkCount++;
        queued = true;
    }
    LeaveCriticalSection(&dataShotFolderWorkLock);
    if (queued) SetEvent(dataShotFolderWorkWakeEvent);
    return queued;
}

static bool PopDataShotFolderWorkResult(RpgDataShotFolderWorkResult *result)
{
    if (result == NULL || !InitOnceExecuteOnce(&dataShotFolderWorkInitOnce,
                                                InitializeDataShotFolderWorker, NULL, NULL)) return false;
    EnterCriticalSection(&dataShotFolderWorkLock);
    if (dataShotFolderWorkResultCount <= 0) {
        LeaveCriticalSection(&dataShotFolderWorkLock);
        return false;
    }
    *result = dataShotFolderWorkResults[dataShotFolderWorkResultHead];
    dataShotFolderWorkResultHead = (dataShotFolderWorkResultHead + 1) % RPG_DATA_SHOT_FOLDER_WORK_QUEUE_CAPACITY;
    dataShotFolderWorkResultCount--;
    LeaveCriticalSection(&dataShotFolderWorkLock);
    return true;
}

static void StopDataShotFolderWorker(void)
{
    HANDLE thread = NULL;
    if (!InitOnceExecuteOnce(&dataShotFolderWorkInitOnce, InitializeDataShotFolderWorker, NULL, NULL)) return;
    EnterCriticalSection(&dataShotFolderWorkLock);
    thread = dataShotFolderWorkThread;
    if (thread != NULL) SetEvent(dataShotFolderWorkStopEvent);
    LeaveCriticalSection(&dataShotFolderWorkLock);
    if (thread != NULL) {
        (void)WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        EnterCriticalSection(&dataShotFolderWorkLock);
        dataShotFolderWorkThread = NULL;
        dataShotFolderWorkHead = 0;
        dataShotFolderWorkCount = 0;
        dataShotFolderWorkResultHead = 0;
        dataShotFolderWorkResultCount = 0;
        LeaveCriticalSection(&dataShotFolderWorkLock);
    }
}

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
    /* All composite geometries share the same owner model.  A fixed door and
       a resizable rectangle therefore resolve component cells to their
       common placement origin before any folder or incremental-build code
       sees them. */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const RpgCompositeObject *object = RpgStage_GetCompositeAtCellConst(stage, row, column);
        if (object != NULL)
            buildCellOwners[row][column] = (RpgGridCell){ object->originRow, object->originColumn };
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
    ResetZipperStorageAudit();
    zipperStorageMonitorPath[0] = '\0';
    zipperCommandRequestDirty = true;
    zipperCommandFallbackPollAt = 0;
}

static void ResetZipperStorageAudit(void)
{
    while (zipperStorageAudit.depth > 0) {
        RpgZipperStorageAuditDirectory *directory =
            &zipperStorageAudit.directories[--zipperStorageAudit.depth];
        if (directory->handle != INVALID_HANDLE_VALUE) FindClose(directory->handle);
    }
    memset(&zipperStorageAudit, 0, sizeof(zipperStorageAudit));
}

static void RequestZipperStorageAudit(void)
{
    /* Do not interrupt the game operation which caused the change.  A scan is
       started only after the directory has been quiet briefly. */
    zipperStorageAuditGeneration++;
    if (zipperStorageAuditGeneration == 0) zipperStorageAuditGeneration = 1;
    zipperStorageAudit.requested = true;
    zipperStorageAudit.requestedAt = GetTickCount64();
}

static void OnZipperStorageFolderChanged(void *context, const char *directory)
{
    (void)context;
    (void)directory;
    zipperStorageChangeSerial++;
    if (zipperStorageChangeSerial == 0) zipperStorageChangeSerial = 1;
    zipperCommandRequestDirty = true;
    RequestZipperStorageAudit();
}

static bool IsZipperAuditHelperFile(const wchar_t *name)
{
    return _wcsicmp(name, L"eat.vbs") == 0 || _wcsicmp(name, L"spit.vbs") == 0 ||
           _wcsicmp(name, L"zipper_command.request") == 0 ||
           _wcsicmp(name, L"assyuku.cmd") == 0 || _wcsicmp(name, L"assyuku.vbs") == 0 ||
           _wcsicmp(name, L"assyuku.request") == 0;
}

static bool BeginZipperStorageAudit(void)
{
    RpgZipperStorageAuditDirectory *root;
    wchar_t wideRoot[1200], search[1200];
    WIN32_FIND_DATAW found;
    HANDLE handle;
    if (activeZipperPath[0] == '\0' || !ToWide(activeZipperPath, wideRoot, 1200) ||
        swprintf(search, 1200, L"%ls\\*", wideRoot) < 0) return false;
    handle = FindFirstFileW(search, &found);
    if (handle == INVALID_HANDLE_VALUE) return false;
    ResetZipperStorageAudit();
    root = &zipperStorageAudit.directories[0];
    root->handle = handle;
    root->current = found;
    root->hasCurrent = true;
    root->isRoot = true;
    wcsncpy(root->path, wideRoot, (sizeof(root->path) / sizeof(root->path[0])) - 1);
    root->path[(sizeof(root->path) / sizeof(root->path[0])) - 1] = L'\0';
    zipperStorageAudit.depth = 1;
    zipperStorageAudit.bytes = 0;
    zipperStorageAudit.generationAtStart = zipperStorageAuditGeneration;
    zipperStorageAudit.requested = false;
    zipperStorageAudit.active = true;
    return true;
}

static void AbortZipperStorageAudit(void)
{
    ResetZipperStorageAudit();
    zipperStorageAudit.requested = true;
    zipperStorageAudit.requestedAt = GetTickCount64();
}

static void FinishZipperStorageAudit(void)
{
    const unsigned long long measuredBytes = zipperStorageAudit.bytes;
    const unsigned int generationAtStart = zipperStorageAudit.generationAtStart;
    const ULONGLONG now = GetTickCount64();
    ResetZipperStorageAudit();
    zipperStorageAudit.nextPeriodicAt = now + 15000ULL;
    if (generationAtStart != zipperStorageAuditGeneration) {
        /* A directory change arrived while walking it.  Never reconcile from
           a mixed snapshot; wait for the folder to settle and retry. */
        zipperStorageAudit.requested = true;
        zipperStorageAudit.requestedAt = now;
        return;
    }
    if (zipperStorageBytes != measuredBytes) {
        char message[192];
        snprintf(message, sizeof(message),
                 "Zipper capacity audit reconciled %llu -> %llu bytes\n",
                 zipperStorageBytes, measuredBytes);
        OutputDebugStringA(message);
        zipperStorageBytes = measuredBytes;
    }
}

static bool AdvanceZipperStorageAuditEntry(void)
{
    RpgZipperStorageAuditDirectory *directory;
    WIN32_FIND_DATAW found;
    wchar_t childPath[1200], search[1200];
    HANDLE handle;
    if (zipperStorageAudit.depth <= 0) return false;
    directory = &zipperStorageAudit.directories[zipperStorageAudit.depth - 1];
    if (!directory->hasCurrent) {
        if (FindNextFileW(directory->handle, &directory->current) == 0) {
            FindClose(directory->handle);
            directory->handle = INVALID_HANDLE_VALUE;
            zipperStorageAudit.depth--;
            return false;
        }
        directory->hasCurrent = true;
    }
    found = directory->current;
    directory->hasCurrent = false;
    if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) return true;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
        if (zipperStorageAudit.depth >= RPG_ZIPPER_AUDIT_MAX_DEPTH ||
            swprintf(childPath, 1200, L"%ls\\%ls", directory->path, found.cFileName) < 0 ||
            swprintf(search, 1200, L"%ls\\*", childPath) < 0) {
            AbortZipperStorageAudit();
            return false;
        }
        handle = FindFirstFileW(search, &found);
        if (handle == INVALID_HANDLE_VALUE) return true;
        directory = &zipperStorageAudit.directories[zipperStorageAudit.depth++];
        directory->handle = handle;
        directory->current = found;
        directory->hasCurrent = true;
        directory->isRoot = false;
        wcsncpy(directory->path, childPath, (sizeof(directory->path) / sizeof(directory->path[0])) - 1);
        directory->path[(sizeof(directory->path) / sizeof(directory->path[0])) - 1] = L'\0';
        return true;
    }
    if (!(directory->isRoot && IsZipperAuditHelperFile(found.cFileName)))
        zipperStorageAudit.bytes += ((unsigned long long)found.nFileSizeHigh << 32) | found.nFileSizeLow;
    return true;
}

void RpgObjectFolder_UpdateZipperStorageAudit(void)
{
#ifdef _WIN32
    ULONGLONG now;
    int processed = 0;
    if (activeZipperPath[0] == '\0') {
        ResetZipperStorageAudit();
        return;
    }
    now = GetTickCount64();
    if (!zipperStorageAudit.active && !zipperStorageAudit.requested &&
        now >= zipperStorageAudit.nextPeriodicAt) {
        zipperStorageAudit.requested = true;
        zipperStorageAudit.requestedAt = now;
    }
    if (!zipperStorageAudit.active && zipperStorageAudit.requested &&
        now >= zipperStorageAudit.requestedAt + 2000ULL) {
        if (!BeginZipperStorageAudit()) {
            /* The active folder may be in the middle of a Windows rename.
               Retain the immediate ledger and retry later instead of zeroing
               capacity from a transient missing directory. */
            zipperStorageAudit.requestedAt = now;
            return;
        }
    }
    while (zipperStorageAudit.active && processed < RPG_ZIPPER_AUDIT_ENTRIES_PER_FRAME) {
        (void)AdvanceZipperStorageAuditEntry();
        processed++;
        if (!zipperStorageAudit.active) break;
        if (zipperStorageAudit.depth == 0) FinishZipperStorageAudit();
    }
#endif
}

unsigned int RpgObjectFolder_GetZipperStorageChangeSerial(void)
{
    return zipperStorageChangeSerial;
}

/* Keep the Win32 watcher connection independent from whether the capacity HUD
   is currently drawn.  Commands need this connection even while its UI is
   hidden. */
static bool EnsureZipperStorageWatcher(char *storageDirectory, size_t storageDirectorySize)
{
    char directory[1200];
    if (!GetZipperStorageDirectory(directory, sizeof(directory))) {
        CloseZipperStorageWatcher();
        zipperStorageBytes = 0;
        return false;
    }
    /* Once connected, the Win32 change notification owns existence changes.
       Do not turn this helper back into a per-frame GetFileAttributesW poll. */
    if (strcmp(zipperStorageMonitorPath, directory) != 0) {
        if (!FolderExistsUtf8(directory)) {
            CloseZipperStorageWatcher();
            zipperStorageBytes = 0;
            return false;
        }
        CloseZipperStorageWatcher();
        zipperStorageMonitorHandle = RpgObjectFolderMonitor_Connect(
            RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE, directory, true,
            OnZipperStorageFolderChanged, NULL);
        snprintf(zipperStorageMonitorPath, sizeof(zipperStorageMonitorPath), "%s", directory);
    }
    if (storageDirectory != NULL && storageDirectorySize > 0)
        snprintf(storageDirectory, storageDirectorySize, "%s", directory);
    return true;
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

static unsigned long long MeasurePathBytesUtf8(const char *path)
{
#ifdef _WIN32
    wchar_t widePath[1200];
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (path == NULL || path[0] == '\0' || !ToWide(path, widePath, 1200) ||
        !GetFileAttributesExW(widePath, GetFileExInfoStandard, &attributes)) return 0;
    if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return MeasureDirectoryBytes(widePath);
    return ((unsigned long long)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
#else
    (void)path;
    return 0;
#endif
}

static bool IsPathInsideActiveZipper(const char *path)
{
#ifdef _WIN32
    size_t rootLength;
    if (path == NULL || activeZipperPath[0] == '\0') return false;
    rootLength = strlen(activeZipperPath);
    return _strnicmp(path, activeZipperPath, rootLength) == 0 &&
           (path[rootLength] == '\0' || path[rootLength] == '\\' || path[rootLength] == '/');
#else
    (void)path;
    return false;
#endif
}

static void SubtractZipperStorageBytes(unsigned long long bytes)
{
    zipperStorageBytes = bytes >= zipperStorageBytes ? 0 : zipperStorageBytes - bytes;
}

/* Keep the capacity cache tied to successful game-owned moves.  This is
   intentionally not a filesystem watcher reconciliation: Explorer-side
   manual edits do not force a recursive scan during rendering. */
static void UpdateZipperStorageForPathMove(const char *source, const char *destination,
                                           unsigned long long sourceBytes,
                                           unsigned long long replacedDestinationBytes,
                                           bool moved)
{
    const bool sourceInZipper = IsPathInsideActiveZipper(source);
    const bool destinationInZipper = IsPathInsideActiveZipper(destination);
    if (replacedDestinationBytes > 0 && destinationInZipper)
        SubtractZipperStorageBytes(replacedDestinationBytes);
    if (!moved) {
        if (replacedDestinationBytes > 0 && destinationInZipper)
            RequestZipperStorageAudit();
        return;
    }
    if (sourceInZipper) SubtractZipperStorageBytes(sourceBytes);
    if (destinationInZipper) zipperStorageBytes += sourceBytes;
    if (replacedDestinationBytes > 0 || sourceInZipper || destinationInZipper)
        RequestZipperStorageAudit();
}

unsigned long long RpgObjectFolder_GetZipperStorageBytes(void)
{
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
    zipperStorageChangeSerial++;
    if (zipperStorageChangeSerial == 0) zipperStorageChangeSerial = 1;
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
    /* This is a build/setup boundary, not a draw-time refresh.  A migrated
       legacy Inbox may already contain real held objects, so seed the ledger
       once before the command helper files are written. */
    zipperStorageBytes = MeasurePathBytesUtf8(activeZipperPath);
    zipperStorageAudit.nextPeriodicAt = GetTickCount64() + 15000ULL;

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
    /* Activating an existing Folder as Zipper imports its current contents
       into the game-owned capacity ledger exactly once. */
    zipperStorageBytes = MeasurePathBytesUtf8(activeZipperPath);
    zipperStorageAudit.nextPeriodicAt = GetTickCount64() + 15000ULL;
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

/* Folder reference objects stopped being terrain cells.  Keep their directory
 * as the Zipper root instead of renaming it to a sibling named "Zipper": a
 * user-controlled folder name can legitimately already have that sibling,
 * and the old rename-only path then rejected zipper.cmd outright. */
bool RpgObjectFolder_ActivateReferenceObjectFolderAsZipper(RpgReferenceObject *object)
{
#ifdef _WIN32
    char folderPath[1200], requestPath[1200];
    wchar_t wideRequestPath[1200];
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
        !RpgObjectFolders_EnsureReferenceObject(object) || object->path[0] == '\0' ||
        snprintf(folderPath, sizeof(folderPath), "%s", object->path) <= 0 ||
        !SetActiveZipperPath(folderPath)) return false;
    MigrateLegacyInboxContents();
    zipperStorageBytes = MeasurePathBytesUtf8(activeZipperPath);
    zipperStorageAudit.nextPeriodicAt = GetTickCount64() + 15000ULL;
    /* Do not carry the one-shot conversion request into the next Play
       session.  zipper.cmd remains available and will create a fresh request
       if the user explicitly runs it again. */
    if (snprintf(requestPath, sizeof(requestPath), "%s\\zipper.request", folderPath) > 0 &&
        ToWide(requestPath, wideRequestPath,
               (int)(sizeof(wideRequestPath) / sizeof(wideRequestPath[0]))))
        (void)DeleteFileW(wideRequestPath);
    RpgObjectFolder_PrepareZipperAnimationCommand();
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, folderPath, NULL);
    NotifyShellPathHierarchyChanged(folderPath);
    return true;
#else
    (void)object;
    return false;
#endif
}

/* Native change notifications are only a wake-up optimisation here.  The
 * short poll is the authoritative fallback, so executing zipper.cmd works
 * even when Explorer batches or loses the ReadDirectoryChangesW event. */
bool RpgObjectFolder_ConsumeReferenceObjectFolderZipperRequest(const RpgStage *stage,
                                                                RpgReferenceObjects *objects,
                                                                Vector2 playerPosition,
                                                                int *objectIndex)
{
#ifdef _WIN32
    static ULONGLONG nextPollAt = 0;
    const ULONGLONG now = GetTickCount64();
    int playerMapIndex;
    if (objectIndex != NULL) *objectIndex = -1;
    if (stage == NULL || objects == NULL || now < nextPollAt) return false;
    nextPollAt = now + 100ULL;
    playerMapIndex = RpgStage_GetMapAtWorldPosition(stage, playerPosition);
    if (playerMapIndex < 0) return false;
    for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *object = &objects->entries[index];
        char requestPath[1200];
        wchar_t wideRequestPath[1200];
        if (object->objectKind != RPG_REFERENCE_OBJECT_FOLDER || object->isMaterializing ||
            RpgStage_GetMapAtWorldPosition(stage, object->position) != playerMapIndex ||
            !RpgObjectFolders_EnsureReferenceObject(object) || object->path[0] == '\0' ||
            snprintf(requestPath, sizeof(requestPath), "%s\\zipper.request", object->path) <= 0 ||
            !ToWide(requestPath, wideRequestPath, (int)(sizeof(wideRequestPath) / sizeof(wideRequestPath[0]))) ||
            GetFileAttributesW(wideRequestPath) == INVALID_FILE_ATTRIBUTES) continue;
        if (objectIndex != NULL) *objectIndex = index;
        return true;
    }
    return false;
#else
    (void)stage; (void)objects; (void)playerPosition; (void)objectIndex;
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

/* Temporary diagnostic for the editor's FILE/FOLDER/PNG-only preview batch.
   It is separate from the transfer path: recording a failed object must not
   influence whether that object is created, removed, or repaired. */
static void TraceEditorPreviewMovableBatch(const char *event, unsigned int id,
                                           const char *detail)
{
#ifdef _WIN32
    char path[1200];
    wchar_t widePath[1200];
    FILE *file;
    if (activeBuildPath[0] == '\0' || event == NULL ||
        snprintf(path, sizeof(path), "%s\\movable_batch_trace.log", activeBuildPath) <= 0 ||
        !ToWide(path, widePath, (int)(sizeof(widePath) / sizeof(widePath[0]))) ||
        (file = _wfopen(widePath, L"ab")) == NULL) return;
    fprintf(file, "%s id=%u detail=%s winerr=%lu\n", event, id,
            detail != NULL ? detail : "-", (unsigned long)GetLastError());
    fflush(file);
    fclose(file);
#else
    (void)event; (void)id; (void)detail;
#endif
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

static unsigned long lastDirectoryMoveError = 0;

unsigned long RpgObjectFolder_GetLastDirectoryMoveError(void)
{
    return lastDirectoryMoveError;
}

bool RpgObjectFolder_IsLastDirectoryMoveErrorRetryable(void)
{
#ifdef _WIN32
    return lastDirectoryMoveError == ERROR_SHARING_VIOLATION ||
           lastDirectoryMoveError == ERROR_LOCK_VIOLATION ||
           lastDirectoryMoveError == ERROR_ACCESS_DENIED;
#else
    return false;
#endif
}

static bool MoveDirectory(const char *source, const char *destination)
{
    wchar_t wideSource[1200], wideDestination[1200];
    HANDLE sourceHandle;
    unsigned long long sourceBytes;
    unsigned long long replacedDestinationBytes;
    bool moved;
    lastDirectoryMoveError = 0;
    if (!FolderExistsUtf8(source)) {
        lastDirectoryMoveError = ERROR_PATH_NOT_FOUND;
        return false;
    }
    if (!ToWide(source, wideSource, 1200) || !ToWide(destination, wideDestination, 1200)) {
        lastDirectoryMoveError = ERROR_INVALID_PARAMETER;
        return false;
    }
    /* A virtual Explorer may have destroyed its window before its process
       releases the root directory.  Probe delete sharing before replacing a
       destination so a transient lock leaves both directory trees intact. */
    sourceHandle = CreateFileW(wideSource, DELETE | FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (sourceHandle == INVALID_HANDLE_VALUE) {
        lastDirectoryMoveError = GetLastError();
        return false;
    }
    CloseHandle(sourceHandle);
    sourceBytes = MeasurePathBytesUtf8(source);
    replacedDestinationBytes = FolderExistsUtf8(destination) ? MeasurePathBytesUtf8(destination) : 0;
    RemoveTree(destination);
    /* RemoveTree has already made a replaced Zipper child disappear, even if
       the following MoveFileExW happens to fail. */
    UpdateZipperStorageForPathMove(source, destination, 0, replacedDestinationBytes, false);
    moved = MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) != 0;
    if (!moved) lastDirectoryMoveError = GetLastError();
    UpdateZipperStorageForPathMove(source, destination, sourceBytes, 0, moved);
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
    wchar_t wideFileName[1024];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL || fileName == NULL || fileName[0] == '\0' ||
        !FolderExistsUtf8(directory) || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, 1200) ||
        !ToWide(fileName, wideFileName, (int)(sizeof(wideFileName) / sizeof(wideFileName[0])))) return false;
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
        /* Windows object folders are case-insensitive.  Compare the original
           UTF-16 directory entry, not a lossy/narrow conversion, so the key
           door monitor observes the same identity as the file-transfer path. */
        } else if (_wcsicmp(data.cFileName, wideFileName) == 0) {
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

/* Terrain blocks have a stable grid identity in addition to their display
   position.  Do not recover this identity from x/y: movable objects write
   world coordinates there, while cell folders historically wrote storage
   coordinates.  Keeping the cell explicit prevents those two coordinate
   systems from being compared as if they were interchangeable. */
static bool WriteBlockObjectInfo(const char *folder, int type, RpgGridCell cell)
{
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    Vector2 storagePosition;
    if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS ||
        !CreateFolderUtf8(folder) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    storagePosition = (Vector2){ (cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                 (cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "type=%d\ncell_row=%d\ncell_column=%d\nx=%.1f\ny=%.1f\n",
            type, cell.row, cell.column, storagePosition.x, storagePosition.y);
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, infoPath, NULL);
    NotifyShellParentChanged(infoPath);
    return true;
}

/* Composite grouping runs when a preview is resumed as well as when it is
   first created.  Do not turn that idempotent check into write traffic: the
   existing base record is authoritative when it already names the expected
   type and centre. */
static bool BlockObjectInfoMatches(const char *folder, int expectedType, RpgGridCell expectedCell)
{
    char infoPath[1200], line[128];
    wchar_t wideInfoPath[1200];
    FILE *file;
    int type = INT_MIN;
    float x = NAN, y = NAN;
    int cellRow = INT_MIN, cellColumn = INT_MIN;
    Vector2 expectedPosition = { (expectedCell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                 (expectedCell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    if (folder == NULL || snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200) || (file = _wfopen(wideInfoPath, L"rb")) == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        (void)sscanf(line, "type=%d", &type);
        (void)sscanf(line, "cell_row=%d", &cellRow);
        (void)sscanf(line, "cell_column=%d", &cellColumn);
        (void)sscanf(line, "x=%f", &x);
        (void)sscanf(line, "y=%f", &y);
    }
    fclose(file);
    return type == expectedType && cellRow == expectedCell.row && cellColumn == expectedCell.column &&
           fabsf(x - expectedPosition.x) < 0.01f &&
           fabsf(y - expectedPosition.y) < 0.01f;
}

bool RpgObjectFolder_BlockTransferMetadataMatches(const RpgBlockTransferMetadata *metadata,
                                                  RpgGridCell expectedCell, int expectedBlockType)
{
    if (metadata == NULL || expectedCell.row < 0 || expectedCell.row >= RPG_STAGE_ROWS ||
        expectedCell.column < 0 || expectedCell.column >= RPG_STAGE_WORLD_COLUMNS ||
        expectedBlockType <= 0) return false;
    return metadata->blockType == expectedBlockType &&
           metadata->cell.row == expectedCell.row && metadata->cell.column == expectedCell.column;
}

bool RpgObjectFolder_ReadBlockTransferMetadata(const RpgObjectFolder *folder, int blockType,
                                               bool inZipper,
                                               RpgBlockTransferMetadata *metadata)
{
#ifdef _WIN32
    char directory[1200], infoPath[1200], line[128];
    wchar_t wideInfoPath[1200];
    FILE *file;
    RpgBlockTransferMetadata parsed = { .blockType = 0, .cell = { -1, -1 },
                                        .position = { NAN, NAN } };
    bool haveType = false, haveCellRow = false, haveCellColumn = false;
    if (folder == NULL || metadata == NULL ||
        !BlockPath(folder, blockType, inZipper, directory, sizeof(directory)) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", directory) <= 0 ||
        !ToWide(infoPath, wideInfoPath, (int)(sizeof(wideInfoPath) / sizeof(wideInfoPath[0]))) ||
        (file = _wfopen(wideInfoPath, L"rb")) == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "type=%d", &parsed.blockType) == 1) { haveType = true; continue; }
        if (sscanf(line, "cell_row=%d", &parsed.cell.row) == 1) { haveCellRow = true; continue; }
        if (sscanf(line, "cell_column=%d", &parsed.cell.column) == 1) { haveCellColumn = true; continue; }
    }
    fclose(file);
    if (!haveType || parsed.blockType <= 0) return false;
    /* Legacy folders did not carry explicit grid coordinates.  Their folder
       name/path is the only reliable identity; x/y may be a world position.
       The next materialisation upgrades such a record to the explicit form. */
    if (haveCellRow != haveCellColumn) return false;
    if (!haveCellRow) parsed.cell = folder->cell;
    parsed.position = (Vector2){ (parsed.cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                 (parsed.cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    if (!RpgObjectFolder_BlockTransferMetadataMatches(&parsed, parsed.cell, parsed.blockType)) return false;
    *metadata = parsed;
    return true;
#else
    (void)folder; (void)blockType; (void)inZipper; (void)metadata;
    return false;
#endif
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

/* Folder objects deliberately contain user-visible files only.  Unlike block
   folders, they do not own object_info.txt: their persistent ID, kind and
   position live in rpg_reference_objects.cfg.  Keep this legacy entry point
   solely to materialize the directory and purge metadata written by old
   builds. */
static bool WriteReferenceFolderInfo(const char *folder, RpgGridCell cell)
{
#ifdef _WIN32
    static const char *legacyNames[] = { "object_info.txt", "reference_info.txt" };
    char path[1200];
    wchar_t widePath[1200];
    /* A legacy Folder record can contain only a display name such as
       "Folder_9".  CreateDirectoryW resolves that against the executable's
       current directory (build/), which must never become an object store.
       All current callers resolve movable folders to an absolute Stage runtime
       path first; reject anything else as a final safety net. */
    if (folder == NULL || folder[0] == '\0' ||
        !((folder[0] != '\0' && folder[1] == ':') ||
          (folder[0] == '\\' && folder[1] == '\\')) ||
        !CreateFolderUtf8(folder)) return false;
    for (int index = 0; index < (int)(sizeof(legacyNames) / sizeof(legacyNames[0])); index++) {
        if (snprintf(path, sizeof(path), "%s\\%s", folder, legacyNames[index]) > 0 &&
            ToWide(path, widePath, (int)(sizeof(widePath) / sizeof(widePath[0]))))
            (void)DeleteFileW(widePath);
    }
    (void)cell;
    return true;
#else
    (void)folder; (void)cell;
    return false;
#endif
}

/* Both records are legacy metadata.  Folder-object identity belongs to the
   stage's movable-object record, never to files inside the visible folder. */
static void RemoveLegacyReferenceFolderInfo(const char *folder)
{
#ifdef _WIN32
    static const char *legacyNames[] = { "object_info.txt", "reference_info.txt" };
    char path[1200];
    wchar_t widePath[1200];
    if (folder == NULL || folder[0] == '\0') return;
    for (int index = 0; index < (int)(sizeof(legacyNames) / sizeof(legacyNames[0])); index++)
        if (snprintf(path, sizeof(path), "%s\\%s", folder, legacyNames[index]) > 0 &&
            ToWide(path, widePath, (int)(sizeof(widePath) / sizeof(widePath[0]))))
            (void)DeleteFileW(widePath);
#else
    (void)folder;
#endif
}

/* 保存方式はパスを知らず、ここから渡す最小のファイル操作だけを利用する。 */
static bool WriteBuildCellFolder(void *context, RpgGridCell cell, int blockType)
{
    RpgObjectFolder folder = { .cell = cell };
    char cellPath[1200];
    (void)context;
    /* File/Folder は地形セルではない。専用の外部オブジェクト構成だけに保存し、cells には作らない。 */
    if (RpgBlockInventory_IsReferenceObject(blockType)) return true;
    return BlockPathDirect(&folder, blockType, false, cellPath, sizeof(cellPath)) &&
           WriteBlockObjectInfo(cellPath, blockType, cell);
}

/* The incremental cell worker calls this before it writes the new value.  It
   is deliberately limited to direct, non-composite cell folders: references
   and composite groups have owners outside one grid cell and stay on the
   map/object repair path. */
static bool RemoveBuildCellFolder(void *context, RpgGridCell cell, int blockType)
{
    RpgObjectFolder folder = { .cell = cell };
    char cellPath[1200];
    (void)context;
    if (RpgBuildCellStorage_UsesMetadataForBlock(blockType)) return true;
    if (!BlockPathDirect(&folder, blockType, false, cellPath, sizeof(cellPath))) return false;
    if (!FolderExistsUtf8(cellPath)) return true;
    RemoveTree(cellPath);
    return !FolderExistsUtf8(cellPath);
}

/* `requestedMaps == NULL` means every active map.  The targeted form is used
   by editor-preview repair so a composite spanning several dirty areas is
   collected once, rather than once for every area it touches. */
static bool CollectEffectComponentFoldersForSelection(
    const RpgStage *stage, const bool requestedMaps[RPG_STAGE_MAP_COUNT],
    bool requestedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS])
{
    int collected[RPG_COMPOSITE_OBJECT_MAX_COUNT] = { 0 };
    int collectedCount = 0;
    if (stage == NULL) return false;
    /* Every composite owns one ID directory.  The direct cell folders are
       created first, then all of them -- including the placement-origin cell
       -- are moved beneath that directory. */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        /* A magnet may span more than one editor area, so a one-area buffer
           would make the common composite enumerator reject it. */
        RpgGridCell cells[RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS];
        int count = 0;
        const int objectId = RpgStage_GetCompositeObjectIdAtCell(stage, row, column);
        bool alreadyCollected = false;
        for (int seen = 0; seen < collectedCount; seen++)
            if (collected[seen] == objectId) { alreadyCollected = true; break; }
        if (objectId <= 0 || alreadyCollected || !RpgStage_GetCompositeCellsAtCell(stage, row, column, cells,
                RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS, &count)) continue;
        if (collectedCount >= (int)(sizeof(collected) / sizeof(collected[0]))) return false;
        collected[collectedCount++] = objectId;
        bool isRequested = requestedMaps == NULL && requestedCells == NULL;
        for (int index = 0; index < count; index++) {
            const RpgGridCell componentCell = cells[index];
            if (requestedMaps != NULL &&
                requestedMaps[componentCell.column / RPG_STAGE_COLUMNS])
                isRequested = true;
            if (requestedCells != NULL &&
                requestedCells[componentCell.row][componentCell.column])
                isRequested = true;
        }
        if (!isRequested) continue;
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
            if (!BlockPathDirect(&component, componentType, false, source, sizeof(source)) ||
                !BlockName(&component, componentType, name, sizeof(name)) ||
                snprintf(destination, sizeof(destination), "%s\\%s", componentsPath, name) <= 0)
                return false;
            if (!FolderExistsUtf8(destination) && FolderExistsUtf8(source) &&
                !MoveDirectory(source, destination)) return false;
            if (!FolderExistsUtf8(destination)) continue; /* incremental preview: try next pass */
            if (!BlockObjectInfoMatches(destination, componentType, cells[index]) &&
                !WriteBlockObjectInfo(destination, componentType, cells[index])) return false;
        }
        /* object_info belongs to the group, never to a component directory.
           It nevertheless needs the group's real type and stable origin so a
           Zipper transfer can validate the one composite object rather than
           treating an arbitrary component as a standalone block. */
        {
            int objectType = stage->blocks[row][column];
            RpgGridCell objectCell = { row, column };
            for (int objectIndex = 0; objectIndex < stage->compositeObjectCount; objectIndex++) {
                const RpgCompositeObject *object = &stage->compositeObjects[objectIndex];
                if (object->id != objectId) continue;
                objectType = object->blockType;
                objectCell = (RpgGridCell){ object->originRow, object->originColumn };
                break;
            }
            if (!BlockObjectInfoMatches(groupPath, objectType, objectCell) &&
                !WriteBlockObjectInfo(groupPath, objectType, objectCell)) return false;
        }
    }
    return true;
}

static bool CollectEffectComponentFoldersForMaps(const RpgStage *stage,
                                                 const bool requestedMaps[RPG_STAGE_MAP_COUNT])
{
    return CollectEffectComponentFoldersForSelection(stage, requestedMaps, NULL);
}

static bool CollectEffectComponentFoldersForCells(
    const RpgStage *stage, bool requestedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS])
{
    return CollectEffectComponentFoldersForSelection(stage, NULL, requestedCells);
}

static bool CollectEffectComponentFolders(const RpgStage *stage, int onlyMapIndex)
{
    bool requestedMaps[RPG_STAGE_MAP_COUNT] = { false };
    if (onlyMapIndex < 0) return CollectEffectComponentFoldersForMaps(stage, NULL);
    if (onlyMapIndex >= RPG_STAGE_MAP_COUNT) return false;
    requestedMaps[onlyMapIndex] = true;
    return CollectEffectComponentFoldersForMaps(stage, requestedMaps);
}

/* A compact preview can legitimately contain the component folders of a
 * composite before its later background repair has collected them into the
 * composite object directory.  Zipper must not depend on that repair having
 * happened: the transfer boundary itself makes just its target canonical.
 *
 * This is intentionally not a global repair.  It touches only the component
 * cells for the requested composite, then uses the same collector as normal
 * preview/build creation. */
bool RpgObjectFolders_EnsureCompositeFolder(const RpgStage *stage, RpgGridCell cell)
{
#ifdef _WIN32
    RpgGridCell components[RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS];
    bool requestedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
    char groupPath[1200], groupedComponentsPath[1200];
    int componentCount = 0;
    int compositeId;
    if (stage == NULL || activeBuildPath[0] == '\0' ||
        cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;

    /* The caller can select any component.  Refresh this small ownership
       snapshot from the current stage so its path resolves to one group. */
    SetBuildCellOwners(stage);
    compositeId = RpgStage_GetCompositeObjectIdAtCell(stage, cell.row, cell.column);
    if (compositeId <= 0) return true;
    if (!RpgStage_GetCompositeCellsAtCell(stage, cell.row, cell.column, components,
                                           RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS,
                                           &componentCount) || componentCount <= 0)
        return false;
    if (!CompositePath(compositeId, false, groupPath, sizeof(groupPath)) ||
        snprintf(groupedComponentsPath, sizeof(groupedComponentsPath), "%s\\components", groupPath) <= 0)
        return false;

    for (int index = 0; index < componentCount; index++) {
        const RpgGridCell component = components[index];
        const int componentType = stage->blocks[component.row][component.column];
        RpgObjectFolder componentFolder = { .cell = component };
        char directPath[1200], componentName[256], groupedPath[1200];
        if (componentType <= 0 || RpgStage_IsCellError(stage, component.row, component.column) ||
            !BlockPathDirect(&componentFolder, componentType, false, directPath, sizeof(directPath)) ||
            !BlockName(&componentFolder, componentType, componentName, sizeof(componentName)) ||
            snprintf(groupedPath, sizeof(groupedPath), "%s\\%s", groupedComponentsPath, componentName) <= 0)
            return false;
        /* Special composite components are folder-backed.  Recreate only a
           missing direct component; existing component folders retain their
           visible contents and metadata until the common collector moves
           them beneath the group. */
        if (!FolderExistsUtf8(directPath) && !FolderExistsUtf8(groupedPath) &&
            !WriteBuildCellFolder(NULL, component, componentType))
            return false;
        requestedCells[component.row][component.column] = true;
    }
    return CollectEffectComponentFoldersForCells(stage, requestedCells);
#else
    (void)stage; (void)cell;
    return false;
#endif
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
                                         .removeCellFolder = RemoveBuildCellFolder,
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
        !CreateFolderUtf8(referenceFolders) || !CreateFolderUtf8(runtimeFolder) ||
        !WriteReferenceFolderInfo(runtimeFolder, cell)) return false;
    RemoveLegacyReferenceFolderInfo(runtimeFolder);
    return true;
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
        const char *relativePath = child + strlen(root);
        while (*relativePath == '\\' || *relativePath == '/') relativePath++;
        unsigned long long pathHash = HashPath(relativePath);
        if (recordBaseline) {
            (*fileCount)++;
            *totalBytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            if (shot->baselinePathCount < RPG_DATA_SHOT_BASELINE_FILE_MAX)
                shot->baselinePathHashes[shot->baselinePathCount++] = pathHash;
        } else if (!IsDataShotBaselinePath(shot, pathHash)) {
            (*fileCount)++;
            *totalBytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            *hasAddedFiles = true;
        }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
}

/* The payload cache intentionally uses the same raw file-count/byte-count
 * definition that data shots historically used.  It scans the shooter's own
 * folder ahead of time, instead of doing equivalent work on the input frame
 * that fires a shot. */
static void GetFolderPayloadStats(const char *directory, int *fileCount,
                                  unsigned long long *totalBytes)
{
    char search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (directory == NULL || fileCount == NULL || totalBytes == NULL ||
        !FolderExistsUtf8(directory) || snprintf(search, sizeof(search), "%s\\*", directory) <= 0 ||
        !ToWide(search, wideSearch, 1200)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[1024], child[1200];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(child, sizeof(child), "%s\\%s", directory, name) <= 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            GetFolderPayloadStats(child, fileCount, totalBytes);
            continue;
        }
        (*fileCount)++;
        *totalBytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
}

static RpgShooterPayloadWatch *FindShooterPayloadWatch(int attachmentId, bool create)
{
    for (int index = 0; index < shooterPayloadWatchCount; index++)
        if (shooterPayloadWatches[index].attachmentId == attachmentId)
            return &shooterPayloadWatches[index];
    if (!create || attachmentId <= 0) return NULL;
    if (shooterPayloadWatchCount >= shooterPayloadWatchCapacity) {
        int capacity = shooterPayloadWatchCapacity > 0 ? shooterPayloadWatchCapacity * 2 : 8;
        RpgShooterPayloadWatch *grown = realloc(shooterPayloadWatches,
                                                 (size_t)capacity * sizeof(*grown));
        if (grown == NULL) return NULL;
        shooterPayloadWatches = grown;
        shooterPayloadWatchCapacity = capacity;
    }
    RpgShooterPayloadWatch *watch = &shooterPayloadWatches[shooterPayloadWatchCount++];
    *watch = (RpgShooterPayloadWatch){ .attachmentId = attachmentId,
                                       .monitorHandle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE,
                                       .dirty = true };
    return watch;
}

static void OnShooterPayloadFolderChanged(void *context, const char *directory)
{
    const int attachmentId = (int)(intptr_t)context;
    RpgShooterPayloadWatch *watch = FindShooterPayloadWatch(attachmentId, false);
    (void)directory;
    if (watch != NULL) watch->dirty = true;
}

static void ClearShooterPayloadWatches(void)
{
    for (int index = 0; index < shooterPayloadWatchCount; index++)
        RpgObjectFolderMonitor_Disconnect(&shooterPayloadWatches[index].monitorHandle);
    free(shooterPayloadWatches);
    shooterPayloadWatches = NULL;
    shooterPayloadWatchCount = 0;
    shooterPayloadWatchCapacity = 0;
}

bool RpgObjectFolders_UpdateShooterPayloadCaches(RpgAttachments *attachments,
                                                 int *updatedAttachmentId)
{
    if (updatedAttachmentId != NULL) *updatedAttachmentId = 0;
    if (attachments == NULL) return false;
    /* One completed folder scan per frame keeps cache refreshes separate from
       input.  A shot always uses the last completed value immediately. */
    for (int index = 0; index < attachments->count; index++) {
        RpgAttachment *attachment = &attachments->entries[index];
        RpgShooterConfig *shooter;
        RpgShooterPayloadWatch *watch;
        char source[1200], inbox[1200];
        const char *folder;
        int fileCount = 0;
        unsigned long long totalBytes = 0;
        if (attachment->type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER ||
            RpgAttachments_IsRuntimeUnavailable(attachment) ||
            (shooter = RpgAttachments_FindShooter(attachments, attachment->folderId)) == NULL ||
            (watch = FindShooterPayloadWatch(attachment->folderId, true)) == NULL ||
            !AttachmentOwnerBlockPath(attachment, false, source, sizeof(source)) ||
            !AttachmentOwnerBlockPath(attachment, true, inbox, sizeof(inbox))) continue;
        folder = FolderExistsUtf8(source) ? source : (FolderExistsUtf8(inbox) ? inbox : NULL);
        if (folder == NULL) continue;
        watch->monitorHandle = RpgObjectFolderMonitor_Connect(
            watch->monitorHandle, folder, true, OnShooterPayloadFolderChanged,
            (void *)(intptr_t)attachment->folderId);
        if (!watch->dirty) continue;
        GetFolderPayloadStats(folder, &fileCount, &totalBytes);
        watch->dirty = false;
        if (!shooter->runtimePayloadReady || shooter->runtimePayloadFileCount != fileCount ||
            shooter->runtimePayloadTotalBytes != totalBytes) {
            shooter->runtimePayloadFileCount = fileCount;
            shooter->runtimePayloadTotalBytes = totalBytes;
            shooter->runtimePayloadReady = true;
            if (updatedAttachmentId != NULL) *updatedAttachmentId = attachment->folderId;
            return true;
        }
    }
    return false;
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
    char source[1200], inbox[1200], objects[1200];
    if (!DataShotPath(shot, false, source, sizeof(source)) || !DataShotPath(shot, true, inbox, sizeof(inbox)) ||
        !GetObjectsPath(objects, sizeof(objects))) return false;
    if (FolderExistsUtf8(source) || FolderExistsUtf8(inbox)) return true;
    if (attachments == NULL || shot->attachmentIndex < 0 || shot->attachmentIndex >= attachments->count) return false;
    /* The owner attachment was already persisted by the stage build.  Do not
       rewrite it for every shot: doing so triggers needless file monitoring
       and shell refresh work on the input frame. */
    // 発射時点の装置フォルダ全体を parent に入れ、弾自身の object_info.txt は root に残す。
    bool created = CreateFolderUtf8(objects) &&
                   WriteObjectInfo(source, 0, shot->position) &&
                   WriteDataShotSpecificInfo(source, shot);
    /* WriteObjectInfo() already creates the folder and refreshes its parent
       hierarchy.  A second hierarchy-wide refresh here is redundant. */
    return created;
}

/* PNGのメタ情報は独立した所有フォルダに書き、セルの存在や当たり判定とは結び付けない。 */
static bool EnsureImageObject(const RpgImageObject *object)
{
    char source[1200], objects[1200], infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    Vector2 position;
    /* PNG objects are editor-owned movable visuals.  They are never moved into
       Zipper's Inbox, so their preview folder must not depend on an active
       Zipper directory.  Requiring the Inbox path here made an otherwise
       ordinary PNG edit fail before the runtime Zipper had been prepared; the
       editor then incorrectly fell back to an area repair. */
    if (object == NULL || !ImageObjectPath(object, false, source, sizeof(source)) ||
        !GetObjectsPath(objects, sizeof(objects))) return false;
    if (FolderExistsUtf8(source)) return true;
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

/* A FILE/PNG placement may be created before its picker has supplied a
 * source path.  It is an incomplete authoring record, not a failed runtime
 * build: there is simply no external payload to materialize yet.  In
 * particular, returning false here used to turn a harmless picker cancel
 * into an editor-wide area repair. */
static bool ReferenceObjectHasSelectedSource(const RpgReferenceObject *object)
{
    return object != NULL &&
           (object->objectKind != RPG_REFERENCE_OBJECT_FILE ||
            object->sourcePath[0] != '\0' || object->path[0] != '\0');
}

static bool ImageObjectHasSelectedSource(const RpgImageObject *object)
{
    return object != NULL && object->path[0] != '\0';
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
    /* The owner tick polls every shared folder monitor once.  Polling from
       every active data shot made N shots poll all N monitors, turning a
       single frame into N^2 Win32 handle checks. */
    if (!shot->folderMonitorDirty) return;
    // 初回も監視を開始する。以後は配下の変更通知が届いた時だけ集計する。
    GetDataShotFolderStats(folder, folder, shot, recordBaseline, &fileCount, &totalBytes, &hasAddedFiles);
    if (recordBaseline) {
        shot->folderStatsInitialized = true;
        shot->folderMonitorDirty = false;
        return;
    }
    shot->folderMonitorDirty = false;
    fileCount += shot->payloadFileCount;
    totalBytes += shot->payloadTotalBytes;
    if (fileCount == shot->fileCount && totalBytes == shot->totalBytes &&
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

bool RpgObjectFolder_MaterializeDataShot(RpgDataShot *shot, const RpgAttachments *attachments)
{
    if (shot == NULL || !shot->active || shot->isPreview) return false;
    if (!EnsureDataShot(shot, attachments)) return false;

    /* The shooter cache supplied the initial payload before this call.  The
       only synchronous scan is this two-file baseline, so later filesystem
       changes can be layered on top without treating metadata as payload. */
    shot->folderStatsInitialized = false;
    shot->folderMonitorDirty = true;
    /* UpdateDataShotProperties() performs the shared monitor connection when
       it consumes this dirty state.  Connecting here as well was redundant. */
    UpdateDataShotProperties(-1, shot, attachments);
    if (!shot->folderStatsInitialized) return false;

    shot->folderMaterialized = true;
    return true;
}

static bool QueueDataShotFolderMaterialization(RpgDataShot *shot, int slotIndex)
{
    RpgDataShotFolderWork work = { 0 };
    if (shot == NULL || slotIndex < 0 || slotIndex >= RPG_DATA_SHOT_MAX_COUNT ||
        shot->folderSerial <= 0 || shot->folderMaterializationQueued ||
        !DataShotPath(shot, false, work.folder, sizeof(work.folder))) return false;
    work.slotIndex = slotIndex;
    work.serial = shot->folderSerial;
    work.attachmentIndex = shot->attachmentIndex;
    work.pathCellIndex = shot->pathCellIndex;
    work.position = shot->position;
    if (!QueueDataShotFolderWork(&work)) return false;
    shot->folderMaterializationQueued = true;
    return true;
}

static void SetKnownDataShotFolderBaseline(RpgDataShot *shot)
{
    if (shot == NULL) return;
    shot->baselinePathCount = 2;
    shot->baselinePathHashes[0] = HashPath("object_info.txt");
    shot->baselinePathHashes[1] = HashPath("data_shot_info.txt");
    shot->folderStatsInitialized = true;
    shot->folderMonitorDirty = false;
}

static bool CompleteDataShotFolderMaterialization(RpgDataShot *shot)
{
    char source[1200], inbox[1200];
    const char *folder;
    if (shot == NULL || !DataShotPath(shot, false, source, sizeof(source)) ||
        !DataShotPath(shot, true, inbox, sizeof(inbox))) return false;
    folder = FolderExistsUtf8(source) ? source : (FolderExistsUtf8(inbox) ? inbox : NULL);
    if (folder == NULL) return false;
    shot->folderMonitorHandle = RpgObjectFolderMonitor_Connect(
        shot->folderMonitorHandle, folder, true, OnDataShotFolderChanged, shot);
    /* The worker has written exactly these two baseline files.  Avoid a
       redundant recursive enumeration on the gameplay thread. */
    SetKnownDataShotFolderBaseline(shot);
    shot->folderMaterializationQueued = false;
    shot->folderMaterializationDelayFrames = 0;
    shot->folderMaterialized = true;
    return true;
}

static void QueueDataShotFolderRemoval(const char *folder)
{
    RpgDataShotFolderWork work = { .removeOnly = true };
    if (folder == NULL || snprintf(work.folder, sizeof(work.folder), "%s", folder) <= 0) return;
    (void)QueueDataShotFolderWork(&work);
}

static void ProcessDataShotFolderWorkerResults(RpgDataShots *shots)
{
    RpgDataShotFolderWorkResult result;
    int processed = 0;
    if (shots == NULL) return;
    while (processed < 4 && PopDataShotFolderWorkResult(&result)) {
        RpgDataShot *shot = result.slotIndex >= 0 && result.slotIndex < RPG_DATA_SHOT_MAX_COUNT ?
                             &shots->entries[result.slotIndex] : NULL;
        processed++;
        if (shot == NULL || !shot->active || shot->folderSerial != result.serial || shot->isZipperHeld) {
            if (result.succeeded) QueueDataShotFolderRemoval(result.folder);
            continue;
        }
        shot->folderMaterializationQueued = false;
        if (result.succeeded && CompleteDataShotFolderMaterialization(shot)) {
            dataShotFolderSerials[result.slotIndex] = result.serial;
            continue;
        }
        /* Keep the cached shot alive and retry an I/O failure later. */
        shot->folderMaterializationDelayFrames = 2;
    }
}

/* データ弾はマスを占有しないため、フォルダに最後のグリッド位置と親装置上の軌道位置だけを残す。 */
static bool WriteDataShotRuntimeMetadata(RpgDataShot *shot)
{
    char source[1200], inbox[1200], staging[1200];
    const char *folder = NULL;
    if (shot == NULL || !shot->active || shot->isPreview ||
        !DataShotPath(shot, false, source, sizeof(source)) ||
        !DataShotPath(shot, true, inbox, sizeof(inbox))) return false;
    if (FolderExistsUtf8(source)) folder = source;
    else if (FolderExistsUtf8(inbox)) folder = inbox;
    else if (GetReturnStagingPath(source, staging, sizeof(staging)) && FolderExistsUtf8(staging)) folder = staging;
    if (folder == NULL || !WriteObjectInfo(folder, 0, shot->position) ||
        !WriteDataShotSpecificInfo(folder, shot)) return false;
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
        return WriteBlockObjectInfo(inbox, storedBlockType, folder->cell);
    }
    // 通常ブロックは普段は仮想的にだけ存在し、アニメーション時にメタ情報付きで実体化する。
    return WriteBlockObjectInfo(inbox, blockType, folder->cell);
}

/* This is intentionally an eat-time operation, not a global pre-sync.  A
   block becomes a real folder only because Zipper is taking it, and that
   folder is complete before its single move into Zipper. */
static bool MaterializeBlockWithAttachmentsInInbox(const RpgObjectFolder *folder, int blockType,
                                                    const RpgAttachments *attachments,
                                                    const RpgReceivers *receivers,
                                                    const RpgWires *wires,
                                                    RpgBlockTransferMetadata *metadata)
{
    char source[1200], inbox[1200], inboxParent[1200];
    int storedBlockType = blockType;
    const int ownerComposite = GetBuildCompositeId(folder->cell);
    if (folder == NULL || !BlockPath(folder, blockType, false, source, sizeof(source)) ||
        !BlockPath(folder, blockType, true, inbox, sizeof(inbox)) ||
        !GetInboxPath(inboxParent, sizeof(inboxParent)) || !CreateFolderUtf8(inboxParent)) return false;
    /* A previous eat may already have placed this object in Zipper.  Do not
       report success with an uninitialised transfer record: the Inbox copy is
       authoritative and must be readable before the caller can queue it. */
    if (FolderExistsUtf8(inbox)) {
        return RpgObjectFolder_ReadBlockTransferMetadata(folder, blockType, true, metadata) &&
               RpgObjectFolder_BlockTransferMetadataMatches(metadata, folder->cell, blockType);
    }

    /* Keep the existing compact-cell behavior, but materialize the source
       folder first so child metadata and object_info always share one move. */
    if (!FolderExistsUtf8(source) && activeBuildPath[0] != '\0') {
        RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
        if (!RpgBuildCellStorage_EnsureCell(folder->cell, blockType, &storageBackend) &&
            !ExtractCompactCellMetadata(folder, &storedBlockType)) return false;
    }
    if (!WriteBlockObjectInfo(source, storedBlockType, folder->cell)) return false;
    /* The transferred folder is authoritative.  Verify the snapshot before
       moving it so the caller's in-memory target cannot silently diverge. */
    if (!RpgObjectFolder_ReadBlockTransferMetadata(folder, blockType, false, metadata) ||
        !RpgObjectFolder_BlockTransferMetadataMatches(metadata, folder->cell, storedBlockType)) return false;

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
    if (!MoveDirectory(source, inbox)) return false;
    /* The actual Inbox folder is checked again after the move.  If another
       process changed object_info meanwhile, put it back and reject eat. */
    if (!RpgObjectFolder_ReadBlockTransferMetadata(folder, blockType, true, metadata) ||
        !RpgObjectFolder_BlockTransferMetadataMatches(metadata, folder->cell, storedBlockType)) {
        (void)MoveDirectory(inbox, source);
        return false;
    }
    return true;
}

static void SetStageEffectFolderMissing(RpgStage *stage, RpgGridCell rootCell, int rootType, bool missing)
{
    const RpgEffectShape *shape;
    if (stage == NULL || rootCell.row < 0 || rootCell.row >= RPG_STAGE_ROWS ||
        rootCell.column < 0 || rootCell.column >= RPG_STAGE_WORLD_COLUMNS) return;
    shape = RpgBlockInventory_GetEffectShape(rootType);
    if (shape == NULL || shape->rootType != rootType) {
        if (missing) RpgStage_SetCellError(stage, rootCell.row, rootCell.column, rootType);
        return;
    }
    for (int index = 0; index < shape->cellCount; index++) {
        const RpgEffectShapeCell *part = &shape->cells[index];
        int row = rootCell.row + part->offsetY;
        int column = rootCell.column + part->offsetX;
        if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) continue;
        if (missing) RpgStage_SetCellError(stage, row, column, part->blockType);
        else if (RpgStage_IsCellError(stage, row, column))
            RpgStage_SetCellBlock(stage, row, column, part->blockType);
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
    unsigned long long sourceBytes;
    if (sourcePath == NULL || destinationDirectory == NULL || sourcePath[0] == '\0' ||
        destinationDirectory[0] == '\0' || !EnsureRuntimeParentFolder(destinationDirectory) ||
        !CreateFolderUtf8(destinationDirectory) ||
        !MakeStoredFilePath(sourcePath, destinationDirectory, destination, sizeof(destination))) return false;
    if (strcmp(sourcePath, destination) == 0) return true;
    if (!ToWide(sourcePath, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0])) ) ||
        !ToWide(destination, wideDestination, (int)(sizeof(wideDestination) / sizeof(wideDestination[0])) )) return false;
    sourceBytes = MeasurePathBytesUtf8(sourcePath);
    /* 同一内容が既にある場合だけ、コピーなしで完了として扱う。 */
    if (GetFileAttributesW(wideDestination) != INVALID_FILE_ATTRIBUTES) {
        if (!AreFilesEqual(wideSource, wideDestination) || DeleteFileW(wideSource) == 0) return false;
        /* The destination already occupied its capacity.  A discarded
           duplicate changes the ledger only when its source was in Zipper. */
        if (IsPathInsideActiveZipper(sourcePath)) {
            SubtractZipperStorageBytes(sourceBytes);
            RequestZipperStorageAudit();
        }
        RemoveStoredSourceObjectFolder(sourcePath);
        /* A key-door/file insertion can be followed immediately by Stop,
           before ReadDirectoryChangesW has delivered its notification. */
        MarkEditorPreviewPathMutation(destinationDirectory);
        RpgObjectFolderMonitor_NotifyLocalChange(destinationDirectory);
        return true;
    }
    if (MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) == 0) return false;
    UpdateZipperStorageForPathMove(sourcePath, destination, sourceBytes, 0, true);
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
    char inbox[1200], script[1200], legacy[1200];
    wchar_t wideScript[1200], wideLegacy[1200]; FILE *file;
    if (!GetInboxPath(inbox, sizeof(inbox)) || !CreateFolderUtf8(inbox) ||
        snprintf(script, sizeof(script), "%s\\eat.vbs", inbox) <= 0 ||
        !ToWide(script, wideScript, 1200)) return;
    /* VBS is the only user-facing command entrypoint. */
    if (snprintf(legacy, sizeof(legacy), "%s\\eat.cmd", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
    if (snprintf(legacy, sizeof(legacy), "%s\\spit.cmd", inbox) > 0 && ToWide(legacy, wideLegacy, 1200))
        (void)DeleteFileW(wideLegacy);
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
    /* Legacy batch launcher retained only as disabled source history. */
#if 0
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
#endif
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
    if (snprintf(script, sizeof(script), "%s\\spit.vbs", inbox) > 0 &&
        ToWide(script, wideScript, 1200)) {
#if 0
        file = _wfopen(wideCommand, L"wb");
        if (file != NULL) {
            fputs("@echo off\r\ndel /f /q \"%~dp0zipper_command.request\" >nul 2>nul\r\n"
                   "> \"%~dp0zipper_command.request\" echo spit %DATE%_%TIME%_%RANDOM%_%RANDOM%\r\n", file);
            if (fclose(file) == 0) {
                NotifyShellChange(RPG_SHCNE_CREATE, command, NULL);
                NotifyShellPathHierarchyChanged(command);
            }
        }
#endif
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
    wchar_t wideRequest[1200];
    FILE *file;
    bool watcherAvailable;
    ULONGLONG now;
    /* A request is read only after the shared Zipper directory watcher has
       reported a change.  The fallback is intentionally slow and is used
       only if Windows could not create that watcher. */
    if (!EnsureZipperStorageWatcher(NULL, 0) || !GetInboxPath(inbox, sizeof(inbox)) ||
        snprintf(request, sizeof(request), "%s\\zipper_command.request", inbox) <= 0 ||
        !ToWide(request, wideRequest, 1200)) {
        hasDispatchedZipperRequest = false;
        hasPendingZipperRequest = false;
        pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
        return false;
    }
    watcherAvailable = RpgObjectFolderMonitor_IsWatching(zipperStorageMonitorHandle);
    if (!watcherAvailable) {
        /* PollAll reports a failed native watcher as changed while it retries
           the handle.  Throttle even those repeated failure notifications. */
        now = GetTickCount64();
        if (now < zipperCommandFallbackPollAt) {
            zipperCommandRequestDirty = false;
            return false;
        }
        zipperCommandFallbackPollAt = now + 250ULL;
    } else if (!zipperCommandRequestDirty) return false;
    zipperCommandRequestDirty = false;
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

static int FindReferenceObjectById(const RpgReferenceObjects *objects, int id)
{
    if (objects == NULL || id <= 0) return -1;
    for (int index = 0; index < objects->count; index++)
        if (objects->entries[index].id == id) return index;
    return -1;
}

static int FindImageObjectById(const RpgImageObjects *objects, unsigned int id)
{
    if (objects == NULL || id == 0) return -1;
    for (int index = 0; index < objects->count; index++)
        if (objects->entries[index].id == id) return index;
    return -1;
}

/* `path` and several movement fields are runtime state.  An editor-preview
   materialisation rewrites them to paths under Stage/editor, so comparing the
   raw structure makes every previously materialised object look changed on
   the next, unrelated inspector edit.  Keep the preview delta keyed only to
   the authored definition which owns its runtime directory. */
static const char *ReferenceObjectAuthoredIdentity(const RpgReferenceObject *object)
{
    if (object == NULL) return "";
    return object->sourcePath[0] != '\0' ? object->sourcePath : object->path;
}

static bool SameReferenceObjectPreviewDefinition(const RpgReferenceObject *left,
                                                 const RpgReferenceObject *right)
{
    return left != NULL && right != NULL &&
           left->id == right->id &&
           left->objectKind == right->objectKind &&
           left->position.x == right->position.x &&
           left->position.y == right->position.y &&
           left->drawScale == right->drawScale &&
           strcmp(ReferenceObjectAuthoredIdentity(left),
                  ReferenceObjectAuthoredIdentity(right)) == 0;
}

static bool SameImageObjectPreviewDefinition(const RpgImageObject *left,
                                             const RpgImageObject *right)
{
    return left != NULL && right != NULL &&
           left->id == right->id &&
           left->row == right->row && left->column == right->column &&
           left->scale == right->scale &&
           left->layer == right->layer && left->appearance == right->appearance &&
           left->positionX == right->positionX && left->positionY == right->positionY &&
           left->hasCustomPosition == right->hasCustomPosition &&
           strcmp(left->path, right->path) == 0;
}

bool RpgObjectFolders_RefreshEditorPreviewMovableObjectBatch(
    RpgStage *stage, const RpgReferenceObjects *previousReferences,
    const RpgImageObjects *previousImages)
{
#ifdef _WIN32
    if (stage == NULL || previousReferences == NULL || previousImages == NULL ||
        activeBuildPath[0] == '\0' || activeBuildPersists) {
        TraceEditorPreviewMovableBatch("batch-precondition-failed", 0, "stage/build state");
        return false;
    }
    TraceEditorPreviewMovableBatch("batch-begin", 0, "direct movable refresh");

    /* Older records can have only a former runtime path.  Normalize the
       current preview record before semantic comparison so that one legacy
       object cannot request a rewrite after every unrelated edit. */
    RpgReferenceObjects_EnsureSourcePaths(&stage->referenceObjects);

    /* Remove only directories whose owning record changed or disappeared.
       A reference FILE uses an ID-owned directory; a Folder may additionally
       change its visible directory name, so the old record is the only safe
       way to find stale runtime state without touching its whole area. */
    for (int index = 0; index < previousReferences->count; index++) {
        const RpgReferenceObject *previous = &previousReferences->entries[index];
        const int currentIndex = FindReferenceObjectById(&stage->referenceObjects, previous->id);
        if (currentIndex >= 0 &&
            SameReferenceObjectPreviewDefinition(previous,
                                                 &stage->referenceObjects.entries[currentIndex]))
            continue;
        char path[1200];
        if (ReferenceObjectDirectory(previous, false, path, sizeof(path))) RemoveTree(path);
    }
    for (int index = 0; index < previousImages->count; index++) {
        const RpgImageObject *previous = &previousImages->entries[index];
        const int currentIndex = FindImageObjectById(&stage->imageObjects, previous->id);
        if (currentIndex >= 0 &&
            SameImageObjectPreviewDefinition(previous,
                                             &stage->imageObjects.entries[currentIndex]))
            continue;
        char path[1200];
        if (ImageObjectPath(previous, false, path, sizeof(path))) RemoveTree(path);
    }

    for (int index = 0; index < stage->referenceObjects.count; index++) {
        RpgReferenceObject *current = &stage->referenceObjects.entries[index];
        const int previousIndex = FindReferenceObjectById(previousReferences, current->id);
        if (previousIndex >= 0 &&
            SameReferenceObjectPreviewDefinition(&previousReferences->entries[previousIndex], current))
            continue;
        if (!ReferenceObjectHasSelectedSource(current)) {
            TraceEditorPreviewMovableBatch("reference-skip-unselected", current->id,
                                           "file source not selected");
            continue;
        }
        if (!RpgObjectFolders_EnsureReferenceObject(current)) {
            TraceEditorPreviewMovableBatch("reference-ensure-failed", current->id,
                                           current->sourcePath[0] != '\0' ? current->sourcePath : current->path);
            return false;
        }
        if (current->objectKind == RPG_REFERENCE_OBJECT_FOLDER) {
            int row, column;
            /* Folder objects are movable objects.  Their visible directory
               does not require a terrain cell, and the legacy metadata writer
               only removes old metadata.  A transient/off-stage position must
               therefore never invalidate the entire preview cache. */
            if (RpgStage_GetWorldCellAtPosition(stage, current->position, &row, &column) &&
                !WriteRuntimeReferenceObjectFolderInfo(current, (RpgGridCell){ row, column })) {
                TraceEditorPreviewMovableBatch("reference-info-failed", current->id,
                                               current->sourcePath[0] != '\0' ? current->sourcePath : current->path);
                return false;
            }
        }
    }
    for (int index = 0; index < stage->imageObjects.count; index++) {
        const RpgImageObject *current = &stage->imageObjects.entries[index];
        const int previousIndex = FindImageObjectById(previousImages, current->id);
        if (previousIndex >= 0 &&
            SameImageObjectPreviewDefinition(&previousImages->entries[previousIndex], current))
            continue;
        if (!ImageObjectHasSelectedSource(current)) {
            TraceEditorPreviewMovableBatch("image-skip-unselected", current->id,
                                           "png source not selected");
            continue;
        }
        if (!EnsureImageObject(current)) {
            TraceEditorPreviewMovableBatch("image-ensure-failed", current->id, current->path);
            return false;
        }
    }
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    TraceEditorPreviewMovableBatch("batch-succeeded", 0, "direct movable refresh");
    return true;
#else
    (void)stage; (void)previousReferences; (void)previousImages;
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

bool RpgObjectFolder_MoveDataShotToZipper(RpgDataShot *shot, const RpgAttachments *attachments)
{
#ifdef _WIN32
    char source[1200], inbox[1200], parent[1200];
    /* Capturing a just-fired shot must not pull pending disk I/O back onto
       the input path.  It remains in the world until the cooperative folder
       task has made its real object folder. */
    if (shot == NULL || !shot->folderMaterialized) return false;
    if (!EnsureDataShot(shot, attachments)) return false;
    (void)WriteDataShotRuntimeMetadata(shot);
    return DataShotPath(shot, false, source, sizeof(source)) &&
           DataShotPath(shot, true, inbox, sizeof(inbox)) && GetInboxPath(parent, sizeof(parent)) && CreateFolderUtf8(parent) &&
           (FolderExistsUtf8(inbox) || MoveDirectory(source, inbox));
#else
    (void)shot; (void)attachments; return false;
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
                                                       const RpgWires *wires,
                                                       RpgBlockTransferMetadata *metadata)
{
#ifdef _WIN32
    bool moved = (blockType != 0 || activeBuildPath[0] != '\0') &&
                 MaterializeBlockWithAttachmentsInInbox(folder, blockType, attachments, receivers, wires,
                                                        metadata);
    if (moved && folder != NULL)
        RpgObjectFolders_MarkEditorPreviewCellMutation(folder->cell, blockType);
    return moved;
#else
    (void)folder; (void)blockType; (void)attachments; (void)receivers; (void)wires; (void)metadata;
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
    char source[1200], inbox[1200];
    if (activeBuildPath[0] == '\0' || !BlockPath(folder, blockType, false, source, sizeof(source)) ||
        !BlockPath(folder, blockType, true, inbox, sizeof(inbox))) return false;
    /* Compact方式でも、取得済みマスはcells内の実フォルダのまま返して読み取る。 */
    return RpgObjectFolder_ReturnBlockFolderToBuild(inbox, folder, blockType);
#else
    (void)folder; (void)blockType; return false;
#endif
}

/* Complete the physical half of a terrain-block return.  `returnSource` may
 * be Zipper's folder or a movable Folder; CompleteFolderReturn prefers the
 * common target.returning staging directory in either case. */
bool RpgObjectFolder_ReturnBlockFolderToBuild(const char *returnSource,
                                              const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    char target[1200], cells[1200];
    bool returned;
    if (returnSource == NULL || returnSource[0] == '\0' || folder == NULL || activeBuildPath[0] == '\0' ||
        !BlockPath(folder, blockType, false, target, sizeof(target)) ||
        !GetCellsPath(cells, sizeof(cells)) || !CreateFolderUtf8(cells)) return false;
    returned = CompleteFolderReturn(returnSource, target);
    if (returned) RpgObjectFolders_MarkEditorPreviewCellMutation(folder->cell, blockType);
    return returned;
#else
    (void)returnSource; (void)folder; (void)blockType; return false;
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

bool RpgObjectFolder_DiscardReferenceFolder(const RpgReferenceObject *object)
{
#ifdef _WIN32
    char source[1200];
    bool removed = object != NULL && object->objectKind == RPG_REFERENCE_OBJECT_FOLDER &&
                   activeBuildPath[0] != '\0' &&
                   ReferenceObjectDirectory(object, false, source, sizeof(source));
    if (removed) {
        RemoveTree(source);
        removed = !FolderExistsUtf8(source);
    }
    if (removed) MarkEditorPreviewPositionMutation(object->position);
    return removed;
#else
    (void)object;
    return false;
#endif
}

bool RpgObjectFolder_ReturnReferenceFolderToDynamicBlock(const RpgReferenceObject *object,
                                                         RpgGridCell identityCell, int blockType,
                                                         Vector2 position)
{
#ifdef _WIN32
    char source[1200], target[1200];
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER ||
        (!RpgBlockInventory_IsMetalBlock(blockType) &&
         !RpgBlockInventory_IsPushBlock(blockType)) || activeBuildPath[0] == '\0' ||
        !ReferenceObjectDirectory(object, false, source, sizeof(source)) ||
        !DynamicBlockPath(identityCell, blockType, false, target, sizeof(target)) ||
        !CompleteFolderReturn(source, target)) return false;
    return WriteObjectInfo(target, blockType, position);
#else
    (void)object; (void)identityCell; (void)blockType; (void)position;
    return false;
#endif
}

bool RpgObjectFolder_PromoteReferenceFolderToBlock(const RpgReferenceObject *object,
                                                   const RpgObjectFolder *destination,
                                                   int blockType)
{
#ifdef _WIN32
    char source[1200];
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER || destination == NULL ||
        !RpgBlockInventory_IsOrdinaryBlock(blockType) || activeBuildPath[0] == '\0' ||
        !ReferenceObjectDirectory(object, false, source, sizeof(source))) return false;
    return RpgObjectFolder_ReturnBlockFolderToBuild(source, destination, blockType);
#else
    (void)object; (void)destination; (void)blockType;
    return false;
#endif
}

bool RpgObjectFolder_BeginReturnReferenceFolderAsBlock(const RpgReferenceObject *object,
                                                        const RpgObjectFolder *destination,
                                                        int blockType)
{
#ifdef _WIN32
    char source[1200], target[1200];
    if (object == NULL || object->objectKind != RPG_REFERENCE_OBJECT_FOLDER || destination == NULL ||
        !RpgBlockInventory_IsOrdinaryBlock(blockType) || activeBuildPath[0] == '\0' ||
        !ReferenceObjectDirectory(object, false, source, sizeof(source)) ||
        !BlockPath(destination, blockType, false, target, sizeof(target))) return false;
    return BeginFolderReturn(source, target);
#else
    (void)object; (void)destination; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_CancelReturnReferenceFolderAsBlock(const char *returnSource,
                                                         const RpgObjectFolder *destination,
                                                         int blockType)
{
#ifdef _WIN32
    char target[1200], staging[1200];
    if (returnSource == NULL || returnSource[0] == '\0' || destination == NULL ||
        !RpgBlockInventory_IsOrdinaryBlock(blockType) || activeBuildPath[0] == '\0' ||
        !BlockPath(destination, blockType, false, target, sizeof(target)) ||
        !GetReturnStagingPath(target, staging, sizeof(staging))) return false;
    return FolderExistsUtf8(staging) && MoveDirectory(staging, returnSource);
#else
    (void)returnSource; (void)destination; (void)blockType; return false;
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

#ifdef _WIN32
static void RemoveOwnedAttachmentMetadata(const char *ownerPath)
{
    char search[1200];
    wchar_t wideSearch[1200], widePath[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (ownerPath == NULL ||
        snprintf(search, sizeof(search), "%s\\*", ownerPath) <= 0 ||
        !ToWide(search, wideSearch, (int)(sizeof(wideSearch) / sizeof(wideSearch[0])))) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[512], path[1200];
        const bool isOwnedMetadata =
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) > 0 &&
            (strncmp(name, "attachment_", 11) == 0 ||
             strncmp(name, "receiver_", 9) == 0 ||
             strncmp(name, "wire_receiver_", 14) == 0);
        if (!isOwnedMetadata || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            snprintf(path, sizeof(path), "%s\\%s", ownerPath, name) <= 0 ||
            !ToWide(path, widePath, (int)(sizeof(widePath) / sizeof(widePath[0])))) continue;
        (void)DeleteFileW(widePath);
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
}

static bool CellBatchContainsOwner(
    bool cells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS], RpgGridCell owner)
{
    if (cells == NULL || owner.row < 0 || owner.row >= RPG_STAGE_ROWS ||
        owner.column < 0 || owner.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!cells[row][column]) continue;
        if (RpgObjectFolders_HaveSameBlockOwner(owner, (RpgGridCell){ row, column })) return true;
    }
    return false;
}
#endif

bool RpgObjectFolders_RefreshEditorPreviewCellBatch(
    const RpgStage *stage, const RpgAttachments *attachments,
    const RpgReceivers *receivers, const RpgWires *wires,
    bool cells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
    const int *removedCompositeIds, int removedCompositeCount)
{
#ifdef _WIN32
    bool clearedOwners[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
    if (stage == NULL || cells == NULL || activeBuildPath[0] == '\0' || activeBuildPersists) return false;

    /* A root folder is the old composite's complete ownership boundary.  It
       must be removed once before direct component folders are recollected;
       doing this per component would either repeat IO or remove a fresh group
       whose ID is retained after a resize. */
    for (int index = 0; index < removedCompositeCount; index++) {
        char oldGroupPath[1200];
        bool seen = false;
        if (removedCompositeIds == NULL || removedCompositeIds[index] <= 0) continue;
        for (int prior = 0; prior < index; prior++)
            if (removedCompositeIds[prior] == removedCompositeIds[index]) { seen = true; break; }
        if (!seen && CompositePath(removedCompositeIds[index], false, oldGroupPath, sizeof(oldGroupPath)))
            RemoveTree(oldGroupPath);
    }

    /* All following owner/path calculations describe the final snapshot. */
    SetBuildCellOwners(stage);
    if (!CollectEffectComponentFoldersForCells(stage, cells)) return false;

    /* Remove stale attachment/receiver records once per actual owner.  This
       is what lets a removed or moved attachment use the same delta path as
       a newly added one without retaining an obsolete .meta file. */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgGridCell cell, owner;
        RpgObjectFolder folder;
        char ownerPath[1200];
        if (!cells[row][column]) continue;
        cell = (RpgGridCell){ row, column };
        owner = GetBuildCellOwner(cell);
        if (clearedOwners[owner.row][owner.column]) continue;
        clearedOwners[owner.row][owner.column] = true;
        folder.cell = owner;
        if (!BlockPath(&folder, stage->blocks[owner.row][owner.column], false,
                       ownerPath, sizeof(ownerPath))) continue;
        if (stage->blocks[owner.row][owner.column] == 0 ||
            RpgStage_IsCellError(stage, owner.row, owner.column)) {
            /* A removed parent cannot retain a metadata-only directory. */
            if (FolderExistsUtf8(ownerPath)) RemoveTree(ownerPath);
            continue;
        }
        if (FolderExistsUtf8(ownerPath)) RemoveOwnedAttachmentMetadata(ownerPath);
        else if (GetBuildCompositeId(owner) <= 0 &&
                 !WriteBlockObjectInfo(ownerPath, stage->blocks[owner.row][owner.column], owner))
            return false;
    }

    if (attachments != NULL) for (int index = 0; index < attachments->count; index++) {
        RpgGridCell owner;
        const RpgAttachment *attachment = &attachments->entries[index];
        if (!RpgAttachments_IsRuntimeUnavailable(attachment) &&
            RpgAttachments_GetOwnerBlockCell(attachment, &owner) &&
            CellBatchContainsOwner(cells, owner) && !EnsureAttachment(attachment)) return false;
    }
    if (receivers != NULL) for (int index = 0; index < RpgReceivers_Count(receivers); index++) {
        RpgReceiver receiver;
        if (RpgReceivers_Get(receivers, index, &receiver) &&
            !RpgReceivers_IsRuntimeUnavailable(&receiver) &&
            CellBatchContainsOwner(cells, receiver.cell) && !EnsureReceiver(&receiver, wires)) return false;
    }
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
    return true;
#else
    (void)stage; (void)attachments; (void)receivers; (void)wires;
    (void)cells; (void)removedCompositeIds; (void)removedCompositeCount;
    return false;
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
    int queuedMaterializationsThisFrame = 0;
    RpgObjectFolderMonitor_PollAll();
    /* Shares the monitor dispatch above; it only scans the Zipper tree after
       that watcher reports an actual directory change. */
    RpgZipperAbilities_Update();
    RpgObjectFolder_UpdateZipperStorageAudit();
    if (shots == NULL) return;
    ProcessDataShotFolderWorkerResults(shots);
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        RpgDataShot *shot = &shots->entries[index];
        // プレビュー弾は設定確認専用であり、実フォルダや File.png を生成・参照しない。
        if (shot->isPreview || shot->isZipperHeld) continue;
        int serial = shot->active ? shot->folderSerial : 0;
        if (serial == dataShotFolderSerials[index]) {
            /* Once materialized, only a real directory notification may
               request another folder scan.  Pending shots are drawn and
               simulated from their cached shooter payload instead. */
            if (serial > 0 && shot->folderMaterialized && shot->folderMonitorDirty &&
                EnsureDataShot(shot, attachments))
                UpdateDataShotProperties(index, shot, attachments);
            continue;
        }
        if (dataShotFolderSerials[index] > 0) {
            RpgDataShot expired = *shot;
            expired.folderSerial = dataShotFolderSerials[index];
            char source[1200], inbox[1200];
            if (DataShotPath(&expired, false, source, sizeof(source)) && DataShotPath(&expired, true, inbox, sizeof(inbox)))
                ReleaseDestroyedDataShotFolder(source, inbox, shot->hitWall ? shot->impactPosition : shot->position,
                                                &expired, referenceObjects);
            dataShotFolderSerials[index] = 0;
        }
        RpgObjectFolderMonitor_Disconnect(&shot->folderMonitorHandle);
        shot->folderMaterialized = false;
        if (serial <= 0) {
            dataShotFolderSerials[index] = 0;
            continue;
        }
        /* Do not perform Win32 I/O in the same frame as the fire input.  The
           shooter cache is already assigned to this visible, moving shot. */
        if (shot->folderMaterializationDelayFrames > 0) {
            shot->folderMaterializationDelayFrames--;
            continue;
        }
        if (shot->folderMaterializationQueued) continue;
        /* Queue submission only copies a small request record.  The Windows
           worker owns directory creation and file writes.  Cap submission so
           a pathological burst cannot monopolize even the queue lock. */
        if (queuedMaterializationsThisFrame >= 4) continue;
        queuedMaterializationsThisFrame++;
        if (!QueueDataShotFolderMaterialization(shot, index)) {
            /* A full queue or a temporary worker setup issue must not erase a
               valid in-world cached shot.  Retry later. */
            shot->folderMaterializationDelayFrames = 1;
            continue;
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
    if (from == NULL || to == NULL) return false;
    RpgObjectFolder_RemoveAttachmentFolder(from);
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
    /* A receiver's route file lives beside its generic attachment metadata.
       Remove it with the attachment; otherwise moving/removing one receiver
       leaves stale editor-preview metadata at the old owner cell. */
    if (attachment != NULL && RpgAttachments_IsReceiver(attachment)) {
        RpgReceiver receiver = { .cell = attachment->cell, .side = attachment->side };
        if (ReceiverWirePath(&receiver, false, source, sizeof(source)) &&
            ToWide(source, wideSource, (int)(sizeof(wideSource) / sizeof(wideSource[0]))))
            (void)DeleteFileW(wideSource);
    }
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
    RpgObjectFolderResumeProfile profile = { 0 };
    double resumeStart = GetTime();
    double phaseStart = resumeStart;
    if (stageNumber <= 0 || stage == NULL || buildPath == NULL || buildPathSize == 0 ||
        !RpgStageStorage_GetRuntimePath(stageNumber, runtimeKind, buildPath, (int)buildPathSize) ||
        !FolderExistsUtf8(buildPath)) return false;
    snprintf(activeBuildPath, sizeof(activeBuildPath), "%s", buildPath);
    activeBuildStageNumber = stageNumber;
    activeBuildPersists = runtimeKind == RPG_STAGE_RUNTIME_GAME;
    RpgReferenceObjects_EnsureSourcePaths(&stage->referenceObjects);
    RpgStage_NormalizeCompositeObjects(stage);
    SetBuildCellOwners(stage);
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);
    /* データ弾など、マスに定着しない実行時オブジェクトは再開時に持ち越さない。 */
    profile.stageSetupMs = (GetTime() - phaseStart) * 1000.0;
    phaseStart = GetTime();
    (void)RpgStageStorage_RepairReferenceFileCopies(stageNumber, stage);
    profile.repairReferenceCopiesMs = (GetTime() - phaseStart) * 1000.0;
    phaseStart = GetTime();
    PrepareRuntimeReferenceFiles(stage, -1);
    profile.prepareReferenceFilesMs = (GetTime() - phaseStart) * 1000.0;
    phaseStart = GetTime();
    RemoveTransientDataShotFolders();
    profile.removeTransientShotsMs = (GetTime() - phaseStart) * 1000.0;
    phaseStart = GetTime();
    if (GetObjectsPath(movablesPath, sizeof(movablesPath)) &&
        snprintf(zipperPath, sizeof(zipperPath), "%s\\Zipper", movablesPath) > 0 &&
        FolderExistsUtf8(zipperPath)) (void)SetActiveZipperPath(zipperPath);
    /* フォルダを取り込んだセルは cells.csv から欠損として復元する。 */
    storageBackend = GetBuildCellStorageBackend();
    (void)RpgBuildCellStorage_ApplyProgressState(stage, &storageBackend);
    profile.applyCellProgressMs = (GetTime() - phaseStart) * 1000.0;
    phaseStart = GetTime();
    /* Builds created before composite ownership stored every component beside
       its representative.  Repair that legacy shape now, before the folder
       availability pass can turn a valid key door into a missing block. */
    if (!CollectEffectComponentFolders(stage, -1)) {
        profile.collectCompositeFoldersMs = (GetTime() - phaseStart) * 1000.0;
        profile.totalMs = (GetTime() - resumeStart) * 1000.0;
        lastResumeProfile = profile;
        hasLastResumeProfile = true;
        WriteResumeProfile(&profile);
        return false;
    }
    profile.collectCompositeFoldersMs = (GetTime() - phaseStart) * 1000.0;
    phaseStart = GetTime();
    /* 特殊ブロックはセルごとのオブジェクトフォルダが正本。格納済みでフォルダが無ければ復活させない。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgObjectFolder folder = { .cell = { row, column } };
        char objectPath[1200];
        int blockType = stage->blocks[row][column];
        const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
        if (RpgBlockInventory_IsReferenceObject(blockType) ||
            RpgBuildCellStorage_UsesMetadataForBlock(blockType) || RpgStage_IsCellError(stage, row, column) ||
            !BlockPath(&folder, blockType, false, objectPath, sizeof(objectPath))) continue;
        /* The root directory owns every component folder.  Do not load or
           validate component cells independently. */
        if (shape != NULL && shape->rootType != blockType) continue;
        if (!FolderExistsUtf8(objectPath)) {
            if (!RpgStage_SetCompositeMissingAtCell(stage, row, column, true))
                SetStageEffectFolderMissing(stage, (RpgGridCell){ row, column }, blockType, true);
        }
    }
    profile.validateSpecialFoldersMs = (GetTime() - phaseStart) * 1000.0;
    profile.totalMs = (GetTime() - resumeStart) * 1000.0;
    profile.succeeded = true;
    lastResumeProfile = profile;
    hasLastResumeProfile = true;
    WriteResumeProfile(&profile);
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

bool RpgObjectFolders_ResumePreparedEditorPreviewBuild(int stageNumber, RpgStage *stage,
                                                        char *buildPath, size_t buildPathSize)
{
#ifdef _WIN32
    char zipperPath[1200], movablesPath[1200];
    RpgBuildCellStorageBackend storageBackend;
    int generated = 0, total = 0;
    bool pending = false;
    if (stageNumber <= 0 || stage == NULL || buildPath == NULL || buildPathSize == 0 ||
        !RpgStageStorage_GetRuntimePath(stageNumber, RPG_STAGE_RUNTIME_EDITOR,
                                        buildPath, (int)buildPathSize) ||
        !FolderExistsUtf8(buildPath)) return false;

    /* This is deliberately a bind-only path.  The editor has already checked
       that this exact preview is clean and complete before calling us. */
    RpgBuildCellStorage_GetGenerationProgress(stage, &generated, &total, &pending);
    if (total <= 0 || generated != total || pending) return false;

    snprintf(activeBuildPath, sizeof(activeBuildPath), "%s", buildPath);
    activeBuildStageNumber = stageNumber;
    activeBuildPersists = false;
    RpgReferenceObjects_EnsureSourcePaths(&stage->referenceObjects);
    RpgStage_NormalizeCompositeObjects(stage);
    SetBuildCellOwners(stage);
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);

    /* A clean Stop has already removed these.  The small cleanup is retained
       for a previously interrupted preview, without revalidating every cell. */
    RemoveTransientDataShotFolders();
    if (GetObjectsPath(movablesPath, sizeof(movablesPath)) &&
        snprintf(zipperPath, sizeof(zipperPath), "%s\\Zipper", movablesPath) > 0 &&
        FolderExistsUtf8(zipperPath)) (void)SetActiveZipperPath(zipperPath);

    /* cells.csv is the one shared compact-cell state file.  Applying it is
       required for current missing/error cells, but is bounded to one file. */
    storageBackend = GetBuildCellStorageBackend();
    (void)RpgBuildCellStorage_ApplyProgressState(stage, &storageBackend);
    return true;
#else
    (void)stageNumber; (void)stage; (void)buildPath; (void)buildPathSize;
    return false;
#endif
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
    if (activeBuildPath[0] == '\0' || stage == NULL) return;
    if (!RpgBuildCellStorage_IsGenerationPending()) return;
    RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
    isBulkBuildOperation = true;
    RpgBuildCellStorage_Update(&storageBackend);
    isBulkBuildOperation = false;
    generationShellNotifyFrameCount++;
    if (!RpgBuildCellStorage_IsGenerationPending() || generationShellNotifyFrameCount >= 15U) {
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
    int removedCompositeIds[RPG_COMPOSITE_OBJECT_MAX_COUNT] = { 0 };
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

/* A deleted area no longer has a current-stage cell array or composite list,
   but its previous coordinate-owned folders still belong to the disposable
   preview.  Use the immutable build snapshot while it is still available.
   This is intentionally limited to the retired slot; it never scans or
   rebuilds unrelated areas. */
static void RemovePreviewRetiredMapArtifacts(int mapIndex)
{
#ifdef _WIN32
    int removedCompositeIds[RPG_COMPOSITE_OBJECT_MAX_COUNT] = { 0 };
    int removedCompositeCount = 0;
    const int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    const int lastColumn = firstColumn + RPG_STAGE_COLUMNS;
    if (mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT || !buildMapActive[mapIndex]) return;

    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = firstColumn;
         column < lastColumn; column++) {
        RpgObjectFolder folder = { .cell = { row, column } };
        char path[1200];
        const int objectId = buildCompositeIds[row][column];
        if (BlockPathDirect(&folder, 0, false, path, sizeof(path))) RemoveTree(path);
        if (objectId <= 0) continue;
        bool alreadyRemoved = false;
        for (int index = 0; index < removedCompositeCount; index++)
            if (removedCompositeIds[index] == objectId) { alreadyRemoved = true; break; }
        if (alreadyRemoved || removedCompositeCount >= RPG_COMPOSITE_OBJECT_MAX_COUNT) continue;
        removedCompositeIds[removedCompositeCount++] = objectId;
        if (CompositePath(objectId, false, path, sizeof(path))) RemoveTree(path);
    }
#else
    (void)mapIndex;
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
            /* Do not infer ownership from the block shape here.  Magnets are
             * variable-size composites and deliberately have no
             * RpgEffectShape, so that old path silently omitted every map
             * beyond the directly edited one.  The stage owns the complete
             * composite map for both fixed shapes and magnets. */
            RpgGridCell componentCells[RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS];
            int componentCount = 0;
            bool touchesDirty = false;
            if (!RpgStage_GetCompositeCellsAtCell(stage, row, column,
                                                   componentCells,
                                                   RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS,
                                                   &componentCount) ||
                componentCount <= 1) continue;
            for (int part = 0; part < componentCount; part++) {
                const int partColumn = componentCells[part].column;
                if (partColumn >= 0 && partColumn < RPG_STAGE_WORLD_COLUMNS &&
                    dirtyMaps[partColumn / RPG_STAGE_COLUMNS]) {
                    touchesDirty = true;
                    break;
                }
            }
            if (!touchesDirty) continue;
            for (int part = 0; part < componentCount; part++) {
                const int partColumn = componentCells[part].column;
                int mapIndex;
                if (partColumn < 0 || partColumn >= RPG_STAGE_WORLD_COLUMNS) continue;
                mapIndex = partColumn / RPG_STAGE_COLUMNS;
                if (!dirtyMaps[mapIndex]) { dirtyMaps[mapIndex] = true; changed = true; }
            }
        }
    } while (changed);
}

static bool HasEditorPreviewDirtyMaps(void)
{
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++)
        if (editorPreviewDirtyMaps[mapIndex]) return true;
    return false;
}

static bool FindNextEditorPreviewRepairMap(const RpgStage *stage, int *cursor)
{
    if (stage == NULL || cursor == NULL) return false;
    while (*cursor < RPG_STAGE_MAP_COUNT) {
        const int mapIndex = (*cursor)++;
        if (editorPreviewRepairJob.maps[mapIndex] && RpgStage_IsMapActive(stage, mapIndex)) {
            *cursor = mapIndex;
            return true;
        }
    }
    return false;
}

/* An explicit Play request may need to use the proven synchronous repair
   path.  Return any partially processed maps to the normal dirty set first;
   the synchronous path will then rebuild a complete, consistent preview. */
static void CancelEditorPreviewRepairJob(void)
{
    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_IDLE) return;
    if (editorPreviewRepairJob.metadataBatchOpen) {
        RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
        (void)RpgBuildCellStorage_EndMetadataBatch(&storageBackend);
    }
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        if (editorPreviewRepairJob.maps[mapIndex]) editorPreviewDirtyMaps[mapIndex] = true;
        if (editorPreviewRepairJob.retiredMaps[mapIndex]) editorPreviewRetiredMaps[mapIndex] = true;
    }
    if (editorPreviewRepairJob.topologyChanged) editorPreviewTopologyMutated = true;
    memset(&editorPreviewRepairJob, 0, sizeof(editorPreviewRepairJob));
    editorPreviewRepairJob.phase = RPG_EDITOR_PREVIEW_REPAIR_IDLE;
    editorPreviewStaticMutated = true;
}

/* A cooperative preview step can fail transiently (for example while Explorer
   still holds a just-renamed object folder).  Leaving its half-advanced job
   alive loses the map list and leaves the editor status at "checking cache"
   forever.  Put every selected map back into the dirty set before reporting
   the failure, so the caller can publish one clean retry. */
static bool FailEditorPreviewRepairJob(void)
{
    CancelEditorPreviewRepairJob();
    return false;
}

bool RpgObjectFolders_UpdateEditorPreviewRepair(const RpgStage *stage,
                                                const RpgAttachments *attachments,
                                                const RpgReceivers *receivers,
                                                const RpgWires *wires,
                                                Vector2 playerStartPosition,
                                                bool *isComplete)
{
#ifdef _WIN32
    RpgBuildCellStorageBackend storageBackend;
    RpgStage runtimeStage;
    bool repaired = true;
    bool wasBulkBuildOperation;
    int mapIndex;
    if (isComplete != NULL) *isComplete = false;
    if (stage == NULL || attachments == NULL || activeBuildPath[0] == '\0' || activeBuildPersists)
        return false;

    /* A clean Stop has only transient runtime files to discard.  This path is
       intentionally kept immediate because it does not recreate map data. */
    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_IDLE &&
        !RpgObjectFolders_HasEditorPreviewStaticMutations()) {
        if (!RpgObjectFolders_RepairEditorPreview(stage, attachments, receivers, wires,
                                                  playerStartPosition)) return FailEditorPreviewRepairJob();
        if (isComplete != NULL) *isComplete = true;
        return true;
    }

    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_IDLE) {
        memcpy(editorPreviewRepairJob.maps, editorPreviewDirtyMaps,
               sizeof(editorPreviewRepairJob.maps));
        memcpy(editorPreviewRepairJob.retiredMaps, editorPreviewRetiredMaps,
               sizeof(editorPreviewRepairJob.retiredMaps));
        editorPreviewRepairJob.topologyChanged = editorPreviewTopologyMutated;
        memset(editorPreviewDirtyMaps, 0, sizeof(editorPreviewDirtyMaps));
        memset(editorPreviewRetiredMaps, 0, sizeof(editorPreviewRetiredMaps));
        editorPreviewTopologyMutated = false;
        ExpandDirtyMapsForCompositeOwners(stage, editorPreviewRepairJob.maps);
        editorPreviewRepairJob.phase = editorPreviewRepairJob.topologyChanged ?
            RPG_EDITOR_PREVIEW_REPAIR_TOPOLOGY_CLEANUP : RPG_EDITOR_PREVIEW_REPAIR_MAPS;
        editorPreviewRepairJob.cursor = 0;
        editorPreviewRepairJob.queuedMapIndex = -1;
        RpgBuildCellStorage_BeginMetadataBatch();
        editorPreviewRepairJob.metadataBatchOpen = true;
    }

    storageBackend = GetBuildCellStorageBackend();
    runtimeStage = *stage;
    wasBulkBuildOperation = isBulkBuildOperation;

    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_TOPOLOGY_CLEANUP) {
        while (editorPreviewRepairJob.cursor < RPG_STAGE_MAP_COUNT) {
            mapIndex = editorPreviewRepairJob.cursor++;
            if (editorPreviewRepairJob.maps[mapIndex] && RpgStage_IsMapActive(stage, mapIndex)) {
                isBulkBuildOperation = true;
                /* The build identity still describes the old grid here. */
                RemovePreviewMapArtifacts(stage, mapIndex);
                isBulkBuildOperation = wasBulkBuildOperation;
                return true;
            }
            if (editorPreviewRepairJob.retiredMaps[mapIndex]) {
                isBulkBuildOperation = true;
                RemovePreviewRetiredMapArtifacts(mapIndex);
                isBulkBuildOperation = wasBulkBuildOperation;
                return true;
            }
        }
        /* All old coordinate folders are gone.  From this point every path
           calculation must use the newly authored area topology. */
        SetBuildCellOwners(stage);
        editorPreviewRepairJob.phase = RPG_EDITOR_PREVIEW_REPAIR_MAPS;
        editorPreviewRepairJob.cursor = 0;
        return true;
    }

    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_MAPS) {
        if (editorPreviewRepairJob.queuedMapIndex >= 0) {
            if (!RpgBuildCellStorage_IsMapGenerated(stage, editorPreviewRepairJob.queuedMapIndex)) {
                /* The cell writer clears its pending state when a filesystem
                   write fails.  Previously this branch interpreted that as
                   "still working" forever, so the editor remained at
                   "checking cache" although no worker existed anymore. */
                if (!RpgBuildCellStorage_IsGenerationPending())
                    return FailEditorPreviewRepairJob();
                return true;
            }
            editorPreviewRepairJob.queuedMapIndex = -1;
            return true;
        }
        if (FindNextEditorPreviewRepairMap(stage, &editorPreviewRepairJob.cursor)) {
            mapIndex = editorPreviewRepairJob.cursor;
            editorPreviewRepairJob.cursor++;
            isBulkBuildOperation = true;
            RemovePreviewMapArtifacts(stage, mapIndex);
            RpgBuildCellStorage_InvalidateMap(mapIndex);
            repaired = RpgBuildCellStorage_QueueMap(stage, mapIndex, &storageBackend);
            isBulkBuildOperation = wasBulkBuildOperation;
            if (repaired) {
                editorPreviewRepairJob.queuedMapIndex = mapIndex;
                return true;
            }
            return FailEditorPreviewRepairJob();
        }
        if (editorPreviewRepairJob.metadataBatchOpen) {
            repaired = RpgBuildCellStorage_EndMetadataBatch(&storageBackend);
            editorPreviewRepairJob.metadataBatchOpen = false;
        }
        if (!repaired) {
            isBulkBuildOperation = wasBulkBuildOperation;
            return FailEditorPreviewRepairJob();
        }
        editorPreviewRepairJob.phase = RPG_EDITOR_PREVIEW_REPAIR_COMPOSITES;
        return true;
    }

    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_COMPOSITES) {
        isBulkBuildOperation = true;
        repaired = CollectEffectComponentFoldersForMaps(stage, editorPreviewRepairJob.maps);
        isBulkBuildOperation = wasBulkBuildOperation;
        if (!repaired) return FailEditorPreviewRepairJob();
        editorPreviewRepairJob.phase = RPG_EDITOR_PREVIEW_REPAIR_REFERENCES;
        editorPreviewRepairJob.cursor = 0;
        return true;
    }

    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_REFERENCES) {
        if (FindNextEditorPreviewRepairMap(stage, &editorPreviewRepairJob.cursor)) {
            mapIndex = editorPreviewRepairJob.cursor;
            editorPreviewRepairJob.cursor++;
            isBulkBuildOperation = true;
            PrepareRuntimeReferenceFiles(&runtimeStage, mapIndex);
            repaired = PrepareRuntimeReferenceFolders(&runtimeStage, mapIndex);
            if (repaired) {
                PrepareBlockOwnedMetadataForMap(attachments, receivers, wires, mapIndex);
                PrepareReferenceFolderMetadataForMap(&runtimeStage, mapIndex);
            }
            isBulkBuildOperation = wasBulkBuildOperation;
            if (repaired) return true;
            return FailEditorPreviewRepairJob();
        }
        editorPreviewRepairJob.phase = RPG_EDITOR_PREVIEW_REPAIR_FINALIZE;
        return true;
    }

    /* Finalization is one small operation after all affected map folders are
       valid again.  Edits received during the job remain in
       editorPreviewDirtyMaps and start a fresh job on a later frame. */
    if (editorPreviewRepairJob.phase == RPG_EDITOR_PREVIEW_REPAIR_FINALIZE) {
        char zipperDirectory[1200];
        isBulkBuildOperation = true;
        if (GetInboxPath(zipperDirectory, sizeof(zipperDirectory))) ClearFolderContents(zipperDirectory);
        RemoveTransientDataShotFolders();
        repaired = EnsurePlayerFolder(playerStartPosition);
        if (repaired) RpgObjectFolder_PrepareZipperAnimationCommand();
        isBulkBuildOperation = wasBulkBuildOperation;
        if (!repaired) return FailEditorPreviewRepairJob();
        memset(&editorPreviewRepairJob, 0, sizeof(editorPreviewRepairJob));
        editorPreviewRepairJob.phase = RPG_EDITOR_PREVIEW_REPAIR_IDLE;
        editorPreviewRuntimeTrackingActive = false;
        if (HasEditorPreviewDirtyMaps()) {
            editorPreviewStaticMutated = true;
            return true;
        }
        editorPreviewStaticMutated = false;
        NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
        NotifyShellParentChanged(activeBuildPath);
        if (isComplete != NULL) *isComplete = true;
        return true;
    }
    return false;
#else
    (void)stage; (void)attachments; (void)receivers; (void)wires;
    (void)playerStartPosition;
    if (isComplete != NULL) *isComplete = false;
    return false;
#endif
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
    /* A user explicitly starting Play is allowed to finish synchronously,
       but it must never race a cooperative editor-frame repair. */
    CancelEditorPreviewRepairJob();
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
    /* EndStageBuild invalidates activeBuildPath below.  A deferred preview
       queue may never outlive that path, otherwise the UI can retain one
       unwriteable cell as its generation target indefinitely. */
    RpgBuildCellStorage_CancelGeneration();
    StopDataShotFolderWorker();
    if (!activeBuildPersists) DiscardEditorPreviewBaseline();
    editorPreviewRuntimeTrackingActive = false;
    if (!activeBuildPersists) RemovePlayerFolder();
    else RemoveTransientDataShotFolders();
    activeBuildPath[0] = '\0';
    ResetBuildCellOwners();
    ClearShooterPayloadWatches();
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
    /* The incremental cell worker owns only this disposable build path.
       Detach its queue before clearing the path so it cannot keep reporting
       a target that has no valid destination. */
    RpgBuildCellStorage_CancelGeneration();
    StopDataShotFolderWorker();
    activeBuildPath[0] = '\0';
    DiscardEditorPreviewBaseline();
    editorPreviewRuntimeTrackingActive = false;
    ResetBuildCellOwners();
    ClearShooterPayloadWatches();
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
    StopDataShotFolderWorker();
    memset(dataShotFolderSerials, 0, sizeof(dataShotFolderSerials)); nextFileDropId = 1;
    hasDispatchedZipperRequest = false;
    hasPendingZipperRequest = false;
    pendingZipperCommand = RPG_ZIPPER_COMMAND_NONE;
    dispatchedZipperRequestToken[0] = '\0';
    pendingZipperRequestToken[0] = '\0';
    RpgObjectFolderMonitor_DisconnectAll();
    ClearShooterPayloadWatches();
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    if (activeBuildPath[0] == '\0' && GetObjectsPath(objects, sizeof(objects))) RemoveTree(objects);
    if (activeBuildPath[0] == '\0' && GetDropsPath(drops, sizeof(drops))) RemoveTree(drops);
    /* Inboxは実Explorerの表示ルートなので、削除・再作成せず同じディレクトリ内を掃除する。 */
    /* Zipper is now the visible storage root.  It contains eat.vbs and any
       captured objects, so cleanup must not empty it as though it were a
       disposable Inbox. */
#endif
}
// 役割: オブジェクト所有フォルダ、Zipper への移動、外部ファイルの引き継ぎを管理する。
