// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_grid_path.h, rpg_stage.h
#ifndef RPG_ATTACHMENT_H
#define RPG_ATTACHMENT_H

#include <stdbool.h>

#include "rpg_block_inventory.h"
#include "rpg_grid_path.h"
#include "rpg_stage.h"

enum { RPG_ATTACHMENT_MAX_COUNT = 64 };

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
    bool flagRaised;
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
int RpgAttachments_FindTouchedSaveFlag(const RpgAttachments *attachments, Vector2 playerPosition);
bool RpgAttachments_SetRaisedSaveFlag(RpgAttachments *attachments, int flagId);
bool RpgAttachments_IsCellOccupied(const RpgAttachments *attachments, RpgGridCell cell);
bool RpgAttachments_GetOccupiedCell(const RpgAttachment *attachment, RpgGridCell *cell);
int RpgAttachments_FindAtPosition(const RpgAttachments *attachments, Vector2 position, float distance);
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
void RpgAttachments_DrawDataPaths(const RpgAttachments *attachments, int mapIndex);
void RpgAttachments_RemoveBroken(RpgAttachments *attachments, const RpgStage *stage);
void RpgAttachments_Draw(const RpgAttachments *attachments);
void RpgAttachments_DrawMap(const RpgAttachments *attachments, int mapIndex);
void RpgAttachments_DrawMapExcept(const RpgAttachments *attachments, int mapIndex, int excludedIndex);
void RpgAttachments_DrawGhost(int type, Vector2 position, RpgGridSide side, bool isSnapped);

#endif
// 役割: ブロック設置物のデータと操作 API を宣言する。
