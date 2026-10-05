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
/* A communication source can explicitly assert or release its state.  Pulse
   consumers keep using Publish(), while state consumers (such as magnets)
   receive both edges through the same area-scoped channel. */
void RpgButtonEvent_PublishState(RpgButtonEvent *event, int sourceMapIndex,
                                 RpgButtonEventSource source, bool isActive);
void RpgButtonEvent_PublishBlockSocket(RpgButtonEvent *event, int sourceMapIndex, bool isActive);
bool RpgButtonEvent_Consume(const RpgButtonEvent *event, unsigned int *lastConsumedSequence);

#endif
