// 役割: 設置ボタンの押下を、特定の用途に依存せず全システムへ通知する。
// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_BUTTON_EVENT_H
#define RPG_BUTTON_EVENT_H

#include <stdbool.h>

typedef struct RpgButtonEvent {
    unsigned int sequence;
    int sourceMapIndex;
} RpgButtonEvent;

RpgButtonEvent RpgButtonEvent_Default(void);
void RpgButtonEvent_Publish(RpgButtonEvent *event, int sourceMapIndex);
bool RpgButtonEvent_Consume(const RpgButtonEvent *event, unsigned int *lastConsumedSequence);

#endif
