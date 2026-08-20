// 依存: editor_ui.h、../Shered/game_font.h、../Shered/player.h、../Shered/enemy_group.h（editor_ui.h 経由）
#include "editor_ui.h"

#include "game_font.h"
#include "stage.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NUMERIC_INPUT_NONE = 0,
    NUMERIC_INPUT_SPACING,
    NUMERIC_INPUT_SPEED,
    NUMERIC_INPUT_PLAYER_SCALE,
    NUMERIC_INPUT_ENEMY_SCALE,
    NUMERIC_INPUT_GRID_OPACITY,
};

static int activeNumericInput = NUMERIC_INPUT_NONE;
static char numericInputBuffer[24];
static int numericCaretIndex;
static bool replaceNumericValueOnInput;
static bool uiAcceptsInput;

static int GetNumericCaretIndex(const char *text, float localX)
{
    int length = (int)strlen(text);
    int closestIndex = 0;
    float closestDistance = 1000000.0f;

    // 文字列の各境界までの幅を測り、クリック位置に最も近い編集位置を選ぶ。
    for (int index = 0; index <= length; index++) {
        char prefix[24];
        memcpy(prefix, text, (size_t)index);
        prefix[index] = '\0';

        float distance = fabsf(localX - (float)MeasureText(prefix, 16));
        if (distance < closestDistance) {
            closestDistance = distance;
            closestIndex = index;
        }
    }

    return closestIndex;
}

static void InsertNumericCharacter(int character)
{
    size_t length = strlen(numericInputBuffer);
    if (replaceNumericValueOnInput) {
        numericInputBuffer[0] = '\0';
        numericCaretIndex = 0;
        length = 0;
        replaceNumericValueOnInput = false;
    }

    if (length + 1 >= sizeof(numericInputBuffer)) {
        return;
    }

    memmove(&numericInputBuffer[numericCaretIndex + 1],
            &numericInputBuffer[numericCaretIndex],
            length - (size_t)numericCaretIndex + 1);
    numericInputBuffer[numericCaretIndex] = (char)character;
    numericCaretIndex++;
}

static float DrawNumericInput(Rectangle bounds, float value, float minimum, float maximum,
                              int inputId, int decimalPlaces)
{
    if (uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), bounds) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (activeNumericInput == inputId) {
            numericCaretIndex = GetNumericCaretIndex(numericInputBuffer,
                                                     GetMousePosition().x - bounds.x - 4.0f);
            replaceNumericValueOnInput = false;
        } else {
            activeNumericInput = inputId;
            snprintf(numericInputBuffer, sizeof(numericInputBuffer),
                     decimalPlaces == 0 ? "%.0f" : "%.2f", value);
            numericCaretIndex = (int)strlen(numericInputBuffer);
            replaceNumericValueOnInput = true;
        }
    }

    if (activeNumericInput == inputId) {
        int character = GetCharPressed();
        while (character > 0) {
            if ((character >= '0' && character <= '9') ||
                (character == '.' && (replaceNumericValueOnInput ||
                                      strchr(numericInputBuffer, '.') == NULL))) {
                InsertNumericCharacter(character);
            }
            character = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t length = strlen(numericInputBuffer);
            if (numericCaretIndex > 0 && length > 0) {
                memmove(&numericInputBuffer[numericCaretIndex - 1],
                        &numericInputBuffer[numericCaretIndex],
                        length - (size_t)numericCaretIndex + 1);
                numericCaretIndex--;
                replaceNumericValueOnInput = false;
            }
        }
        if (IsKeyPressed(KEY_ENTER)) {
            activeNumericInput = NUMERIC_INPUT_NONE;
            replaceNumericValueOnInput = false;
        }

        char *endPointer = NULL;
        float parsedValue = strtof(numericInputBuffer, &endPointer);
        if (endPointer != numericInputBuffer) {
            value = parsedValue < minimum ? minimum : parsedValue > maximum ? maximum : parsedValue;
        }
    }

    DrawRectangleRec(bounds, activeNumericInput == inputId ? Fade(SKYBLUE, 0.35f) : RAYWHITE);
    DrawRectangleLinesEx(bounds, 1.0f, activeNumericInput == inputId ? DARKBLUE : DARKGRAY);
    const char *displayText = activeNumericInput == inputId ? numericInputBuffer :
                              TextFormat(decimalPlaces == 0 ? "%.0f" : "%.2f", value);
    DrawText(displayText, (int)bounds.x + 4, (int)bounds.y + 3, 16, DARKGRAY);
    if (activeNumericInput == inputId) {
        char prefix[24];
        memcpy(prefix, numericInputBuffer, (size_t)numericCaretIndex);
        prefix[numericCaretIndex] = '\0';
        int caretX = (int)bounds.x + 4 + MeasureText(prefix, 16);
        DrawLine(caretX, (int)bounds.y + 4, caretX, (int)bounds.y + (int)bounds.height - 4, DARKBLUE);
    }
    return value;
}

