// 依存: editor_ui.h、file_dialog.h、../Shered/enemy_group.h、../Shered/enemy_settings.h、../Shered/game_font.h、../Shered/player.h、../Shered/stage.h
#include "raylib.h"
#include "raymath.h"

#include "editor_ui.h"
#include "file_dialog.h"
#include "enemy_group.h"
#include "enemy_settings.h"
#include "game_font.h"
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

typedef struct EditorState {
    Stage stage;
    Player player;
    EnemyGroup enemyGroup;
    Camera2D camera;
    bool playerSelected;
    bool enemySelected;
    bool globalSelected;
    bool isPlaying;
    bool showGrid;
    float gridOverlayOpacity;
    int selectedTileType;
    bool isGridEditing;
    bool inspectorOpenedThisFrame;
    char appearancePath[APPEARANCE_PATH_SIZE];
    char message[128];
} EditorState;

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

static bool IsEnemyGroupClicked(const EnemyGroup *group, float groundY, Vector2 worldMousePosition)
{
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (group->enemies[index].isActive && CheckCollisionPointRec(worldMousePosition,
                                   Enemy_GetBounds(&group->enemies[index], groundY))) {
            return true;
        }
    }

    return false;
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

static bool IsInsideSelectedInspector(const EditorState *editor, Vector2 screenMousePosition)
{
    const Rectangle playerInspector = { 620.0f, 24.0f, 316.0f, 360.0f };
    const Rectangle enemyInspector = { 620.0f, 24.0f, 316.0f, 490.0f };
    const Rectangle globalInspector = { 620.0f, 24.0f, 316.0f, 404.0f };

    if (editor->playerSelected) {
        return CheckCollisionPointRec(screenMousePosition, playerInspector);
    }
    if (editor->enemySelected) {
        return CheckCollisionPointRec(screenMousePosition, enemyInspector);
    }
    if (editor->globalSelected) {
        return CheckCollisionPointRec(screenMousePosition, globalInspector);
    }
    return false;
}

static EditorState Editor_Create(void)
{
    EditorState editor = {0};
    editor.stage = Stage_Create();
    Stage_Load(TextFormat("%s../assets/Settings/stage_grid.cfg", GetApplicationDirectory()),
               &editor.stage);
    editor.player = Player_Create(Stage_GetCellCenter(7, 7));
    Player_LoadDefaultAppearance(&editor.player,
                                 TextFormat("%s../assets/Sprite/ZIPPER.png",
                                            GetApplicationDirectory()));
    editor.enemyGroup = EnemyGroup_Create(editor.stage.groundY);
    EnemyGroup_LoadAppearance(&editor.enemyGroup,
                              TextFormat("%s../assets/Sprite/FILE.png", GetApplicationDirectory()));
    EnemyFollowSettings settings = EnemySettings_Default();
    EnemySettings_Load(TextFormat("%s../assets/Settings/enemy_settings.cfg",
                                  GetApplicationDirectory()), &settings);
    EnemyGroup_SetFollowSettings(&editor.enemyGroup, settings.spacing,
                                 settings.interpolationSpeed, settings.subordinateColor);
    Player_SetScale(&editor.player, settings.playerScale);
    EnemyGroup_SetScale(&editor.enemyGroup, settings.enemyScale);
    editor.gridOverlayOpacity = settings.gridOverlayOpacity;
    ApplyStageEnemySpawns(&editor.stage, &editor.enemyGroup);
    UpdateStageEnemyTiles(&editor.stage, &editor.enemyGroup);
    editor.camera = CreateCamera();
    snprintf(editor.appearancePath, sizeof(editor.appearancePath), "未指定");
    snprintf(editor.message, sizeof(editor.message), "主人公をクリックして選択");
    return editor;
}

