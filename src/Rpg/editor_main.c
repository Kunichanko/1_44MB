// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_dialogue.h, rpg_layout.h, rpg_stage.h, game_font.h
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <imm.h>
#endif

#include "raylib.h"

#include <string.h>

#include "game_font.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_layout.h"
#include "rpg_stage.h"

enum { RPG_EDITOR_WIDTH = 960, RPG_EDITOR_HEIGHT = 540, NPC_VISIBLE_LINES = 9 };

static const Rectangle playerInspectorBounds = { 700.0f, 80.0f, 220.0f, 130.0f };
static const Rectangle npcInspectorBounds = { 700.0f, 80.0f, 220.0f, 150.0f };
static const Rectangle dialogueEditorBounds = { 140.0f, 56.0f, 680.0f, 424.0f };

static Rectangle GetInspectorCloseButton(int selected)
{
    (void)selected;
    return (Rectangle){ 894.0f, 88.0f, 18.0f, 18.0f };
}

static Rectangle GetDialogueEditorCloseButton(void)
{
    return (Rectangle){ 792.0f, 64.0f, 18.0f, 18.0f };
}

static int GetVisibleDialogueLines(int blockHeight)
{
    int visibleLines = 252 / (blockHeight + 4);
    return visibleLines < 1 ? 1 : visibleLines;
}

static void UpdateImeCandidateWindow(int activeLine, int scroll)
{
#ifdef _WIN32
    // 日本語IMEの変換候補を左上ではなく、選択中の会話行の近くに表示する。
    HIMC inputContext = ImmGetContext((HWND)GetWindowHandle());
    if (inputContext == NULL) return;
    CANDIDATEFORM candidateForm = { 0 };
    candidateForm.dwStyle = CFS_CANDIDATEPOS;
    candidateForm.ptCurrentPos.x = 200;
    candidateForm.ptCurrentPos.y = 152 + (activeLine - scroll) * 28;
    ImmSetCandidateWindow(inputContext, &candidateForm);
    COMPOSITIONFORM compositionForm = { 0 };
    compositionForm.dwStyle = CFS_POINT;
    compositionForm.ptCurrentPos = candidateForm.ptCurrentPos;
    ImmSetCompositionWindow(inputContext, &compositionForm);
    ImmReleaseContext((HWND)GetWindowHandle(), inputContext);
#else
    (void)activeLine;
    (void)scroll;
#endif
}

static bool IsCharacterClicked(const RpgCharacter *character, Vector2 mousePosition)
{
    Rectangle characterBounds = { character->position.x - 22.0f, character->position.y - 84.0f,
                                  44.0f, 90.0f };
    return CheckCollisionPointRec(mousePosition, characterBounds);
}

static bool IsCharacterInMap(const RpgCharacter *character, int mapIndex)
{
    float mapStartX = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    float mapEndX = mapStartX + RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    return character->position.x >= mapStartX && character->position.x < mapEndX;
}

static RpgCharacter GetLocalCharacter(const RpgCharacter *character, int mapIndex)
{
    RpgCharacter localCharacter = *character;
    localCharacter.position.x -= mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    return localCharacter;
}

static bool SaveEditorData(RpgLayout *layout, const RpgCharacter *player,
                           const RpgCharacter *npc, const RpgStage *stage,
                           const RpgDialogue *dialogue)
{
    layout->playerPosition = player->position;
    layout->npcPosition = npc->position;
    layout->playerMoveSpeed = player->moveSpeed;
    bool layoutSaved = RpgLayout_Save(TextFormat("%s../assets/Settings/rpg_layout.cfg",
                                                  GetApplicationDirectory()), layout);
    bool stageSaved = RpgStage_Save(TextFormat("%s../assets/Settings/rpg_stage.cfg",
                                                GetApplicationDirectory()), stage);
    bool dialogueSaved = RpgDialogue_Save(TextFormat("%s../assets/Settings/rpg_dialogue.txt",
                                                      GetApplicationDirectory()), dialogue);
    return layoutSaved && stageSaved && dialogueSaved;
}

