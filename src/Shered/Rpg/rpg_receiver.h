// 依存する自プロジェクト内ファイル: rpg_grid_path.h, rpg_stage.h
#ifndef RPG_RECEIVER_H
#define RPG_RECEIVER_H

#include <stdbool.h>

#include "rpg_attachment.h"

// 受容体は1ブロックにつき1個のため、ステージのマス数を上限にする。
typedef struct RpgReceiver {
    RpgGridCell cell;
    RpgGridSide side;
    /* Runtime-only: the owning terrain block is inside Zipper. */
    bool isOwnerBlockZipperHeld;
} RpgReceiver;
/* Compatibility façade only. Receiver data lives exclusively in its bound
 * RpgAttachments collection.  Existing systems can keep their receiver API
 * while no second receiver array can become stale. */
typedef struct RpgReceivers { RpgAttachments *attachments; } RpgReceivers;

RpgReceivers RpgReceivers_Default(void);
void RpgReceivers_Bind(RpgReceivers *receivers, RpgAttachments *attachments);
int RpgReceivers_Count(const RpgReceivers *receivers);
bool RpgReceivers_Get(const RpgReceivers *receivers, int receiverIndex, RpgReceiver *receiver);
bool RpgReceivers_Set(RpgReceivers *receivers, int receiverIndex, RpgReceiver receiver);
bool RpgReceivers_Add(RpgReceivers *receivers, const RpgStage *stage, RpgGridCell cell);
/* One-time import only for pre-v10 rpg_receivers.cfg files. */
bool RpgReceivers_LoadLegacy(const char *filePath, RpgReceivers *receivers);
int RpgReceivers_FindAtCell(const RpgReceivers *receivers, RpgGridCell cell);
int RpgReceivers_FindAtPosition(const RpgReceivers *receivers, Vector2 position, float distance);
bool RpgReceivers_CycleSide(RpgReceivers *receivers, int receiverIndex);
bool RpgReceivers_Remove(RpgReceivers *receivers, int receiverIndex);
void RpgReceivers_RemoveBroken(RpgReceivers *receivers, const RpgStage *stage);
bool RpgReceivers_IsRuntimeUnavailable(const RpgReceiver *receiver);
void RpgReceivers_SetOwnerBlockZipperHeld(RpgReceivers *receivers, RpgGridCell blockCell,
                                          bool isHeld);
void RpgReceivers_Draw(const RpgReceivers *receivers);
void RpgReceivers_DrawMap(const RpgReceivers *receivers, int mapIndex);

#endif
// 役割: 導線受容体の構造と操作 API を宣言する。
