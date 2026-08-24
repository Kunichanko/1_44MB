// 役割: StageN/build の生成と ReadDirectoryChangesW によるビルド出力の変更監視を担当する。
// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_stage.h
#ifndef RPG_STAGE_BUILD_H
#define RPG_STAGE_BUILD_H

#include "rpg_attachment.h"
#include "rpg_stage.h"

/* 「ビルドする」操作で全マスと設置物のフォルダを生成し、監視を開始する。 */
bool RpgStageBuild_Create(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                          Vector2 playerStartPosition);
/* エディター内プレイ用。保存方式にかかわらず開始エリアのみを準備する。 */
bool RpgStageBuild_CreateEditorPreview(int stageNumber, RpgStage *stage, const RpgAttachments *attachments,
                                       Vector2 playerStartPosition);
/* 非同期通知を処理し、削除されたセルを赤い実行時壁へ反映する。 */
void RpgStageBuild_Update(RpgStage *stage);
/* Folder内のzipper.cmdが作った要求を一度だけ取り出す。別エリアの要求は消費して無視する。 */
bool RpgStageBuild_ConsumeReferenceFolderZipperRequest(const RpgStage *stage, int playerMapIndex,
                                                        RpgGridCell *folderCell);
void RpgStageBuild_Close(void);

#endif