static void AppendUtf8(char *text, size_t capacity, int codepoint)
{
    char encoded[5] = { 0 };
    int length = 0;
    size_t used = strlen(text);
    if (codepoint <= 0x7f) { encoded[0] = (char)codepoint; length = 1; }
    else if (codepoint <= 0x7ff) {
        encoded[0] = (char)(0xc0 | (codepoint >> 6));
        encoded[1] = (char)(0x80 | (codepoint & 0x3f)); length = 2;
    } else if (codepoint <= 0xffff) {
        encoded[0] = (char)(0xe0 | (codepoint >> 12));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[2] = (char)(0x80 | (codepoint & 0x3f)); length = 3;
    } else {
        encoded[0] = (char)(0xf0 | (codepoint >> 18));
        encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
        encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[3] = (char)(0x80 | (codepoint & 0x3f)); length = 4;
    }
    if (used + (size_t)length < capacity) memcpy(text + used, encoded, (size_t)length + 1);
}

static void RemoveLastUtf8Character(char *text)
{
    size_t length = strlen(text);
    if (length == 0) return;
    length--;
    while (length > 0 && ((unsigned char)text[length] & 0xc0) == 0x80) length--;
    text[length] = '\0';
}

static int GetPreviousUtf8Index(const char *text, int index)
{
    if (index <= 0) return 0;
    index--;
    while (index > 0 && ((unsigned char)text[index] & 0xc0) == 0x80) index--;
    return index;
}

static int GetNextUtf8Index(const char *text, int index)
{
    int byteCount = 0;
    if (text[index] == '\0') return index;
    GetCodepointNext(text + index, &byteCount);
    return index + byteCount;
}

static void InsertUtf8AtCursor(char *text, size_t capacity, int *cursorIndex, int codepoint)
{
    char suffix[RPG_DIALOGUE_LINE_LENGTH];
    size_t originalLength = strlen(text);
    strcpy(suffix, text + *cursorIndex);
    text[*cursorIndex] = '\0';
    AppendUtf8(text, capacity, codepoint);
    if (strlen(text) + strlen(suffix) < capacity) {
        *cursorIndex = (int)strlen(text);
        strcat(text, suffix);
    } else {
        memcpy(text + *cursorIndex, suffix, originalLength - (size_t)*cursorIndex + 1);
    }
}

static void RemoveBeforeCursor(char *text, int *cursorIndex)
{
    if (*cursorIndex <= 0) return;
    if (text[*cursorIndex] == '\0') {
        RemoveLastUtf8Character(text);
        *cursorIndex = (int)strlen(text);
        return;
    }
    int previousIndex = GetPreviousUtf8Index(text, *cursorIndex);
    memmove(text + previousIndex, text + *cursorIndex, strlen(text + *cursorIndex) + 1);
    *cursorIndex = previousIndex;
}

static int GetCursorIndexAtX(const char *text, float x, float fontSize)
{
    int index = 0;
    char prefix[RPG_DIALOGUE_LINE_LENGTH];
    while (text[index] != '\0') {
        int nextIndex = GetNextUtf8Index(text, index);
        memcpy(prefix, text, (size_t)nextIndex);
        prefix[nextIndex] = '\0';
        float rightX = GameFont_MeasureText(prefix, fontSize).x;
        if (x < rightX) {
            memcpy(prefix, text, (size_t)index);
            prefix[index] = '\0';
            float leftX = GameFont_MeasureText(prefix, fontSize).x;
            return x - leftX < rightX - x ? index : nextIndex;
        }
        index = nextIndex;
    }
    return index;
}

static int GetWrappedLineCount(const char *text, float fontSize)
{
    int lineCount = 1;
    int lineStart = 0;
    int index = 0;
    char part[RPG_DIALOGUE_LINE_LENGTH];
    while (text[index] != '\0') {
        int nextIndex = GetNextUtf8Index(text, index);
        memcpy(part, text + lineStart, (size_t)(nextIndex - lineStart));
        part[nextIndex - lineStart] = '\0';
        if (index > lineStart && GameFont_MeasureText(part, fontSize).x > 590.0f) {
            lineCount++;
            lineStart = index;
        } else index = nextIndex;
    }
    return lineCount;
}

