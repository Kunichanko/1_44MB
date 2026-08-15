// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_dialogue.h, rpg_editor_history.h, rpg_layout.h, rpg_stage.h, game_font.h
// 依存関係を更新: rpg_stage3_event.h を追加した。
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <imm.h>

typedef LRESULT (CALLBACK *EditorWindowProcedureType)(HWND, UINT, WPARAM, LPARAM);
extern LONG_PTR WINAPI SetWindowLongPtrA(HWND window, int index, LONG_PTR value);
extern LRESULT WINAPI CallWindowProcA(EditorWindowProcedureType previousProcedure, HWND window,
                                      UINT message, WPARAM wParam, LPARAM lParam);
enum { EDITOR_WM_CLOSE = 0x0010, EDITOR_GWLP_WNDPROC = -4 };
#endif

#include "raylib.h"
#include "raymath.h"

#include <string.h>
#include <stdio.h>

#include "game_font.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_editor_history.h"
#include "rpg_layout.h"
#include "rpg_inspect.h"
#include "rpg_stage3_event.h"
#include "rpg_stage.h"
#include "rpg_zipper.h"

enum { RPG_EDITOR_WIDTH = 960, RPG_EDITOR_HEIGHT = 540, NPC_VISIBLE_LINES = 9 };
enum { MODAL_HISTORY_CAPACITY = 8 };

typedef enum EditorModal {
    EDITOR_MODAL_NONE,
    EDITOR_MODAL_EXAMINE_LIST,
    EDITOR_MODAL_FUNCTION_TYPE_LIST,
    EDITOR_MODAL_MOVE_EDITOR
} EditorModal;

typedef enum EditorSaveState {
    EDITOR_SAVE_NONE,
    EDITOR_SAVE_SUCCEEDED,
    EDITOR_SAVE_FAILED
} EditorSaveState;

typedef RpgEditorState EditorSaveSnapshot;

typedef struct BlockHistoryEntry {
    int row;
    int column;
    int previousValue;
} BlockHistoryEntry;

typedef struct BlockHistory {
    int count;
    BlockHistoryEntry entries[RPG_EDITOR_HISTORY_CAPACITY];
} BlockHistory;

enum { EXIT_DETAIL_MAX = 40, EXIT_DETAIL_TEXT_LENGTH = 96, EXIT_DETAIL_VISIBLE_ROWS = 7 };

typedef struct ExitDetailList {
    char rows[EXIT_DETAIL_MAX][EXIT_DETAIL_TEXT_LENGTH];
    int count;
} ExitDetailList;

typedef struct ModalHistory {
    EditorModal entries[MODAL_HISTORY_CAPACITY];
    int count;
} ModalHistory;

static const Rectangle playerInspectorBounds = { 700.0f, 80.0f, 220.0f, 164.0f };
static const Rectangle npcInspectorBounds = { 700.0f, 80.0f, 220.0f, 226.0f };
static const Rectangle dialogueEditorBounds = { 140.0f, 56.0f, 680.0f, 424.0f };
static const Rectangle zipperInspectorBounds = { 700.0f, 218.0f, 220.0f, 142.0f };
static Rectangle movePanelBounds = { 654.0f, 56.0f, 282.0f, 466.0f };
static const Rectangle exitConfirmationBounds = { 250.0f, 120.0f, 460.0f, 300.0f };
static const Rectangle revertSavedBounds = { 408.0f, 42.0f, 160.0f, 28.0f };
static RpgInspect npcInspectData;
static RpgZipper zipperData;
#define zipperInspectData (zipperData.inspect)
static RpgInspect *activeInspect = &npcInspectData;
#define npcInspect (*activeInspect)

static bool IsDialogueDifferent(const RpgDialogue *first, const RpgDialogue *second);
static bool IsInspectDifferent(const RpgInspect *first, const RpgInspect *second);
static bool IsInspectFunctionDifferent(const RpgInspectFunction *firstFunction,
                                       const RpgInspectFunction *secondFunction);
static int GetShiftedDeletedDialogueIndex(const RpgDialogue *dialogue, const RpgDialogue *savedDialogue,
                                          int currentIndex);
static int GetShiftedDeletedFunctionIndex(const RpgInspect *inspect, const RpgInspect *savedInspect,
                                          int currentIndex);

#ifdef _WIN32
static EditorWindowProcedureType editorOriginalWindowProcedure = NULL;
static bool isEditorCloseRequested = false;

// ネイティブの閉じる通知だけを保留し、raylibの描画ループで保存確認を表示する。
static LRESULT CALLBACK EditorWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == EDITOR_WM_CLOSE) {
        isEditorCloseRequested = true;
        return 0;
    }
    return CallWindowProcA(editorOriginalWindowProcedure, window, message, wParam, lParam);
}

static void InstallEditorCloseHandler(void)
{
    HWND window = (HWND)GetWindowHandle();
    editorOriginalWindowProcedure = (EditorWindowProcedureType)SetWindowLongPtrA(
        window, EDITOR_GWLP_WNDPROC, (LONG_PTR)EditorWindowProcedure);
}

static void RestoreEditorCloseHandler(void)
{
    if (editorOriginalWindowProcedure != NULL)
        SetWindowLongPtrA((HWND)GetWindowHandle(), EDITOR_GWLP_WNDPROC,
                           (LONG_PTR)editorOriginalWindowProcedure);
}
#else
static bool isEditorCloseRequested = false;
static void InstallEditorCloseHandler(void) { }
static void RestoreEditorCloseHandler(void) { }
#endif

static void ModalHistory_Push(ModalHistory *history, EditorModal modal)
{
    if (history->count < MODAL_HISTORY_CAPACITY) history->entries[history->count++] = modal;
}

static EditorModal ModalHistory_Pop(ModalHistory *history)
{
    return history->count > 0 ? history->entries[--history->count] : EDITOR_MODAL_NONE;
}

static Rectangle GetInspectorCloseButton(int selected)
{
    return selected == 3 ? (Rectangle){ 894.0f, 226.0f, 18.0f, 18.0f } :
                           (Rectangle){ 894.0f, 88.0f, 18.0f, 18.0f };
}

static Rectangle GetDialogueEditorCloseButton(void)
{
    return (Rectangle){ 792.0f, 64.0f, 18.0f, 18.0f };
}

static Rectangle GetMovePanelControl(float x, float y, float width, float height)
{
    return (Rectangle){ movePanelBounds.x + x, movePanelBounds.y + y, width, height };
}

static Rectangle GetExitConfirmationButton(int index)
{
    return (Rectangle){ exitConfirmationBounds.x + 18.0f + index * 142.0f,
                        exitConfirmationBounds.y + 240.0f, 128.0f, 34.0f };
}

static Rectangle GetExitDetailsToggle(void)
{
    return (Rectangle){ exitConfirmationBounds.x + 140.0f, exitConfirmationBounds.y + 84.0f, 180.0f, 26.0f };
}

static Rectangle GetExitDetailsBounds(void)
{
    return (Rectangle){ exitConfirmationBounds.x + 24.0f, exitConfirmationBounds.y + 116.0f, 412.0f, 106.0f };
}

static void AddExitDetail(ExitDetailList *details, const char *text)
{
    if (details->count >= EXIT_DETAIL_MAX) return;
    snprintf(details->rows[details->count++], EXIT_DETAIL_TEXT_LENGTH, "%s", text);
}

static void AddInspectDetails(ExitDetailList *details, const char *owner,
                              const RpgInspect *inspect, const RpgInspect *savedInspect)
{
    if (inspect->functionCount != savedInspect->functionCount || inspect->enabled != savedInspect->enabled)
        AddExitDetail(details, TextFormat("- %s examine settings", owner));
    for (int index = 0; index < inspect->functionCount; index++) {
        if (index >= savedInspect->functionCount ||
            IsInspectFunctionDifferent(&inspect->functions[index], &savedInspect->functions[index]))
            AddExitDetail(details, TextFormat("- %s: %s", owner, inspect->functions[index].title));
    }
}

static ExitDetailList BuildUnsavedDetails(const EditorSaveSnapshot *snapshot, const RpgCharacter *player,
                                          const RpgCharacter *npc, const RpgStage *stage,
                                          const RpgDialogue *dialogue, const RpgStage3Event *stage3Event)
{
    ExitDetailList details = { 0 };
    if (snapshot->player.position.x != player->position.x || snapshot->player.moveSpeed != player->moveSpeed ||
        snapshot->player.scale != player->scale) AddExitDetail(&details, "- Player");
    if (snapshot->npc.position.x != npc->position.x || snapshot->npc.scale != npc->scale) {
        AddExitDetail(&details, "- NPC");
    }
    if (snapshot->zipper.character.position.x != zipperData.character.position.x ||
        snapshot->zipper.character.scale != zipperData.character.scale) {
        AddExitDetail(&details, "- Zipper");
    }
    if (memcmp(&snapshot->stage.blocks, &stage->blocks, sizeof(stage->blocks)) != 0) {
        AddExitDetail(&details, "- Stage blocks");
    }
    if (IsDialogueDifferent(&snapshot->dialogue, dialogue)) AddExitDetail(&details, "- NPC dialogue");
    if (snapshot->stage3Event.enabled != stage3Event->enabled ||
        IsDialogueDifferent(&snapshot->stage3Event.dialogue, &stage3Event->dialogue)) {
        AddExitDetail(&details, "- Stage 3 event");
    }
    AddInspectDetails(&details, "NPC", &npcInspectData, &snapshot->npcInspectSnapshot);
    AddInspectDetails(&details, "Zipper", &zipperInspectData, &snapshot->zipperInspectSnapshot);
    return details;
}

static void DrawExitConfirmation(bool showDetails, const EditorSaveSnapshot *snapshot,
                                 const RpgCharacter *player, const RpgCharacter *npc,
                                 const RpgStage *stage, const RpgDialogue *dialogue,
                                 const RpgStage3Event *stage3Event, int detailScroll)
{
    DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.68f));
    DrawRectangleRec(exitConfirmationBounds, RAYWHITE);
    DrawRectangleLinesEx(exitConfirmationBounds, 2.0f, MAROON);
    DrawText("Unsaved changes", 322, 144, 24, MAROON);
    DrawText("Save before closing?", 346, 178, 18, DARKGRAY);
    DrawRectangleRec(GetExitDetailsToggle(), DARKBLUE);
    DrawText(showDetails ? "Hide details" : "Show details", 389, 210, 16, RAYWHITE);
    if (showDetails) {
        ExitDetailList details = BuildUnsavedDetails(snapshot, player, npc, stage, dialogue, stage3Event);
        Rectangle detailBounds = GetExitDetailsBounds();
        DrawRectangleRec(detailBounds, Fade(LIGHTGRAY, 0.55f));
        DrawRectangleLinesEx(detailBounds, 1.0f, GRAY);
        for (int row = 0; row < EXIT_DETAIL_VISIBLE_ROWS; row++) {
            int index = detailScroll + row;
            if (index >= details.count) break;
            DrawText(details.rows[index], (int)detailBounds.x + 8, (int)detailBounds.y + 5 + row * 14, 14, MAROON);
        }
        if (details.count > EXIT_DETAIL_VISIBLE_ROWS)
            DrawText(TextFormat("%d-%d / %d", detailScroll + 1,
                                 detailScroll + EXIT_DETAIL_VISIBLE_ROWS < details.count ? detailScroll + EXIT_DETAIL_VISIBLE_ROWS : details.count,
                                 details.count), 600, 330, 12, DARKGRAY);
    }
    DrawRectangleRec(GetExitConfirmationButton(0), DARKGREEN);
    DrawText("Save and exit", 284, 366, 16, RAYWHITE);
    DrawRectangleRec(GetExitConfirmationButton(1), MAROON);
    DrawText("Discard", 450, 366, 16, RAYWHITE);
    DrawRectangleRec(GetExitConfirmationButton(2), DARKBLUE);
    DrawText("Cancel", 596, 366, 16, RAYWHITE);
}

static EditorSaveState GetSaveState(const char *message)
{
    if (strcmp(message, "Saved") == 0) return EDITOR_SAVE_SUCCEEDED;
    if (strcmp(message, "Save failed") == 0) return EDITOR_SAVE_FAILED;
    return EDITOR_SAVE_NONE;
}

static void DrawSaveButton(Rectangle bounds, EditorSaveState saveState)
{
    const char *label = saveState == EDITOR_SAVE_SUCCEEDED ? "Saved!" :
                        saveState == EDITOR_SAVE_FAILED ? "Save failed" : "Save";
    Color color = saveState == EDITOR_SAVE_SUCCEEDED ? DARKGREEN :
                  saveState == EDITOR_SAVE_FAILED ? MAROON : DARKBLUE;
    int fontSize = 17;
    DrawRectangleRec(bounds, color);
    DrawText(label, (int)(bounds.x + (bounds.width - MeasureText(label, fontSize)) * 0.5f),
             (int)(bounds.y + (bounds.height - fontSize) * 0.5f), fontSize, RAYWHITE);
}

