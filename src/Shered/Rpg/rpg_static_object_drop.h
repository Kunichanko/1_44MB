#ifndef RPG_STATIC_OBJECT_DROP_H
#define RPG_STATIC_OBJECT_DROP_H

#include "rpg_stage.h"

/* Editor-only source folders.  They belong to Settings/Stage/StageN and are
   never confused with the dynamic editor/game build folders. */
bool RpgStaticObjectDrop_EnsureFolder(int stageNumber, const RpgStage *stage,
                                      int row, int column, int blockType,
                                      char *path, int pathSize);
/* Renames only the retired cell_r##_c###_t### directories.  The stage data
   remains untouched; the new name is derived from its current area layout. */
bool RpgStaticObjectDrop_MigrateLegacyFolders(int stageNumber, const RpgStage *stage);
bool RpgStaticObjectDrop_Expand(RpgStage *stage, int stageNumber, int row, int column,
                                int *expandedFileCount);
bool RpgStaticObjectDrop_Restore(RpgStage *stage, int stageNumber, int row, int column);

#endif
