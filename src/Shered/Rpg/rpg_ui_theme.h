// Shared RPG UI colors.  Keep gameplay sprites and world effects independent
// from these values so a UI adjustment does not recolor stage assets.
#ifndef RPG_UI_THEME_H
#define RPG_UI_THEME_H

#include "raylib.h"

/* Slightly brighter than raylib's DARKBLUE; used for primary UI surfaces. */
#define RPG_UI_PRIMARY_BLUE ((Color){ 20, 112, 215, 255 })

#endif
