// Shared axis-based physics for characters and runtime movable blocks.
#ifndef RPG_PHYSICS_H
#define RPG_PHYSICS_H

#include <stdbool.h>

#include "raylib.h"
#include "rpg_stage.h"

typedef bool (*RpgPhysicsObstacleTest)(void *context, Rectangle bounds);

/* localBounds is relative to position.  Static stage collision, one-way-floor
   landing, and an optional runtime-obstacle test use this exact same path. */
bool RpgPhysics_MoveAxis(const RpgStage *stage, Vector2 *position, Rectangle localBounds,
                         float amount, bool vertical, RpgPhysicsObstacleTest obstacleTest,
                         void *obstacleContext);
bool RpgPhysics_HasGroundBelow(const RpgStage *stage, Vector2 position, Rectangle localBounds,
                               RpgPhysicsObstacleTest obstacleTest, void *obstacleContext);

#endif
