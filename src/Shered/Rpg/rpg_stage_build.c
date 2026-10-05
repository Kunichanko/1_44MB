// 役割: ステージ build のフォルダ変更を全件走査せず、Win32 の ReadDirectoryChangesW で反映する。
// 依存する自プロジェクト内ファイル: rpg_stage_build.h, rpg_object_folder.h, rpg_block_inventory.h
#include "rpg_stage_build.h"

#include "rpg_block_inventory.h"
#include "rpg_build_cell_storage.h"
#include "rpg_object_folder.h"
#include "rpg_stage_storage.h"
#include "rpg_stage_authority.h"

// 依存関係: 通常マスがメタデータ方式かどうかは保存方式モジュールから取得する。

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>

enum { RPG_STAGE_BUILD_WATCH_BUFFER_SIZE = 65536 };

typedef struct RpgStageBuildWatcher {
    HANDLE directory;
    HANDLE event;
    OVERLAPPED overlapped;
    unsigned char buffer[RPG_STAGE_BUILD_WATCH_BUFFER_SIZE];
    int originalBlocks[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    bool compactCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    bool missingCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    /* ReadDirectoryChangesWで受けたFolder→Zipper要求。描画とは分離して一度だけ消費する。 */
    bool hasReferenceFolderZipperRequest;
    RpgGridCell referenceFolderZipperCell;
    char buildPath[1200];
    bool isWatching;
} RpgStageBuildWatcher;

static RpgStageBuildWatcher watcher = { 0 };
static double nextReferenceRequestPollTime = 0.0;

static bool ToWide(const char *source, wchar_t *destination, int destinationCount)
{
    return source != NULL && MultiByteToWideChar(CP_UTF8, 0, source, -1, destination, destinationCount) > 0;
}

static void CloseWatcher(void)
{
    if (watcher.directory != NULL && watcher.directory != INVALID_HANDLE_VALUE) {
        CancelIo(watcher.directory);
        CloseHandle(watcher.directory);
    }
    if (watcher.event != NULL) CloseHandle(watcher.event);
    watcher.directory = NULL;
    watcher.event = NULL;
    watcher.isWatching = false;
}

static bool QueueDirectoryRead(void)
{
    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                   FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    if (watcher.directory == NULL || watcher.directory == INVALID_HANDLE_VALUE || watcher.event == NULL) return false;
    ZeroMemory(&watcher.overlapped, sizeof(watcher.overlapped));
    watcher.overlapped.hEvent = watcher.event;
    ResetEvent(watcher.event);
    watcher.isWatching = ReadDirectoryChangesW(watcher.directory, watcher.buffer, sizeof(watcher.buffer), TRUE,
                                               filter, NULL, &watcher.overlapped, NULL) != 0;
    return watcher.isWatching;
}

static bool StartWatcher(const char *buildPath)
{
    wchar_t wideBuildPath[1200];
    if (!ToWide(buildPath, wideBuildPath, 1200)) return false;
    snprintf(watcher.buildPath, sizeof(watcher.buildPath), "%s", buildPath);
    watcher.directory = CreateFileW(wideBuildPath, FILE_LIST_DIRECTORY,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (watcher.directory == INVALID_HANDLE_VALUE) { watcher.directory = NULL; return false; }
    watcher.event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (watcher.event == NULL) { CloseWatcher(); return false; }
    return QueueDirectoryRead();
}

static bool ParseCellPath(const RpgStage *stage, const wchar_t *relativePath, int *row, int *column)
{
    const wchar_t *name;
    int gridX, gridY, localX, localY, mapIndex;
    if (relativePath == NULL || wcsncmp(relativePath, L"cells\\", 6) != 0) return false;
    name = relativePath + 6;
    if (stage == NULL || swscanf(name, L"area_x%d_y%d_cell_x%d_y%d", &gridX, &gridY,
                                 &localX, &localY) != 4 ||
        localX < 0 || localX >= RPG_STAGE_COLUMNS || localY < 0 || localY >= RPG_STAGE_ROWS ||
        (mapIndex = RpgStage_GetMapAtGrid(stage, gridX, gridY)) < 0) return false;
    *row = localY;
    *column = mapIndex * RPG_STAGE_COLUMNS + localX;
    return true;
}

static bool ParseCellFolder(const RpgStage *stage, const wchar_t *relativePath, int *row, int *column)
{
    const wchar_t *name = relativePath == NULL ? NULL : relativePath + 6;
    return ParseCellPath(stage, relativePath, row, column) && wcschr(name, L'\\') == NULL;
}

static void RefreshCompactCells(RpgStage *stage)
{
    bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    bool couldRead = RpgObjectFolders_ReadCompactCellAvailability(available);
    if (stage == NULL) return;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!watcher.compactCells[row][column]) continue;
        if (!couldRead || !available[row][column]) {
            watcher.missingCells[row][column] = true;
            stage->blocks[row][column] = RPG_BLOCK_BUILD_MISSING;
        } else if (watcher.missingCells[row][column]) {
            watcher.missingCells[row][column] = false;
            stage->blocks[row][column] = watcher.originalBlocks[row][column];
        }
    }
}

