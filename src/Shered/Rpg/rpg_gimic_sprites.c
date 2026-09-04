#include "rpg_gimic_sprites.h"

#include <stdio.h>

static Texture2D sprites[RPG_GIMIC_SPRITE_COUNT];
static bool attemptedLoad = false;

static const char *const spriteFileNames[RPG_GIMIC_SPRITE_COUNT] = {
    "box.png", "data_receiver.png", "circle.png", "magnet.png", "steel.png", "shooter.png"
};

void RpgGimicSprites_EnsureLoaded(void)
{
    if (attemptedLoad) return;
    attemptedLoad = true;
    const char *directory = GetApplicationDirectory();
    for (int index = 0; index < RPG_GIMIC_SPRITE_COUNT; index++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s../assets/Sprite/gimics/%s", directory,
                 spriteFileNames[index]);
        sprites[index] = LoadTexture(path);
        if (sprites[index].id != 0) SetTextureFilter(sprites[index], TEXTURE_FILTER_POINT);
    }
}

void RpgGimicSprites_Unload(void)
{
    for (int index = 0; index < RPG_GIMIC_SPRITE_COUNT; index++) {
        if (sprites[index].id != 0) UnloadTexture(sprites[index]);
        sprites[index] = (Texture2D){ 0 };
    }
    attemptedLoad = false;
}

static bool DrawSpriteRegion(RpgGimicSprite sprite, Rectangle source, Rectangle destination,
                             float rotationDegrees, Color tint)
{
    if (sprite < 0 || sprite >= RPG_GIMIC_SPRITE_COUNT) return false;
    RpgGimicSprites_EnsureLoaded();
    Texture2D texture = sprites[sprite];
    if (texture.id == 0) return false;
    Vector2 origin = { destination.width * 0.5f, destination.height * 0.5f };
    DrawTexturePro(texture, source, (Rectangle){ destination.x + origin.x,
                   destination.y + origin.y, destination.width, destination.height },
                   origin, rotationDegrees, tint);
    return true;
}

bool RpgGimicSprites_Draw(RpgGimicSprite sprite, Rectangle destination, Color tint)
{
    if (sprite < 0 || sprite >= RPG_GIMIC_SPRITE_COUNT) return false;
    RpgGimicSprites_EnsureLoaded();
    Texture2D texture = sprites[sprite];
    if (texture.id == 0) return false;
    return DrawSpriteRegion(sprite, (Rectangle){ 0.0f, 0.0f, (float)texture.width,
                             (float)texture.height }, destination, 0.0f, tint);
}

bool RpgGimicSprites_DrawRotated(RpgGimicSprite sprite, Rectangle destination,
                                 float rotationDegrees, Color tint)
{
    if (sprite < 0 || sprite >= RPG_GIMIC_SPRITE_COUNT) return false;
    RpgGimicSprites_EnsureLoaded();
    Texture2D texture = sprites[sprite];
    if (texture.id == 0) return false;
    return DrawSpriteRegion(sprite, (Rectangle){ 0.0f, 0.0f, (float)texture.width,
                             (float)texture.height }, destination, rotationDegrees, tint);
}

bool RpgGimicSprites_DrawShooter(Rectangle destination, float rotationDegrees,
                                 float animationProgress, Color tint)
{
    RpgGimicSprites_EnsureLoaded();
    Texture2D texture = sprites[RPG_GIMIC_SPRITE_SHOOTER];
    if (texture.id == 0) return false;
    int frame = (int)(animationProgress * RPG_GIMIC_SHOOTER_FRAME_COUNT);
    if (frame < 0) frame = 0;
    if (frame >= RPG_GIMIC_SHOOTER_FRAME_COUNT) frame = RPG_GIMIC_SHOOTER_FRAME_COUNT - 1;
    const float frameWidth = (float)texture.width / 4.0f;
    const float frameHeight = (float)texture.height / 2.0f;
    Rectangle source = { (float)(frame % 4) * frameWidth,
                         (float)(frame / 4) * frameHeight, frameWidth, frameHeight };
    return DrawSpriteRegion(RPG_GIMIC_SPRITE_SHOOTER, source, destination, rotationDegrees, tint);
}
