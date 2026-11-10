#ifndef RPG_FOLDER_TRAVEL_VISUAL_H
#define RPG_FOLDER_TRAVEL_VISUAL_H

#include "raylib.h"

/* Shared visual used whenever a physical object folder travels between two
   world locations.  Zipper spit and folder-to-block materialisation use the
   identical easing, icon, scale and fade. */
void RpgFolderTravelVisual_Draw(Vector2 start, Vector2 destination, float elapsed, float duration);
/* Used by the same folder-return path when the final commit is rejected. */
void RpgFolderTravelVisual_DrawFailure(Vector2 position, float elapsed, float duration);

#endif