static void SetWatchedEffectMissing(RpgStage *stage, int rootRow, int rootColumn, bool missing)
{
    const RpgEffectShape *shape;
    int rootType;
    if (stage == NULL || rootRow < 0 || rootRow >= RPG_STAGE_ROWS || rootColumn < 0 ||
        rootColumn >= RPG_STAGE_WORLD_COLUMNS) return;
    rootType = watcher.originalBlocks[rootRow][rootColumn];
    shape = RpgBlockInventory_GetEffectShape(rootType);
    if (shape == NULL || shape->rootType != rootType) {
        watcher.missingCells[rootRow][rootColumn] = missing;
        stage->blocks[rootRow][rootColumn] = missing ? RPG_BLOCK_BUILD_MISSING : rootType;
        return;
    }
    for (int index = 0; index < shape->cellCount; index++) {
        const RpgEffectShapeCell *part = &shape->cells[index];
        int row = rootRow + part->offsetY;
        int column = rootColumn + part->offsetX;
        if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) continue;
        watcher.missingCells[row][column] = missing;
        stage->blocks[row][column] = missing ? RPG_BLOCK_BUILD_MISSING : part->blockType;
    }
}

static void ApplyCellChange(RpgStage *stage, DWORD action, const wchar_t *relativePath)
{
    int row, column;
    RpgGridCell cell;
    if (relativePath != NULL && wcscmp(relativePath, L"cells.csv") == 0 &&
        RpgBuildCellStorage_IsMetadataFile("cells.csv")) {
        RefreshCompactCells(stage);
        return;
    }
    if (stage == NULL || !ParseCellFolder(stage, relativePath, &row, &column)) return;
    cell = (RpgGridCell){ row, column };
    /* 外部File/Folder は1マス表示でも地形を占有しないため、専用フォルダの移動で赤壁へ変換しない。 */
    if (RpgBlockInventory_IsReferenceObject(watcher.originalBlocks[row][column])) return;
    if (action == FILE_ACTION_REMOVED || action == FILE_ACTION_RENAMED_OLD_NAME) {
        /* Zipper による移動中は Inbox 側に同じマスが存在するため、赤壁へは変換しない。 */
        if (!RpgObjectFolders_IsBuildCellAvailable(cell)) {
            SetWatchedEffectMissing(stage, row, column, true);
        }
    } else if (action == FILE_ACTION_ADDED || action == FILE_ACTION_RENAMED_NEW_NAME) {
        /* Compactマスも、返却された実フォルダを検知した時点で元のマスとして復帰する。 */
        if (watcher.missingCells[row][column] && RpgObjectFolders_IsBuildCellAvailable(cell)) {
            SetWatchedEffectMissing(stage, row, column, false);
        }
    }
}

/* Resolve the actual folder identity from its own metadata.  Folder names are
   user-editable and therefore must never be used as an area/object identity. */
