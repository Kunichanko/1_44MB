// 依存する自プロジェクト内ファイル: rpg_scene.h, ../Shered/game_font.h。
// 役割: タイトルと設定画面を描画し、ゲーム本体とは独立してシーン遷移を処理する。
// 依存関係を更新: rpg_viewport.h を追加した。
#include "rpg_scene.h"

#include <stddef.h>
#include <stdio.h>

#include "game_font.h"
#include "rpg_viewport.h"

static const Rectangle startButton = { 360.0f, 238.0f, 240.0f, 44.0f };
static const Rectangle settingsButton = { 360.0f, 298.0f, 240.0f, 44.0f };
static const Rectangle newGameButton = { 360.0f, 238.0f, 240.0f, 44.0f };
static const Rectangle continueButton = { 360.0f, 298.0f, 240.0f, 44.0f };
static const Rectangle gameSettingsButton = { 842.0f, 506.0f, 94.0f, 26.0f };
static const Rectangle returnToGameButton = { 340.0f, 292.0f, 280.0f, 40.0f };
static const Rectangle returnToTitleButton = { 340.0f, 344.0f, 280.0f, 40.0f };

static void DrawButton(Rectangle bounds, const char *label, bool emphasized);

RpgSceneState RpgScene_Default(void)
{
    RpgSceneState scene = { .kind = RPG_SCENE_TITLE, .allowsTitleReturn = true, .selectedStageNumber = 1,
                            .stageNumbers = { 1 }, .stageCount = 1 };
    return scene;
}

RpgSceneState RpgScene_GameOnly(void)
{
    RpgSceneState scene = { .kind = RPG_SCENE_GAME, .allowsTitleReturn = false, .selectedStageNumber = 1,
                            .stageNumbers = { 1 }, .stageCount = 1 };
    return scene;
}

void RpgScene_Release(RpgSceneState *scene)
{
    if (scene != NULL && scene->hasFrozenBackdrop) UnloadTexture(scene->frozenBackdrop);
    if (scene != NULL) {
        scene->frozenBackdrop = (Texture2D){ 0 };
        scene->hasFrozenBackdrop = false;
    }
}

void RpgScene_RegisterText(void)
{
    GameFont_AddText(u8"ステージを選択");
    GameFont_AddText(u8"をビルドする");
    GameFont_AddText(u8"をビルドする");
    // 日本語表示を追加する前に、フォントの文字セットへ必要な文字を登録する。
    GameFont_AddText("始める設定初めから続きから戻るタイトルへ");
}

bool RpgScene_IsGameScene(const RpgSceneState *scene)
{
    // 設定中は本編シーンではないため、ゲーム更新ループへ入れない。
    return scene != NULL && scene->kind == RPG_SCENE_GAME;
}

void RpgScene_SetStageNumber(RpgSceneState *scene, int stageNumber)
{
    if (scene != NULL && stageNumber > 0) scene->selectedStageNumber = stageNumber;
}

void RpgScene_SetStageList(RpgSceneState *scene, const int *stageNumbers, int stageCount)
{
    if (scene == NULL || stageNumbers == NULL || stageCount <= 0) return;
    if (stageCount > RPG_SCENE_MAX_STAGE_BUTTONS) stageCount = RPG_SCENE_MAX_STAGE_BUTTONS;
    scene->stageCount = stageCount;
    for (int index = 0; index < stageCount; index++) scene->stageNumbers[index] = stageNumbers[index];
    for (int index = 0; index < stageCount; index++)
        if (scene->stageNumbers[index] == scene->selectedStageNumber) return;
    scene->selectedStageNumber = scene->stageNumbers[0];
}

bool RpgScene_ConsumeGameReset(RpgSceneState *scene)
{
    bool requiresReset;
    if (scene == NULL) return false;
    requiresReset = scene->requiresGameReset;
    scene->requiresGameReset = false;
    return requiresReset;
}

bool RpgScene_ConsumeContinueLoad(RpgSceneState *scene)
{
    bool requiresLoad;
    if (scene == NULL) return false;
    requiresLoad = scene->requiresContinueLoad;
    scene->requiresContinueLoad = false;
    return requiresLoad;
}

bool RpgScene_IsGameSettings(const RpgSceneState *scene)
{
    return scene != NULL && scene->kind == RPG_SCENE_GAME_SETTINGS;
}

