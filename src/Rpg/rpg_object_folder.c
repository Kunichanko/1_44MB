// 依存する自プロジェクト内ファイル: rpg_object_folder.h
#include "rpg_object_folder.h"
#include "rpg_build_cell_storage.h"
#include "rpg_explorer_launcher.h"

// 依存関係: build の通常マス生成は rpg_build_cell_storage の選択方式へ委譲する。

#include <stdio.h>
#include <string.h>

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
/* 本編の「ビルドする」で選択した StageN/build。エディターは空のまま従来の一時領域を使う。 */
static char activeBuildPath[1200] = { 0 };
/* 全マス生成中は数千件の Shell 通知をまとめ、ビルド処理が Explorer 更新で詰まらないようにする。 */
static bool isBulkBuildOperation = false;
/* 描画中はファイルシステムへ触れず、build の変更通知を受けたマスだけ更新する。 */
static bool buildCellLinkedFiles[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
// データ弾ごとに配下を含む変更通知を持ち、毎フレームの再帰走査を避ける。
static HANDLE dataShotFolderWatchers[RPG_DATA_SHOT_MAX_COUNT] = { NULL };
static char dataShotFolderWatchPaths[RPG_DATA_SHOT_MAX_COUNT][1200] = { { 0 } };

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
    char parent[1200];
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

static bool FolderExistsUtf8(const char *path)
{
    wchar_t wide[1200];
    DWORD attributes;
    if (!ToWide(path, wide, 1200)) return false;
    attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// データ弾フォルダの変更を軽量に検出し、変更時だけ内容を再集計する。
static bool GetStagePath(const char *suffix, char *path, size_t size)
{ return snprintf(path, size, "%s../assets/Settings/Stage/%s", GetApplicationDirectory(), suffix) > 0; }
static bool GetObjectsPath(char *path, size_t size)
{
    if (activeBuildPath[0] != '\0') return snprintf(path, size, "%s\\objects", activeBuildPath) > 0;
    return GetStagePath("Objects", path, size);
}
static bool GetCellsPath(char *path, size_t size)
{
    if (activeBuildPath[0] != '\0') return snprintf(path, size, "%s\\cells", activeBuildPath) > 0;
    return GetObjectsPath(path, size);
}
static bool RPG_OBJECT_FOLDER_UNUSED GetCompactCellMetadataPath(char *path, size_t size)
{
    return activeBuildPath[0] != '\0' && snprintf(path, size, "%s\\cells_metadata.txt", activeBuildPath) > 0;
}
static bool GetDropsPath(char *path, size_t size)
{
    if (activeBuildPath[0] != '\0') return snprintf(path, size, "%s\\drops", activeBuildPath) > 0;
    return GetStagePath("Drops", path, size);
}
static bool GetInboxPath(char *path, size_t size)
{ return snprintf(path, size, "%s../assets/Settings/Zipper/Inbox", GetApplicationDirectory()) > 0; }

static bool RpgObjectFolders_UsesCompactCellMetadata(int blockType)
{
    /* 地形の基本色と空気だけを一つの一覧へ圧縮し、穴・効果・参照物などは個別フォルダに残す。 */
    return RpgBuildCellStorage_UsesMetadataForBlock(blockType);
}

static bool AttachmentName(const RpgAttachment *attachment, char *name, size_t size)
{
    return attachment != NULL && attachment->folderId > 0 &&
           snprintf(name, size, "attachment_%03d_%06d_r%02d_c%03d_s%d", attachment->type, attachment->folderId,
                    attachment->cell.row, attachment->cell.column, attachment->side) > 0;
}

static bool DataShotName(const RpgDataShot *shot, char *name, size_t size)
{
    return shot != NULL && shot->folderSerial > 0 &&
           snprintf(name, size, "data_shot_%03d_%06d_a%06d", 0, shot->folderSerial, shot->attachmentIndex + 1) > 0;
}

static bool BlockName(const RpgObjectFolder *folder, int blockType, char *name, size_t size)
{
    int identity;
    if (folder == NULL || folder->cell.row < 0 || folder->cell.row >= RPG_STAGE_ROWS ||
        folder->cell.column < 0 || folder->cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    identity = folder->cell.row * RPG_STAGE_WORLD_COLUMNS + folder->cell.column + 1;
    (void)blockType;
    /* 種類を名前へ埋め込まず、マスごとに同じフォルダ名を保つ。種類は object_info.txt に記録する。 */
    return snprintf(name, size, "cell_block_%06d_r%02d_c%03d", identity,
                    folder->cell.row, folder->cell.column) > 0;
}

static bool ObjectPath(const char *name, bool inInbox, char *path, size_t size)
{
    char parent[1200];
    if (name == NULL || !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetObjectsPath(parent, sizeof(parent)))) return false;
    return snprintf(path, size, "%s\\%s", parent, name) > 0;
}

static bool AttachmentPath(const RpgAttachment *attachment, bool inInbox, char *path, size_t size)
{
    char name[256];
    return AttachmentName(attachment, name, sizeof(name)) && ObjectPath(name, inInbox, path, size);
}

static bool DataShotPath(const RpgDataShot *shot, bool inInbox, char *path, size_t size)
{
    char name[256];
    return DataShotName(shot, name, sizeof(name)) && ObjectPath(name, inInbox, path, size);
}

static void CloseDataShotFolderWatcher(int index)
{
    if (index < 0 || index >= RPG_DATA_SHOT_MAX_COUNT) return;
    if (dataShotFolderWatchers[index] != NULL && dataShotFolderWatchers[index] != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(dataShotFolderWatchers[index]);
    dataShotFolderWatchers[index] = NULL;
    dataShotFolderWatchPaths[index][0] = '\0';
}

// サブディレクトリを含む変更通知。通知を受けたデータ弾だけが再帰集計を実行する。
static bool HasDataShotFolderChanged(int index, const char *folder)
{
    wchar_t wideFolder[1200];
    DWORD state;
    if (index < 0 || index >= RPG_DATA_SHOT_MAX_COUNT || folder == NULL || !FolderExistsUtf8(folder)) return false;
    if (strcmp(dataShotFolderWatchPaths[index], folder) != 0) {
        CloseDataShotFolderWatcher(index);
        if (!ToWide(folder, wideFolder, 1200)) return true;
        dataShotFolderWatchers[index] = FindFirstChangeNotificationW(
            wideFolder, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (dataShotFolderWatchers[index] == INVALID_HANDLE_VALUE) {
            dataShotFolderWatchers[index] = NULL;
            return true;
        }
        snprintf(dataShotFolderWatchPaths[index], sizeof(dataShotFolderWatchPaths[index]), "%s", folder);
        return true;
    }
    if (dataShotFolderWatchers[index] == NULL) return true;
    state = WaitForSingleObject(dataShotFolderWatchers[index], 0);
    if (state == WAIT_TIMEOUT) return false;
    if (state == WAIT_OBJECT_0) {
        if (FindNextChangeNotification(dataShotFolderWatchers[index]) == 0)
            CloseDataShotFolderWatcher(index);
        return true;
    }
    CloseDataShotFolderWatcher(index);
    return true;
}

static bool BlockPath(const RpgObjectFolder *folder, int blockType, bool inInbox, char *path, size_t size)
{
    char name[256], parent[1200];
    if (!BlockName(folder, blockType, name, sizeof(name)) ||
        !(inInbox ? GetInboxPath(parent, sizeof(parent)) : GetCellsPath(parent, sizeof(parent)))) return false;
    return snprintf(path, size, "%s\\%s", parent, name) > 0;
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

/* 実Explorerが開いているZipper/Inboxの実体を消すと、Shellビューが古いフォルダを保持して
   以後の移動通知を受け取れなくなる。セッション掃除ではルートを保持して内容だけを消す。 */
static void ClearFolderContents(const char *directory)
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
    }
    return moved;
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
            wcscmp(data.cFileName, L"object_info.txt") == 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) > 0 &&
            snprintf(child, sizeof(child), "%s\\%s", directory, name) > 0) {
            if (HasExternalFiles(child)) { FindClose(handle); return true; }
        } else { FindClose(handle); return true; }
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    return false;
}

static bool WriteInfo(const char *folder, const char *kind, int type, int identity, Vector2 position)
{
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    if (!CreateFolderUtf8(folder) || snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "kind=%s\ntype=%d\nid=%d\nworld_x=%.1f\nworld_y=%.1f\n", kind, type, identity, position.x, position.y);
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, infoPath, NULL);
    NotifyShellParentChanged(infoPath);
    return true;
}