static void QueueReferenceFolderZipperRequest(const RpgStage *stage, DWORD action,
                                              const wchar_t *relativePath)
{
    char relativeUtf8[1024];
    char *separator;
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *info;
    float x = -1.0f;
    float y = -1.0f;
    char line[256];
    if (stage == NULL || relativePath == NULL || action == FILE_ACTION_REMOVED ||
        WideCharToMultiByte(CP_UTF8, 0, relativePath, -1, relativeUtf8,
                            (int)sizeof(relativeUtf8), NULL, NULL) <= 0) return;
    separator = strrchr(relativeUtf8, '\\');
    if (separator == NULL || _stricmp(separator + 1, "zipper.request") != 0) return;
    *separator = '\0';
    if (snprintf(infoPath, sizeof(infoPath), "%s\\%s\\object_info.txt", watcher.buildPath,
                 relativeUtf8) <= 0 ||
        !ToWide(infoPath, wideInfoPath, (int)(sizeof(wideInfoPath) / sizeof(wideInfoPath[0]))) ||
        (info = _wfopen(wideInfoPath, L"rb")) == NULL) return;
    while (fgets(line, sizeof(line), info) != NULL) {
        (void)sscanf(line, "x=%f", &x);
        (void)sscanf(line, "y=%f", &y);
    }
    fclose(info);
    const int row = (int)(y / RPG_STAGE_TILE_SIZE);
    const int column = (int)(x / RPG_STAGE_TILE_SIZE);
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column])) return;
    watcher.referenceFolderZipperCell = (RpgGridCell){ row, column };
    watcher.hasReferenceFolderZipperRequest = true;
}

/* ReadDirectoryChangesW の通知が Shell / cmd の連続操作でまとまった場合にも、残った要求ファイルを拾う。 */
static void PollReferenceFolderZipperRequest(const RpgStage *stage)
{
    double now;
    if (stage == NULL || watcher.hasReferenceFolderZipperRequest) return;
    now = GetTime();
    if (now < nextReferenceRequestPollTime) return;
    nextReferenceRequestPollTime = now + 0.10;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        char requestPath[1200];
        wchar_t wideRequestPath[1200];
        if (!RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column]) ||
            snprintf(requestPath, sizeof(requestPath), "%s\\zipper.request",
                     RpgStage_GetReferencePathAtCell(stage, row, column)) <= 0 ||
            !ToWide(requestPath, wideRequestPath, (int)(sizeof(wideRequestPath) / sizeof(wideRequestPath[0]))) ||
            GetFileAttributesW(wideRequestPath) == INVALID_FILE_ATTRIBUTES) continue;
        watcher.referenceFolderZipperCell = (RpgGridCell){ row, column };
        watcher.hasReferenceFolderZipperRequest = true;
        return;
    }
}

static void ProcessChanges(RpgStage *stage, DWORD byteCount)
{
    FILE_NOTIFY_INFORMATION *entry = (FILE_NOTIFY_INFORMATION *)(void *)watcher.buffer;
    DWORD offset = 0;
    while (offset < byteCount) {
        wchar_t relativePath[1024];
        size_t characterCount = entry->FileNameLength / sizeof(wchar_t);
        if (characterCount >= sizeof(relativePath) / sizeof(relativePath[0])) characterCount = sizeof(relativePath) / sizeof(relativePath[0]) - 1;
        memcpy(relativePath, entry->FileName, characterCount * sizeof(wchar_t));
        relativePath[characterCount] = L'\0';
        /* build ルートの通知を入口にし、変化したマスだけ外部ファイル状態を再評価する。 */
        int row, column;
        if (ParseCellPath(stage, relativePath, &row, &column)) {
            RpgObjectFolders_RefreshBuildCellLinkedFiles((RpgGridCell){ row, column });
            /* A filesystem-side cell change cannot be attributed safely to a
               single runtime helper, so let editor Stop perform its guarded
               static repair rather than trusting the untouched-cache fast path. */
            RpgObjectFolders_MarkEditorPreviewCellMutation((RpgGridCell){ row, column },
                                                            stage->blocks[row][column]);
        }
        QueueReferenceFolderZipperRequest(stage, entry->Action, relativePath);
        ApplyCellChange(stage, entry->Action, relativePath);
        if (entry->NextEntryOffset == 0) break;
        offset += entry->NextEntryOffset;
        entry = (FILE_NOTIFY_INFORMATION *)(void *)(watcher.buffer + offset);
    }
}
#endif