static float DrawSlider(Rectangle bounds, float value, float minimum, float maximum)
{
    Vector2 mousePosition = GetMousePosition();
    if (uiAcceptsInput && CheckCollisionPointRec(mousePosition, bounds) &&
        IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float ratio = (mousePosition.x - bounds.x) / bounds.width;
        if (ratio < 0.0f) {
            ratio = 0.0f;
        } else if (ratio > 1.0f) {
            ratio = 1.0f;
        }
        value = minimum + (maximum - minimum) * ratio;
    }

    float ratio = (value - minimum) / (maximum - minimum);
    DrawRectangleRec(bounds, LIGHTGRAY);
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)(bounds.width * ratio),
                  (int)bounds.height, DARKBLUE);
    DrawRectangleLinesEx(bounds, 1.0f, DARKGRAY);
    return value;
}

bool EditorUI_DrawInspector(Player *player, const char *appearancePath,
                            bool selected, bool acceptsInput, const char *message,
                            bool *requestedSave)
{
    const Rectangle panel = { 620.0f, 24.0f, 316.0f, 360.0f };
    const Rectangle chooseButton = { 644.0f, 174.0f, 268.0f, 44.0f };
    const Rectangle scaleSlider = { 644.0f, 278.0f, 268.0f, 18.0f };
    const Rectangle scaleInput = { 832.0f, 246.0f, 80.0f, 25.0f };
    const Rectangle saveButton = { 644.0f, 310.0f, 268.0f, 36.0f };

    *requestedSave = false;
    if (!selected) {
        return false;
    }
    uiAcceptsInput = acceptsInput;

    DrawRectangleRec(panel, Fade(RAYWHITE, 0.95f));
    DrawRectangleLinesEx(panel, 2.0f, DARKGRAY);
    GameFont_Draw("インスペクター", 644, 46, 26, DARKGRAY);
    GameFont_Draw("対象: 主人公", 644, 88, 20, DARKGRAY);
    GameFont_Draw(player->hasAppearance ? "見た目: PNG" : "見た目: 仮キャラクター", 644, 118, 18, GRAY);
    DrawText(GetFileName(appearancePath), 644, 144, 16, GRAY);

    DrawRectangleRec(chooseButton, DARKBLUE);
    GameFont_Draw("PNG を選択", 710, 184, 22, RAYWHITE);
    GameFont_Draw("Scale", 644, 244, 18, DARKGRAY);
    float scale = DrawSlider(scaleSlider, Player_GetScale(player), 0.5f, 3.0f);
    scale = DrawNumericInput(scaleInput, scale, 0.5f, 3.0f, NUMERIC_INPUT_PLAYER_SCALE, 2);
    Player_SetScale(player, scale);

    bool isSavePressed = uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), saveButton) &&
                         IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    DrawRectangleRec(saveButton, isSavePressed ? BLUE : DARKBLUE);
    GameFont_Draw("設定を保存", 724, 317, 20, RAYWHITE);
    GameFont_Draw(message, 644, 352, 14, isSavePressed ? DARKBLUE : MAROON);
    *requestedSave = isSavePressed;

    return uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), chooseButton) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool EditorUI_DrawEnemyInspector(EnemyGroup *group, bool selected, bool acceptsInput,
                                 const char *message)
{
    const Rectangle panel = { 620.0f, 24.0f, 316.0f, 490.0f };
    const Rectangle spacingSlider = { 644.0f, 128.0f, 268.0f, 18.0f };
    const Rectangle spacingInput = { 832.0f, 96.0f, 80.0f, 25.0f };
    const Rectangle speedSlider = { 644.0f, 194.0f, 268.0f, 18.0f };
    const Rectangle speedInput = { 832.0f, 162.0f, 80.0f, 25.0f };
    const Rectangle scaleSlider = { 644.0f, 260.0f, 268.0f, 18.0f };
    const Rectangle scaleInput = { 832.0f, 228.0f, 80.0f, 25.0f };
    const Rectangle greenButton = { 644.0f, 320.0f, 76.0f, 36.0f };
    const Rectangle blueButton = { 740.0f, 320.0f, 76.0f, 36.0f };
    const Rectangle orangeButton = { 836.0f, 320.0f, 76.0f, 36.0f };
    const Rectangle saveButton = { 644.0f, 418.0f, 268.0f, 40.0f };

    if (!selected) {
        return false;
    }
    uiAcceptsInput = acceptsInput;

    float spacing = EnemyGroup_GetFollowSpacing(group);
    float speed = EnemyGroup_GetFollowInterpolationSpeed(group);
    Color color = EnemyGroup_GetSubordinateColor(group);

    DrawRectangleRec(panel, Fade(RAYWHITE, 0.95f));
    DrawRectangleLinesEx(panel, 2.0f, DARKGRAY);
    GameFont_Draw("敵インスペクター", 644, 46, 26, DARKGRAY);
    GameFont_Draw("追従間隔", 644, 94, 18, DARKGRAY);
    spacing = DrawSlider(spacingSlider, spacing, 10.0f, 100.0f);
    spacing = DrawNumericInput(spacingInput, spacing, 10.0f, 100.0f, NUMERIC_INPUT_SPACING, 0);
    GameFont_Draw("補間速度", 644, 160, 18, DARKGRAY);
    speed = DrawSlider(speedSlider, speed, 1.0f, 12.0f);
    speed = DrawNumericInput(speedInput, speed, 1.0f, 12.0f, NUMERIC_INPUT_SPEED, 2);
    GameFont_Draw("Scale", 644, 226, 18, DARKGRAY);
    float scale = DrawSlider(scaleSlider, EnemyGroup_GetScale(group), 0.5f, 3.0f);
    scale = DrawNumericInput(scaleInput, scale, 0.5f, 3.0f, NUMERIC_INPUT_ENEMY_SCALE, 2);
    EnemyGroup_SetScale(group, scale);
    GameFont_Draw("従属時の色", 644, 292, 18, DARKGRAY);

    DrawRectangleRec(greenButton, LIME);
    DrawRectangleRec(blueButton, SKYBLUE);
    DrawRectangleRec(orangeButton, ORANGE);
    if (uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), greenButton) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        color = LIME;
    } else if (uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), blueButton) &&
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        color = SKYBLUE;
    } else if (uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), orangeButton) &&
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        color = ORANGE;
    }

    DrawRectangle(644, 370, 268, 22, color);
    GameFont_Draw("選択中の従属色", 650, 373, 16, DARKGRAY);
    bool isSavePressed = uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), saveButton) &&
                         IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    DrawRectangleRec(saveButton, isSavePressed ? BLUE : DARKBLUE);
    GameFont_Draw("設定を保存", 724, 427, 20, RAYWHITE);
    GameFont_Draw(message, 644, 464, 14, isSavePressed ? DARKBLUE : MAROON);

    // 変更中の値をまとめて敵グループへ渡し、全敵の追従設定を同じ内容で更新する。
    EnemyGroup_SetFollowSettings(group, spacing, speed, color);
    return isSavePressed;
}

