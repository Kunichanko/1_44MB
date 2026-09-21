#include "rpg_light_source.h"

#include "raymath.h"

#include <math.h>
#include <stddef.h>

Vector2 RpgLightSource_DirectionFromDegrees(float degrees)
{
    float radians = degrees * PI / 180.0f;
    return (Vector2){ sinf(radians), -cosf(radians) };
}

static Vector2 LerpVector2(Vector2 first, Vector2 second, float amount)
{
    return (Vector2){ first.x + (second.x - first.x) * amount,
                      first.y + (second.y - first.y) * amount };
}

void RpgLightSource_DrawSegmentFanOccluded(const RpgLightSourceSegment *source,
                                           RpgLightSourceRaycast raycast, void *context)
{
    enum { sourceSamples = 12, gradientBands = 8 };
    Vector2 origins[sourceSamples + 1];
    Vector2 directions[sourceSamples + 1];
    float lengths[sourceSamples + 1];
    Vector2 firstDirection;
    Vector2 secondDirection;
    if (source == NULL || source->rayLength <= 0.0f || source->opacity <= 0.0f) return;

    firstDirection = RpgLightSource_DirectionFromDegrees(source->firstDirectionDegrees);
    secondDirection = RpgLightSource_DirectionFromDegrees(source->secondDirectionDegrees);
    for (int index = 0; index <= sourceSamples; index++) {
        float amount = (float)index / (float)sourceSamples;
        origins[index] = LerpVector2(source->first, source->second, amount);
        directions[index] = Vector2Normalize(LerpVector2(firstDirection, secondDirection, amount));
        lengths[index] = raycast == NULL ? source->rayLength :
            Clamp(raycast(context, origins[index], directions[index], source->rayLength),
                  0.0f, source->rayLength);
    }
    /* Render contiguous trapezoids from the source outward.  This preserves
       continuous (non-pixel-snapped) geometry, while per-band alpha creates
       an actual source-to-end gradient and clipped rays stop at blockers. */
    for (int band = 0; band < gradientBands; band++) {
        float nearT = (float)band / (float)gradientBands;
        float farT = (float)(band + 1) / (float)gradientBands;
        float alpha = source->opacity * (1.0f - (nearT + farT) * 0.5f);
        Color fill = Fade(source->color, alpha);
        for (int index = 0; index < sourceSamples; index++) {
            Vector2 nearFirst = Vector2Add(origins[index], Vector2Scale(directions[index], lengths[index] * nearT));
            Vector2 nearSecond = Vector2Add(origins[index + 1], Vector2Scale(directions[index + 1], lengths[index + 1] * nearT));
            Vector2 farFirst = Vector2Add(origins[index], Vector2Scale(directions[index], lengths[index] * farT));
            Vector2 farSecond = Vector2Add(origins[index + 1], Vector2Scale(directions[index + 1], lengths[index + 1] * farT));
            DrawTriangle(nearFirst, nearSecond, farSecond, fill);
            DrawTriangle(nearFirst, farSecond, farFirst, fill);
        }
    }
    DrawLineEx(source->first, source->second, 1.0f, Fade(source->color, source->opacity));
    DrawLineEx(source->first, Vector2Add(origins[0], Vector2Scale(directions[0], lengths[0])),
               1.0f, Fade(source->color, source->opacity * 0.80f));
    DrawLineEx(source->second, Vector2Add(origins[sourceSamples],
                                           Vector2Scale(directions[sourceSamples], lengths[sourceSamples])),
               1.0f, Fade(source->color, source->opacity * 0.80f));
}

void RpgLightSource_DrawSegmentFan(const RpgLightSourceSegment *source)
{
    RpgLightSource_DrawSegmentFanOccluded(source, NULL, NULL);
}