static bool CreateStageBuild(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                             const RpgReceivers *receivers, const RpgWires *wires,
                             Vector2 playerStartPosition, bool isSimpleBuild)
{
#ifdef _WIN32
    char buildPath[1200];
    RpgStageBuild_Close();
    /* 参照Fileの復元失敗は、そのFileだけの問題として扱い、Play全体は止めない。 */
    (void)RpgStageStorage_RepairReferenceFileCopies(stageNumber, stage);
    if (!RpgObjectFolders_BeginStageBuild(stageNumber, stage, attachments, receivers, wires, playerStartPosition,
                                          isSimpleBuild, buildPath, sizeof(buildPath))) return false;
    memcpy(watcher.originalBlocks, stage->blocks, sizeof(watcher.originalBlocks));
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        watcher.compactCells[row][column] = RpgBuildCellStorage_UsesMetadataForBlock(stage->blocks[row][column]);
    memset(watcher.missingCells, 0, sizeof(watcher.missingCells));
    /* 変更監視は外部編集の反映用。開始に失敗しても、生成済みステージのプレイは継続できる。 */
    (void)StartWatcher(buildPath);
    /* 本編は static を展開後に削除し、実行時は build/Stage/StageN/game だけを使う。 */
    if (!isSimpleBuild) RpgStageStorage_ClearPackagedStaticStage(stageNumber);
    return true;
#else
    (void)stageNumber; (void)stage; (void)attachments; (void)receivers; (void)wires;
    (void)playerStartPosition; (void)isSimpleBuild;
    return false;
#endif
}

bool RpgStageBuild_Create(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                          const RpgReceivers *receivers, const RpgWires *wires,
                          Vector2 playerStartPosition)
{
    if (!RpgStageAuthority_CanWriteGameRuntime()) return false;
    return CreateStageBuild(stageNumber, stage, attachments, receivers, wires, playerStartPosition, false);
}

bool RpgStageBuild_CreateEditorPreview(int stageNumber, RpgStage *stage,
                                       const RpgAttachments *attachments,
                                       const RpgReceivers *receivers, const RpgWires *wires,
                                       Vector2 playerStartPosition)
{
    if (!RpgStageAuthority_CanWriteEditorRuntime()) return false;
    return CreateStageBuild(stageNumber, stage, attachments, receivers, wires, playerStartPosition, true);
}

static bool ResumeStageBuildForKind(int stageNumber, RpgStage *stage, bool isEditorPreview)
{
#ifdef _WIN32
    char buildPath[1200];
    if (stage == NULL) return false;
    RpgStageBuild_Close();
    /* originalBlocks は静的定義なので、build の欠損状態を反映する前に控える。 */
    memcpy(watcher.originalBlocks, stage->blocks, sizeof(watcher.originalBlocks));
    if (!(isEditorPreview ? RpgObjectFolders_ResumeEditorPreviewBuild(stageNumber, stage, buildPath,
                                                                        sizeof(buildPath)) :
          RpgObjectFolders_ResumeStageBuild(stageNumber, stage, buildPath, sizeof(buildPath)))) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        watcher.compactCells[row][column] = RpgBuildCellStorage_UsesMetadataForBlock(watcher.originalBlocks[row][column]);
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        watcher.missingCells[row][column] = stage->blocks[row][column] == RPG_BLOCK_BUILD_MISSING;
    (void)StartWatcher(buildPath);
    return true;
#else
    (void)stageNumber;
    (void)stage;
    return false;
#endif
}

bool RpgStageBuild_Resume(int stageNumber, RpgStage *stage)
{
    if (!RpgStageAuthority_CanWriteGameRuntime()) return false;
    return ResumeStageBuildForKind(stageNumber, stage, false);
}

bool RpgStageBuild_ResumeEditorPreview(int stageNumber, RpgStage *stage)
{
    if (!RpgStageAuthority_CanWriteEditorRuntime()) return false;
    return ResumeStageBuildForKind(stageNumber, stage, true);
}

