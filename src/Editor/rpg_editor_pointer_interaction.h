#ifndef RPG_EDITOR_POINTER_INTERACTION_H
#define RPG_EDITOR_POINTER_INTERACTION_H

#include <stdbool.h>

#include "raylib.h"

/* The editor keeps click and drag candidates independent until the gesture is
 * known.  A cell can therefore expose a click-only object and a drag-only
 * object at the same time.  `kind` and `index` are intentionally caller-owned
 * values so this small dispatcher is reusable by every editor object type. */
typedef struct RpgEditorPointerTarget {
    int kind;
    int index;
    bool acceptsClick;
    bool acceptsDrag;
} RpgEditorPointerTarget;

typedef struct RpgEditorPointerInteraction {
    bool active;
    bool dragDispatched;
    Vector2 pressPosition;
    RpgEditorPointerTarget clickTarget;
    RpgEditorPointerTarget dragTarget;
} RpgEditorPointerInteraction;

void RpgEditorPointerInteraction_Begin(RpgEditorPointerInteraction *interaction,
                                       Vector2 pointer,
                                       RpgEditorPointerTarget clickTarget,
                                       RpgEditorPointerTarget dragTarget);
bool RpgEditorPointerInteraction_IsBusy(const RpgEditorPointerInteraction *interaction);
/* Returns the registered drag target exactly once, after the drag threshold.
 * A returned target with kind == 0 means that no drag was confirmed. */
RpgEditorPointerTarget RpgEditorPointerInteraction_Update(RpgEditorPointerInteraction *interaction,
                                                           Vector2 pointer);
/* Returns the click target only when the pointer was released without having
 * confirmed a drag.  This always clears the interaction. */
RpgEditorPointerTarget RpgEditorPointerInteraction_End(RpgEditorPointerInteraction *interaction);
void RpgEditorPointerInteraction_Cancel(RpgEditorPointerInteraction *interaction);

#endif
