// 依存する自プロジェクト内ファイル: rpg_preview_event.h
// 役割: エディター専用プレビュー通知の連番管理を実装する。
#include "rpg_preview_event.h"

#include <stddef.h>

RpgPreviewEvent RpgPreviewEvent_Default(void) { return (RpgPreviewEvent){ 0 }; }

void RpgPreviewEvent_Publish(RpgPreviewEvent *event, int target)
{
    if (event == NULL) return;
    event->target = target;
    event->sequence++;
    if (event->sequence == 0) event->sequence = 1;
}

bool RpgPreviewEvent_Consume(const RpgPreviewEvent *event, unsigned int *lastConsumedSequence)
{
    if (event == NULL || lastConsumedSequence == NULL || event->sequence == *lastConsumedSequence)
        return false;
    *lastConsumedSequence = event->sequence;
    return true;
}
