#include "rpg_physics.h"

#include "raymath.h"

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

static bool GetSlopeSurfaceY(RpgPhysicsSlope slope, float x, float *surfaceY)
{
    float minimumX = fminf(slope.start.x, slope.end.x);
    float maximumX = fmaxf(slope.start.x, slope.end.x);
    float horizontalLength = slope.end.x - slope.start.x;
    float t;
    if (surfaceY == NULL || fabsf(horizontalLength) < 0.001f ||
        x < minimumX || x > maximumX) return false;
    t = (x - slope.start.x) / horizontalLength;
    *surfaceY = slope.start.y + (slope.end.y - slope.start.y) * t - slope.thickness * 0.5f;
    return true;
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

bool RpgPhysics_MoveHorizontalWithStep(const RpgStage *stage, RpgPhysicsBody *body,
                                       float amount, RpgPhysicsObstacleTest obstacleTest,
                                       void *obstacleContext)
{
    Vector2 startPosition;
    if (stage == NULL || body == NULL || fabsf(amount) < 0.0001f) return false;

    startPosition = body->position;
    if (!RpgPhysics_MoveAxis(stage, &body->position, body->localBounds, amount, false,
                             obstacleTest, obstacleContext))
        return false;

    /* Do not turn a side collision into an air jump.  A step is only a
       ground traversal aid, never a way to climb a wall while falling. */
    if (!body->isGrounded) return true;

    for (int rise = 1; rise <= RPG_PHYSICS_MAX_STEP_HEIGHT; rise++) {
        Vector2 candidate = { startPosition.x, startPosition.y - (float)rise };
        if (HasObstacle(stage, GetWorldBounds(candidate, body->localBounds),
                        obstacleTest, obstacleContext))
            continue;
        if (RpgPhysics_MoveAxis(stage, &candidate, body->localBounds, amount, false,
                                obstacleTest, obstacleContext))
            continue;
        /* A vertical obstacle without a top is still a wall, rather than a
           step.  Require a shared solid/one-way-ground probe at the raised
           destination before accepting the new height. */
        if (!RpgPhysics_HasGroundBelow(stage, candidate, body->localBounds,
                                      obstacleTest, obstacleContext))
            continue;
        body->position = candidate;
        return false;
    }

    return true;
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

void RpgPhysics_UpdateBody(const RpgStage *stage, RpgPhysicsBody *body,
                           float horizontalAmount, float gravity, float deltaTime,
                           RpgPhysicsObstacleTest obstacleTest, void *obstacleContext)
{
    bool collidedVertically;
    if (stage == NULL || body == NULL || deltaTime <= 0.0f) return;
    (void)RpgPhysics_MoveHorizontalWithStep(stage, body, horizontalAmount,
                                            obstacleTest, obstacleContext);
    body->verticalSpeed += gravity * deltaTime;
    collidedVertically = RpgPhysics_MoveAxis(stage, &body->position, body->localBounds,
                                              body->verticalSpeed * deltaTime, true,
                                              obstacleTest, obstacleContext);
    if (collidedVertically) {
        body->isGrounded = body->verticalSpeed > 0.0f;
        body->verticalSpeed = 0.0f;
    } else if (body->verticalSpeed >= 0.0f &&
               RpgPhysics_HasGroundBelow(stage, body->position, body->localBounds,
                                         obstacleTest, obstacleContext)) {
        body->isGrounded = true;
        body->verticalSpeed = 0.0f;
    } else body->isGrounded = false;
}

static bool OverlapsHorizontally(Rectangle first, Rectangle second)
{
    return first.x < second.x + second.width && first.x + first.width > second.x;
}

void RpgPhysics_ResolveMovingSolidContact(const RpgStage *stage, RpgPhysicsBody *body,
                                          Rectangle previousSolidBounds, Rectangle solidBounds,
                                          RpgPhysicsObstacleTest obstacleTest,
                                          void *obstacleContext)
{
    Rectangle bodyBounds;
    float deltaX, deltaY;
    bool wasStandingOnSolid;
    if (stage == NULL || body == NULL) return;

    bodyBounds = GetWorldBounds(body->position, body->localBounds);
    deltaX = solidBounds.x - previousSolidBounds.x;
    deltaY = solidBounds.y - previousSolidBounds.y;
    wasStandingOnSolid = OverlapsHorizontally(bodyBounds, previousSolidBounds) &&
                         fabsf((bodyBounds.y + bodyBounds.height) - previousSolidBounds.y) <= 2.0f;

    if (wasStandingOnSolid && (fabsf(deltaX) > 0.0001f || fabsf(deltaY) > 0.0001f)) {
        Vector2 previousPosition = body->position;
        body->position.x += deltaX;
        body->position.y += deltaY;
        bodyBounds = GetWorldBounds(body->position, body->localBounds);
        if (HasObstacle(stage, bodyBounds, obstacleTest, obstacleContext))
            body->position = previousPosition;
        else {
            body->isGrounded = true;
            body->verticalSpeed = 0.0f;
            bodyBounds = GetWorldBounds(body->position, body->localBounds);
        }
    }

    if (!CheckCollisionRecs(bodyBounds, solidBounds)) return;

    {
        float pushLeft = solidBounds.x - (bodyBounds.x + bodyBounds.width);
        float pushRight = solidBounds.x + solidBounds.width - bodyBounds.x;
        float pushUp = solidBounds.y - (bodyBounds.y + bodyBounds.height);
        float pushDown = solidBounds.y + solidBounds.height - bodyBounds.y;
        float horizontalPush = fabsf(pushLeft) < fabsf(pushRight) ? pushLeft : pushRight;
        float verticalPush = fabsf(pushUp) < fabsf(pushDown) ? pushUp : pushDown;
        bool preferHorizontal = fabsf(deltaX) > fabsf(deltaY) ? true :
                                fabsf(deltaY) > fabsf(deltaX) ? false :
                                fabsf(horizontalPush) <= fabsf(verticalPush);
        const bool horizontalCandidates[2] = { preferHorizontal, !preferHorizontal };
        const float pushes[2] = { preferHorizontal ? horizontalPush : verticalPush,
                                  preferHorizontal ? verticalPush : horizontalPush };
        for (int candidate = 0; candidate < 2; candidate++) {
            bool horizontal = horizontalCandidates[candidate];
            float push = pushes[candidate];
            if (horizontal) body->position.x += push;
            else body->position.y += push;
            bodyBounds = GetWorldBounds(body->position, body->localBounds);
            if (!HasObstacle(stage, bodyBounds, obstacleTest, obstacleContext)) {
                if (!horizontal) {
                    if (push < 0.0f) {
                        body->isGrounded = true;
                        body->verticalSpeed = 0.0f;
                    } else if (body->verticalSpeed < 0.0f) body->verticalSpeed = 0.0f;
                }
                break;
            }
            if (horizontal) body->position.x -= push;
            else body->position.y -= push;
        }
    }
}

bool RpgPhysics_FindOneWaySlopeLanding(RpgPhysicsSlope slope, Rectangle previousBounds,
                                       Rectangle candidateBounds, float *landingY)
{
    float previousBottom = previousBounds.y + previousBounds.height;
    float candidateBottom = candidateBounds.y + candidateBounds.height;
    float surfaceY;
    float contactX = candidateBounds.x + candidateBounds.width * 0.5f;
    if (candidateBottom <= previousBottom ||
        !GetSlopeSurfaceY(slope, contactX, &surfaceY) ||
        previousBottom > surfaceY + 0.001f || candidateBottom < surfaceY) return false;
    if (landingY != NULL) *landingY = surfaceY;
    return true;
}

bool RpgPhysics_IsBodyOnSlopeTop(RpgPhysicsSlope slope, Rectangle bounds, float tolerance)
{
    float surfaceY;
    float contactX = bounds.x + bounds.width * 0.5f;
    return GetSlopeSurfaceY(slope, contactX, &surfaceY) &&
           bounds.y < surfaceY && fabsf((bounds.y + bounds.height) - surfaceY) <= tolerance;
}

bool RpgPhysics_SlideBoundsOnSlope(const RpgStage *stage, Rectangle *bounds,
                                   RpgPhysicsSlope slope, float fallSpeed, float deltaTime,
                                   RpgPhysicsObstacleTest obstacleTest, void *obstacleContext)
{
    Vector2 direction = Vector2Normalize(Vector2Subtract(slope.end, slope.start));
    Vector2 position;
    float surfaceY;
    if (stage == NULL || bounds == NULL || fallSpeed <= 0.0f || deltaTime <= 0.0f ||
        fabsf(direction.x) < 0.001f || fabsf(direction.y) < 0.001f ||
        !RpgPhysics_IsBodyOnSlopeTop(slope, *bounds, 2.0f)) return false;
    if (direction.y < 0.0f) direction = (Vector2){ -direction.x, -direction.y };
    /* Match the body's falling speed vertically.  The required path distance
       is larger on shallow slopes, while the configured minimum angle keeps
       that physically expected increase under control. */
    position = (Vector2){ bounds->x, bounds->y };
    (void)RpgPhysics_MoveAxis(stage, &position,
                              (Rectangle){ 0.0f, 0.0f, bounds->width, bounds->height },
                              direction.x * (fallSpeed / fabsf(direction.y)) * deltaTime, false,
                              obstacleTest, obstacleContext);
    bounds->x = position.x;
    if (!GetSlopeSurfaceY(slope, bounds->x + bounds->width * 0.5f, &surfaceY)) return true;
    position.y = surfaceY - bounds->height;
    if (!HasObstacle(stage, GetWorldBounds(position,
                                           (Rectangle){ 0.0f, 0.0f, bounds->width, bounds->height }),
                     obstacleTest, obstacleContext))
        bounds->y = position.y;
    return true;
}
