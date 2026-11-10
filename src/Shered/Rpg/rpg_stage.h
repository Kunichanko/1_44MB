// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_STAGE_H
#define RPG_STAGE_H

#include "raylib.h"
#include "rpg_grid_path.h"

struct RpgAttachments;
#include "rpg_block_inventory.h"
#include "rpg_image_object.h"

struct RpgAttachments;

enum { RPG_STAGE_TILE_SIZE = 32, RPG_STAGE_COLUMNS = 20, RPG_STAGE_ROWS = 12,
       RPG_STAGE_INITIAL_MAP_COUNT = 6, RPG_STAGE_MAP_COUNT = 24,
       RPG_STAGE_WORLD_COLUMNS = RPG_STAGE_COLUMNS * RPG_STAGE_MAP_COUNT,
       RPG_STAGE_WORLD_WIDTH = RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE,
       RPG_STAGE_WORLD_HEIGHT = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE,
       RPG_STAGE_GROUND_START_ROW = RPG_STAGE_ROWS - 2,
       RPG_STAGE_GROUND_TOP = RPG_STAGE_GROUND_START_ROW * RPG_STAGE_TILE_SIZE,
       RPG_STAGE_REFERENCE_PATH_LENGTH = 260, RPG_KEY_DOOR_MAX_COUNT = 32,
       /* Fixed and resizable composites share one ID space and one record
          collection.  The former 128 fixed + 64 magnet slots are retained as
          one capacity so opening an existing stage cannot lose an object. */
       RPG_COMPOSITE_OBJECT_MAX_COUNT = 192 };

typedef enum RpgAreaDirection {
    RPG_AREA_LEFT,
    RPG_AREA_RIGHT,
    RPG_AREA_UP,
    RPG_AREA_DOWN
} RpgAreaDirection;

/* The saved cell value and its existence are deliberately separate.  A zero
 * is ordinary air; an ERROR is serialized as an empty CSV field and renders
 * as the red missing-object marker.  RUNTIME_ERROR is an invalid block type
 * discovered while the game is running: it renders black and is never
 * serialized as authored stage data. */
typedef enum RpgCellState {
    RPG_CELL_AIR = 0,
    RPG_CELL_BLOCK,
    RPG_CELL_ERROR,
    RPG_CELL_RUNTIME_ERROR
} RpgCellState;

typedef struct RpgKeyDoor {
    int rootRow;
    int rootColumn;
    char keyPath[RPG_STAGE_REFERENCE_PATH_LENGTH];
} RpgKeyDoor;

typedef enum RpgCompositeGeometry {
    /* Components come from the immutable RpgEffectShape registered for the
       block type (doors, extend/shrink blocks, etc.). */
    RPG_COMPOSITE_GEOMETRY_FIXED_SHAPE = 0,
    /* Components occupy every cell in the inclusive origin/width/height
       rectangle.  Magnets currently use this, but the record is not
       magnet-specific. */
    RPG_COMPOSITE_GEOMETRY_RECTANGLE
} RpgCompositeGeometry;

/* A composite has a stable identity independent from every tile that happens
 * to make up its current geometry.  Both fixed and resizable objects use this
 * one record. `origin*` is placement data only; it is never a filesystem
 * owner.  Fixed shapes leave width/height at zero. */
typedef struct RpgCompositeObject {
    int id;
    union { int originRow; int rootRow; };
    union { int originColumn; int rootColumn; };
    int blockType;
    RpgCompositeGeometry geometry;
    int width;
    int height;
} RpgCompositeObject;
/* Source compatibility for gameplay code that deals specifically with the
 * rectangular magnet flavour.  This is an alias only: no second record or
 * collection exists in RpgStage. */
typedef RpgCompositeObject RpgMagnetObject;

typedef enum RpgReferenceObjectKind {
    RPG_REFERENCE_OBJECT_FILE = 0,
    RPG_REFERENCE_OBJECT_FOLDER = 1
} RpgReferenceObjectKind;