static void DrawRevertButton(Rectangle bounds)
{
    const int fontSize = 15;
    DrawRectangleRec(bounds, MAROON);
    DrawText("Revert", (int)(bounds.x + (bounds.width - MeasureText("Revert", fontSize)) * 0.5f),
             (int)(bounds.y + (bounds.height - fontSize) * 0.5f), fontSize, RAYWHITE);
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
    Rectangle characterBounds = { character->position.x - 22.0f * character->scale,
                                  character->position.y - 84.0f * character->scale,
                                  44.0f * character->scale, 90.0f * character->scale };
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
                           const RpgDialogue *dialogue, const RpgStage3Event *stage3Event)
{
    layout->playerPosition = player->position;
    layout->npcPosition = npc->position;
    layout->playerMoveSpeed = player->moveSpeed;
    layout->playerScale = player->scale;
    layout->npcScale = npc->scale;
    bool layoutSaved = RpgLayout_Save(TextFormat("%s../assets/Settings/rpg_layout.cfg",
                                                  GetApplicationDirectory()), layout);
    bool stageSaved = RpgStage_Save(TextFormat("%s../assets/Settings/rpg_stage.cfg",
                                                GetApplicationDirectory()), stage);
    bool dialogueSaved = RpgDialogue_Save(TextFormat("%s../assets/Settings/rpg_dialogue.txt",
                                                      GetApplicationDirectory()), dialogue);
    bool eventSaved = RpgStage3Event_Save(TextFormat("%s../assets/Settings/rpg_stage3_event.cfg",
                                                      GetApplicationDirectory()), stage3Event);
    bool zipperSaved = RpgZipper_Save(TextFormat("%s../assets/Settings/rpg_zipper.cfg",
                                                  GetApplicationDirectory()), &zipperData);
    bool npcInspectSaved = RpgInspect_Save(TextFormat("%s../assets/Settings/rpg_inspect.cfg",
                                                      GetApplicationDirectory()), &npcInspectData);
    bool zipperInspectSaved = RpgInspect_Save(TextFormat("%s../assets/Settings/rpg_zipper_inspect.cfg",
                                                         GetApplicationDirectory()), &zipperInspectData);
    return layoutSaved && stageSaved && dialogueSaved && eventSaved && zipperSaved && npcInspectSaved && zipperInspectSaved;
}

static void UpdateSaveSnapshot(EditorSaveSnapshot *snapshot, const RpgCharacter *player,
                               const RpgCharacter *npc, const RpgStage *stage,
                               const RpgDialogue *dialogue, const RpgStage3Event *stage3Event)
{
    snapshot->player = *player;
    snapshot->npc = *npc;
    snapshot->stage = *stage;
    snapshot->dialogue = *dialogue;
    snapshot->stage3Event = *stage3Event;
    snapshot->zipper = zipperData;
    snapshot->npcInspectSnapshot = npcInspectData;
    snapshot->zipperInspectSnapshot = zipperInspectData;
}

static void CaptureInspectorUi(EditorSaveSnapshot *snapshot, int mapIndex, int selected,
                               bool isDialogueEditorOpen, bool isExamineFunctionListOpen,
                               bool isFunctionTypeListOpen, bool isMoveFunctionEditorOpen,
                               bool isStage3DialogueEditing, bool isInspectDialogueEditing)
{
    snapshot->mapIndex = mapIndex;
    snapshot->selected = selected;
    snapshot->activeInspectKind = activeInspect == &zipperInspectData ? 1 : 0;
    snapshot->isDialogueEditorOpen = isDialogueEditorOpen;
    snapshot->isExamineFunctionListOpen = isExamineFunctionListOpen;
    snapshot->isFunctionTypeListOpen = isFunctionTypeListOpen;
    snapshot->isMoveFunctionEditorOpen = isMoveFunctionEditorOpen;
    snapshot->isStage3DialogueEditing = isStage3DialogueEditing;
    snapshot->isInspectDialogueEditing = isInspectDialogueEditing;
}

static bool IsInspectorUiDifferent(const EditorSaveSnapshot *before, int mapIndex, int selected,
                                   bool isDialogueEditorOpen, bool isExamineFunctionListOpen,
                                   bool isFunctionTypeListOpen, bool isMoveFunctionEditorOpen,
                                   bool isStage3DialogueEditing, bool isInspectDialogueEditing)
{
    return before->mapIndex != mapIndex || before->selected != selected ||
           before->activeInspectKind != (activeInspect == &zipperInspectData ? 1 : 0) ||
           before->isDialogueEditorOpen != isDialogueEditorOpen ||
           before->isExamineFunctionListOpen != isExamineFunctionListOpen ||
           before->isFunctionTypeListOpen != isFunctionTypeListOpen ||
           before->isMoveFunctionEditorOpen != isMoveFunctionEditorOpen ||
           before->isStage3DialogueEditing != isStage3DialogueEditing ||
           before->isInspectDialogueEditing != isInspectDialogueEditing;
}

static void RestoreInspectorUi(const EditorSaveSnapshot *state, int *mapIndex, int *selected,
                               bool *isDialogueEditorOpen, bool *isExamineFunctionListOpen,
                               bool *isFunctionTypeListOpen, bool *isMoveFunctionEditorOpen,
                               bool *isStage3DialogueEditing, bool *isInspectDialogueEditing)
{
    *mapIndex = state->mapIndex;
    *selected = state->selected;
    activeInspect = state->activeInspectKind ? &zipperInspectData : &npcInspectData;
    *isDialogueEditorOpen = state->isDialogueEditorOpen;
    *isExamineFunctionListOpen = state->isExamineFunctionListOpen;
    *isFunctionTypeListOpen = state->isFunctionTypeListOpen;
    *isMoveFunctionEditorOpen = state->isMoveFunctionEditorOpen;
    *isStage3DialogueEditing = state->isStage3DialogueEditing;
    *isInspectDialogueEditing = state->isInspectDialogueEditing;
}

static void PushBlockHistory(BlockHistory *history, int row, int column, int previousValue)
{
    if (history->count == RPG_EDITOR_HISTORY_CAPACITY) {
        memmove(history->entries, history->entries + 1, sizeof(history->entries[0]) * (RPG_EDITOR_HISTORY_CAPACITY - 1));
        history->count--;
    }
    history->entries[history->count++] = (BlockHistoryEntry){ row, column, previousValue };
}

static bool UndoBlockChange(BlockHistory *history, RpgStage *stage)
{
    if (history->count <= 0) return false;
    BlockHistoryEntry entry = history->entries[--history->count];
    stage->blocks[entry.row][entry.column] = entry.previousValue;
    return true;
}

// 未保存の編集中データだけを、直近の保存スナップショットへ戻す。
static void RevertToSavedSnapshot(const EditorSaveSnapshot *snapshot, RpgLayout *layout,
                                  RpgCharacter *player, RpgCharacter *npc, RpgStage *stage,
                                  RpgDialogue *dialogue, RpgStage3Event *stage3Event)
{
    *player = snapshot->player;
    *npc = snapshot->npc;
    *stage = snapshot->stage;
    *dialogue = snapshot->dialogue;
    *stage3Event = snapshot->stage3Event;
    zipperData = snapshot->zipper;
    npcInspectData = snapshot->npcInspectSnapshot;
    zipperInspectData = snapshot->zipperInspectSnapshot;
    layout->playerPosition = player->position;
    layout->npcPosition = npc->position;
    layout->playerMoveSpeed = player->moveSpeed;
    layout->playerScale = player->scale;
    layout->npcScale = npc->scale;
}

static bool IsDialogueDifferent(const RpgDialogue *first, const RpgDialogue *second)
{
    if (first->lineCount != second->lineCount) return true;
    for (int index = 0; index < first->lineCount; index++) {
        if (strcmp(first->speakers[index], second->speakers[index]) != 0 ||
            strcmp(first->lines[index], second->lines[index]) != 0) return true;
    }
    return false;
}

static bool IsDialogueLineDifferent(const RpgDialogue *dialogue, const RpgDialogue *savedDialogue, int lineIndex)
{
    return lineIndex >= savedDialogue->lineCount ||
           strcmp(dialogue->speakers[lineIndex], savedDialogue->speakers[lineIndex]) != 0 ||
           strcmp(dialogue->lines[lineIndex], savedDialogue->lines[lineIndex]) != 0;
}

static bool IsInspectFunctionDifferent(const RpgInspectFunction *firstFunction,
                                       const RpgInspectFunction *secondFunction)
{
    if (strcmp(firstFunction->title, secondFunction->title) != 0 || firstFunction->type != secondFunction->type)
        return true;
    if (firstFunction->type == RPG_INSPECT_DIALOGUE)
        return IsDialogueDifferent(&firstFunction->dialogue, &secondFunction->dialogue);
    return firstFunction->move.target != secondFunction->move.target ||
           firstFunction->move.destinationX != secondFunction->move.destinationX ||
           firstFunction->move.duration != secondFunction->move.duration;
}

static bool IsInspectDifferent(const RpgInspect *first, const RpgInspect *second)
{
    if (first->enabled != second->enabled || first->functionCount != second->functionCount) return true;
    for (int index = 0; index < first->functionCount; index++) {
        const RpgInspectFunction *firstFunction = &first->functions[index];
        const RpgInspectFunction *secondFunction = &second->functions[index];
        if (IsInspectFunctionDifferent(firstFunction, secondFunction)) return true;
    }
    return false;
}

// 後続要素が詰められた削除だけを検出し、表示中の要素を邪魔しない右端マーカーにする。
static int GetShiftedDeletedDialogueIndex(const RpgDialogue *dialogue, const RpgDialogue *savedDialogue,
                                          int currentIndex)
{
    if (currentIndex >= savedDialogue->lineCount ||
        !IsDialogueLineDifferent(dialogue, savedDialogue, currentIndex)) return -1;
    for (int savedIndex = currentIndex + 1; savedIndex < savedDialogue->lineCount; savedIndex++) {
        if (strcmp(dialogue->speakers[currentIndex], savedDialogue->speakers[savedIndex]) == 0 &&
            strcmp(dialogue->lines[currentIndex], savedDialogue->lines[savedIndex]) == 0)
            return currentIndex;
    }
    return -1;
}

static int GetShiftedDeletedFunctionIndex(const RpgInspect *inspect, const RpgInspect *savedInspect,
                                          int currentIndex)
{
    if (currentIndex >= savedInspect->functionCount ||
        !IsInspectFunctionDifferent(&inspect->functions[currentIndex], &savedInspect->functions[currentIndex])) return -1;
    for (int savedIndex = currentIndex + 1; savedIndex < savedInspect->functionCount; savedIndex++)
        if (!IsInspectFunctionDifferent(&inspect->functions[currentIndex], &savedInspect->functions[savedIndex]))
            return currentIndex;
    return -1;
}

static bool HasUnsavedChanges(const EditorSaveSnapshot *snapshot, const RpgCharacter *player,
                              const RpgCharacter *npc, const RpgStage *stage,
                              const RpgDialogue *dialogue, const RpgStage3Event *stage3Event)
{
    return snapshot->player.position.x != player->position.x || snapshot->player.moveSpeed != player->moveSpeed ||
           snapshot->player.scale != player->scale || snapshot->npc.position.x != npc->position.x ||
           snapshot->npc.scale != npc->scale || memcmp(&snapshot->stage.blocks, &stage->blocks, sizeof(stage->blocks)) != 0 ||
           IsDialogueDifferent(&snapshot->dialogue, dialogue) || snapshot->stage3Event.enabled != stage3Event->enabled ||
           snapshot->zipper.character.position.x != zipperData.character.position.x ||
           snapshot->zipper.character.scale != zipperData.character.scale ||
           IsDialogueDifferent(&snapshot->stage3Event.dialogue, &stage3Event->dialogue) ||
           IsInspectDifferent(&snapshot->npcInspectSnapshot, &npcInspectData) ||
           IsInspectDifferent(&snapshot->zipperInspectSnapshot, &zipperInspectData);
}

static void RecordEditorHistory(RpgEditorHistory *history, const EditorSaveSnapshot *beforeEdit,
                                const RpgCharacter *player, const RpgCharacter *npc, const RpgStage *stage,
                                const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                int mapIndex, int selected, bool isDialogueEditorOpen,
                                bool isExamineFunctionListOpen, bool isFunctionTypeListOpen,
                                bool isMoveFunctionEditorOpen, bool isStage3DialogueEditing,
                                bool isInspectDialogueEditing)
{
    if (HasUnsavedChanges(beforeEdit, player, npc, stage, dialogue, stage3Event) ||
        IsInspectorUiDifferent(beforeEdit, mapIndex, selected, isDialogueEditorOpen,
                               isExamineFunctionListOpen, isFunctionTypeListOpen,
                               isMoveFunctionEditorOpen, isStage3DialogueEditing,
                               isInspectDialogueEditing))
        RpgEditorHistory_Push(history, beforeEdit);
}

