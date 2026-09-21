#ifndef RPG_STATIC_OBJECT_DROP_H
#define RPG_STATIC_OBJECT_DROP_H

#include "rpg_stage.h"

/* Editor-only source folders.  They belong to Settings/Stage/StageN and are
   never confused with the dynamic editor/game build folders. */
bool RpgStaticObjectDrop_EnsureFolder(int stageNumber, int row, int column, int blockType,
                                      char *path, int pathSize);
bool RpgStaticObjectDrop_Expand(RpgStage *stage, int stageNumber, int row, int column,
                                int *expandedFileCount);
bool RpgStaticObjectDrop_Restore(RpgStage *stage, int stageNumber, int row, int column);

#endif
