#ifndef RPG_STAGE_GROUND_TEXTURE_H
#define RPG_STAGE_GROUND_TEXTURE_H

#include "raylib.h"
#include "rpg_layout.h"

/* The image stays outside the executable; only its stage-owned path is saved. */
typedef struct RpgStageGroundTexture {
    Texture2D texture;
    char loadedPath[RPG_LAYOUT_BACKGROUND_PATH_LENGTH];
} RpgStageGroundTexture;

RpgStageGroundTexture RpgStageGroundTexture_Default(void);
bool RpgStageGroundTexture_Load(RpgStageGroundTexture *groundTexture, const char *path);
void RpgStageGroundTexture_Unload(RpgStageGroundTexture *groundTexture);

#endif