static bool UndoEditorChange(RpgEditorHistory *history, RpgLayout *layout, RpgCharacter *player,
                             RpgCharacter *npc, RpgStage *stage, RpgDialogue *dialogue,
                             RpgStage3Event *stage3Event, int *mapIndex, int *selected,
                             bool *isDialogueEditorOpen, bool *isExamineFunctionListOpen,
                             bool *isFunctionTypeListOpen, bool *isMoveFunctionEditorOpen,
                             bool *isStage3DialogueEditing, bool *isInspectDialogueEditing)
{
    static EditorSaveSnapshot previousState;
    if (!RpgEditorHistory_Pop(history, &previousState)) return false;
    RevertToSavedSnapshot(&previousState, layout, player, npc, stage, dialogue, stage3Event);
    RestoreInspectorUi(&previousState, mapIndex, selected, isDialogueEditorOpen,
                       isExamineFunctionListOpen, isFunctionTypeListOpen,
                       isMoveFunctionEditorOpen, isStage3DialogueEditing,
                       isInspectDialogueEditing);
    return true;
}

static bool SaveEditorAndUpdateSnapshot(RpgLayout *layout, const RpgCharacter *player,
                                        const RpgCharacter *npc, const RpgStage *stage,
                                        const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                        EditorSaveSnapshot *snapshot)
{
    if (!SaveEditorData(layout, player, npc, stage, dialogue, stage3Event)) return false;
    UpdateSaveSnapshot(snapshot, player, npc, stage, dialogue, stage3Event);
    return true;
}

// キャラクター用の保存は、他の未保存項目を書き込まず位置・大きさだけを反映する。
static bool SaveCharacterSettings(RpgLayout *layout, const RpgCharacter *character,
                                  bool isPlayer, EditorSaveSnapshot *snapshot)
{
    RpgLayout savedLayout = *layout;
    savedLayout.playerPosition = isPlayer ? character->position : snapshot->player.position;
    savedLayout.playerMoveSpeed = isPlayer ? character->moveSpeed : snapshot->player.moveSpeed;
    savedLayout.playerScale = isPlayer ? character->scale : snapshot->player.scale;
    savedLayout.npcPosition = isPlayer ? snapshot->npc.position : character->position;
    savedLayout.npcScale = isPlayer ? snapshot->npc.scale : character->scale;
    if (!RpgLayout_Save(TextFormat("%s../assets/Settings/rpg_layout.cfg", GetApplicationDirectory()), &savedLayout))
        return false;
    *layout = savedLayout;
    if (isPlayer) snapshot->player = *character;
    else snapshot->npc = *character;
    return true;
}

static bool SaveZipperSettings(EditorSaveSnapshot *snapshot)
{
    if (!RpgZipper_Save(TextFormat("%s../assets/Settings/rpg_zipper.cfg", GetApplicationDirectory()), &zipperData))
        return false;
    snapshot->zipper.character = zipperData.character;
    return true;
}

static void RevertCharacterSettings(const EditorSaveSnapshot *snapshot, RpgLayout *layout,
                                    RpgCharacter *character, bool isPlayer)
{
    *character = isPlayer ? snapshot->player : snapshot->npc;
    if (isPlayer) {
        layout->playerPosition = character->position;
        layout->playerMoveSpeed = character->moveSpeed;
        layout->playerScale = character->scale;
    } else {
        layout->npcPosition = character->position;
        layout->npcScale = character->scale;
    }
}

static void RevertActiveInspect(const EditorSaveSnapshot *snapshot)
{
    *activeInspect = activeInspect == &npcInspectData ?
        snapshot->npcInspectSnapshot : snapshot->zipperInspectSnapshot;
}

static void RevertEditedDialogue(bool isInspectDialogueEditing, bool isStage3DialogueEditing,
                                 RpgDialogue *dialogue, RpgStage3Event *stage3Event,
                                 const EditorSaveSnapshot *snapshot)
{
    if (isInspectDialogueEditing) RevertActiveInspect(snapshot);
    else if (isStage3DialogueEditing) *stage3Event = snapshot->stage3Event;
    else *dialogue = snapshot->dialogue;
}

static bool SaveActiveInspect(EditorSaveSnapshot *snapshot)
{
    const char *fileName = activeInspect == &npcInspectData ? "rpg_inspect.cfg" : "rpg_zipper_inspect.cfg";
    if (!RpgInspect_Save(TextFormat("%s../assets/Settings/%s", GetApplicationDirectory(), fileName), activeInspect))
        return false;
    if (activeInspect == &npcInspectData) snapshot->npcInspectSnapshot = npcInspectData;
    else snapshot->zipperInspectSnapshot = zipperInspectData;
    return true;
}

static bool SaveEditedDialogue(bool isInspectDialogueEditing, bool isStage3DialogueEditing,
                               RpgDialogue *dialogue, RpgStage3Event *stage3Event,
                               EditorSaveSnapshot *snapshot)
{
    if (isInspectDialogueEditing) return SaveActiveInspect(snapshot);
    if (isStage3DialogueEditing) {
        if (!RpgStage3Event_Save(TextFormat("%s../assets/Settings/rpg_stage3_event.cfg",
                                            GetApplicationDirectory()), stage3Event)) return false;
        snapshot->stage3Event = *stage3Event;
        return true;
    }
    if (!RpgDialogue_Save(TextFormat("%s../assets/Settings/rpg_dialogue.txt",
                                     GetApplicationDirectory()), dialogue)) return false;
    snapshot->dialogue = *dialogue;
    return true;
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

static void UpdateSpeakerText(RpgDialogue *dialogue, int activeLine, int *cursorIndex,
                              int *selectionAnchor, int *selectionEnd)
{
    if (activeLine < 0 || activeLine >= dialogue->lineCount) return;
    char *speaker = dialogue->speakers[activeLine];
    int textLength = (int)strlen(speaker);
    if (*cursorIndex > textLength) *cursorIndex = textLength;
    if (IsKeyPressed(KEY_LEFT)) *cursorIndex = GetPreviousUtf8Index(speaker, *cursorIndex);
    if (IsKeyPressed(KEY_RIGHT)) *cursorIndex = GetNextUtf8Index(speaker, *cursorIndex);
    if (IsKeyPressed(KEY_HOME)) *cursorIndex = 0;
    if (IsKeyPressed(KEY_END)) *cursorIndex = textLength;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_END)) {
        *selectionAnchor = *cursorIndex;
        *selectionEnd = *cursorIndex;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !DeleteSelectedText(speaker, cursorIndex, selectionAnchor, selectionEnd)) {
        RemoveBeforeCursor(speaker, cursorIndex);
    }
    if (IsKeyPressed(KEY_DELETE) && !DeleteSelectedText(speaker, cursorIndex, selectionAnchor, selectionEnd) && speaker[*cursorIndex] != '\0') {
        int nextIndex = GetNextUtf8Index(speaker, *cursorIndex);
        memmove(speaker + *cursorIndex, speaker + nextIndex, strlen(speaker + nextIndex) + 1);
    }
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        if (codepoint >= 32) {
            DeleteSelectedText(speaker, cursorIndex, selectionAnchor, selectionEnd);
            InsertUtf8AtCursor(speaker, RPG_DIALOGUE_SPEAKER_LENGTH, cursorIndex, codepoint);
            GameFont_AddText(speaker);
        }
        codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE)) RemoveLastUtf8Character(speaker);
}

static void UpdateShortText(char *text, size_t capacity, int *cursorIndex)
{
    int textLength = (int)strlen(text);
    if (*cursorIndex > textLength) *cursorIndex = textLength;
    if (IsKeyPressed(KEY_LEFT)) *cursorIndex = GetPreviousUtf8Index(text, *cursorIndex);
    if (IsKeyPressed(KEY_RIGHT)) *cursorIndex = GetNextUtf8Index(text, *cursorIndex);
    if (IsKeyPressed(KEY_BACKSPACE)) RemoveBeforeCursor(text, cursorIndex);
    if (IsKeyPressed(KEY_DELETE) && text[*cursorIndex] != '\0') {
        int nextIndex = GetNextUtf8Index(text, *cursorIndex);
        memmove(text + *cursorIndex, text + nextIndex, strlen(text + nextIndex) + 1);
    }
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        if (codepoint >= 32) InsertUtf8AtCursor(text, capacity, cursorIndex, codepoint);
        codepoint = GetCharPressed();
    }
}

static void DrawNpcInspector(const RpgDialogue *dialogue, int scroll, int activeLine, int cursorIndex,
                             int selectionAnchor, int selectionEnd, int draggedLine,
                             int blockHeight, int fontSize, bool isSpeakerEditing,
                             int speakerCursorIndex, int speakerSelectionAnchor, int speakerSelectionEnd,
                             bool isExamineFunctionEditor, EditorSaveState saveState,
                             const RpgDialogue *savedDialogue)
{
    int textStartX = isExamineFunctionEditor ? 280 : 200;
    DrawRectangleRec(dialogueEditorBounds, Fade(RAYWHITE, 0.98f));
    DrawRectangleLinesEx(dialogueEditorBounds, 2.0f, PURPLE);
    DrawText(isExamineFunctionEditor ? "Examine Functions" : "NPC Inspector - Dialogue", 156, 68, 22, PURPLE);
    DrawRectangleRec(GetDialogueEditorCloseButton(), Fade(MAROON, 0.88f));
    DrawText("x", 797, 65, 16, RAYWHITE);
    DrawText(TextFormat("Font: %d", fontSize), 590, 68, 16, DARKGRAY);
    DrawRectangleLines(680, 64, 42, 22, MAROON);
    DrawRectangleLines(730, 64, 42, 22, DARKGREEN);
    DrawText("-", 695, 67, 18, MAROON);
    DrawText("+", 745, 67, 18, DARKGREEN);
    DrawText(isExamineFunctionEditor ? "Dialogue function settings" : "Speaker", 156, 101, 16, DARKGRAY);
    DrawRectangle(224, 96, 260, 26, isSpeakerEditing ? Fade(SKYBLUE, 0.45f) : Fade(LIGHTGRAY, 0.55f));
    DrawRectangleLines(224, 96, 260, 26, isSpeakerEditing ? PURPLE : GRAY);
    if (activeLine >= 0) {
        bool isActiveLineDirty = IsDialogueLineDifferent(dialogue, savedDialogue, activeLine);
        if (isSpeakerEditing && speakerSelectionAnchor != speakerSelectionEnd) {
            int start = speakerSelectionAnchor < speakerSelectionEnd ? speakerSelectionAnchor : speakerSelectionEnd;
            int end = speakerSelectionAnchor < speakerSelectionEnd ? speakerSelectionEnd : speakerSelectionAnchor;
            char prefix[RPG_DIALOGUE_SPEAKER_LENGTH];
            memcpy(prefix, dialogue->speakers[activeLine], (size_t)start);
            prefix[start] = '\0';
            int startX = 232 + (int)GameFont_MeasureText(prefix, 17.0f).x;
            memcpy(prefix, dialogue->speakers[activeLine], (size_t)end);
            prefix[end] = '\0';
            int endX = 232 + (int)GameFont_MeasureText(prefix, 17.0f).x;
            DrawRectangle(startX, 99, endX - startX, 20, Fade(SKYBLUE, 0.65f));
        }
        GameFont_Draw(dialogue->speakers[activeLine], 232, 101, 17,
                      isActiveLineDirty ? MAROON : DARKBLUE);
        if (isSpeakerEditing) {
            char prefix[RPG_DIALOGUE_SPEAKER_LENGTH];
            memcpy(prefix, dialogue->speakers[activeLine], (size_t)speakerCursorIndex);
            prefix[speakerCursorIndex] = '\0';
            int cursorX = 232 + (int)GameFont_MeasureText(prefix, 17.0f).x;
            DrawLine(cursorX, 99, cursorX, 120, PURPLE);
        }
    } else DrawText("Select a dialogue line", 232, 102, 16, GRAY);
    DrawText(isExamineFunctionEditor ? "Drag: order  Right click: delete" : "Right click: delete  Wheel: scroll", 500, 101, 14, DARKGRAY);

    int visibleLineCount = GetVisibleDialogueLines(blockHeight);
    for (int visibleIndex = 0; visibleIndex < visibleLineCount; visibleIndex++) {
        int lineIndex = scroll + visibleIndex;
        int lineY = 128 + visibleIndex * (blockHeight + 4);
        if (lineIndex >= dialogue->lineCount) {
            // 現在の同じインデックスが空の場合だけ、保存前に存在した削除行を示す。
            if (lineIndex < savedDialogue->lineCount) {
                DrawRectangle(154, lineY, 652, blockHeight, Fade(MAROON, 0.06f));
                DrawRectangleLines(154, lineY, 652, blockHeight, Fade(MAROON, 0.32f));
                DrawText(TextFormat("%02d  Deleted", lineIndex + 1), 160,
                         lineY + (blockHeight - 14) / 2, 14, Fade(MAROON, 0.45f));
            }
            continue;
        }
        bool isLineDirty = IsDialogueLineDifferent(dialogue, savedDialogue, lineIndex);
        int deletedIndex = GetShiftedDeletedDialogueIndex(dialogue, savedDialogue, lineIndex);
        Color background = lineIndex == draggedLine ? Fade(ORANGE, 0.35f) :
                           isLineDirty ? Fade(RED, 0.18f) :
                           lineIndex == activeLine ? Fade(PURPLE, 0.20f) : Fade(LIGHTGRAY, 0.45f);
        DrawRectangle(154, lineY, 652, blockHeight, background);
        DrawRectangleLines(154, lineY, 652, blockHeight, lineIndex == draggedLine ? ORANGE :
                           isLineDirty ? MAROON : lineIndex == activeLine ? PURPLE : GRAY);
        DrawText(isExamineFunctionEditor ? TextFormat("%02d  Dialogue", lineIndex + 1) : TextFormat("%02d", lineIndex + 1),
                 160, lineY + (blockHeight - 14) / 2, 14, DARKGRAY);
        if (lineIndex == activeLine && selectionAnchor != selectionEnd) {
            int start = selectionAnchor < selectionEnd ? selectionAnchor : selectionEnd;
            int end = selectionAnchor < selectionEnd ? selectionEnd : selectionAnchor;
            char prefix[RPG_DIALOGUE_LINE_LENGTH];
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)start);
            prefix[start] = '\0';
            int startX = textStartX + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)end);
            prefix[end] = '\0';
            int endX = textStartX + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            DrawRectangle(startX, lineY + 3, endX - startX, blockHeight - 6, Fade(SKYBLUE, 0.65f));
        }
        DrawWrappedDialogueText(dialogue->lines[lineIndex][0] == '\0' ? " " : dialogue->lines[lineIndex],
                                textStartX, lineY + 4, (float)fontSize, isLineDirty ? MAROON : DARKBLUE);
        if (deletedIndex >= 0) {
            DrawRectangle(724, lineY + 3, 78, 14, Fade(MAROON, 0.14f));
            DrawText(TextFormat("Deleted #%d", deletedIndex + 1), 727, lineY + 4, 10, Fade(MAROON, 0.65f));
        }
        if (lineIndex == activeLine) {
            char prefix[RPG_DIALOGUE_LINE_LENGTH];
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)cursorIndex);
            prefix[cursorIndex] = '\0';
            int cursorX = textStartX + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            DrawLine(cursorX, lineY + 3, cursorX, lineY + blockHeight - 3, PURPLE);
        }
    }
    DrawText(isExamineFunctionEditor ? TextFormat("%d functions", dialogue->lineCount) : TextFormat("%d lines", dialogue->lineCount), 156, 386, 16, DARKGRAY);
    DrawRectangle(156, 414, 210, 32, PURPLE);
    DrawText(isExamineFunctionEditor ? "Add Dialogue function" : "Add dialogue line", 182, 422, 18, RAYWHITE);
    DrawSaveButton((Rectangle){ 382, 414, 116, 32 }, saveState);
    DrawRevertButton((Rectangle){ 510, 414, 116, 32 });
}

