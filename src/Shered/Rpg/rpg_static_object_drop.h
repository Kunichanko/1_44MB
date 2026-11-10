#ifndef RPG_STATIC_OBJECT_DROP_H
#define RPG_STATIC_OBJECT_DROP_H

#include "rpg_attachment.h"

/* Editor-only source folders.  They belong to Settings/Stage/StageN and are
   never confused with the dynamic editor/game build folders. */
bool RpgStaticObjectDrop_EnsureFolder(int stageNumber, const RpgStage *stage,
                                      int row, int column, int blockType,
                                      char *path, int pathSize);
/* Renames only the retired cell_r##_c###_t### directories.  The stage data
   remains untouched; the new name is derived from its current area layout. */
bool RpgStaticObjectDrop_MigrateLegacyFolders(int stageNumber, const RpgStage *stage);
/* Serializes the current children of this parent immediately before the
   folder is expanded.  Drop therefore exposes every owned metadata file,
   rather than only the pre-existing object_info.txt. */
bool RpgStaticObjectDrop_Expand(RpgStage *stage, int stageNumber, int row, int column,
                                const RpgAttachments *attachments,
                                int *expandedFileCount);
/* Drop only changes the static object folder.  The editor invokes
   this deferred reconciliation step to derive the visible missing-block state
   from that folder's presence, like the runtime build watcher does for eat. */
/* Applies folder-presence changes to the authored cell state.  When supplied,
   changedCells identifies exactly the cells whose folder disappearance was
   converted to a red-error cell during this call. */
bool RpgStaticObjectDrop_UpdateFolderAvailability(
    int stageNumber, RpgStage *stage,
    bool changedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS]);

#endif
