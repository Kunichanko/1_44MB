// 役割: ステージ build のフォルダ変更を全件走査せず、Win32 の ReadDirectoryChangesW で反映する。
// 依存する自プロジェクト内ファイル: rpg_stage_build.h, rpg_object_folder.h, rpg_block_inventory.h
#include "rpg_stage_build.h"

#include "rpg_block_inventory.h"
#include "rpg_build_cell_storage.h"
#include "rpg_object_folder.h"

// 依存関係: 通常マスがメタデータ方式かどうかは保存方式モジュールから取得する。

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
    bool isWatching;
} RpgStageBuildWatcher;

static RpgStageBuildWatcher watcher = { 0 };

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
    watcher.directory = CreateFileW(wideBuildPath, FILE_LIST_DIRECTORY,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (watcher.directory == INVALID_HANDLE_VALUE) { watcher.directory = NULL; return false; }
    watcher.event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (watcher.event == NULL) { CloseWatcher(); return false; }
    return QueueDirectoryRead();
}

static bool ParseCellPath(const wchar_t *relativePath, int *row, int *column)
{
    const wchar_t *name;
    int identity;
    if (relativePath == NULL || wcsncmp(relativePath, L"cells\\", 6) != 0) return false;
    name = relativePath + 6;
    if (swscanf(name, L"cell_block_%d_r%d_c%d", &identity, row, column) != 3 ||
        *row < 0 || *row >= RPG_STAGE_ROWS || *column < 0 || *column >= RPG_STAGE_WORLD_COLUMNS) return false;
    return identity == *row * RPG_STAGE_WORLD_COLUMNS + *column + 1;
}

static bool ParseCellFolder(const wchar_t *relativePath, int *row, int *column)
{
    const wchar_t *name = relativePath == NULL ? NULL : relativePath + 6;
    return ParseCellPath(relativePath, row, column) && wcschr(name, L'\\') == NULL;
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

static void ApplyCellChange(RpgStage *stage, DWORD action, const wchar_t *relativePath)
{
    int row, column;
    RpgGridCell cell;
    if (relativePath != NULL && wcscmp(relativePath, L"cells_metadata.txt") == 0 &&
        RpgBuildCellStorage_IsMetadataFile("cells_metadata.txt")) {
        RefreshCompactCells(stage);
        return;
    }
    if (stage == NULL || !ParseCellFolder(relativePath, &row, &column)) return;
    cell = (RpgGridCell){ row, column };
    if (action == FILE_ACTION_REMOVED || action == FILE_ACTION_RENAMED_OLD_NAME) {
        /* Zipper による移動中は Inbox 側に同じマスが存在するため、赤壁へは変換しない。 */
        if (!RpgObjectFolders_IsBuildCellAvailable(cell)) {
            watcher.missingCells[row][column] = true;
            stage->blocks[row][column] = RPG_BLOCK_BUILD_MISSING;
        }
    } else if (action == FILE_ACTION_ADDED || action == FILE_ACTION_RENAMED_NEW_NAME) {
        /* Compactマスも、返却された実フォルダを検知した時点で元のマスとして復帰する。 */
        if (watcher.missingCells[row][column] && RpgObjectFolders_IsBuildCellAvailable(cell)) {
            watcher.missingCells[row][column] = false;
            stage->blocks[row][column] = watcher.originalBlocks[row][column];
        }
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
        if (ParseCellPath(relativePath, &row, &column))
            RpgObjectFolders_RefreshBuildCellLinkedFiles((RpgGridCell){ row, column });
        ApplyCellChange(stage, entry->Action, relativePath);
        if (entry->NextEntryOffset == 0) break;
        offset += entry->NextEntryOffset;
        entry = (FILE_NOTIFY_INFORMATION *)(void *)(watcher.buffer + offset);
    }
}
#endif

static bool CreateStageBuild(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                             Vector2 playerStartPosition, bool isSimpleBuild)
{
#ifdef _WIN32
    char buildPath[1200];
    RpgStageBuild_Close();
    if (!RpgObjectFolders_BeginStageBuild(stageNumber, stage, attachments, playerStartPosition,
                                          isSimpleBuild, buildPath, sizeof(buildPath))) return false;
    memcpy(watcher.originalBlocks, stage->blocks, sizeof(watcher.originalBlocks));
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        watcher.compactCells[row][column] = RpgBuildCellStorage_UsesMetadataForBlock(stage->blocks[row][column]);
    memset(watcher.missingCells, 0, sizeof(watcher.missingCells));
    return StartWatcher(buildPath);
#else
    (void)stageNumber; (void)stage; (void)attachments; (void)playerStartPosition; (void)isSimpleBuild;
    return false;
#endif
}

bool RpgStageBuild_Create(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                          Vector2 playerStartPosition)
{
    return CreateStageBuild(stageNumber, stage, attachments, playerStartPosition, false);
}

bool RpgStageBuild_CreateEditorPreview(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                                       Vector2 playerStartPosition)
{
    return CreateStageBuild(stageNumber, stage, attachments, playerStartPosition, true);
}

void RpgStageBuild_Update(RpgStage *stage)
{
#ifdef _WIN32
    DWORD byteCount = 0;
    RpgObjectFolders_UpdateBuildCellGeneration();
    if (!watcher.isWatching || watcher.event == NULL || WaitForSingleObject(watcher.event, 0) != WAIT_OBJECT_0) return;
    if (GetOverlappedResult(watcher.directory, &watcher.overlapped, &byteCount, FALSE) != 0 && byteCount > 0)
        ProcessChanges(stage, byteCount);
    QueueDirectoryRead();
#else
    (void)stage;
#endif
}

void RpgStageBuild_Close(void)
{
#ifdef _WIN32
    CloseWatcher();
    memset(&watcher, 0, sizeof(watcher));
#endif
}
