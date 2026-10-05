// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_grid_path.h, rpg_stage.h
#ifndef RPG_ATTACHMENT_H
#define RPG_ATTACHMENT_H

#include <stdbool.h>

#include "rpg_block_inventory.h"
#include "rpg_character.h"
#include "rpg_grid_path.h"
#include "rpg_wire.h"

struct RpgStageBackground;
#include "rpg_stage.h"

/* Editable attachments are not limited by a palette-sized fixed array. */
enum {
    /* Receiver is a normal attachment data record.  It intentionally is not
       a terrain block type: its parent terrain owns collision and folders. */
    RPG_ATTACHMENT_TYPE_RECEIVER = 204,
    RPG_ATTACHMENT_INITIAL_CAPACITY = 16,
    RPG_ATTACHMENT_LIMIT = RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS * 4,
    RPG_BLOCK_SOCKET_RECESS_DEPTH = 4,
    /* Physics moves blocks at sub-tile precision.  A nearly centred block is
       seated into the socket, rather than requiring a fragile sub-pixel match. */
    RPG_BLOCK_SOCKET_HORIZONTAL_SNAP_TOLERANCE = 3,
    RPG_BLOCK_SOCKET_VERTICAL_SNAP_TOLERANCE = 3
};

typedef struct RpgAttachment {
    int type;
    // 保存される固有 ID。移動しても実フォルダの識別番号は変えない。
    int folderId;
    RpgGridCell cell;
    RpgGridSide side;
    /* Transitional editor facade. Runtime and v12 persistence use the
       per-kind modules below; this cache keeps the legacy inspector source
       isolated until its last direct field references are removed. */
    float dataSize, dataSpeed, dataInterval;
    bool dataPreviewEnabled;
    float speedPerKilobyte;
    unsigned long long previewTotalBytes;
    RpgGridPath dataPath;
    float socketRightLightAngle, socketLightOpacity;
    bool flagStartZipperConnected, flagRaised;
    float shooterAnimationElapsed;
    // 実弾のファイルごとの増分。32pxマスの半分(16px)単位へ正規化する。
    float sizePerFile; /* 8px（1マスの1/4）単位で正規化する。 */
    // プレビュー専用の仮想フォルダ内容。実際のフォルダや実弾には影響しない。
    int previewFileCount;
    /* Block-socket-only visual settings.  The right source endpoint uses the
       positive angle; the left endpoint uses the matching negative angle. */
    /* 旗からエディタープレイを始める時だけ使う初期状態。通常の続きからには関与しない。 */
    /* Zipperが実フォルダを保持している間は、配置物を描画・機能とも停止する。 */
    bool isZipperHeld;
    /* This child stays inside its owning block folder.  Parent capture must
       therefore disable it without treating it as another captured object. */
    bool isOwnerBlockZipperHeld;
} RpgAttachment;

/* Per-kind modules are keyed solely by attachmentId. The generic mount record
   only retains placement, ownership and zipper-capture state. */
typedef struct RpgShooterConfig {
    int attachmentId;
    float dataSize, dataSpeed, dataInterval;
    bool dataPreviewEnabled;
    float sizePerFile, speedPerKilobyte;
    int previewFileCount;
    unsigned long long previewTotalBytes;
    RpgGridPath path;
    float animationElapsed;
} RpgShooterConfig;
typedef struct RpgFlagConfig { int attachmentId; bool startZipperConnected; bool raised; } RpgFlagConfig;
typedef struct RpgSocketConfig { int attachmentId; float rightLightAngle, lightOpacity; } RpgSocketConfig;

/* Persisted route configuration.  This is deliberately separate from an
   attachment's data-shot path: routes are shared topology for electric
   propagation and conveyors, while dataPath belongs only to a shooter. */
