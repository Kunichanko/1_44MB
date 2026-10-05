// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_data_shot.h, rpg_item.h, rpg_stage.h
#ifndef RPG_OBJECT_FOLDER_H
#define RPG_OBJECT_FOLDER_H

#include <stddef.h>

#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_receiver.h"
#include "rpg_stage.h"
#include "rpg_wire.h"

typedef struct RpgObjectFolder { RpgGridCell cell; } RpgObjectFolder;

typedef enum RpgZipperCommandRequest {
    RPG_ZIPPER_COMMAND_NONE = 0,
    RPG_ZIPPER_COMMAND_EAT,
    RPG_ZIPPER_COMMAND_SPIT
} RpgZipperCommandRequest;

/* Zipper への格納先を返す。移動本体は通常Folder格納と同じ StoreFileInDirectory を使用する。 */
bool RpgObjectFolder_GetZipperInboxDirectory(char *path, size_t pathSize);
/* Zipper 内の実ファイル容量を byte 単位で返す。変更通知時だけ再走査する。 */
unsigned long long RpgObjectFolder_GetZipperStorageBytes(void);
/* 追従Fileをゲーム内Folderへ格納する。ファイル操作は描画処理から独立させる。 */
/* ステージFileの永続コピーを残したまま、Folderへ同名ファイルを格納する。 */
bool RpgObjectFolder_StoreFileInDirectory(const char *sourcePath, const char *destinationDirectory);
/* ブロック固有の build/objects フォルダを、ランタイム格納先として取得する。 */
bool RpgObjectFolder_GetBlockDirectory(const RpgObjectFolder *folder, int blockType,
                                       char *path, size_t pathSize);
/* Runtime folder ownership is also the ownership relation used by composite
   blocks and their attachment metadata. */
bool RpgObjectFolders_HaveSameBlockOwner(RpgGridCell first, RpgGridCell second);
bool RpgObjectFolder_OpenZipperDirectory(void);
/* Creates and activates the runtime Zipper folder for an already
   connected Zipper.  The caller must have an active stage build. */
bool RpgObjectFolder_EnsureRuntimeZipperDirectory(void);
/* Folder を移動せずに Zipper 構造へ更新し、以後の Zipper ルートとして採用する。 */
bool RpgObjectFolder_ActivateReferenceFolderAsZipper(RpgStage *stage, RpgGridCell cell);
void RpgObjectFolder_PrepareZipperAnimationCommand(void);
// cmd の実行要求を一度だけ受け取る。アニメーション・ゲーム機能の内容は呼び出し側で独立して処理する。
bool RpgObjectFolder_BeginZipperCommandRequest(void);
RpgZipperCommandRequest RpgObjectFolder_GetPendingZipperCommandRequest(void);
bool RpgObjectFolder_CompleteZipperCommandRequest(void);

// Zipper 操作は複製ではなく、対象フォルダそのものを Zipper 直下へ移動して行う。
bool RpgObjectFolder_MoveDataShotToZipper(RpgDataShot *shot);
bool RpgObjectFolder_MoveBlockToZipper(const RpgObjectFolder *folder, int blockType);
/* Builds the parent folder as the immediate result of eating a block, then
   moves that one folder with every owned attachment/receiver metadata file
   inside it. */
bool RpgObjectFolder_MoveBlockWithAttachmentsToZipper(const RpgObjectFolder *folder, int blockType,
                                                       const RpgAttachments *attachments,
                                                       const RpgReceivers *receivers,
                                                       const RpgWires *wires);
/* A file object owns the small runtime folder which contains its real file.
   Move that folder as one unit so eat/spit never copies the file or creates a
   missing terrain cell. */
bool RpgObjectFolder_MoveReferenceFileToZipper(const RpgReferenceObject *object);
/* 返却演出中は build の親（StageN）へ一時移動し、演出完了時に Return で build へ確定する。 */
bool RpgObjectFolder_BeginReturnDataShotFromZipper(const RpgDataShot *shot);
bool RpgObjectFolder_BeginReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_ReturnDataShotFromZipper(const RpgDataShot *shot);
bool RpgObjectFolder_ReturnBlockFromZipper(const RpgObjectFolder *folder, int blockType);
bool RpgObjectFolder_BeginReturnReferenceFileFromZipper(const RpgReferenceObject *object);
bool RpgObjectFolder_ReturnReferenceFileFromZipper(const RpgReferenceObject *object);
/* Dynamic metal/push blocks own folders under build/objects, never under a
   terrain cell. Their original cell is identity only; position is metadata. */
