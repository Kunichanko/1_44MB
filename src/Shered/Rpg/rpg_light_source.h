#ifndef RPG_LIGHT_SOURCE_H
#define RPG_LIGHT_SOURCE_H

#include "raylib.h"

/* A rendering-only light fan.  The source is a line segment; each endpoint
   has its own direction where 0 degrees faces up and positive angles face
   right.  Gameplay objects own their settings, not this renderer. */
typedef struct RpgLightSourceSegment {
    Vector2 first;
    Vector2 second;
    float firstDirectionDegrees;
    float secondDirectionDegrees;
    float rayLength;
    float opacity;
    Color color;
} RpgLightSourceSegment;

/* Returns the unblocked length of a single light ray, in the same continuous
   world units as rayLength.  Rendering owns no stage/object dependency. */
typedef float (*RpgLightSourceRaycast)(void *context, Vector2 origin,
                                      Vector2 direction, float maximumLength);

Vector2 RpgLightSource_DirectionFromDegrees(float degrees);
void RpgLightSource_DrawSegmentFan(const RpgLightSourceSegment *source);
void RpgLightSource_DrawSegmentFanOccluded(const RpgLightSourceSegment *source,
                                           RpgLightSourceRaycast raycast, void *context);

#endif