typedef struct RpgAttachmentRoute {
    int id;
    RpgWireKind kind;
    int ownerAttachmentId; /* receiver attachment id for electric routes; 0 otherwise */
    RpgGridCell ownerCell; /* representative terrain cell, notably for conveyors */
    RpgGridPath cells;
    float conveyorSpeed;
    int conveyorDirection;
    bool conveyorHasFloor;
    float conveyorSlideAngleDegrees;
} RpgAttachmentRoute;
typedef struct RpgAttachments {
    int count;
    int capacity;
    RpgAttachment *entries;
    int routeCount;
    int routeCapacity;
    RpgAttachmentRoute *routes;
    int shooterCount, shooterCapacity;
    RpgShooterConfig *shooters;
    int flagCount, flagCapacity;
    RpgFlagConfig *flags;
    int socketCount, socketCapacity;
    RpgSocketConfig *sockets;
} RpgAttachments;

RpgAttachments RpgAttachments_Default(void);
void RpgAttachments_Destroy(RpgAttachments *attachments);
bool RpgAttachments_Reserve(RpgAttachments *attachments, int requiredCapacity);
bool RpgAttachments_Append(RpgAttachments *attachments, const RpgAttachment *attachment);
bool RpgAttachments_Clone(RpgAttachments *destination, const RpgAttachments *source);
bool RpgAttachments_IsReceiver(const RpgAttachment *attachment);
RpgShooterConfig *RpgAttachments_FindShooter(RpgAttachments *attachments, int attachmentId);
const RpgShooterConfig *RpgAttachments_FindShooterConst(const RpgAttachments *attachments, int attachmentId);
RpgFlagConfig *RpgAttachments_FindFlag(RpgAttachments *attachments, int attachmentId);
const RpgFlagConfig *RpgAttachments_FindFlagConst(const RpgAttachments *attachments, int attachmentId);
RpgSocketConfig *RpgAttachments_FindSocket(RpgAttachments *attachments, int attachmentId);
const RpgSocketConfig *RpgAttachments_FindSocketConst(const RpgAttachments *attachments, int attachmentId);
void RpgAttachments_SynchronizeModuleConfigs(RpgAttachments *attachments);
void RpgAttachments_RefreshEditorFacades(RpgAttachments *attachments);
/* Static route ownership lives here. RpgWires is rebuilt as a runtime cache. */
void RpgAttachments_ImportWires(RpgAttachments *attachments, const RpgWires *wires);
void RpgAttachments_BuildRuntimeWires(const RpgAttachments *attachments, RpgWires *wires);
bool RpgAttachments_Load(const char *filePath, RpgAttachments *attachments);
bool RpgAttachments_Save(const char *filePath, const RpgAttachments *attachments);
/* Static stage ownership format. v10 stores every attachment, including
   receivers, followed by its paths.  v9 receiver records are imported into
   attachment records so the old split runtime storage is never recreated. */
bool RpgAttachments_LoadStatic(const char *filePath, RpgAttachments *attachments,
                               RpgWires *wires, bool *isUnifiedFormat);
bool RpgAttachments_SaveStatic(const char *filePath, const RpgAttachments *attachments,
                               const RpgWires *wires);
bool RpgAttachments_Add(RpgAttachments *attachments, RpgStage *stage, int type,
                        RpgGridCell cell, RpgGridSide side);
bool RpgAttachments_Remove(RpgAttachments *attachments, RpgStage *stage, RpgAttachment attachment);
bool RpgAttachments_Replace(RpgAttachments *attachments, RpgStage *stage, int index,
                            RpgAttachment replacement);
/* Converts legacy config-only shooter/flag records into their real owning
   stage cells.  Safe to call after every static load. */
void RpgAttachments_MaterializeBlockCells(RpgAttachments *attachments, RpgStage *stage);
bool RpgAttachments_GetOwnerBlockCell(const RpgAttachment *attachment, RpgGridCell *cell);
bool RpgAttachments_IsOwnedByBlock(const RpgAttachment *attachment, RpgGridCell blockCell);
bool RpgAttachments_IsRuntimeUnavailable(const RpgAttachment *attachment);
void RpgAttachments_SetOwnerBlockZipperHeld(RpgAttachments *attachments, RpgGridCell blockCell,
                                             bool isHeld);
void RpgAttachments_MigrateLegacyButtons(RpgAttachments *attachments, RpgStage *stage);
bool RpgAttachments_IsButtonPressed(const RpgAttachments *attachments, Vector2 playerPosition);
bool RpgAttachments_IsButtonPressedWorld(const RpgAttachments *attachments, const RpgStage *stage,
                                         Vector2 playerPosition);
