// 依存する自プロジェクト内ファイル: rpg_character.h
// 依存関係を更新: game_font.h, rpg_dialogue.h を追加した。
#include "raylib.h"

#include "game_font.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_layout.h"
#include "rpg_stage.h"

enum { RPG_SCREEN_WIDTH = 960, RPG_SCREEN_HEIGHT = 540 };

static void UpdateRpgCamera(Camera2D *camera, float playerX, bool followsPlayer)
{
    if (followsPlayer) {
        camera->target.x = playerX;
        if (camera->target.x < RPG_SCREEN_WIDTH / 2.0f) camera->target.x = RPG_SCREEN_WIDTH / 2.0f;
        if (camera->target.x > RPG_STAGE_WORLD_WIDTH - RPG_SCREEN_WIDTH / 2.0f)
            camera->target.x = RPG_STAGE_WORLD_WIDTH - RPG_SCREEN_WIDTH / 2.0f;
    } else {
        int mapIndex = (int)(playerX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
        if (mapIndex >= RPG_STAGE_MAP_COUNT) mapIndex = RPG_STAGE_MAP_COUNT - 1;
        camera->target.x = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE +
                           RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f;
    }
    camera->target.y = RPG_SCREEN_HEIGHT / 2.0f;
}

static void DrawRpgWorld(const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgStage *stage, Camera2D camera, bool followsPlayer,
                         int currentMap, bool canTalk, const RpgDialogue *dialogue,
                         int dialogueIndex)
{
    BeginDrawing();
    ClearBackground((Color){ 135, 206, 235, 255 });
    DrawCircle(780, 95, 42, Fade(YELLOW, 0.9f));
    DrawEllipse(180, 105, 80, 20, Fade(RAYWHITE, 0.85f));
    DrawEllipse(510, 160, 110, 24, Fade(RAYWHITE, 0.8f));
    BeginMode2D(camera);
    DrawRectangle(0, 400, RPG_STAGE_WORLD_WIDTH, 140, (Color){ 103, 161, 70, 255 });
    RpgStage_Draw(stage, false);
    DrawRectangle(0, 400, RPG_STAGE_WORLD_WIDTH, 14, DARKGREEN);
    RpgCharacter_Draw(npc, "NPC");
    RpgCharacter_Draw(player, "Hero");
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 48, (int)npc->position.y - 116, 96, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw("[E] 話しかける", npc->position.x - 48, npc->position.y - 112, 16, MAROON);
    }
    EndMode2D();
    DrawText("RPG Version  -  Move: A/D or Arrow keys  Jump: W / Space", 24, 22, 22, DARKGRAY);
    DrawText("Approach the NPC and press E", 24, 48, 18, DARKGRAY);
    DrawText(followsPlayer ? "Camera: Follow [C]" : "Camera: Map Pivot [C]", 700, 48, 17,
             DARKBLUE);
    DrawText(TextFormat("Map %d / %d", currentMap, RPG_STAGE_MAP_COUNT), 24, 500, 20, RAYWHITE);
    if (dialogueIndex >= 0) {
        const Rectangle dialogBounds = { 150.0f, 350.0f, 660.0f, 130.0f };
        const Rectangle speakerBounds = { 174.0f, 332.0f, 150.0f, 36.0f };
        DrawRectangleRec(dialogBounds, Fade(RAYWHITE, 0.96f));
        DrawRectangleLinesEx(dialogBounds, 2.0f, DARKBLUE);
        DrawRectangleRec(speakerBounds, DARKBLUE);
        DrawText("NPC", 190, 340, 21, RAYWHITE);
        GameFont_Draw(dialogue->lines[dialogueIndex], 178, 390, 24, DARKBLUE);
        GameFont_Draw(TextFormat("E: 次へ  %d / %d", dialogueIndex + 1, dialogue->lineCount),
                      178, 432, 17, GRAY);
    }
    EndDrawing();
}

int main(void)
{
    const float groundY = 400.0f;
    InitWindow(RPG_SCREEN_WIDTH, RPG_SCREEN_HEIGHT, "1_44MB - RPG Version");
    SetTargetFPS(60);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));

    RpgLayout layout = RpgLayout_Default();
    RpgStage stage = RpgStage_Default();
    RpgLayout_Load(TextFormat("%s../assets/Settings/rpg_layout.cfg", GetApplicationDirectory()),
                   &layout);
    RpgStage_Load(TextFormat("%s../assets/Settings/rpg_stage.cfg", GetApplicationDirectory()),
                  &stage);
    RpgDialogue dialogue = RpgDialogue_Default();
    RpgDialogue_Load(TextFormat("%s../assets/Settings/rpg_dialogue.txt",
                                GetApplicationDirectory()), &dialogue);
    for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
        GameFont_AddText(dialogue.lines[lineIndex]);
    }
    RpgCharacter player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
    RpgCharacter npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
    player.position.y = groundY;
    player.moveSpeed = layout.playerMoveSpeed;
    npc.position.y = groundY;
    int dialogueIndex = -1;
    bool cameraFollowsPlayer = false;
    Camera2D camera = { .offset = { RPG_SCREEN_WIDTH / 2.0f, RPG_SCREEN_HEIGHT / 2.0f }, .zoom = 1.0f };
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_C)) cameraFollowsPlayer = !cameraFollowsPlayer;
        if (dialogueIndex < 0) {
            Vector2 previousPosition = player.position;
            RpgCharacter_UpdatePlayer(&player, GetFrameTime(), groundY, 32.0f,
                                      RPG_STAGE_WORLD_WIDTH - 32.0f);
            if (CheckCollisionRecs(RpgCharacter_GetFootBounds(&player),
                                   RpgCharacter_GetFootBounds(&npc))) {
                player.position = previousPosition;
            }
        }
        bool canTalk = RpgCharacter_IsNear(&player, &npc, 72.0f);
        if (IsKeyPressed(KEY_E)) {
            if (dialogueIndex >= 0) {
                dialogueIndex++;
                if (dialogueIndex >= dialogue.lineCount) dialogueIndex = -1;
            } else if (canTalk) {
                dialogueIndex = 0;
            }
        }
        UpdateRpgCamera(&camera, player.position.x, cameraFollowsPlayer);
        int currentMap = (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
        DrawRpgWorld(&player, &npc, &stage, camera, cameraFollowsPlayer, currentMap, canTalk,
                     &dialogue, dialogueIndex);
    }
    GameFont_Unload();
    CloseWindow();
    return 0;
}
