#ifndef RPG_PLAYER_INTERACTION_H
#define RPG_PLAYER_INTERACTION_H

#include <stdbool.h>

/*
 * Player input owns key selection and priority.  World-object modules only
 * implement the concrete action selected here.  This deliberately does not
 * include Zipper Space/Q controls: those are conditional Zipper controls,
 * not player-to-object interactions.
 */
typedef enum RpgPlayerInteractionAction {
    RPG_PLAYER_INTERACTION_PUSH_MOVABLE = 0,
    RPG_PLAYER_INTERACTION_COLLECT_REFERENCE,
    RPG_PLAYER_INTERACTION_STORE_REFERENCE,
    RPG_PLAYER_INTERACTION_CLOSE_REFERENCE_TEXT,
    RPG_PLAYER_INTERACTION_ADVANCE_INSPECT,
    RPG_PLAYER_INTERACTION_ADVANCE_DIALOGUE,
    RPG_PLAYER_INTERACTION_START_NPC_DIALOGUE,
    RPG_PLAYER_INTERACTION_USE_KEY_DOOR,
    RPG_PLAYER_INTERACTION_OPEN_REFERENCE
} RpgPlayerInteractionAction;

typedef bool (*RpgPlayerInteractionExecuteFn)(void *userData,
                                               RpgPlayerInteractionAction action,
                                               int targetId);

typedef struct RpgPlayerInteractionInput {
    bool isGrounded;
    bool worldActionsEnabled;
    bool referenceTextOpen;
    bool inspectActive;
    bool dialogueActive;
    bool canCollectReference;
    bool canStoreReference;
    bool canTalkToNpc;
    bool canUseKeyDoor;
    int movableTargetId;
    int referenceTargetId;
    int folderTargetId;
    int inspectTargetId;
    int dialogueTargetId;
    int keyDoorTargetId;
    void *userData;
    RpgPlayerInteractionExecuteFn execute;
} RpgPlayerInteractionInput;

/* Dispatches at most one action per physical key press.  Availability and
 * target discovery are supplied by the runtime; the selected object's code
 * remains responsible only for executing the requested action. */
bool RpgPlayerInteraction_Dispatch(const RpgPlayerInteractionInput *input);

#endif
