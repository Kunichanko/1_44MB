// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_STAGE_H
#define RPG_STAGE_H

#include "raylib.h"
#include "rpg_image_object.h"

enum { RPG_STAGE_TILE_SIZE = 32, RPG_STAGE_COLUMNS = 16, RPG_STAGE_ROWS = 8,
       RPG_STAGE_INITIAL_MAP_COUNT = 6, RPG_STAGE_MAP_COUNT = 24,
       RPG_STAGE_WORLD_COLUMNS = RPG_STAGE_COLUMNS * RPG_STAGE_MAP_COUNT,
       RPG_STAGE_WORLD_WIDTH = RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE,
       RPG_STAGE_WORLD_HEIGHT = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE,
       RPG_STAGE_GROUND_START_ROW = RPG_STAGE_ROWS - 2,
       RPG_STAGE_GROUND_TOP = RPG_STAGE_GROUND_START_ROW * RPG_STAGE_TILE_SIZE,
       RPG_STAGE_REFERENCE_PATH_LENGTH = 260 };

typedef enum RpgAreaDirection {
    RPG_AREA_LEFT,
    RPG_AREA_RIGHT,
    RPG_AREA_UP,
    RPG_AREA_DOWN
} RpgAreaDirection;

typedef struct RpgStage {
    // エリアの実体は固定スロットに置き、使用中のスロットだけを台帳で管理する。
    bool mapActive[RPG_STAGE_MAP_COUNT];
    int mapGridX[RPG_STAGE_MAP_COUNT];
    int mapGridY[RPG_STAGE_MAP_COUNT];
    int blocks[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    char referencePaths[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS][RPG_STAGE_REFERENCE_PATH_LENGTH];
    /* PNG画像はFolderやFILE.pngと別の、見た目専用オブジェクトとして管理する。 */
    RpgImageObjects imageObjects;
} RpgStage;

RpgStage RpgStage_Default(void);
bool RpgStage_Load(const char *filePath, RpgStage *stage);
bool RpgStage_Save(const char *filePath, const RpgStage *stage);
int RpgStage_GetMapCount(const RpgStage *stage);
bool RpgStage_IsMapActive(const RpgStage *stage, int mapIndex);
int RpgStage_GetMapAtGrid(const RpgStage *stage, int gridX, int gridY);
// 二次元ステージID (x, y) に最も近い、現在有効なステージスロットを返す。
int RpgStage_FindNearestActiveMapAtGrid(const RpgStage *stage, int gridX, int gridY);
// 無効になったスロットを参照している場合も、二次元IDを基準に最寄りへ補正する。
int RpgStage_FindNearestActiveMap(const RpgStage *stage, int mapIndex);
int RpgStage_GetAdjacentMap(const RpgStage *stage, int mapIndex, RpgAreaDirection direction);
int RpgStage_GetOrCreateAdjacentMap(RpgStage *stage, int mapIndex, RpgAreaDirection direction);
bool RpgStage_RemoveMap(RpgStage *stage, int mapIndex);
bool RpgStage_SetBlockAtPosition(RpgStage *stage, Vector2 position, bool isBlock);
bool RpgStage_SetBlockTypeAtPosition(RpgStage *stage, Vector2 position, int blockType);
int RpgStage_GetBlockTypeAtPosition(const RpgStage *stage, Vector2 position);
// 指定マスを含むドア全体を開閉状態へ切り替える。ドア以外では何もしない。
bool RpgStage_SetDoorOpenAtCell(RpgStage *stage, int row, int column, bool isOpen);
bool RpgStage_SetReferencePathAtCell(RpgStage *stage, int row, int column, const char *path);
const char *RpgStage_GetReferencePathAtCell(const RpgStage *stage, int row, int column);
enum { RPG_REFERENCE_OBJECT_MAX_COUNT = 32 };
typedef struct RpgReferenceObject {
    Vector2 position;
    char path[RPG_STAGE_REFERENCE_PATH_LENGTH];
    bool isFalling;
    float fallSpeed;
    /* Gで取得したFileはステージ上の対象から独立し、プレイヤー後方を小さく追従する。 */
    bool followsPlayer;
    float drawScale;
} RpgReferenceObject;
typedef struct RpgReferenceObjects { int count; RpgReferenceObject entries[RPG_REFERENCE_OBJECT_MAX_COUNT]; } RpgReferenceObjects;
typedef enum RpgReferenceTargetKind { RPG_REFERENCE_TARGET_NONE, RPG_REFERENCE_TARGET_CELL, RPG_REFERENCE_TARGET_DROP } RpgReferenceTargetKind;
typedef struct RpgReferenceTarget { RpgReferenceTargetKind kind; int row; int column; int dropIndex; } RpgReferenceTarget;
RpgReferenceObjects RpgReferenceObjects_Default(void);
bool RpgReferenceObjects_AddDrop(RpgReferenceObjects *objects, Vector2 position, const char *path);
void RpgReferenceObjects_Update(RpgReferenceObjects *objects, float deltaTime);
void RpgReferenceObjects_UpdateFollowers(RpgReferenceObjects *objects, Vector2 playerPosition,
                                         float playerScale, float playerMoveSpeed, float followerScale,
                                         float deltaTime);
bool RpgReferenceObjects_CollectTarget(RpgStage *stage, RpgReferenceObjects *objects,
                                       RpgReferenceTarget target);
void RpgReferenceObjects_Draw(const RpgReferenceObjects *objects, Texture2D fileTexture);
void RpgReferenceObjects_DrawExcept(const RpgReferenceObjects *objects, Texture2D fileTexture,
                                    int excludedIndex);
int RpgReferenceObjects_FindNearby(const RpgReferenceObjects *objects, Vector2 position, float distance);
bool RpgReferenceObjects_FindNearbyTarget(const RpgStage *stage, const RpgReferenceObjects *objects,
                                          Vector2 position, float distance, RpgReferenceTarget *target);
/* Fileの追従先とは別に、近くのFolderだけを検索して格納操作へ利用する。 */
bool RpgReferenceObjects_FindNearbyFolderTarget(const RpgStage *stage, Vector2 position,
                                                float distance, RpgReferenceTarget *target);
int RpgReferenceObjects_FindFollowerIndex(const RpgReferenceObjects *objects);
bool RpgReferenceObjects_FindTarget(const RpgStage *stage, const RpgReferenceObjects *objects,
                                    Vector2 position, RpgReferenceTarget *target);
const char *RpgReferenceObjects_GetTargetPath(const RpgStage *stage, const RpgReferenceObjects *objects,
                                               RpgReferenceTarget target);
bool RpgReferenceObjects_RemoveTarget(RpgStage *stage, RpgReferenceObjects *objects,
                                      RpgReferenceTarget target);
bool RpgStage_IsSolidBlock(int blockType);
bool RpgStage_IsSolidAtPosition(const RpgStage *stage, Vector2 position);
bool RpgStage_CheckSolidCollision(const RpgStage *stage, Rectangle bounds);
bool RpgStage_FindSolidCollisionCenter(const RpgStage *stage, Rectangle bounds, Vector2 *center);
bool RpgStage_CheckSolidCircleCollision(const RpgStage *stage, Vector2 center, float radius);
bool RpgStage_FindSolidCircleCollisionCenter(const RpgStage *stage, Vector2 center, float radius,
                                             Vector2 *collisionCenter);
Color RpgStage_GetBlockColor(int blockType);
// 描画と衝突判定で共有する穴ブロックの実体部分。縦・横とも中央20pxを空洞にする。
int RpgStage_GetHoleSolidParts(Rectangle cell, int blockType, Rectangle solidParts[2]);
// ステージ・パレット共通の1マス描画。穴ブロックは上の実体部分だけを描画する。
void RpgStage_DrawBlockCell(Rectangle cell, int blockType, float brightness);
// 16x8マスを960x480へ拡大する際、ワールド座標を表示ピクセルへそろえる共通処理。
float RpgStage_SnapRenderCoordinate(float coordinate);
Vector2 RpgStage_SnapRenderPoint(Vector2 point);
Rectangle RpgStage_SnapRenderRectangle(Rectangle rectangle);
// 1マス=32px基準の特殊ブロック記号を、ゲームとエディターで共通描画する。
void RpgStage_DrawEffectSymbol(Rectangle cell, int blockType);
void RpgStage_Draw(const RpgStage *stage, bool showGrid, float brightness);
void RpgStage_DrawMap(const RpgStage *stage, int mapIndex, bool showGrid, float brightness);
void RpgStage_DrawEffects(const RpgStage *stage);
void RpgStage_DrawMapEffects(const RpgStage *stage, int mapIndex);
void RpgStage_DrawReferenceObject(Texture2D fileTexture, Rectangle cell, Color tint);
void RpgStage_DrawReferenceFolder(Rectangle cell, Color tint);
void RpgStage_DrawReferenceObjects(const RpgStage *stage, Texture2D fileTexture);
void RpgStage_DrawReferenceObjectsExcept(const RpgStage *stage, Texture2D fileTexture,
                                         int excludedRow, int excludedColumn);
void RpgStage_DrawMapReferenceObjects(const RpgStage *stage, int mapIndex, Texture2D fileTexture);

#endif
// 役割: RPG ステージのグリッド・衝突・描画 API を宣言する。
