// 依存する自プロジェクト内ファイル: rpg_object_folder.h
#include "rpg_object_folder.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <shellapi.h>

// NOUSER を維持したまま Explorer 更新通知だけを利用するための最小宣言。
void WINAPI SHChangeNotify(LONG eventId, UINT flags, const void *item1, const void *item2);
enum { RPG_SHCNE_MKDIR = 0x00000008L, RPG_SHCNE_RMDIR = 0x00000010L,
       RPG_SHCNE_UPDATEDIR = 0x00001000L, RPG_SHCNE_RENAMEFOLDER = 0x00020000L,
       RPG_SHCNF_PATHW = 0x0005U };

static int dataShotFolderSerials[RPG_DATA_SHOT_MAX_COUNT] = { 0 };
static int nextFileDropId = 1;
// データ弾ごとに配下を含む変更通知を持ち、毎フレームの再帰走査を避ける。
static HANDLE dataShotFolderWatchers[RPG_DATA_SHOT_MAX_COUNT] = { NULL };
static char dataShotFolderWatchPaths[RPG_DATA_SHOT_MAX_COUNT][1200] = { { 0 } };

static bool ToWide(const char *path, wchar_t *wide, int count)
{ return path != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) > 0; }

// GetApplicationDirectory() を起点にした ".." を解決し、Explorer が表示中の実パスへ通知する。
static bool ToAbsoluteWide(const char *path, wchar_t *wide, int count)
{
    wchar_t input[1200];
    DWORD length;
    if (!ToWide(path, input, 1200)) return false;
    length = GetFullPathNameW(input, (DWORD)count, wide, NULL);
    return length > 0 && length < (DWORD)count;
}

bool RpgObjectFolder_OpenZipperDirectory(void)
{
    char directory[1200];
    wchar_t wideDirectory[1200];
    if (snprintf(directory, sizeof(directory), "%s../assets/Settings/Zipper",
                 GetApplicationDirectory()) <= 0 ||
        !ToAbsoluteWide(directory, wideDirectory, 1200)) return false;
    CreateDirectoryW(wideDirectory, NULL);
    return (INT_PTR)ShellExecuteW(NULL, L"open", wideDirectory, NULL, NULL, 1) > 32;
}

// Explorer を Inbox のまま開いていても、移動・生成・削除をその場で反映させる。
static void NotifyExplorerDirectory(const char *directory)
{
    wchar_t wideDirectory[1200];
    if (ToAbsoluteWide(directory, wideDirectory, 1200))
        SHChangeNotify(RPG_SHCNE_UPDATEDIR, RPG_SHCNF_PATHW, wideDirectory, NULL);
}

static void NotifyExplorerParentDirectory(const char *path)
{
    char parent[1200];
    char *separator;
    if (path == NULL || snprintf(parent, sizeof(parent), "%s", path) <= 0) return;
    separator = strrchr(parent, '\\');
    if (separator == NULL) separator = strrchr(parent, '/');
    if (separator == NULL) return;
    *separator = '\0';
    NotifyExplorerDirectory(parent);
}

static void NotifyExplorerFolderMoved(const char *source, const char *destination)
{
    wchar_t wideSource[1200], wideDestination[1200];
    if (ToAbsoluteWide(source, wideSource, 1200) && ToAbsoluteWide(destination, wideDestination, 1200))
        SHChangeNotify(RPG_SHCNE_RENAMEFOLDER, RPG_SHCNF_PATHW, wideSource, wideDestination);
    NotifyExplorerParentDirectory(source);
    NotifyExplorerParentDirectory(destination);
}

