#include "rpg_interaction.h"

#include <stddef.h>

void RpgInteraction_BeginFrame(RpgInteractionSet *set)
{
    if (set != NULL) set->count = 0;
}

bool RpgInteraction_Register(RpgInteractionSet *set, RpgInteraction interaction)
{
    if (set == NULL || interaction.key == KEY_NULL || interaction.start == NULL ||
        set->count >= RPG_INTERACTION_MAX_BINDINGS) return false;
    set->entries[set->count++] = interaction;
    return true;
}

bool RpgInteraction_RegisterProfile(RpgInteractionSet *set, const RpgInteractionProfile *profile,
                                    int ownerId, void *userData)
{
    bool registered = false;
    if (set == NULL || profile == NULL || profile->bindingCount < 0 ||
        profile->bindingCount > RPG_INTERACTION_MAX_BINDINGS_PER_OBJECT) return false;
    for (int index = 0; index < profile->bindingCount; index++) {
        RpgInteractionBinding binding = profile->bindings[index];
        if (RpgInteraction_Register(set, (RpgInteraction){
                .owner = profile->owner, .ownerId = ownerId, .key = binding.key,
                .priority = binding.priority, .start = binding.start, .userData = userData }))
            registered = true;
    }
    return registered;
}

bool RpgInteraction_DispatchPressed(RpgInteractionSet *set)
{
    bool invoked = false;
    if (set == NULL) return false;
    for (int keyIndex = 0; keyIndex < set->count; keyIndex++) {
        KeyboardKey key = set->entries[keyIndex].key;
        int bestIndex;
        if (key == KEY_NULL || !IsKeyPressed(key)) continue;
        /* One pressed key may have several candidates.  Try them in priority
         * order and stop only when one actually starts. */
        for (;;) {
            bestIndex = -1;
            for (int index = 0; index < set->count; index++) {
                if (set->entries[index].key != key || set->entries[index].key == KEY_NULL) continue;
                if (bestIndex < 0 || set->entries[index].priority > set->entries[bestIndex].priority)
                    bestIndex = index;
            }
            if (bestIndex < 0) break;
            RpgInteraction interaction = set->entries[bestIndex];
            set->entries[bestIndex].key = KEY_NULL;
            if (interaction.start(interaction.userData, &interaction)) {
                /* A single physical key press starts at most one action. */
                for (int index = 0; index < set->count; index++)
                    if (set->entries[index].key == key) set->entries[index].key = KEY_NULL;
                invoked = true;
                break;
            }
        }
    }
    return invoked;
}
