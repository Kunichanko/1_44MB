// 依存: player.h、stage.h
#include "raylib.h"
#include "raymath.h"

#include "player.h"
#include "stage.h"

enum {
    SCREEN_WIDTH = 960,
    SCREEN_HEIGHT = 540,
    TARGET_FPS = 60,
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

    // 画面端で背景だけが見えないよう、カメラの追従位置をステージ幅に合わせて固定する。
    camera->target.x = Clamp(player->position.x, minimumTargetX, maximumTargetX);
    camera->target.y = SCREEN_HEIGHT / 2.0f;
}

static void DrawGame(const Player *player, const Stage *stage, const Camera2D *camera)
{
    BeginDrawing();
    ClearBackground(SKYBLUE);

    BeginMode2D(*camera);
    Stage_Draw(stage);
    Player_Draw(player, stage->groundY);
    EndMode2D();

    DrawRectangle(16, 16, 380, 62, Fade(RAYWHITE, 0.86f));
    DrawText("A / D または ← / → で移動", 28, 28, 22, DARKGRAY);
    DrawText("仮キャラクター・横スクロール土台", 28, 52, 16, GRAY);
    EndDrawing();
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "1_44MB - 横スクロールゲーム");
    SetTargetFPS(TARGET_FPS);

    Stage stage = Stage_Create();
    Player player = Player_Create((Vector2){ 120.0f, stage.groundY });
    Camera2D camera = CreateCamera();

    while (!WindowShouldClose()) {
        Player_Update(&player, GetFrameTime(), stage.width);
        GameCamera_Update(&camera, &player, &stage);
        DrawGame(&player, &stage, &camera);
    }

    CloseWindow();
    return 0;
}
