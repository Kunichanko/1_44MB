#ifndef RPG_INTERACTION_H
#define RPG_INTERACTION_H

#include "raylib.h"

/* Every runtime object exposes interaction in the same form: an owner, an
 * input key, and a start callback.  Object-specific state remains owned by
 * the subsystem that implements the actual behaviour. */
typedef enum RpgInteractionOwner {
    RPG_INTERACTION_OWNER_NONE = 0,
    RPG_INTERACTION_OWNER_PLAYER,
    RPG_INTERACTION_OWNER_NPC,
    RPG_INTERACTION_OWNER_ZIPPER,
    RPG_INTERACTION_OWNER_BLOCK,
    RPG_INTERACTION_OWNER_ATTACHMENT,
    RPG_INTERACTION_OWNER_MOVABLE,
    RPG_INTERACTION_OWNER_REFERENCE_FILE,
    RPG_INTERACTION_OWNER_REFERENCE_FOLDER,
    RPG_INTERACTION_OWNER_KEY_DOOR,
    RPG_INTERACTION_OWNER_INSPECT,
    RPG_INTERACTION_OWNER_DIALOGUE
} RpgInteractionOwner;

typedef struct RpgInteraction RpgInteraction;
typedef bool (*RpgInteractionStartFn)(void *userData, const RpgInteraction *interaction);

/* A profile belongs to one object kind.  It describes only that kind's own
 * inputs and start functions; the interaction module never interprets an
 * operation or substitutes shared gameplay behaviour. */
typedef struct RpgInteractionBinding {
    KeyboardKey key;
    int priority;
    RpgInteractionStartFn start;
} RpgInteractionBinding;

enum { RPG_INTERACTION_MAX_BINDINGS_PER_OBJECT = 4 };

typedef struct RpgInteractionProfile {
    RpgInteractionOwner owner;
    RpgInteractionBinding bindings[RPG_INTERACTION_MAX_BINDINGS_PER_OBJECT];
    int bindingCount;
} RpgInteractionProfile;

struct RpgInteraction {
    RpgInteractionOwner owner;
    int ownerId;
    KeyboardKey key;
    int priority;
    RpgInteractionStartFn start;
    void *userData;
};

enum { RPG_INTERACTION_MAX_BINDINGS = 32 };

typedef struct RpgInteractionSet {
    RpgInteraction entries[RPG_INTERACTION_MAX_BINDINGS];
    int count;
} RpgInteractionSet;

void RpgInteraction_BeginFrame(RpgInteractionSet *set);
bool RpgInteraction_Register(RpgInteractionSet *set, RpgInteraction interaction);
/* Instantiates the bindings configured by an object type.  ownerId and
 * userData identify this concrete object/candidate for its callbacks. */
bool RpgInteraction_RegisterProfile(RpgInteractionSet *set, const RpgInteractionProfile *profile,
                                    int ownerId, void *userData);
/* Invokes the highest-priority enabled binding for each pressed key.  A
 * callback returning false declines the input and permits the next binding
 * for that key to try. */
bool RpgInteraction_DispatchPressed(RpgInteractionSet *set);

#endif
