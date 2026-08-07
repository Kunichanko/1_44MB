// 依存: editor_ui.h
#include "editor_ui.h"

#include "raylib.h"

bool EditorUI_DrawInspector(const Player *player, const char *appearancePath,
                            bool selected, const char *message)
{
    const Rectangle panel = { 620.0f, 24.0f, 316.0f, 260.0f };
    const Rectangle chooseButton = { 644.0f, 174.0f, 268.0f, 44.0f };

    if (!selected) {
        return false;
    }

    DrawRectangleRec(panel, Fade(RAYWHITE, 0.95f));
    DrawRectangleLinesEx(panel, 2.0f, DARKGRAY);
    DrawText("インスペクター", 644, 46, 26, DARKGRAY);
    DrawText("対象: 主人公", 644, 88, 20, DARKGRAY);
    DrawText(player->hasAppearance ? "見た目: PNG" : "見た目: 仮キャラクター", 644, 118, 18, GRAY);
    DrawText(GetFileName(appearancePath), 644, 144, 16, GRAY);

    DrawRectangleRec(chooseButton, DARKBLUE);
    DrawText("PNG を選択", 710, 184, 22, RAYWHITE);
    DrawText(message, 644, 232, 16, MAROON);

    return CheckCollisionPointRec(GetMousePosition(), chooseButton) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void EditorUI_DrawHint(bool selected)
{
    const char *hint = selected ? "主人公を選択中" : "主人公をクリックして選択";
    DrawText("2D 横スクロールゲーム エディター", 20, 20, 24, DARKGRAY);
    DrawText(hint, 20, 52, 18, DARKGRAY);
}
