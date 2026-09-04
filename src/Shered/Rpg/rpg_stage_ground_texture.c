#include "rpg_stage_ground_texture.h"

#include "rpg_file_io.h"

#include <string.h>

static Texture2D LoadPngUtf8(const char *path)
{
    unsigned char *bytes = NULL;
    int byteCount = 0;
    if (!RpgFileIo_ReadAllBytesUtf8(path, 256U * 1024U * 1024U, &bytes, &byteCount))
        return (Texture2D){ 0 };
    Image image = LoadImageFromMemory(".png", bytes, byteCount);
    RpgFileIo_FreeBytes(bytes);
    if (image.data == NULL) return (Texture2D){ 0 };
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}

RpgStageGroundTexture RpgStageGroundTexture_Default(void)
{
    return (RpgStageGroundTexture){ 0 };
}

bool RpgStageGroundTexture_Load(RpgStageGroundTexture *groundTexture, const char *path)
{
    if (groundTexture == NULL) return false;
    if (path == NULL) path = "";
    if (strcmp(groundTexture->loadedPath, path) == 0)
        return path[0] == '\0' || groundTexture->texture.id != 0;
    if (path[0] == '\0') {
        RpgStageGroundTexture_Unload(groundTexture);
        return true;
    }
    Texture2D texture = LoadPngUtf8(path);
    if (texture.id == 0) return false;
    RpgStageGroundTexture_Unload(groundTexture);
    groundTexture->texture = texture;
    strncpy(groundTexture->loadedPath, path, sizeof(groundTexture->loadedPath) - 1);
    groundTexture->loadedPath[sizeof(groundTexture->loadedPath) - 1] = '\0';
    return true;
}

void RpgStageGroundTexture_Unload(RpgStageGroundTexture *groundTexture)
{
    if (groundTexture == NULL) return;
    if (groundTexture->texture.id != 0) UnloadTexture(groundTexture->texture);
    groundTexture->texture = (Texture2D){ 0 };
    groundTexture->loadedPath[0] = '\0';
}
