#include "rpg_player_interaction.h"

#include <stddef.h>

#include "rpg_interaction.h"

typedef struct RpgPlayerInteractionCandidate {
    const RpgPlayerInteractionInput *input;
    RpgPlayerInteractionAction action;
    int targetId;
} RpgPlayerInteractionCandidate;

typedef struct RpgPlayerInteractionDefinition {
    RpgInteractionOwner owner;
    KeyboardKey key;
    int priority;
    RpgPlayerInteractionAction action;
} RpgPlayerInteractionDefinition;

static bool ExecuteCandidate(void *userData, const RpgInteraction *interaction)
{
    RpgPlayerInteractionCandidate *candidate = userData;
    (void)interaction;
    return candidate != NULL && candidate->input != NULL &&
           candidate->input->execute != NULL &&
           candidate->input->execute(candidate->input->userData, candidate->action,
                                     candidate->targetId);
}

static bool RegisterCandidate(RpgInteractionSet *set, RpgPlayerInteractionCandidate candidates[],
                              int *candidateCount, const RpgPlayerInteractionInput *input,
                              RpgPlayerInteractionDefinition definition, int targetId)
{
    RpgPlayerInteractionCandidate *candidate;
    if (set == NULL || candidates == NULL || candidateCount == NULL || input == NULL ||
        *candidateCount < 0 || *candidateCount >= RPG_INTERACTION_MAX_BINDINGS) return false;
    candidate = &candidates[(*candidateCount)++];
    *candidate = (RpgPlayerInteractionCandidate){ .input = input, .action = definition.action,
                                                   .targetId = targetId };
    return RpgInteraction_Register(set, (RpgInteraction){
        .owner = definition.owner, .ownerId = targetId, .key = definition.key,
        .priority = definition.priority, .start = ExecuteCandidate, .userData = candidate
    });
}

bool RpgPlayerInteraction_Dispatch(const RpgPlayerInteractionInput *input)
{
    static const RpgPlayerInteractionDefinition push = {
        RPG_INTERACTION_OWNER_MOVABLE, KEY_G, 100, RPG_PLAYER_INTERACTION_PUSH_MOVABLE
    };
    static const RpgPlayerInteractionDefinition collect = {
        RPG_INTERACTION_OWNER_REFERENCE_FILE, KEY_G, 90, RPG_PLAYER_INTERACTION_COLLECT_REFERENCE
    };
    static const RpgPlayerInteractionDefinition store = {
        RPG_INTERACTION_OWNER_REFERENCE_FOLDER, KEY_P, 100, RPG_PLAYER_INTERACTION_STORE_REFERENCE
    };
    static const RpgPlayerInteractionDefinition closeReference = {
        RPG_INTERACTION_OWNER_REFERENCE_FILE, KEY_E, 100, RPG_PLAYER_INTERACTION_CLOSE_REFERENCE_TEXT
    };
    static const RpgPlayerInteractionDefinition advanceInspect = {
        RPG_INTERACTION_OWNER_INSPECT, KEY_E, 90, RPG_PLAYER_INTERACTION_ADVANCE_INSPECT
    };
    static const RpgPlayerInteractionDefinition advanceDialogue = {
        RPG_INTERACTION_OWNER_DIALOGUE, KEY_E, 80, RPG_PLAYER_INTERACTION_ADVANCE_DIALOGUE
    };
    static const RpgPlayerInteractionDefinition npcDialogue = {
        RPG_INTERACTION_OWNER_NPC, KEY_E, 70, RPG_PLAYER_INTERACTION_START_NPC_DIALOGUE
    };
    static const RpgPlayerInteractionDefinition keyDoor = {
        RPG_INTERACTION_OWNER_KEY_DOOR, KEY_E, 60, RPG_PLAYER_INTERACTION_USE_KEY_DOOR
    };
    static const RpgPlayerInteractionDefinition openReference = {
        RPG_INTERACTION_OWNER_REFERENCE_FILE, KEY_E, 50, RPG_PLAYER_INTERACTION_OPEN_REFERENCE
    };
    RpgInteractionSet set;
    RpgPlayerInteractionCandidate candidates[RPG_INTERACTION_MAX_BINDINGS];
    int candidateCount = 0;

    if (input == NULL || input->execute == NULL) return false;
    RpgInteraction_BeginFrame(&set);

    if (input->isGrounded && input->worldActionsEnabled) {
        (void)RegisterCandidate(&set, candidates, &candidateCount, input, push, input->movableTargetId);
        if (input->canCollectReference)
            (void)RegisterCandidate(&set, candidates, &candidateCount, input, collect,
                                    input->referenceTargetId);
        if (input->canStoreReference)
            (void)RegisterCandidate(&set, candidates, &candidateCount, input, store,
                                    input->folderTargetId);
    }
    if (!input->isGrounded) return RpgInteraction_DispatchPressed(&set);

    if (input->referenceTextOpen)
        (void)RegisterCandidate(&set, candidates, &candidateCount, input, closeReference, -1);
    else if (input->inspectActive)
        (void)RegisterCandidate(&set, candidates, &candidateCount, input, advanceInspect,
                                input->inspectTargetId);
    else if (input->dialogueActive)
        (void)RegisterCandidate(&set, candidates, &candidateCount, input, advanceDialogue,
                                input->dialogueTargetId);
    else {
        if (input->canTalkToNpc)
            (void)RegisterCandidate(&set, candidates, &candidateCount, input, npcDialogue, 0);
        if (input->canUseKeyDoor)
            (void)RegisterCandidate(&set, candidates, &candidateCount, input, keyDoor,
                                    input->keyDoorTargetId);
        if (input->canCollectReference)
            (void)RegisterCandidate(&set, candidates, &candidateCount, input, openReference,
                                    input->referenceTargetId);
    }
    return RpgInteraction_DispatchPressed(&set);
}
