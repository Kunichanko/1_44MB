#include "rpg_static_object_drop.h"
#include "rpg_block_inventory.h"
#include "rpg_stage_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>

enum { RPG_STATIC_DROP_MAX_FILES = RPG_REFERENCE_OBJECT_MAX_COUNT };

static bool ToWide(const char *text, wchar_t *wide, int count)
{ return text != NULL && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count) > 0; }
static bool ToUtf8(const wchar_t *wide, char *text, int count)
{ return wide != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, count, NULL, NULL) > 0; }

static bool GetObjectDefinitionsRoot(int stageNumber, char *root, int rootSize)
{
    wchar_t wideRoot[RPG_STAGE_PATH_LENGTH];
    return root != NULL && rootSize > 0 && stageNumber > 0 &&
           RpgStageStorage_EnsureStageDirectory(stageNumber) &&
           RpgStageStorage_GetFilePath(stageNumber, "blocks\\object_defs", root, rootSize) &&
           ToWide(root, wideRoot, RPG_STAGE_PATH_LENGTH) &&
           (CreateDirectoryW(wideRoot, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS);
}

/* Object folders are named from authored map coordinates, never from the
   internal storage slot.  Type belongs in object_info.txt, so renaming a
   block type does not create a second identity for the same cell. */
static bool BuildObjectFolderName(const RpgStage *stage, int row, int column,
                                  char *name, int nameSize)
{
    int areaId;
    int localColumn;
    if (stage == NULL || name == NULL || nameSize <= 0 || row < 0 || row >= RPG_STAGE_ROWS ||
        column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    areaId = column / RPG_STAGE_COLUMNS;
    localColumn = column % RPG_STAGE_COLUMNS;
    if (!RpgStage_IsMapActive(stage, areaId)) return false;
    return snprintf(name, (size_t)nameSize, "area_x%02d_y%02d_cell_x%02d_y%02d",
                    stage->mapGridX[areaId], stage->mapGridY[areaId], localColumn, row) > 0;
}

static bool BuildLegacyObjectFolderPath(const char *root, int row, int column, int blockType,
                                        char *path, int pathSize)
{
    return root != NULL && path != NULL && pathSize > 0 &&
           snprintf(path, (size_t)pathSize, "%s\\cell_r%02d_c%03d_t%03d", root, row, column,
                    blockType) > 0;
}

bool RpgStaticObjectDrop_EnsureFolder(int stageNumber, const RpgStage *stage,
                                      int row, int column, int blockType,
                                      char *path, int pathSize)
{
    char root[RPG_STAGE_PATH_LENGTH], name[128], legacyPath[RPG_STAGE_PATH_LENGTH];
    char info[RPG_STAGE_PATH_LENGTH];
    wchar_t widePath[RPG_STAGE_PATH_LENGTH], wideLegacyPath[RPG_STAGE_PATH_LENGTH];
    DWORD legacyAttributes = INVALID_FILE_ATTRIBUTES;
    FILE *file;
    if (path == NULL || pathSize <= 0 || stageNumber <= 0 || row < 0 || row >= RPG_STAGE_ROWS ||
        column < 0 || column >= RPG_STAGE_WORLD_COLUMNS || blockType == 0 ||
        !GetObjectDefinitionsRoot(stageNumber, root, (int)sizeof(root)) ||
        !BuildObjectFolderName(stage, row, column, name, (int)sizeof(name)) ||
        snprintf(path, (size_t)pathSize, "%s\\%s", root, name) <= 0 ||
        !ToWide(path, widePath, RPG_STAGE_PATH_LENGTH)) return false;
    /* A one-time, non-destructive migration for folders created before the
       area/cell identity convention.  Never overwrite an already-migrated
       target; it is the canonical folder if both happen to exist. */
    if (GetFileAttributesW(widePath) == INVALID_FILE_ATTRIBUTES &&
        BuildLegacyObjectFolderPath(root, row, column, blockType, legacyPath,
                                    (int)sizeof(legacyPath)) &&
        ToWide(legacyPath, wideLegacyPath, RPG_STAGE_PATH_LENGTH))
        legacyAttributes = GetFileAttributesW(wideLegacyPath);
    if (GetFileAttributesW(widePath) == INVALID_FILE_ATTRIBUTES &&
        legacyAttributes != INVALID_FILE_ATTRIBUTES &&
        (legacyAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        (void)MoveFileW(wideLegacyPath, widePath);
    (void)CreateDirectoryW(widePath, NULL);
    if (snprintf(info, sizeof(info), "%s\\object_info.txt", path) <= 0 ||
        (file = fopen(info, "wb")) == NULL) return false;
    fprintf(file, "type=%d\nx=%.1f\ny=%.1f\n", blockType,
            (column + 0.5f) * RPG_STAGE_TILE_SIZE, (row + 0.5f) * RPG_STAGE_TILE_SIZE);
    fclose(file);
    return true;
}

bool RpgStaticObjectDrop_MigrateLegacyFolders(int stageNumber, const RpgStage *stage)
{
    char root[RPG_STAGE_PATH_LENGTH], search[RPG_STAGE_PATH_LENGTH];
    wchar_t wideSearch[RPG_STAGE_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    bool result = true;
    if (stage == NULL || !GetObjectDefinitionsRoot(stageNumber, root, (int)sizeof(root)) ||
        snprintf(search, sizeof(search), "%s\\cell_r*_c*_t*", root) <= 0 ||
        !ToWide(search, wideSearch, RPG_STAGE_PATH_LENGTH)) return false;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        char legacyName[256], ignored[RPG_STAGE_PATH_LENGTH];
        int row, column, blockType, used = 0;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            !ToUtf8(data.cFileName, legacyName, (int)sizeof(legacyName)) ||
            sscanf(legacyName, "cell_r%d_c%d_t%d%n", &row, &column, &blockType, &used) != 3 ||
            legacyName[used] != '\0') continue;
        if (!RpgStaticObjectDrop_EnsureFolder(stageNumber, stage, row, column, blockType,
                                              ignored, (int)sizeof(ignored))) result = false;
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    return result;
}

static bool FindAir(const RpgStage *stage, int sourceRow, int sourceColumn,
                    const bool used[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS], int *outRow, int *outColumn)
{
    int bestRow = -1, bestColumn = -1, bestDistance = 0;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        int distance;
        if (stage->blocks[row][column] != 0 || used[row][column]) continue;
        distance = abs(row - sourceRow) + abs(column - sourceColumn);
        if (bestRow >= 0 && distance >= bestDistance) continue;
        bestRow = row; bestColumn = column; bestDistance = distance;
    }
    if (bestRow < 0) return false;
    *outRow = bestRow; *outColumn = bestColumn;
    return true;
}

bool RpgStaticObjectDrop_Expand(RpgStage *stage, int stageNumber, int row, int column, int *count)
{
    char folder[RPG_STAGE_PATH_LENGTH], files[RPG_STATIC_DROP_MAX_FILES][RPG_STAGE_PATH_LENGTH];
    bool used[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { false };
    wchar_t wideFolder[RPG_STAGE_PATH_LENGTH], search[RPG_STAGE_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    int blockType, fileCount = 0;
    if (count != NULL) *count = 0;
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        (blockType = stage->blocks[row][column]) == 0 || blockType == RPG_BLOCK_BUILD_MISSING ||
        RpgBlockInventory_IsReferenceObject(blockType) ||
        !RpgStaticObjectDrop_EnsureFolder(stageNumber, stage, row, column, blockType,
                                          folder, (int)sizeof(folder)) ||
        !ToWide(folder, wideFolder, RPG_STAGE_PATH_LENGTH) ||
        swprintf(search, RPG_STAGE_PATH_LENGTH, L"%ls\\*", wideFolder) < 0) return false;
    handle = FindFirstFileW(search, &data);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        char name[512];
        /* object_info.txt is the cell object's actual serialized data.  It
           must be emitted too: older stages have no other child files, and
           excluding it made Drop fail for every pre-existing object. */
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            fileCount >= RPG_STATIC_DROP_MAX_FILES || !ToUtf8(data.cFileName, name, (int)sizeof(name)) ||
            snprintf(files[fileCount], sizeof(files[fileCount]), "%s\\%s", folder, name) <= 0) continue;
        fileCount++;
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
    if (fileCount == 0) return false;
    for (int index = 0; index < fileCount; index++) { int targetRow, targetColumn; if (!FindAir(stage, row, column, used, &targetRow, &targetColumn)) return false; used[targetRow][targetColumn] = true; }
    memset(used, 0, sizeof(used));
    for (int index = 0; index < fileCount; index++) {
        char copied[RPG_STAGE_PATH_LENGTH]; wchar_t wideFile[RPG_STAGE_PATH_LENGTH]; int targetRow, targetColumn;
        if (!FindAir(stage, row, column, used, &targetRow, &targetColumn) ||
            !RpgStageStorage_CopyReferenceFileToBuild(stageNumber, targetRow, targetColumn, files[index], copied, (int)sizeof(copied)) ||
            !ToWide(files[index], wideFile, RPG_STAGE_PATH_LENGTH) || DeleteFileW(wideFile) == 0) return false;
        used[targetRow][targetColumn] = true;
        (void)RpgReferenceObjects_Add(&stage->referenceObjects, RPG_REFERENCE_OBJECT_FILE,
                                      RpgStage_GetWorldPositionForCell(stage, targetRow, targetColumn),
                                      copied, 0);
    }
    { char info[RPG_STAGE_PATH_LENGTH]; wchar_t wideInfo[RPG_STAGE_PATH_LENGTH];
      if (snprintf(info, sizeof(info), "%s\\object_info.txt", folder) > 0 && ToWide(info, wideInfo, RPG_STAGE_PATH_LENGTH)) DeleteFileW(wideInfo); }
    if (ToWide(folder, wideFolder, RPG_STAGE_PATH_LENGTH)) RemoveDirectoryW(wideFolder);
    stage->missingBlockTypes[row][column] = blockType;
    stage->blocks[row][column] = RPG_BLOCK_BUILD_MISSING;
    if (count != NULL) *count = fileCount;
    return true;
}

bool RpgStaticObjectDrop_Restore(RpgStage *stage, int stageNumber, int row, int column)
{
    int type;
    char ignored[RPG_STAGE_PATH_LENGTH];
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        stage->blocks[row][column] != RPG_BLOCK_BUILD_MISSING || (type = stage->missingBlockTypes[row][column]) == 0 ||
        !RpgStaticObjectDrop_EnsureFolder(stageNumber, stage, row, column, type,
                                          ignored, (int)sizeof(ignored))) return false;
    stage->blocks[row][column] = type;
    stage->missingBlockTypes[row][column] = 0;
    return true;
}
#else
bool RpgStaticObjectDrop_EnsureFolder(int a,const RpgStage *b,int c,int d,int e,char *f,int g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;return false;}
bool RpgStaticObjectDrop_MigrateLegacyFolders(int a,const RpgStage *b){(void)a;(void)b;return false;}
bool RpgStaticObjectDrop_Expand(RpgStage *a,int b,int c,int d,int *e){(void)a;(void)b;(void)c;(void)d;if(e)*e=0;return false;}
bool RpgStaticObjectDrop_Restore(RpgStage *a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;return false;}
#endif
