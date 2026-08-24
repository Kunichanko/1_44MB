// 役割: 背景PNGを必要な時だけTextureへ変換し、画面の背面へ描画する。
// 依存する自プロジェクト内ファイル: rpg_stage_background.h
#include "rpg_stage_background.h"

#include "rpg_file_io.h"

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
    if (background == NULL) return false;
    if (path == NULL) path = "";
    if (strcmp(background->loadedPath, path) == 0) return background->texture.id != 0;
    if (path[0] == '\0') {
        RpgStageBackground_Unload(background);
        return true;
    }

    // WindowsのUnicode APIで読み取ったPNGをraylibのメモリ読み込みへ渡す。
    // raylibのパス経由読み込みに依存しないため、日本語を含む選択パスにも対応する。
    Texture2D loadedTexture = LoadTextureFromUtf8PngPath(path);
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
