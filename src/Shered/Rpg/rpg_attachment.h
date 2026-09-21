// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_grid_path.h, rpg_stage.h
#ifndef RPG_ATTACHMENT_H
#define RPG_ATTACHMENT_H

#include <stdbool.h>

#include "rpg_block_inventory.h"
#include "rpg_grid_path.h"

struct RpgStageBackground;
#include "rpg_stage.h"

enum { RPG_ATTACHMENT_MAX_COUNT = 64, RPG_BLOCK_SOCKET_RECESS_DEPTH = 4 };

typedef struct RpgAttachment {
    int type;
    // 保存される固有 ID。移動しても実フォルダの識別番号は変えない。
    int folderId;
    RpgGridCell cell;
    RpgGridSide side;
    float dataSize;
    float dataSpeed;
    float dataInterval;
    bool dataPreviewEnabled;
    // 実弾のファイルごとの増分。32pxマスの半分(16px)単位へ正規化する。
    float sizePerFile; /* 8px（1マスの1/4）単位で正規化する。 */
    float speedPerKilobyte;
    // プレビュー専用の仮想フォルダ内容。実際のフォルダや実弾には影響しない。
    int previewFileCount;
    unsigned long long previewTotalBytes;
    RpgGridPath dataPath;
    /* Block-socket-only visual settings.  The right source endpoint uses the
       positive angle; the left endpoint uses the matching negative angle. */
    float socketRightLightAngle;
    float socketLightOpacity;
    bool flagRaised;
    /* Runtime-only shooter animation gate.  It is intentionally not serialized. */
    float shooterAnimationElapsed;
    /* Zipperが実フォルダを保持している間は、配置物を描画・機能とも停止する。 */
    bool isZipperHeld;
} RpgAttachment;
typedef struct RpgAttachments {
    int count;
    RpgAttachment entries[RPG_ATTACHMENT_MAX_COUNT];
} RpgAttachments;

RpgAttachments RpgAttachments_Default(void);
bool RpgAttachments_Load(const char *filePath, RpgAttachments *attachments);
bool RpgAttachments_Save(const char *filePath, const RpgAttachments *attachments);
bool RpgAttachments_Add(RpgAttachments *attachments, const RpgStage *stage, int type,
                        RpgGridCell cell, RpgGridSide side);
bool RpgAttachments_Remove(RpgAttachments *attachments, RpgAttachment attachment);
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
                                        int mapIndex);
int RpgAttachments_FindTouchedSaveFlag(const RpgAttachments *attachments, Vector2 playerPosition);
int RpgAttachments_FindTouchedSaveFlagWorld(const RpgAttachments *attachments, const RpgStage *stage,
                                            Vector2 playerPosition);
bool RpgAttachments_SetRaisedSaveFlag(RpgAttachments *attachments, int flagId);
bool RpgAttachments_StartShooterAnimation(RpgAttachments *attachments, int attachmentIndex);
void RpgAttachments_UpdateShooterAnimations(RpgAttachments *attachments, float deltaTime);
float RpgAttachments_GetShooterAnimationProgress(const RpgAttachment *attachment);
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
void RpgAttachments_RemoveBroken(RpgAttachments *attachments, const RpgStage *stage);
void RpgAttachments_Draw(const RpgAttachments *attachments);
void RpgAttachments_DrawMap(const RpgAttachments *attachments, int mapIndex);
void RpgAttachments_DrawMapExcept(const RpgAttachments *attachments, int mapIndex, int excludedIndex);
void RpgAttachments_DrawGhost(int type, Vector2 position, RpgGridSide side, bool isSnapped);

#endif
// 役割: ブロック設置物のデータと操作 API を宣言する。