static bool CreateFolderUtf8(const char *path)
{
    wchar_t wide[1200];
    if (!ToWide(path, wide, 1200)) return false;
    if (CreateDirectoryW(wide, NULL) != 0) {
        if (ToAbsoluteWide(path, wide, 1200))
            SHChangeNotify(RPG_SHCNE_MKDIR, RPG_SHCNF_PATHW, wide, NULL);
        NotifyExplorerParentDirectory(path);
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
static bool GetObjectsPath(char *path, size_t size) { return GetStagePath("Objects", path, size); }
static bool GetDropsPath(char *path, size_t size) { return GetStagePath("Drops", path, size); }
static bool GetInboxPath(char *path, size_t size)
{ return snprintf(path, size, "%s../assets/Settings/Zipper/Inbox", GetApplicationDirectory()) > 0; }

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
    return snprintf(name, size, "block_%03d_%06d_r%02d_c%03d", blockType, identity,
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
    char name[256];
    return BlockName(folder, blockType, name, sizeof(name)) && ObjectPath(name, inInbox, path, size);
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
        if (ToAbsoluteWide(directory, wideChild, 1200))
            SHChangeNotify(RPG_SHCNE_RMDIR, RPG_SHCNF_PATHW, wideChild, NULL);
        NotifyExplorerParentDirectory(directory);
    }
}

static bool MoveDirectory(const char *source, const char *destination)
{
    wchar_t wideSource[1200], wideDestination[1200];
    if (!FolderExistsUtf8(source) || !ToWide(source, wideSource, 1200) || !ToWide(destination, wideDestination, 1200)) return false;
    RemoveTree(destination);
    if (MoveFileExW(wideSource, wideDestination, MOVEFILE_WRITE_THROUGH) == 0) return false;
    NotifyExplorerFolderMoved(source, destination);
    return true;
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
    return fclose(file) == 0;
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
    return CreateFolderUtf8(objects) && WriteInfo(source, "data_shot", 0, shot->folderSerial, shot->position) &&
           CopyTree(parentSource, parent);
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
    return CopyFileW(wideSource, wideDestination, FALSE) != 0;
#else
    (void)sourcePath; return false;
#endif
}

void RpgObjectFolder_PrepareZipperAnimationCommand(void)
{
#ifdef _WIN32
    char inbox[1200], command[1200]; wchar_t wideCommand[1200]; FILE *file;
    if (!GetInboxPath(inbox, sizeof(inbox)) || !CreateFolderUtf8(inbox) ||
        snprintf(command, sizeof(command), "%s\\zipper_animate.cmd", inbox) <= 0 || !ToWide(command, wideCommand, 1200)) return;
    file = _wfopen(wideCommand, L"wb");
    if (file != NULL) { fputs("@echo off\r\necho animate> \"%~dp0zipper_animate.request\"\r\n", file); fclose(file); }
#endif
}

bool RpgObjectFolder_ConsumeZipperAnimationRequest(void)
{
#ifdef _WIN32
    char inbox[1200], request[1200]; wchar_t wideRequest[1200];
    if (!GetInboxPath(inbox, sizeof(inbox)) || snprintf(request, sizeof(request), "%s\\zipper_animate.request", inbox) <= 0 ||
        !ToWide(request, wideRequest, 1200) || GetFileAttributesW(wideRequest) == INVALID_FILE_ATTRIBUTES) return false;
    DeleteFileW(wideRequest); return true;
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

bool RpgObjectFolder_MoveDataShotToZipper(const RpgDataShot *shot)
{
#ifdef _WIN32
    char source[1200], inbox[1200], parent[1200];
    return EnsureDataShot(shot, NULL) && DataShotPath(shot, false, source, sizeof(source)) &&
           DataShotPath(shot, true, inbox, sizeof(inbox)) && GetInboxPath(parent, sizeof(parent)) && CreateFolderUtf8(parent) &&
           (FolderExistsUtf8(inbox) || MoveDirectory(source, inbox));
#else
    (void)shot; return false;
#endif
}

bool RpgObjectFolder_MoveBlockToZipper(const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    return blockType != 0 && MaterializeBlockInInbox(folder, blockType);
#else
    (void)folder; (void)blockType; return false;
#endif
}

bool RpgObjectFolder_ReturnAttachmentFromZipper(const RpgAttachment *attachment)
{
#ifdef _WIN32
    char source[1200], inbox[1200], objects[1200];
    return AttachmentPath(attachment, false, source, sizeof(source)) && AttachmentPath(attachment, true, inbox, sizeof(inbox)) &&
           GetObjectsPath(objects, sizeof(objects)) && CreateFolderUtf8(objects) && (!FolderExistsUtf8(inbox) || MoveDirectory(inbox, source));
#else
    (void)attachment; return false;
#endif
}

bool RpgObjectFolder_ReturnDataShotFromZipper(const RpgDataShot *shot)
{
#ifdef _WIN32
    char source[1200], inbox[1200], objects[1200];
    return DataShotPath(shot, false, source, sizeof(source)) && DataShotPath(shot, true, inbox, sizeof(inbox)) &&
           GetObjectsPath(objects, sizeof(objects)) && CreateFolderUtf8(objects) && (!FolderExistsUtf8(inbox) || MoveDirectory(inbox, source));
#else
    (void)shot; return false;
#endif
}

bool RpgObjectFolder_ReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType)
{
#ifdef _WIN32
    char source[1200], inbox[1200], objects[1200];
    if (!BlockPath(folder, blockType, false, source, sizeof(source)) ||
        !BlockPath(folder, blockType, true, inbox, sizeof(inbox)) || !FolderExistsUtf8(inbox)) return false;
    // 編集がなければ仮想ブロックへ戻し、外部ファイルが追加されたときだけ実フォルダを残す。
    if (!HasExternalFiles(inbox)) { RemoveTree(inbox); return true; }
    return GetObjectsPath(objects, sizeof(objects)) && CreateFolderUtf8(objects) && MoveDirectory(inbox, source);
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
        if (shot->isPreview) continue;
        int serial = shot->active ? shot->folderSerial : 0;
        if (serial == dataShotFolderSerials[index]) {
            if (serial > 0 && EnsureDataShot(shot, attachments)) UpdateDataShotProperties(index, shot, attachments);
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
        if (serial > 0 && EnsureDataShot(shot, attachments)) UpdateDataShotProperties(index, shot, attachments);
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
    return blockType != 0 && ((BlockPath(folder, blockType, false, source, sizeof(source)) && HasExternalFiles(source)) ||
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

void RpgObjectFolders_ClearSessionStorage(void)
{
#ifdef _WIN32
    char objects[1200], drops[1200], inbox[1200];
    memset(dataShotFolderSerials, 0, sizeof(dataShotFolderSerials)); nextFileDropId = 1;
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) CloseDataShotFolderWatcher(index);
    if (GetObjectsPath(objects, sizeof(objects))) RemoveTree(objects);
    if (GetDropsPath(drops, sizeof(drops))) RemoveTree(drops);
    if (GetInboxPath(inbox, sizeof(inbox))) RemoveTree(inbox);
#endif
}
// 役割: オブジェクト所有フォルダ、Zipper への移動、外部ファイルの引き継ぎを管理する。