static void DrawWrappedDialogueText(const char *text, int x, int y, float fontSize, Color color)
{
    int lineStart = 0;
    int index = 0;
    int drawY = y;
    char part[RPG_DIALOGUE_LINE_LENGTH];
    while (text[index] != '\0') {
        int nextIndex = GetNextUtf8Index(text, index);
        memcpy(part, text + lineStart, (size_t)(nextIndex - lineStart));
        part[nextIndex - lineStart] = '\0';
        if (index > lineStart && GameFont_MeasureText(part, fontSize).x > 590.0f) {
            memcpy(part, text + lineStart, (size_t)(index - lineStart));
            part[index - lineStart] = '\0';
            GameFont_Draw(part, (float)x, (float)drawY, fontSize, color);
            drawY += (int)fontSize + 2;
            lineStart = index;
        } else index = nextIndex;
    }
    GameFont_Draw(text + lineStart, (float)x, (float)drawY, fontSize, color);
}

static bool DeleteSelectedText(char *text, int *cursorIndex, int *selectionAnchor,
                               int *selectionEnd)
{
    int start = *selectionAnchor < *selectionEnd ? *selectionAnchor : *selectionEnd;
    int end = *selectionAnchor < *selectionEnd ? *selectionEnd : *selectionAnchor;
    if (start == end) return false;
    memmove(text + start, text + end, strlen(text + end) + 1);
    *cursorIndex = start;
    *selectionAnchor = start;
    *selectionEnd = start;
    return true;
}

static void UpdateDialogueText(RpgDialogue *dialogue, int activeLine, int *cursorIndex,
                               int *selectionAnchor, int *selectionEnd)
{
    if (activeLine < 0 || activeLine >= dialogue->lineCount) return;
    char *text = dialogue->lines[activeLine];
    int textLength = (int)strlen(text);
    if (*cursorIndex > textLength) *cursorIndex = textLength;
    if (IsKeyPressed(KEY_LEFT)) *cursorIndex = GetPreviousUtf8Index(text, *cursorIndex);
    if (IsKeyPressed(KEY_RIGHT)) *cursorIndex = GetNextUtf8Index(text, *cursorIndex);
    if (IsKeyPressed(KEY_HOME)) *cursorIndex = 0;
    if (IsKeyPressed(KEY_END)) *cursorIndex = textLength;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_END)) {
        *selectionAnchor = *cursorIndex;
        *selectionEnd = *cursorIndex;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !DeleteSelectedText(text, cursorIndex, selectionAnchor, selectionEnd)) {
        RemoveBeforeCursor(text, cursorIndex);
    }
    if (IsKeyPressed(KEY_DELETE) && !DeleteSelectedText(text, cursorIndex, selectionAnchor, selectionEnd) && text[*cursorIndex] != '\0') {
        int nextIndex = GetNextUtf8Index(text, *cursorIndex);
        memmove(text + *cursorIndex, text + nextIndex, strlen(text + nextIndex) + 1);
    }
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        if (codepoint >= 32) {
            DeleteSelectedText(text, cursorIndex, selectionAnchor, selectionEnd);
            InsertUtf8AtCursor(text, RPG_DIALOGUE_LINE_LENGTH, cursorIndex, codepoint);
            // 入力済み文字をフォントへ加え、任意の日本語を ? で表示しないようにする。
            GameFont_AddText(dialogue->lines[activeLine]);
        }
        codepoint = GetCharPressed();
    }
}