/* Returns the top-mounted socket whose empty cell is completely covered by a
   movable block.  The returned index is stable for this runtime session. */
int RpgAttachments_FindBlockSocketAtBoundsWorld(const RpgAttachments *attachments,
                                                const RpgStage *stage, Rectangle blockBounds);
/* True when this supporting terrain cell contains a socket recess. */
bool RpgAttachments_HasBlockSocketAtBaseCell(const RpgAttachments *attachments, int row, int column);
/* Repaints only the open centre of top-mounted block sockets with the already
   selected stage background.  The sensor rim is drawn later by DrawMap(). */
void RpgAttachments_DrawBlockSocketRecesses(const RpgAttachments *attachments, int mapIndex,
                                            const struct RpgStageBackground *background,
                                            Rectangle mapBounds, float brightness);
void RpgAttachments_DrawSocketLightsMap(const RpgAttachments *attachments, const RpgStage *stage,
                                        int mapIndex, const RpgMovingSolidSet *movingSolids);
int RpgAttachments_FindTouchedSaveFlag(const RpgAttachments *attachments, Vector2 playerPosition);
int RpgAttachments_FindTouchedSaveFlagWorld(const RpgAttachments *attachments, const RpgStage *stage,
                                            Vector2 playerPosition);
bool RpgAttachments_SetRaisedSaveFlag(RpgAttachments *attachments, int flagId);
bool RpgAttachments_StartShooterAnimation(RpgAttachments *attachments, int attachmentIndex);
void RpgAttachments_UpdateShooterAnimations(RpgAttachments *attachments, float deltaTime);
float RpgAttachments_GetShooterAnimationProgress(const RpgAttachments *attachments, int attachmentId);
bool RpgAttachments_IsCellOccupied(const RpgAttachments *attachments, RpgGridCell cell);
bool RpgAttachments_GetOccupiedCell(const RpgAttachment *attachment, RpgGridCell *cell);
int RpgAttachments_FindAtPosition(const RpgAttachments *attachments, Vector2 position, float distance);
/* Attachments are stored in packed cell coordinates, while the connected
 * stage uses a two-dimensional world.  Editor/runtime pointer picking must
 * use this helper rather than comparing a world pointer with storage space. */
int RpgAttachments_FindAtWorldPosition(const RpgAttachments *attachments, const RpgStage *stage,
                                       Vector2 position, float distance);
bool RpgAttachments_FindSnap(const RpgAttachments *attachments, const RpgStage *stage, int type,
                             Vector2 position, int ignoredAttachmentIndex, RpgAttachment *attachment);
bool RpgAttachments_MoveDataPathEndpoint(RpgAttachments *attachments, const RpgStage *stage,
                                          int attachmentIndex, int row, int column);
bool RpgAttachments_FindDataPathEndpoint(const RpgAttachments *attachments, int row, int column,
                                         int *attachmentIndex);
// 支持ブロックと空気マスの境界にある取付基準点。描画・選択・当たり判定で共用する。
Vector2 RpgAttachments_GetPosition(const RpgAttachment *attachment, int firstColumn);
// 保存旗の土台ブロック上へ、キャラクターの足元基準で復帰させる座標を返す。
Vector2 RpgAttachments_GetSaveFlagRespawnPosition(const RpgAttachment *attachment);
Vector2 RpgAttachments_GetSaveFlagRespawnPositionWorld(const RpgAttachment *attachment,
                                                        const RpgStage *stage);
void RpgAttachments_DrawDataPaths(const RpgAttachments *attachments, int mapIndex);
void RpgAttachments_RemoveBroken(RpgAttachments *attachments, RpgStage *stage);
void RpgAttachments_Draw(const RpgAttachments *attachments);
void RpgAttachments_DrawMap(const RpgAttachments *attachments, int mapIndex);
void RpgAttachments_DrawMapExcept(const RpgAttachments *attachments, int mapIndex, int excludedIndex);
void RpgAttachments_DrawGhost(int type, Vector2 position, RpgGridSide side, bool isSnapped);

#endif
// 役割: ブロック設置物のデータと操作 API を宣言する。
