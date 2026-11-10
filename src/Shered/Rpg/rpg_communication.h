#ifndef RPG_COMMUNICATION_H
#define RPG_COMMUNICATION_H

#include "rpg_button_event.h"
#include "rpg_magnet.h"
#include "rpg_stage.h"

/* Central dispatch point for stateful, area-scoped communication consumers.
   New communication-reactive object types join here rather than adding their
   own special case to Zipper spit. */
typedef struct RpgCommunicationContext {
    RpgStage *stage;
    RpgMagnetRuntime *magnetRuntime;
} RpgCommunicationContext;

void RpgCommunication_Consume(RpgCommunicationContext *context,
                              const RpgButtonEvent *communication);
void RpgCommunication_Reconcile(RpgCommunicationContext *context);

#endif