static void DrawNpcInspector(const RpgDialogue *dialogue, int scroll, int activeLine, int cursorIndex,
                             int selectionAnchor, int selectionEnd, int draggedLine,
                             int blockHeight, int fontSize)
{
    DrawRectangleRec(dialogueEditorBounds, Fade(RAYWHITE, 0.98f));
    DrawRectangleLinesEx(dialogueEditorBounds, 2.0f, PURPLE);
    DrawText("NPC Inspector - Dialogue", 156, 68, 22, PURPLE);
    DrawRectangleRec(GetDialogueEditorCloseButton(), Fade(MAROON, 0.88f));
    DrawText("x", 797, 65, 16, RAYWHITE);
    DrawText(TextFormat("Font: %d", fontSize), 590, 68, 16, DARKGRAY);
    DrawRectangleLines(680, 64, 42, 22, MAROON);
    DrawRectangleLines(730, 64, 42, 22, DARKGREEN);
    DrawText("-", 695, 67, 18, MAROON);
    DrawText("+", 745, 67, 18, DARKGREEN);
    DrawText("Drag: move  Right click: delete  Wheel: scroll", 156, 98, 17, DARKGRAY);

    int visibleLineCount = GetVisibleDialogueLines(blockHeight);
    for (int visibleIndex = 0; visibleIndex < visibleLineCount; visibleIndex++) {
        int lineIndex = scroll + visibleIndex;
        if (lineIndex >= dialogue->lineCount) break;
        int lineY = 128 + visibleIndex * (blockHeight + 4);
        Color background = lineIndex == draggedLine ? Fade(ORANGE, 0.35f) :
                           lineIndex == activeLine ? Fade(PURPLE, 0.20f) : Fade(LIGHTGRAY, 0.45f);
        DrawRectangle(154, lineY, 652, blockHeight, background);
        DrawRectangleLines(154, lineY, 652, blockHeight, lineIndex == draggedLine ? ORANGE :
                           lineIndex == activeLine ? PURPLE : GRAY);
        DrawText(TextFormat("%02d", lineIndex + 1), 160, lineY + (blockHeight - 14) / 2, 14, DARKGRAY);
        if (lineIndex == activeLine && selectionAnchor != selectionEnd) {
            int start = selectionAnchor < selectionEnd ? selectionAnchor : selectionEnd;
            int end = selectionAnchor < selectionEnd ? selectionEnd : selectionAnchor;
            char prefix[RPG_DIALOGUE_LINE_LENGTH];
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)start);
            prefix[start] = '\0';
            int startX = 200 + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)end);
            prefix[end] = '\0';
            int endX = 200 + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            DrawRectangle(startX, lineY + 3, endX - startX, blockHeight - 6, Fade(SKYBLUE, 0.65f));
        }
        DrawWrappedDialogueText(dialogue->lines[lineIndex][0] == '\0' ? " " : dialogue->lines[lineIndex],
                                200, lineY + 4, (float)fontSize, DARKBLUE);
        if (lineIndex == activeLine) {
            char prefix[RPG_DIALOGUE_LINE_LENGTH];
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)cursorIndex);
            prefix[cursorIndex] = '\0';
            int cursorX = 200 + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            DrawLine(cursorX, lineY + 3, cursorX, lineY + blockHeight - 3, PURPLE);
        }
    }
    DrawText(TextFormat("%d lines", dialogue->lineCount), 156, 386, 16, DARKGRAY);
    DrawRectangle(156, 414, 210, 32, PURPLE);
    DrawText("Add dialogue line", 182, 422, 18, RAYWHITE);
    DrawRectangle(382, 414, 244, 32, DARKBLUE);
    DrawText("Save NPC dialogue", 412, 422, 18, RAYWHITE);
}

static void DrawNpcSummaryInspector(const RpgDialogue *dialogue)
{
    DrawRectangleRec(npcInspectorBounds, Fade(RAYWHITE, 0.94f));
    DrawRectangleLinesEx(npcInspectorBounds, 2.0f, PURPLE);
    DrawText("NPC Inspector", 716, 92, 18, PURPLE);
    DrawRectangleRec(GetInspectorCloseButton(2), Fade(MAROON, 0.88f));
    DrawText("x", 899, 89, 16, RAYWHITE);
    DrawText(TextFormat("Dialogue: %d lines", dialogue->lineCount), 716, 122, 16, DARKGRAY);
    DrawRectangle(716, 164, 188, 32, PURPLE);
    DrawText("Edit dialogue", 750, 172, 18, RAYWHITE);
}

