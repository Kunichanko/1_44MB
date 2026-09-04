#include "rpg_physics.h"

#include <math.h>
#include <stddef.h>

static Rectangle GetWorldBounds(Vector2 position, Rectangle localBounds)
{
    return (Rectangle){ position.x + localBounds.x, position.y + localBounds.y,
                        localBounds.width, localBounds.height };
}

static bool HasObstacle(const RpgStage *stage, Rectangle bounds,
                        RpgPhysicsObstacleTest obstacleTest, void *obstacleContext)
{
    return RpgStage_CheckSolidCollision(stage, bounds) ||
           (obstacleTest != NULL && obstacleTest(obstacleContext, bounds));
}

bool RpgPhysics_MoveAxis(const RpgStage *stage, Vector2 *position, Rectangle localBounds,
                         float amount, bool vertical, RpgPhysicsObstacleTest obstacleTest,
                         void *obstacleContext)
{
    const float maximumStep = 4.0f;
    if (stage == NULL || position == NULL || fabsf(amount) < 0.0001f) return false;
    float remaining = fabsf(amount);
    float direction = amount < 0.0f ? -1.0f : 1.0f;
    while (remaining > 0.0f) {
        float step = direction * fminf(remaining, maximumStep);
        Rectangle previousBounds = GetWorldBounds(*position, localBounds);
        if (vertical) position->y += step;
        else position->x += step;
        Rectangle candidateBounds = GetWorldBounds(*position, localBounds);
        if (vertical && step > 0.0f) {
            float landingY;
            if (RpgStage_FindOneWayPlatformLanding(stage, previousBounds, candidateBounds, &landingY)) {
                // landingY is the platform's top edge.  Align the *bottom*
                // of the moving collider to it; aligning its top places the
                // collider through the platform and makes one-way floors
                // appear pass-through.
                position->y += landingY - (candidateBounds.y + candidateBounds.height);
                return true;
            }
        }
        if (HasObstacle(stage, candidateBounds, obstacleTest, obstacleContext)) {
            if (vertical) position->y -= step;
            else position->x -= step;
            return true;
        }
        remaining -= fabsf(step);
    }
    return false;
}

bool RpgPhysics_HasGroundBelow(const RpgStage *stage, Vector2 position, Rectangle localBounds,
                               RpgPhysicsObstacleTest obstacleTest, void *obstacleContext)
{
    if (stage == NULL) return false;
    Rectangle currentBounds = GetWorldBounds(position, localBounds);
    position.y += 1.0f;
    Rectangle probeBounds = GetWorldBounds(position, localBounds);
    return HasObstacle(stage, probeBounds, obstacleTest, obstacleContext) ||
           RpgStage_FindOneWayPlatformLanding(stage, currentBounds, probeBounds, NULL);
}