bool RpgObjectFolder_EnsureDynamicBlock(RpgGridCell identityCell, int blockType, Vector2 position);
bool RpgObjectFolder_MoveDynamicBlockToZipper(RpgGridCell identityCell, int blockType, Vector2 position);
bool RpgObjectFolder_BeginReturnDynamicBlockFromZipper(RpgGridCell identityCell, int blockType,
                                                        Vector2 position);
bool RpgObjectFolder_ReturnDynamicBlockFromZipper(RpgGridCell identityCell, int blockType,
                                                   Vector2 position);
bool RpgObjectFolder_RestoreDataShotFromMetadata(RpgDataShot *shot);

// フォルダ寿命はオブジェクト寿命と一致する。メタデータだけの通常ブロックには生成しない。
/* Shared ownership serialization for normal build, preview repair, and eat. */
void RpgObjectFolders_PrepareBlockOwnedMetadata(const RpgAttachments *attachments,
                                                const RpgReceivers *receivers,
                                                const RpgWires *wires);
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
                                      const RpgAttachments *attachments,
                                      const RpgReceivers *receivers,
                                      const RpgWires *wires, Vector2 playerStartPosition,
                                      bool isSimpleBuild,
                                      char *buildPath, size_t buildPathSize);
/* 続きから用。静的ステージを再生成せず、残っている本編用オブジェクトフォルダを操作対象に戻す。 */
bool RpgObjectFolders_ResumeStageBuild(int stageNumber, RpgStage *stage, char *buildPath, size_t buildPathSize);
/* Editor preview cache counterpart.  It reconnects the already prepared
   Stage/editor/StageN runtime directory without recreating every cell. */
bool RpgObjectFolders_ResumeEditorPreviewBuild(int stageNumber, RpgStage *stage,
                                               char *buildPath, size_t buildPathSize);
/* build/drops に残る File オブジェクトを、続きからの実行時オブジェクトへ復元する。 */
void RpgObjectFolders_LoadReferenceDrops(RpgReferenceObjects *objects);
bool RpgObjectFolders_IsStageBuildActive(void);
void RpgObjectFolders_UpdateBuildCellGeneration(const RpgStage *stage);
/* An editor stop needs its current area plus its four direct neighbours
   immediately.  All other areas stay in the preview generation queue. */
bool RpgObjectFolders_EnsurePreviewNeighborhood(RpgStage *stage, int startMapIndex);
bool RpgObjectFolders_EnsureMapGenerated(RpgStage *stage, int mapIndex);
bool RpgObjectFolders_EnsureAllMapsGenerated(RpgStage *stage);
/* Fast editor-preview update for an edit proven to affect only compact
   ordinary cells.  Special-folder changes keep the normal rebuild route. */
bool RpgObjectFolders_RefreshEditorPreviewCompactCells(const RpgStage *stage);
/* Restore a detached editor preview after Play without deleting and rebuilding
   unchanged static cells.  Dynamic Zipper/data-shot artifacts are discarded;
   only missing static folders and metadata are repaired. */
bool RpgObjectFolders_RepairEditorPreview(const RpgStage *stage,
                                          const RpgAttachments *attachments,
                                          const RpgReceivers *receivers,
                                          const RpgWires *wires,
                                          Vector2 playerStartPosition);
/* Play marks this only when a static block/reference folder is actually
   moved or changed.  Stop can then skip the expensive static-cache audit for
   an untouched preview while still removing transient runtime artifacts. */
void RpgObjectFolders_BeginEditorPreviewRuntimeTracking(void);
bool RpgObjectFolders_HasEditorPreviewStaticMutations(void);
void RpgObjectFolders_MarkEditorPreviewStaticMutation(void);
void RpgObjectFolders_MarkEditorPreviewCellMutation(RpgGridCell cell, int blockType);
bool RpgObjectFolders_ReadCompactCellAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS]);
bool RpgObjectFolders_IsBuildCellAvailable(RpgGridCell cell);
void RpgObjectFolders_RefreshBuildCellLinkedFiles(RpgGridCell cell);
void RpgObjectFolders_ClearBuildCellLinkedFiles(void);
void RpgObjectFolders_EndStageBuild(void);
/* Editor preview stop: detach immediately.  Its generated artifacts are
   cleared synchronously by the next preview build, not by the Stop click. */
void RpgObjectFolders_AbandonStageBuild(void);
void RpgObjectFolders_ClearSessionStorage(void);

#endif
// 役割: オブジェクト所有フォルダの生成・移動・消滅 API を宣言する。