static void DrawEditor(const RpgCharacter *player, const RpgCharacter *npc, const RpgStage *stage,
                       const RpgDialogue *dialogue, int selected, int mapIndex, bool blockMode,
                       int dialogueScroll, int activeDialogueLine, int dialogueCursorIndex,
                       int selectionAnchor, int selectionEnd, int draggedDialogueLine,
                       bool isDialogueEditorOpen, int dialogueBlockHeight,
                       int dialogueFontSize, const char *message)
{
    BeginDrawing();
    ClearBackground((Color){ 135, 206, 235, 255 });
    DrawCircle(780, 95, 42, Fade(YELLOW, 0.9f));
    DrawEllipse(180, 105, 80, 20, Fade(RAYWHITE, 0.85f));
    DrawRectangle(0, 400, RPG_EDITOR_WIDTH, 140, (Color){ 103, 161, 70, 255 });
    RpgStage_DrawMap(stage, mapIndex, blockMode);
    DrawRectangle(0, 400, RPG_EDITOR_WIDTH, 14, DARKGREEN);
    if (IsCharacterInMap(npc, mapIndex)) {
        RpgCharacter localNpc = GetLocalCharacter(npc, mapIndex);
        RpgCharacter_Draw(&localNpc, "NPC");
        if (selected == 2) DrawCircleLines((int)localNpc.position.x, (int)localNpc.position.y, 32, PURPLE);
    }
    if (IsCharacterInMap(player, mapIndex)) {
        RpgCharacter localPlayer = GetLocalCharacter(player, mapIndex);
        RpgCharacter_Draw(&localPlayer, "Hero");
        if (selected == 1) DrawCircleLines((int)localPlayer.position.x, (int)localPlayer.position.y, 32, BLUE);
    }
    DrawText("RPG Editor - Click Hero or NPC, then click the map to place", 24, 20, 21, DARKGRAY);
    DrawText(blockMode ? "Block mode: left place / right erase / B exit / S save" :
             "B: Block mode    S: Save all    Esc: Deselect", 24, 48, 18, DARKGRAY);
    DrawText(TextFormat("Map %d", mapIndex + 1), 700, 20, 20, MAROON);
    DrawText(message, 580, 48, 18, MAROON);
    if (selected == 1) {
        DrawRectangleRec(playerInspectorBounds, Fade(RAYWHITE, 0.92f));
        DrawRectangleLinesEx(playerInspectorBounds, 2.0f, DARKBLUE);
        DrawText("Player Inspector", 716, 92, 18, DARKBLUE);
        DrawRectangleRec(GetInspectorCloseButton(1), Fade(MAROON, 0.88f));
        DrawText("x", 899, 89, 16, RAYWHITE);
        DrawText(TextFormat("Move speed: %.0f", player->moveSpeed), 716, 120, 17, DARKGRAY);
        DrawText("[-] 20", 716, 144, 16, MAROON);
        DrawText("[+] 20", 820, 144, 16, DARKGREEN);
        DrawRectangle(716, 174, 188, 26, DARKBLUE);
        DrawText("Save player settings", 732, 179, 16, RAYWHITE);
    } else if (selected == 2) {
        DrawNpcSummaryInspector(dialogue);
    }
    if (isDialogueEditorOpen) {
        DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
        DrawNpcInspector(dialogue, dialogueScroll, activeDialogueLine, dialogueCursorIndex,
                         selectionAnchor, selectionEnd, draggedDialogueLine,
                         dialogueBlockHeight, dialogueFontSize);
    }
    EndDrawing();
}

