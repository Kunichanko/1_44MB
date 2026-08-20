// 依存する自プロジェクト内ファイル: なし
// 役割: エディター専用の無害な一回限りプレビュー通知を、機能間で共有する。
#ifndef RPG_PREVIEW_EVENT_H
#define RPG_PREVIEW_EVENT_H

#include <stdbool.h>

typedef struct RpgPreviewEvent {
    unsigned int sequence;
    int target;
} RpgPreviewEvent;

RpgPreviewEvent RpgPreviewEvent_Default(void);
void RpgPreviewEvent_Publish(RpgPreviewEvent *event, int target);
bool RpgPreviewEvent_Consume(const RpgPreviewEvent *event, unsigned int *lastConsumedSequence);

#endif
