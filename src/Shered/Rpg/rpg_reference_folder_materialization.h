#ifndef RPG_REFERENCE_FOLDER_MATERIALIZATION_H
#define RPG_REFERENCE_FOLDER_MATERIALIZATION_H

#include <stddef.h>

#include "rpg_stage.h"
#include "rpg_magnet.h"

/* Watches movable Folder objects for a complete object_info.txt.  A valid
   normal block returns into its target error cell; a valid movable block
   returns as the same gravity-enabled runtime solid it was before Drop. */
void RpgReferenceFolderMaterialization_Reset(void);
void RpgReferenceFolderMaterialization_Update(RpgStage *stage, RpgReferenceObjects *objects,
                                              RpgMagnetRuntime *magnetRuntime,
                                              float deltaTime, float travelDuration);
/* Registers a bounded monitoring task after the game itself has stored a
 * File into a Folder.  The task always dispatches one validation; native
 * monitor failure is recorded without leaving the operation pending. */
void RpgReferenceFolderMaterialization_QueueFolderMonitorTask(RpgStage *stage,
                                                               RpgReferenceObjects *objects,
                                                               int folderObjectId);
/* Returns one pending player-facing diagnostic, then clears it. */
bool RpgReferenceFolderMaterialization_ConsumeMonitorTaskMessage(char *text, size_t textSize);
void RpgReferenceFolderMaterialization_Draw(void);

#endif
