// 役割: PNG画像を独立したステージ配置物として保持・選択・描画する。
// 依存する自プロジェクト内ファイル: なし。
#ifndef RPG_IMAGE_OBJECT_H
#define RPG_IMAGE_OBJECT_H

#include "raylib.h"

enum { RPG_IMAGE_OBJECT_MAX_COUNT = 64, RPG_IMAGE_OBJECT_PATH_LENGTH = 260 };

/* PNG配置物の描画順。背景→ブロック→中間→キャラクター→前景の順で描画する。 */
typedef enum RpgImageObjectLayer {
    RPG_IMAGE_OBJECT_LAYER_BACK = 0,
    RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK,
    RPG_IMAGE_OBJECT_LAYER_FRONT,
    RPG_IMAGE_OBJECT_LAYER_COUNT
} RpgImageObjectLayer;

/* PNG配置物は任意画像に加え、既存Explorer UIと同じShellアイコン表示へ切り替えられる。 */
typedef enum RpgImageObjectAppearance {
    RPG_IMAGE_OBJECT_APPEARANCE_PNG,
    RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FOLDER,
    RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FILE
} RpgImageObjectAppearance;

typedef struct RpgImageObject {
    unsigned int id;
    int row;
    int column;
    float scale;
    RpgImageObjectLayer layer;
    RpgImageObjectAppearance appearance;
    /* Shiftドラッグで保存する、マス中心に限定しない通常位置。 */
    float positionX;
    float positionY;
    bool hasCustomPosition;
    /* Function実行中だけ使う連続座標。保存位置はrow/columnのまま維持する。 */
    float runtimeX;
    float runtimeY;
    bool hasRuntimePosition;
    char path[RPG_IMAGE_OBJECT_PATH_LENGTH];
} RpgImageObject;

typedef struct RpgImageObjects {
    RpgImageObject entries[RPG_IMAGE_OBJECT_MAX_COUNT];
    int count;
    unsigned int nextId;
} RpgImageObjects;

RpgImageObjects RpgImageObjects_Default(void);
int RpgImageObjects_Add(RpgImageObjects *objects, int row, int column);
int RpgImageObjects_FindAtCell(const RpgImageObjects *objects, int row, int column);
bool RpgImageObjects_RemoveAtCell(RpgImageObjects *objects, int row, int column);
int RpgImageObjects_FindById(const RpgImageObjects *objects, unsigned int id);
bool RpgImageObjects_MoveToCell(RpgImageObjects *objects, int index, int row, int column);
bool RpgImageObjects_MoveToPosition(RpgImageObjects *objects, int index, Vector2 worldCenter,
                                    int tileSize, int worldColumns, int worldRows);
int RpgImageObjects_DuplicateRight(RpgImageObjects *objects, int index, int worldColumns);
void RpgImageObjects_DrawMap(const RpgImageObjects *objects, int mapIndex, int mapColumns,
                             int tileSize, Color tint);
void RpgImageObjects_DrawLayer(const RpgImageObjects *objects, int mapIndex, int mapColumns,
                               int tileSize, Color tint, RpgImageObjectLayer layer);
/* エディター共通ドラッグ用。配置済み画像と同じ経路でゴーストを描画する。 */
void RpgImageObjects_DrawPreview(const RpgImageObject *object, Rectangle bounds, Color tint);
Rectangle RpgImageObjects_GetLocalBounds(const RpgImageObject *object, int mapIndex,
                                         int mapColumns, int tileSize);
float RpgImageObjects_GetWorldCenterX(const RpgImageObject *object, int tileSize);
float RpgImageObjects_GetWorldCenterY(const RpgImageObject *object, int tileSize);
void RpgImageObjects_SetRuntimePosition(RpgImageObject *object, Vector2 worldCenter);
void RpgImageObjects_CommitRuntimePosition(RpgImageObject *object, int tileSize, int worldColumns, int worldRows);
void RpgImageObjects_UnloadTextures(void);

#endif
