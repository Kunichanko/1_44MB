// 依存: editor_ui.h、file_dialog.h、../Shered/player.h、../Shered/stage.h
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

typedef struct EditorState {
    Stage stage;
    Player player;
    Camera2D camera;
    bool playerSelected;
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

static bool IsPlayerClicked(const Player *player, const Stage *stage, const Camera2D *camera)
{
    Vector2 worldMousePosition = GetScreenToWorld2D(GetMousePosition(), *camera);
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(worldMousePosition, Player_GetBounds(player, stage->groundY));
}

static EditorState Editor_Create(void)
{
    EditorState editor = {0};
    editor.stage = Stage_Create();
    editor.player = Player_Create((Vector2){ 280.0f, editor.stage.groundY });
    editor.camera = CreateCamera();
    snprintf(editor.appearancePath, sizeof(editor.appearancePath), "未指定");
    snprintf(editor.message, sizeof(editor.message), "主人公をクリックして選択");
    return editor;
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
    GameCamera_Update(&editor->camera, &editor->player, &editor->stage);

    if (IsPlayerClicked(&editor->player, &editor->stage, &editor->camera)) {
        editor->playerSelected = true;
        snprintf(editor->message, sizeof(editor->message), "主人公を選択しました");
    }
}

static void Editor_Draw(EditorState *editor)
{
    BeginDrawing();
    ClearBackground(SKYBLUE);
    BeginMode2D(editor->camera);
    Stage_Draw(&editor->stage);
    Player_Draw(&editor->player, editor->stage.groundY);
    if (editor->playerSelected) {
        DrawRectangleLinesEx(Player_GetBounds(&editor->player, editor->stage.groundY), 3.0f, BLUE);
    }
    EndMode2D();

    EditorUI_DrawHint(editor->playerSelected);
    bool requestedFile = EditorUI_DrawInspector(&editor->player, editor->appearancePath,
                                                editor->playerSelected, editor->message);
    EndDrawing();

    if (requestedFile) {
        Editor_ApplySelectedPng(editor);
    }
}

static void Editor_Close(EditorState *editor)
{
    Player_UnloadAppearance(&editor->player);
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "1_44MB - ゲームエディター");
    SetTargetFPS(TARGET_FPS);

    EditorState editor = Editor_Create();

    while (!WindowShouldClose()) {
        Editor_Update(&editor);
        Editor_Draw(&editor);
    }

    Editor_Close(&editor);
    CloseWindow();
    return 0;
}
