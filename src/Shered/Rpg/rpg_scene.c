// 依存する自プロジェクト内ファイル: rpg_scene.h, ../Shered/game_font.h。
// 役割: タイトルと設定画面を描画し、ゲーム本体とは独立してシーン遷移を処理する。
// 依存関係を更新: rpg_viewport.h を追加した。
#include "rpg_scene.h"

#include <stddef.h>
#include <stdio.h>

#include "game_font.h"
#include "rpg_viewport.h"

static Rectangle gameSettingsButton = { 842.0f, 506.0f, 94.0f, 26.0f };

static void DrawButton(Rectangle bounds, const char *label, bool emphasized);
static Rectangle GetCenteredButton(float y, float width, float height);

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
    GameFont_AddText(u8"始める");
    GameFont_AddText(u8"設定");
    GameFont_AddText(u8"初めから");
    GameFont_AddText(u8"続きから");
    GameFont_AddText(u8"戻る");
    GameFont_AddText(u8"タイトルへ");
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

void RpgScene_SetGameSettingsButtonBounds(Rectangle bounds)
{
    // 本編とエディターで異なる仮想表示サイズを使っても、描画と入力で同じ設定ボタン矩形を共有する。
    if (bounds.width > 0.0f && bounds.height > 0.0f) gameSettingsButton = bounds;
}

bool RpgScene_TryOpenGameSettings(RpgSceneState *scene)
{
    if (scene == NULL || scene->kind != RPG_SCENE_GAME || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
        !CheckCollisionPointRec(RpgViewport_GetMousePosition(), gameSettingsButton)) return false;
    // DPI依存の画面キャプチャではなく、論理ビューポートの直前フレームを保存する。
    RpgScene_Release(scene);
    scene->frozenBackdrop = RpgViewport_CopyLastFrameTexture();
    scene->hasFrozenBackdrop = scene->frozenBackdrop.id != 0;
    scene->kind = RPG_SCENE_GAME_SETTINGS;
    return true;
}

bool RpgScene_UpdateGameSettings(RpgSceneState *scene)
{
    Rectangle returnToGameButton = GetCenteredButton(292.0f, 280.0f, 40.0f);
    Rectangle returnToTitleButton = GetCenteredButton(344.0f, 280.0f, 40.0f);
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
    Vector2 labelSize;
    DrawRectangleRec(gameSettingsButton, Fade(DARKBLUE, 0.90f));
    DrawRectangleLinesEx(gameSettingsButton, 1.0f, RAYWHITE);
    labelSize = GameFont_MeasureText(u8"設定", 18.0f);
    GameFont_Draw(u8"設定", gameSettingsButton.x + (gameSettingsButton.width - labelSize.x) * 0.5f,
                  gameSettingsButton.y + (gameSettingsButton.height - labelSize.y) * 0.5f,
                  18.0f, RAYWHITE);
}

void RpgScene_DrawGameSettingsOverlay(const RpgSceneState *scene)
{
    Rectangle returnToGameButton = GetCenteredButton(292.0f, 280.0f, 40.0f);
    Rectangle returnToTitleButton = GetCenteredButton(344.0f, 280.0f, 40.0f);
    Vector2 titleSize;
    if (!RpgScene_IsGameSettings(scene)) return;
    // 背面の状態を視認できる濃さに限定した、設定用の半透明マスク。
    DrawRectangle(0, 0, RpgViewport_GetWidth(), RpgViewport_GetHeight(), Fade(BLACK, 0.42f));
    titleSize = GameFont_MeasureText(u8"設定", 32.0f);
    GameFont_Draw(u8"設定", ((float)RpgViewport_GetWidth() - titleSize.x) * 0.5f, 220.0f, 32.0f, RAYWHITE);
    DrawButton(returnToGameButton, u8"戻る", true);
    if (scene->allowsTitleReturn) DrawButton(returnToTitleButton, u8"タイトルへ", false);
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

static Rectangle GetCenteredButton(float y, float width, float height)
{
    return (Rectangle){ ((float)RpgViewport_GetWidth() - width) * 0.5f, y, width, height };
}

static void DrawTitle(void)
{
    int titleWidth = MeasureText("RPG", 72);
    int titleX = (RpgViewport_GetWidth() - titleWidth) / 2;
    DrawText("RPG", titleX - 4, 112, 72, RAYWHITE);
    DrawText("RPG", titleX, 108, 72, SKYBLUE);
}

static Rectangle GetBuildStageButton(int visibleIndex)
{
    return (Rectangle){ ((float)RpgViewport_GetWidth() - 464.0f) * 0.5f + (visibleIndex % 2) * 236.0f,
                        174.0f + (visibleIndex / 2) * 58.0f, 228.0f, 44.0f };
}

static void DrawTitleScene(RpgSceneState *scene)
{
    Rectangle startButton = GetCenteredButton(238.0f, 240.0f, 44.0f);
    Rectangle settingsButton = GetCenteredButton(298.0f, 240.0f, 44.0f);
    Rectangle newGameButton = GetCenteredButton(238.0f, 240.0f, 44.0f);
    Rectangle continueButton = GetCenteredButton(298.0f, 240.0f, 44.0f);
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
        Rectangle stageListBounds = { ((float)RpgViewport_GetWidth() - 496.0f) * 0.5f, 154.0f, 496.0f, 280.0f };
        int maxScroll = scene->stageCount > visibleButtonCount ? scene->stageCount - visibleButtonCount : 0;
        if (CheckCollisionPointRec(RpgViewport_GetMousePosition(), stageListBounds)) {
            scene->stageButtonScroll -= (int)GetMouseWheelMove() * 2;
            if (scene->stageButtonScroll < 0) scene->stageButtonScroll = 0;
            if (scene->stageButtonScroll > maxScroll) scene->stageButtonScroll = maxScroll;
        }
        Vector2 headingSize = GameFont_MeasureText(u8"ステージを選択", 24.0f);
        GameFont_Draw(u8"ステージを選択", ((float)RpgViewport_GetWidth() - headingSize.x) * 0.5f, 126.0f, 24.0f, RAYWHITE);
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
                                scene->stageCount), RpgViewport_GetWidth() - 66, 126, 14, LIGHTGRAY);
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
                       (Rectangle){ 0.0f, 0.0f, (float)scene->frozenBackdrop.width, -(float)scene->frozenBackdrop.height },
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
