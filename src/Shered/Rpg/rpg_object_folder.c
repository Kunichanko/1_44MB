// 依存する自プロジェクト内ファイル: rpg_object_folder.h
#include "rpg_object_folder.h"
#include "rpg_build_cell_storage.h"
#include "rpg_explorer_launcher.h"
#include "rpg_stage_storage.h"

// 依存関係: build の通常マス生成は rpg_build_cell_storage の選択方式へ委譲する。

#include <stdio.h>
#include <string.h>
#include <wchar.h>

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
/* 本編の「ビルドする」で選択した StageN/build。エディターは空のまま従来の一時領域を使う。 */
static char activeBuildPath[1200] = { 0 };
static int activeBuildStageNumber = 0;
/* 現在の Zipper 構造のルート。build/Zipper 固定ではなく、cmd を実行した Folder へ切り替わる。 */
static char activeZipperPath[1200] = { 0 };
static bool activeBuildPersists = false;
/* 全マス生成中は数千件の Shell 通知をまとめ、ビルド処理が Explorer 更新で詰まらないようにする。 */
static bool isBulkBuildOperation = false;
/* 描画中はファイルシステムへ触れず、build の変更通知を受けたマスだけ更新する。 */
static bool buildCellLinkedFiles[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { { false } };
// データ弾ごとに配下を含む変更通知を持ち、毎フレームの再帰走査を避ける。
static HANDLE dataShotFolderWatchers[RPG_DATA_SHOT_MAX_COUNT] = { NULL };
static char dataShotFolderWatchPaths[RPG_DATA_SHOT_MAX_COUNT][1200] = { { 0 } };
/* Zipper は毎フレーム走査せず、Windows の変更通知を受けた時だけ容量を再集計する。 */
static HANDLE zipperStorageWatcher = NULL;
static char zipperStorageWatchPath[1200] = { 0 };
static unsigned long long zipperStorageBytes = 0;

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

/* Q で開く実 Zipper フォルダそのものを容量対象にし、Explorer のプロパティと一致させる。 */
static bool GetZipperStorageDirectory(char *path, size_t size)
{
    return RpgExplorerLauncher_GetZipperDirectory(path, size);
}

static void CloseZipperStorageWatcher(void)
{
    if (zipperStorageWatcher != NULL && zipperStorageWatcher != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(zipperStorageWatcher);
    zipperStorageWatcher = NULL;
    zipperStorageWatchPath[0] = '\0';
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
    DWORD state;
    if (!GetZipperStorageDirectory(storageDirectory, sizeof(storageDirectory)) ||
        !FolderExistsUtf8(storageDirectory)) {
        CloseZipperStorageWatcher();
        zipperStorageBytes = 0;
        return 0;
    }
    if (strcmp(zipperStorageWatchPath, storageDirectory) != 0) {
        CloseZipperStorageWatcher();
        if (!ToWide(storageDirectory, widePath, 1200)) return zipperStorageBytes = 0;
        zipperStorageWatcher = FindFirstChangeNotificationW(
            widePath, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (zipperStorageWatcher == INVALID_HANDLE_VALUE) zipperStorageWatcher = NULL;
        snprintf(zipperStorageWatchPath, sizeof(zipperStorageWatchPath), "%s", storageDirectory);
        zipperStorageBytes = MeasureDirectoryBytes(widePath);
        return zipperStorageBytes;
    }
    if (zipperStorageWatcher == NULL) return zipperStorageBytes;
    state = WaitForSingleObject(zipperStorageWatcher, 0);
    if (state == WAIT_OBJECT_0) {
        if (!ToWide(storageDirectory, widePath, 1200)) return zipperStorageBytes;
        zipperStorageBytes = MeasureDirectoryBytes(widePath);
        if (FindNextChangeNotification(zipperStorageWatcher) == 0) CloseZipperStorageWatcher();
    } else if (state != WAIT_TIMEOUT) {
        CloseZipperStorageWatcher();
    }
    return zipperStorageBytes;
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
{
    return activeZipperPath[0] != '\0' && snprintf(path, size, "%s\\Inbox", activeZipperPath) > 0;
}

static bool SetActiveZipperPath(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    if (snprintf(activeZipperPath, sizeof(activeZipperPath), "%s", path) <= 0) return false;
    RpgExplorerLauncher_SetZipperDirectory(activeZipperPath);
    return true;
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
    {
        char inbox[1200];
        if (!GetInboxPath(inbox, sizeof(inbox)) || !CreateFolderUtf8(inbox)) return false;
    }
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

/* PNG配置物はセルフォルダではなく、弾・設置物と同じ非占有オブジェクト名で区別する。 */
static bool ImageObjectName(const RpgImageObject *object, char *name, size_t size)
{
    return object != NULL && object->id > 0 &&
           snprintf(name, size, "image_object_%03d_%06u", (int)object->appearance, object->id) > 0;
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

static bool ImageObjectPath(const RpgImageObject *object, bool inInbox, char *path, size_t size)
{
    char name[256];
    return ImageObjectName(object, name, sizeof(name)) && ObjectPath(name, inInbox, path, size);
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

bool RpgObjectFolder_GetBlockDirectory(const RpgObjectFolder *folder, int blockType,
                                       char *path, size_t pathSize)
{
    if (!BlockPath(folder, blockType, false, path, pathSize)) return false;
    return CreateFolderUtf8(path);
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
            wcscmp(data.cFileName, L"object_info.txt") == 0 ||
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

/* Folderを外部CMDから操作しても、どのステージ・マスの配置物か再現できるように記録する。 */
static bool WriteReferenceFolderInfo(const char *folder, RpgGridCell cell)
{
#ifdef _WIN32
    char infoPath[1200];
    wchar_t wideInfoPath[1200];
    FILE *file;
    Vector2 position = { (cell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                         (cell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    if (folder == NULL || folder[0] == '\0' || !WriteInfo(folder, "reference_folder",
        RPG_BLOCK_REFERENCE_FOLDER, cell.row * RPG_STAGE_WORLD_COLUMNS + cell.column + 1, position) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\object_info.txt", folder) <= 0 ||
        !ToWide(infoPath, wideInfoPath, (int)(sizeof(wideInfoPath) / sizeof(wideInfoPath[0])))) return false;
    file = _wfopen(wideInfoPath, L"ab");
    if (file == NULL) return false;
    fprintf(file, "stage=%d\ncell_row=%d\ncell_column=%d\nzipper_command=zipper.cmd\n",
            activeBuildStageNumber, cell.row, cell.column);
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
        char folderPath[1200], targetPath[1200];
        wchar_t wideSource[1200], wideTarget[1200];
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        source = RpgStage_GetReferencePathAtCell(stage, row, column);
        name = strrchr(source, '\\');
        if (name == NULL) name = strrchr(source, '/');
        name = name == NULL ? source : name + 1;
        if (name[0] == '\0' || snprintf(folderPath, sizeof(folderPath), "%s\\reference_files\\reference_r%02d_c%03d", activeBuildPath, row, column) <= 0 ||
            snprintf(targetPath, sizeof(targetPath), "%s\\%s", folderPath, name) <= 0 || !CreateFolderUtf8(TextFormat("%s\\reference_files", activeBuildPath)) ||
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
    static const char *folders[] = { "cells", "objects", "drops", "Zipper", "folders", "reference_files", "runtime_state" };
    char path[1200];
    wchar_t widePath[1200];
    if (buildPath == NULL || buildPath[0] == '\0') return false;
    for (int index = 0; index < (int)(sizeof(folders) / sizeof(folders[0])); index++) {
        if (snprintf(path, sizeof(path), "%s\\%s", buildPath, folders[index]) <= 0) return false;
        RemoveTree(path);
        if (FolderExistsUtf8(path)) return false;
    }
    if (snprintf(path, sizeof(path), "%s\\cells_metadata.txt", buildPath) <= 0 || !ToWide(path, widePath, 1200)) return false;
    if (DeleteFileW(widePath) == 0 && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    return true;
#else
    (void)buildPath;
    return false;
#endif
}

/* static の参照コピーを実行用へ直接複製する。Fileの不備はそのマスだけを除外し、Playは継続する。 */
static void PrepareRuntimeReferenceFiles(RpgStage *stage)
{
#ifdef _WIN32
    char referenceRoot[1200], sourcePath[1200], fallbackPath[1200], folderPath[1200], targetPath[1200];
    wchar_t wideSource[1200], wideTarget[1200];
    if (stage == NULL ||
        snprintf(referenceRoot, sizeof(referenceRoot), "%s\\reference_files", activeBuildPath) <= 0 ||
        !CreateFolderUtf8(referenceRoot)) return;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const char *source;
        const char *name;
        DWORD attributes;
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
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
            !ToWide(targetPath, wideTarget, 1200) || CopyFileW(wideSource, wideTarget, FALSE) == 0 ||
            !RpgStage_SetReferencePathAtCell(stage, row, column, targetPath)) goto unavailable;
        continue;
unavailable:
        /* A missing source can be transient: the static package may have just
           been extracted, or a reference copy may be repaired on the next
           build.  Never turn that temporary lookup failure into a deletion of
           the stage object (and then persist it into runtime_state). */
        continue;
    }
#else
    (void)stage;
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
    if (!CreateFolderUtf8(objects) || !WriteInfo(source, "image_object", object->appearance,
                                                  (int)object->id, position) ||
        snprintf(infoPath, sizeof(infoPath), "%s\\image_info.txt", source) <= 0 ||
        !ToWide(infoPath, wideInfoPath, 1200)) return false;
    file = _wfopen(wideInfoPath, L"wb");
    if (file == NULL) return false;
    fprintf(file, "row=%d\ncolumn=%d\nworld_x=%.3f\nworld_y=%.3f\nlayer=%d\nscale=%.3f\npath=%s\n",
            object->row, object->column, position.x, position.y, (int)object->layer, object->scale,
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
    return CreateFolderUtf8(objects) && WriteInfo(playerFolder, "player", 0, 1, position);
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
    char cells[1200], search[1200];
    wchar_t wideSearch[1200];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (sourcePath == NULL || (reference = strstr(sourcePath, "\\reference_files\\reference_r")) == NULL ||
        sscanf(reference, "\\reference_files\\reference_r%d_c%d", &row, &column) != 2 ||
        row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !GetCellsPath(cells, sizeof(cells)) ||
        snprintf(search, sizeof(search), "%s\\cell_block_*_r%02d_c%03d", cells, row, column) <= 0 ||
        !ToWide(search, wideSearch, (int)(sizeof(wideSearch) / sizeof(wideSearch[0])))) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[512], path[1200];
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(path, sizeof(path), "%s\\%s", cells, name) <= 0) continue;
        RemoveTree(path);
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
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
        return true;
    }
    if (MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) == 0) return false;
    RemoveStoredSourceObjectFolder(sourcePath);
    NotifyShellChange(RPG_SHCNE_RENAMEITEM, sourcePath, destination);
    NotifyShellParentChanged(sourcePath);
    NotifyShellParentChanged(destination);
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

void RpgObjectFolders_PrepareReferenceFolderMetadata(const RpgStage *stage)
{
    if (stage == NULL) return;
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

bool RpgObjectFolders_BeginStageBuild(int stageNumber, RpgStage *stage,
                                      const RpgAttachments *attachments, Vector2 playerStartPosition,
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
    if (!(isSimpleBuild ? RpgBuildCellStorage_CreatePreview(stage, playerStartMap, &storageBackend) :
                          RpgBuildCellStorage_Create(stage, playerStartMap, &storageBackend))) {
        RpgObjectFolders_EndStageBuild();
        isBulkBuildOperation = false;
        return false;
    }
    PrepareRuntimeReferenceFiles(stage);
    /* Folder は設計側では名前だけを持ち、実行時の実体はこの動的ディレクトリへ作る。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        const char *oldPath;
        const char *name;
        char folderPath[1200];
        if (!RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column])) continue;
        oldPath = RpgStage_GetReferencePathAtCell(stage, row, column);
        name = strrchr(oldPath, '\\');
        if (name == NULL) name = strrchr(oldPath, '/');
        name = name == NULL ? oldPath : name + 1;
        if (name[0] == '\0' || snprintf(folderPath, sizeof(folderPath), "%s\\folders\\%s", activeBuildPath, name) <= 0 ||
            !CreateFolderUtf8(TextFormat("%s\\folders", activeBuildPath)) || !CreateFolderUtf8(folderPath) ||
            !RpgStage_SetReferencePathAtCell(stage, row, column, folderPath)) { RpgObjectFolders_EndStageBuild(); isBulkBuildOperation = false; return false; }
    }
    RpgObjectFolders_PrepareAttachmentFolders(attachments);
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

bool RpgObjectFolders_ResumeStageBuild(int stageNumber, RpgStage *stage, char *buildPath, size_t buildPathSize)
{
#ifdef _WIN32
    char zipperPath[1200];
    RpgBuildCellStorageBackend storageBackend;
    if (stageNumber <= 0 || stage == NULL || buildPath == NULL || buildPathSize == 0 ||
        !RpgStageStorage_GetRuntimePath(stageNumber, RPG_STAGE_RUNTIME_GAME, buildPath, (int)buildPathSize) ||
        !FolderExistsUtf8(buildPath)) return false;
    snprintf(activeBuildPath, sizeof(activeBuildPath), "%s", buildPath);
    activeBuildStageNumber = stageNumber;
    activeBuildPersists = true;
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    activeZipperPath[0] = '\0';
    RpgExplorerLauncher_SetZipperDirectory(NULL);
    /* データ弾など、マスに定着しない実行時オブジェクトは再開時に持ち越さない。 */
    (void)RpgStageStorage_RepairReferenceFileCopies(stageNumber, stage);
    PrepareRuntimeReferenceFiles(stage);
    RemoveTransientDataShotFolders();
    if (snprintf(zipperPath, sizeof(zipperPath), "%s\\folders\\Zipper", activeBuildPath) > 0 &&
        FolderExistsUtf8(zipperPath)) (void)SetActiveZipperPath(zipperPath);
    /* フォルダを取り込んだセルは cells_metadata.txt から欠損として復元する。 */
    storageBackend = GetBuildCellStorageBackend();
    (void)RpgBuildCellStorage_ApplyProgressState(stage, &storageBackend);
    /* 特殊ブロックはセルごとのオブジェクトフォルダが正本。格納済みでフォルダが無ければ復活させない。 */
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgObjectFolder folder = { .cell = { row, column } };
        char objectPath[1200];
        int blockType = stage->blocks[row][column];
        if (RpgBlockInventory_IsReferenceObject(blockType) ||
            RpgBuildCellStorage_UsesMetadataForBlock(blockType) || blockType == RPG_BLOCK_BUILD_MISSING ||
            !BlockPath(&folder, blockType, false, objectPath, sizeof(objectPath))) continue;
        if (!FolderExistsUtf8(objectPath)) stage->blocks[row][column] = RPG_BLOCK_BUILD_MISSING;
    }
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
            (void)sscanf(line, "world_x=%f", &x);
            (void)sscanf(line, "world_y=%f", &y);
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
    /* ステージを離れた時点でPlayerは残さない。stage.package は同じ StageN にあるため、
       ここでは実行時成果物だけを消し、次回ビルド用の静的パッケージを削除しない。 */
    char completedBuildPath[1200];
    snprintf(completedBuildPath, sizeof(completedBuildPath), "%s", activeBuildPath);
    if (!activeBuildPersists) RemovePlayerFolder();
    else RemoveTransientDataShotFolders();
    activeBuildPath[0] = '\0';
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
    CloseZipperStorageWatcher();
    zipperStorageBytes = 0;
    if (activeBuildPath[0] == '\0' && GetObjectsPath(objects, sizeof(objects))) RemoveTree(objects);
    if (activeBuildPath[0] == '\0' && GetDropsPath(drops, sizeof(drops))) RemoveTree(drops);
    /* Inboxは実Explorerの表示ルートなので、削除・再作成せず同じディレクトリ内を掃除する。 */
    if (GetInboxPath(inbox, sizeof(inbox))) ClearFolderContents(inbox);
#endif
}
// 役割: オブジェクト所有フォルダ、Zipper への移動、外部ファイルの引き継ぎを管理する。