static void DrawNpcSummaryInspector(const RpgDialogue *dialogue, const RpgCharacter *npc,
                                    const RpgCharacter *savedNpc, EditorSaveState saveState)
{
    DrawRectangleRec(npcInspectorBounds, Fade(RAYWHITE, 0.94f));
    DrawRectangleLinesEx(npcInspectorBounds, 2.0f, PURPLE);
    DrawText("NPC Inspector", 716, 92, 18, PURPLE);
    DrawRectangleRec(GetInspectorCloseButton(2), Fade(MAROON, 0.88f));
    DrawText("x", 899, 89, 16, RAYWHITE);
    DrawText(TextFormat("Dialogue: %d lines", dialogue->lineCount), 716, 122, 16, DARKGRAY);
    DrawText(TextFormat("Scale: %.1f", npc->scale), 716, 144, 16,
             npc->scale != savedNpc->scale ? MAROON : DARKGRAY);
    DrawText("[-]", 812, 144, 16, MAROON);
    DrawText("[+]", 864, 144, 16, DARKGREEN);
    DrawRectangle(716, 170, 188, 32, PURPLE);
    DrawText("Edit dialogue", 750, 178, 18, RAYWHITE);
    DrawRectangle(716, 208, 188, 32, DARKBLUE);
    DrawText("Edit examine", 750, 216, 17, RAYWHITE);
    DrawSaveButton((Rectangle){ 716, 246, 90, 26 }, saveState);
    DrawRevertButton((Rectangle){ 814, 246, 90, 26 });
}