bool EditorUI_DrawGlobalInspector(float *gridOverlayOpacity, int *selectedTileType,
                                  bool *isGridEditing, bool selected, bool acceptsInput,
                                  const char *message)
{
    const Rectangle panel = { 620.0f, 24.0f, 316.0f, 404.0f };
    const Rectangle opacitySlider = { 644.0f, 128.0f, 268.0f, 18.0f };
    const Rectangle opacityInput = { 832.0f, 96.0f, 80.0f, 25.0f };
    const Rectangle editButton = { 644.0f, 174.0f, 268.0f, 36.0f };
    const Rectangle emptyButton = { 644.0f, 250.0f, 76.0f, 38.0f };
    const Rectangle blockButton = { 740.0f, 250.0f, 76.0f, 38.0f };
    const Rectangle enemyButton = { 836.0f, 250.0f, 76.0f, 38.0f };
    const Rectangle saveButton = { 644.0f, 324.0f, 268.0f, 40.0f };

    if (!selected) {
        return false;
    }
    uiAcceptsInput = acceptsInput;

    DrawRectangleRec(panel, Fade(RAYWHITE, 0.95f));
    DrawRectangleLinesEx(panel, 2.0f, DARKGRAY);
    DrawText("Global Inspector", 644, 46, 24, DARKGRAY);
    DrawText("Grid opacity", 644, 94, 18, DARKGRAY);
    *gridOverlayOpacity = DrawSlider(opacitySlider, *gridOverlayOpacity, 0.05f, 0.95f);
    *gridOverlayOpacity = DrawNumericInput(opacityInput, *gridOverlayOpacity,
                                           0.05f, 0.95f, NUMERIC_INPUT_GRID_OPACITY, 2);

    if (uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), editButton) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        *isGridEditing = !*isGridEditing;
    }
    DrawRectangleRec(editButton, *isGridEditing ? MAROON : DARKGREEN);
    DrawText(*isGridEditing ? "Grid edit: ON" : "Grid edit: OFF", 696, 183, 18, RAYWHITE);
    if (*isGridEditing) {
        DrawText("Tile to place", 644, 222, 18, DARKGRAY);
        DrawRectangleRec(emptyButton, *selectedTileType == STAGE_TILE_EMPTY ? DARKBLUE : GRAY);
        DrawRectangleRec(blockButton, *selectedTileType == STAGE_TILE_BLOCK ? DARKBLUE : GRAY);
        DrawRectangleRec(enemyButton, *selectedTileType == STAGE_TILE_ENEMY ? DARKBLUE : GRAY);
        DrawText("0", 676, 258, 20, RAYWHITE);
        DrawText("1", 772, 258, 20, RAYWHITE);
        DrawText("2", 868, 258, 20, RAYWHITE);
    }
    if (*isGridEditing && uiAcceptsInput && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mousePosition = GetMousePosition();
        if (CheckCollisionPointRec(mousePosition, emptyButton)) {
            *selectedTileType = STAGE_TILE_EMPTY;
        } else if (CheckCollisionPointRec(mousePosition, blockButton)) {
            *selectedTileType = STAGE_TILE_BLOCK;
        } else if (CheckCollisionPointRec(mousePosition, enemyButton)) {
            *selectedTileType = STAGE_TILE_ENEMY;
        }
    }

    bool isSavePressed = uiAcceptsInput && CheckCollisionPointRec(GetMousePosition(), saveButton) &&
                         IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    DrawRectangleRec(saveButton, isSavePressed ? BLUE : DARKBLUE);
    GameFont_Draw("設定を保存", 724, 333, 20, RAYWHITE);
    GameFont_Draw(message, 644, 378, 14, isSavePressed ? DARKBLUE : MAROON);
    return isSavePressed;
}

