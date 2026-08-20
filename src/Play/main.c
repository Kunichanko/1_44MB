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

typedef struct MergeSelection {
    bool isActive;
    bool hasSelection;
    int startColumn;
    int startRow;
    int endColumn;
    int endRow;
} MergeSelection;

static Rectangle GetMergeSelectionBounds(const MergeSelection *selection)
{
    int columnDifference = selection->endColumn - selection->startColumn;
    int rowDifference = selection->endRow - selection->startRow;
    int size = columnDifference < 0 ? -columnDifference : columnDifference;
    int rowSize = rowDifference < 0 ? -rowDifference : rowDifference;
    if (rowSize > size) {
        size = rowSize;
    }
    size++;
    int column = columnDifference < 0 ? selection->startColumn - size + 1 : selection->startColumn;
    int row = rowDifference < 0 ? selection->startRow - size + 1 : selection->startRow;
    return (Rectangle){ column * STAGE_GRID_TILE_SIZE, row * STAGE_GRID_TILE_SIZE,
                        size * STAGE_GRID_TILE_SIZE, size * STAGE_GRID_TILE_SIZE };
}

static void UpdateMergeSelection(MergeSelection *selection, Stage *stage, Camera2D camera)
{
    if (IsKeyPressed(KEY_R)) {
        selection->isActive = !selection->isActive;
        selection->hasSelection = false;
    }
    if (!selection->isActive) {
        return;
    }
    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera);
    int column = 0;
    int row = 0;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        Stage_GetCellAtWorldPosition(mouseWorld, &column, &row)) {
        selection->startColumn = column;
        selection->startRow = row;
        selection->endColumn = column;
        selection->endRow = row;
        selection->hasSelection = true;
    }
    if (selection->hasSelection && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        Stage_GetCellAtWorldPosition(mouseWorld, &column, &row)) {
        selection->endColumn = column;
        selection->endRow = row;
    }
    if (selection->hasSelection && IsKeyPressed(KEY_ENTER)) {
        Rectangle bounds = GetMergeSelectionBounds(selection);
        Stage_MergeSquare(stage, (int)(bounds.x / STAGE_GRID_TILE_SIZE),
                          (int)(bounds.y / STAGE_GRID_TILE_SIZE),
                          (int)(bounds.width / STAGE_GRID_TILE_SIZE));
        selection->hasSelection = false;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        selection->isActive = false;
        selection->hasSelection = false;
    }
}

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

static void UpdateStageEnemyTiles(Stage *stage, const EnemyGroup *enemyGroup)
{
    Stage_ClearEnemyTiles(stage);
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (enemyGroup->enemies[index].isActive) {
            Stage_SetEnemyTile(stage, enemyGroup->enemies[index].position);
        }
    }
}

static void ApplyStageEnemySpawns(const Stage *stage, EnemyGroup *enemyGroup)
{
    Vector2 positions[ENEMY_GROUP_MAX_COUNT];
    int count = Stage_GetEnemySpawnPositions(stage, positions, ENEMY_GROUP_MAX_COUNT);
    EnemyGroup_SetSpawnPositions(enemyGroup, positions, count, STAGE_GRID_TILE_SIZE * 1.5f);
}

static void DrawGame(const Player *player, const EnemyGroup *enemyGroup, const Stage *stage,
                     const Camera2D *camera, const MergeSelection *selection, bool showGrid,
                     float gridOverlayOpacity)
{
    BeginDrawing();
    ClearBackground(SKYBLUE);

    BeginMode2D(*camera);
    Stage_Draw(stage);
    if (selection->isActive && selection->hasSelection) {
        Rectangle bounds = GetMergeSelectionBounds(selection);
        DrawRectangleRec(bounds, Fade(ORANGE, 0.25f));
        DrawRectangleLinesEx(bounds, 3.0f, ORANGE);
    }
    if (Player_IsActionActive(player)) {
        DrawCircleLines((int)player->position.x, (int)stage->groundY,
                        (int)SUBORDINATE_RANGE, Fade(PURPLE, 0.7f));
    }
    EnemyGroup_Draw(enemyGroup, stage->groundY);
    // 主人公を最後に描画し、敵と重なった場合も主人公が前面に見えるようにする。
    Player_Draw(player, stage->groundY);
    if (showGrid) {
        Stage_DrawGridOverlay(stage, gridOverlayOpacity);
    }
    EndMode2D();

    if (selection->isActive) {
        DrawText("Merge mode: drag square / Enter confirm / Esc cancel", 410, 18, 16, MAROON);
    }

    DrawRectangle(16, 16, 390, 92, Fade(RAYWHITE, 0.86f));
    GameFont_Draw("A / D または ← / → で移動", 28, 28, 22, DARKGRAY);
    GameFont_Draw("Space: アクション（範囲220px・再生中は操作不可）", 28, 52, 18, DARKGRAY);
    GameFont_Draw(TextFormat("敵: 従属化 %d/%d 体", EnemyGroup_GetSubordinateCount(enemyGroup),
                             EnemyGroup_GetActiveCount(enemyGroup)), 28, 76, 16, GRAY);
    EndDrawing();
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "1_44MB - 横スクロールゲーム");
    SetTargetFPS(TARGET_FPS);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));

    Stage stage = Stage_Create();
    Stage_Load(TextFormat("%s../assets/Settings/Stage/stage_grid.cfg", GetApplicationDirectory()),
               &stage);
    Player player = Player_Create(Stage_GetCellCenter(1, 3));
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
    ApplyStageEnemySpawns(&stage, &enemyGroup);
    UpdateStageEnemyTiles(&stage, &enemyGroup);
    Camera2D camera = CreateCamera();
    bool showGrid = false;
    float gridOverlayOpacity = enemySettings.gridOverlayOpacity;
    MergeSelection mergeSelection = {0};

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_Q)) {
            showGrid = !showGrid;
        }
        UpdateMergeSelection(&mergeSelection, &stage, camera);
        float deltaTime = GetFrameTime();
        if (!mergeSelection.isActive) {
        Player_Update(&player, deltaTime, stage.width);
        if (Player_ConsumeActionStarted(&player)) {
            EnemyGroup_TrySubordinateNearest(&enemyGroup, player.position,
                                             Player_GetFacingDirection(&player),
                                             Player_GetScale(&player), SUBORDINATE_RANGE);
        }
        EnemyGroup_Update(&enemyGroup, deltaTime, player.position,
                          Player_GetFacingDirection(&player), Player_GetScale(&player),
                          Player_GetMoveSpeed(&player));
        }
        UpdateStageEnemyTiles(&stage, &enemyGroup);
        GameCamera_Update(&camera, &player, &stage);
        DrawGame(&player, &enemyGroup, &stage, &camera, &mergeSelection, showGrid,
                 gridOverlayOpacity);
    }

    EnemyGroup_UnloadAppearance(&enemyGroup);
    Player_UnloadDefaultAppearance(&player);
    GameFont_Unload();
    CloseWindow();
    return 0;
}
// 役割: 横スクロールゲームの起動、更新、描画を制御するエントリーポイント。