bool RpgStageBuild_EnsureAllAreasGenerated(RpgStage *stage)
{
    int generated = 0, total = 0;
    bool pending = false;
    if (!RpgStageAuthority_CanWriteEditorRuntime() && !RpgStageAuthority_CanWriteGameRuntime()) return false;
    if (stage == NULL) return false;
    RpgBuildCellStorage_GetGenerationProgress(stage, &generated, &total, &pending);
    if (total > 0 && generated == total && !pending) return true;
    if (!RpgObjectFolders_EnsureAllMapsGenerated(stage)) return false;
    RpgBuildCellStorage_GetGenerationProgress(stage, &generated, &total, &pending);
    return total > 0 && generated == total && !pending;
}

bool RpgStageBuild_RefreshEditorPreviewCompactCells(const RpgStage *stage)
{
    if (!RpgStageAuthority_CanWriteEditorRuntime()) return false;
    return RpgObjectFolders_RefreshEditorPreviewCompactCells(stage);
}

bool RpgStageBuild_RepairEditorPreview(const RpgStage *stage,
                                       const RpgAttachments *attachments,
                                       const RpgReceivers *receivers,
                                       const RpgWires *wires,
                                       Vector2 playerStartPosition)
{
    if (!RpgStageAuthority_CanWriteEditorRuntime()) return false;
    return RpgObjectFolders_RepairEditorPreview(stage, attachments, receivers, wires, playerStartPosition);
}

void RpgStageBuild_Update(RpgStage *stage)
{
#ifdef _WIN32
    DWORD byteCount = 0;
    if (!RpgStageAuthority_CanWriteEditorRuntime() && !RpgStageAuthority_CanWriteGameRuntime()) return;
    RpgObjectFolders_UpdateBuildCellGeneration(stage);
    if (!watcher.isWatching || watcher.event == NULL || WaitForSingleObject(watcher.event, 0) != WAIT_OBJECT_0) return;
    if (GetOverlappedResult(watcher.directory, &watcher.overlapped, &byteCount, FALSE) != 0 && byteCount > 0)
        ProcessChanges(stage, byteCount);
    QueueDirectoryRead();
#else
    (void)stage;
#endif
}

bool RpgStageBuild_ConsumeReferenceFolderZipperRequest(const RpgStage *stage, Vector2 playerPosition,
                                                        RpgGridCell *folderCell)
{
#ifdef _WIN32
    RpgGridCell requestedCell;
    char requestPath[1200];
    wchar_t wideRequestPath[1200];
    int playerMapIndex;
    int requestMapIndex;
    PollReferenceFolderZipperRequest(stage);
    if (!watcher.hasReferenceFolderZipperRequest || stage == NULL) return false;
    requestedCell = watcher.referenceFolderZipperCell;
    if (requestedCell.row < 0 || requestedCell.row >= RPG_STAGE_ROWS || requestedCell.column < 0 ||
        requestedCell.column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsReferenceFolder(stage->blocks[requestedCell.row][requestedCell.column])) return false;
    playerMapIndex = RpgStage_GetMapAtWorldPosition(stage, playerPosition);
    requestMapIndex = requestedCell.column / RPG_STAGE_COLUMNS;
    /* Do not consume a request from another area.  It must remain pending until
       the player actually enters that same connected-world area. */
    if (playerMapIndex < 0 || playerMapIndex != requestMapIndex) return false;
    watcher.hasReferenceFolderZipperRequest = false;
    /* The request is now consumed by its owning area. */
    if (snprintf(requestPath, sizeof(requestPath), "%s\\zipper.request",
                 RpgStage_GetReferencePathAtCell(stage, requestedCell.row, requestedCell.column)) > 0 &&
        ToWide(requestPath, wideRequestPath, (int)(sizeof(wideRequestPath) / sizeof(wideRequestPath[0]))))
        (void)DeleteFileW(wideRequestPath);
    if (folderCell != NULL) *folderCell = requestedCell;
    return true;
#else
    (void)stage; (void)playerPosition; (void)folderCell;
    return false;
#endif
}

void RpgStageBuild_Close(void)
{
#ifdef _WIN32
    CloseWatcher();
    memset(&watcher, 0, sizeof(watcher));
    nextReferenceRequestPollTime = 0.0;
#endif
}
