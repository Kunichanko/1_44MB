// 役割: 設置ボタンの押下を、特定の用途に依存せず全システムへ通知する。
// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_BUTTON_EVENT_H
#define RPG_BUTTON_EVENT_H

#include <stdbool.h>

typedef enum RpgButtonEventSource {
    RPG_BUTTON_EVENT_SOURCE_PLAYER_BUTTON,
    RPG_BUTTON_EVENT_SOURCE_BLOCK_SOCKET
} RpgButtonEventSource;

typedef struct RpgButtonEvent {
    unsigned int sequence;
    int sourceMapIndex;
    RpgButtonEventSource source;
    /* Button presses are pulses.  A socket additionally exposes whether its
       area signal was asserted or released. */
    bool isActive;
} RpgButtonEvent;

RpgButtonEvent RpgButtonEvent_Default(void);
void RpgButtonEvent_Publish(RpgButtonEvent *event, int sourceMapIndex, RpgButtonEventSource source);
void RpgButtonEvent_PublishBlockSocket(RpgButtonEvent *event, int sourceMapIndex, bool isActive);
bool RpgButtonEvent_Consume(const RpgButtonEvent *event, unsigned int *lastConsumedSequence);

#endif
