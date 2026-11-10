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

/* Static Drop is an authoring operation, while eat is runtime-only.  Drop
   queues a check after removing the source folder; the normal editor tick
   then writes the durable blank/error cell state. */
static struct {
    int stageNumber;
    bool pending[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    /* A movable block has no terrain-cell representation while dropped,
       just as it has none after Zipper eats it.  Every other Drop source,
       including an air-cell object folder, leaves a durable missing-cell
       marker when its source folder disappears.  Keep this intent at the
       time Drop starts; folder absence alone cannot distinguish the cases. */
    bool missingCellOnRemoval[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
} folderAvailability = { 0 };

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

static bool GetObjectFolderPath(int stageNumber, const RpgStage *stage, int row, int column,
                                char *path, int pathSize)
{
    char root[RPG_STAGE_PATH_LENGTH], name[128];
    return stageNumber > 0 && stage != NULL && path != NULL && pathSize > 0 &&
           GetObjectDefinitionsRoot(stageNumber, root, (int)sizeof(root)) &&
           BuildObjectFolderName(stage, row, column, name, (int)sizeof(name)) &&
           snprintf(path, (size_t)pathSize, "%s\\%s", root, name) > 0;
}

static void QueueFolderAvailabilityCheck(int stageNumber, int row, int column,
                                         bool missingCellOnRemoval)
{
    if (stageNumber <= 0 || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS) return;
    if (folderAvailability.stageNumber != stageNumber) {
        memset(folderAvailability.pending, 0, sizeof(folderAvailability.pending));
        memset(folderAvailability.missingCellOnRemoval, 0,
               sizeof(folderAvailability.missingCellOnRemoval));
        folderAvailability.stageNumber = stageNumber;
    }
    folderAvailability.pending[row][column] = true;
    folderAvailability.missingCellOnRemoval[row][column] = missingCellOnRemoval;
}

static bool IsObjectFolderPresent(int stageNumber, const RpgStage *stage, int row, int column)
{
    char path[RPG_STAGE_PATH_LENGTH];
    wchar_t widePath[RPG_STAGE_PATH_LENGTH];
    DWORD attributes;
    return GetObjectFolderPath(stageNumber, stage, row, column, path, (int)sizeof(path)) &&
           ToWide(path, widePath, RPG_STAGE_PATH_LENGTH) &&
           (attributes = GetFileAttributesW(widePath)) != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool ReconcileFolderAvailability(RpgStage *stage, int stageNumber, int row, int column,
                                        bool missingCellOnRemoval)
{
    int currentType;
    bool folderPresent;
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS) return false;
    currentType = stage->blocks[row][column];
    if (RpgBlockInventory_IsReferenceObject(currentType)) return false;
    folderPresent = IsObjectFolderPresent(stageNumber, stage, row, column);
    if (missingCellOnRemoval && !folderPresent && !RpgStage_IsCellError(stage, row, column)) {
        /* Static Drop has no automatic restore payload.  Its blank CSV field
           is the durable red-error state, not a hidden copy of the block. */
        RpgStage_SetCellError(stage, row, column, 0);
        return true;
    }
    return false;
}

static bool IsMovableBlockDrop(int blockType)
{
    return RpgBlockInventory_IsMetalBlock(blockType) ||
           RpgBlockInventory_IsPushBlock(blockType);
}

/* This value travels with object_info.txt when Drop splits the parent
   directory into movable FILE objects.  It is deliberately not a second
   file: putting the origin beside object_info keeps reassembly atomic and
   prevents an air-origin folder from being mistaken for an ordinary block. */
static bool WriteDropOrigin(const char *folder, int blockType)
{
    char path[RPG_STAGE_PATH_LENGTH];
    FILE *file;
    const char *kind = IsMovableBlockDrop(blockType) ? "movable" :
                       blockType == 0 ? "air" : "block";
    if (folder == NULL ||
        snprintf(path, sizeof(path), "%s\\object_info.txt", folder) <= 0 ||
        (file = fopen(path, "ab")) == NULL) return false;
    fprintf(file, "drop_origin=%s\n", kind);
    return fclose(file) == 0;
}

/* The normal Play/preview folder writer targets its disposable runtime
   build.  Static Drop instead consumes Settings/Stage/.../object_defs, so it
   must materialise the current attachment children in that exact source
   folder before enumerating it. */
static void RemoveOwnedMetadataFiles(const char *folder)
{
    char search[RPG_STAGE_PATH_LENGTH];
    wchar_t wideSearch[RPG_STAGE_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    if (folder == NULL || snprintf(search, sizeof(search), "%s\\*", folder) <= 0 ||
        !ToWide(search, wideSearch, RPG_STAGE_PATH_LENGTH)) return;
    handle = FindFirstFileW(wideSearch, &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        char name[512], path[RPG_STAGE_PATH_LENGTH];
        wchar_t widePath[RPG_STAGE_PATH_LENGTH];
        const bool owned = ToUtf8(data.cFileName, name, (int)sizeof(name)) &&
            (strncmp(name, "attachment_", 11) == 0 ||
             strncmp(name, "receiver_", 9) == 0 ||
             strncmp(name, "wire_receiver_", 14) == 0);
        if (!owned || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            snprintf(path, sizeof(path), "%s\\%s", folder, name) <= 0 ||
            !ToWide(path, widePath, RPG_STAGE_PATH_LENGTH)) continue;
        (void)DeleteFileW(widePath);
    } while (FindNextFileW(handle, &data) != 0);
    FindClose(handle);
}

static bool WriteDropAttachmentMetadata(const char *folder, const RpgAttachment *attachment,
                                        RpgGridCell owner, const RpgAttachments *attachments)
{
    char path[RPG_STAGE_PATH_LENGTH];
    FILE *file;
    if (folder == NULL || attachment == NULL || attachments == NULL ||
        snprintf(path, sizeof(path), "%s\\attachment_%06d_t%03d.meta", folder,
                 attachment->folderId, attachment->type) <= 0 ||
        (file = fopen(path, "wb")) == NULL) return false;
    fprintf(file, "kind=attachment\ntype=%d\nid=%d\nowner_row=%d\nowner_column=%d\n"
                  "mount_row=%d\nmount_column=%d\nside=%d\n",
            attachment->type, attachment->folderId, owner.row, owner.column,
            attachment->cell.row, attachment->cell.column, attachment->side);
    if (fclose(file) != 0) return false;

    /* Receiver paths are owned by the same parent.  Their runtime wire is
       derived from the attachment route list, but Drop must expose it as a
       separate child file alongside the receiver's attachment metadata. */
    if (RpgAttachments_IsReceiver(attachment)) {
        for (int index = 0; index < attachments->routeCount; index++) {
            const RpgAttachmentRoute *route = &attachments->routes[index];
            if (route->kind != RPG_WIRE_KIND_ELECTRIC ||
                route->ownerAttachmentId != attachment->folderId) continue;
            if (snprintf(path, sizeof(path), "%s\\wire_receiver_r%03d_c%03d.meta",
                         folder, attachment->cell.row, attachment->cell.column) <= 0 ||
                (file = fopen(path, "wb")) == NULL) return false;
            fprintf(file, "kind=receiver_wire\nowner_row=%d\nowner_column=%d\nside=%d\npath_count=%d\n",
                    owner.row, owner.column, attachment->side, route->cells.cellCount);
            for (int point = 0; point < route->cells.cellCount; point++)
                fprintf(file, "path=%d,%d\n", route->cells.cells[point].row,
                        route->cells.cells[point].column);
            if (fclose(file) != 0) return false;
            break;
        }
    }
    return true;
}

static bool MaterializeDropOwnedMetadata(const char *folder, RpgGridCell owner,
                                         const RpgAttachments *attachments)
{
    if (folder == NULL) return false;
    RemoveOwnedMetadataFiles(folder);
    if (attachments == NULL) return true;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        RpgGridCell attachmentOwner;
        if (RpgAttachments_IsRuntimeUnavailable(attachment) ||
            !RpgAttachments_GetOwnerBlockCell(attachment, &attachmentOwner) ||
            attachmentOwner.row != owner.row || attachmentOwner.column != owner.column) continue;
        if (!WriteDropAttachmentMetadata(folder, attachment, owner, attachments)) return false;
    }
    return true;
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
    /* This is a terrain-cell folder, not a movable object's world-space
       record.  Persist its grid identity explicitly so transfer code never
       has to infer a cell from x/y. */
    fprintf(file, "type=%d\ncell_row=%d\ncell_column=%d\nx=%.1f\ny=%.1f\n",
            blockType, row, column,
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

bool RpgStaticObjectDrop_Expand(RpgStage *stage, int stageNumber, int row, int column,
                                const RpgAttachments *attachments, int *count)
{
    char folder[RPG_STAGE_PATH_LENGTH], files[RPG_STATIC_DROP_MAX_FILES][RPG_STAGE_PATH_LENGTH];
    char droppedFolder[RPG_STAGE_PATH_LENGTH], droppedFolderName[64];
    bool used[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS] = { false };
    wchar_t wideFolder[RPG_STAGE_PATH_LENGTH], search[RPG_STAGE_PATH_LENGTH];
    WIN32_FIND_DATAW data;
    HANDLE handle;
    int blockType, fileCount = 0;
    int folderRow, folderColumn;
    if (count != NULL) *count = 0;
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS) return false;
    blockType = stage->blocks[row][column];
    /* An attachment is stored in the same parent folder as its terrain
       block, so a normal parent-cell Drop already releases attachment meta
       files as ordinary FILE objects.  Air/error cells can also be targets:
       do not manufacture a new object folder for them, but release an
       existing folder left at that authored cell. */
    if ((blockType != 0 &&
         (RpgStage_IsCellError(stage, row, column) ||
          RpgBlockInventory_IsReferenceObject(blockType) ||
          !RpgStaticObjectDrop_EnsureFolder(stageNumber, stage, row, column, blockType,
                                            folder, (int)sizeof(folder)))) ||
        (blockType == 0 &&
         (!GetObjectFolderPath(stageNumber, stage, row, column, folder, (int)sizeof(folder)) ||
          !IsObjectFolderPresent(stageNumber, stage, row, column))) ||
        !MaterializeDropOwnedMetadata(folder, (RpgGridCell){ row, column }, attachments) ||
        !WriteDropOrigin(folder, blockType) ||
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
    /* Dropping exposes both layers of an object folder:
       - each existing child file becomes a FILE movable, and
       - the now-empty object folder itself becomes a FOLDER movable.
       Reserve every target before mutating storage, so a full map cannot
       leave a partially-expanded object. */
    if (stage->referenceObjects.count + fileCount + 1 > RPG_REFERENCE_OBJECT_MAX_COUNT ||
        !FindAir(stage, row, column, used, &folderRow, &folderColumn)) return false;
    used[folderRow][folderColumn] = true;
    for (int index = 0; index < fileCount; index++) {
        int targetRow, targetColumn;
        if (!FindAir(stage, row, column, used, &targetRow, &targetColumn)) return false;
        used[targetRow][targetColumn] = true;
    }
    if (snprintf(droppedFolderName, sizeof(droppedFolderName), "Dropped_%04d",
                 stage->referenceObjects.nextId) <= 0 ||
        !RpgStageStorage_CreateBuildFolder(stageNumber, droppedFolderName, droppedFolder,
                                           (int)sizeof(droppedFolder))) return false;
    memset(used, 0, sizeof(used));
    used[folderRow][folderColumn] = true;
    for (int index = 0; index < fileCount; index++) {
        char copied[RPG_STAGE_PATH_LENGTH]; wchar_t wideFile[RPG_STAGE_PATH_LENGTH]; int targetRow, targetColumn;
        int objectId = stage->referenceObjects.nextId;
        if (!FindAir(stage, row, column, used, &targetRow, &targetColumn) ||
            !RpgStageStorage_CopyReferenceFileToBuild(stageNumber, targetRow, targetColumn, files[index], copied, (int)sizeof(copied)) ||
            /* Drop consumes the original object file.  Preserve an
               ID-addressed static source first; otherwise the movable FILE
               could only be repaired by selecting it in the inspector. */
            !RpgStageStorage_CopyReferenceFileToStaticSource(stageNumber, objectId, copied) ||
            !ToWide(files[index], wideFile, RPG_STAGE_PATH_LENGTH) || DeleteFileW(wideFile) == 0) return false;
        used[targetRow][targetColumn] = true;
        if (!RpgReferenceObjects_Add(&stage->referenceObjects, RPG_REFERENCE_OBJECT_FILE,
                                     RpgStage_GetWorldPositionForCell(stage, targetRow, targetColumn),
                                     copied, objectId)) return false;
    }
    { char info[RPG_STAGE_PATH_LENGTH]; wchar_t wideInfo[RPG_STAGE_PATH_LENGTH];
      if (snprintf(info, sizeof(info), "%s\\object_info.txt", folder) > 0 && ToWide(info, wideInfo, RPG_STAGE_PATH_LENGTH)) DeleteFileW(wideInfo); }
    if (ToWide(folder, wideFolder, RPG_STAGE_PATH_LENGTH)) RemoveDirectoryW(wideFolder);
    if (!RpgReferenceObjects_Add(&stage->referenceObjects, RPG_REFERENCE_OBJECT_FOLDER,
                                 RpgStage_GetWorldPositionForCell(stage, folderRow, folderColumn),
                                 droppedFolder, 0)) return false;
    if (IsMovableBlockDrop(blockType)) {
        /* Metal/push blocks are materialised as RpgMagnetMetal at runtime,
           where eat removes their terrain cell rather than leaving an error
           marker.  Make editor Drop match that same state now: the released
           FILE/FOLDER objects remain, but their source cell is simply air. */
        RpgStage_SetCellBlock(stage, row, column, 0);
    } else {
        /* The normal availability pass owns the resulting error-cell
           transition.  This intentionally includes an authored air cell
           with a source object folder: dropping that folder means the
           authored object is missing and must remain visible as red. */
        QueueFolderAvailabilityCheck(stageNumber, row, column, true);
    }
    if (count != NULL) *count = fileCount + 1;
    return true;
}


bool RpgStaticObjectDrop_UpdateFolderAvailability(
    int stageNumber, RpgStage *stage,
    bool changedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS])
{
    bool changed = false;
    if (stage == NULL || stageNumber <= 0) return false;
    if (changedCells != NULL) memset(changedCells, 0, sizeof(bool) * RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS);
    if (folderAvailability.stageNumber != stageNumber) {
        memset(folderAvailability.pending, 0, sizeof(folderAvailability.pending));
        memset(folderAvailability.missingCellOnRemoval, 0,
               sizeof(folderAvailability.missingCellOnRemoval));
        folderAvailability.stageNumber = stageNumber;
        return false;
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!folderAvailability.pending[row][column]) continue;
        folderAvailability.pending[row][column] = false;
        if (ReconcileFolderAvailability(stage, stageNumber, row, column,
                                        folderAvailability.missingCellOnRemoval[row][column])) {
            if (changedCells != NULL) changedCells[row][column] = true;
            changed = true;
        }
        folderAvailability.missingCellOnRemoval[row][column] = false;
    }
    return changed;
}
#else
bool RpgStaticObjectDrop_EnsureFolder(int a,const RpgStage *b,int c,int d,int e,char *f,int g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;return false;}
bool RpgStaticObjectDrop_MigrateLegacyFolders(int a,const RpgStage *b){(void)a;(void)b;return false;}
bool RpgStaticObjectDrop_Expand(RpgStage *a,int b,int c,int d,const RpgAttachments *e,int *f){(void)a;(void)b;(void)c;(void)d;(void)e;if(f)*f=0;return false;}
bool RpgStaticObjectDrop_UpdateFolderAvailability(int a,RpgStage *b,bool c[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS]){(void)a;(void)b;(void)c;return false;}
#endif