static void DrawEditorZipper(Texture2D zipperTexture, const RpgZipper *zipper)
{
    // ZIPPER.png のアニメーション先頭フレームを、ステージ上の停止スプライトとして使う。
    Rectangle source = { 0.0f, 0.0f, 32.0f, 40.0f };
    float localX = zipper->character.position.x - 2.0f * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    Rectangle destination = { localX - 24.0f * zipper->character.scale,
                              380.0f - 60.0f * zipper->character.scale,
                              48.0f * zipper->character.scale, 60.0f * zipper->character.scale };
    DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

static void DrawMovePreviewSprite(Texture2D zipperTexture, const RpgCharacter *player,
                                  const RpgCharacter *npc, const RpgZipper *zipper,
                                  RpgInspectMoveTarget target, float worldX, int mapIndex)
{
    if ((int)(worldX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) != mapIndex) return;
    float localX = worldX - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    if (target == RPG_INSPECT_MOVE_ZIPPER) {
        Rectangle source = { 0, 0, 32, 40 };
        Rectangle destination = { localX - 24.0f * zipper->character.scale, 340.0f - 60.0f * zipper->character.scale,
                                  48.0f * zipper->character.scale, 60.0f * zipper->character.scale };
        DrawTexturePro(zipperTexture, source, destination, (Vector2){0}, 0, Fade(WHITE, 0.75f));
    } else {
        RpgCharacter sprite = target == RPG_INSPECT_MOVE_PLAYER ? *player : *npc;
        sprite.position = (Vector2){ localX, 400.0f };
        RpgCharacter_Draw(&sprite, "");
    }
}

static void DrawStage3EventPanel(const RpgStage3Event *event)
{
    DrawRectangle(700, 16, 244, 132, Fade(RAYWHITE, 0.96f));
    DrawRectangleLines(700, 16, 244, 132, MAROON);
    DrawText("Stage 3 Event", 712, 26, 18, MAROON);
    DrawRectangle(832, 22, 100, 24, event->enabled ? DARKGREEN : GRAY);
    DrawText(event->enabled ? "ON" : "OFF", 866, 27, 16, RAYWHITE);
    DrawText(TextFormat("Dialogue: %d lines", event->dialogue.lineCount), 712, 60, 16, DARKGRAY);
    DrawRectangle(712, 92, 220, 32, PURPLE);
    DrawText("Edit stage dialogue", 738, 100, 18, RAYWHITE);
}

static void DrawZipperInspector(const RpgZipper *zipper, const RpgZipper *savedZipper,
                                EditorSaveState saveState)
{
    DrawRectangleRec(zipperInspectorBounds, Fade(RAYWHITE, 0.94f));
    DrawRectangleLinesEx(zipperInspectorBounds, 2.0f, ORANGE);
    DrawText("Zipper Inspector", 716, 230, 18, ORANGE);
    DrawRectangleRec(GetInspectorCloseButton(3), Fade(MAROON, 0.88f));
    DrawText("x", 899, 227, 16, RAYWHITE);
    DrawText(TextFormat("Position: %.0f", zipper->character.position.x), 716, 248, 16,
             zipper->character.position.x != savedZipper->character.position.x ? MAROON : DARKGRAY);
    DrawText(TextFormat("Scale: %.1f", zipper->character.scale), 716, 270, 16,
             zipper->character.scale != savedZipper->character.scale ? MAROON : DARKGRAY);
    DrawText("[-]", 812, 270, 16, MAROON);
    DrawText("[+]", 864, 270, 16, DARKGREEN);
    DrawRectangle(716, 290, 188, 28, DARKBLUE);
    DrawText("Edit examine", 750, 296, 17, RAYWHITE);
    DrawSaveButton((Rectangle){ 716, 326, 90, 26 }, saveState);
    DrawRevertButton((Rectangle){ 814, 326, 90, 26 });
}

static void DrawExamineFunctionList(const RpgInspect *inspect, int selectedIndex,
                                    int draggedIndex, bool isTitleEditing, int titleCursorIndex,
                                    const RpgInspect *savedInspect)
{
    const Rectangle panel = { 190.0f, 86.0f, 580.0f, 360.0f };
    DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
    DrawRectangleRec(panel, Fade(RAYWHITE, 0.98f));
    DrawRectangleLinesEx(panel, 2.0f, DARKBLUE);
    DrawText("Examine Functions", 212, 104, 24, DARKBLUE);
    DrawRectangle(648, 100, 78, 24, inspect->enabled ? DARKGREEN : GRAY);
    DrawText(inspect->enabled ? "ON" : "OFF", 672, 104, 16, RAYWHITE);
    DrawText("Title", 212, 136, 16, DARKGRAY);
    DrawRectangle(260, 132, 368, 24, isTitleEditing ? Fade(SKYBLUE, 0.55f) : Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(260, 132, 368, 24, isTitleEditing ? PURPLE : GRAY);
    bool isSelectedDirty = selectedIndex >= savedInspect->functionCount ||
                           IsInspectFunctionDifferent(&inspect->functions[selectedIndex],
                                                      &savedInspect->functions[selectedIndex]);
    GameFont_Draw(inspect->functions[selectedIndex].title, 268, 135, 16,
                  isSelectedDirty ? MAROON : DARKBLUE);
    if (isTitleEditing) {
        char prefix[RPG_INSPECT_TITLE_LENGTH];
        memcpy(prefix, inspect->functions[selectedIndex].title, (size_t)titleCursorIndex);
        prefix[titleCursorIndex] = '\0';
        int cursorX = 268 + (int)GameFont_MeasureText(prefix, 16.0f).x;
        DrawLine(cursorX, 134, cursorX, 154, PURPLE);
    }
    for (int index = 0; index < inspect->functionCount; index++) {
        int y = 166 + index * 34;
        bool isFunctionDirty = index >= savedInspect->functionCount ||
                               IsInspectFunctionDifferent(&inspect->functions[index], &savedInspect->functions[index]);
        int deletedIndex = GetShiftedDeletedFunctionIndex(inspect, savedInspect, index);
        DrawRectangle(212, y, 536, 28, index == draggedIndex ? Fade(ORANGE, 0.45f) :
                      isFunctionDirty ? Fade(RED, 0.18f) : Fade(LIGHTGRAY, 0.6f));
        DrawRectangleLines(212, y, 536, 28, index == draggedIndex ? ORANGE : isFunctionDirty ? MAROON : GRAY);
        DrawText(inspect->functions[index].type == RPG_INSPECT_MOVE ?
                 TextFormat("%02d  %s  (Move)", index + 1, inspect->functions[index].title) :
                 TextFormat("%02d  %s  (%d lines)", index + 1, inspect->functions[index].title,
                            inspect->functions[index].dialogue.lineCount), 226, y + 6, 17,
                 isFunctionDirty ? MAROON : DARKBLUE);
        if (deletedIndex >= 0) {
            DrawRectangle(662, y + 4, 82, 18, Fade(MAROON, 0.14f));
            DrawText(TextFormat("Deleted #%d", deletedIndex + 1), 665, y + 7, 10, Fade(MAROON, 0.65f));
        }
    }
    // 現在の同じインデックスが空の場合だけ、保存前に存在した削除Functionを表示する。
    for (int index = inspect->functionCount; index < savedInspect->functionCount; index++) {
        int y = 166 + index * 34;
        if (y >= 394) break;
        DrawRectangle(212, y, 536, 28, Fade(MAROON, 0.06f));
        DrawRectangleLines(212, y, 536, 28, Fade(MAROON, 0.32f));
        DrawText(TextFormat("%02d  Deleted", index + 1), 226, y + 6, 17, Fade(MAROON, 0.45f));
    }
    DrawRectangle(212, 394, 210, 30, PURPLE);
    DrawText("Add function", 260, 401, 17, RAYWHITE);
    DrawRectangle(730, 96, 24, 24, MAROON);
    DrawText("x", 735, 99, 18, RAYWHITE);
}

static void DrawFunctionTypeList(void)
{
    const Rectangle panel = { 300.0f, 150.0f, 360.0f, 200.0f };
    DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
    DrawRectangleRec(panel, Fade(RAYWHITE, 0.98f));
    DrawRectangleLinesEx(panel, 2.0f, DARKBLUE);
    DrawText("Add Function", 324, 172, 24, DARKBLUE);
    DrawText("Select a function type", 324, 204, 16, DARKGRAY);
    DrawRectangle(324, 236, 312, 36, PURPLE);
    DrawText("Dialogue", 344, 245, 20, RAYWHITE);
    DrawRectangle(324, 280, 312, 36, DARKBLUE);
    DrawText("Move", 344, 289, 20, RAYWHITE);
    DrawRectangle(620, 160, 22, 22, MAROON);
    DrawText("x", 625, 162, 17, RAYWHITE);
}

static void DrawMoveFunctionEditor(const RpgInspectMove *move, bool isPreviewPlaying, float previewStartX,
                                   EditorSaveState saveState, bool isMoveDirty)
{
    const char *targetName = move->target == RPG_INSPECT_MOVE_PLAYER ? "Player" :
                             move->target == RPG_INSPECT_MOVE_NPC ? "NPC" : "Zipper";
    const int x = (int)movePanelBounds.x, y = (int)movePanelBounds.y;
    DrawRectangleRec(movePanelBounds, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(movePanelBounds, 2.0f, PURPLE);
    DrawText("Move Function", x + 16, y + 16, 21, PURPLE);
    DrawRectangleRec(GetMovePanelControl(250, 10, 20, 20), MAROON);
    DrawText("x", x + 255, y + 12, 16, RAYWHITE);
    DrawText("Target", x + 16, y + 50, 17, DARKGRAY);
    DrawRectangleRec(GetMovePanelControl(16, 74, 76, 30), move->target == RPG_INSPECT_MOVE_PLAYER ?
                     (isMoveDirty ? MAROON : PURPLE) : GRAY);
    DrawText("Player", x + 22, y + 82, 15, RAYWHITE);
    DrawRectangleRec(GetMovePanelControl(100, 74, 76, 30), move->target == RPG_INSPECT_MOVE_NPC ?
                     (isMoveDirty ? MAROON : PURPLE) : GRAY);
    DrawText("NPC", x + 124, y + 82, 15, RAYWHITE);
    DrawRectangleRec(GetMovePanelControl(184, 74, 76, 30), move->target == RPG_INSPECT_MOVE_ZIPPER ?
                     (isMoveDirty ? MAROON : PURPLE) : GRAY);
    DrawText("Zipper", x + 190, y + 82, 15, RAYWHITE);
    DrawText("Destination", x + 16, y + 126, 17, DARKGRAY);
    DrawText(TextFormat("Map %d  X %.0f", (int)(move->destinationX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1, move->destinationX), x + 16, y + 150, 18, isMoveDirty ? MAROON : DARKBLUE);
    DrawText("Click outside this panel to set", x + 16, y + 184, 16, DARKGRAY);
    DrawText("Preview start", x + 16, y + 234, 17, DARKGRAY);
    DrawText(TextFormat("Map %d  X %.0f", (int)(previewStartX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1, previewStartX), x + 16, y + 258, 18, DARKBLUE);
    DrawText("Right-click outside: preview start", x + 16, y + 292, 15, DARKGRAY);
    DrawText("Duration", x + 16, y + 342, 17, DARKGRAY);
    DrawRectangleRec(GetMovePanelControl(16, 366, 38, 28), MAROON);
    DrawText("-", x + 29, y + 369, 21, RAYWHITE);
    DrawText(TextFormat("%.1f sec", move->duration), x + 66, y + 371, 19, isMoveDirty ? MAROON : DARKBLUE);
    DrawRectangleRec(GetMovePanelControl(152, 366, 38, 28), DARKGREEN);
    DrawText("+", x + 165, y + 369, 21, RAYWHITE);
    DrawRectangleRec(GetMovePanelControl(16, 402, 244, 22), isPreviewPlaying ? MAROON : DARKGREEN);
    DrawText(isPreviewPlaying ? "Stop preview" : TextFormat("Play preview: %s", targetName), x + 54, y + 404, 16, RAYWHITE);
    DrawSaveButton(GetMovePanelControl(16, 432, 116, 24), saveState);
    DrawRevertButton(GetMovePanelControl(144, 432, 116, 24));
}

static void DrawEditor(const RpgCharacter *player, const RpgCharacter *npc, const RpgStage *stage,
                       const RpgLayout *layout, const RpgStage3Event *stage3Event,
                       const RpgZipper *zipper,
                       Texture2D zipperTexture,
                       const RpgDialogue *dialogue, int selected, int mapIndex, bool blockMode,
                       int dialogueScroll, int activeDialogueLine, int dialogueCursorIndex,
                       int selectionAnchor, int selectionEnd, int draggedDialogueLine,
                       bool isDialogueEditorOpen, int dialogueBlockHeight,
                       int dialogueFontSize, bool isSpeakerEditing, bool isStage3DialogueEditing,
                       bool isInspectDialogueEditing, bool isMoveFunctionEditorOpen, bool isMovePreviewPlaying, float previewStartX, float movePreviewSpriteX, int inspectFunctionIndex, bool isExamineFunctionListOpen, bool isFunctionTypeListOpen, int draggedInspectFunction, bool isInspectTitleEditing, int titleCursorIndex, int speakerCursorIndex, int speakerSelectionAnchor, int speakerSelectionEnd,
                       const char *message, bool isExitConfirmationOpen, bool isExitDetailsOpen,
                       int detailScroll, const EditorSaveSnapshot *savedSnapshot)
{
    (void)layout;
    EditorSaveState saveState = GetSaveState(message);
    const RpgInspect *savedInspect = activeInspect == &npcInspectData ?
                                     &savedSnapshot->npcInspectSnapshot : &savedSnapshot->zipperInspectSnapshot;
    RpgDialogue emptySavedDialogue = { 0 };
    const RpgDialogue *savedEditedDialogue = isInspectDialogueEditing ?
        (inspectFunctionIndex < savedInspect->functionCount ?
         &savedInspect->functions[inspectFunctionIndex].dialogue : &emptySavedDialogue) :
        isStage3DialogueEditing ? &savedSnapshot->stage3Event.dialogue : &savedSnapshot->dialogue;
    BeginDrawing();
    ClearBackground((Color){ 135, 206, 235, 255 });
    DrawCircle(780, 95, 42, Fade(YELLOW, 0.9f));
    DrawEllipse(180, 105, 80, 20, Fade(RAYWHITE, 0.85f));
    DrawRectangle(0, 400, RPG_EDITOR_WIDTH, 140, (Color){ 103, 161, 70, 255 });
    RpgStage_DrawMap(stage, mapIndex, blockMode);
    DrawRectangle(0, 400, RPG_EDITOR_WIDTH, 14, DARKGREEN);
    if (isMoveFunctionEditorOpen) {
        const RpgInspectMove *move = &npcInspect.functions[inspectFunctionIndex].move;
        int destinationMap = (int)(move->destinationX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
        if (destinationMap == mapIndex) {
            int destinationX = (int)(move->destinationX - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
            DrawLine(destinationX, 304, destinationX, 400, ORANGE);
            DrawCircle(destinationX, 304, 10.0f, Fade(ORANGE, 0.85f));
            DrawText("Destination", destinationX - 38, 282, 14, MAROON);
        }
        int previewMap = (int)(previewStartX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
        if (previewMap == mapIndex) {
            int previewX = (int)(previewStartX - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
            DrawCircleLines(previewX, 330, 9.0f, SKYBLUE);
            DrawText("Preview start", previewX - 35, 342, 13, DARKBLUE);
        }
        if (isMovePreviewPlaying && (int)(movePreviewSpriteX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) == mapIndex) {
            int spriteX = (int)(movePreviewSpriteX - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
            DrawRectangle(spriteX - 10, 370, 20, 20, Fade(PURPLE, 0.85f));
            DrawRectangleLines(spriteX - 10, 370, 20, 20, RAYWHITE);
        }
    }
    if (mapIndex == 2) DrawEditorZipper(zipperTexture, zipper);
    if (isMovePreviewPlaying) DrawMovePreviewSprite(zipperTexture, player, npc, zipper,
                                                     npcInspect.functions[inspectFunctionIndex].move.target,
                                                     movePreviewSpriteX, mapIndex);
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
    bool hasUnsavedChanges = HasUnsavedChanges(savedSnapshot, player, npc, stage, dialogue, stage3Event);
    DrawRectangleRec(revertSavedBounds, hasUnsavedChanges ? MAROON : GRAY);
    DrawText("Revert saved", 426, 48, 16, RAYWHITE);
    DrawText(TextFormat("Map %d", mapIndex + 1), 24, 20, 20, MAROON);
    if (mapIndex == 2) {
        DrawStage3EventPanel(stage3Event);
    }
    DrawText(message, 580, 48, 18, saveState == EDITOR_SAVE_SUCCEEDED ? DARKGREEN : MAROON);
    bool isModalOpen = isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen ||
                       isMoveFunctionEditorOpen;
    if (!isModalOpen && selected == 1) {
        DrawRectangleRec(playerInspectorBounds, Fade(RAYWHITE, 0.92f));
        DrawRectangleLinesEx(playerInspectorBounds, 2.0f, DARKBLUE);
        DrawText("Player Inspector", 716, 92, 18, DARKBLUE);
        DrawRectangleRec(GetInspectorCloseButton(1), Fade(MAROON, 0.88f));
        DrawText("x", 899, 89, 16, RAYWHITE);
        DrawText(TextFormat("Move speed: %.0f", player->moveSpeed), 716, 120, 17,
                 player->moveSpeed != savedSnapshot->player.moveSpeed ? MAROON : DARKGRAY);
        DrawText("[-] 20", 716, 144, 16, MAROON);
        DrawText("[+] 20", 820, 144, 16, DARKGREEN);
        DrawText(TextFormat("Scale: %.1f", player->scale), 716, 166, 16,
                 player->scale != savedSnapshot->player.scale ? MAROON : DARKGRAY);
        DrawText("[-]", 812, 166, 16, MAROON);
        DrawText("[+]", 864, 166, 16, DARKGREEN);
        DrawSaveButton((Rectangle){ 716, 204, 90, 26 }, saveState);
        DrawRevertButton((Rectangle){ 814, 204, 90, 26 });
    } else if (!isModalOpen && selected == 2) {
        DrawNpcSummaryInspector(dialogue, npc, &savedSnapshot->npc, saveState);
    } else if (!isModalOpen && selected == 3) {
        DrawZipperInspector(zipper, &savedSnapshot->zipper, saveState);
    }
    if (isMoveFunctionEditorOpen) {
        DrawMoveFunctionEditor(&npcInspect.functions[inspectFunctionIndex].move, isMovePreviewPlaying, previewStartX,
                               saveState, inspectFunctionIndex >= savedInspect->functionCount ||
                               IsInspectFunctionDifferent(&npcInspect.functions[inspectFunctionIndex],
                                                          &savedInspect->functions[inspectFunctionIndex]));
    } else if (isFunctionTypeListOpen) {
        DrawFunctionTypeList();
    } else if (isExamineFunctionListOpen) {
        DrawExamineFunctionList(&npcInspect, inspectFunctionIndex, draggedInspectFunction, isInspectTitleEditing,
                                titleCursorIndex, savedInspect);
    } else if (isDialogueEditorOpen) {
        DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
        DrawNpcInspector(isInspectDialogueEditing ? &npcInspect.functions[inspectFunctionIndex].dialogue : isStage3DialogueEditing ? &stage3Event->dialogue : dialogue, dialogueScroll, activeDialogueLine, dialogueCursorIndex,
                         selectionAnchor, selectionEnd, draggedDialogueLine,
                         dialogueBlockHeight, dialogueFontSize, isSpeakerEditing,
                         speakerCursorIndex, speakerSelectionAnchor, speakerSelectionEnd,
                         false, saveState, savedEditedDialogue);
        if (isInspectDialogueEditing) {
            DrawRectangle(478, 64, 96, 22, DARKBLUE);
            DrawText("Back (Tab)", 483, 67, 16, RAYWHITE);
        }
    }
    if (isExitConfirmationOpen) DrawExitConfirmation(isExitDetailsOpen, savedSnapshot, player, npc, stage,
                                                      dialogue, stage3Event, detailScroll);
    EndDrawing();
}

int main(void)
{
    const Rectangle area = { 32.0f, 80.0f, 896.0f, 320.0f };
    InitWindow(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, "1_44MB - RPG Editor");
    SetExitKey(KEY_NULL);
    InstallEditorCloseHandler();
    SetTargetFPS(60);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));
    Texture2D zipperTexture = LoadTexture(TextFormat("%s../assets/Sprite/ZIPPER.png",
                                                      GetApplicationDirectory()));
    RpgLayout layout = RpgLayout_Default();
    RpgLayout_Load(TextFormat("%s../assets/Settings/rpg_layout.cfg", GetApplicationDirectory()), &layout);
    RpgStage stage = RpgStage_Default();
    RpgStage_Load(TextFormat("%s../assets/Settings/rpg_stage.cfg", GetApplicationDirectory()), &stage);
    RpgStage3Event stage3Event = RpgStage3Event_Default();
    RpgStage3Event_Load(TextFormat("%s../assets/Settings/rpg_stage3_event.cfg",
                                   GetApplicationDirectory()), &stage3Event);
    zipperData = RpgZipper_Default();
    RpgZipper_Load(TextFormat("%s../assets/Settings/rpg_zipper.cfg", GetApplicationDirectory()), &zipperData);
    npcInspect = RpgInspect_Default("Inspect", "Nothing unusual here.");
    RpgInspect_Load(TextFormat("%s../assets/Settings/rpg_inspect.cfg", GetApplicationDirectory()), &npcInspect);
    for (int functionIndex = 0; functionIndex < npcInspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < npcInspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    activeInspect = &zipperInspectData;
    RpgInspect_Load(TextFormat("%s../assets/Settings/rpg_zipper_inspect.cfg", GetApplicationDirectory()), &npcInspect);
    for (int functionIndex = 0; functionIndex < npcInspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < npcInspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    activeInspect = &npcInspectData;
    for (int lineIndex = 0; lineIndex < stage3Event.dialogue.lineCount; lineIndex++) {
        GameFont_AddText(stage3Event.dialogue.speakers[lineIndex]);
        GameFont_AddText(stage3Event.dialogue.lines[lineIndex]);
    }
    RpgDialogue dialogue = RpgDialogue_Default();
    RpgDialogue_Load(TextFormat("%s../assets/Settings/rpg_dialogue.txt",
                                GetApplicationDirectory()), &dialogue);
    for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
        GameFont_AddText(dialogue.lines[lineIndex]);
        GameFont_AddText(dialogue.speakers[lineIndex]);
    }
    RpgCharacter player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
    RpgCharacter npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
    player.position.y = 400.0f;
    player.moveSpeed = layout.playerMoveSpeed;
    player.scale = layout.playerScale;
    npc.position.y = 400.0f;
    npc.scale = layout.npcScale;
    int selected = 0;
    int mapIndex = 0;
    int dialogueScroll = 0;
    int activeDialogueLine = -1;
    int dialogueCursorIndex = 0;
    int selectionAnchor = 0;
    int selectionEnd = 0;
    int draggedDialogueLine = -1;
    bool isDialogueEditorOpen = false;
    bool isSpeakerEditing = false;
    int speakerCursorIndex = 0;
    int speakerSelectionAnchor = 0;
    int speakerSelectionEnd = 0;
    bool isStage3DialogueEditing = false;
    bool isInspectDialogueEditing = false;
    bool isExamineFunctionListOpen = false;
    bool isFunctionTypeListOpen = false;
    bool isMoveFunctionEditorOpen = false;
    bool isDraggingMovePanel = false;
    Vector2 movePanelDragOffset = { 0.0f, 0.0f };
    bool isMovePreviewPlaying = false;
    float movePreviewElapsed = 0.0f;
    float movePreviewStartX = 0.0f;
    float movePreviewSpriteX = 0.0f;
    bool isPreviewStartPicking = false;
    float previewStartX = 0.0f;
    int inspectFunctionIndex = 0;
    int draggedInspectFunction = -1;
    int lastInspectFunctionClick = -1;
    double lastInspectFunctionClickTime = -1.0;
    bool isInspectTitleEditing = false;
    int titleCursorIndex = 0;
    ModalHistory modalHistory = { 0 };
    int dialogueFontSize = 22;
    int dialogueBlockHeight = dialogueFontSize + 10;
    bool blockMode = false;
    const char *message = "Select a character";
    static EditorSaveSnapshot savedSnapshot;
    UpdateSaveSnapshot(&savedSnapshot, &player, &npc, &stage, &dialogue, &stage3Event);
    // 50件の編集状態は大きいため、起動スタックではなく静的領域に確保する。
    static RpgEditorHistory history;
    RpgEditorHistory_Load(TextFormat("%s../assets/Settings/rpg_editor_undo.dat", GetApplicationDirectory()), &history);
    BlockHistory blockHistory = { 0 };
    bool shouldExit = false;
    bool isExitConfirmationOpen = false;
    bool isExitDetailsOpen = false;
    int exitDetailsScroll = 0;

    while (!shouldExit) {
        static EditorSaveSnapshot frameStartState;
        UpdateSaveSnapshot(&frameStartState, &player, &npc, &stage, &dialogue, &stage3Event);
        CaptureInspectorUi(&frameStartState, mapIndex, selected, isDialogueEditorOpen,
                           isExamineFunctionListOpen, isFunctionTypeListOpen, isMoveFunctionEditorOpen,
                           isStage3DialogueEditing, isInspectDialogueEditing);
        bool didUndoThisFrame = false;
        bool blockEditedThisFrame = false;
        if (isEditorCloseRequested && !isExitConfirmationOpen) {
            // ネイティブの閉じる通知を保留し、未保存データがある時だけ確認画面へ移る。
            isEditorCloseRequested = false;
            if (HasUnsavedChanges(&savedSnapshot, &player, &npc, &stage, &dialogue, &stage3Event)) {
                isExitConfirmationOpen = true;
                isExitDetailsOpen = false;
                exitDetailsScroll = 0;
            } else shouldExit = true;
        }
        if (shouldExit) break;
        Vector2 mousePosition = GetMousePosition();
        bool wasExitConfirmationOpen = isExitConfirmationOpen;
        if (isExitConfirmationOpen && isExitDetailsOpen &&
            CheckCollisionPointRec(mousePosition, GetExitDetailsBounds())) {
            ExitDetailList details = BuildUnsavedDetails(&savedSnapshot, &player, &npc, &stage,
                                                          &dialogue, &stage3Event);
            int maxScroll = details.count > EXIT_DETAIL_VISIBLE_ROWS ? details.count - EXIT_DETAIL_VISIBLE_ROWS : 0;
            exitDetailsScroll -= (int)GetMouseWheelMove();
            if (exitDetailsScroll < 0) exitDetailsScroll = 0;
            if (exitDetailsScroll > maxScroll) exitDetailsScroll = maxScroll;
        }
        if (isExitConfirmationOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, GetExitDetailsToggle())) {
                isExitDetailsOpen = !isExitDetailsOpen;
                exitDetailsScroll = 0;
            } else if (CheckCollisionPointRec(mousePosition, GetExitConfirmationButton(0))) {
                if (SaveEditorAndUpdateSnapshot(&layout, &player, &npc, &stage, &dialogue, &stage3Event,
                                                &savedSnapshot)) shouldExit = true;
                else message = "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, GetExitConfirmationButton(1))) {
                shouldExit = true;
            } else if (CheckCollisionPointRec(mousePosition, GetExitConfirmationButton(2))) {
                isExitConfirmationOpen = false;
                isExitDetailsOpen = false;
                exitDetailsScroll = 0;
                message = "Exit cancelled";
            }
        }
        if (!wasExitConfirmationOpen && !isExitConfirmationOpen) {
        // 保存結果は、保存後の次の操作が始まった時点で通常のボタン表示へ戻す。
        if (GetSaveState(message) != EDITOR_SAVE_NONE &&
            (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
             GetKeyPressed() != KEY_NULL)) {
            message = "Editing";
        }
        // モーダルを閉じたクリックが、同じフレームの背面UIへ届かないように記憶する。
        bool wasModalOpenAtFrameStart = isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen;
        RpgDialogue *editedDialogue = isInspectDialogueEditing ? &npcInspect.functions[inspectFunctionIndex].dialogue :
                                      isStage3DialogueEditing ? &stage3Event.dialogue : &dialogue;
        int visibleDialogueLines = GetVisibleDialogueLines(dialogueBlockHeight);
        bool isDialogueEditing = isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen;
        if (isMovePreviewPlaying) {
            RpgInspectMove *previewMove = &npcInspect.functions[inspectFunctionIndex].move;
            movePreviewElapsed += GetFrameTime();
            float progress = Clamp(movePreviewElapsed / previewMove->duration, 0.0f, 1.0f);
            movePreviewSpriteX = movePreviewStartX + (previewMove->destinationX - movePreviewStartX) * progress;
            if (progress >= 1.0f) {
                isMovePreviewPlaying = false;
                message = "Move preview complete";
            }
        }
        // 日本語IMEのローマ字入力中は、文字キーのエディター操作を受け付けない。
        // マップを切り替える時は、前のマップの選択・入力状態を必ず解除する。
        if (!isDialogueEditing && IsKeyPressed(KEY_ONE)) {
            mapIndex = 0; selected = 0; activeDialogueLine = -1; isDialogueEditorOpen = false;
            isSpeakerEditing = false; isStage3DialogueEditing = false;
            isInspectDialogueEditing = false; isExamineFunctionListOpen = false; isFunctionTypeListOpen = false; isMoveFunctionEditorOpen = false; isMovePreviewPlaying = false;
        }
        if (!isDialogueEditing && IsKeyPressed(KEY_TWO)) {
            mapIndex = 1; selected = 0; activeDialogueLine = -1; isDialogueEditorOpen = false;
            isSpeakerEditing = false; isStage3DialogueEditing = false;
            isInspectDialogueEditing = false; isExamineFunctionListOpen = false; isFunctionTypeListOpen = false; isMoveFunctionEditorOpen = false; isMovePreviewPlaying = false;
        }
        if (!isDialogueEditing && IsKeyPressed(KEY_THREE)) {
            mapIndex = 2; selected = 0; activeDialogueLine = -1; isDialogueEditorOpen = false;
            isSpeakerEditing = false; isStage3DialogueEditing = false;
            isInspectDialogueEditing = false; isExamineFunctionListOpen = false; isFunctionTypeListOpen = false; isMoveFunctionEditorOpen = false; isMovePreviewPlaying = false;
        }
        if (!isDialogueEditing && IsKeyPressed(KEY_B)) { blockMode = !blockMode; selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; }
        if (!isDialogueEditing && IsKeyPressed(KEY_ESCAPE)) { selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; message = "Selection cleared"; }
        if (IsKeyPressed(KEY_Z) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
            if (blockMode) {
                if (UndoBlockChange(&blockHistory, &stage)) message = "Block undo";
                else message = "Nothing to undo";
            } else if (selected != 0 || mapIndex == 2 || isDialogueEditorOpen || isExamineFunctionListOpen ||
                       isFunctionTypeListOpen || isMoveFunctionEditorOpen) {
                if (UndoEditorChange(&history, &layout, &player, &npc, &stage, &dialogue, &stage3Event,
                                     &mapIndex, &selected, &isDialogueEditorOpen, &isExamineFunctionListOpen,
                                     &isFunctionTypeListOpen, &isMoveFunctionEditorOpen,
                                     &isStage3DialogueEditing, &isInspectDialogueEditing)) {
                isMovePreviewPlaying = false;
                activeDialogueLine = -1;
                isSpeakerEditing = false;
                message = "Undo";
                } else message = "Nothing to undo";
            }
            didUndoThisFrame = true;
        }
        if (isDialogueEditorOpen && activeDialogueLine >= 0) {
            UpdateImeCandidateWindow(activeDialogueLine, dialogueScroll);
        }

        bool isPlayerInspectorClicked = !wasModalOpenAtFrameStart && selected == 1 &&
                                        CheckCollisionPointRec(mousePosition, playerInspectorBounds);
        bool isNpcSummaryClicked = !wasModalOpenAtFrameStart && selected == 2 && !isDialogueEditorOpen && !isExamineFunctionListOpen &&
                                   CheckCollisionPointRec(mousePosition, npcInspectorBounds);
        bool isZipperInspectorClicked = !wasModalOpenAtFrameStart && selected == 3 &&
                                        CheckCollisionPointRec(mousePosition, zipperInspectorBounds);
        bool isDialogueEditorClicked = isDialogueEditorOpen &&
                                       CheckCollisionPointRec(mousePosition, dialogueEditorBounds);
        bool isInspectorClicked = isPlayerInspectorClicked || isNpcSummaryClicked ||
                                  isZipperInspectorClicked || isDialogueEditorClicked;
        bool isCloseInspectorClicked = (isPlayerInspectorClicked || isNpcSummaryClicked || isZipperInspectorClicked) &&
                                       CheckCollisionPointRec(mousePosition, GetInspectorCloseButton(selected));
        bool isCloseDialogueEditorClicked = isDialogueEditorClicked &&
                                            CheckCollisionPointRec(mousePosition, GetDialogueEditorCloseButton());
        bool isBackToExamineClicked = isDialogueEditorClicked && isInspectDialogueEditing &&
                                      CheckCollisionPointRec(mousePosition, (Rectangle){ 478, 64, 96, 22 });
        bool isStage3ToggleClicked = !wasModalOpenAtFrameStart && mapIndex == 2 && !isDialogueEditorOpen &&
                                     CheckCollisionPointRec(mousePosition, (Rectangle){ 832, 22, 100, 24 });
        bool isRevertSavedClicked = !wasModalOpenAtFrameStart &&
                                    CheckCollisionPointRec(mousePosition, revertSavedBounds);
        if (isRevertSavedClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            HasUnsavedChanges(&savedSnapshot, &player, &npc, &stage, &dialogue, &stage3Event)) {
            RevertToSavedSnapshot(&savedSnapshot, &layout, &player, &npc, &stage, &dialogue, &stage3Event);
            message = "Reverted to saved";
        }
        if (isStage3ToggleClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            stage3Event.enabled = !stage3Event.enabled;
            message = stage3Event.enabled ? "Stage 3 intro enabled" : "Stage 3 intro disabled";
        }
        if (!wasModalOpenAtFrameStart && mapIndex == 2 && !isDialogueEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 712, 92, 220, 32 })) {
            isDialogueEditorOpen = true;
            isStage3DialogueEditing = true;
            activeDialogueLine = -1;
            message = "Stage dialogue editor opened";
        }
        if (isDialogueEditorOpen && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int maxScroll = editedDialogue->lineCount > visibleDialogueLines ? editedDialogue->lineCount - visibleDialogueLines : 0;
            dialogueScroll -= (int)GetMouseWheelMove();
            if (dialogueScroll < 0) dialogueScroll = 0;
            if (dialogueScroll > maxScroll) dialogueScroll = maxScroll;
        }
        if (isMoveFunctionEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgInspectMove *move = &npcInspect.functions[inspectFunctionIndex].move;
            if (CheckCollisionPointRec(mousePosition, area) && !CheckCollisionPointRec(mousePosition, movePanelBounds)) {
                if (isPreviewStartPicking) {
                    previewStartX = mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
                    isPreviewStartPicking = false;
                    message = "Preview start set";
                } else {
                    move->destinationX = mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
                    message = "Move destination set";
                }
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(250, 10, 20, 20))) {
                if (isMovePreviewPlaying) {
                    isMovePreviewPlaying = false;
                }
                isMoveFunctionEditorOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 74, 76, 30))) move->target = RPG_INSPECT_MOVE_PLAYER;
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(100, 74, 76, 30))) move->target = RPG_INSPECT_MOVE_NPC;
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(184, 74, 76, 30))) move->target = RPG_INSPECT_MOVE_ZIPPER;
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 366, 38, 28))) move->duration = Clamp(move->duration - 0.1f, 0.1f, 30.0f);
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(152, 366, 38, 28))) move->duration = Clamp(move->duration + 0.1f, 0.1f, 30.0f);
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 402, 244, 22))) {
                if (isMovePreviewPlaying) {
                    isMovePreviewPlaying = false;
                    message = "Move preview stopped";
                } else {
                    movePreviewStartX = previewStartX;
                    movePreviewSpriteX = previewStartX;
                    movePreviewElapsed = 0.0f;
                    isMovePreviewPlaying = true;
                    message = "Move preview playing";
                }
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 432, 116, 24))) {
                message = SaveActiveInspect(&savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(144, 432, 116, 24))) {
                RevertActiveInspect(&savedSnapshot);
                isMovePreviewPlaying = false;
                isMoveFunctionEditorOpen = false;
                isExamineFunctionListOpen = true;
                if (inspectFunctionIndex >= npcInspect.functionCount) inspectFunctionIndex = npcInspect.functionCount - 1;
                message = "Reverted to saved";
            } else if (CheckCollisionPointRec(mousePosition, movePanelBounds)) {
                isDraggingMovePanel = true;
                movePanelDragOffset = (Vector2){ mousePosition.x - movePanelBounds.x, mousePosition.y - movePanelBounds.y };
            }
        } else if (isFunctionTypeListOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 620, 160, 22, 22 })) {
                isFunctionTypeListOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
                message = "Returned to Examine functions";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 324, 236, 312, 36 })) {
                if (npcInspect.functionCount < RPG_INSPECT_MAX_FUNCTIONS) {
                    RpgInspectFunction *newFunction = &npcInspect.functions[npcInspect.functionCount++];
                    memset(newFunction, 0, sizeof(*newFunction));
                    newFunction->type = RPG_INSPECT_DIALOGUE;
                    newFunction->dialogue.lineCount = 1;
                    strcpy(newFunction->dialogue.speakers[0], "Inspect");
                    snprintf(newFunction->title, RPG_INSPECT_TITLE_LENGTH,
                             "Dialogue%d", npcInspect.functionCount);
                    inspectFunctionIndex = npcInspect.functionCount - 1;
                    message = "Dialogue function added";
                } else message = "Dialogue function limit reached";
                isFunctionTypeListOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 324, 280, 312, 36 })) {
                int index = npcInspect.functionCount;
                if (index < RPG_INSPECT_MAX_FUNCTIONS) {
                    npcInspect.functionCount++;
                    npcInspect.functions[index].type = RPG_INSPECT_MOVE;
                    npcInspect.functions[index].move = (RpgInspectMove){ RPG_INSPECT_MOVE_PLAYER, 0.0f, 1.0f };
                    snprintf(npcInspect.functions[index].title, RPG_INSPECT_TITLE_LENGTH, "Move%d", index + 1);
                    inspectFunctionIndex = index;
                    message = "Move function added";
                } else message = "Function limit reached";
                isFunctionTypeListOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            }
        } else if (isExamineFunctionListOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 730, 96, 24, 24 })) {
                isExamineFunctionListOpen = false;
                isInspectTitleEditing = false;
                modalHistory.count = 0;
                message = "Examine function list closed";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 648, 100, 78, 24 })) {
                npcInspect.enabled = !npcInspect.enabled;
                message = npcInspect.enabled ? "Examine enabled" : "Examine disabled";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 260, 132, 368, 24 })) {
                isInspectTitleEditing = true;
                titleCursorIndex = GetCursorIndexAtX(npcInspect.functions[inspectFunctionIndex].title, mousePosition.x - 268.0f, 16.0f);
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 212, 394, 210, 30 })) {
                ModalHistory_Push(&modalHistory, EDITOR_MODAL_EXAMINE_LIST);
                isFunctionTypeListOpen = true;
                isExamineFunctionListOpen = false;
                isInspectTitleEditing = false;
                message = "Select a function type";
            } else if (mousePosition.y >= 166 && mousePosition.y < 166 + npcInspect.functionCount * 34 &&
                       mousePosition.x >= 212 && mousePosition.x < 748) {
                int clickedFunction = (int)((mousePosition.y - 166) / 34);
                if (lastInspectFunctionClick == clickedFunction &&
                    GetTime() - lastInspectFunctionClickTime <= 0.35) {
                    inspectFunctionIndex = clickedFunction;
                    ModalHistory_Push(&modalHistory, EDITOR_MODAL_EXAMINE_LIST);
                    isExamineFunctionListOpen = false;
                    isDialogueEditorOpen = npcInspect.functions[clickedFunction].type == RPG_INSPECT_DIALOGUE;
                    isMoveFunctionEditorOpen = npcInspect.functions[clickedFunction].type == RPG_INSPECT_MOVE;
                    isInspectDialogueEditing = isDialogueEditorOpen;
                    if (isMoveFunctionEditorOpen) {
                        RpgInspectMove *move = &npcInspect.functions[clickedFunction].move;
                        previewStartX = move->target == RPG_INSPECT_MOVE_PLAYER ? player.position.x :
                                        move->target == RPG_INSPECT_MOVE_NPC ? npc.position.x :
                                        zipperData.character.position.x;
                        isPreviewStartPicking = false;
                    }
                    isStage3DialogueEditing = false;
                    activeDialogueLine = -1;
                    dialogueScroll = 0;
                    lastInspectFunctionClick = -1;
                    message = "Examine function editor opened";
                } else {
                    draggedInspectFunction = clickedFunction;
                    inspectFunctionIndex = clickedFunction;
                    lastInspectFunctionClick = clickedFunction;
                    lastInspectFunctionClickTime = GetTime();
                    message = "Dialogue function selected";
                }
                isInspectTitleEditing = false;
            } else isInspectTitleEditing = false;
        }
        if (isMoveFunctionEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            CheckCollisionPointRec(mousePosition, area) && !CheckCollisionPointRec(mousePosition, movePanelBounds)) {
            previewStartX = mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            isPreviewStartPicking = false;
            message = "Preview start set";
        }
        if (isDraggingMovePanel && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            movePanelBounds.x = Clamp(mousePosition.x - movePanelDragOffset.x, 0.0f, RPG_EDITOR_WIDTH - movePanelBounds.width);
            movePanelBounds.y = Clamp(mousePosition.y - movePanelDragOffset.y, 0.0f, RPG_EDITOR_HEIGHT - movePanelBounds.height);
        }
        if (isDraggingMovePanel && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) isDraggingMovePanel = false;
        if (isExamineFunctionListOpen && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            mousePosition.y >= 166 && mousePosition.y < 166 + npcInspect.functionCount * 34 &&
            mousePosition.x >= 212 && mousePosition.x < 748) {
            int removeIndex = (int)((mousePosition.y - 166) / 34);
            if (npcInspect.functionCount > 1) {
                for (int i = removeIndex; i < npcInspect.functionCount - 1; i++)
                    npcInspect.functions[i] = npcInspect.functions[i + 1];
                npcInspect.functionCount--;
                if (inspectFunctionIndex >= npcInspect.functionCount) inspectFunctionIndex = npcInspect.functionCount - 1;
                message = "Dialogue function deleted";
            } else message = "Keep at least one dialogue function";
        }
        if (isExamineFunctionListOpen && draggedInspectFunction >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            int destination = (int)((mousePosition.y - 166) / 34);
            if (destination < 0) destination = 0;
            if (destination >= npcInspect.functionCount) destination = npcInspect.functionCount - 1;
            if (destination != draggedInspectFunction) {
                RpgInspectFunction movedFunction = npcInspect.functions[draggedInspectFunction];
                if (destination > draggedInspectFunction) {
                    for (int i = draggedInspectFunction; i < destination; i++) {
                        npcInspect.functions[i] = npcInspect.functions[i + 1];
                    }
                } else {
                    for (int i = draggedInspectFunction; i > destination; i--) {
                        npcInspect.functions[i] = npcInspect.functions[i - 1];
                    }
                }
                npcInspect.functions[destination] = movedFunction;
                inspectFunctionIndex = destination;
                message = "Function reordered";
            }
            draggedInspectFunction = -1;
        }
        if (isExamineFunctionListOpen && isInspectTitleEditing) {
            UpdateShortText(npcInspect.functions[inspectFunctionIndex].title, RPG_INSPECT_TITLE_LENGTH, &titleCursorIndex);
            GameFont_AddText(npcInspect.functions[inspectFunctionIndex].title);
        }
        if ((isCloseDialogueEditorClicked || isBackToExamineClicked) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            EditorModal previousModal = ModalHistory_Pop(&modalHistory);
            bool returnToExamineList = previousModal == EDITOR_MODAL_EXAMINE_LIST;
            isDialogueEditorOpen = false;
            activeDialogueLine = -1;
            isSpeakerEditing = false;
            isStage3DialogueEditing = false;
            isInspectDialogueEditing = false;
            isExamineFunctionListOpen = returnToExamineList;
            draggedDialogueLine = -1;
            message = returnToExamineList ? "Returned to Examine functions" : "Dialogue editor closed";
        } else if (isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            selected = 0;
            activeDialogueLine = -1;
            draggedDialogueLine = -1;
            message = "Inspector closed";
        } else if (isPlayerInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 708, 138, 86, 28 })) player.moveSpeed -= 20.0f;
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 810, 138, 86, 28 })) player.moveSpeed += 20.0f;
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 800, 158, 48, 26 })) player.scale -= 0.1f;
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 852, 158, 48, 26 })) player.scale += 0.1f;
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 204, 90, 26 })) {
                message = SaveCharacterSettings(&layout, &player, true, &savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 814, 204, 90, 26 })) {
                RevertCharacterSettings(&savedSnapshot, &layout, &player, true);
                message = "Reverted to saved";
            }
            if (player.moveSpeed < 60.0f) player.moveSpeed = 60.0f;
            if (player.moveSpeed > 480.0f) player.moveSpeed = 480.0f;
            player.scale = Clamp(player.scale, 0.5f, 3.0f);
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 170, 188, 32 })) {
            isDialogueEditorOpen = true;
            activeDialogueLine = -1;
            isSpeakerEditing = false;
            message = "Dialogue editor opened";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 208, 188, 32 })) {
            activeInspect = &npcInspectData;
            modalHistory.count = 0;
            isExamineFunctionListOpen = true;
            isFunctionTypeListOpen = false;
            isMoveFunctionEditorOpen = false;
            isDialogueEditorOpen = false; isInspectDialogueEditing = false;
            isStage3DialogueEditing = false; activeDialogueLine = -1;
            isInspectTitleEditing = false;
            message = "Examine functions opened";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 246, 90, 26 })) {
            message = SaveCharacterSettings(&layout, &npc, false, &savedSnapshot) ? "Saved" : "Save failed";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 814, 246, 90, 26 })) {
            RevertCharacterSettings(&savedSnapshot, &layout, &npc, false);
            message = "Reverted to saved";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 800, 136, 48, 26 })) npc.scale = Clamp(npc.scale - 0.1f, 0.5f, 3.0f);
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 852, 136, 48, 26 })) npc.scale = Clamp(npc.scale + 0.1f, 0.5f, 3.0f);
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 800, 262, 48, 26 })) {
            zipperData.character.scale = Clamp(zipperData.character.scale - 0.1f, 0.5f, 3.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 852, 262, 48, 26 })) {
            zipperData.character.scale = Clamp(zipperData.character.scale + 0.1f, 0.5f, 3.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 290, 188, 28 })) {
            activeInspect = &zipperInspectData;
            modalHistory.count = 0;
            isExamineFunctionListOpen = true;
            isFunctionTypeListOpen = false;
            isMoveFunctionEditorOpen = false;
            isDialogueEditorOpen = false;
            isInspectDialogueEditing = false;
            isStage3DialogueEditing = false;
            activeDialogueLine = -1;
            isInspectTitleEditing = false;
            message = "Zipper examine functions opened";
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 716, 326, 90, 26 })) {
            message = SaveZipperSettings(&savedSnapshot) ? "Saved" : "Save failed";
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 814, 326, 90, 26 })) {
            zipperData.character = savedSnapshot.zipper.character;
            message = "Reverted to saved";
        }
        if (!isCloseDialogueEditorClicked && isDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 224, 96, 260, 26 }) &&
                activeDialogueLine >= 0) {
                isSpeakerEditing = true;
                speakerCursorIndex = GetCursorIndexAtX(editedDialogue->speakers[activeDialogueLine],
                                                       mousePosition.x - 232.0f, 17.0f);
                speakerSelectionAnchor = speakerCursorIndex;
                speakerSelectionEnd = speakerCursorIndex;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 680, 64, 42, 22 })) {
                isSpeakerEditing = false;
                if (dialogueFontSize > 12) dialogueFontSize--;
                dialogueBlockHeight = dialogueFontSize + 10;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 730, 64, 42, 22 })) {
                if (dialogueFontSize < 32) dialogueFontSize++;
                dialogueBlockHeight = dialogueFontSize + 10;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
                isSpeakerEditing = false;
                int line = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
                activeDialogueLine = line < editedDialogue->lineCount ? line : -1;
                if (activeDialogueLine >= 0) {
                    dialogueCursorIndex = GetCursorIndexAtX(editedDialogue->lines[activeDialogueLine],
                                                            mousePosition.x - (isInspectDialogueEditing ? 280.0f : 200.0f), (float)dialogueFontSize);
                    selectionAnchor = dialogueCursorIndex;
                    selectionEnd = dialogueCursorIndex;
                }
                draggedDialogueLine = activeDialogueLine;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 156, 414, 210, 32 })) {
                if (RpgDialogue_AddLine(editedDialogue)) {
                    activeDialogueLine = editedDialogue->lineCount - 1;
                    dialogueCursorIndex = 0;
                    selectionAnchor = 0;
                    selectionEnd = 0;
                    if (activeDialogueLine >= visibleDialogueLines) dialogueScroll = activeDialogueLine - visibleDialogueLines + 1;
                    message = "Dialogue line added";
                } else message = "Dialogue line limit reached";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 382, 414, 116, 32 })) {
                message = SaveEditedDialogue(isInspectDialogueEditing, isStage3DialogueEditing,
                                              &dialogue, &stage3Event, &savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 510, 414, 116, 32 })) {
                RevertEditedDialogue(isInspectDialogueEditing, isStage3DialogueEditing,
                                     &dialogue, &stage3Event, &savedSnapshot);
                activeDialogueLine = -1;
                isSpeakerEditing = false;
                message = "Reverted to saved";
            }
        }
        if (isDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int line = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
            if (RpgDialogue_DeleteLine(editedDialogue, line)) {
                activeDialogueLine = -1;
                message = "Dialogue line deleted";
            } else message = "Keep at least one dialogue line";
        }
        if (isSpeakerEditing && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 224, 96, 260, 26 }) &&
            activeDialogueLine >= 0) {
            speakerSelectionEnd = GetCursorIndexAtX(editedDialogue->speakers[activeDialogueLine],
                                                     mousePosition.x - 232.0f, 17.0f);
            speakerCursorIndex = speakerSelectionEnd;
        }
        if (draggedDialogueLine >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            isDialogueEditorClicked && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int hoveredLine = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
            if (hoveredLine == draggedDialogueLine) {
                selectionEnd = GetCursorIndexAtX(editedDialogue->lines[activeDialogueLine], mousePosition.x - (isInspectDialogueEditing ? 280.0f : 200.0f),
                                                  (float)dialogueFontSize);
                dialogueCursorIndex = selectionEnd;
            }
        }
        if (draggedDialogueLine >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (isDialogueEditorClicked && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
                int destination = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
                if (destination >= editedDialogue->lineCount) destination = editedDialogue->lineCount - 1;
                if (destination != draggedDialogueLine &&
                    RpgDialogue_MoveLine(editedDialogue, draggedDialogueLine, destination)) {
                    activeDialogueLine = destination;
                    dialogueCursorIndex = (int)strlen(editedDialogue->lines[destination]);
                    selectionAnchor = dialogueCursorIndex;
                    selectionEnd = dialogueCursorIndex;
                    message = "Dialogue line moved";
                }
            }
            draggedDialogueLine = -1;
        }
        if (isDialogueEditorOpen) {
            if (isSpeakerEditing) UpdateSpeakerText(editedDialogue, activeDialogueLine,
                                                    &speakerCursorIndex, &speakerSelectionAnchor,
                                                    &speakerSelectionEnd);
            else UpdateDialogueText(editedDialogue, activeDialogueLine, &dialogueCursorIndex,
                                    &selectionAnchor, &selectionEnd);
        }
        if (IsKeyPressed(KEY_TAB) && (isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen)) {
            isPreviewStartPicking = false;
            if (isMovePreviewPlaying) {
                isMovePreviewPlaying = false;
            }
            EditorModal previousModal = ModalHistory_Pop(&modalHistory);
            isDialogueEditorOpen = false;
            isMoveFunctionEditorOpen = false;
            isExamineFunctionListOpen = previousModal == EDITOR_MODAL_EXAMINE_LIST;
            isFunctionTypeListOpen = previousModal == EDITOR_MODAL_FUNCTION_TYPE_LIST;
            isInspectDialogueEditing = false;
            isStage3DialogueEditing = false;
            activeDialogueLine = -1;
            isSpeakerEditing = false;
            message = (isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen) ? "Returned to previous modal" : "Modal closed";
        }
        int largestWrappedLineCount = 1;
        for (int lineIndex = 0; lineIndex < editedDialogue->lineCount; lineIndex++) {
            int wrappedLineCount = GetWrappedLineCount(editedDialogue->lines[lineIndex], (float)dialogueFontSize);
            if (wrappedLineCount > largestWrappedLineCount) largestWrappedLineCount = wrappedLineCount;
        }
        dialogueBlockHeight = largestWrappedLineCount * (dialogueFontSize + 2) + 8;

        bool isUiBlockingMap = wasModalOpenAtFrameStart || isInspectorClicked || isDialogueEditorOpen ||
                               isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen ||
                               isStage3ToggleClicked || isRevertSavedClicked;
        if (blockMode && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            mousePosition.x += mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            int column = (int)(mousePosition.x / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
                if (stage.blocks[row][column] != 1) {
                    PushBlockHistory(&blockHistory, row, column, stage.blocks[row][column]);
                    RpgStage_SetBlockAtPosition(&stage, mousePosition, true);
                    blockEditedThisFrame = true;
                }
            }
        } else if (blockMode && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            mousePosition.x += mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            int column = (int)(mousePosition.x / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
                if (stage.blocks[row][column] != 0) {
                    PushBlockHistory(&blockHistory, row, column, stage.blocks[row][column]);
                    RpgStage_SetBlockAtPosition(&stage, mousePosition, false);
                    blockEditedThisFrame = true;
                }
            }
        } else if (!isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgCharacter localPlayer = GetLocalCharacter(&player, mapIndex);
            RpgCharacter localNpc = GetLocalCharacter(&npc, mapIndex);
            if (IsCharacterInMap(&player, mapIndex) && IsCharacterClicked(&localPlayer, mousePosition)) {
                selected = 1; activeDialogueLine = -1; message = "Hero selected";
            } else if (IsCharacterInMap(&npc, mapIndex) && IsCharacterClicked(&localNpc, mousePosition)) {
                selected = 2; activeDialogueLine = -1; message = "NPC selected";
            } else if (mapIndex == 2 && CheckCollisionPointRec(mousePosition,
                       (Rectangle){ zipperData.character.position.x - 2.0f * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE - 24.0f * zipperData.character.scale,
                                    380.0f - 60.0f * zipperData.character.scale,
                                    48.0f * zipperData.character.scale, 60.0f * zipperData.character.scale })) {
                selected = 3; activeDialogueLine = -1; message = "Zipper selected";
            } else if (selected != 0 && CheckCollisionPointRec(mousePosition, area)) {
                float worldX = mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
                if (selected == 1) player.position.x = worldX;
                else if (selected == 2) npc.position.x = worldX;
                else if (selected == 3 && mapIndex == 2) zipperData.character.position.x = worldX;
                message = "Position updated";
            }
        }
        if (IsKeyPressed(KEY_S) && activeDialogueLine < 0) {
            message = SaveEditorAndUpdateSnapshot(&layout, &player, &npc, &stage, &dialogue, &stage3Event, &savedSnapshot) ? "Saved" : "Save failed";
        }
        if (!didUndoThisFrame && !blockEditedThisFrame)
            RecordEditorHistory(&history, &frameStartState, &player, &npc, &stage, &dialogue, &stage3Event,
                                mapIndex, selected, isDialogueEditorOpen, isExamineFunctionListOpen,
                                isFunctionTypeListOpen, isMoveFunctionEditorOpen,
                                isStage3DialogueEditing, isInspectDialogueEditing);
        }
        DrawEditor(&player, &npc, &stage, &layout, &stage3Event, &zipperData, zipperTexture, &dialogue, selected, mapIndex, blockMode,
                   dialogueScroll, activeDialogueLine, dialogueCursorIndex, selectionAnchor,
                   selectionEnd, draggedDialogueLine, isDialogueEditorOpen, dialogueBlockHeight,
                   dialogueFontSize, isSpeakerEditing, isStage3DialogueEditing, isInspectDialogueEditing, isMoveFunctionEditorOpen, isMovePreviewPlaying, previewStartX, movePreviewSpriteX,
                   inspectFunctionIndex, isExamineFunctionListOpen, isFunctionTypeListOpen, draggedInspectFunction, isInspectTitleEditing, titleCursorIndex,
                   speakerCursorIndex, speakerSelectionAnchor, speakerSelectionEnd, message, isExitConfirmationOpen,
                   isExitDetailsOpen,
                   exitDetailsScroll, &savedSnapshot);
    }
    RpgEditorHistory_Save(TextFormat("%s../assets/Settings/rpg_editor_undo.dat", GetApplicationDirectory()), &history);
    UnloadTexture(zipperTexture);
    GameFont_Unload();
    RestoreEditorCloseHandler();
    CloseWindow();
    return 0;
}