int main(void)
{
    const Rectangle area = { 32.0f, 80.0f, 896.0f, 320.0f };
    InitWindow(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, "1_44MB - RPG Editor");
    SetTargetFPS(60);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));
    RpgLayout layout = RpgLayout_Default();
    RpgLayout_Load(TextFormat("%s../assets/Settings/rpg_layout.cfg", GetApplicationDirectory()), &layout);
    RpgStage stage = RpgStage_Default();
    RpgStage_Load(TextFormat("%s../assets/Settings/rpg_stage.cfg", GetApplicationDirectory()), &stage);
    RpgDialogue dialogue = RpgDialogue_Default();
    RpgDialogue_Load(TextFormat("%s../assets/Settings/rpg_dialogue.txt",
                                GetApplicationDirectory()), &dialogue);
    for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
        GameFont_AddText(dialogue.lines[lineIndex]);
    }
    RpgCharacter player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
    RpgCharacter npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
    player.position.y = 400.0f;
    player.moveSpeed = layout.playerMoveSpeed;
    npc.position.y = 400.0f;
    int selected = 0;
    int mapIndex = 0;
    int dialogueScroll = 0;
    int activeDialogueLine = -1;
    int dialogueCursorIndex = 0;
    int selectionAnchor = 0;
    int selectionEnd = 0;
    int draggedDialogueLine = -1;
    bool isDialogueEditorOpen = false;
    int dialogueFontSize = 22;
    int dialogueBlockHeight = dialogueFontSize + 10;
    bool blockMode = false;
    const char *message = "Select a character";

    while (!WindowShouldClose()) {
        Vector2 mousePosition = GetMousePosition();
        int visibleDialogueLines = GetVisibleDialogueLines(dialogueBlockHeight);
        bool isDialogueEditing = isDialogueEditorOpen;
        // 日本語IMEのローマ字入力中は、文字キーのエディター操作を受け付けない。
        if (!isDialogueEditing && IsKeyPressed(KEY_ONE)) mapIndex = 0;
        if (!isDialogueEditing && IsKeyPressed(KEY_TWO)) mapIndex = 1;
        if (!isDialogueEditing && IsKeyPressed(KEY_THREE)) mapIndex = 2;
        if (!isDialogueEditing && IsKeyPressed(KEY_B)) { blockMode = !blockMode; selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; }
        if (!isDialogueEditing && IsKeyPressed(KEY_ESCAPE)) { selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; message = "Selection cleared"; }
        if (isDialogueEditorOpen && activeDialogueLine >= 0) {
            UpdateImeCandidateWindow(activeDialogueLine, dialogueScroll);
        }

        bool isPlayerInspectorClicked = selected == 1 &&
                                        CheckCollisionPointRec(mousePosition, playerInspectorBounds);
        bool isNpcSummaryClicked = selected == 2 && !isDialogueEditorOpen &&
                                   CheckCollisionPointRec(mousePosition, npcInspectorBounds);
        bool isDialogueEditorClicked = selected == 2 && isDialogueEditorOpen &&
                                       CheckCollisionPointRec(mousePosition, dialogueEditorBounds);
        bool isInspectorClicked = isPlayerInspectorClicked || isNpcSummaryClicked || isDialogueEditorClicked;
        bool isCloseInspectorClicked = (isPlayerInspectorClicked || isNpcSummaryClicked) &&
                                       CheckCollisionPointRec(mousePosition, GetInspectorCloseButton(selected));
        bool isCloseDialogueEditorClicked = isDialogueEditorClicked &&
                                            CheckCollisionPointRec(mousePosition, GetDialogueEditorCloseButton());
        if (isDialogueEditorOpen && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int maxScroll = dialogue.lineCount > visibleDialogueLines ? dialogue.lineCount - visibleDialogueLines : 0;
            dialogueScroll -= (int)GetMouseWheelMove();
            if (dialogueScroll < 0) dialogueScroll = 0;
            if (dialogueScroll > maxScroll) dialogueScroll = maxScroll;
        }
        if (isCloseDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isDialogueEditorOpen = false;
            activeDialogueLine = -1;
            draggedDialogueLine = -1;
            message = "Dialogue editor closed";
        } else if (isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            selected = 0;
            activeDialogueLine = -1;
            draggedDialogueLine = -1;
            message = "Inspector closed";
        } else if (isPlayerInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 708, 138, 86, 28 })) player.moveSpeed -= 20.0f;
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 810, 138, 86, 28 })) player.moveSpeed += 20.0f;
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 174, 188, 26 })) {
                message = SaveEditorData(&layout, &player, &npc, &stage, &dialogue) ?
                          "Player settings saved" : "Could not save";
            }
            if (player.moveSpeed < 60.0f) player.moveSpeed = 60.0f;
            if (player.moveSpeed > 480.0f) player.moveSpeed = 480.0f;
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 164, 188, 32 })) {
            isDialogueEditorOpen = true;
            activeDialogueLine = -1;
            message = "Dialogue editor opened";
        }
        if (!isCloseDialogueEditorClicked && isDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 680, 64, 42, 22 })) {
                if (dialogueFontSize > 12) dialogueFontSize--;
                dialogueBlockHeight = dialogueFontSize + 10;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 730, 64, 42, 22 })) {
                if (dialogueFontSize < 32) dialogueFontSize++;
                dialogueBlockHeight = dialogueFontSize + 10;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
                int line = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
                activeDialogueLine = line < dialogue.lineCount ? line : -1;
                if (activeDialogueLine >= 0) {
                    dialogueCursorIndex = GetCursorIndexAtX(dialogue.lines[activeDialogueLine],
                                                            mousePosition.x - 200.0f, (float)dialogueFontSize);
                    selectionAnchor = dialogueCursorIndex;
                    selectionEnd = dialogueCursorIndex;
                }
                draggedDialogueLine = activeDialogueLine;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 156, 414, 210, 32 })) {
                if (RpgDialogue_AddLine(&dialogue)) {
                    activeDialogueLine = dialogue.lineCount - 1;
                    dialogueCursorIndex = 0;
                    selectionAnchor = 0;
                    selectionEnd = 0;
                    if (activeDialogueLine >= visibleDialogueLines) dialogueScroll = activeDialogueLine - visibleDialogueLines + 1;
                    message = "Dialogue line added";
                } else message = "Dialogue line limit reached";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 382, 414, 244, 32 })) {
                message = SaveEditorData(&layout, &player, &npc, &stage, &dialogue) ?
                          "NPC dialogue saved" : "Could not save";
            }
        }
        if (isDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int line = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
            if (RpgDialogue_DeleteLine(&dialogue, line)) {
                activeDialogueLine = -1;
                message = "Dialogue line deleted";
            } else message = "Keep at least one dialogue line";
        }
        if (draggedDialogueLine >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            isDialogueEditorClicked && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int hoveredLine = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
            if (hoveredLine == draggedDialogueLine) {
                selectionEnd = GetCursorIndexAtX(dialogue.lines[activeDialogueLine], mousePosition.x - 200.0f,
                                                  (float)dialogueFontSize);
                dialogueCursorIndex = selectionEnd;
            }
        }
        if (draggedDialogueLine >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (isDialogueEditorClicked && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
                int destination = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
                if (destination >= dialogue.lineCount) destination = dialogue.lineCount - 1;
                if (destination != draggedDialogueLine &&
                    RpgDialogue_MoveLine(&dialogue, draggedDialogueLine, destination)) {
                    activeDialogueLine = destination;
                    dialogueCursorIndex = (int)strlen(dialogue.lines[destination]);
                    selectionAnchor = dialogueCursorIndex;
                    selectionEnd = dialogueCursorIndex;
                    message = "Dialogue line moved";
                }
            }
            draggedDialogueLine = -1;
        }
        if (isDialogueEditorOpen) {
            UpdateDialogueText(&dialogue, activeDialogueLine, &dialogueCursorIndex,
                               &selectionAnchor, &selectionEnd);
        }
        int largestWrappedLineCount = 1;
        for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
            int wrappedLineCount = GetWrappedLineCount(dialogue.lines[lineIndex], (float)dialogueFontSize);
            if (wrappedLineCount > largestWrappedLineCount) largestWrappedLineCount = wrappedLineCount;
        }
        dialogueBlockHeight = largestWrappedLineCount * (dialogueFontSize + 2) + 8;

        bool isUiBlockingMap = isInspectorClicked || isDialogueEditorOpen;
        if (blockMode && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            mousePosition.x += mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            RpgStage_SetBlockAtPosition(&stage, mousePosition, true);
        } else if (blockMode && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            mousePosition.x += mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            RpgStage_SetBlockAtPosition(&stage, mousePosition, false);
        } else if (!isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgCharacter localPlayer = GetLocalCharacter(&player, mapIndex);
            RpgCharacter localNpc = GetLocalCharacter(&npc, mapIndex);
            if (IsCharacterInMap(&player, mapIndex) && IsCharacterClicked(&localPlayer, mousePosition)) {
                selected = 1; activeDialogueLine = -1; message = "Hero selected";
            } else if (IsCharacterInMap(&npc, mapIndex) && IsCharacterClicked(&localNpc, mousePosition)) {
                selected = 2; activeDialogueLine = -1; message = "NPC selected";
            } else if (selected != 0 && CheckCollisionPointRec(mousePosition, area)) {
                float worldX = mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
                if (selected == 1) player.position.x = worldX;
                else npc.position.x = worldX;
                message = "Position updated";
            }
        }
        if (IsKeyPressed(KEY_S) && activeDialogueLine < 0) {
            message = SaveEditorData(&layout, &player, &npc, &stage, &dialogue) ? "All data saved" : "Could not save";
        }
        DrawEditor(&player, &npc, &stage, &dialogue, selected, mapIndex, blockMode,
                   dialogueScroll, activeDialogueLine, dialogueCursorIndex, selectionAnchor,
                   selectionEnd, draggedDialogueLine, isDialogueEditorOpen, dialogueBlockHeight,
                   dialogueFontSize, message);
    }
    GameFont_Unload();
    CloseWindow();
    return 0;
}
