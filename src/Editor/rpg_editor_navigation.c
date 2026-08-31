// Role: owns editor-only arrow-key shortcuts for area navigation and insertion.
#include "rpg_editor_navigation.h"

#include <stddef.h>
#include "raylib.h"

int RpgEditorNavigation_RequestAreaMove(const RpgStage *stage, int currentMapIndex)
{
    RpgAreaDirection direction;
    if (stage == NULL || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return -1;
    if (IsKeyPressed(KEY_LEFT)) direction = RPG_AREA_LEFT;
    else if (IsKeyPressed(KEY_RIGHT)) direction = RPG_AREA_RIGHT;
    else if (IsKeyPressed(KEY_UP)) direction = RPG_AREA_UP;
    else if (IsKeyPressed(KEY_DOWN)) direction = RPG_AREA_DOWN;
    else return -1;
    currentMapIndex = RpgStage_FindNearestActiveMap(stage, currentMapIndex);
    return RpgStage_GetAdjacentMap(stage, currentMapIndex, direction);
}

bool RpgEditorNavigation_RequestAreaInsertion(RpgAreaDirection *direction)
{
    if (direction == NULL ||
        !(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) return false;
    if (IsKeyPressed(KEY_LEFT)) *direction = RPG_AREA_LEFT;
    else if (IsKeyPressed(KEY_RIGHT)) *direction = RPG_AREA_RIGHT;
    else if (IsKeyPressed(KEY_UP)) *direction = RPG_AREA_UP;
    else if (IsKeyPressed(KEY_DOWN)) *direction = RPG_AREA_DOWN;
    else return false;
    return true;
}
