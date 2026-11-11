// Responsibility: declarative abilities granted only while their .ability file is in Zipper.
#ifndef RPG_ZIPPER_ABILITY_H
#define RPG_ZIPPER_ABILITY_H

#include <stdbool.h>

/* Add new abilities here first.  The implementation intentionally maps a
   known filename to a bool; .ability files never execute arbitrary code. */
typedef enum RpgZipperAbilityId {
    RPG_ZIPPER_ABILITY_DOUBLE_JUMP = 0,
    RPG_ZIPPER_ABILITY_COUNT
} RpgZipperAbilityId;

typedef struct RpgZipperAbilityState {
    /* Granted by double_jump.ability anywhere under the active Zipper
       directory.  No gameplay behavior is attached yet. */
    bool doubleJumpEnabled;
} RpgZipperAbilityState;

/* Polls no filesystem state by itself.  It scans only after the pre-existing
   Zipper folder watcher reports a change, or once for a new runtime root. */
void RpgZipperAbilities_Update(void);
const RpgZipperAbilityState *RpgZipperAbilities_GetState(void);
bool RpgZipperAbilities_IsEnabled(RpgZipperAbilityId ability);

#endif
