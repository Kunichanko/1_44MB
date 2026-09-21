// 役割: 背景PNGを必要な時だけTextureへ変換し、画面の背面へ描画する。
// 依存する自プロジェクト内ファイル: rpg_stage_background.h
#include "rpg_stage_background.h"

#include "rpg_file_io.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 背景の透過率は変えず、RGB成分だけを倍率で暗くして明るさを表現する。 */
static Color ApplyBrightness(Color color, float brightness)
{
    if (brightness < 0.15f) brightness = 0.15f;
    if (brightness > 1.0f) brightness = 1.0f;
    color.r = (unsigned char)((float)color.r * brightness + 0.5f);
    color.g = (unsigned char)((float)color.g * brightness + 0.5f);
    color.b = (unsigned char)((float)color.b * brightness + 0.5f);
    return color;
}

static Texture2D LoadTextureFromUtf8PngPath(const char *path)
{
    unsigned char *fileData = NULL;
    int fileDataSize = 0;
    if (!RpgFileIo_ReadAllBytesUtf8(path, 256U * 1024U * 1024U, &fileData, &fileDataSize)) return (Texture2D){ 0 };
    Image image = LoadImageFromMemory(".png", fileData, fileDataSize);
    RpgFileIo_FreeBytes(fileData);
    if (image.data == NULL) return (Texture2D){ 0 };
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

RpgStageBackground RpgStageBackground_Default(void)
{
    return (RpgStageBackground){ 0 };
}

bool RpgStageBackground_Load(RpgStageBackground *background, const char *path)
{
    char resolvedPath[1200];
    if (background == NULL) return false;
    if (path == NULL) path = "";
    if (strcmp(background->loadedPath, path) == 0) return background->texture.id != 0;
    if (path[0] == '\0') {
        RpgStageBackground_Unload(background);
        return true;
    }

    // WindowsのUnicode APIで読み取ったPNGをraylibのメモリ読み込みへ渡す。
    // raylibのパス経由読み込みに依存しないため、日本語を含む選択パスにも対応する。
    if (!RpgFileIo_ResolveAssetPath("Sprite", path, resolvedPath, (int)sizeof(resolvedPath))) return false;
    Texture2D loadedTexture = LoadTextureFromUtf8PngPath(resolvedPath);
    if (loadedTexture.id == 0) return false;
    RpgStageBackground_Unload(background);
    background->texture = loadedTexture;
    snprintf(background->loadedPath, sizeof(background->loadedPath), "%s", path);
    return true;
}

void RpgStageBackground_Unload(RpgStageBackground *background)
{
    if (background == NULL) return;
    if (background->texture.id != 0) UnloadTexture(background->texture);
    background->texture = (Texture2D){ 0 };
    background->loadedPath[0] = '\0';
}

void RpgStageBackground_Draw(const RpgStageBackground *background, Rectangle destination,
                             float brightness)
{
    if (background == NULL || background->texture.id == 0) return;
    DrawTexturePro(background->texture,
                   (Rectangle){ 0.0f, 0.0f, (float)background->texture.width,
                                (float)background->texture.height },
                   destination,
                   (Vector2){ 0.0f, 0.0f }, 0.0f, ApplyBrightness(WHITE, brightness));
}

void RpgStageBackground_DrawRegion(const RpgStageBackground *background, Rectangle destination,
                                   Rectangle region, float brightness)
{
    if (background == NULL || background->texture.id == 0 ||
        destination.width <= 0.0f || destination.height <= 0.0f ||
        region.width <= 0.0f || region.height <= 0.0f) return;

    /* The recess must never sample outside its area's background. */
    float left = fmaxf(region.x, destination.x);
    float top = fmaxf(region.y, destination.y);
    float right = fminf(region.x + region.width, destination.x + destination.width);
    float bottom = fminf(region.y + region.height, destination.y + destination.height);
    if (right <= left || bottom <= top) return;

    Rectangle clipped = { left, top, right - left, bottom - top };
    Rectangle source = {
        (clipped.x - destination.x) * (float)background->texture.width / destination.width,
        (clipped.y - destination.y) * (float)background->texture.height / destination.height,
        clipped.width * (float)background->texture.width / destination.width,
        clipped.height * (float)background->texture.height / destination.height
    };
    DrawTexturePro(background->texture, source, clipped, (Vector2){ 0.0f, 0.0f }, 0.0f,
                   ApplyBrightness(WHITE, brightness));
}
