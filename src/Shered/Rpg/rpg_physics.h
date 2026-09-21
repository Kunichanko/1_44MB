// Shared axis-based physics for characters and runtime movable blocks.
#ifndef RPG_PHYSICS_H
#define RPG_PHYSICS_H

#include <stdbool.h>

#include "raylib.h"
#include "rpg_stage.h"

typedef bool (*RpgPhysicsObstacleTest)(void *context, Rectangle bounds);

/* A one-way surface is kept as a line plus its rendered thickness.  The
   producer (for example a rotating conveyor) supplies geometry only; moving
   bodies reuse the resolver below for landing and downhill motion. */
typedef struct RpgPhysicsSlope {
    Vector2 start;
    Vector2 end;
    float thickness;
} RpgPhysicsSlope;

/* Input-independent state shared by every gravity-driven moving body.  The
   caller owns rendering and controls; this module owns stage collision,
   one-way-floor landing and grounded-state transitions. */
typedef struct RpgPhysicsBody {
    Vector2 position;
    float verticalSpeed;
    bool isGrounded;
    Rectangle localBounds;
} RpgPhysicsBody;

/* A mover may climb a ledge no higher than half a map tile while it is
   grounded.  Keep this in the shared resolver so the player, characters and
   movable blocks all agree on what counts as a traversable step. */
enum { RPG_PHYSICS_MAX_STEP_HEIGHT = RPG_STAGE_TILE_SIZE / 2 };

/* localBounds is relative to position.  Static stage collision, one-way-floor
   landing, and an optional runtime-obstacle test use this exact same path. */
bool RpgPhysics_MoveAxis(const RpgStage *stage, Vector2 *position, Rectangle localBounds,
                         float amount, bool vertical, RpgPhysicsObstacleTest obstacleTest,
                         void *obstacleContext);
/* Horizontal movement with the common grounded step-up rule.  Returns true
   only when the requested horizontal path remains blocked after trying a
   climb of at most RPG_PHYSICS_MAX_STEP_HEIGHT. */
bool RpgPhysics_MoveHorizontalWithStep(const RpgStage *stage, RpgPhysicsBody *body,
                                       float amount, RpgPhysicsObstacleTest obstacleTest,
                                       void *obstacleContext);
bool RpgPhysics_HasGroundBelow(const RpgStage *stage, Vector2 position, Rectangle localBounds,
                               RpgPhysicsObstacleTest obstacleTest, void *obstacleContext);
void RpgPhysics_UpdateBody(const RpgStage *stage, RpgPhysicsBody *body,
                           float horizontalAmount, float gravity, float deltaTime,
                           RpgPhysicsObstacleTest obstacleTest, void *obstacleContext);
/* Carries a body standing on a moving solid, then resolves any overlap along
   one axis.  The producer owns the solid; every rider only supplies its body
   bounds and optional dynamic-obstacle test. */
void RpgPhysics_ResolveMovingSolidContact(const RpgStage *stage, RpgPhysicsBody *body,
                                          Rectangle previousSolidBounds, Rectangle solidBounds,
                                          RpgPhysicsObstacleTest obstacleTest,
                                          void *obstacleContext);
bool RpgPhysics_FindOneWaySlopeLanding(RpgPhysicsSlope slope, Rectangle previousBounds,
                                       Rectangle candidateBounds, float *landingY);
bool RpgPhysics_IsBodyOnSlopeTop(RpgPhysicsSlope slope, Rectangle bounds, float tolerance);
/* Moves a body down the current slope and keeps its bottom on the visible top
   face. The optional obstacle callback lets dynamic bodies use the same path. */
bool RpgPhysics_SlideBoundsOnSlope(const RpgStage *stage, Rectangle *bounds,
                                   RpgPhysicsSlope slope, float fallSpeed, float deltaTime,
                                   RpgPhysicsObstacleTest obstacleTest, void *obstacleContext);

#endif
