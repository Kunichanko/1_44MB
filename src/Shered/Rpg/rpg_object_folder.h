// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_data_shot.h, rpg_item.h, rpg_stage.h
#ifndef RPG_OBJECT_FOLDER_H
#define RPG_OBJECT_FOLDER_H

#include <stddef.h>

#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_stage.h"

typedef struct RpgObjectFolder { RpgGridCell cell; } RpgObjectFolder;

/* Zipper への格納先を返す。移動本体は通常Folder格納と同じ StoreFileInDirectory を使用する。 */
bool RpgObjectFolder_GetZipperInboxDirectory(char *path, size_t pathSize);
/* Zipper 内の実ファイル容量を byte 単位で返す。変更通知時だけ再走査する。 */
unsigned long long RpgObjectFolder_GetZipperStorageBytes(void);
/* 追従Fileをゲーム内Folderへ格納する。ファイル操作は描画処理から独立させる。 */
/* ステージFileの永続コピーを残したまま、Folderへ同名ファイルを格納する。 */
bool RpgObjectFolder_StoreFileInDirectory(const char *sourcePath, const char *destinationDirectory);
bool RpgObjectFolder_OpenZipperDirectory(void);
/* Folder を移動せずに Zipper 構造へ更新し、以後の Inbox と Explorer のルートに採用する。 */
bool RpgObjectFolder_ActivateReferenceFolderAsZipper(RpgStage *stage, RpgGridCell cell);
void RpgObjectFolder_PrepareZipperAnimationCommand(void);
// cmd の実行要求を一度だけ受け取る。アニメーション・ゲーム機能の内容は呼び出し側で独立して処理する。
bool RpgObjectFolder_BeginZipperCommandRequest(void);
bool RpgObjectFolder_CompleteZipperCommandRequest(void);

// Zipper 操作は複製ではなく、対象フォルダそのものを Inbox へ移動して行う。
bool RpgObjectFolder_MoveAttachmentToZipper(const RpgAttachment *attachment);
bool RpgObjectFolder_MoveDataShotToZipper(RpgDataShot *shot);
bool RpgObjectFolder_MoveBlockToZipper(const RpgObjectFolder *folder, int blockType);
/* 返却演出中は build の親（StageN）へ一時移動し、演出完了時に Return で build へ確定する。 */
bool RpgObjectFolder_BeginReturnAttachmentFromZipper(const RpgAttachment *attachment);
bool RpgObjectFolder_BeginReturnDataShotFromZipper(const RpgDataShot *shot);
bool RpgObjectFolder_BeginReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_ReturnAttachmentFromZipper(const RpgAttachment *attachment);
bool RpgObjectFolder_ReturnDataShotFromZipper(const RpgDataShot *shot);
bool RpgObjectFolder_ReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_RestoreDataShotFromMetadata(RpgDataShot *shot);

// フォルダ寿命はオブジェクト寿命と一致する。メタデータだけの通常ブロックには生成しない。
void RpgObjectFolders_PrepareAttachmentFolders(const RpgAttachments *attachments);
void RpgObjectFolders_PrepareReferenceFolderMetadata(const RpgStage *stage);
/* PNG配置物はマスを占有せず、データ弾と同じ build/objects 配下の所有フォルダを使う。 */
void RpgObjectFolders_PrepareImageObjectFolders(const RpgImageObjects *objects);
void RpgObjectFolders_UpdateDataShotLifetimes(RpgDataShots *shots, const RpgAttachments *attachments,
                                              RpgReferenceObjects *referenceObjects);
bool RpgObjectFolder_AttachmentHasLinkedFiles(const RpgAttachment *attachment);
bool RpgObjectFolder_DataShotHasLinkedFiles(const RpgDataShot *shot);
bool RpgObjectFolder_BlockHasLinkedFiles(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_HasLinkedFiles(const RpgObjectFolder *folder);
bool RpgObjectFolder_MoveAttachmentFolder(const RpgAttachment *from, const RpgAttachment *to);
void RpgObjectFolder_RemoveAttachmentFolder(const RpgAttachment *attachment);

/* 指定ステージの build を生成し、その中をオブジェクトフォルダの保存先として選択する。 */
bool RpgObjectFolders_BeginStageBuild(int stageNumber, RpgStage *stage,
                                      const RpgAttachments *attachments, Vector2 playerStartPosition,
                                      bool isSimpleBuild,
                                      char *buildPath, size_t buildPathSize);
/* 続きから用。静的ステージを再生成せず、残っている本編用オブジェクトフォルダを操作対象に戻す。 */
bool RpgObjectFolders_ResumeStageBuild(int stageNumber, RpgStage *stage, char *buildPath, size_t buildPathSize);
/* build/drops に残る File オブジェクトを、続きからの実行時オブジェクトへ復元する。 */
void RpgObjectFolders_LoadReferenceDrops(RpgReferenceObjects *objects);
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
