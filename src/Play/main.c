// 依存: ../Shered/enemy_group.h、../Shered/enemy_settings.h、../Shered/game_font.h、../Shered/player.h、../Shered/stage.h
#include "raylib.h"
#include "raymath.h"

#include "enemy_group.h"
#include "enemy_settings.h"
#include "game_font.h"
#include "player.h"
#include "stage.h"

enum {
    SCREEN_WIDTH = 960,
    SCREEN_HEIGHT = 540,
    TARGET_FPS = 60,
};

static const float SUBORDINATE_RANGE = 220.0f;

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

static void DrawGame(const Player *player, const EnemyGroup *enemyGroup, const Stage *stage,
                     const Camera2D *camera)
{
    BeginDrawing();
    ClearBackground(SKYBLUE);

    BeginMode2D(*camera);
    Stage_Draw(stage);
    if (Player_IsActionActive(player)) {
        DrawCircleLines((int)player->position.x, (int)stage->groundY,
                        (int)SUBORDINATE_RANGE, Fade(PURPLE, 0.7f));
    }
    EnemyGroup_Draw(enemyGroup, stage->groundY);
    // 主人公を最後に描画し、敵と重なった場合も主人公が前面に見えるようにする。
    Player_Draw(player, stage->groundY);
    EndMode2D();

    DrawRectangle(16, 16, 390, 92, Fade(RAYWHITE, 0.86f));
    GameFont_Draw("A / D または ← / → で移動", 28, 28, 22, DARKGRAY);
    GameFont_Draw("Space: アクション（範囲220px・再生中は操作不可）", 28, 52, 18, DARKGRAY);
    GameFont_Draw(TextFormat("敵: 従属化 %d/%d 体", EnemyGroup_GetSubordinateCount(enemyGroup),
                             ENEMY_GROUP_MAX_COUNT), 28, 76, 16, GRAY);
    EndDrawing();
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "1_44MB - 横スクロールゲーム");
    SetTargetFPS(TARGET_FPS);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));

    Stage stage = Stage_Create();
    Player player = Player_Create((Vector2){ 120.0f, stage.groundY });
    // 実行時のカレントフォルダに左右されないよう、実行ファイルの隣にある assets/Sprite から画像を読む。
    Player_LoadDefaultAppearance(&player,
                                 TextFormat("%s../assets/Sprite/ZIPPER.png",
                                            GetApplicationDirectory()));
    EnemyGroup enemyGroup = EnemyGroup_Create(stage.groundY);
    EnemyGroup_LoadAppearance(&enemyGroup,
                              TextFormat("%s../assets/Sprite/FILE.png", GetApplicationDirectory()));
    EnemyFollowSettings enemySettings = EnemySettings_Default();
    EnemySettings_Load(TextFormat("%s../assets/Settings/enemy_settings.cfg",
                                  GetApplicationDirectory()), &enemySettings);
    EnemyGroup_SetFollowSettings(&enemyGroup, enemySettings.spacing,
                                 enemySettings.interpolationSpeed, enemySettings.subordinateColor);
    Player_SetScale(&player, enemySettings.playerScale);
    EnemyGroup_SetScale(&enemyGroup, enemySettings.enemyScale);
    Camera2D camera = CreateCamera();

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        Player_Update(&player, deltaTime, stage.width);
        if (Player_ConsumeActionStarted(&player)) {
            EnemyGroup_TrySubordinateNearest(&enemyGroup, player.position,
                                             Player_GetFacingDirection(&player),
                                             Player_GetScale(&player), SUBORDINATE_RANGE);
        }
        EnemyGroup_Update(&enemyGroup, deltaTime, player.position,
                          Player_GetFacingDirection(&player), Player_GetScale(&player),
                          Player_GetMoveSpeed(&player));
        GameCamera_Update(&camera, &player, &stage);
        DrawGame(&player, &enemyGroup, &stage, &camera);
    }

    EnemyGroup_UnloadAppearance(&enemyGroup);
    Player_UnloadDefaultAppearance(&player);
    GameFont_Unload();
    CloseWindow();
    return 0;
}
