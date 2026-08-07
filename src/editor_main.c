// 依存: editor_ui.h、file_dialog.h、player.h、stage.h
#include "raylib.h"
#include "raymath.h"

#include "editor_ui.h"
#include "file_dialog.h"
#include "player.h"
#include "stage.h"

#include <stdbool.h>
#include <stdio.h>

enum {
    SCREEN_WIDTH = 960,
    SCREEN_HEIGHT = 540,
    TARGET_FPS = 60,
    APPEARANCE_PATH_SIZE = 1024,
};

static Camera2D CreateCamera(void)
{
    Camera2D camera = {0};
    camera.offset = (Vector2){ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.zoom = 1.0f;
    return camera;
}

static void GameCamera_Update(Camera2D *camera, const Player *player, const Stage *stage)
{
    float minimumTargetX = SCREEN_WIDTH / 2.0f;
    float maximumTargetX = stage->width - SCREEN_WIDTH / 2.0f;

    camera->target.x = Clamp(player->position.x, minimumTargetX, maximumTargetX);
    camera->target.y = SCREEN_HEIGHT / 2.0f;
}

static bool IsPlayerClicked(const Player *player, const Stage *stage, const Camera2D *camera)
{
    Vector2 worldMousePosition = GetScreenToWorld2D(GetMousePosition(), *camera);
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(worldMousePosition, Player_GetBounds(player, stage->groundY));
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "1_44MB - ゲームエディター");
    SetTargetFPS(TARGET_FPS);

    Stage stage = Stage_Create();
    Player player = Player_Create((Vector2){ 280.0f, stage.groundY });
    Camera2D camera = CreateCamera();
    bool playerSelected = false;
    char appearancePath[APPEARANCE_PATH_SIZE] = "未指定";
    char message[128] = "主人公をクリックして選択";

    while (!WindowShouldClose()) {
        GameCamera_Update(&camera, &player, &stage);

        if (IsPlayerClicked(&player, &stage, &camera)) {
            playerSelected = true;
            snprintf(message, sizeof(message), "主人公を選択しました");
        }

        BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode2D(camera);
        Stage_Draw(&stage);
        Player_Draw(&player, stage.groundY);
        if (playerSelected) {
            DrawRectangleLinesEx(Player_GetBounds(&player, stage.groundY), 3.0f, BLUE);
        }
        EndMode2D();

        EditorUI_DrawHint(playerSelected);
        bool requestedFile = EditorUI_DrawInspector(&player, appearancePath, playerSelected, message);
        EndDrawing();

        // ネイティブの選択ダイアログは描画フレームを閉じた後に開き、PNG を安全に差し替える。
        if (requestedFile && FileDialog_SelectPng(appearancePath, sizeof(appearancePath))) {
            if (Player_SetAppearance(&player, appearancePath)) {
                snprintf(message, sizeof(message), "PNG を適用しました");
            } else {
                snprintf(message, sizeof(message), "PNG を読み込めませんでした");
            }
        }
    }

    Player_UnloadAppearance(&player);
    CloseWindow();
    return 0;
}
