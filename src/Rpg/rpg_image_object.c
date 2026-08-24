// 役割: PNG画像オブジェクトの軽量なTextureキャッシュと、マスにスナップする描画を実装する。
// 依存する自プロジェクト内ファイル: rpg_image_object.h, rpg_explorer_shell.h。
// 依存関係を更新: Folder/File表示へ既存ExplorerのShellアイコン取得を再利用する。
#include "rpg_image_object.h"

#include "rpg_explorer_shell.h"
#include "rpg_file_io.h"

#include "raymath.h"

#include <stdio.h>
#include <string.h>

enum { RPG_IMAGE_OBJECT_TEXTURE_CACHE_COUNT = 32 };
typedef struct RpgImageObjectTextureCacheEntry {
    char path[RPG_IMAGE_OBJECT_PATH_LENGTH];
    Texture2D texture;
} RpgImageObjectTextureCacheEntry;

static RpgImageObjectTextureCacheEntry textureCache[RPG_IMAGE_OBJECT_TEXTURE_CACHE_COUNT];
static Texture2D shellFolderTexture = { 0 };
static Texture2D shellFileTexture = { 0 };

/* 共通のUnicodeファイル読込からPNGをTexture化し、raylibのANSIパス読込を避ける。 */
static Texture2D LoadTextureFromUtf8PngPath(const char *path)
{
    unsigned char *bytes = NULL;
    int byteCount = 0;
    Image image;
    if (!RpgFileIo_ReadAllBytesUtf8(path, 64U * 1024U * 1024U, &bytes, &byteCount)) return (Texture2D){ 0 };
    image = LoadImageFromMemory(".png", bytes, byteCount);
    RpgFileIo_FreeBytes(bytes);
    if (image.data == NULL) return (Texture2D){ 0 };
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

static Texture2D GetTexture(const char *path)
{
    if (path == NULL || path[0] == '\0') return (Texture2D){ 0 };
    for (int index = 0; index < RPG_IMAGE_OBJECT_TEXTURE_CACHE_COUNT; index++)
        if (strcmp(textureCache[index].path, path) == 0) return textureCache[index].texture;
    for (int index = 0; index < RPG_IMAGE_OBJECT_TEXTURE_CACHE_COUNT; index++) {
        if (textureCache[index].path[0] != '\0') continue;
        snprintf(textureCache[index].path, sizeof(textureCache[index].path), "%s", path);
        textureCache[index].texture = LoadTextureFromUtf8PngPath(path);
        if (textureCache[index].texture.id != 0) SetTextureFilter(textureCache[index].texture, TEXTURE_FILTER_POINT);
        return textureCache[index].texture;
    }
    return (Texture2D){ 0 };
}

static Texture2D GetAppearanceTexture(const RpgImageObject *object)
{
    if (object == NULL) return (Texture2D){ 0 };
    if (object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FOLDER) {
        if (shellFolderTexture.id == 0) shellFolderTexture = RpgExplorerShell_LoadFolderIconTexture();
        return shellFolderTexture;
    }
    if (object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FILE) {
        if (shellFileTexture.id == 0) shellFileTexture = RpgExplorerShell_LoadFileIconTexture();
        return shellFileTexture;
    }
    return GetTexture(object->path);
}

RpgImageObjects RpgImageObjects_Default(void) { return (RpgImageObjects){ .nextId = 1 }; }

int RpgImageObjects_FindAtCell(const RpgImageObjects *objects, int row, int column)
{
    if (objects == NULL) return -1;
    for (int index = objects->count - 1; index >= 0; index--)
        if (objects->entries[index].row == row && objects->entries[index].column == column) return index;
    return -1;
}

bool RpgImageObjects_RemoveAtCell(RpgImageObjects *objects, int row, int column)
{
    int index = RpgImageObjects_FindAtCell(objects, row, column);
    if (objects == NULL || index < 0) return false;
    /* 配列を詰め、PNG配置物も他の1マス配置物と同じ削除結果にする。 */
    for (int next = index; next < objects->count - 1; next++)
        objects->entries[next] = objects->entries[next + 1];
    objects->count--;
    return true;
}

int RpgImageObjects_FindById(const RpgImageObjects *objects, unsigned int id)
{
    if (objects == NULL || id == 0) return -1;
    for (int index = 0; index < objects->count; index++)
        if (objects->entries[index].id == id) return index;
    return -1;
}

int RpgImageObjects_Add(RpgImageObjects *objects, int row, int column)
{
    /* PNGは見た目専用の非占有オブジェクトなので、ブロックや他のPNGと同じマスへ重ねて置ける。 */
    if (objects == NULL || objects->count >= RPG_IMAGE_OBJECT_MAX_COUNT) return -1;
    if (objects->nextId == 0) objects->nextId = 1;
    int index = objects->count++;
    objects->entries[index] = (RpgImageObject){ .id = objects->nextId++, .row = row, .column = column,
                                                .scale = 1.0f,
                                                .layer = RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK };
    return index;
}

bool RpgImageObjects_MoveToCell(RpgImageObjects *objects, int index, int row, int column)
{
    if (objects == NULL || index < 0 || index >= objects->count) return false;
    objects->entries[index].row = row;
    objects->entries[index].column = column;
    objects->entries[index].hasCustomPosition = false;
    objects->entries[index].positionX = 0.0f;
    objects->entries[index].positionY = 0.0f;
    return true;
}

bool RpgImageObjects_MoveToPosition(RpgImageObjects *objects, int index, Vector2 worldCenter,
                                    int tileSize, int worldColumns, int worldRows)
{
    int column;
    int row;
    if (objects == NULL || index < 0 || index >= objects->count || tileSize <= 0 ||
        worldColumns <= 0 || worldRows <= 0 || worldCenter.x < 0.0f || worldCenter.y < 0.0f ||
        worldCenter.x >= worldColumns * tileSize || worldCenter.y >= worldRows * tileSize) return false;
    column = (int)floorf(worldCenter.x / tileSize);
    row = (int)floorf(worldCenter.y / tileSize);
    objects->entries[index].row = row;
    objects->entries[index].column = column;
    objects->entries[index].positionX = worldCenter.x;
    objects->entries[index].positionY = worldCenter.y;
    objects->entries[index].hasCustomPosition = true;
    return true;
}

int RpgImageObjects_DuplicateRight(RpgImageObjects *objects, int index, int worldColumns)
{
    if (objects == NULL || index < 0 || index >= objects->count) return -1;
    const RpgImageObject source = objects->entries[index];
    int destinationColumn = source.column + 1;
    if (destinationColumn >= worldColumns || objects->count >= RPG_IMAGE_OBJECT_MAX_COUNT) return -1;
    if (objects->nextId == 0) objects->nextId = 1;
    /* Inspectorで編集できる設定は構造体全体から引き継ぎ、固有IDと配置先だけを変更する。 */
    RpgImageObject copy = source;
    copy.id = objects->nextId++;
    copy.column = destinationColumn;
    /* 複製先は右隣のマスを基準にし、元の任意位置をそのまま重ねない。 */
    copy.hasCustomPosition = false;
    copy.positionX = 0.0f;
    copy.positionY = 0.0f;
    objects->entries[objects->count] = copy;
    return objects->count++;
}

Rectangle RpgImageObjects_GetLocalBounds(const RpgImageObject *object, int mapIndex,
                                         int mapColumns, int tileSize)
{
    if (object == NULL) return (Rectangle){ 0 };
    float size = tileSize * Clamp(object->scale, 0.25f, 8.0f);
    float worldCenterX = RpgImageObjects_GetWorldCenterX(object, tileSize);
    float centerX = worldCenterX - mapIndex * mapColumns * tileSize;
    float centerY = RpgImageObjects_GetWorldCenterY(object, tileSize);
    return (Rectangle){ centerX - size * 0.5f, centerY - size * 0.5f, size, size };
}

float RpgImageObjects_GetWorldCenterX(const RpgImageObject *object, int tileSize)
{
    if (object == NULL) return 0.0f;
    return object->hasRuntimePosition ? object->runtimeX :
           object->hasCustomPosition ? object->positionX : (object->column + 0.5f) * tileSize;
}

float RpgImageObjects_GetWorldCenterY(const RpgImageObject *object, int tileSize)
{
    if (object == NULL) return 0.0f;
    return object->hasRuntimePosition ? object->runtimeY :
           object->hasCustomPosition ? object->positionY : (object->row + 0.5f) * tileSize;
}

void RpgImageObjects_SetRuntimePosition(RpgImageObject *object, Vector2 worldCenter)
{
    if (object == NULL) return;
    object->runtimeX = worldCenter.x;
    object->runtimeY = worldCenter.y;
    object->hasRuntimePosition = true;
}

void RpgImageObjects_CommitRuntimePosition(RpgImageObject *object, int tileSize, int worldColumns, int worldRows)
{
    if (object == NULL || tileSize <= 0 || worldColumns <= 0 || worldRows <= 0) return;
    int column = (int)floorf(object->runtimeX / tileSize);
    int row = (int)floorf(object->runtimeY / tileSize);
    object->column = Clamp(column, 0, worldColumns - 1);
    object->row = Clamp(row, 0, worldRows - 1);
    object->positionX = object->runtimeX;
    object->positionY = object->runtimeY;
    object->hasCustomPosition = true;
    /* 実行中だけ使う座標は確定時に消し、保存比較や次回の配置に混ざらないようにする。 */
    object->runtimeX = 0.0f;
    object->runtimeY = 0.0f;
    object->hasRuntimePosition = false;
}

void RpgImageObjects_DrawPreview(const RpgImageObject *object, Rectangle bounds, Color tint)
{
    if (object == NULL) return;
    Texture2D texture = GetAppearanceTexture(object);
    if (texture.id != 0) {
        DrawTexturePro(texture, (Rectangle){ 0, 0, (float)texture.width, (float)texture.height },
                       bounds, (Vector2){ 0 }, 0.0f, tint);
    } else if (object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_PNG && object->path[0] == '\0') {
        DrawRectangleRec(bounds, Fade(GOLD, 0.55f * ((float)tint.a / 255.0f)));
        DrawRectangleLinesEx(bounds, 2.0f, ORANGE);
        DrawText("SET", (int)bounds.x + 4, (int)bounds.y + (int)bounds.height / 2 - 6, 11, MAROON);
    } else {
        DrawRectangleRec(bounds, Fade(MAROON, 0.48f * ((float)tint.a / 255.0f)));
        DrawRectangleLinesEx(bounds, 2.0f, RED);
        DrawText("ERR", (int)bounds.x + 4, (int)bounds.y + (int)bounds.height / 2 - 6, 11, RAYWHITE);
    }
}

void RpgImageObjects_DrawMap(const RpgImageObjects *objects, int mapIndex, int mapColumns,
                             int tileSize, Color tint)
{
    if (objects == NULL) return;
    int firstColumn = mapIndex * mapColumns;
    for (int index = 0; index < objects->count; index++) {
        const RpgImageObject *object = &objects->entries[index];
        int displayColumn = (int)(RpgImageObjects_GetWorldCenterX(object, tileSize) / tileSize);
        if (displayColumn < firstColumn || displayColumn >= firstColumn + mapColumns) continue;
        Rectangle bounds = RpgImageObjects_GetLocalBounds(object, mapIndex, mapColumns, tileSize);
        Texture2D texture = GetAppearanceTexture(object);
        if (texture.id != 0) {
            DrawTexturePro(texture, (Rectangle){ 0, 0, (float)texture.width, (float)texture.height },
                           bounds, (Vector2){ 0 }, 0.0f, tint);
        } else if (object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_PNG && object->path[0] == '\0') {
            /* 未割当は読み込み失敗と混同しないよう、黄色い選択待ちマーカーで明示する。 */
            DrawRectangleRec(bounds, Fade(GOLD, 0.55f));
            DrawRectangleLinesEx(bounds, 2.0f, ORANGE);
            DrawText("SET", (int)bounds.x + 4, (int)bounds.y + (int)bounds.height / 2 - 6, 11, MAROON);
        } else {
            DrawRectangleRec(bounds, Fade(MAROON, 0.48f));
            DrawRectangleLinesEx(bounds, 2.0f, RED);
            DrawText("ERR", (int)bounds.x + 4, (int)bounds.y + (int)bounds.height / 2 - 6, 11, RAYWHITE);
        }
    }
}

void RpgImageObjects_DrawLayer(const RpgImageObjects *objects, int mapIndex, int mapColumns,
                               int tileSize, Color tint, RpgImageObjectLayer layer)
{
    if (objects == NULL || layer < RPG_IMAGE_OBJECT_LAYER_BACK || layer >= RPG_IMAGE_OBJECT_LAYER_COUNT) return;
    int firstColumn = mapIndex * mapColumns;
    for (int index = 0; index < objects->count; index++) {
        const RpgImageObject *object = &objects->entries[index];
        int displayColumn = (int)(RpgImageObjects_GetWorldCenterX(object, tileSize) / tileSize);
        if (object->layer != layer || displayColumn < firstColumn ||
            displayColumn >= firstColumn + mapColumns) continue;
        RpgImageObjects_DrawPreview(object,
                                    RpgImageObjects_GetLocalBounds(object, mapIndex, mapColumns, tileSize), tint);
    }
}

void RpgImageObjects_UnloadTextures(void)
{
    for (int index = 0; index < RPG_IMAGE_OBJECT_TEXTURE_CACHE_COUNT; index++) {
        if (textureCache[index].texture.id != 0) UnloadTexture(textureCache[index].texture);
        textureCache[index] = (RpgImageObjectTextureCacheEntry){ 0 };
    }
    if (shellFolderTexture.id != 0) UnloadTexture(shellFolderTexture);
    if (shellFileTexture.id != 0) UnloadTexture(shellFileTexture);
    shellFolderTexture = (Texture2D){ 0 };
    shellFileTexture = (Texture2D){ 0 };
}
