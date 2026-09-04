#ifndef RPG_GIMIC_SPRITES_H
#define RPG_GIMIC_SPRITES_H

#include "raylib.h"

typedef enum RpgGimicSprite {
    RPG_GIMIC_SPRITE_BOX,
    RPG_GIMIC_SPRITE_DATA_RECEIVER,
    RPG_GIMIC_SPRITE_CIRCLE,
    RPG_GIMIC_SPRITE_MAGNET,
    RPG_GIMIC_SPRITE_STEEL,
    RPG_GIMIC_SPRITE_SHOOTER,
    RPG_GIMIC_SPRITE_COUNT
} RpgGimicSprite;

enum { RPG_GIMIC_SHOOTER_FRAME_COUNT = 8 };

/* All gimmick artwork comes from assets/Sprite/gimics at runtime. */
void RpgGimicSprites_EnsureLoaded(void);
void RpgGimicSprites_Unload(void);
bool RpgGimicSprites_Draw(RpgGimicSprite sprite, Rectangle destination, Color tint);
bool RpgGimicSprites_DrawRotated(RpgGimicSprite sprite, Rectangle destination,
                                 float rotationDegrees, Color tint);
bool RpgGimicSprites_DrawShooter(Rectangle destination, float rotationDegrees,
                                 float animationProgress, Color tint);

#endif