/* 保存方式はパスを知らず、ここから渡す最小のファイル操作だけを利用する。 */
static bool WriteBuildCellFolder(void *context, RpgGridCell cell, int blockType)
{
    RpgObjectFolder folder = { .cell = cell };
    char cellPath[1200];
    Vector2 position = { (cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                         (cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    (void)context;
    return BlockPath(&folder, blockType, false, cellPath, sizeof(cellPath)) &&
           WriteInfo(cellPath, "cell_block", blockType,
                     cell.row * RPG_STAGE_WORLD_COLUMNS + cell.column + 1, position);
}

static bool GetBuildCellFilePath(void *context, const char *fileName, char *path, size_t pathSize)
{
    (void)context;
    return activeBuildPath[0] != '\0' && fileName != NULL &&
           snprintf(path, pathSize, "%s\\%s", activeBuildPath, fileName) > 0;
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

static bool RPG_OBJECT_FOLDER_UNUSED WriteCompactCellMetadata(const RpgStage *stage)
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
    if (fclose(file) != 0) return false;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, metadataPath, NULL);
    NotifyShellParentChanged(metadataPath);
    return true;
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
    char source[1200], inbox[1200], objects[1200];
    if (!AttachmentPath(attachment, false, source, sizeof(source)) || !AttachmentPath(attachment, true, inbox, sizeof(inbox)) ||
        !GetObjectsPath(objects, sizeof(objects))) return false;
    if (FolderExistsUtf8(source) || FolderExistsUtf8(inbox)) return true;
    return CreateFolderUtf8(objects) && WriteInfo(source, "attachment", attachment->type, attachment->folderId,
                                                RpgAttachments_GetPosition(attachment, 0));
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
    if (!EnsureAttachment(attachment) || !AttachmentPath(attachment, false, attachmentSource, sizeof(attachmentSource)) ||
        !AttachmentPath(attachment, true, attachmentInbox, sizeof(attachmentInbox)) ||
        snprintf(parent, sizeof(parent), "%s\\parent", source) <= 0) return false;
    // 発射時点の装置フォルダ全体を parent に入れ、弾自身の object_info.txt は root に残す。
    const char *parentSource = FolderExistsUtf8(attachmentSource) ? attachmentSource : attachmentInbox;
    bool created = CreateFolderUtf8(objects) &&
                   WriteInfo(source, "data_shot", 0, shot->folderSerial, shot->position) &&
                   CopyTree(parentSource, parent);
    if (created) NotifyShellPathHierarchyChanged(source);
    return created;
}

static void UpdateDataShotProperties(int shotIndex, RpgDataShot *shot, const RpgAttachments *attachments)
{
    char source[1200], inbox[1200];
    int fileCount = 0;
    unsigned long long totalBytes = 0;
    bool hasAddedFiles = false;
    bool recordBaseline;
    if (!DataShotPath(shot, false, source, sizeof(source)) || !DataShotPath(shot, true, inbox, sizeof(inbox))) return;
    const char *folder = FolderExistsUtf8(source) ? source : inbox;
    recordBaseline = !shot->folderStatsInitialized;
    if (!recordBaseline && !HasDataShotFolderChanged(shotIndex, folder)) return;
    // 初回も監視を開始する。以後は配下の変更通知が届いた時だけ集計する。
    if (recordBaseline) HasDataShotFolderChanged(shotIndex, folder);
    GetDataShotFolderStats(folder, folder, shot, recordBaseline, &fileCount, &totalBytes, &hasAddedFiles);
    if (recordBaseline) shot->folderStatsInitialized = true;
    if (!recordBaseline && fileCount == shot->fileCount && totalBytes == shot->totalBytes &&
        hasAddedFiles == shot->hasAddedFiles) return;
    if (shot->attachmentIndex < 0 || shot->attachmentIndex >= attachments->count) return;
    shot->hasAddedFiles = hasAddedFiles;
    // 容量が増えるほど緩やかに減速し、極端な容量でも停止しない最低速度を残す。
    shot->speed = 360.0f / (1.0f + (float)totalBytes / (64.0f * 1024.0f));
    if (shot->speed < 30.0f) shot->speed = 30.0f;
    if (shot->speed > 360.0f) shot->speed = 360.0f;
    RpgDataShot_SetFileProperties(shot, &attachments->entries[shot->attachmentIndex],
                                  fileCount, totalBytes);
}

/* データ弾はマスを占有しないため、フォルダに最後のグリッド位置と親装置上の軌道位置だけを残す。 */
static bool WriteDataShotRuntimeMetadata(RpgDataShot *shot)
{
    char source[1200], inbox[1200], staging[1200], infoPath[1200];
    wchar_t wideInfoPath[1200];
    RpgGridCell cell;
    const char *folder = NULL;
    FILE *file;
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
    if (folder == NULL || snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    /* object_info.txt は既存の共通メタ情報。データ弾の復元情報もここへまとめる。 */
    fprintf(file, "kind=data_shot\ntype=0\nid=%d\nworld_x=%.1f\nworld_y=%.1f\n"
            "cell_row=%d\ncell_column=%d\nattachment_index=%d\npath_cell_index=%d\n",
            shot->folderSerial, shot->position.x, shot->position.y,
            cell.row, cell.column, shot->attachmentIndex, shot->pathCellIndex);
    if (fclose(file) != 0) return false;
    shot->metadataCell = cell;
    NotifyShellChange(RPG_SHCNE_UPDATEITEM, infoPath, NULL);
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
        return WriteInfo(inbox, "cell_block", storedBlockType,
                         folder->cell.row * RPG_STAGE_WORLD_COLUMNS + folder->cell.column + 1, position);
    }
    // 通常ブロックは普段は仮想的にだけ存在し、アニメーション時にメタ情報付きで実体化する。
    position = (Vector2){ (folder->cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                          (folder->cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    return WriteInfo(inbox, "block", blockType,
                     folder->cell.row * RPG_STAGE_WORLD_COLUMNS + folder->cell.column + 1, position);
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

bool RpgObjectFolder_CopyFileToZipperInbox(const char *sourcePath)
{
#ifdef _WIN32
    char inbox[1200], destination[1200];
    const char *fileName;
    wchar_t wideSource[1200], wideDestination[1200];
    if (sourcePath == NULL || sourcePath[0] == '\0' || !GetInboxPath(inbox, sizeof(inbox)) || !CreateFolderUtf8(inbox)) return false;
    fileName = strrchr(sourcePath, '\\');
    fileName = fileName == NULL ? sourcePath : fileName + 1;
    if (snprintf(destination, sizeof(destination), "%s\\%s", inbox, fileName) <= 0 ||
        !ToWide(sourcePath, wideSource, 1200) || !ToWide(destination, wideDestination, 1200)) return false;
    if (CopyFileW(wideSource, wideDestination, FALSE) == 0) return false;
    NotifyShellChange(RPG_SHCNE_CREATE, destination, NULL);
    NotifyShellPathHierarchyChanged(destination);
    return true;
#else
    (void)sourcePath; return false;
#endif
}

void RpgObjectFolder_PrepareZipperAnimationCommand(void)
{
#ifdef _WIN32
    char inbox[1200], command[1200], script[1200]; wchar_t wideCommand[1200], wideScript[1200]; FILE *file;
    if (!GetInboxPath(inbox, sizeof(inbox)) || !CreateFolderUtf8(inbox) ||
        snprintf(command, sizeof(command), "%s\\assyuku.cmd", inbox) <= 0 ||
        snprintf(script, sizeof(script), "%s\\assyuku.vbs", inbox) <= 0 ||
        !ToWide(command, wideCommand, 1200) || !ToWide(script, wideScript, 1200)) return;
    file = _wfopen(wideCommand, L"wb");
    if (file != NULL) {
        /* cmdは既存操作との互換用。ダブルクリック時のコンソール表示を避けるには.vbsを使う。 */
        fputs("@echo off\r\ndel /f /q \"%~dp0assyuku.request\" >nul 2>nul\r\n"
               "> \"%~dp0assyuku.request\" echo animate %DATE%_%TIME%_%RANDOM%_%RANDOM%\r\n", file);
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
              "request = fso.BuildPath(fso.GetParentFolderName(WScript.ScriptFullName), \"assyuku.request\")\r\n"
              "If fso.FileExists(request) Then fso.DeleteFile request, True\r\n"
              "Set output = fso.CreateTextFile(request, True)\r\n"
              "output.WriteLine \"animate \" & Timer & \"_\" & Rnd & \"_\" & Rnd\r\n"
              "output.Close\r\n", file);
        if (fclose(file) == 0) {
            NotifyShellChange(RPG_SHCNE_CREATE, script, NULL);
            NotifyShellPathHierarchyChanged(script);
        }
    }
#endif
}

bool RpgObjectFolder_BeginZipperCommandRequest(void)
{
#ifdef _WIN32
    char inbox[1200], request[1200], token[256] = { 0 }; wchar_t wideRequest[1200]; FILE *file;
    if (!GetInboxPath(inbox, sizeof(inbox)) || snprintf(request, sizeof(request), "%s\\assyuku.request", inbox) <= 0 ||
        !ToWide(request, wideRequest, 1200)) {
        hasDispatchedZipperRequest = false;
        hasPendingZipperRequest = false;
        return false;
    }
    /* 完了後に削除が遅れても、同一の書込み要求は再実行しない。 */
    file = _wfopen(wideRequest, L"rb");
    if (file == NULL) {
        hasPendingZipperRequest = false;
        return false;
    }
    (void)fgets(token, (int)sizeof(token), file);
    fclose(file);
    token[strcspn(token, "\r\n")] = '\0';
    if (token[0] == '\0' || (hasDispatchedZipperRequest && strcmp(token, dispatchedZipperRequestToken) == 0)) return false;
    snprintf(pendingZipperRequestToken, sizeof(pendingZipperRequestToken), "%s", token);
    hasPendingZipperRequest = true;
    return true;
#else
    return false;
#endif
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
    if (!GetInboxPath(inbox, sizeof(inbox)) || snprintf(request, sizeof(request), "%s\\assyuku.request", inbox) <= 0 ||
        !ToWide(request, wideRequest, 1200)) return true;
    if (DeleteFileW(wideRequest) != 0) {
        /* 一時要求はExplorerへ見せる対象でないため、重い同期通知は送らない。 */
    }
    return true;
#else
    return false;
#endif
}

bool RpgObjectFolder_MoveAttachmentToZipper(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char source[1200], inbox[1200], parent[1200];
    return EnsureAttachment(attachment) && AttachmentPath(attachment, false, source, sizeof(source)) &&
           AttachmentPath(attachment, true, inbox, sizeof(inbox)) && GetInboxPath(parent, sizeof(parent)) && CreateFolderUtf8(parent) &&
           (FolderExistsUtf8(inbox) || MoveDirectory(source, inbox));
#else
    (void)attachment; return false;
#endif
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
    return (blockType != 0 || activeBuildPath[0] != '\0') && MaterializeBlockInInbox(folder, blockType);
#else
    (void)folder; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_BeginReturnAttachmentFromZipper(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    return activeBuildPath[0] != '\0' && AttachmentPath(attachment, false, source, sizeof(source)) &&
           AttachmentPath(attachment, true, inbox, sizeof(inbox)) && BeginFolderReturn(inbox, source);
#else
    (void)attachment; return false;
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

bool RpgObjectFolder_ReturnAttachmentFromZipper(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char source[1200], inbox[1200], objects[1200];
    /* 返却先は常に実行中のbuild成果物に限定し、設定元のStageは書き換えない。 */
    return activeBuildPath[0] != '\0' && AttachmentPath(attachment, false, source, sizeof(source)) && AttachmentPath(attachment, true, inbox, sizeof(inbox)) &&
           GetObjectsPath(objects, sizeof(objects)) && CreateFolderUtf8(objects) && CompleteFolderReturn(inbox, source);
#else
    (void)attachment; return false;
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
    char source[1200], infoPath[1200], kind[64];
    wchar_t wideInfoPath[1200];
    int type, identity, row, column, attachmentIndex, pathCellIndex;
    float worldX, worldY;
    FILE *file;
    if (shot == NULL || !DataShotPath(shot, false, source, sizeof(source)) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", source) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"rb");
    if (file == NULL) return false;
    bool read = fscanf(file, "kind=%63[^\n]\ntype=%d\nid=%d\nworld_x=%f\nworld_y=%f\n"
                       "cell_row=%d\ncell_column=%d\nattachment_index=%d\npath_cell_index=%d",
                       kind, &type, &identity, &worldX, &worldY,
                       &row, &column, &attachmentIndex, &pathCellIndex) == 9;
    fclose(file);
    if (!read || strcmp(kind, "data_shot") != 0 || type != 0 || identity != shot->folderSerial ||
        row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    shot->position = (Vector2){ (column + 0.5f) * RPG_STAGE_TILE_SIZE, (row + 0.5f) * RPG_STAGE_TILE_SIZE };
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
    return GetCellsPath(cells, sizeof(cells)) && CreateFolderUtf8(cells) && CompleteFolderReturn(inbox, source);
#else
    (void)folder; (void)blockType; return false;
#endif
}

void RpgObjectFolders_PrepareAttachmentFolders(const RpgAttachments *attachments)
{
#ifdef _WIN32
    if (attachments != NULL) for (int index = 0; index < attachments->count; index++) EnsureAttachment(&attachments->entries[index]);
#else
    (void)attachments;
#endif
}

void RpgObjectFolders_UpdateDataShotLifetimes(RpgDataShots *shots, const RpgAttachments *attachments,
                                              RpgReferenceObjects *referenceObjects)
{
#ifdef _WIN32
    if (shots == NULL) return;
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
        CloseDataShotFolderWatcher(index);
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
#ifdef _WIN32
    char source[1200], inbox[1200];
    return (AttachmentPath(attachment, false, source, sizeof(source)) && HasExternalFiles(source)) ||
           (AttachmentPath(attachment, true, inbox, sizeof(inbox)) && HasExternalFiles(inbox));
#else
    (void)attachment; return false;
#endif
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
    char source[1200], destination[1200];
    return EnsureAttachment(from) && AttachmentPath(from, false, source, sizeof(source)) &&
           AttachmentPath(to, false, destination, sizeof(destination)) && (strcmp(source, destination) == 0 || MoveDirectory(source, destination));
#else
    (void)from; (void)to; return false;
#endif
}

void RpgObjectFolder_RemoveAttachmentFolder(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char source[1200], inbox[1200];
    if (AttachmentPath(attachment, false, source, sizeof(source))) RemoveTree(source);
    if (AttachmentPath(attachment, true, inbox, sizeof(inbox))) RemoveTree(inbox);
#else
    (void)attachment;
#endif
}

bool RpgObjectFolders_BeginStageBuild(int stageNumber, const RpgStage *stage,
                                      const RpgAttachments *attachments, Vector2 playerStartPosition,
                                      bool isSimpleBuild,
                                      char *buildPath, size_t buildPathSize)
{
#ifdef _WIN32
    char cells[1200], objects[1200], inbox[1200];
    if (stageNumber <= 0 || stage == NULL || buildPath == NULL || buildPathSize == 0) return false;
    int written = snprintf(buildPath, buildPathSize, "%s../assets/Settings/Stage/Stage%d/build",
                           GetApplicationDirectory(), stageNumber);
    if (written <= 0 || (size_t)written >= buildPathSize) return false;

    RpgObjectFolders_EndStageBuild();
    isBulkBuildOperation = true;
    RpgObjectFolders_ClearSessionStorage();
    RemoveTree(buildPath);
    /* 前回の成果物が一つでも残った状態で初期化を続けると世代が混ざるため、完全削除できない場合は失敗にする。 */
    if (FolderExistsUtf8(buildPath)) { isBulkBuildOperation = false; return false; }
    snprintf(activeBuildPath, sizeof(activeBuildPath), "%s", buildPath);
    if (!CreateFolderUtf8(activeBuildPath) || !GetCellsPath(cells, sizeof(cells)) ||
        !GetObjectsPath(objects, sizeof(objects)) || !GetInboxPath(inbox, sizeof(inbox)) ||
        !CreateFolderUtf8(cells) || !CreateFolderUtf8(objects) || !CreateFolderUtf8(inbox)) {
        activeBuildPath[0] = '\0';
        isBulkBuildOperation = false;
        return false;
    }
    int playerStartMap = (int)(playerStartPosition.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
    if (playerStartMap < 0) playerStartMap = 0;
    if (playerStartMap >= RPG_STAGE_MAP_COUNT) playerStartMap = RPG_STAGE_MAP_COUNT - 1;
    RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
    if (!(isSimpleBuild ? RpgBuildCellStorage_CreatePreview(stage, playerStartMap, &storageBackend) :
                          RpgBuildCellStorage_Create(stage, playerStartMap, &storageBackend))) {
        RpgObjectFolders_EndStageBuild();
        isBulkBuildOperation = false;
        return false;
    }
    RpgObjectFolders_PrepareAttachmentFolders(attachments);
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

void RpgObjectFolders_UpdateBuildCellGeneration(void)
{
#ifdef _WIN32
    if (activeBuildPath[0] == '\0') return;
    RpgBuildCellStorageBackend storageBackend = GetBuildCellStorageBackend();
    isBulkBuildOperation = true;
    RpgBuildCellStorage_Update(&storageBackend);
    isBulkBuildOperation = false;
    NotifyShellChange(RPG_SHCNE_UPDATEDIR, activeBuildPath, NULL);
#endif
}

bool RpgObjectFolders_IsBuildCellAvailable(RpgGridCell cell)
{
#ifdef _WIN32
    RpgObjectFolder folder = { .cell = cell };
    char source[1200], inbox[1200];
    bool compactAvailable[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return false;
    if (RpgObjectFolders_ReadCompactCellAvailability(compactAvailable) && compactAvailable[cell.row][cell.column]) return true;
    return (BlockPath(&folder, 0, false, source, sizeof(source)) && FolderExistsUtf8(source)) ||
           (BlockPath(&folder, 0, true, inbox, sizeof(inbox)) && FolderExistsUtf8(inbox));
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
    activeBuildPath[0] = '\0';
    RpgObjectFolders_ClearBuildCellLinkedFiles();
}

void RpgObjectFolders_ClearSessionStorage(void)
{
#ifdef _WIN32
    char objects[1200], drops[1200], inbox[1200];
    memset(dataShotFolderSerials, 0, sizeof(dataShotFolderSerials)); nextFileDropId = 1;
    hasDispatchedZipperRequest = false;
    hasPendingZipperRequest = false;
    dispatchedZipperRequestToken[0] = '\0';
    pendingZipperRequestToken[0] = '\0';
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) CloseDataShotFolderWatcher(index);
    if (activeBuildPath[0] == '\0' && GetObjectsPath(objects, sizeof(objects))) RemoveTree(objects);
    if (activeBuildPath[0] == '\0' && GetDropsPath(drops, sizeof(drops))) RemoveTree(drops);
    /* Inboxは実Explorerの表示ルートなので、削除・再作成せず同じディレクトリ内を掃除する。 */
    if (GetInboxPath(inbox, sizeof(inbox))) ClearFolderContents(inbox);
#endif
}
// 役割: オブジェクト所有フォルダ、Zipper への移動、外部ファイルの引き継ぎを管理する。