static bool Editor_SaveSettings(EditorState *editor)
{
    EnemyFollowSettings settings = {
        .spacing = EnemyGroup_GetFollowSpacing(&editor->enemyGroup),
        .interpolationSpeed = EnemyGroup_GetFollowInterpolationSpeed(&editor->enemyGroup),
        .subordinateColor = EnemyGroup_GetSubordinateColor(&editor->enemyGroup),
        .playerScale = Player_GetScale(&editor->player),
        .enemyScale = EnemyGroup_GetScale(&editor->enemyGroup),
        .gridOverlayOpacity = editor->gridOverlayOpacity,
    };

    bool settingsSaved = EnemySettings_Save(TextFormat("%s../assets/Settings/enemy_settings.cfg",
                                                       GetApplicationDirectory()), &settings);
    bool stageSaved = Stage_Save(&editor->stage,
                                 TextFormat("%s../assets/Settings/stage_grid.cfg",
                                            GetApplicationDirectory()));
    if (settingsSaved && stageSaved) {
        snprintf(editor->message, sizeof(editor->message), "敵設定を保存しました");
        return true;
    } else {
        snprintf(editor->message, sizeof(editor->message), "敵設定を保存できませんでした");
        return false;
    }
}

static void Editor_ApplySelectedPng(EditorState *editor)
{
    // ネイティブの選択ダイアログは描画フレームを閉じた後に開き、PNG を安全に差し替える。
    if (FileDialog_SelectPng(editor->appearancePath, sizeof(editor->appearancePath))) {
        if (Player_SetAppearance(&editor->player, editor->appearancePath)) {
            snprintf(editor->message, sizeof(editor->message), "PNG を適用しました");
        } else {
            snprintf(editor->message, sizeof(editor->message), "PNG を読み込めませんでした");
        }
    }
}

static void Editor_Update(EditorState *editor)
{
    float deltaTime = GetFrameTime();

    if (IsKeyPressed(KEY_Q)) {
        editor->showGrid = !editor->showGrid;
    }

    if (!editor->isPlaying && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 screenMousePosition = GetMousePosition();
        const Rectangle globalButton = { 170.0f, 82.0f, 140.0f, 38.0f };
        if (CheckCollisionPointRec(screenMousePosition, globalButton)) {
            EditorUI_ResetInput();
            editor->playerSelected = false;
            editor->enemySelected = false;
            editor->globalSelected = !editor->globalSelected;
            editor->isGridEditing = false;
            editor->inspectorOpenedThisFrame = true;
        } else if (editor->globalSelected && editor->isGridEditing &&
                   !IsInsideSelectedInspector(editor, screenMousePosition)) {
            Vector2 worldMousePosition = GetScreenToWorld2D(screenMousePosition, editor->camera);
            if (Stage_SetTileAtWorldPosition(&editor->stage, worldMousePosition,
                                             editor->selectedTileType)) {
                ApplyStageEnemySpawns(&editor->stage, &editor->enemyGroup);
                UpdateStageEnemyTiles(&editor->stage, &editor->enemyGroup);
                snprintf(editor->message, sizeof(editor->message), "Grid tile updated");
            }
        } else if (editor->playerSelected || editor->enemySelected || editor->globalSelected) {
            // インスペクターの外側を押した時は、UI操作へ渡さず選択を閉じる。
            if (!IsInsideSelectedInspector(editor, screenMousePosition)) {
                editor->playerSelected = false;
                editor->enemySelected = false;
                editor->globalSelected = false;
                editor->isGridEditing = false;
                EditorUI_ResetInput();
            }
        } else {
            // 選択を開いたフレームでは、そのクリックをインスペクターのボタンへ渡さない。
            Vector2 worldMousePosition = GetScreenToWorld2D(screenMousePosition, editor->camera);
            if (CheckCollisionPointRec(worldMousePosition,
                                       Player_GetBounds(&editor->player, editor->stage.groundY))) {
                EditorUI_ResetInput();
                editor->playerSelected = true;
                editor->globalSelected = false;
                editor->isGridEditing = false;
                editor->inspectorOpenedThisFrame = true;
                snprintf(editor->message, sizeof(editor->message), "主人公を選択しました");
            } else if (IsEnemyGroupClicked(&editor->enemyGroup, editor->stage.groundY,
                                           worldMousePosition)) {
                EditorUI_ResetInput();
                editor->enemySelected = true;
                editor->globalSelected = false;
                editor->isGridEditing = false;
                editor->inspectorOpenedThisFrame = true;
                snprintf(editor->message, sizeof(editor->message), "敵を選択しました");
            }
        }
    }

    if (editor->isPlaying) {
        Player_Update(&editor->player, deltaTime, editor->stage.width);
        if (Player_ConsumeActionStarted(&editor->player)) {
            EnemyGroup_TrySubordinateNearest(&editor->enemyGroup, editor->player.position,
                                             Player_GetFacingDirection(&editor->player),
                                             Player_GetScale(&editor->player), 220.0f);
        }
    }

    if (editor->isPlaying) {
        EnemyGroup_Update(&editor->enemyGroup, deltaTime, editor->player.position,
                          Player_GetFacingDirection(&editor->player),
                          Player_GetScale(&editor->player),
                          Player_GetMoveSpeed(&editor->player));
    }
    UpdateStageEnemyTiles(&editor->stage, &editor->enemyGroup);
    GameCamera_Update(&editor->camera, &editor->player, &editor->stage);
}

