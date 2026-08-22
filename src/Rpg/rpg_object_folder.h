// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_data_shot.h, rpg_item.h, rpg_stage.h
#ifndef RPG_OBJECT_FOLDER_H
#define RPG_OBJECT_FOLDER_H

#include <stddef.h>

#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_stage.h"

typedef struct RpgObjectFolder { RpgGridCell cell; } RpgObjectFolder;

bool RpgObjectFolder_CopyFileToZipperInbox(const char *sourcePath);
bool RpgObjectFolder_OpenZipperDirectory(void);
void RpgObjectFolder_PrepareZipperAnimationCommand(void);
// cmd の実行要求を一度だけ受け取る。アニメーション・ゲーム機能の内容は呼び出し側で独立して処理する。
bool RpgObjectFolder_BeginZipperCommandRequest(void);
bool RpgObjectFolder_CompleteZipperCommandRequest(void);

// Zipper 操作は複製ではなく、対象フォルダそのものを Inbox へ移動して行う。
bool RpgObjectFolder_MoveAttachmentToZipper(const RpgAttachment *attachment);
bool RpgObjectFolder_MoveDataShotToZipper(const RpgDataShot *shot);
bool RpgObjectFolder_MoveBlockToZipper(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_ReturnAttachmentFromZipper(const RpgAttachment *attachment);
bool RpgObjectFolder_ReturnDataShotFromZipper(const RpgDataShot *shot);
bool RpgObjectFolder_ReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType);

// フォルダ寿命はオブジェクト寿命と一致する。メタデータだけの通常ブロックには生成しない。
void RpgObjectFolders_PrepareAttachmentFolders(const RpgAttachments *attachments);
void RpgObjectFolders_UpdateDataShotLifetimes(RpgDataShots *shots, const RpgAttachments *attachments,
                                              RpgReferenceObjects *referenceObjects);
bool RpgObjectFolder_AttachmentHasLinkedFiles(const RpgAttachment *attachment);
bool RpgObjectFolder_DataShotHasLinkedFiles(const RpgDataShot *shot);
bool RpgObjectFolder_BlockHasLinkedFiles(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_HasLinkedFiles(const RpgObjectFolder *folder);
bool RpgObjectFolder_MoveAttachmentFolder(const RpgAttachment *from, const RpgAttachment *to);
void RpgObjectFolder_RemoveAttachmentFolder(const RpgAttachment *attachment);

/* 指定ステージの build を生成し、その中をオブジェクトフォルダの保存先として選択する。 */
bool RpgObjectFolders_BeginStageBuild(int stageNumber, const RpgStage *stage,
                                      const RpgAttachments *attachments, Vector2 playerStartPosition,
                                      char *buildPath, size_t buildPathSize);
bool RpgObjectFolders_IsStageBuildActive(void);
void RpgObjectFolders_UpdateBuildCellGeneration(void);
bool RpgObjectFolders_ReadCompactCellAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS]);
bool RpgObjectFolders_IsBuildCellAvailable(RpgGridCell cell);
void RpgObjectFolders_RefreshBuildCellLinkedFiles(RpgGridCell cell);
void RpgObjectFolders_ClearBuildCellLinkedFiles(void);
void RpgObjectFolders_EndStageBuild(void);
void RpgObjectFolders_ClearSessionStorage(void);

#endif
// 役割: オブジェクト所有フォルダの生成・移動・消滅 API を宣言する。
