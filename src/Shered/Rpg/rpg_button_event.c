// 役割: 設置ボタンの押下通知を連番として管理する。
// 依存する自プロジェクト内ファイル: rpg_button_event.h
#include "rpg_button_event.h"

#include <stddef.h>

RpgButtonEvent RpgButtonEvent_Default(void)
{
    return (RpgButtonEvent){ .sequence = 0, .sourceMapIndex = -1,
                             .source = RPG_BUTTON_EVENT_SOURCE_PLAYER_BUTTON, .isActive = false };
}

void RpgButtonEvent_Publish(RpgButtonEvent *event, int sourceMapIndex, RpgButtonEventSource source)
{
    RpgButtonEvent_PublishState(event, sourceMapIndex, source, true);
}

void RpgButtonEvent_PublishState(RpgButtonEvent *event, int sourceMapIndex,
                                 RpgButtonEventSource source, bool isActive)
{
    if (event == NULL) return;
    event->sequence++;
    // 連番のゼロは未受信状態に使うため、周回時も通知として扱える値を維持する。
    if (event->sequence == 0) event->sequence = 1;
    event->sourceMapIndex = sourceMapIndex;
    event->source = source;
    event->isActive = isActive;
}

void RpgButtonEvent_PublishBlockSocket(RpgButtonEvent *event, int sourceMapIndex, bool isActive)
{
    RpgButtonEvent_PublishState(event, sourceMapIndex,
                                RPG_BUTTON_EVENT_SOURCE_BLOCK_SOCKET, isActive);
}

bool RpgButtonEvent_Consume(const RpgButtonEvent *event, unsigned int *lastConsumedSequence)
{
    if (event == NULL || lastConsumedSequence == NULL || event->sequence == *lastConsumedSequence)
        return false;
    *lastConsumedSequence = event->sequence;
    return true;
}
