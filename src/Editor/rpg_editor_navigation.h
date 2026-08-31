// Role: editor-only area navigation shortcuts. Never used by the game runtime.
#ifndef RPG_EDITOR_NAVIGATION_H
#define RPG_EDITOR_NAVIGATION_H

#include <stdbool.h>
#include "rpg_stage.h"

/* Returns an adjacent editor area requested with an unmodified arrow key. */
int RpgEditorNavigation_RequestAreaMove(const RpgStage *stage, int currentMapIndex);

/* Returns true only for Ctrl+Arrow, which is reserved for editor area insertion. */
bool RpgEditorNavigation_RequestAreaInsertion(RpgAreaDirection *direction);

#endif