bool EditorUI_DrawPlayButton(bool isPlaying)
{
    const Rectangle button = { 20.0f, 82.0f, 132.0f, 38.0f };
    DrawRectangleRec(button, isPlaying ? MAROON : DARKGREEN);
    GameFont_Draw(isPlaying ? "停止" : "Play", 60, 90, 22, RAYWHITE);
    return CheckCollisionPointRec(GetMousePosition(), button) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void EditorUI_DrawGlobalButton(bool selected)
{
    const Rectangle button = { 170.0f, 82.0f, 140.0f, 38.0f };
    DrawRectangleRec(button, selected ? BLUE : DARKBLUE);
    DrawText("Global", 204, 92, 20, RAYWHITE);
}

void EditorUI_DrawHint(bool playerSelected, bool enemySelected, bool isPlaying)
{
    const char *hint = playerSelected ? "主人公を選択中" :
                       enemySelected ? "敵を選択中" : "主人公または敵をクリックして選択";
    GameFont_Draw("2D 横スクロールゲーム エディター", 20, 20, 24, DARKGRAY);
    GameFont_Draw(isPlaying ? "Play中: A/D と Space で動作確認" : hint, 20, 52, 18, DARKGRAY);
}

void EditorUI_ResetInput(void)
{
    activeNumericInput = NUMERIC_INPUT_NONE;
    replaceNumericValueOnInput = false;
}
// 役割: 横スクロール用エディターの共通 UI 描画と入力補助を提供する。
