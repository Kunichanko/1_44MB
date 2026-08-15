// 依存する自プロジェクト内ファイル: rpg_character.h
// 依存関係を更新: game_font.h, rpg_dialogue.h を追加した。
// 依存関係を更新: rpg_stage3_event.h を追加した。
#include "raylib.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

#include "game_font.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_layout.h"
#include "rpg_inspect.h"
#include "rpg_stage3_event.h"
#include "rpg_stage.h"
#include "rpg_zipper.h"

enum { RPG_SCREEN_WIDTH = 960, RPG_SCREEN_HEIGHT = 540 };

static const char *npcTalkPrompt = u8"[E] \u8a71\u3057\u304b\u3051\u308b";
static const char *zipperInspectPrompt = u8"[I] \u8abf\u3079\u308b";

static void DrawZipper(Texture2D zipperTexture, const RpgCharacter *zipper)
{
    // ZIPPER.png は32x40のフレームが並ぶため、停止状態として先頭フレームだけを描画する。
    Rectangle source = { 0.0f, 0.0f, 32.0f, 40.0f };
    Rectangle destination = { zipper->position.x - 24.0f * zipper->scale,
                              380.0f - 60.0f * zipper->scale,
                              48.0f * zipper->scale, 60.0f * zipper->scale };
    DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

static void DrawMoveSprite(Texture2D zipperTexture, const RpgCharacter *player, const RpgCharacter *npc,
                           const RpgZipper *zipper, RpgInspectMoveTarget target, float x)
{
    if (target == RPG_INSPECT_MOVE_ZIPPER) {
        Rectangle source = { 0, 0, 32, 40 };
        Rectangle destination = { x - 24.0f * zipper->character.scale, 340.0f - 60.0f * zipper->character.scale,
                                  48.0f * zipper->character.scale, 60.0f * zipper->character.scale };
        DrawTexturePro(zipperTexture, source, destination, (Vector2){0}, 0, Fade(WHITE, 0.75f));
    } else {
        RpgCharacter sprite = target == RPG_INSPECT_MOVE_PLAYER ? *player : *npc;
        sprite.position = (Vector2){ x, 400.0f };
        RpgCharacter_Draw(&sprite, "");
    }
}

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

static void UpdateZipperFollow(RpgZipper *zipper, const RpgCharacter *player, float deltaTime)
{
    // 調べるFunction列が完了した後だけ、Zipperを主人公の少し後ろへ滑らかに追従させる。
    float targetX = player->position.x - 48.0f * player->scale;
    float distance = targetX - zipper->character.position.x;
    float maximumStep = player->moveSpeed * deltaTime;
    zipper->character.position.x += Clamp(distance, -maximumStep, maximumStep);
}

static void DrawRpgWorld(const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgStage *stage, Camera2D camera, bool followsPlayer,
                         int currentMap, bool canTalk, const RpgDialogue *dialogue,
                         int dialogueIndex, int stage3IntroIndex, const RpgStage3Event *stage3Event, const RpgZipper *zipper,
                         Texture2D zipperTexture, const RpgInspect *inspect, const RpgInspect *zipperInspect,
                         int inspectTarget, int inspectFunctionIndex, int inspectLineIndex,
                         bool isMoveSpriteVisible, float moveSpriteX, RpgInspectMoveTarget moveSpriteTarget,
                         bool npcInspectCompleted, bool zipperInspectCompleted)
{
    (void)npcInspectCompleted;
    BeginDrawing();
    ClearBackground((Color){ 135, 206, 235, 255 });
    DrawCircle(780, 95, 42, Fade(YELLOW, 0.9f));
    DrawEllipse(180, 105, 80, 20, Fade(RAYWHITE, 0.85f));
    DrawEllipse(510, 160, 110, 24, Fade(RAYWHITE, 0.8f));
    BeginMode2D(camera);
    DrawRectangle(0, 400, RPG_STAGE_WORLD_WIDTH, 140, (Color){ 103, 161, 70, 255 });
    RpgStage_Draw(stage, false);
    DrawRectangle(0, 400, RPG_STAGE_WORLD_WIDTH, 14, DARKGREEN);
    DrawZipper(zipperTexture, &zipper->character);
    if (isMoveSpriteVisible) DrawMoveSprite(zipperTexture, player, npc, zipper, moveSpriteTarget, moveSpriteX);
    RpgCharacter_Draw(npc, "NPC");
    RpgCharacter_Draw(player, "Hero");
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 48, (int)npc->position.y - 116, 96, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw("[E] 話しかける", npc->position.x - 48, npc->position.y - 112, 16, MAROON);
    }
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 58, (int)npc->position.y - 116, 116, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw(npcTalkPrompt, npc->position.x - 54, npc->position.y - 112, 16, MAROON);
    }
    if (fabsf(player->position.x - zipper->character.position.x) <= 72.0f && inspectTarget < 0 &&
        zipperInspect->enabled && !zipperInspectCompleted) {
        GameFont_Draw(zipperInspectPrompt, zipper->character.position.x - 44.0f, 270.0f, 16, MAROON);
    }
    EndMode2D();
    DrawText("RPG Version  -  Move: A/D or Arrow keys  Jump: W / Space", 24, 22, 22, DARKGRAY);
    DrawText("Approach the NPC and press E", 24, 48, 18, DARKGRAY);
    DrawText(followsPlayer ? "Camera: Follow [C]" : "Camera: Map Pivot [C]", 700, 48, 17,
             DARKBLUE);
    DrawText(TextFormat("Map %d / %d", currentMap, RPG_STAGE_MAP_COUNT), 24, 500, 20, RAYWHITE);
    const RpgInspect *activeInspect = inspectTarget == 2 ? zipperInspect : inspect;
    bool isInspectDialogue = inspectTarget >= 0 &&
                             activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_DIALOGUE;
    if (dialogueIndex >= 0 || stage3IntroIndex >= 0 || isInspectDialogue) {
        const Rectangle dialogBounds = { 150.0f, 350.0f, 660.0f, 130.0f };
        const Rectangle speakerBounds = { 174.0f, 332.0f, 150.0f, 36.0f };
        DrawRectangleRec(dialogBounds, Fade(RAYWHITE, 0.96f));
        DrawRectangleLinesEx(dialogBounds, 2.0f, DARKBLUE);
        DrawRectangleRec(speakerBounds, DARKBLUE);
        const RpgDialogue *inspectDialogue = isInspectDialogue ? &activeInspect->functions[inspectFunctionIndex].dialogue : NULL;
        const char *speaker = inspectDialogue != NULL ? inspectDialogue->speakers[inspectLineIndex] : stage3IntroIndex >= 0 ? stage3Event->dialogue.speakers[stage3IntroIndex] : dialogue->speakers[dialogueIndex];
        const char *text = inspectDialogue != NULL ? inspectDialogue->lines[inspectLineIndex] : stage3IntroIndex >= 0 ? stage3Event->dialogue.lines[stage3IntroIndex] : dialogue->lines[dialogueIndex];
        GameFont_Draw(speaker, 190, 340, 21, RAYWHITE);
        GameFont_Draw(text, 178, 390, 24, DARKBLUE);
        GameFont_Draw(inspectDialogue != NULL ? TextFormat("E: next  function %d / %d, line %d / %d", inspectFunctionIndex + 1, activeInspect->functionCount, inspectLineIndex + 1, inspectDialogue->lineCount) : stage3IntroIndex >= 0 ? TextFormat("E: 次へ  %d / %d", stage3IntroIndex + 1, stage3Event->dialogue.lineCount) : TextFormat("E: 次へ  %d / %d", dialogueIndex + 1, dialogue->lineCount),
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
    GameFont_AddText(npcTalkPrompt);
    GameFont_AddText(zipperInspectPrompt);
    Texture2D zipperTexture = LoadTexture(TextFormat("%s../assets/Sprite/ZIPPER.png",
                                                      GetApplicationDirectory()));

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
        GameFont_AddText(dialogue.speakers[lineIndex]);
    }
    RpgStage3Event stage3Event = RpgStage3Event_Default();
    RpgStage3Event_Load(TextFormat("%s../assets/Settings/rpg_stage3_event.cfg",
                                   GetApplicationDirectory()), &stage3Event);
    RpgZipper zipper = RpgZipper_Default();
    RpgZipper_Load(TextFormat("%s../assets/Settings/rpg_zipper.cfg", GetApplicationDirectory()), &zipper);
    for (int lineIndex = 0; lineIndex < stage3Event.dialogue.lineCount; lineIndex++) {
        GameFont_AddText(stage3Event.dialogue.speakers[lineIndex]);
        GameFont_AddText(stage3Event.dialogue.lines[lineIndex]);
    }
    RpgInspect inspect = RpgInspect_Default("Inspect", "Nothing unusual here.");
    RpgInspect_Load(TextFormat("%s../assets/Settings/rpg_inspect.cfg", GetApplicationDirectory()), &inspect);
    for (int functionIndex = 0; functionIndex < inspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(inspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    RpgInspect_Load(TextFormat("%s../assets/Settings/rpg_zipper_inspect.cfg", GetApplicationDirectory()), &zipper.inspect);
    for (int functionIndex = 0; functionIndex < zipper.inspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < zipper.inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(zipper.inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(zipper.inspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    RpgCharacter player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
    RpgCharacter npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
    player.position.y = groundY;
    player.moveSpeed = layout.playerMoveSpeed;
    player.scale = layout.playerScale;
    npc.position.y = groundY;
    npc.scale = layout.npcScale;
    int dialogueIndex = -1;
    int stage3IntroIndex = -1;
    int inspectFunctionIndex = -1;
    int inspectLineIndex = -1;
    int inspectTarget = -1;
    bool isInspectMoveRunning = false;
    float inspectMoveElapsed = 0.0f;
    float inspectMoveStartX = 0.0f;
    bool stage3IntroShown = false;
    bool zipperFollowsPlayer = false;
    bool npcInspectCompleted = false;
    bool zipperInspectCompleted = false;
    int previousMap = (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
    bool cameraFollowsPlayer = false;
    Camera2D camera = { .offset = { RPG_SCREEN_WIDTH / 2.0f, RPG_SCREEN_HEIGHT / 2.0f }, .zoom = 1.0f };
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_C)) cameraFollowsPlayer = !cameraFollowsPlayer;
        if (dialogueIndex < 0 && stage3IntroIndex < 0 && inspectTarget < 0) {
            Vector2 previousPosition = player.position;
            RpgCharacter_UpdatePlayer(&player, GetFrameTime(), groundY, 32.0f,
                                      RPG_STAGE_WORLD_WIDTH - 32.0f);
            if (CheckCollisionRecs(RpgCharacter_GetFootBounds(&player),
                                   RpgCharacter_GetFootBounds(&npc))) {
                player.position = previousPosition;
            }
        }
        if (zipperFollowsPlayer && inspectTarget < 0 && dialogueIndex < 0 && stage3IntroIndex < 0)
            UpdateZipperFollow(&zipper, &player, GetFrameTime());
        bool canTalk = RpgCharacter_IsNear(&player, &npc, 72.0f);
        // 調べる機能列のMoveは、対象を指定時間で補間して完了後に次の機能へ進める。
        if (inspectTarget >= 0) {
            RpgInspect *activeInspect = inspectTarget == 2 ? &zipper.inspect : &inspect;
            if (activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_MOVE) {
                RpgInspectMove *move = &activeInspect->functions[inspectFunctionIndex].move;
                if (!isInspectMoveRunning) {
                    isInspectMoveRunning = true;
                    inspectMoveElapsed = 0.0f;
                    inspectMoveStartX = move->target == RPG_INSPECT_MOVE_PLAYER ? player.position.x :
                                        move->target == RPG_INSPECT_MOVE_NPC ? npc.position.x : zipper.character.position.x;
                }
                inspectMoveElapsed += GetFrameTime();
                float progress = Clamp(inspectMoveElapsed / move->duration, 0.0f, 1.0f);
                float *targetX = move->target == RPG_INSPECT_MOVE_PLAYER ? &player.position.x :
                                 move->target == RPG_INSPECT_MOVE_NPC ? &npc.position.x : &zipper.character.position.x;
                *targetX = inspectMoveStartX + (move->destinationX - inspectMoveStartX) * progress;
                if (progress >= 1.0f) {
                    isInspectMoveRunning = false;
                    inspectFunctionIndex++;
                    if (inspectFunctionIndex >= activeInspect->functionCount) {
                        if (inspectTarget == 2) {
                            zipperFollowsPlayer = true;
                            zipperInspectCompleted = true;
                        } else npcInspectCompleted = true;
                        inspectTarget = -1;
                        inspectFunctionIndex = -1;
                        inspectLineIndex = -1;
                    } else inspectLineIndex = 0;
                }
            }
        }
        int currentMap = (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
        if (currentMap == 3 && previousMap != 3 && stage3Event.enabled && !stage3IntroShown) {
            stage3IntroIndex = 0;
            stage3IntroShown = true;
        }
        previousMap = currentMap;
        if (IsKeyPressed(KEY_E)) {
            if (inspectTarget >= 0 && !isInspectMoveRunning) {
                const RpgInspect *activeInspect = inspectTarget == 2 ? &zipper.inspect : &inspect;
                if (activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_DIALOGUE) {
                    inspectLineIndex++;
                    if (inspectLineIndex >= activeInspect->functions[inspectFunctionIndex].dialogue.lineCount) {
                        inspectFunctionIndex++;
                        inspectLineIndex = 0;
                        if (inspectFunctionIndex >= activeInspect->functionCount) {
                            if (inspectTarget == 2) {
                                zipperFollowsPlayer = true;
                                zipperInspectCompleted = true;
                            } else npcInspectCompleted = true;
                            inspectTarget = -1;
                            inspectFunctionIndex = -1;
                            inspectLineIndex = -1;
                        }
                    }
                }
            } else if (stage3IntroIndex >= 0) {
                stage3IntroIndex++;
                if (stage3IntroIndex >= stage3Event.dialogue.lineCount) stage3IntroIndex = -1;
            } else
            if (dialogueIndex >= 0) {
                dialogueIndex++;
                if (dialogueIndex >= dialogue.lineCount) dialogueIndex = -1;
            } else if (canTalk) {
                dialogueIndex = 0;
            }
        }
        bool canInspectZipper = fabsf(player.position.x - zipper.character.position.x) <= 72.0f;
        if (IsKeyPressed(KEY_I) && inspectTarget < 0 && inspect.enabled && !npcInspectCompleted && canTalk &&
            dialogueIndex < 0 && stage3IntroIndex < 0) {
            inspectTarget = 1;
            inspectFunctionIndex = 0;
            inspectLineIndex = 0;
        } else if (IsKeyPressed(KEY_I) && inspectTarget < 0 && zipper.inspect.enabled && !zipperInspectCompleted && canInspectZipper &&
                   dialogueIndex < 0 && stage3IntroIndex < 0) {
            inspectTarget = 2;
            inspectFunctionIndex = 0;
            inspectLineIndex = 0;
        }
        UpdateRpgCamera(&camera, player.position.x, cameraFollowsPlayer);
        DrawRpgWorld(&player, &npc, &stage, camera, cameraFollowsPlayer, currentMap, canTalk,
                     &dialogue, dialogueIndex, stage3IntroIndex, &stage3Event, &zipper, zipperTexture, &inspect, &zipper.inspect,
                     inspectTarget, inspectFunctionIndex, inspectLineIndex, false, 0.0f, RPG_INSPECT_MOVE_PLAYER,
                     npcInspectCompleted, zipperInspectCompleted);
    }
    UnloadTexture(zipperTexture);
    GameFont_Unload();
    CloseWindow();
    return 0;
}