bool RpgScene_TryOpenGameSettings(RpgSceneState *scene)
{
    if (scene == NULL || scene->kind != RPG_SCENE_GAME || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
        !CheckCollisionPointRec(RpgViewport_GetMousePosition(), gameSettingsButton)) return false;
    // 更新停止中も直前のゲーム状態を確認できるよう、画面を一枚だけ保存する。
    RpgScene_Release(scene);
    Image screenshot = LoadImageFromScreen();
    if (screenshot.data != NULL) {
        scene->frozenBackdrop = LoadTextureFromImage(screenshot);
        scene->hasFrozenBackdrop = scene->frozenBackdrop.id != 0;
        UnloadImage(screenshot);
    }
    scene->kind = RPG_SCENE_GAME_SETTINGS;
    return true;
}

bool RpgScene_UpdateGameSettings(RpgSceneState *scene)
{
    if (scene == NULL || scene->kind != RPG_SCENE_GAME_SETTINGS) return false;
    if (IsKeyPressed(KEY_ESCAPE)) {
        scene->kind = RPG_SCENE_GAME;
        RpgScene_Release(scene);
        return true;
    }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    Vector2 pointer = RpgViewport_GetMousePosition();
    if (CheckCollisionPointRec(pointer, returnToGameButton)) {
        scene->kind = RPG_SCENE_GAME;
        RpgScene_Release(scene);
    } else if (scene->allowsTitleReturn && CheckCollisionPointRec(pointer, returnToTitleButton)) {
        scene->kind = RPG_SCENE_TITLE;
        RpgScene_Release(scene);
    }
    // 設定オーバーレイ中のクリックは、背面のゲームやエディターに渡さない。
    return true;
}

void RpgScene_DrawGameSettingsButton(void)
{
    DrawRectangleRec(gameSettingsButton, Fade(DARKBLUE, 0.90f));
    DrawRectangleLinesEx(gameSettingsButton, 1.0f, RAYWHITE);
    GameFont_Draw("設定", 868.0f, 510.0f, 18.0f, RAYWHITE);
}

void RpgScene_DrawGameSettingsOverlay(const RpgSceneState *scene)
{
    if (!RpgScene_IsGameSettings(scene)) return;
    // 背面の状態を視認できる濃さに限定した、設定用の半透明マスク。
    DrawRectangle(0, 0, RpgViewport_GetWidth(), RpgViewport_GetHeight(), Fade(BLACK, 0.42f));
    GameFont_Draw("設定", 430.0f, 220.0f, 32.0f, RAYWHITE);
    DrawButton(returnToGameButton, "戻る", true);
    if (scene->allowsTitleReturn) DrawButton(returnToTitleButton, "タイトルへ", false);
}

static void DrawBackground(void)
{
    ClearBackground((Color){ 31, 48, 76, 255 });
    DrawCircle(760, 98, 66, Fade(SKYBLUE, 0.24f));
    DrawCircle(134, 446, 110, Fade(PURPLE, 0.16f));
}

static void DrawButton(Rectangle bounds, const char *label, bool emphasized)
{
    bool hovered = CheckCollisionPointRec(RpgViewport_GetMousePosition(), bounds);
    Color color = emphasized ? DARKBLUE : DARKGRAY;
    if (hovered) color = BLUE;
    DrawRectangleRec(bounds, color);
    DrawRectangleLinesEx(bounds, 2.0f, RAYWHITE);
    float textWidth = GameFont_MeasureText(label, 22.0f).x;
    GameFont_Draw(label, bounds.x + (bounds.width - textWidth) * 0.5f, bounds.y + 11.0f,
                  22.0f, RAYWHITE);
}

static void DrawTitle(void)
{
    DrawText("RPG", 388, 112, 72, RAYWHITE);
    DrawText("RPG", 392, 108, 72, SKYBLUE);
}

static Rectangle GetBuildStageButton(int visibleIndex)
{
    return (Rectangle){ 248.0f + (visibleIndex % 2) * 236.0f,
                        174.0f + (visibleIndex / 2) * 58.0f, 228.0f, 44.0f };
}

