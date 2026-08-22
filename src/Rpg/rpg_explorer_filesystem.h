// 依存する自プロジェクト内ファイル: なし。
// 役割: Zipper 直下に限定した実ファイルの列挙・移動を、画面表示から独立して管理する。
#ifndef RPG_EXPLORER_FILESYSTEM_H
#define RPG_EXPLORER_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RPG_EXPLORER_PATH_LENGTH = 1024, RPG_EXPLORER_NAME_LENGTH = 260, RPG_EXPLORER_TYPE_LENGTH = 96,
       RPG_EXPLORER_MAX_ENTRIES = 256, RPG_EXPLORER_MAX_TREE_NODES = 256 };

typedef struct RpgExplorerEntry {
    char path[RPG_EXPLORER_PATH_LENGTH];
    char name[RPG_EXPLORER_NAME_LENGTH];
    char typeName[RPG_EXPLORER_TYPE_LENGTH];
    uint64_t size;
    uint64_t lastWrite;
    bool isDirectory;
    int iconSlot;
} RpgExplorerEntry;

typedef struct RpgExplorerTreeNode {
    char path[RPG_EXPLORER_PATH_LENGTH];
    char name[RPG_EXPLORER_NAME_LENGTH];
    int depth;
} RpgExplorerTreeNode;

typedef struct RpgExplorerFilesystem {
    char rootPath[RPG_EXPLORER_PATH_LENGTH];
    char currentPath[RPG_EXPLORER_PATH_LENGTH];
    RpgExplorerEntry entries[RPG_EXPLORER_MAX_ENTRIES];
    int entryCount;
    RpgExplorerTreeNode treeNodes[RPG_EXPLORER_MAX_TREE_NODES];
    int treeNodeCount;
} RpgExplorerFilesystem;

bool RpgExplorerFilesystem_Initialize(RpgExplorerFilesystem *filesystem, const char *rootPath);
bool RpgExplorerFilesystem_Refresh(RpgExplorerFilesystem *filesystem);
bool RpgExplorerFilesystem_NavigateTo(RpgExplorerFilesystem *filesystem, const char *directory);
bool RpgExplorerFilesystem_NavigateUp(RpgExplorerFilesystem *filesystem);
bool RpgExplorerFilesystem_OpenEntry(RpgExplorerFilesystem *filesystem, int entryIndex);
bool RpgExplorerFilesystem_MoveEntryToDirectory(RpgExplorerFilesystem *filesystem, int sourceIndex,
                                                 const char *destinationDirectory);
bool RpgExplorerFilesystem_IsInsideRoot(const RpgExplorerFilesystem *filesystem, const char *path);
const char *RpgExplorerFilesystem_GetRelativePath(const RpgExplorerFilesystem *filesystem, const char *path);
void RpgExplorerFilesystem_FormatDate(uint64_t fileTime, char *text, size_t textSize);

#endif
