// 依存する自プロジェクト内ファイル: rpg_explorer_filesystem.h。
// 役割: Zipper の実フォルダを安全に操作し、UI が描画する一覧データを提供する。
#include "rpg_explorer_filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static bool Utf8ToWide(const char *text, wchar_t *wide, int count)
{
    return text != NULL && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count) > 0;
}

static bool WideToUtf8(const wchar_t *wide, char *text, int count)
{
    return wide != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, count, NULL, NULL) > 0;
}

static bool NormalizePath(const char *path, char *normalized, size_t size)
{
    wchar_t wide[RPG_EXPLORER_PATH_LENGTH], full[RPG_EXPLORER_PATH_LENGTH];
    DWORD length;
    if (!Utf8ToWide(path, wide, RPG_EXPLORER_PATH_LENGTH)) return false;
    length = GetFullPathNameW(wide, RPG_EXPLORER_PATH_LENGTH, full, NULL);
    if (length == 0 || length >= RPG_EXPLORER_PATH_LENGTH || !WideToUtf8(full, normalized, (int)size)) return false;
    size_t used = strlen(normalized);
    while (used > 3 && (normalized[used - 1] == '\\' || normalized[used - 1] == '/')) normalized[--used] = '\0';
    return true;
}

static bool DirectoryExists(const char *path)
{
    wchar_t wide[RPG_EXPLORER_PATH_LENGTH];
    DWORD attributes;
    if (!Utf8ToWide(path, wide, RPG_EXPLORER_PATH_LENGTH)) return false;
    attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int CompareEntries(const void *left, const void *right)
{
    const RpgExplorerEntry *a = left, *b = right;
    if (a->isDirectory != b->isDirectory) return a->isDirectory ? -1 : 1;
    return _stricmp(a->name, b->name);
}

static void BuildTree(RpgExplorerFilesystem *filesystem, const char *directory, int depth)
{
    wchar_t wideDirectory[RPG_EXPLORER_PATH_LENGTH], search[RPG_EXPLORER_PATH_LENGTH];
    char parent[RPG_EXPLORER_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (depth >= 8 || filesystem->treeNodeCount >= RPG_EXPLORER_MAX_TREE_NODES ||
        !Utf8ToWide(directory, wideDirectory, RPG_EXPLORER_PATH_LENGTH) ||
        swprintf(search, RPG_EXPLORER_PATH_LENGTH, L"%ls\\*", wideDirectory) < 0 ||
        snprintf(parent, sizeof(parent), "%s", directory) <= 0) return;
    handle = FindFirstFileW(search, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        RpgExplorerTreeNode *node;
        char name[RPG_EXPLORER_NAME_LENGTH], child[RPG_EXPLORER_PATH_LENGTH];
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0 || (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 ||
            filesystem->treeNodeCount >= RPG_EXPLORER_MAX_TREE_NODES) continue;
        if (!WideToUtf8(data.cFileName, name, sizeof(name)) ||
            snprintf(child, sizeof(child), "%s\\%s", parent, name) <= 0) continue;
        node = &filesystem->treeNodes[filesystem->treeNodeCount++];
        memset(node, 0, sizeof(*node));
        snprintf(node->name, sizeof(node->name), "%s", name);
        snprintf(node->path, sizeof(node->path), "%s", child);
        node->depth = depth;
        BuildTree(filesystem, node->path, depth + 1);
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
}

bool RpgExplorerFilesystem_IsInsideRoot(const RpgExplorerFilesystem *filesystem, const char *path)
{
    char normalized[RPG_EXPLORER_PATH_LENGTH];
    size_t rootLength;
    if (filesystem == NULL || path == NULL || !NormalizePath(path, normalized, sizeof(normalized))) return false;
    rootLength = strlen(filesystem->rootPath);
    return _strnicmp(filesystem->rootPath, normalized, rootLength) == 0 &&
           (normalized[rootLength] == '\0' || normalized[rootLength] == '\\');
}

const char *RpgExplorerFilesystem_GetRelativePath(const RpgExplorerFilesystem *filesystem, const char *path)
{
    size_t rootLength;
    if (filesystem == NULL || path == NULL || !RpgExplorerFilesystem_IsInsideRoot(filesystem, path)) return "";
    rootLength = strlen(filesystem->rootPath);
    return path[rootLength] == '\\' ? path + rootLength + 1 : path + rootLength;
}

void RpgExplorerFilesystem_FormatDate(uint64_t fileTime, char *text, size_t textSize)
{
    FILETIME utc = { .dwLowDateTime = (DWORD)fileTime, .dwHighDateTime = (DWORD)(fileTime >> 32) }, local;
    SYSTEMTIME date;
    if (text == NULL || textSize == 0) return;
    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &date)) {
        snprintf(text, textSize, "%04u/%02u/%02u %02u:%02u", date.wYear, date.wMonth, date.wDay, date.wHour, date.wMinute);
        return;
    }
    snprintf(text, textSize, "-");
}

bool RpgExplorerFilesystem_Refresh(RpgExplorerFilesystem *filesystem)
{
    wchar_t search[RPG_EXPLORER_PATH_LENGTH], wideCurrent[RPG_EXPLORER_PATH_LENGTH];
    char current[RPG_EXPLORER_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (filesystem == NULL || !DirectoryExists(filesystem->currentPath) ||
        !Utf8ToWide(filesystem->currentPath, wideCurrent, RPG_EXPLORER_PATH_LENGTH) ||
        swprintf(search, RPG_EXPLORER_PATH_LENGTH, L"%ls\\*", wideCurrent) < 0 ||
        snprintf(current, sizeof(current), "%s", filesystem->currentPath) <= 0) return false;
    filesystem->entryCount = 0;
    handle = FindFirstFileW(search, &data);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        RpgExplorerEntry *entry;
        char name[RPG_EXPLORER_NAME_LENGTH], fullPath[RPG_EXPLORER_PATH_LENGTH];
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 || filesystem->entryCount >= RPG_EXPLORER_MAX_ENTRIES) continue;
        if (!WideToUtf8(data.cFileName, name, sizeof(name)) ||
            snprintf(fullPath, sizeof(fullPath), "%s\\%s", current, name) <= 0) continue;
        entry = &filesystem->entries[filesystem->entryCount];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        snprintf(entry->path, sizeof(entry->path), "%s", fullPath);
        entry->isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry->iconSlot = -1;
        entry->size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        entry->lastWrite = ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
        snprintf(entry->typeName, sizeof(entry->typeName), "%s", entry->isDirectory ? "File folder" : "File");
        filesystem->entryCount++;
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    qsort(filesystem->entries, (size_t)filesystem->entryCount, sizeof(filesystem->entries[0]), CompareEntries);
    filesystem->treeNodeCount = 0;
    BuildTree(filesystem, filesystem->rootPath, 1);
    return true;
}

bool RpgExplorerFilesystem_Initialize(RpgExplorerFilesystem *filesystem, const char *rootPath)
{
    wchar_t wideRoot[RPG_EXPLORER_PATH_LENGTH];
    if (filesystem == NULL || rootPath == NULL || !NormalizePath(rootPath, filesystem->rootPath, sizeof(filesystem->rootPath))) return false;
    if (!DirectoryExists(filesystem->rootPath) &&
        (!Utf8ToWide(filesystem->rootPath, wideRoot, RPG_EXPLORER_PATH_LENGTH) ||
         (!CreateDirectoryW(wideRoot, NULL) && GetLastError() != ERROR_ALREADY_EXISTS))) return false;
    snprintf(filesystem->currentPath, sizeof(filesystem->currentPath), "%s", filesystem->rootPath);
    return RpgExplorerFilesystem_Refresh(filesystem);
}

bool RpgExplorerFilesystem_NavigateTo(RpgExplorerFilesystem *filesystem, const char *directory)
{
    char normalized[RPG_EXPLORER_PATH_LENGTH];
    if (filesystem == NULL || !NormalizePath(directory, normalized, sizeof(normalized)) ||
        !RpgExplorerFilesystem_IsInsideRoot(filesystem, normalized) || !DirectoryExists(normalized)) return false;
    snprintf(filesystem->currentPath, sizeof(filesystem->currentPath), "%s", normalized);
    return RpgExplorerFilesystem_Refresh(filesystem);
}

bool RpgExplorerFilesystem_NavigateUp(RpgExplorerFilesystem *filesystem)
{
    char parent[RPG_EXPLORER_PATH_LENGTH], *slash;
    if (filesystem == NULL || _stricmp(filesystem->currentPath, filesystem->rootPath) == 0) return false;
    snprintf(parent, sizeof(parent), "%s", filesystem->currentPath);
    slash = strrchr(parent, '\\');
    if (slash == NULL) return false;
    *slash = '\0';
    return RpgExplorerFilesystem_NavigateTo(filesystem, parent);
}

bool RpgExplorerFilesystem_OpenEntry(RpgExplorerFilesystem *filesystem, int entryIndex)
{
    if (filesystem == NULL || entryIndex < 0 || entryIndex >= filesystem->entryCount ||
        !filesystem->entries[entryIndex].isDirectory) return false;
    return RpgExplorerFilesystem_NavigateTo(filesystem, filesystem->entries[entryIndex].path);
}

bool RpgExplorerFilesystem_MoveEntryToDirectory(RpgExplorerFilesystem *filesystem, int sourceIndex,
                                                 const char *destinationDirectory)
{
    const RpgExplorerEntry *source;
    char destination[RPG_EXPLORER_PATH_LENGTH];
    wchar_t wideSource[RPG_EXPLORER_PATH_LENGTH], wideDestination[RPG_EXPLORER_PATH_LENGTH];
    if (filesystem == NULL || sourceIndex < 0 || sourceIndex >= filesystem->entryCount ||
        !RpgExplorerFilesystem_IsInsideRoot(filesystem, destinationDirectory) || !DirectoryExists(destinationDirectory)) return false;
    source = &filesystem->entries[sourceIndex];
    if (source->isDirectory && RpgExplorerFilesystem_IsInsideRoot(filesystem, destinationDirectory) &&
        _strnicmp(destinationDirectory, source->path, strlen(source->path)) == 0) return false;
    if (snprintf(destination, sizeof(destination), "%s\\%s", destinationDirectory, source->name) <= 0 ||
        _stricmp(source->path, destination) == 0 || !Utf8ToWide(source->path, wideSource, RPG_EXPLORER_PATH_LENGTH) ||
        !Utf8ToWide(destination, wideDestination, RPG_EXPLORER_PATH_LENGTH) || GetFileAttributesW(wideDestination) != INVALID_FILE_ATTRIBUTES) return false;
    if (!MoveFileW(wideSource, wideDestination)) return false;
    return RpgExplorerFilesystem_Refresh(filesystem);
}
#else
bool RpgExplorerFilesystem_Initialize(RpgExplorerFilesystem *filesystem, const char *rootPath)
{ (void)filesystem; (void)rootPath; return false; }
bool RpgExplorerFilesystem_Refresh(RpgExplorerFilesystem *filesystem) { (void)filesystem; return false; }
bool RpgExplorerFilesystem_NavigateTo(RpgExplorerFilesystem *filesystem, const char *directory)
{ (void)filesystem; (void)directory; return false; }
bool RpgExplorerFilesystem_NavigateUp(RpgExplorerFilesystem *filesystem) { (void)filesystem; return false; }
bool RpgExplorerFilesystem_OpenEntry(RpgExplorerFilesystem *filesystem, int entryIndex)
{ (void)filesystem; (void)entryIndex; return false; }
bool RpgExplorerFilesystem_MoveEntryToDirectory(RpgExplorerFilesystem *filesystem, int sourceIndex, const char *destinationDirectory)
{ (void)filesystem; (void)sourceIndex; (void)destinationDirectory; return false; }
bool RpgExplorerFilesystem_IsInsideRoot(const RpgExplorerFilesystem *filesystem, const char *path)
{ (void)filesystem; (void)path; return false; }
const char *RpgExplorerFilesystem_GetRelativePath(const RpgExplorerFilesystem *filesystem, const char *path)
{ (void)filesystem; (void)path; return ""; }
void RpgExplorerFilesystem_FormatDate(uint64_t fileTime, char *text, size_t textSize)
{ (void)fileTime; if (text != NULL && textSize > 0) snprintf(text, textSize, "-"); }
#endif
