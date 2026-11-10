#include "rpg_communication.h"

void RpgCommunication_Consume(RpgCommunicationContext *context,
                              const RpgButtonEvent *communication)
{
    if (context == NULL || context->stage == NULL) return;

    /* Stateful communication consumers are dispatched together here. */
    RpgMagnets_ConsumeCommunication(context->magnetRuntime, context->stage, communication);
}

void RpgCommunication_Reconcile(RpgCommunicationContext *context)
{
    if (context == NULL || context->stage == NULL) return;

    /* A spit has no new signal edge.  Reapply the canonical state stored by
       the stage so returned receivers do not wait for a later socket change. */
    RpgMagnets_ReconcileCommunicationState(context->magnetRuntime, context->stage);
}
