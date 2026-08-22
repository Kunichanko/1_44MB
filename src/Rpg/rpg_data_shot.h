// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_button_event.h, rpg_receiver.h, rpg_stage.h, rpg_wire.h
#ifndef RPG_DATA_SHOT_H
#define RPG_DATA_SHOT_H

#include "rpg_attachment.h"
#include "rpg_button_event.h"
#include "rpg_preview_event.h"
#include "rpg_receiver.h"
#include "rpg_wire.h"

enum { RPG_DATA_SHOT_MAX_COUNT = 96, RPG_DATA_SHOT_BASELINE_FILE_MAX = 128,
       // 速度計算とプレビュー容量の基準単位は100B。
       RPG_DATA_SPEED_BASE_BYTES = 100 };

typedef struct RpgDataShot {
    bool active;
    bool isPreview;
    /* Zipperに取り込まれている間は描画・更新を止め、返却時にメタデータから復元する。 */
    bool isZipperHeld;
    // 同時に存在する弾を電波発生装置とは別の実フォルダで識別する連番。
    int folderSerial;
    int attachmentIndex;
    int pathCellIndex;
    Vector2 position;
    Vector2 impactPosition;
    RpgGridCell metadataCell;
    // 実フォルダを走査して得たファイル数・合計容量。見た目と速度はこの値から更新する。
    int fileCount;
    unsigned long long totalBytes;
    // 発射直後のフォルダ内容を記録し、後から追加されたファイルだけを強調・ドロップ対象にする。
    bool folderStatsInitialized;
    unsigned long long folderWriteStamp;
    int baselinePathCount;
    unsigned long long baselinePathHashes[RPG_DATA_SHOT_BASELINE_FILE_MAX];
    bool hasAddedFiles;
    float size;
    float speed;
    bool hitWall;
    // 受容体に届いた後は、同じデータ弾が電気として導線のマスを順に移動する。
    bool isElectric;
    int electricWireIndex;
    int electricCellIndex;
    float electricDelayElapsed;
} RpgDataShot;
typedef struct RpgDataShots {
    RpgDataShot entries[RPG_DATA_SHOT_MAX_COUNT];
    float emitElapsed[RPG_ATTACHMENT_MAX_COUNT];
    int nextFolderSerial;
    unsigned int lastButtonEventSequence;
    unsigned int lastPreviewEventSequence;
} RpgDataShots;

RpgDataShots RpgDataShots_Default(void);
// ファイル数・容量から実弾またはプレビュー弾の大きさと速度を同じ式で求める。
void RpgDataShot_SetFileProperties(RpgDataShot *shot, const RpgAttachment *attachment,
                                   int fileCount, unsigned long long totalBytes);
void RpgDataShots_Trigger(RpgDataShots *shots, const RpgAttachments *attachments, int attachmentIndex);
void RpgDataShots_TriggerAll(RpgDataShots *shots, const RpgAttachments *attachments);
// ボタン押下通知を受信した場合だけ、各電波発生装置からデータ弾を射出する。
void RpgDataShots_ConsumeButtonEvent(RpgDataShots *shots, const RpgAttachments *attachments,
                                     const RpgButtonEvent *buttonEvent);
// プレビュー通知は見た目専用の弾を一回だけ生成し、ゲーム中のギミックへ作用させない。
void RpgDataShots_ConsumePreviewEvent(RpgDataShots *shots, const RpgAttachments *attachments,
                                      const RpgPreviewEvent *previewEvent);
void RpgDataShots_TriggerPreview(RpgDataShots *shots, const RpgAttachments *attachments, int target);
void RpgDataShots_Update(RpgDataShots *shots, const RpgAttachments *attachments,
                         RpgStage *stage, const RpgReceivers *receivers,
                         const RpgWires *wires, float electricCellDelay,
                         float deltaTime, bool previewsOnly);
void RpgDataShots_Draw(const RpgDataShots *shots);
void RpgDataShots_DrawMap(const RpgDataShots *shots, int mapIndex);

#endif
// 役割: データ弾の状態と生成・更新・描画 API を宣言する。