static void DrawTitleScene(RpgSceneState *scene)
{
    DrawTitle();
    if (scene->kind == RPG_SCENE_TITLE) {
        DrawButton(startButton, "始める", true);
        DrawButton(settingsButton, "設定", false);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(RpgViewport_GetMousePosition(), startButton)) scene->kind = RPG_SCENE_TITLE_START_MENU;
            else if (CheckCollisionPointRec(RpgViewport_GetMousePosition(), settingsButton)) scene->kind = RPG_SCENE_TITLE_SETTINGS;
        }
    } else if (scene->kind == RPG_SCENE_TITLE_START_MENU) {
        DrawButton(newGameButton, "初めから", true);
        DrawButton(continueButton, "続きから", false);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(RpgViewport_GetMousePosition(), newGameButton))
            scene->kind = RPG_SCENE_TITLE_STAGE_BUILD;
        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(RpgViewport_GetMousePosition(), continueButton)) {
            scene->requiresContinueLoad = true;
            scene->requiresGameReset = true;
            scene->kind = RPG_SCENE_GAME;
        }
        if (IsKeyPressed(KEY_ESCAPE)) scene->kind = RPG_SCENE_TITLE;
    } else if (scene->kind == RPG_SCENE_TITLE_STAGE_BUILD) {
        const int visibleButtonCount = 8;
        int maxScroll = scene->stageCount > visibleButtonCount ? scene->stageCount - visibleButtonCount : 0;
        if (CheckCollisionPointRec(RpgViewport_GetMousePosition(), (Rectangle){ 232.0f, 154.0f, 496.0f, 280.0f })) {
            scene->stageButtonScroll -= (int)GetMouseWheelMove() * 2;
            if (scene->stageButtonScroll < 0) scene->stageButtonScroll = 0;
            if (scene->stageButtonScroll > maxScroll) scene->stageButtonScroll = maxScroll;
        }
        GameFont_Draw("ステージを選択", 380.0f, 126.0f, 24.0f, RAYWHITE);
        for (int visibleIndex = 0; visibleIndex < visibleButtonCount; visibleIndex++) {
            int stageIndex = scene->stageButtonScroll + visibleIndex;
            if (stageIndex >= scene->stageCount) break;
            char label[96];
            Rectangle button = GetBuildStageButton(visibleIndex);
            snprintf(label, sizeof(label), "[Stage%d]をビルドする", scene->stageNumbers[stageIndex]);
            DrawButton(button, label, true);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(RpgViewport_GetMousePosition(), button)) {
                scene->selectedStageNumber = scene->stageNumbers[stageIndex];
                scene->requiresGameReset = true;
                scene->kind = RPG_SCENE_GAME;
            }
        }
        if (maxScroll > 0) {
            DrawText(TextFormat("%d-%d / %d", scene->stageButtonScroll + 1,
                                scene->stageButtonScroll + visibleButtonCount < scene->stageCount ?
                                scene->stageButtonScroll + visibleButtonCount : scene->stageCount,
                                scene->stageCount), 662, 126, 14, LIGHTGRAY);
        }
        if (IsKeyPressed(KEY_ESCAPE)) scene->kind = RPG_SCENE_TITLE_START_MENU;
    } else {
        // タイトル設定の項目は将来追加する。現在はゲーム状態を更新しない独立シーンとして表示する。
        if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) scene->kind = RPG_SCENE_TITLE;
    }
}

static void DrawGameSettingsScene(RpgSceneState *scene)
{
    if (scene->hasFrozenBackdrop)
        DrawTexturePro(scene->frozenBackdrop,
                       (Rectangle){ 0.0f, 0.0f, (float)scene->frozenBackdrop.width, (float)scene->frozenBackdrop.height },
                       (Rectangle){ 0.0f, 0.0f, (float)RpgViewport_GetWidth(), (float)RpgViewport_GetHeight() },
                       (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    else DrawBackground();
    RpgScene_DrawGameSettingsOverlay(scene);
}

void RpgScene_UpdateAndDraw(RpgSceneState *scene)
{
    if (scene == NULL) return;
    RpgViewport_BeginFrame();
    if (scene->kind == RPG_SCENE_GAME_SETTINGS) DrawGameSettingsScene(scene);
    else {
        DrawBackground();
        DrawTitleScene(scene);
    }
    RpgViewport_EndFrame();
}
