// 役割: 設置ボタンの押下通知を連番として管理する。
// 依存する自プロジェクト内ファイル: rpg_button_event.h
#include "rpg_button_event.h"

#include <stddef.h>

RpgButtonEvent RpgButtonEvent_Default(void) { return (RpgButtonEvent){ 0 }; }

void RpgButtonEvent_Publish(RpgButtonEvent *event)
{
    if (event == NULL) return;
    event->sequence++;
    // 連番のゼロは未受信状態に使うため、周回時も通知として扱える値を維持する。
    if (event->sequence == 0) event->sequence = 1;
}

bool RpgButtonEvent_Consume(const RpgButtonEvent *event, unsigned int *lastConsumedSequence)
{
    if (event == NULL || lastConsumedSequence == NULL || event->sequence == *lastConsumedSequence)
        return false;
    *lastConsumedSequence = event->sequence;
    return true;
}