static bool Editor_Draw(EditorState *editor)
{
    BeginDrawing();
    ClearBackground(SKYBLUE);
    BeginMode2D(editor->camera);
    Stage_Draw(&editor->stage);
    EnemyGroup_Draw(&editor->enemyGroup, editor->stage.groundY);
    Player_Draw(&editor->player, editor->stage.groundY);
    if (editor->playerSelected) {
        DrawRectangleLinesEx(Player_GetBounds(&editor->player, editor->stage.groundY), 3.0f, BLUE);
    }
    if (editor->enemySelected) {
        for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
            if (editor->enemyGroup.enemies[index].isActive) {
                DrawRectangleLinesEx(Enemy_GetBounds(&editor->enemyGroup.enemies[index],
                                                     editor->stage.groundY), 3.0f, PURPLE);
            }
        }
    }
    if (editor->showGrid) {
        Stage_DrawGridOverlay(&editor->stage, editor->gridOverlayOpacity);
    }
    EndMode2D();

    EditorUI_DrawHint(editor->playerSelected, editor->enemySelected, editor->isPlaying);
    bool inspectorAcceptsInput = !editor->inspectorOpenedThisFrame;
    bool requestedPlayerSave = false;
    bool requestedFile = EditorUI_DrawInspector(&editor->player, editor->appearancePath,
                                                editor->playerSelected, inspectorAcceptsInput,
                                                editor->message, &requestedPlayerSave);
    bool requestedEnemySave = EditorUI_DrawEnemyInspector(&editor->enemyGroup,
                                                           editor->enemySelected, inspectorAcceptsInput,
                                                      editor->message);
    bool requestedGlobalSave = EditorUI_DrawGlobalInspector(&editor->gridOverlayOpacity,
                                                             &editor->selectedTileType,
                                                             &editor->isGridEditing,
                                                             editor->globalSelected,
                                                             inspectorAcceptsInput, editor->message);
    bool requestedPlayToggle = EditorUI_DrawPlayButton(editor->isPlaying);
    EditorUI_DrawGlobalButton(editor->globalSelected);
    EndDrawing();

    if (requestedFile) {
        Editor_ApplySelectedPng(editor);
    }
    if (requestedPlayerSave || requestedEnemySave || requestedGlobalSave) {
        if (Editor_SaveSettings(editor)) {
            editor->playerSelected = false;
            editor->enemySelected = false;
            editor->globalSelected = false;
            editor->isGridEditing = false;
        }
        EditorUI_ResetInput();
    }
    editor->inspectorOpenedThisFrame = false;

    return requestedPlayToggle;
}

static void Editor_Close(EditorState *editor)
{
    Player_UnloadAppearance(&editor->player);
    Player_UnloadDefaultAppearance(&editor->player);
    EnemyGroup_UnloadAppearance(&editor->enemyGroup);
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "1_44MB - ゲームエディター");
    SetTargetFPS(TARGET_FPS);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));

    EditorState editor = Editor_Create();

    while (!WindowShouldClose()) {
        Editor_Update(&editor);
        if (Editor_Draw(&editor)) {
            editor.isPlaying = !editor.isPlaying;
        }
    }

    Editor_Close(&editor);
    GameFont_Unload();
    CloseWindow();
    return 0;
}
