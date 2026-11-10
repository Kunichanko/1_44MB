#include "rpg_editor_pointer_interaction.h"

#include <stddef.h>

#include "raymath.h"

enum { RPG_EDITOR_POINTER_DRAG_DISTANCE = 5 };

static RpgEditorPointerTarget EmptyTarget(void)
{
    return (RpgEditorPointerTarget){ 0 };
}

void RpgEditorPointerInteraction_Begin(RpgEditorPointerInteraction *interaction,
                                       Vector2 pointer,
                                       RpgEditorPointerTarget clickTarget,
                                       RpgEditorPointerTarget dragTarget)
{
    if (interaction == NULL) return;
    interaction->active = clickTarget.acceptsClick || dragTarget.acceptsDrag;
    interaction->dragDispatched = false;
    interaction->pressPosition = pointer;
    interaction->clickTarget = clickTarget;
    interaction->dragTarget = dragTarget;
}

bool RpgEditorPointerInteraction_IsBusy(const RpgEditorPointerInteraction *interaction)
{
    return interaction != NULL && interaction->active;
}

RpgEditorPointerTarget RpgEditorPointerInteraction_Update(RpgEditorPointerInteraction *interaction,
                                                           Vector2 pointer)
{
    if (interaction == NULL || !interaction->active || interaction->dragDispatched ||
        !interaction->dragTarget.acceptsDrag ||
        Vector2Distance(pointer, interaction->pressPosition) < RPG_EDITOR_POINTER_DRAG_DISTANCE)
        return EmptyTarget();
    interaction->dragDispatched = true;
    return interaction->dragTarget;
}

RpgEditorPointerTarget RpgEditorPointerInteraction_End(RpgEditorPointerInteraction *interaction)
{
    RpgEditorPointerTarget target = EmptyTarget();
    if (interaction != NULL && interaction->active && !interaction->dragDispatched &&
        interaction->clickTarget.acceptsClick)
        target = interaction->clickTarget;
    RpgEditorPointerInteraction_Cancel(interaction);
    return target;
}

void RpgEditorPointerInteraction_Cancel(RpgEditorPointerInteraction *interaction)
{
    if (interaction == NULL) return;
    *interaction = (RpgEditorPointerInteraction){ 0 };
}
