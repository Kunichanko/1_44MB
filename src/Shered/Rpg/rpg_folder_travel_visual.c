#include "rpg_folder_travel_visual.h"

#include "rpg_explorer_shell.h"
#include "raymath.h"

static Texture2D icon = { 0 };
static bool hasLoadedIcon = false;

static Texture2D GetFolderIcon(void)
{
    if (!hasLoadedIcon) {
        icon = RpgExplorerShell_LoadFolderIconTexture();
        hasLoadedIcon = true;
    }
    return icon;
}

void RpgFolderTravelVisual_Draw(Vector2 start, Vector2 destination, float elapsed, float duration)
{
    float progress;
    float eased;
    float size;
    Vector2 position;
    Rectangle bounds;
    Texture2D folderIcon = GetFolderIcon();
    duration = Clamp(duration, 0.10f, 5.0f);
    progress = Clamp(elapsed / duration, 0.0f, 1.0f);
    eased = progress * progress * (3.0f - 2.0f * progress);
    position = Vector2Lerp(start, destination, eased);
    size = 24.0f - 7.0f * progress;
    bounds = (Rectangle){ position.x - size * 0.5f, position.y - size * 0.5f, size, size };
    if (folderIcon.id != 0)
        DrawTexturePro(folderIcon, (Rectangle){ 0, 0, (float)folderIcon.width, (float)folderIcon.height }, bounds,
                       (Vector2){ 0, 0 }, 0.0f, Fade(WHITE, 1.0f - progress * 0.20f));
    else {
        DrawRectangleRounded(bounds, 0.16f, 4, Fade(GOLD, 1.0f - progress * 0.20f));
        DrawRectangleLinesEx(bounds, 1.0f, ORANGE);
    }
}

void RpgFolderTravelVisual_DrawFailure(Vector2 position, float elapsed, float duration)
{
    Texture2D folderIcon = GetFolderIcon();
    float progress;
    float shake;
    Rectangle bounds;
    duration = Clamp(duration, 0.10f, 2.0f);
    progress = Clamp(elapsed / duration, 0.0f, 1.0f);
    shake = sinf(progress * 10.0f * PI) * (1.0f - progress) * 4.0f;
    bounds = (Rectangle){ position.x - 12.0f + shake, position.y - 12.0f, 24.0f, 24.0f };
    if (folderIcon.id != 0)
        DrawTexturePro(folderIcon, (Rectangle){ 0, 0, (float)folderIcon.width, (float)folderIcon.height }, bounds,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    else DrawRectangleRounded(bounds, 0.16f, 4, GOLD);
    DrawRectangleLinesEx(bounds, 1.5f, Fade(RED, 0.9f));
}