enum { RPG_REFERENCE_OBJECT_MAX_COUNT = 32 };
typedef struct RpgReferenceObject {
    int id;
    RpgReferenceObjectKind objectKind;
    Vector2 position;
    /* Only populated while importing the retired cell-based reference format.
       It is not persistent runtime state: stage storage writes the resolved
       world position after the import has completed. */
    int legacySourceRow;
    int legacySourceColumn;
    /* The editor selection is kept separately from the transient build copy.
       It is always relative to the file-picker base (assets/Files) and is the
       value shown back to the author in the inspector. */
    char sourcePath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    char path[RPG_STAGE_REFERENCE_PATH_LENGTH];
    bool isFalling;
    float fallSpeed;
    bool followsPlayer;
    float drawScale;
    bool isCompressing;
    bool isCompressed;
    float compressionElapsed;
    /* Runtime-only: this Folder is travelling into a valid block cell. */
    bool isMaterializing;
} RpgReferenceObject;
typedef struct RpgReferenceObjects {
    int count;
    int nextId;
    RpgReferenceObject entries[RPG_REFERENCE_OBJECT_MAX_COUNT];
} RpgReferenceObjects;

typedef struct RpgStage {
    // エリアの実体は固定スロットに置き、使用中のスロットだけを台帳で管理する。
    bool mapActive[RPG_STAGE_MAP_COUNT];
    int mapGridX[RPG_STAGE_MAP_COUNT];
    int mapGridY[RPG_STAGE_MAP_COUNT];
    /* Runtime-only level state for block-socket signals.  It deliberately is
       not written into the static stage file. */
    bool socketSignalActive[RPG_STAGE_MAP_COUNT];
    /* Transient query origin: never serialized.  It lets a movement step cross
       a map edge in the continuous stage space before its storage slot changes. */
    int spatialReferenceMap;
    int blocks[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    RpgCellState cellStates[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    char referencePaths[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS][RPG_STAGE_REFERENCE_PATH_LENGTH];
    /* Legacy cell paths are migration input only; reference objects are
       movable and serialize under the per-area movables tree. */
    RpgReferenceObjects referenceObjects;
    int keyDoorCount;
    RpgKeyDoor keyDoors[RPG_KEY_DOOR_MAX_COUNT];
    int compositeObjectCount;
    int nextCompositeObjectId;
    RpgCompositeObject compositeObjects[RPG_COMPOSITE_OBJECT_MAX_COUNT];
    /* Runtime lookup cache.  It is derived from compositeObjects and
       is deliberately not part of the saved stage format. */
    int compositeObjectIds[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
    /* PNG画像はFolderやFILE.pngと別の、見た目専用オブジェクトとして管理する。 */
    RpgImageObjects imageObjects;
} RpgStage;

/* 大きいステージ本体を値返却せず、呼出し側が保持する領域を初期化する。 */
void RpgStage_Initialize(RpgStage *stage);
RpgCellState RpgStage_GetCellState(const RpgStage *stage, int row, int column);
/* Static red missing-object marker. */
bool RpgStage_IsCellError(const RpgStage *stage, int row, int column);
/* Runtime-only invalid/corrupt block type marker. */
bool RpgStage_IsCellRuntimeError(const RpgStage *stage, int row, int column);
/* Both error variants are drawn and collide as a complete solid cell. */
bool RpgStage_IsCellUnavailable(const RpgStage *stage, int row, int column);
void RpgStage_SetCellBlock(RpgStage *stage, int row, int column, int blockType);
/* `storedBlockType` is retained only for runtime eat/spit restoration.  Pass
   zero for a static/editor error which deliberately has no restore payload. */
void RpgStage_SetCellError(RpgStage *stage, int row, int column, int storedBlockType);
/* Runtime-only invalid block type.  Saving the stage writes this as air. */
void RpgStage_SetCellRuntimeError(RpgStage *stage, int row, int column, int brokenBlockType);
bool RpgStage_Load(const char *filePath, RpgStage *stage);
bool RpgStage_Save(const char *filePath, const RpgStage *stage);
int RpgStage_GetMapCount(const RpgStage *stage);
bool RpgStage_IsMapActive(const RpgStage *stage, int mapIndex);
int RpgStage_GetMapAtGrid(const RpgStage *stage, int gridX, int gridY);
/* Runtime positions use the persistent two-dimensional map grid.  Storage slots
   are an implementation detail and must only be used when accessing arrays. */
int RpgStage_GetMapAtWorldPosition(const RpgStage *stage, Vector2 position);
bool RpgStage_GetWorldCellAtPosition(const RpgStage *stage, Vector2 position,
                                     int *storageRow, int *storageColumn);
Vector2 RpgStage_GetWorldPositionForCell(const RpgStage *stage, int row, int column);
Rectangle RpgStage_GetWorldBoundsForCell(const RpgStage *stage, int row, int column);
// 二次元ステージID (x, y) に最も近い、現在有効なステージスロットを返す。
int RpgStage_FindNearestActiveMapAtGrid(const RpgStage *stage, int gridX, int gridY);
// 無効になったスロットを参照している場合も、二次元IDを基準に最寄りへ補正する。
int RpgStage_FindNearestActiveMap(const RpgStage *stage, int mapIndex);
int RpgStage_GetAdjacentMap(const RpgStage *stage, int mapIndex, RpgAreaDirection direction);
/* Retained as a source-compatible no-op.  Runtime coordinates are world-space. */
void RpgStage_SetSpatialReferenceMap(RpgStage *stage, int mapIndex);
int RpgStage_GetOrCreateAdjacentMap(RpgStage *stage, int mapIndex, RpgAreaDirection direction);
/* 指定方向の座標列／行を空けて新エリアを挿入する。既存エリアは必要に応じて一マスずらす。 */
int RpgStage_InsertAdjacentMap(RpgStage *stage, int mapIndex, RpgAreaDirection direction);
bool RpgStage_RemoveMap(RpgStage *stage, int mapIndex);
/* Moves an existing area slot to an unoccupied 2D map coordinate without
   relocating the map's stored blocks or owned objects. */
bool RpgStage_MoveMapToGrid(RpgStage *stage, int mapIndex, int gridX, int gridY);
/* Exchanges the user-facing 2D positions of two existing area slots.  The
 * slots themselves (and therefore their cells, attachments and per-area
 * files) stay intact; callers translate separately owned world-position
 * objects with the same two area offsets. */
bool RpgStage_SwapMapGridPositions(RpgStage *stage, int firstMapIndex, int secondMapIndex);
bool RpgStage_SetBlockAtPosition(RpgStage *stage, Vector2 position, bool isBlock);
bool RpgStage_SetBlockTypeAtPosition(RpgStage *stage, Vector2 position, int blockType);
int RpgStage_GetBlockTypeAtPosition(const RpgStage *stage, Vector2 position);
// 指定マスを含むドア全体を開閉状態へ切り替える。ドア以外では何もしない。
bool RpgStage_SetDoorOpenAtCell(RpgStage *stage, int row, int column, bool isOpen);
/* Key-door records always belong to an effect shape's representative cell.
   Stale records from older per-cell saves are normalized here. */
void RpgStage_NormalizeKeyDoors(RpgStage *stage);
RpgKeyDoor *RpgStage_EnsureKeyDoor(RpgStage *stage, int row, int column);
RpgKeyDoor *RpgStage_GetKeyDoorAtCell(RpgStage *stage, int row, int column);
const RpgKeyDoor *RpgStage_GetKeyDoorAtCellConst(const RpgStage *stage, int row, int column);
/* Assign/retain stable object IDs for both fixed shapes and variable
 * rectangles.  It imports old `magnet` records and old cell-only magnets. */
void RpgStage_NormalizeCompositeObjects(RpgStage *stage);
/* Returns a stable object ID for any composite component, or zero for a
 * single-cell block.  The ID is the sole folder/eat/spit identity. */
int RpgStage_GetCompositeObjectIdAtCell(const RpgStage *stage, int row, int column);
/* The caller has already moved the component cells.  Move the persistent
 * fixed-composite record to the same root so folder ownership and incremental
 * preview generation see one object move rather than unrelated cell edits. */
bool RpgStage_RelocateCompositeObjectById(RpgStage *stage, int objectId,
                                          int destinationRow, int destinationColumn);
/* Collect all component cells of the one composite containing `row,column`.
 * Returns false for a one-cell block or an insufficient output buffer. */
bool RpgStage_GetCompositeCellsAtCell(const RpgStage *stage, int row, int column,
                                      RpgGridCell *cells, int cellCapacity, int *cellCount);
/* Set/restore all tiles of one composite atomically. */
bool RpgStage_SetCompositeMissingAtCell(RpgStage *stage, int row, int column, bool missing);
bool RpgStage_IsCompositeRectangle(const RpgCompositeObject *object);
const RpgCompositeObject *RpgStage_GetCompositeAtCellConst(const RpgStage *stage, int row, int column);
RpgCompositeObject *RpgStage_GetCompositeAtCell(RpgStage *stage, int row, int column);
/* Magnet-named accessors retain a narrow gameplay API, but return the common
 * composite record and never access a separate magnet collection. */
const RpgCompositeObject *RpgStage_GetMagnetAtCellConst(const RpgStage *stage, int row, int column);
RpgCompositeObject *RpgStage_GetMagnetAtCell(RpgStage *stage, int row, int column);
bool RpgStage_AddMagnet(RpgStage *stage, int row, int column, int width, int height, int blockType);
bool RpgStage_RemoveMagnetAtCell(RpgStage *stage, int row, int column);
bool RpgStage_MoveMagnetAtCell(RpgStage *stage, int row, int column, int destinationRow,
                               int destinationColumn);
bool RpgStage_ResizeMagnetAtCell(RpgStage *stage, int row, int column, int oppositeRow,
                                 int oppositeColumn);
/* Resize one magnet to explicit inclusive cell bounds.  Unlike the legacy
 * opposite-corner helper this supports dragging any one of the four edges. */
bool RpgStage_ResizeMagnetBoundsAtCell(RpgStage *stage, int row, int column,
                                       int minimumRow, int minimumColumn,
                                       int maximumRow, int maximumColumn);
bool RpgStage_SetReferencePathAtCell(RpgStage *stage, int row, int column, const char *path);
const char *RpgStage_GetReferencePathAtCell(const RpgStage *stage, int row, int column);
#if 0 /* obsolete pre-movable reference-object declaration */
enum { RPG_REFERENCE_OBJECT_MAX_COUNT = 32 };
typedef struct RpgReferenceObject {
    Vector2 position;
    char path[RPG_STAGE_REFERENCE_PATH_LENGTH];
    bool isFalling;
    float fallSpeed;
    /* Gで取得したFileはステージ上の対象から独立し、プレイヤー後方を小さく追従する。 */
    bool followsPlayer;
    float drawScale;
    /* G取得時は圧縮アニメーション完了後にだけ追従へ移行する。 */
    bool isCompressing;
    bool isCompressed;
    float compressionElapsed;
} RpgReferenceObject;
typedef struct RpgReferenceObjects { int count; RpgReferenceObject entries[RPG_REFERENCE_OBJECT_MAX_COUNT]; } RpgReferenceObjects;
#endif
typedef enum RpgReferenceTargetKind { RPG_REFERENCE_TARGET_NONE, RPG_REFERENCE_TARGET_CELL, RPG_REFERENCE_TARGET_DROP } RpgReferenceTargetKind;
typedef struct RpgReferenceTarget { RpgReferenceTargetKind kind; int row; int column; int dropIndex; } RpgReferenceTarget;
RpgReferenceObjects RpgReferenceObjects_Default(void);
bool RpgReferenceObjects_Add(RpgReferenceObjects *objects, RpgReferenceObjectKind objectKind,
                             Vector2 position, const char *path, int id);
/* sourcePath is the stable author-selected identity.  Old records may only
   have a disposable build path, so derive a normalized filename identity
   before runtime systems (notably key doors) use them. */
void RpgReferenceObjects_EnsureSourcePaths(RpgReferenceObjects *objects);
/* Compare a movable FILE with an authored file identity (for example a key
   door's keyPath). Both stored source identities and disposable runtime
   paths are accepted, but only normalized file names participate. */
bool RpgReferenceObjects_FileMatchesPath(const RpgReferenceObject *object,
                                         const char *authoredPath);
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
/* Fileの追従先とは別に、現在の可動Object一覧から近くのFolderだけを検索する。 */
bool RpgReferenceObjects_FindNearbyFolderTarget(const RpgReferenceObjects *objects, Vector2 position,
                                                float distance, RpgReferenceTarget *target);
int RpgReferenceObjects_FindFollowerIndex(const RpgReferenceObjects *objects);
bool RpgReferenceObjects_FindTarget(const RpgStage *stage, const RpgReferenceObjects *objects,
                                    Vector2 position, RpgReferenceTarget *target);
const char *RpgReferenceObjects_GetTargetPath(const RpgStage *stage, const RpgReferenceObjects *objects,
                                               RpgReferenceTarget target);
/* A movable FILE's runtime path is rebuilt for every Play session.  UI labels
   must instead prefer its stable author-selected sourcePath so a missing or
   delayed build copy never changes the visible name to a generic fallback. */
const char *RpgReferenceObjects_GetTargetDisplayPath(const RpgStage *stage,
                                                     const RpgReferenceObjects *objects,
                                                     RpgReferenceTarget target);
bool RpgReferenceObjects_RemoveTarget(RpgStage *stage, RpgReferenceObjects *objects,
                                      RpgReferenceTarget target);
bool RpgStage_IsSolidBlock(int blockType);
void RpgStage_SetSocketSignalActive(RpgStage *stage, int mapIndex, bool isActive);
bool RpgStage_IsSocketSignalActive(const RpgStage *stage, int mapIndex);
bool RpgStage_IsSolidAtPosition(const RpgStage *stage, Vector2 position);
bool RpgStage_CheckSolidCollision(const RpgStage *stage, Rectangle bounds);
/* Collision geometry normally belongs to the stage.  The optional attachment
   view only subtracts socket recesses from their supporting tiles. */
void RpgStage_SetCollisionAttachments(const struct RpgAttachments *attachments);
bool RpgStage_FindSolidCollisionCenter(const RpgStage *stage, Rectangle bounds, Vector2 *center);
/* 一方向床を上から横切った落下だけを検出し、着地すべき足元Y座標を返す。 */
bool RpgStage_FindOneWayPlatformLanding(const RpgStage *stage, Rectangle previousBounds,
                                        Rectangle candidateBounds, float *landingY);
/* 複数マス特殊ブロックの任意の構成マスから、ドラッグと共通の代表（先頭）マスを求める。 */
bool RpgStage_FindEffectRootCell(const RpgStage *stage, int row, int column, int *rootRow,
                                 int *rootColumn, const RpgEffectShape **shapeResult);
/* Static stage data may store only a composite block's representative cell.
   Recreate its empty component cells before systems query the shape. */
void RpgStage_MaterializeCompositeBlocks(RpgStage *stage);
bool RpgStage_CheckSolidCircleCollision(const RpgStage *stage, Vector2 center, float radius);
bool RpgStage_FindSolidCircleCollisionCenter(const RpgStage *stage, Vector2 center, float radius,
                                             Vector2 *collisionCenter);
Color RpgStage_GetBlockColor(int blockType);
/* The caller owns the texture lifetime; RpgStage borrows it while drawing
   ordinary ground blocks. */
void RpgStage_SetGroundTexture(Texture2D texture);
/* Values are normalized 0..1; 0.50 is the neutral hue/saturation/lightness. */
void RpgStage_SetGroundAppearance(float hue, float saturation, float lightness);
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
void RpgStage_DrawWithAttachments(const RpgStage *stage,
                                  const struct RpgAttachments *attachments,
                                  bool showGrid, float brightness);
void RpgStage_DrawMap(const RpgStage *stage, int mapIndex, bool showGrid, float brightness);
/* Variant used by gameplay/editor rendering: top-mounted block sockets leave
   a transparent recess in their supporting terrain cell. */
void RpgStage_DrawMapWithAttachments(const RpgStage *stage,
                                     const struct RpgAttachments *attachments,
                                     int mapIndex, bool showGrid, float brightness);
/* Error cells are normally included in the terrain pass.  Draw them again
   after routes when an error must visually cut a wire or trajectory. */
void RpgStage_DrawUnavailableOverlay(const RpgStage *stage, bool showGrid, float brightness);
void RpgStage_DrawMapUnavailableOverlay(const RpgStage *stage, int mapIndex,
                                        bool showGrid, float brightness);
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
