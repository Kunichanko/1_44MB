// 依存する自プロジェクト内ファイル: ../Editor/file_dialog.h, rpg_attachment.h, rpg_block_inventory.h, rpg_character.h, rpg_data_shot.h, rpg_dialogue.h, rpg_item.h, rpg_layout.h, rpg_map_event.h, rpg_receiver.h, rpg_stage.h, rpg_wire.h, game_font.h
// 依存関係を更新: rpg_stage3_event.h を追加した。
// 依存関係を更新: PC上のテキストファイル選択を再利用するため ../Editor/file_dialog.h を追加した。
// 依存関係を更新: 設置物・キャラ・FILE.png の共通ドラッグ状態に rpg_editor_drag.h を追加した。
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
#include <stdlib.h>

#include "game_font.h"
#include "../Editor/file_dialog.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_editor_text.h"
#include "rpg_editor_drag.h"
#include "rpg_layout.h"
#include "rpg_inspect.h"
#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_preview_event.h"
#include "rpg_preview_system.h"
#include "rpg_block_inventory.h"
#include "rpg_item.h"
#include "rpg_map_event.h"
#include "rpg_object_folder.h"
#include "rpg_receiver.h"
#include "rpg_stage3_event.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_wire.h"
#include "rpg_zipper.h"

// 既存の編集 UI から共通テキストモジュールへ移行するための互換名。
#define GetCursorIndexAtX RpgEditorText_GetCursorIndexAtX
#define GetWrappedLineCount(text, fontSize) RpgEditorText_GetWrappedLineCount((text), (fontSize), 590.0f)
#define DrawTextCaret RpgEditorText_DrawCaret

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

// 保存済み状態との比較・Revert専用のスナップショット。履歴には保持しない。
typedef struct EditorSaveSnapshot {
    RpgLayout layout;
    RpgCharacter player;
    RpgCharacter npc;
    RpgStage stage;
    RpgDialogue dialogue;
    RpgStage3Event stage3Event;
    RpgZipper zipper;
    RpgInspect npcInspectSnapshot;
    RpgInspect zipperInspectSnapshot;
    RpgMapEvents mapEvents;
    RpgWires wires;
    RpgReceivers receivers;
    RpgAttachments attachments;
    int mapIndex;
    int selected;
    int activeInspectKind;
    bool isDialogueEditorOpen;
    bool isExamineFunctionListOpen;
    bool isFunctionTypeListOpen;
    bool isMoveFunctionEditorOpen;
    bool isStage3DialogueEditing;
    bool isInspectDialogueEditing;
} EditorSaveSnapshot;

typedef enum BlockHistoryKind {
    BLOCK_HISTORY_CELL_CHANGE,
    BLOCK_HISTORY_ITEM_ADDED,
    BLOCK_HISTORY_WIRE_ADDED,
    BLOCK_HISTORY_WIRE_CHANGED,
    BLOCK_HISTORY_RECEIVER_ADDED,
    BLOCK_HISTORY_RECEIVER_CHANGED,
    BLOCK_HISTORY_ATTACHMENT_ADDED,
    BLOCK_HISTORY_ATTACHMENT_CHANGED,
    BLOCK_HISTORY_ATTACHMENT_REMOVED
} BlockHistoryKind;

typedef struct BlockHistoryEntry {
    BlockHistoryKind kind;
    int row;
    int column;
    int previousValue;
    bool restoresItem;
    RpgItem item;
    int propertyIndex;
    RpgWire wire;
    RpgReceiver receiver;
    RpgAttachment attachment;
    int receiverWireCount;
    int receiverWireIndices[RPG_WIRE_MAX_COUNT];
    RpgGridSide receiverWireSides[RPG_WIRE_MAX_COUNT];
} BlockHistoryEntry;

typedef struct BlockHistory {
    // Ctrl+Zの履歴は廃止済み。既存の配置処理の引数互換だけを維持する。
    int count;
    BlockHistoryEntry entries[1];
} BlockHistory;

#if defined(__GNUC__)
#define RPG_UNUSED __attribute__((unused))
#else
#define RPG_UNUSED
#endif

typedef struct BlockPropertyPlacementContext {
    RpgStage *stage;
    RpgItems *items;
    RpgWires *wires;
    RpgReceivers *receivers;
    BlockHistory *history;
} BlockPropertyPlacementContext;
typedef bool (*BlockPropertyPlacementFunction)(BlockPropertyPlacementContext *context,
                                               RpgGridCell cell, const char **message);
typedef struct BlockPropertyPlacementDefinition {
    int blockType;
    BlockPropertyPlacementFunction place;
} BlockPropertyPlacementDefinition;

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
static const Rectangle zipperInspectorBounds = { 700.0f, 218.0f, 220.0f, 208.0f };
static const Rectangle doorInspectorBounds = { 700.0f, 80.0f, 220.0f, 220.0f };
static const Rectangle referenceInspectorBounds = { 700.0f, 80.0f, 220.0f, 176.0f };
// 各サイドインスペクターの表示位置。内容は共通の座標系で描画して移動量だけを加える。
static Vector2 inspectorOffsets[8];
static Rectangle movePanelBounds = { 654.0f, 56.0f, 282.0f, 466.0f };
static const Rectangle exitConfirmationBounds = { 250.0f, 120.0f, 460.0f, 300.0f };
static const Rectangle revertSavedBounds = { 408.0f, 42.0f, 160.0f, 28.0f };
static const Rectangle globalSettingsButtonBounds = { 12.0f, 10.0f, 96.0f, 26.0f };
static const Rectangle globalSettingsPanelBounds = { 12.0f, 76.0f, 270.0f, 120.0f };
static const Rectangle areaInspectorButtonBounds = { 112.0f, 10.0f, 82.0f, 26.0f };
static const Rectangle areaInspectorPanelBounds = { 112.0f, 42.0f, 230.0f, 108.0f };
static RpgInspect npcInspectData;
static RpgZipper zipperData;
static RpgMapEvents mapEvents;
static RpgMapEvents savedMapEvents;
static RpgWires wires;
static RpgWires savedWires;
static RpgReceivers receivers;
static RpgReceivers savedReceivers;
static RpgAttachments attachments;
static RpgAttachments savedAttachments;
static RpgDataShots attachmentPreviewShots;
// 機能に依存しない一回限りプレビュー通知。将来のプレビュー対象も同じ通知を購読できる。
static RpgPreviewEvent previewEvent;
static RpgPreviewSystem previewSystem;
static RpgSignalBlocks signalBlocks;
static RpgSignalBlocks savedSignalBlocks;
static RpgStage *previewStage;
// 数値欄の編集中だけ入力を受け、ほかのエディター操作へ伝搬させない。
static bool isAttachmentCapacityEditing;
static char attachmentCapacityInput[24];
static bool isAttachmentSpeedEditing;
static char attachmentSpeedInput[24];

// 共通プレビュー通知を受け、電波装置は見た目専用のデータ弾だけを生成する。
static void PreviewRadioEmitters(void *context, const RpgPreviewEvent *event)
{
    (void)context;
    RpgDataShots_TriggerPreview(&attachmentPreviewShots, &attachments, event->target);
}

// 伸縮ブロックも同じ通知を購読し、プレビュー中も実際の縮小形状と当たり判定を再現する。
static void PreviewSignalShrinkBlocks(void *context, const RpgPreviewEvent *event)
{
    RpgSignalBlocks_Preview((RpgSignalBlocks *)context, previewStage, event);
}
static bool isAttachmentDragPreviewVisible;
static bool isAttachmentDragPreviewSnapped;
static bool isAttachmentPathDragVisualActive;
static int attachmentDragDrawSkipIndex = -1;
static RpgAttachment attachmentDragPreview;
static Vector2 attachmentDragPointer;
// 特殊ブロックは実体を離すまで動かさず、同じドラッグ操作中にゴーストだけをカーソルへ追従させる。
static bool isEffectBlockDragPreviewVisible;
static Vector2 effectBlockDragPointer;
static int effectBlockDragPreviewRow = -1;
static int effectBlockDragPreviewColumn = -1;
// FILE.png も他の設置物と同じく、実体を離すまで動かさずゴーストだけを追従させる。
static bool isReferenceDragPreviewVisible;
static Vector2 referenceDragPointer;
static bool isZipperLaunchPreviewVisible;
static bool isZipperLaunchPreviewReturning;
static Vector2 zipperLaunchPreviewPosition;
static float zipperLaunchPreviewCooldown;
#define zipperInspectData (zipperData.inspect)
static RpgInspect *activeInspect = &npcInspectData;

static bool AreItemsDifferent(const RpgItems *first, const RpgItems *second);
static bool AreWiresDifferent(const RpgWires *first, const RpgWires *second);
static bool AreReceiversDifferent(const RpgReceivers *first, const RpgReceivers *second);
static bool AreAttachmentsDifferent(const RpgAttachments *first, const RpgAttachments *second);
static bool AreSignalBlockSettingsDifferent(const RpgSignalBlocks *first, const RpgSignalBlocks *second);
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
    return selected == 4 || selected == 5 || selected == 7 ? (Rectangle){ 894.0f, 88.0f, 18.0f, 18.0f } : selected == 3 ? (Rectangle){ 894.0f, 226.0f, 18.0f, 18.0f } :
                           (Rectangle){ 894.0f, 88.0f, 18.0f, 18.0f };
}

static Rectangle GetInspectorBounds(int selected)
{
    if (selected == 1) return playerInspectorBounds;
    if (selected == 2) return npcInspectorBounds;
    if (selected == 3) return zipperInspectorBounds;
    if (selected == 5) return doorInspectorBounds;
    if (selected == 7) return referenceInspectorBounds;
    if (selected == 6) return (Rectangle){ 700.0f, 80.0f, 220.0f, 350.0f };
    return selected == 4 ? (Rectangle){ 700.0f, 80.0f, 220.0f, 150.0f } :
                           (Rectangle){ 700.0f, 80.0f, 220.0f, 220.0f };
}

static Rectangle GetInspectorScreenBounds(int selected)
{
    Rectangle bounds = GetInspectorBounds(selected);
    bounds.x += inspectorOffsets[selected].x;
    bounds.y += inspectorOffsets[selected].y;
    return bounds;
}

static Vector2 GetInspectorLocalPointer(Vector2 screenPointer, int selected)
{
    return Vector2Subtract(screenPointer, inspectorOffsets[selected]);
}

static bool IsInspectorControlPoint(int selected, Vector2 point)
{
    // 内容を操作する領域と閉じるボタンでは、パネル移動を始めない。
    if (CheckCollisionPointRec(point, GetInspectorCloseButton(selected))) return true;
    if (selected == 1) return CheckCollisionPointRec(point, (Rectangle){ 708, 138, 188, 28 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 800, 158, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 204, 188, 26 });
    if (selected == 2) return CheckCollisionPointRec(point, (Rectangle){ 800, 136, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 170, 188, 102 });
    if (selected == 3) return CheckCollisionPointRec(point, (Rectangle){ 800, 262, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 800, 284, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 800, 306, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 330, 188, 28 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 358, 188, 62 });
    if (selected == 4) return CheckCollisionPointRec(point, (Rectangle){ 716, 144, 188, 28 });
    if (selected == 5) return CheckCollisionPointRec(point, (Rectangle){ 716, 144, 188, 112 });
    if (selected == 7) return CheckCollisionPointRec(point, (Rectangle){ 716, 144, 188, 28 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 180, 188, 28 });
    if (selected == 6) return CheckCollisionPointRec(point, (Rectangle){ 808, 114, 96, 52 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 206, 188, 28 });
    return false;
}

// キャラクター・アイテム共通のインスペクター枠と閉じるボタンを描画する。
static void DrawInspectorFrame(Rectangle bounds, const char *title, Color accent, Rectangle closeButton)
{
    DrawRectangleRec(bounds, Fade(RAYWHITE, 0.94f));
    DrawRectangleLinesEx(bounds, 2.0f, accent);
    DrawText(title, (int)bounds.x + 16, (int)bounds.y + 12, 18, accent);
    DrawRectangleRec(closeButton, Fade(MAROON, 0.88f));
    DrawText("x", (int)closeButton.x + 5, (int)closeButton.y + 1, 16, RAYWHITE);
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
                                          const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                          const RpgItems *items, const RpgItems *savedItems)
{
    ExitDetailList details = { 0 };
    if (snapshot->player.position.x != player->position.x || snapshot->player.moveSpeed != player->moveSpeed ||
        snapshot->player.scale != player->scale) AddExitDetail(&details, "- Player");
    if (snapshot->npc.position.x != npc->position.x || snapshot->npc.scale != npc->scale) {
        AddExitDetail(&details, "- NPC");
    }
    if (snapshot->zipper.character.position.x != zipperData.character.position.x ||
        snapshot->zipper.character.scale != zipperData.character.scale ||
        snapshot->zipper.launchSpeed != zipperData.launchSpeed ||
        snapshot->zipper.returnSpeed != zipperData.returnSpeed ||
        snapshot->zipper.launchPreviewEnabled != zipperData.launchPreviewEnabled) {
        AddExitDetail(&details, "- Zipper");
    }
    if (memcmp(&snapshot->stage, stage, sizeof(*stage)) != 0) {
        AddExitDetail(&details, "- Stage blocks");
    }
    if (IsDialogueDifferent(&snapshot->dialogue, dialogue)) AddExitDetail(&details, "- NPC dialogue");
    if (snapshot->stage3Event.enabled != stage3Event->enabled ||
        IsDialogueDifferent(&snapshot->stage3Event.dialogue, &stage3Event->dialogue)) {
        AddExitDetail(&details, "- Stage 3 event");
    }
    AddInspectDetails(&details, "NPC", &npcInspectData, &snapshot->npcInspectSnapshot);
    AddInspectDetails(&details, "Zipper", &zipperInspectData, &snapshot->zipperInspectSnapshot);
    if (AreItemsDifferent(items, savedItems)) AddExitDetail(&details, "- Item properties");
    if (AreWiresDifferent(&wires, &savedWires)) AddExitDetail(&details, "- Wires");
    if (AreReceiversDifferent(&receivers, &savedReceivers)) AddExitDetail(&details, "- Receivers");
    if (AreAttachmentsDifferent(&attachments, &savedAttachments)) AddExitDetail(&details, "- Attachments");
    if (AreSignalBlockSettingsDifferent(&signalBlocks, &savedSignalBlocks))
        AddExitDetail(&details, "- Signal shrink blocks");
    if (memcmp(&mapEvents, &savedMapEvents, sizeof(mapEvents)) != 0) AddExitDetail(&details, "- Map events");
    return details;
}

static void DrawExitConfirmation(bool showDetails, const EditorSaveSnapshot *snapshot,
                                 const RpgCharacter *player, const RpgCharacter *npc,
                                 const RpgStage *stage, const RpgDialogue *dialogue,
                                 const RpgStage3Event *stage3Event, const RpgItems *items,
                                 const RpgItems *savedItems, int detailScroll)
{
    DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.68f));
    DrawRectangleRec(exitConfirmationBounds, RAYWHITE);
    DrawRectangleLinesEx(exitConfirmationBounds, 2.0f, MAROON);
    DrawText("Unsaved changes", 322, 144, 24, MAROON);
    DrawText("Save before closing?", 346, 178, 18, DARKGRAY);
    DrawRectangleRec(GetExitDetailsToggle(), DARKBLUE);
    DrawText(showDetails ? "Hide details" : "Show details", 389, 210, 16, RAYWHITE);
    if (showDetails) {
        ExitDetailList details = BuildUnsavedDetails(snapshot, player, npc, stage, dialogue, stage3Event,
                                                      items, savedItems);
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

static void UpdateImeCandidateWindowAt(int x, int y)
{
#ifdef _WIN32
    // 日本語IMEの変換候補を、編集している入力欄の近くに表示する。
    HIMC inputContext = ImmGetContext((HWND)GetWindowHandle());
    if (inputContext == NULL) return;
    CANDIDATEFORM candidateForm = { 0 };
    candidateForm.dwStyle = CFS_CANDIDATEPOS;
    candidateForm.ptCurrentPos.x = x;
    candidateForm.ptCurrentPos.y = y;
    ImmSetCandidateWindow(inputContext, &candidateForm);
    COMPOSITIONFORM compositionForm = { 0 };
    compositionForm.dwStyle = CFS_POINT;
    compositionForm.ptCurrentPos = candidateForm.ptCurrentPos;
    ImmSetCompositionWindow(inputContext, &compositionForm);
    ImmReleaseContext((HWND)GetWindowHandle(), inputContext);
#else
    (void)x;
    (void)y;
#endif
}

static void UpdateImeCandidateWindow(int activeLine, int scroll)
{
    UpdateImeCandidateWindowAt(200, 152 + (activeLine - scroll) * 28);
}

static bool IsCharacterClicked(const RpgCharacter *character, Vector2 mousePosition)
{
    Rectangle characterBounds = { character->position.x - 22.0f * character->scale,
                                  character->position.y - 84.0f * character->scale,
                                  44.0f * character->scale, 90.0f * character->scale };
    return CheckCollisionPointRec(mousePosition, characterBounds);
}

// キャラクター種別に依存しない、エディター上の横位置ドラッグ更新。
// 足元の高さは各キャラクターの設定を保ち、地面から浮かない編集にする。
static void MoveCharacterToEditorPointer(RpgCharacter *character, int mapIndex, Vector2 pointer)
{
    if (character == NULL) return;
    character->position.x = pointer.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
}

// 共通ドラッグが始まった瞬間に、背後のインスペクター操作と選択表示を止める。
static void HideInspectorDuringObjectDrag(int *selected, int *activeDialogueLine,
                                          bool *isItemNameEditing, bool *isAttachmentPathEditing,
                                          bool *isReferencePathEditing)
{
    *selected = 0;
    *activeDialogueLine = -1;
    *isItemNameEditing = false;
    *isAttachmentPathEditing = false;
    *isReferencePathEditing = false;
}

static int GetClickedItemIndex(const RpgItems *items, int mapIndex, Vector2 mousePosition)
{
    Vector2 worldPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                              mousePosition.y };
    return RpgItems_FindAtPosition(items, worldPosition, 18.0f);
}

static int FindWireStartingAtReceiver(const RpgWires *wireList, const RpgReceiver *receiver)
{
    for (int wireIndex = wireList->count - 1; wireIndex >= 0; wireIndex--) {
        const RpgWire *wire = &wireList->entries[wireIndex];
        if (wire->hasReceiverSource && wire->receiverCell.row == receiver->cell.row &&
            wire->receiverCell.column == receiver->cell.column) return wireIndex;
    }
    return -1;
}

// 矢印方向の隣接エリアへ移動し、未作成なら空のエリアを台帳へ追加する。
static int GetRequestedMapIndex(RpgStage *stage, int currentMapIndex)
{
    RpgAreaDirection direction;
    if (IsKeyPressed(KEY_LEFT)) direction = RPG_AREA_LEFT;
    else if (IsKeyPressed(KEY_RIGHT)) direction = RPG_AREA_RIGHT;
    else if (IsKeyPressed(KEY_UP)) direction = RPG_AREA_UP;
    else if (IsKeyPressed(KEY_DOWN)) direction = RPG_AREA_DOWN;
    else return -1;
    int nextMapIndex = RpgStage_GetOrCreateAdjacentMap(stage, currentMapIndex, direction);
    if (nextMapIndex >= 0) return nextMapIndex;
    return -1;
}

static int FindAnyAdjacentMap(const RpgStage *stage, int mapIndex)
{
    for (int direction = RPG_AREA_LEFT; direction <= RPG_AREA_DOWN; direction++) {
        int adjacent = RpgStage_GetAdjacentMap(stage, mapIndex, (RpgAreaDirection)direction);
        if (adjacent >= 0) return adjacent;
    }
    return -1;
}

static bool IsPositionInMap(Vector2 position, int mapIndex)
{
    float firstX = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    float lastX = firstX + RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    return position.x >= firstX && position.x < lastX;
}

// エリア削除後に、スロットへ残る配置物をまとめて除去して再利用時の混在を防ぐ。
static void RemoveMapOwnedObjects(int mapIndex, RpgItems *items, RpgMapEvents *events,
                                  RpgStage *stage, RpgWires *wireList,
                                  RpgReceivers *receiverList, RpgAttachments *attachmentList)
{
    int writeIndex = 0;
    for (int index = 0; index < items->count; index++)
        if (!IsPositionInMap(items->entries[index].position, mapIndex))
            items->entries[writeIndex++] = items->entries[index];
    items->count = writeIndex;
    writeIndex = 0;
    for (int index = 0; index < events->count; index++)
        if (!IsPositionInMap(events->entries[index].position, mapIndex))
            events->entries[writeIndex++] = events->entries[index];
    events->count = writeIndex;
    RpgWires_RemoveBroken(wireList, stage);
    RpgReceivers_RemoveBroken(receiverList, stage);
    RpgAttachments_RemoveBroken(attachmentList, stage);
}

// ドラッグで飛び越えたマスも順にたどり、導線端点を滑らかに延長・短縮する。
static bool MoveWireEndpointToward(RpgWires *wireList, const RpgStage *stage, int wireIndex,
                                   bool isStart, int destinationRow, int destinationColumn,
                                   int *lastRow, int *lastColumn)
{
    bool changed = false;
    while (*lastRow != destinationRow || *lastColumn != destinationColumn) {
        int nextRow = *lastRow;
        int nextColumn = *lastColumn;
        if (nextColumn != destinationColumn) nextColumn += destinationColumn > nextColumn ? 1 : -1;
        else nextRow += destinationRow > nextRow ? 1 : -1;
        if (!RpgWires_MoveEndpoint(wireList, stage, wireIndex, isStart, nextRow, nextColumn)) break;
        *lastRow = nextRow;
        *lastColumn = nextColumn;
        changed = true;
    }
    return changed;
}

// データ弾の軌道も同じ補間方式で編集し、速いドラッグで経路が欠けないようにする。
static bool MoveAttachmentPathEndpointToward(RpgAttachments *attachmentList, const RpgStage *stage,
                                             int attachmentIndex, int destinationRow, int destinationColumn)
{
    if (attachmentIndex < 0 || attachmentIndex >= attachmentList->count) return false;
    RpgGridPath *path = &attachmentList->entries[attachmentIndex].dataPath;
    if (path->cellCount <= 0) return false;
    bool changed = false;
    while (path->cells[path->cellCount - 1].row != destinationRow ||
           path->cells[path->cellCount - 1].column != destinationColumn) {
        RpgGridCell endpoint = path->cells[path->cellCount - 1];
        int nextRow = endpoint.row;
        int nextColumn = endpoint.column;
        if (nextColumn != destinationColumn) nextColumn += destinationColumn > nextColumn ? 1 : -1;
        else nextRow += destinationRow > nextRow ? 1 : -1;
        if (!RpgAttachments_MoveDataPathEndpoint(attachmentList, stage, attachmentIndex,
                                                  nextRow, nextColumn)) break;
        changed = true;
    }
    return changed;
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
                           const RpgCharacter *npc, RpgStage *stage,
                           const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                           const RpgItems *items)
{
    // プレビュー用に一時的に外したマスは保存前に必ず復元する。
    RpgSignalBlocks_EndPreviews(&signalBlocks, stage);
    layout->playerPosition = player->position;
    layout->npcPosition = npc->position;
    layout->playerMoveSpeed = player->moveSpeed;
    layout->playerScale = player->scale;
    layout->npcScale = npc->scale;
    bool layoutSaved = RpgLayout_Save(TextFormat("%s../assets/Settings/Stage/rpg_layout.cfg",
                                                  GetApplicationDirectory()), layout);
    bool stageSaved = RpgStage_Save(TextFormat("%s../assets/Settings/Stage/rpg_stage.cfg",
                                                GetApplicationDirectory()), stage);
    bool dialogueSaved = RpgDialogue_Save(TextFormat("%s../assets/Settings/Stage/rpg_dialogue.txt",
                                                      GetApplicationDirectory()), dialogue);
    bool eventSaved = RpgStage3Event_Save(TextFormat("%s../assets/Settings/Stage/rpg_stage3_event.cfg",
                                                      GetApplicationDirectory()), stage3Event);
    bool zipperSaved = RpgZipper_Save(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg",
                                                  GetApplicationDirectory()), &zipperData);
    bool npcInspectSaved = RpgInspect_Save(TextFormat("%s../assets/Settings/Stage/rpg_inspect.cfg",
                                                      GetApplicationDirectory()), &npcInspectData);
    bool zipperInspectSaved = RpgInspect_Save(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg",
                                                         GetApplicationDirectory()), &zipperInspectData);
    bool itemsSaved = RpgItems_Save(TextFormat("%s../assets/Settings/Stage/rpg_items.cfg",
                                                GetApplicationDirectory()), items);
    bool wiresSaved = RpgWires_Save(TextFormat("%s../assets/Settings/Stage/rpg_wires.cfg",
                                                 GetApplicationDirectory()), &wires);
    bool receiversSaved = RpgReceivers_Save(TextFormat("%s../assets/Settings/Stage/rpg_receivers.cfg",
                                                        GetApplicationDirectory()), &receivers);
    bool attachmentsSaved = RpgAttachments_Save(TextFormat("%s../assets/Settings/Stage/rpg_attachments.cfg",
                                                            GetApplicationDirectory()), &attachments);
    bool signalBlocksSaved = RpgSignalBlocks_Save(TextFormat("%s../assets/Settings/Stage/rpg_signal_blocks.cfg",
                                                              GetApplicationDirectory()), &signalBlocks);
    bool mapEventsSaved = RpgMapEvents_Save(TextFormat("%s../assets/Settings/Stage/rpg_map_events.cfg", GetApplicationDirectory()), &mapEvents);
    if (mapEventsSaved) savedMapEvents = mapEvents;
    if (wiresSaved) savedWires = wires;
    if (receiversSaved) savedReceivers = receivers;
    if (attachmentsSaved) savedAttachments = attachments;
    if (signalBlocksSaved) savedSignalBlocks = signalBlocks;
    return layoutSaved && stageSaved && dialogueSaved && eventSaved && zipperSaved && npcInspectSaved && zipperInspectSaved && itemsSaved && wiresSaved && receiversSaved && attachmentsSaved && signalBlocksSaved && mapEventsSaved;
}

static void UpdateSaveSnapshot(EditorSaveSnapshot *snapshot, const RpgCharacter *player,
                               const RpgCharacter *npc, const RpgLayout *layout, const RpgStage *stage,
                               const RpgDialogue *dialogue, const RpgStage3Event *stage3Event)
{
    snapshot->layout = *layout;
    snapshot->player = *player;
    snapshot->npc = *npc;
    snapshot->stage = *stage;
    snapshot->dialogue = *dialogue;
    snapshot->stage3Event = *stage3Event;
    snapshot->zipper = zipperData;
    snapshot->npcInspectSnapshot = npcInspectData;
    snapshot->zipperInspectSnapshot = zipperInspectData;
}

static RPG_UNUSED void CaptureInspectorUi(EditorSaveSnapshot *snapshot, int mapIndex, int selected,
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

static RPG_UNUSED bool IsInspectorUiDifferent(const EditorSaveSnapshot *before, int mapIndex, int selected,
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

static RPG_UNUSED void RestoreInspectorUi(const EditorSaveSnapshot *state, int *mapIndex, int *selected,
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

static void AppendBlockHistory(BlockHistory *history, BlockHistoryEntry entry)
{
    // 配置処理を履歴機構から切り離したため、ここでは状態を保持しない。
    (void)history;
    (void)entry;
}

static void PushBlockHistory(BlockHistory *history, int row, int column, int previousValue, const RpgItem *removedItem)
{
    BlockHistoryEntry entry = {
        .kind = BLOCK_HISTORY_CELL_CHANGE,
        .row = row,
        .column = column,
        .previousValue = previousValue,
        .restoresItem = removedItem != NULL
    };
    if (removedItem != NULL) entry.item = *removedItem;
    AppendBlockHistory(history, entry);
}

static void PushItemAddedHistory(BlockHistory *history, RpgItem item)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_ITEM_ADDED, .item = item });
}

// ブロック上の追加要素は、変更した要素だけを共通履歴へ積む。今後のプロパティもこの形式を使う。
static void PushWireAddedHistory(BlockHistory *history, int wireIndex)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_WIRE_ADDED,
                                                      .propertyIndex = wireIndex });
}

static void PushWireChangedHistory(BlockHistory *history, int wireIndex, RpgWire previousWire)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_WIRE_CHANGED,
                                                      .propertyIndex = wireIndex,
                                                      .wire = previousWire });
}

static void PushReceiverAddedHistory(BlockHistory *history, int receiverIndex)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_RECEIVER_ADDED,
                                                      .propertyIndex = receiverIndex });
}

static void PushAttachmentAddedHistory(BlockHistory *history, RpgAttachment attachment)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_ATTACHMENT_ADDED,
                                                      .attachment = attachment });
}

static void PushAttachmentChangedHistory(BlockHistory *history, int attachmentIndex,
                                         RpgAttachment previousAttachment)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_ATTACHMENT_CHANGED,
                                                      .propertyIndex = attachmentIndex,
                                                      .attachment = previousAttachment });
}

static void PushAttachmentRemovedHistory(BlockHistory *history, RpgAttachment attachment)
{
    AppendBlockHistory(history, (BlockHistoryEntry){ .kind = BLOCK_HISTORY_ATTACHMENT_REMOVED,
                                                      .attachment = attachment });
}

static BlockHistoryEntry CreateReceiverChangedHistory(int receiverIndex,
                                                      const RpgReceivers *receiverList,
                                                      const RpgWires *wireList)
{
    BlockHistoryEntry entry = { .kind = BLOCK_HISTORY_RECEIVER_CHANGED,
                                .propertyIndex = receiverIndex,
                                .receiver = receiverList->entries[receiverIndex] };
    const RpgReceiver *receiver = &entry.receiver;
    for (int wireIndex = 0; wireIndex < wireList->count; wireIndex++) {
        const RpgWire *wire = &wireList->entries[wireIndex];
        if (!wire->hasReceiverSource || wire->receiverCell.row != receiver->cell.row ||
            wire->receiverCell.column != receiver->cell.column || wire->receiverSide != receiver->side)
            continue;
        entry.receiverWireIndices[entry.receiverWireCount] = wireIndex;
        entry.receiverWireSides[entry.receiverWireCount++] = wire->receiverSide;
    }
    return entry;
}

// 相対座標で定義した全マスが一致する場合だけ、特殊ブロックを完全な一個として扱う。
static bool IsEffectBlockCompleteAt(const RpgStage *stage, int rootRow, int rootColumn,
                                    const RpgEffectShape *shape)
{
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int row = rootRow + cell->offsetY;
        int column = rootColumn + cell->offsetX;
        if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
            stage->blocks[row][column] != cell->blockType) return false;
    }
    return true;
}

// 構成マスのどこをクリックしても、形状定義から先頭マスを逆算する。
static bool FindEffectBlockRoot(const RpgStage *stage, int row, int column, int *rootRow,
                                int *rootColumn, const RpgEffectShape **shapeResult)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(stage->blocks[row][column]);
    if (shape == NULL) return false;
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        if (cell->blockType != stage->blocks[row][column]) continue;
        int candidateRow = row - cell->offsetY;
        int candidateColumn = column - cell->offsetX;
        if (IsEffectBlockCompleteAt(stage, candidateRow, candidateColumn, shape)) {
            *rootRow = candidateRow;
            *rootColumn = candidateColumn;
            if (shapeResult != NULL) *shapeResult = shape;
            return true;
        }
    }
    return false;
}

static bool PlaceItemProperty(BlockPropertyPlacementContext *context, RpgGridCell cell,
                              const char **message)
{
    int rootRow;
    int rootColumn;
    if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS || cell.column < 0 ||
        cell.column >= RPG_STAGE_WORLD_COLUMNS || context->stage->blocks[cell.row][cell.column] == 0)
        return false;
    if (FindEffectBlockRoot(context->stage, cell.row, cell.column, &rootRow, &rootColumn, NULL))
        cell = (RpgGridCell){ rootRow, rootColumn };
    Vector2 position = { cell.column * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                         cell.row * RPG_STAGE_TILE_SIZE - 12.0f };
    if (RpgItems_FindAtPosition(context->items, position, 20.0f) >= 0 ||
        !RpgItems_Add(context->items, position)) return false;
    PushItemAddedHistory(context->history, context->items->entries[context->items->count - 1]);
    *message = "Item property added";
    return true;
}

static bool PlaceWireProperty(BlockPropertyPlacementContext *context, RpgGridCell cell,
                              const char **message)
{
    if (!RpgWires_AddAdjacent(context->wires, context->stage, cell.row, cell.column)) return false;
    PushWireAddedHistory(context->history, context->wires->count - 1);
    *message = "Wire start and adjacent endpoint added";
    return true;
}

static bool PlaceReceiverProperty(BlockPropertyPlacementContext *context, RpgGridCell cell,
                                  const char **message)
{
    if (!RpgReceivers_Add(context->receivers, context->stage, cell)) return false;
    PushReceiverAddedHistory(context->history, context->receivers->count - 1);
    if (RpgWires_AddFromReceiver(context->wires, context->stage, cell, RPG_GRID_SIDE_TOP))
        *message = "Receiver and default wire added";
    else *message = "Receiver added, but wire limit reached";
    return true;
}

static const BlockPropertyPlacementDefinition blockPropertyPlacements[] = {
    { RPG_BLOCK_PROPERTY_ITEM, PlaceItemProperty },
    { RPG_BLOCK_PROPERTY_WIRE, PlaceWireProperty },
    { RPG_BLOCK_PROPERTY_RECEIVER, PlaceReceiverProperty }
};

static const BlockPropertyPlacementDefinition *FindBlockPropertyPlacement(int blockType)
{
    for (int index = 0; index < (int)(sizeof(blockPropertyPlacements) / sizeof(blockPropertyPlacements[0])); index++)
        if (blockPropertyPlacements[index].blockType == blockType) return &blockPropertyPlacements[index];
    return NULL;
}

// 設置入力は種類を知らず、登録済みの設置定義だけを呼び出す。
static bool PlaceSelectedBlockProperty(int blockType, RpgGridCell cell, RpgStage *stage,
                                       RpgItems *items, RpgWires *wireList,
                                       RpgReceivers *receiverList, BlockHistory *history,
                                       const char **message)
{
    const BlockPropertyPlacementDefinition *definition = FindBlockPropertyPlacement(blockType);
    if (definition == NULL) return false;
    BlockPropertyPlacementContext context = { stage, items, wireList, receiverList, history };
    return definition->place(&context, cell, message);
}

// 複数マス特殊ブロックを配置する前に、必要な全マスが空いていることを確認する。
static bool PlaceBlockType(RpgStage *stage, BlockHistory *history, int row, int column, int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    if (shape == NULL) {
        if (stage->blocks[row][column] == blockType) return false;
        PushBlockHistory(history, row, column, stage->blocks[row][column], NULL);
        stage->blocks[row][column] = blockType;
        return true;
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = row + cell->offsetY;
        int targetColumn = column + cell->offsetX;
        if (targetRow < 0 || targetRow >= RPG_STAGE_ROWS || targetColumn < 0 ||
            targetColumn >= RPG_STAGE_WORLD_COLUMNS || stage->blocks[targetRow][targetColumn] != 0) return false;
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = row + cell->offsetY;
        int targetColumn = column + cell->offsetX;
        PushBlockHistory(history, targetRow, targetColumn, 0, NULL);
        stage->blocks[targetRow][targetColumn] = cell->blockType;
    }
    return true;
}

static int GetEffectShapeBlockTypeAt(const RpgEffectShape *shape, int rootRow, int rootColumn,
                                     int row, int column)
{
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        if (row == rootRow + cell->offsetY && column == rootColumn + cell->offsetX) return cell->blockType;
    }
    return 0;
}

// 特殊ブロックを形状ごと移動し、構成マス・付随アイテム・Ctrl+Z用情報をまとめて更新する。
static bool MoveEffectBlock(RpgStage *stage, RpgItems *items, BlockHistory *history,
                            int sourceRow, int sourceColumn, int destinationRow, int destinationColumn)
{
    int sourceRootRow;
    int sourceRootColumn;
    const RpgEffectShape *shape;
    if (!FindEffectBlockRoot(stage, sourceRow, sourceColumn, &sourceRootRow, &sourceRootColumn, &shape)) return false;

    int movedItemIndices[RPG_BLOCK_EFFECT_MAX_SHAPE_CELLS];
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int sourceCellRow = sourceRootRow + cell->offsetY;
        int sourceCellColumn = sourceRootColumn + cell->offsetX;
        Vector2 itemPosition = { sourceCellColumn * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                                 sourceCellRow * RPG_STAGE_TILE_SIZE - 12.0f };
        movedItemIndices[cellIndex] = RpgItems_FindAtPosition(items, itemPosition, 20.0f);
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = destinationRow + cell->offsetY;
        int targetColumn = destinationColumn + cell->offsetX;
        if (targetRow < 0 || targetRow >= RPG_STAGE_ROWS || targetColumn < 0 || targetColumn >= RPG_STAGE_WORLD_COLUMNS)
            return false;
        bool isSourceCell = GetEffectShapeBlockTypeAt(shape, sourceRootRow, sourceRootColumn, targetRow, targetColumn) != 0;
        Vector2 targetItemPosition = { targetColumn * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                                       targetRow * RPG_STAGE_TILE_SIZE - 12.0f };
        int targetItemIndex = RpgItems_FindAtPosition(items, targetItemPosition, 20.0f);
        bool isMovedItem = false;
        for (int itemIndex = 0; itemIndex < shape->cellCount; itemIndex++)
            if (targetItemIndex >= 0 && targetItemIndex == movedItemIndices[itemIndex]) isMovedItem = true;
        if ((!isSourceCell && stage->blocks[targetRow][targetColumn] != 0) ||
            (targetItemIndex >= 0 && !isMovedItem)) return false;
    }

    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int sourceCellRow = sourceRootRow + cell->offsetY;
        int sourceCellColumn = sourceRootColumn + cell->offsetX;
        int finalType = GetEffectShapeBlockTypeAt(shape, destinationRow, destinationColumn, sourceCellRow, sourceCellColumn);
        if (stage->blocks[sourceCellRow][sourceCellColumn] != finalType) {
            const RpgItem *restoredItem = movedItemIndices[cellIndex] >= 0 ?
                                          &items->entries[movedItemIndices[cellIndex]] : NULL;
            PushBlockHistory(history, sourceCellRow, sourceCellColumn,
                             stage->blocks[sourceCellRow][sourceCellColumn], restoredItem);
        }
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = destinationRow + cell->offsetY;
        int targetColumn = destinationColumn + cell->offsetX;
        if (GetEffectShapeBlockTypeAt(shape, sourceRootRow, sourceRootColumn, targetRow, targetColumn) == 0)
            PushBlockHistory(history, targetRow, targetColumn, stage->blocks[targetRow][targetColumn], NULL);
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        stage->blocks[sourceRootRow + cell->offsetY][sourceRootColumn + cell->offsetX] = 0;
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        stage->blocks[destinationRow + cell->offsetY][destinationColumn + cell->offsetX] = cell->blockType;
        if (movedItemIndices[cellIndex] >= 0) {
            RpgItem movedItem = items->entries[movedItemIndices[cellIndex]];
            movedItem.position = (Vector2){ (destinationColumn + cell->offsetX) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                                            (destinationRow + cell->offsetY) * RPG_STAGE_TILE_SIZE - 12.0f };
            PushItemAddedHistory(history, movedItem);
            items->entries[movedItemIndices[cellIndex]].position = movedItem.position;
        }
    }
    return true;
}

// 特殊ブロックの移動入口を一つに集約する。通常の形状ブロックは共通形状移動を使い、
// 状態によって一部のマスが存在しないブロックだけが専用処理を追加できる。
static bool MoveSpecialBlock(RpgStage *stage, RpgItems *items, BlockHistory *history,
                             RpgSignalBlocks *signalBlockList, int sourceRow, int sourceColumn,
                             int destinationRow, int destinationColumn)
{
    int signalIndex = RpgSignalBlocks_FindAtCell(signalBlockList, stage, sourceRow, sourceColumn);
    if (signalIndex < 0)
        return MoveEffectBlock(stage, items, history, sourceRow, sourceColumn, destinationRow, destinationColumn);
    if (signalBlockList->entries[signalIndex].startsExpanded) {
        if (!MoveEffectBlock(stage, items, history, sourceRow, sourceColumn, destinationRow, destinationColumn))
            return false;
        return RpgSignalBlocks_Move(signalBlockList, signalIndex, destinationRow, destinationColumn);
    }

    // 通常時に縮んでいる伸縮ブロックは根元だけを形状として移動する。
    RpgSignalBlock *signalBlock = &signalBlockList->entries[signalIndex];
    if (destinationRow < 0 || destinationRow >= RPG_STAGE_ROWS || destinationColumn < 0 ||
        destinationColumn >= RPG_STAGE_WORLD_COLUMNS || stage->blocks[destinationRow][destinationColumn] != 0)
        return false;
    int rootType = stage->blocks[signalBlock->row][signalBlock->column];
    if (rootType == 0) return false;
    Vector2 sourceItemPosition = { signalBlock->column * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                                   signalBlock->row * RPG_STAGE_TILE_SIZE - 12.0f };
    Vector2 destinationItemPosition = { destinationColumn * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                                        destinationRow * RPG_STAGE_TILE_SIZE - 12.0f };
    int itemIndex = RpgItems_FindAtPosition(items, sourceItemPosition, 20.0f);
    if (RpgItems_FindAtPosition(items, destinationItemPosition, 20.0f) >= 0) return false;
    RpgItem movedItem;
    const RpgItem *movedItemPointer = NULL;
    if (itemIndex >= 0) { movedItem = items->entries[itemIndex]; movedItemPointer = &movedItem; }
    PushBlockHistory(history, signalBlock->row, signalBlock->column, rootType, movedItemPointer);
    PushBlockHistory(history, destinationRow, destinationColumn, 0, NULL);
    stage->blocks[signalBlock->row][signalBlock->column] = 0;
    stage->blocks[destinationRow][destinationColumn] = rootType;
    if (itemIndex >= 0) {
        items->entries[itemIndex].position = destinationItemPosition;
        movedItem.position = destinationItemPosition;
        PushItemAddedHistory(history, movedItem);
    }
    return RpgSignalBlocks_Move(signalBlockList, signalIndex, destinationRow, destinationColumn);
}

// ドアは同じ占有形状の開閉用タイルへ置き換え、ステージ保存だけで状態を保持する。
static bool SetDoorOpen(RpgStage *stage, int row, int column, bool isOpen)
{
    return RpgStage_SetDoorOpenAtCell(stage, row, column, isOpen);
}

// 連結ブロックは、どの構成マスを削除しても全体と付随アイテムを同時に削除する。
static bool RemoveBlockAt(RpgStage *stage, RpgItems *items, BlockHistory *history, int row, int column)
{
    if (stage->blocks[row][column] == 0) return false;
    int rootRow = row;
    int rootColumn = column;
    const RpgEffectShape *shape = NULL;
    bool isEffect = FindEffectBlockRoot(stage, row, column, &rootRow, &rootColumn, &shape);
    int cellCount = isEffect ? shape->cellCount : 1;
    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        int targetRow = isEffect ? rootRow + shape->cells[cellIndex].offsetY : row;
        int targetColumn = isEffect ? rootColumn + shape->cells[cellIndex].offsetX : column;
        Vector2 itemPosition = { targetColumn * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE / 2.0f,
                                 targetRow * RPG_STAGE_TILE_SIZE - 12.0f };
        int itemIndex = RpgItems_FindAtPosition(items, itemPosition, 20.0f);
        RpgItem removedItem;
        const RpgItem *removedItemPointer = NULL;
        if (itemIndex >= 0) {
            removedItem = items->entries[itemIndex];
            removedItemPointer = &removedItem;
        }
        PushBlockHistory(history, targetRow, targetColumn, stage->blocks[targetRow][targetColumn], removedItemPointer);
        stage->blocks[targetRow][targetColumn] = 0;
        if (removedItemPointer != NULL) RpgItems_RemoveAtPosition(items, itemPosition, 20.0f);
    }
    return true;
}

// ブロックと一体の星アイテムは、変更したマスとアイテム一件だけを記録して復元する。
static RPG_UNUSED bool UndoBlockChange(BlockHistory *history, RpgStage *stage, RpgItems *items,
                            RpgWires *wireList, RpgReceivers *receiverList,
                            RpgAttachments *attachmentList)
{
    if (history->count <= 0) return false;
    BlockHistoryEntry entry = history->entries[--history->count];
    if (entry.kind == BLOCK_HISTORY_WIRE_ADDED) {
        if (entry.propertyIndex < 0 || entry.propertyIndex >= wireList->count) return false;
        for (int index = entry.propertyIndex; index < wireList->count - 1; index++)
            wireList->entries[index] = wireList->entries[index + 1];
        wireList->count--;
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_WIRE_CHANGED) {
        if (entry.propertyIndex < 0 || entry.propertyIndex >= wireList->count) return false;
        wireList->entries[entry.propertyIndex] = entry.wire;
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_RECEIVER_ADDED) {
        if (entry.propertyIndex < 0 || entry.propertyIndex >= receiverList->count) return false;
        RpgReceiver receiver = receiverList->entries[entry.propertyIndex];
        // 受容体の作成時に自動生成した導線も、同じ一回のUndoで取り除く。
        for (int wireIndex = wireList->count - 1; wireIndex >= 0; wireIndex--) {
            RpgWire *wire = &wireList->entries[wireIndex];
            if (!wire->hasReceiverSource || wire->receiverCell.row != receiver.cell.row ||
                wire->receiverCell.column != receiver.cell.column) continue;
            for (int next = wireIndex; next < wireList->count - 1; next++)
                wireList->entries[next] = wireList->entries[next + 1];
            wireList->count--;
        }
        for (int index = entry.propertyIndex; index < receiverList->count - 1; index++)
            receiverList->entries[index] = receiverList->entries[index + 1];
        receiverList->count--;
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_RECEIVER_CHANGED) {
        if (entry.propertyIndex < 0 || entry.propertyIndex >= receiverList->count) return false;
        receiverList->entries[entry.propertyIndex] = entry.receiver;
        for (int index = 0; index < entry.receiverWireCount; index++) {
            int wireIndex = entry.receiverWireIndices[index];
            if (wireIndex >= 0 && wireIndex < wireList->count)
                wireList->entries[wireIndex].receiverSide = entry.receiverWireSides[index];
        }
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_ITEM_ADDED) {
        RpgItems_RemoveAtPosition(items, entry.item.position, 1.0f);
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_ATTACHMENT_ADDED)
        return RpgAttachments_Remove(attachmentList, entry.attachment);
    if (entry.kind == BLOCK_HISTORY_ATTACHMENT_CHANGED) {
        if (entry.propertyIndex < 0 || entry.propertyIndex >= attachmentList->count) return false;
        attachmentList->entries[entry.propertyIndex] = entry.attachment;
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_ATTACHMENT_REMOVED) {
        if (attachmentList->count >= RPG_ATTACHMENT_MAX_COUNT) return false;
        attachmentList->entries[attachmentList->count++] = entry.attachment;
        return true;
    }

    stage->blocks[entry.row][entry.column] = entry.previousValue;
    if (entry.restoresItem && items->count < RPG_ITEM_MAX_COUNT &&
        RpgItems_FindAtPosition(items, entry.item.position, 1.0f) < 0) {
        items->entries[items->count++] = entry.item;
    }
    return true;
}

// 未保存の編集中データだけを、直近の保存スナップショットへ戻す。
static RPG_UNUSED void RevertToSavedSnapshot(const EditorSaveSnapshot *snapshot, RpgLayout *layout,
                                  RpgCharacter *player, RpgCharacter *npc, RpgStage *stage,
                                  RpgDialogue *dialogue, RpgStage3Event *stage3Event)
{
    *layout = snapshot->layout;
    *player = snapshot->player;
    *npc = snapshot->npc;
    *stage = snapshot->stage;
    *dialogue = snapshot->dialogue;
    *stage3Event = snapshot->stage3Event;
    zipperData = snapshot->zipper;
    npcInspectData = snapshot->npcInspectSnapshot;
    zipperInspectData = snapshot->zipperInspectSnapshot;
    mapEvents = savedMapEvents;
    wires = savedWires;
    receivers = savedReceivers;
    attachments = savedAttachments;
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

static bool AreItemsDifferent(const RpgItems *first, const RpgItems *second)
{
    if (first->count != second->count) return true;
    for (int index = 0; index < first->count; index++) {
        const RpgItem *firstItem = &first->entries[index];
        const RpgItem *secondItem = &second->entries[index];
        if (firstItem->position.x != secondItem->position.x || firstItem->position.y != secondItem->position.y ||
            strcmp(firstItem->name, secondItem->name) != 0) return true;
    }
    return false;
}

static bool AreWiresDifferent(const RpgWires *first, const RpgWires *second)
{
    return memcmp(first, second, sizeof(*first)) != 0;
}

static bool AreReceiversDifferent(const RpgReceivers *first, const RpgReceivers *second)
{
    return memcmp(first, second, sizeof(*first)) != 0;
}

static bool AreAttachmentsDifferent(const RpgAttachments *first, const RpgAttachments *second)
{
    return memcmp(first, second, sizeof(*first)) != 0;
}

// 実行中・プレビュー中の残り時間は保存対象ではないため、設定値だけを比較する。
static bool AreSignalBlockSettingsDifferent(const RpgSignalBlocks *first, const RpgSignalBlocks *second)
{
    if (first->count != second->count) return true;
    for (int index = 0; index < first->count; index++) {
        const RpgSignalBlock *a = &first->entries[index];
        const RpgSignalBlock *b = &second->entries[index];
        if (a->row != b->row || a->column != b->column || a->duration != b->duration ||
            a->startsExpanded != b->startsExpanded) return true;
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
           snapshot->npc.scale != npc->scale || memcmp(&snapshot->stage, stage, sizeof(*stage)) != 0 ||
           IsDialogueDifferent(&snapshot->dialogue, dialogue) || snapshot->stage3Event.enabled != stage3Event->enabled ||
           snapshot->zipper.character.position.x != zipperData.character.position.x ||
           snapshot->zipper.character.scale != zipperData.character.scale ||
           snapshot->zipper.launchSpeed != zipperData.launchSpeed ||
           snapshot->zipper.returnSpeed != zipperData.returnSpeed ||
           snapshot->zipper.launchPreviewEnabled != zipperData.launchPreviewEnabled ||
           IsDialogueDifferent(&snapshot->stage3Event.dialogue, &stage3Event->dialogue) ||
           IsInspectDifferent(&snapshot->npcInspectSnapshot, &npcInspectData) ||
           IsInspectDifferent(&snapshot->zipperInspectSnapshot, &zipperInspectData) ||
           memcmp(&mapEvents, &savedMapEvents, sizeof(mapEvents)) != 0 ||
           AreAttachmentsDifferent(&attachments, &savedAttachments) ||
           AreSignalBlockSettingsDifferent(&signalBlocks, &savedSignalBlocks);
}

static bool HasAnyUnsavedChanges(const EditorSaveSnapshot *snapshot, const RpgCharacter *player,
                                 const RpgCharacter *npc, const RpgLayout *layout, const RpgStage *stage,
                                 const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                 const RpgItems *items, const RpgItems *savedItems)
{
    return snapshot->layout.electricCellDelay != layout->electricCellDelay ||
            HasUnsavedChanges(snapshot, player, npc, stage, dialogue, stage3Event) ||
            AreItemsDifferent(items, savedItems) || AreWiresDifferent(&wires, &savedWires) ||
            AreReceiversDifferent(&receivers, &savedReceivers);
}

static RPG_UNUSED void RecordEditorHistory(void *history, const EditorSaveSnapshot *beforeEdit,
                                 const RpgCharacter *player, const RpgCharacter *npc, const RpgLayout *layout,
                                 const RpgStage *stage,
                                const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                int mapIndex, int selected, bool isDialogueEditorOpen,
                                bool isExamineFunctionListOpen, bool isFunctionTypeListOpen,
                                bool isMoveFunctionEditorOpen, bool isStage3DialogueEditing,
                                bool isInspectDialogueEditing)
{
    if (beforeEdit->layout.electricCellDelay != layout->electricCellDelay ||
        HasUnsavedChanges(beforeEdit, player, npc, stage, dialogue, stage3Event) ||
        IsInspectorUiDifferent(beforeEdit, mapIndex, selected, isDialogueEditorOpen,
                               isExamineFunctionListOpen, isFunctionTypeListOpen,
                               isMoveFunctionEditorOpen, isStage3DialogueEditing,
                               isInspectDialogueEditing))
        (void)history;
}

static RPG_UNUSED bool UndoEditorChange(void *history, RpgLayout *layout, RpgCharacter *player,
                             RpgCharacter *npc, RpgStage *stage, RpgDialogue *dialogue,
                             RpgStage3Event *stage3Event, int *mapIndex, int *selected,
                             bool *isDialogueEditorOpen, bool *isExamineFunctionListOpen,
                             bool *isFunctionTypeListOpen, bool *isMoveFunctionEditorOpen,
                             bool *isStage3DialogueEditing, bool *isInspectDialogueEditing)
{
    (void)history;
    (void)layout;
    (void)player;
    (void)npc;
    (void)stage;
    (void)dialogue;
    (void)stage3Event;
    (void)mapIndex;
    (void)selected;
    (void)isDialogueEditorOpen;
    (void)isExamineFunctionListOpen;
    (void)isFunctionTypeListOpen;
    (void)isMoveFunctionEditorOpen;
    (void)isStage3DialogueEditing;
    (void)isInspectDialogueEditing;
    return false;
}

static bool SaveEditorAndUpdateSnapshot(RpgLayout *layout, const RpgCharacter *player,
                                        const RpgCharacter *npc, RpgStage *stage,
                                        const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                        const RpgItems *items, EditorSaveSnapshot *snapshot,
                                        RpgItems *savedItems)
{
    if (!SaveEditorData(layout, player, npc, stage, dialogue, stage3Event, items)) return false;
    UpdateSaveSnapshot(snapshot, player, npc, layout, stage, dialogue, stage3Event);
    *savedItems = *items;
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
    if (!RpgLayout_Save(TextFormat("%s../assets/Settings/Stage/rpg_layout.cfg", GetApplicationDirectory()), &savedLayout))
        return false;
    *layout = savedLayout;
    if (isPlayer) snapshot->player = *character;
    else snapshot->npc = *character;
    return true;
}

static bool SaveZipperSettings(EditorSaveSnapshot *snapshot)
{
    if (!RpgZipper_Save(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg", GetApplicationDirectory()), &zipperData))
        return false;
    snapshot->zipper = zipperData;
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
    const char *folderName = activeInspect == &npcInspectData ? "Stage" : "Zipper";
    const char *fileName = activeInspect == &npcInspectData ? "rpg_inspect.cfg" : "rpg_zipper_inspect.cfg";
    if (!RpgInspect_Save(TextFormat("%s../assets/Settings/%s/%s", GetApplicationDirectory(), folderName, fileName), activeInspect))
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
        if (!RpgStage3Event_Save(TextFormat("%s../assets/Settings/Stage/rpg_stage3_event.cfg",
                                            GetApplicationDirectory()), stage3Event)) return false;
        snapshot->stage3Event = *stage3Event;
        return true;
    }
    if (!RpgDialogue_Save(TextFormat("%s../assets/Settings/Stage/rpg_dialogue.txt",
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
    char suffix[RPG_STAGE_REFERENCE_PATH_LENGTH];
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

// 会話・話者・短い名称で共通のUTF-8テキスト編集操作を使う。
static void UpdateTextInput(char *text, size_t capacity, int *cursorIndex,
                            int *selectionAnchor, int *selectionEnd)
{
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
    bool isPasteShortcut = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V);
    if (isPasteShortcut) {
        const char *clipboard = GetClipboardText();
        DeleteSelectedText(text, cursorIndex, selectionAnchor, selectionEnd);
        while (clipboard != NULL && *clipboard != '\0') {
            int byteCount = 0;
            int codepoint = GetCodepointNext(clipboard, &byteCount);
            if (byteCount <= 0) break;
            if (codepoint >= 32) InsertUtf8AtCursor(text, capacity, cursorIndex, codepoint);
            clipboard += byteCount;
        }
        *selectionAnchor = *cursorIndex;
        *selectionEnd = *cursorIndex;
        // 貼り付けた日本語も、通常の入力と同じく描画用の文字セットへ登録する。
        GameFont_AddText(text);
    }
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        if (!isPasteShortcut && codepoint >= 32) {
            DeleteSelectedText(text, cursorIndex, selectionAnchor, selectionEnd);
            InsertUtf8AtCursor(text, capacity, cursorIndex, codepoint);
            // 入力済み文字をフォントへ加え、任意の日本語を ? で表示しないようにする。
            GameFont_AddText(text);
        }
        codepoint = GetCharPressed();
    }
}

static void UpdateDialogueText(RpgDialogue *dialogue, int activeLine, int *cursorIndex,
                               int *selectionAnchor, int *selectionEnd)
{
    if (activeLine < 0 || activeLine >= dialogue->lineCount) return;
    UpdateTextInput(dialogue->lines[activeLine], RPG_DIALOGUE_LINE_LENGTH,
                    cursorIndex, selectionAnchor, selectionEnd);
}

static void UpdateSpeakerText(RpgDialogue *dialogue, int activeLine, int *cursorIndex,
                              int *selectionAnchor, int *selectionEnd)
{
    if (activeLine < 0 || activeLine >= dialogue->lineCount) return;
    UpdateTextInput(dialogue->speakers[activeLine], RPG_DIALOGUE_SPEAKER_LENGTH,
                    cursorIndex, selectionAnchor, selectionEnd);
}

static void UpdateShortText(char *text, size_t capacity, int *cursorIndex,
                            int *selectionAnchor, int *selectionEnd)
{
    UpdateTextInput(text, capacity, cursorIndex, selectionAnchor, selectionEnd);
}

// すべてのテキスト入力欄で同じ太さ・点滅のカーソルを表示する。

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
            DrawTextCaret(cursorX, 99, 21);
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
            DrawTextCaret(cursorX, lineY + 3, blockHeight - 6);
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
    DrawInspectorFrame(npcInspectorBounds, "NPC Inspector", PURPLE, GetInspectorCloseButton(2));
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
    RpgCharacter localZipper = zipper->character;
    localZipper.position.x = localX;
    Rectangle destination = RpgZipper_GetSpriteBounds(&localZipper, 380.0f);
    DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

static Rectangle GetZipperPreviewCollisionBounds(const RpgZipper *zipper)
{
    Rectangle spriteBounds = RpgZipper_GetSpriteBounds(&zipper->character, 380.0f);
    return (Rectangle){ spriteBounds.x + spriteBounds.width * 0.20f,
                        spriteBounds.y + spriteBounds.height * 0.18f,
                        spriteBounds.width * 0.60f, spriteBounds.height * 0.82f };
}

static void UpdateZipperLaunchPreview(const RpgStage *stage, float deltaTime)
{
    // プレビューは本体を動かさず、同じスプライトを使うダミーだけを右向きに射出する。
    if (!zipperData.launchPreviewEnabled) {
        isZipperLaunchPreviewVisible = false;
        isZipperLaunchPreviewReturning = false;
        zipperLaunchPreviewCooldown = 0.0f;
        return;
    }
    if (!isZipperLaunchPreviewVisible) {
        zipperLaunchPreviewCooldown += deltaTime;
        if (zipperLaunchPreviewCooldown < 1.0f) return;
        zipperLaunchPreviewCooldown = 0.0f;
        zipperLaunchPreviewPosition = zipperData.character.position;
        isZipperLaunchPreviewVisible = true;
        isZipperLaunchPreviewReturning = false;
        return;
    }
    RpgZipper preview = zipperData;
    preview.character.position = zipperLaunchPreviewPosition;
    if (isZipperLaunchPreviewReturning) {
        Vector2 distance = Vector2Subtract(zipperData.character.position, preview.character.position);
        float length = Vector2Length(distance);
        float step = preview.returnSpeed * deltaTime;
        if (length <= step) {
            isZipperLaunchPreviewVisible = false;
            isZipperLaunchPreviewReturning = false;
            zipperLaunchPreviewCooldown = 0.0f;
            return;
        }
        preview.character.position = Vector2Add(preview.character.position,
                                                Vector2Scale(distance, step / length));
        zipperLaunchPreviewPosition = preview.character.position;
        return;
    }
    preview.character.position.x += preview.launchSpeed * deltaTime;
    Rectangle bounds = GetZipperPreviewCollisionBounds(&preview);
    if (bounds.x < 0.0f || bounds.x + bounds.width > RPG_STAGE_WORLD_WIDTH ||
        RpgStage_CheckSolidCollision(stage, bounds)) {
        // 衝突後はダミーを消さず、設定した帰還速度で開始位置へ戻す。
        isZipperLaunchPreviewReturning = true;
        return;
    }
    zipperLaunchPreviewPosition = preview.character.position;
}

static void DrawZipperLaunchPreview(Texture2D zipperTexture, const RpgZipper *zipper, int mapIndex)
{
    if (!isZipperLaunchPreviewVisible ||
        (int)(zipperLaunchPreviewPosition.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) != mapIndex) return;
    RpgCharacter preview = zipper->character;
    preview.position = zipperLaunchPreviewPosition;
    preview.position.x -= mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    Rectangle source = { 0.0f, 0.0f, 32.0f, 40.0f };
    Rectangle destination = RpgZipper_GetSpriteBounds(&preview, 380.0f);
    DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, Fade(WHITE, 0.68f));
    DrawRectangleLinesEx(destination, 1.0f, ORANGE);
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
    DrawInspectorFrame(zipperInspectorBounds, "Zipper Inspector", ORANGE, GetInspectorCloseButton(3));
    DrawText(TextFormat("Position: %.0f", zipper->character.position.x), 716, 248, 16,
             zipper->character.position.x != savedZipper->character.position.x ? MAROON : DARKGRAY);
    DrawText(TextFormat("Scale: %.1f", zipper->character.scale), 716, 270, 16,
             zipper->character.scale != savedZipper->character.scale ? MAROON : DARKGRAY);
    DrawText("[-]", 812, 270, 16, MAROON);
    DrawText("[+]", 864, 270, 16, DARKGREEN);
    DrawText(TextFormat("Launch speed: %.0f", zipper->launchSpeed), 716, 292, 16,
             zipper->launchSpeed != savedZipper->launchSpeed ? MAROON : DARKGRAY);
    DrawText("[-]", 812, 292, 16, MAROON);
    DrawText("[+]", 864, 292, 16, DARKGREEN);
    DrawText(TextFormat("Return speed: %.0f", zipper->returnSpeed), 716, 314, 16,
             zipper->returnSpeed != savedZipper->returnSpeed ? MAROON : DARKGRAY);
    DrawText("[-]", 812, 314, 16, MAROON);
    DrawText("[+]", 864, 314, 16, DARKGREEN);
    DrawRectangle(716, 330, 188, 28, zipper->launchPreviewEnabled ? DARKGREEN : GRAY);
    DrawText(zipper->launchPreviewEnabled ? "Preview: ON" : "Preview: OFF", 754, 336, 16, RAYWHITE);
    DrawRectangle(716, 358, 188, 28, DARKBLUE);
    DrawText("Edit examine", 750, 364, 17, RAYWHITE);
    DrawSaveButton((Rectangle){ 716, 394, 90, 26 }, saveState);
    DrawRevertButton((Rectangle){ 814, 394, 90, 26 });
}

static void DrawExamineFunctionList(const RpgInspect *inspect, int selectedIndex,
                                    int draggedIndex, bool isTitleEditing, int titleCursorIndex,
                                    int titleSelectionAnchor, int titleSelectionEnd,
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
    if (isTitleEditing && titleSelectionAnchor != titleSelectionEnd) {
        int start = titleSelectionAnchor < titleSelectionEnd ? titleSelectionAnchor : titleSelectionEnd;
        int end = titleSelectionAnchor < titleSelectionEnd ? titleSelectionEnd : titleSelectionAnchor;
        char prefix[RPG_INSPECT_TITLE_LENGTH];
        char selectedText[RPG_INSPECT_TITLE_LENGTH];
        memcpy(prefix, inspect->functions[selectedIndex].title, (size_t)start); prefix[start] = '\0';
        memcpy(selectedText, inspect->functions[selectedIndex].title + start, (size_t)(end - start)); selectedText[end - start] = '\0';
        int startX = 268 + (int)GameFont_MeasureText(prefix, 16.0f).x;
        int width = (int)GameFont_MeasureText(selectedText, 16.0f).x;
        DrawRectangle(startX, 134, width + 1, 19, Fade(SKYBLUE, 0.7f));
    }
    GameFont_Draw(inspect->functions[selectedIndex].title, 268, 135, 16,
                  isSelectedDirty ? MAROON : DARKBLUE);
    if (isTitleEditing) {
        char prefix[RPG_INSPECT_TITLE_LENGTH];
        memcpy(prefix, inspect->functions[selectedIndex].title, (size_t)titleCursorIndex);
        prefix[titleCursorIndex] = '\0';
        int cursorX = 268 + (int)GameFont_MeasureText(prefix, 16.0f).x;
        DrawTextCaret(cursorX, 134, 20);
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

static void DrawEditorItems(const RpgItems *items, int mapIndex)
{
    for (int index = 0; index < items->count; index++) {
        float localX = items->entries[index].position.x - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
        if (localX >= 0.0f && localX < RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)
            DrawPoly((Vector2){ localX, items->entries[index].position.y }, 5, 15.0f, -90.0f, GOLD);
    }
}

static void DrawEditorMapEvents(int mapIndex)
{
    float mapOffset = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    for (int index = 0; index < mapEvents.count; index++) {
        float localX = mapEvents.entries[index].position.x - mapOffset;
        if (localX >= 0.0f && localX < RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) {
            DrawCircleLines((int)localX, (int)mapEvents.entries[index].position.y - 24, 15.0f, ORANGE);
            DrawText("E", (int)localX - 5, (int)mapEvents.entries[index].position.y - 30, 14, ORANGE);
        }
    }
}

static void DrawItemInspector(const RpgItems *items, const RpgItems *savedItems,
                              int selectedItemIndex, bool isNameEditing, int cursorIndex, int selectionAnchor,
                              int selectionEnd)
{
    if (selectedItemIndex < 0 || selectedItemIndex >= items->count) return;
    const RpgItem *item = &items->entries[selectedItemIndex];
    bool isItemUnsaved = selectedItemIndex >= savedItems->count ||
                         item->position.x != savedItems->entries[selectedItemIndex].position.x ||
                         item->position.y != savedItems->entries[selectedItemIndex].position.y ||
                         strcmp(item->name, savedItems->entries[selectedItemIndex].name) != 0;
    Color itemTextColor = isItemUnsaved ? MAROON : DARKBLUE;
    DrawInspectorFrame((Rectangle){ 700, 80, 220, 150 }, "Item Inspector", GOLD, GetInspectorCloseButton(4));
    DrawText("Name", 716, 122, 16, isItemUnsaved ? MAROON : DARKGRAY);
    DrawRectangle(716, 144, 188, 28, isNameEditing ? Fade(SKYBLUE, 0.55f) : Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 144, 188, 28, isItemUnsaved ? MAROON : (isNameEditing ? PURPLE : GRAY));
    if (isNameEditing && selectionAnchor != selectionEnd) {
        int start = selectionAnchor < selectionEnd ? selectionAnchor : selectionEnd;
        int end = selectionAnchor < selectionEnd ? selectionEnd : selectionAnchor;
        char prefix[RPG_ITEM_NAME_LENGTH];
        char selectedText[RPG_ITEM_NAME_LENGTH];
        memcpy(prefix, item->name, (size_t)start); prefix[start] = '\0';
        memcpy(selectedText, item->name + start, (size_t)(end - start)); selectedText[end - start] = '\0';
        float startX = 724.0f + GameFont_MeasureText(prefix, 17.0f).x;
        float width = GameFont_MeasureText(selectedText, 17.0f).x;
        DrawRectangle((int)startX, 148, (int)width + 1, 19, Fade(SKYBLUE, 0.7f));
    }
    GameFont_Draw(item->name, 724, 149, 17, itemTextColor);
    if (isNameEditing) {
        char prefix[RPG_ITEM_NAME_LENGTH];
        memcpy(prefix, item->name, (size_t)cursorIndex);
        prefix[cursorIndex] = '\0';
        int cursorX = 724 + (int)GameFont_MeasureText(prefix, 17.0f).x;
        DrawTextCaret(cursorX, 147, 22);
    }
    DrawText(isItemUnsaved ? "Unsaved - Save all: S" : "Save all: S", 716, 188, 16,
             isItemUnsaved ? MAROON : DARKGRAY);
}

// FILE.png ごとに保存される参照先を、他のオブジェクトと同じインスペクターで編集する。
static void DrawReferenceInspector(const RpgStage *stage, const RpgStage *savedStage, int row, int column,
                                   bool isPathEditing, int cursorIndex, int selectionAnchor, int selectionEnd)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) return;
    const char *path = RpgStage_GetReferencePathAtCell(stage, row, column);
    bool isUnsaved = strcmp(path, RpgStage_GetReferencePathAtCell(savedStage, row, column)) != 0;
    DrawInspectorFrame(referenceInspectorBounds, "File Inspector", DARKBLUE, GetInspectorCloseButton(7));
    DrawText("Text file (.txt)", 716, 122, 16, isUnsaved ? MAROON : DARKGRAY);
    DrawRectangle(716, 144, 188, 28, isPathEditing ? Fade(SKYBLUE, 0.55f) : Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 144, 188, 28, isUnsaved ? MAROON : (isPathEditing ? PURPLE : GRAY));
    if (isPathEditing && selectionAnchor != selectionEnd) {
        int start = selectionAnchor < selectionEnd ? selectionAnchor : selectionEnd;
        int end = selectionAnchor < selectionEnd ? selectionEnd : selectionAnchor;
        char prefix[RPG_STAGE_REFERENCE_PATH_LENGTH];
        char selectedText[RPG_STAGE_REFERENCE_PATH_LENGTH];
        memcpy(prefix, path, (size_t)start); prefix[start] = '\0';
        memcpy(selectedText, path + start, (size_t)(end - start)); selectedText[end - start] = '\0';
        float startX = 724.0f + GameFont_MeasureText(prefix, 15.0f).x;
        DrawRectangle((int)startX, 149, (int)GameFont_MeasureText(selectedText, 15.0f).x + 1, 17,
                      Fade(SKYBLUE, 0.7f));
    }
    GameFont_Draw(path[0] != '\0' ? path : "Click to set a path", 724, 150, 15,
                  path[0] != '\0' ? (isUnsaved ? MAROON : DARKBLUE) : GRAY);
    if (isPathEditing) {
        char prefix[RPG_STAGE_REFERENCE_PATH_LENGTH];
        memcpy(prefix, path, (size_t)cursorIndex); prefix[cursorIndex] = '\0';
        DrawTextCaret(724 + (int)GameFont_MeasureText(prefix, 15.0f).x, 147, 22);
    }
    DrawRectangle(716, 180, 188, 28, DARKBLUE);
    DrawText("Select .txt file", 752, 186, 16, RAYWHITE);
    DrawText(isUnsaved ? "Unsaved - Save all: S" : "Save all: S", 716, 222, 16,
             isUnsaved ? MAROON : DARKGRAY);
}

static void DrawDoorInspector(const RpgStage *stage, const RpgStage *savedStage, int row, int column)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsDoorBlock(stage->blocks[row][column])) return;
    bool isOpen = RpgBlockInventory_IsDoorOpen(stage->blocks[row][column]);
    bool isUnsaved = stage->blocks[row][column] != savedStage->blocks[row][column];
    DrawInspectorFrame(doorInspectorBounds, "Door Inspector", BROWN, GetInspectorCloseButton(5));
    DrawText(isOpen ? "State: Open" : "State: Closed", 716, 122, 17, isUnsaved ? MAROON : DARKGRAY);
    DrawRectangle(716, 144, 86, 28, isOpen ? DARKGREEN : GRAY);
    DrawText("Open", 738, 150, 16, RAYWHITE);
    DrawRectangle(818, 144, 86, 28, isOpen ? GRAY : MAROON);
    DrawText("Close", 836, 150, 16, RAYWHITE);
    DrawText(isUnsaved ? "Unsaved - Save all: S" : "Save all: S", 716, 188, 16,
             isUnsaved ? MAROON : DARKGRAY);
}

static void DrawSignalShrinkInspector(int index)
{
    if (index < 0 || index >= signalBlocks.count) return;
    const RpgSignalBlock *block = &signalBlocks.entries[index];
    bool isUnsaved = index >= savedSignalBlocks.count || block->row != savedSignalBlocks.entries[index].row ||
                     block->column != savedSignalBlocks.entries[index].column ||
                     block->duration != savedSignalBlocks.entries[index].duration ||
                     block->startsExpanded != savedSignalBlocks.entries[index].startsExpanded;
    DrawInspectorFrame(doorInspectorBounds, "Signal Shrink Inspector", PURPLE, GetInspectorCloseButton(5));
    DrawText("Signal switches to the opposite state", 716, 122, 14, isUnsaved ? MAROON : DARKGRAY);
    DrawText(TextFormat("Duration: %.1fs", block->duration), 716, 150, 16, isUnsaved ? MAROON : DARKGRAY);
    DrawRectangle(816, 144, 38, 24, MAROON);
    DrawText("-", 830, 145, 20, RAYWHITE);
    DrawRectangle(864, 144, 38, 24, DARKGREEN);
    DrawText("+", 877, 145, 20, RAYWHITE);
    DrawRectangle(716, 180, 188, 26, block->startsExpanded ? DARKBLUE : DARKPURPLE);
    DrawText(block->startsExpanded ? "Default: Expanded" : "Default: Shrunk", 752, 185, 16, RAYWHITE);
    DrawRectangle(716, 212, 188, 26, DARKBLUE);
    DrawText("Preview signal", 754, 217, 16, RAYWHITE);
    DrawText("Click: rotate / drag: move", 716, 250, 14, DARKGRAY);
    DrawText(isUnsaved ? "Unsaved - Save all: S" : "Save all: S", 716, 270, 15,
             isUnsaved ? MAROON : DARKGRAY);
}

static void DrawAttachmentInspector(int attachmentIndex, bool isPathEditing)
{
    if (attachmentIndex < 0 || attachmentIndex >= attachments.count) return;
    const RpgAttachment *attachment = &attachments.entries[attachmentIndex];
    bool isUnsaved = attachmentIndex >= savedAttachments.count ||
                     memcmp(attachment, &savedAttachments.entries[attachmentIndex], sizeof(*attachment)) != 0;
    Rectangle bounds = { 700, 80, 220, 350 };
    bool isButton = attachment->type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON;
    DrawInspectorFrame(bounds, isButton ? "Button Inspector" : "Emitter Inspector", SKYBLUE,
                       GetInspectorCloseButton(6));
    if (isButton) {
        DrawText("Trigger: radio emitters", 716, 122, 16, isUnsaved ? MAROON : DARKGRAY);
        DrawText("Attached to block edge", 716, 150, 16, DARKGRAY);
        DrawRectangle(716, 176, 188, 26, DARKBLUE);
        DrawText("Preview signal", 756, 181, 16, RAYWHITE);
        DrawText("Run once (preview only)", 716, 214, 14, DARKGRAY);
        DrawText(isUnsaved ? "Unsaved - Save all: S" : "Save all: S", 716, 240, 16,
                 isUnsaved ? MAROON : DARKGRAY);
        return;
    }
    DrawText("Actual shot: folder based", 716, 122, 15, isUnsaved ? MAROON : DARKGRAY);
    DrawText("Trigger: Data Button", 716, 150, 16, isUnsaved ? MAROON : DARKGRAY);
    DrawText(TextFormat("Size / file: %.0fpx", attachment->sizePerFile), 716, 182, 16, isUnsaved ? MAROON : DARKGRAY);
    DrawRectangle(816, 194, 38, 24, MAROON);
    DrawText("-", 830, 195, 20, RAYWHITE);
    DrawRectangle(864, 194, 38, 24, DARKGREEN);
    DrawText("+", 877, 195, 20, RAYWHITE);
    DrawRectangle(716, 226, 94, 24, isAttachmentSpeedEditing ? Fade(SKYBLUE, 0.60f) : Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 226, 94, 24, isAttachmentSpeedEditing ? PURPLE : (isUnsaved ? MAROON : GRAY));
    DrawText(isAttachmentSpeedEditing ? attachmentSpeedInput :
             TextFormat("%.1f at 100B", attachment->dataSpeed), 720, 230, 15, DARKBLUE);
    if (isAttachmentSpeedEditing)
        DrawRectangle(722 + MeasureText(attachmentSpeedInput, 15), 230, 2, 16, PURPLE);
    DrawRectangle(816, 226, 38, 24, MAROON);
    DrawText("-", 830, 227, 20, RAYWHITE);
    DrawRectangle(864, 226, 38, 24, DARKGREEN);
    DrawText("+", 877, 227, 20, RAYWHITE);
    DrawRectangle(716, 258, 188, 26, DARKBLUE);
    DrawText("Preview shot", 760, 263, 16, RAYWHITE);
    DrawText(TextFormat("Preview files: %d", attachment->previewFileCount), 716, 304, 16, isUnsaved ? MAROON : DARKGRAY);
    DrawRectangle(816, 294, 38, 24, MAROON);
    DrawText("-", 830, 295, 20, RAYWHITE);
    DrawRectangle(864, 294, 38, 24, DARKGREEN);
    DrawText("+", 877, 295, 20, RAYWHITE);
    DrawRectangle(716, 326, 94, 24, isAttachmentCapacityEditing ? Fade(SKYBLUE, 0.60f) : Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 326, 94, 24, isAttachmentCapacityEditing ? PURPLE : (isUnsaved ? MAROON : GRAY));
    DrawText(isAttachmentCapacityEditing ? attachmentCapacityInput :
             TextFormat("%llu B", attachment->previewTotalBytes), 720, 330, 15, DARKBLUE);
    if (isAttachmentCapacityEditing)
        DrawRectangle(722 + MeasureText(attachmentCapacityInput, 15), 330, 2, 16, PURPLE);
    DrawRectangle(816, 326, 38, 24, MAROON);
    DrawText("-", 830, 327, 20, RAYWHITE);
    DrawRectangle(864, 326, 38, 24, DARKGREEN);
    DrawText("+", 877, 327, 20, RAYWHITE);
    (void)isPathEditing;
    DrawText("Block mode: drag trajectory", 716, 368, 15, DARKBLUE);
    DrawText(isUnsaved ? "Unsaved - Save all: S" : "Save all: S", 716, 396, 15,
             isUnsaved ? MAROON : DARKGRAY);
}

static void DrawGlobalSettingsPanel(const RpgLayout *layout, const EditorSaveSnapshot *savedSnapshot,
                                    bool isOpen)
{
    DrawRectangleRec(globalSettingsButtonBounds, isOpen ? DARKBLUE : GRAY);
    DrawRectangleLinesEx(globalSettingsButtonBounds, 1.0f, RAYWHITE);
    DrawText("Settings", 28, 16, 16, RAYWHITE);
    if (!isOpen) return;
    DrawRectangleRec(globalSettingsPanelBounds, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(globalSettingsPanelBounds, 2.0f, DARKBLUE);
    DrawText("Global settings", 26, 88, 18, DARKBLUE);
    Color delayColor = layout->electricCellDelay != savedSnapshot->layout.electricCellDelay ? MAROON : DARKGRAY;
    DrawText(TextFormat("Electric delay: %.2fs", layout->electricCellDelay), 26, 116, 16, delayColor);
    DrawRectangle(26, 142, 44, 26, MAROON);
    DrawText("-", 43, 146, 20, RAYWHITE);
    DrawRectangle(82, 142, 44, 26, DARKGREEN);
    DrawText("+", 98, 146, 20, RAYWHITE);
    DrawText("Save all: S", 142, 148, 14, delayColor);
    DrawText("Delay before next wire cell", 26, 174, 14, DARKGRAY);
}

// 現在のエリアだけを対象にした管理パネル。削除操作はここに閉じ込める。
static void DrawAreaInspectorPanel(const RpgStage *stage, int mapIndex, bool isOpen)
{
    DrawRectangleRec(areaInspectorButtonBounds, isOpen ? DARKBLUE : GRAY);
    DrawRectangleLinesEx(areaInspectorButtonBounds, 1.0f, RAYWHITE);
    DrawText("Area", 132, 16, 16, RAYWHITE);
    if (!isOpen) return;
    DrawRectangleRec(areaInspectorPanelBounds, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(areaInspectorPanelBounds, 2.0f, DARKBLUE);
    DrawText(TextFormat("Area %d  (%d, %d)", mapIndex + 1,
                        stage->mapGridX[mapIndex], stage->mapGridY[mapIndex]),
             126, 54, 18, DARKBLUE);
    DrawText(TextFormat("Areas: %d", RpgStage_GetMapCount(stage)), 126, 80, 15, DARKGRAY);
    DrawRectangle(126, 108, 202, 26, MAROON);
    DrawText("Delete this area", 158, 113, 16, RAYWHITE);
}

static Rectangle GetBlockInventoryCell(int index)
{
    return (Rectangle){ 305.0f + index * 33.0f, 494.0f, 32.0f, 32.0f };
}

static Rectangle GetBlockInventoryListItem(int index)
{
    return (Rectangle){ 24.0f, 486.0f - (index + 1) * 32.0f, 260.0f, 32.0f };
}

static void DrawBlockInventory(int selectedInventory, int selectedBlockType, bool isListOpen, Texture2D fileTexture)
{
    const RpgBlockInventory *inventory = RpgBlockInventory_Get(selectedInventory);
    // インベントリ名とブロック枠を分け、名前の操作がブロック選択へ干渉しないようにする。
    DrawRectangle(24, 486, 260, 48, Fade(BLACK, 0.55f));
    DrawRectangleLines(24, 486, 260, 48, RAYWHITE);
    DrawText("<", 34, 501, 18, RAYWHITE);
    DrawText(inventory->name, 62, 502, 16, RAYWHITE);
    DrawText(">", 254, 501, 18, RAYWHITE);
    DrawRectangle(300, 486, 340, 48, Fade(BLACK, 0.55f));
    DrawRectangleLines(300, 486, 340, 48, RAYWHITE);
    for (int index = 0; index < inventory->count; index++) {
        Rectangle cell = GetBlockInventoryCell(index);
        int blockType = inventory->blockTypes[index];
        DrawRectangleRec(cell, inventory->isProperty || inventory->isAttachment ?
                         Fade(DARKGRAY, 0.9f) : RpgStage_GetBlockColor(blockType));
        if (blockType == RPG_BLOCK_REFERENCE_FILE && fileTexture.id != 0)
            DrawTexturePro(fileTexture, (Rectangle){ 0.0f, 0.0f, (float)fileTexture.width, (float)fileTexture.height },
                           (Rectangle){ cell.x + 3.0f, cell.y + 3.0f, cell.width - 6.0f, cell.height - 6.0f },
                           (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        if (blockType == RPG_BLOCK_PROPERTY_ITEM)
            DrawPoly((Vector2){ cell.x + 16.0f, cell.y + 16.0f }, 5, 11.0f, -90.0f, GOLD);
        if (blockType == RPG_BLOCK_PROPERTY_WIRE) {
            DrawLineEx((Vector2){ cell.x + 5.0f, cell.y + 16.0f },
                       (Vector2){ cell.x + 27.0f, cell.y + 16.0f }, 4.0f, SKYBLUE);
            DrawCircle((int)cell.x + 5, (int)cell.y + 16, 4.0f, DARKGREEN);
            DrawCircle((int)cell.x + 27, (int)cell.y + 16, 4.0f, MAROON);
        }
        if (blockType == RPG_BLOCK_PROPERTY_RECEIVER) {
            DrawRectangle((int)cell.x + 6, (int)cell.y + 13, 20, 7, DARKBROWN);
            DrawRectangleLinesEx((Rectangle){ cell.x + 6.0f, cell.y + 13.0f, 20.0f, 7.0f }, 2.0f, GOLD);
            DrawRectangle((int)cell.x + 9, (int)cell.y + 15, 14, 3, Fade(BLACK, 0.72f));
        }
        if (blockType == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
            DrawCircle((int)cell.x + 16, (int)cell.y + 18, 7.0f, DARKBLUE);
            DrawLine((int)cell.x + 16, (int)cell.y + 18, (int)cell.x + 16, (int)cell.y + 5, GOLD);
            DrawCircleLines((int)cell.x + 16, (int)cell.y + 5, 6.0f, SKYBLUE);
        }
        if (blockType == RPG_BLOCK_ATTACHMENT_DATA_BUTTON) {
            DrawRectangle((int)cell.x + 4, (int)cell.y + 19, 24, 7, DARKGRAY);
            DrawCircle((int)cell.x + 16, (int)cell.y + 17, 6.0f, RED);
        }
        if (blockType == RPG_BLOCK_EFFECT_BOUNCE) DrawCircle((int)(cell.x + 16.0f), (int)(cell.y + 16.0f), 8.0f, RAYWHITE);
        if (blockType == RPG_BLOCK_EFFECT_SLOW) DrawTriangle((Vector2){ cell.x + 8.0f, cell.y + 24.0f },
                                                             (Vector2){ cell.x + 24.0f, cell.y + 16.0f },
                                                             (Vector2){ cell.x + 8.0f, cell.y + 8.0f }, RAYWHITE);
        if (blockType == RPG_BLOCK_EFFECT_WIDE_BOUNCE) {
            DrawRectangle((int)cell.x + 4, (int)cell.y + 12, 28, 9, RAYWHITE);
            DrawCircle((int)cell.x + 10, (int)cell.y + 16, 5.0f, ORANGE);
            DrawCircle((int)cell.x + 25, (int)cell.y + 16, 5.0f, ORANGE);
        }
        if (blockType == RPG_BLOCK_EFFECT_CORNER_BOUNCE) {
            DrawLineEx((Vector2){ cell.x + 7.0f, cell.y + 8.0f }, (Vector2){ cell.x + 26.0f, cell.y + 8.0f }, 3.0f, RAYWHITE);
            DrawLineEx((Vector2){ cell.x + 7.0f, cell.y + 8.0f }, (Vector2){ cell.x + 7.0f, cell.y + 26.0f }, 3.0f, RAYWHITE);
        }
        if (blockType == RPG_BLOCK_DOOR_CLOSED_TOP) {
            DrawRectangleLinesEx((Rectangle){ cell.x + 10.0f, cell.y + 4.0f, 12.0f, 26.0f }, 2.0f, GOLD);
        }
        if (blockType == RPG_BLOCK_EFFECT_BUTTON) {
            DrawRectangle((int)cell.x + 5, (int)cell.y + 20, 22, 8, DARKGRAY);
            DrawRectangleLinesEx((Rectangle){ cell.x + 5.0f, cell.y + 20.0f, 22.0f, 8.0f }, 2.0f, RAYWHITE);
            DrawCircle((int)cell.x + 16, (int)cell.y + 19, 5.0f, RED);
        }
        DrawRectangleLinesEx(cell, blockType == selectedBlockType ? 3.0f : 1.0f,
                             blockType == selectedBlockType ? GOLD : RAYWHITE);
    }
    if (isListOpen) for (int index = 0; index < RpgBlockInventory_Count(); index++) {
        Rectangle row = GetBlockInventoryListItem(index);
        DrawRectangleRec(row, index == selectedInventory ? Fade(DARKBLUE, 0.92f) : Fade(BLACK, 0.82f));
        DrawRectangleLinesEx(row, 1.0f, RAYWHITE);
        DrawText(RpgBlockInventory_Get(index)->name, 316, (int)row.y + 7, 16, RAYWHITE);
    }
}

// 既存の設置物ゴーストと同様、特殊ブロックは配置確定前にカーソル位置へ半透明で表示する。
static void DrawEffectBlockDragGhost(const RpgStage *stage, int rootRow, int rootColumn, Vector2 pointer)
{
    const RpgEffectShape *shape;
    if (stage == NULL || rootRow < 0 || rootColumn < 0 ||
        rootRow >= RPG_STAGE_ROWS || rootColumn >= RPG_STAGE_WORLD_COLUMNS) return;
    shape = RpgBlockInventory_GetEffectShape(stage->blocks[rootRow][rootColumn]);
    if (shape == NULL) return;
    Vector2 origin = { pointer.x - RPG_STAGE_TILE_SIZE * 0.5f, pointer.y - RPG_STAGE_TILE_SIZE * 0.5f };
    for (int index = 0; index < shape->cellCount; index++) {
        const RpgEffectShapeCell *cell = &shape->cells[index];
        int sourceRow = rootRow + cell->offsetY;
        int sourceColumn = rootColumn + cell->offsetX;
        // 通常時に縮んでいる伸縮ブロックは、存在している根元マスだけをゴースト化する。
        if (sourceRow < 0 || sourceRow >= RPG_STAGE_ROWS || sourceColumn < 0 ||
            sourceColumn >= RPG_STAGE_WORLD_COLUMNS || stage->blocks[sourceRow][sourceColumn] != cell->blockType) continue;
        Rectangle cellBounds = { origin.x + cell->offsetX * RPG_STAGE_TILE_SIZE,
                                 origin.y + cell->offsetY * RPG_STAGE_TILE_SIZE,
                                 RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        DrawRectangleRec(cellBounds, Fade(RpgStage_GetBlockColor(cell->blockType), 0.70f));
        DrawRectangleLinesEx(cellBounds, 2.0f, PURPLE);
    }
    DrawText("MOVE", (int)origin.x + 3, (int)origin.y + 17, 12, RAYWHITE);
}

static void DrawEditor(const RpgCharacter *player, const RpgCharacter *npc, const RpgStage *stage,
                       const RpgLayout *layout, const RpgStage3Event *stage3Event,
                       const RpgZipper *zipper,
                       Texture2D zipperTexture, Texture2D fileTexture,
                       const RpgDialogue *dialogue, int selected, int mapIndex, bool blockMode,
                       int dialogueScroll, int activeDialogueLine, int dialogueCursorIndex,
                       int selectionAnchor, int selectionEnd, int draggedDialogueLine,
                       bool isDialogueEditorOpen, int dialogueBlockHeight,
                       int dialogueFontSize, bool isSpeakerEditing, bool isStage3DialogueEditing,
                       bool isInspectDialogueEditing, bool isMoveFunctionEditorOpen, bool isMovePreviewPlaying, float previewStartX, float movePreviewSpriteX, int inspectFunctionIndex, bool isExamineFunctionListOpen, bool isFunctionTypeListOpen, int draggedInspectFunction, bool isInspectTitleEditing, int titleCursorIndex, int titleSelectionAnchor, int titleSelectionEnd, int speakerCursorIndex, int speakerSelectionAnchor, int speakerSelectionEnd,
                       const char *message, bool isExitConfirmationOpen, bool isExitDetailsOpen,
                       int detailScroll, const EditorSaveSnapshot *savedSnapshot, const RpgItems *items,
                       const RpgItems *savedItems,
                       bool eventPlacementMode, int selectedItemIndex, bool isItemNameEditing, int itemNameCursorIndex, int itemNameSelectionAnchor,
                       int itemNameSelectionEnd, int selectedDoorRow, int selectedDoorColumn,
                       int selectedAttachmentIndex, bool isAttachmentPathEditing,
                       int selectedInventory, int selectedBlockType,
                       bool isBlockInventoryListOpen,
                       bool isZipperPointerFeedbackSuppressed,
                       int selectedReferenceRow, int selectedReferenceColumn, bool isReferencePathEditing,
                       bool isReferencePointerFeedbackSuppressed,
                        int referencePathCursorIndex, int referencePathSelectionAnchor,
                        int referencePathSelectionEnd, bool isGlobalSettingsOpen, bool isAreaInspectorOpen)
{
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
    RpgStage_DrawMapReferenceObjects(stage, mapIndex, fileTexture);
    if (isReferenceDragPreviewVisible) {
        Rectangle ghostBounds = { referenceDragPointer.x - RPG_STAGE_TILE_SIZE * 0.5f,
                                  referenceDragPointer.y - RPG_STAGE_TILE_SIZE * 0.5f,
                                  RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        RpgStage_DrawReferenceObject(fileTexture, ghostBounds, Fade(WHITE, 0.70f));
    }
    DrawEditorItems(items, mapIndex);
    DrawEditorMapEvents(mapIndex);
    DrawRectangle(0, 400, RPG_EDITOR_WIDTH, 14, DARKGREEN);
    RpgStage_DrawMapEffects(stage, mapIndex);
    RpgSignalBlocks_DrawPreview(&signalBlocks, stage, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    if (isEffectBlockDragPreviewVisible)
        DrawEffectBlockDragGhost(stage, effectBlockDragPreviewRow, effectBlockDragPreviewColumn,
                                 effectBlockDragPointer);
    RpgWires_DrawMap(&wires, stage, mapIndex);
    RpgWires_DrawElectric(&wires, &attachmentPreviewShots,
                          mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    RpgReceivers_DrawMap(&receivers, mapIndex);
    RpgAttachments_DrawMapExcept(&attachments, mapIndex, attachmentDragDrawSkipIndex);
    RpgDataShots_DrawMap(&attachmentPreviewShots, mapIndex);
    if (blockMode) RpgAttachments_DrawDataPaths(&attachments, mapIndex);
    if (isAttachmentDragPreviewVisible) {
        Vector2 previewPosition = attachmentDragPointer;
        RpgGridCell previewOuterCell = RpgGridPath_GetSideNeighbor(attachmentDragPreview.cell,
                                                                    attachmentDragPreview.side);
        bool isPreviewInMap = isAttachmentDragPreviewSnapped &&
                              previewOuterCell.column >= mapIndex * RPG_STAGE_COLUMNS &&
                              previewOuterCell.column < (mapIndex + 1) * RPG_STAGE_COLUMNS;
        if (isPreviewInMap)
            previewPosition = RpgAttachments_GetPosition(&attachmentDragPreview,
                                                         mapIndex * RPG_STAGE_COLUMNS);
        else previewPosition.x -= mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
        RpgAttachments_DrawGhost(attachmentDragPreview.type, previewPosition,
                                 attachmentDragPreview.side, isPreviewInMap);
    }
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
    DrawZipperLaunchPreview(zipperTexture, zipper, mapIndex);
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
    if (mapIndex == 2) {
        RpgCharacter localZipper = zipper->character;
        localZipper.position.x -= mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
        Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&localZipper, 380.0f);
        RpgZipper_DrawPointerFeedback(zipperBounds,
                                      CheckCollisionPointRec(GetMousePosition(), zipperBounds) && !isZipperPointerFeedbackSuppressed,
                                      selected == 3 && !isZipperPointerFeedbackSuppressed);
    }
    // FILE.png もZipperと同じWindows風のホバー・選択表示を使う。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int localColumn = 0; localColumn < RPG_STAGE_COLUMNS; localColumn++) {
        int column = mapIndex * RPG_STAGE_COLUMNS + localColumn;
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        Rectangle bounds = { localColumn * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                             RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        bool isSelectedReference = selected == 7 && selectedReferenceRow == row &&
                                   selectedReferenceColumn == column && !isReferencePointerFeedbackSuppressed;
        RpgZipper_DrawPointerFeedback(bounds, CheckCollisionPointRec(GetMousePosition(), bounds) &&
                                      !isReferencePointerFeedbackSuppressed, isSelectedReference);
    }
    // ブロック編集中はキャンバス全面を導線・軌道の操作領域として使えるようにする。
    if (!blockMode) {
        DrawGlobalSettingsPanel(layout, savedSnapshot, isGlobalSettingsOpen);
        DrawAreaInspectorPanel(stage, mapIndex, isAreaInspectorOpen);
    }
    DrawText("RPG Editor", 120, 16, 20, DARKGRAY);
    DrawText(TextFormat("Map %d", mapIndex + 1), 230, 16, 20, MAROON);
    DrawText(blockMode ? "Block: left place / drag Effect or Wire / right erase / B exit / S save" :
             "B: Block mode    S: Save all    Esc: Deselect", 24, 48, 18, DARKGRAY);
    if (!blockMode) {
        DrawRectangle(280, 52, 112, 24, eventPlacementMode ? ORANGE : DARKBLUE);
        DrawText(eventPlacementMode ? "Place event" : "Add event", 292, 57, 15, RAYWHITE);
    }
    if (blockMode) DrawBlockInventory(selectedInventory, selectedBlockType, isBlockInventoryListOpen, fileTexture);
    bool hasUnsavedChanges = HasAnyUnsavedChanges(savedSnapshot, player, npc, layout, stage, dialogue, stage3Event,
                                                   items, savedItems);
    DrawRectangleRec(revertSavedBounds, hasUnsavedChanges ? MAROON : GRAY);
    DrawText("Revert saved", 426, 48, 16, RAYWHITE);
    if (AreItemsDifferent(items, savedItems)) DrawText("Items: unsaved", 704, 48, 16, MAROON);
    if (AreWiresDifferent(&wires, &savedWires)) DrawText("Wires: unsaved", 704, 66, 16, MAROON);
    if (AreReceiversDifferent(&receivers, &savedReceivers)) DrawText("Receivers: unsaved", 704, 84, 16, MAROON);
    if (AreAttachmentsDifferent(&attachments, &savedAttachments)) DrawText("Attachments: unsaved", 704, 102, 16, MAROON);
    if (mapIndex == 2 && !blockMode) {
        DrawStage3EventPanel(stage3Event);
    }
    DrawText(message, 580, 48, 18, saveState == EDITOR_SAVE_SUCCEEDED ? DARKGREEN : MAROON);
    bool isModalOpen = isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen ||
                       isMoveFunctionEditorOpen;
    if (!blockMode && !isModalOpen && selected >= 1 && selected <= 7 &&
        !(selected == 6 && isAttachmentPathDragVisualActive)) {
        Camera2D inspectorCamera = { .offset = inspectorOffsets[selected], .target = { 0.0f, 0.0f }, .zoom = 1.0f };
        BeginMode2D(inspectorCamera);
    if (selected == 1) {
        DrawInspectorFrame(playerInspectorBounds, "Player Inspector", DARKBLUE, GetInspectorCloseButton(1));
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
    } else if (selected == 2) {
        DrawNpcSummaryInspector(dialogue, npc, &savedSnapshot->npc, saveState);
    } else if (selected == 3) {
        DrawZipperInspector(zipper, &savedSnapshot->zipper, saveState);
    } else if (selected == 4) {
        DrawItemInspector(items, savedItems, selectedItemIndex, isItemNameEditing, itemNameCursorIndex,
                          itemNameSelectionAnchor, itemNameSelectionEnd);
    } else if (selected == 5) {
        int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, stage, selectedDoorRow, selectedDoorColumn);
        if (signalIndex >= 0) DrawSignalShrinkInspector(signalIndex);
        else DrawDoorInspector(stage, &savedSnapshot->stage, selectedDoorRow, selectedDoorColumn);
    } else if (selected == 6) {
        DrawAttachmentInspector(selectedAttachmentIndex, isAttachmentPathEditing);
    } else if (selected == 7) {
        DrawReferenceInspector(stage, &savedSnapshot->stage, selectedReferenceRow, selectedReferenceColumn,
                               isReferencePathEditing, referencePathCursorIndex,
                               referencePathSelectionAnchor, referencePathSelectionEnd);
    }
        EndMode2D();
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
                                titleCursorIndex, titleSelectionAnchor, titleSelectionEnd, savedInspect);
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
                                                      dialogue, stage3Event, items, savedItems, detailScroll);
    EndDrawing();
}

int main(void)
{
    const Rectangle area = { 32.0f, 80.0f, 896.0f, 320.0f };
    InitWindow(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, "1_44MB - RPG Editor");
    SetExitKey(KEY_NULL);
    InstallEditorCloseHandler();
    SetTargetFPS(60);
    // エディター起動時にも、プレイ中に残った一時objectフォルダとInboxを掃除する。
    RpgObjectFolders_ClearSessionStorage();
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));
    Texture2D zipperTexture = LoadTexture(TextFormat("%s../assets/Sprite/ZIPPER.png",
                                                      GetApplicationDirectory()));
    Texture2D fileTexture = LoadTexture(TextFormat("%s../assets/Sprite/FILE.png",
                                                    GetApplicationDirectory()));
    RpgLayout layout = RpgLayout_Default();
    RpgLayout_Load(TextFormat("%s../assets/Settings/Stage/rpg_layout.cfg", GetApplicationDirectory()), &layout);
    RpgStage stage = RpgStage_Default();
    RpgStage_Load(TextFormat("%s../assets/Settings/Stage/rpg_stage.cfg", GetApplicationDirectory()), &stage);
    // 保存済みFileの日本語パスも、選択前から ? にならないようフォントへ登録する。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (stage.blocks[row][column] == RPG_BLOCK_REFERENCE_FILE)
            GameFont_AddText(RpgStage_GetReferencePathAtCell(&stage, row, column));
    wires = RpgWires_Default();
    RpgWires_Load(TextFormat("%s../assets/Settings/Stage/rpg_wires.cfg", GetApplicationDirectory()), &wires);
    RpgWires_RemoveBroken(&wires, &stage);
    savedWires = wires;
    receivers = RpgReceivers_Default();
    RpgReceivers_Load(TextFormat("%s../assets/Settings/Stage/rpg_receivers.cfg", GetApplicationDirectory()), &receivers);
    RpgReceivers_RemoveBroken(&receivers, &stage);
    savedReceivers = receivers;
    attachments = RpgAttachments_Default();
    RpgAttachments_Load(TextFormat("%s../assets/Settings/Stage/rpg_attachments.cfg", GetApplicationDirectory()),
                        &attachments);
    RpgAttachments_MigrateLegacyButtons(&attachments, &stage);
    RpgAttachments_RemoveBroken(&attachments, &stage);
    RpgObjectFolders_PrepareAttachmentFolders(&attachments);
    savedAttachments = attachments;
    signalBlocks = RpgSignalBlocks_Default();
    RpgSignalBlocks_Load(TextFormat("%s../assets/Settings/Stage/rpg_signal_blocks.cfg", GetApplicationDirectory()),
                         &signalBlocks);
    RpgSignalBlocks_RemoveBroken(&signalBlocks, &stage);
    savedSignalBlocks = signalBlocks;
    previewStage = &stage;
    attachmentPreviewShots = RpgDataShots_Default();
    previewEvent = RpgPreviewEvent_Default();
    previewSystem = RpgPreviewSystem_Default();
    RpgPreviewSystem_Register(&previewSystem, PreviewRadioEmitters, NULL);
    RpgPreviewSystem_Register(&previewSystem, PreviewSignalShrinkBlocks, &signalBlocks);
    RpgItems items = RpgItems_Default();
    RpgItems_Load(TextFormat("%s../assets/Settings/Stage/rpg_items.cfg", GetApplicationDirectory()), &items);
    mapEvents = RpgMapEvents_Default();
    RpgMapEvents_Load(TextFormat("%s../assets/Settings/Stage/rpg_map_events.cfg", GetApplicationDirectory()), &mapEvents);
    savedMapEvents = mapEvents;
    for (int itemIndex = 0; itemIndex < items.count; itemIndex++) GameFont_AddText(items.entries[itemIndex].name);
    RpgStage3Event stage3Event = RpgStage3Event_Default();
    RpgStage3Event_Load(TextFormat("%s../assets/Settings/Stage/rpg_stage3_event.cfg",
                                   GetApplicationDirectory()), &stage3Event);
    zipperData = RpgZipper_Default();
    RpgZipper_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg", GetApplicationDirectory()), &zipperData);
    npcInspect = RpgInspect_Default("Inspect", "Nothing unusual here.");
    RpgInspect_Load(TextFormat("%s../assets/Settings/Stage/rpg_inspect.cfg", GetApplicationDirectory()), &npcInspect);
    for (int functionIndex = 0; functionIndex < npcInspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < npcInspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    activeInspect = &zipperInspectData;
    RpgInspect_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg", GetApplicationDirectory()), &npcInspect);
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
    RpgDialogue_Load(TextFormat("%s../assets/Settings/Stage/rpg_dialogue.txt",
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
    int titleSelectionAnchor = 0;
    int titleSelectionEnd = 0;
    ModalHistory modalHistory = { 0 };
    int dialogueFontSize = 22;
    int dialogueBlockHeight = dialogueFontSize + 10;
    bool blockMode = false;
    bool eventPlacementMode = false;
    bool isZipperPointerFeedbackSuppressed = false;
    bool isReferencePointerFeedbackSuppressed = false;
    int draggedMapEventIndex = -1;
    int selectedBlockInventory = 0;
    int selectedBlockType = 1;
    int selectedReferenceRow = -1;
    int selectedReferenceColumn = -1;
    bool isReferencePathEditing = false;
    bool isReferencePathPointerHeld = false;
    int referencePathCursorIndex = 0;
    int referencePathSelectionAnchor = 0;
    int referencePathSelectionEnd = 0;
    bool isBlockInventoryListOpen = false;
    bool isBlockInventoryPointerHeld = false;
    bool isInspectorPointerHeld = false;
    bool isPropertyPlacementPending = false;
    int pendingPropertyBlockType = 0;
    bool isWireEndpointDragActive = false;
    int draggedWireIndex = -1;
    bool draggedWireStart = false;
    int draggedWireLastRow = -1;
    int draggedWireLastColumn = -1;
    RpgWire draggedWireBeforeEdit = { 0 };
    bool hasRecordedDraggedWireHistory = false;
    bool isReceiverClickPending = false;
    int pendingReceiverIndex = -1;
    Vector2 receiverPressPosition = { 0.0f, 0.0f };
    RpgEditorDrag attachmentDrag = { 0 };
    int draggedAttachmentIndex = -1;
    RpgAttachment draggedAttachmentBeforeEdit = { 0 };
    bool isAttachmentErasePointerHeld = false;
    RpgEditorDrag effectBlockDrag = { 0 };
    int draggedEffectBlockRow = -1;
    int draggedEffectBlockColumn = -1;
    int pendingEffectBlockRow = -1;
    int pendingEffectBlockColumn = -1;
    RpgEditorDrag characterDrag = { 0 };
    int draggedCharacterKind = 0;
    RpgEditorDrag referenceDrag = { 0 };
    int draggedReferenceRow = -1;
    int draggedReferenceColumn = -1;
    int selectedItemIndex = -1;
    int selectedAttachmentIndex = -1;
    bool isAttachmentPathEditing = false;
    bool isAttachmentPathDragActive = false;
    int selectedDoorRow = -1;
    int selectedDoorColumn = -1;
    bool isItemNameEditing = false;
    int itemNameCursorIndex = 0;
    int itemNameSelectionAnchor = 0;
    int itemNameSelectionEnd = 0;
    const char *message = "Select a character";
    static EditorSaveSnapshot savedSnapshot;
    RpgItems savedItems = items;
    UpdateSaveSnapshot(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event);
    // 50件の編集状態は大きいため、起動スタックではなく静的領域に確保する。
    BlockHistory blockHistory = { 0 };
    bool shouldExit = false;
    bool isExitConfirmationOpen = false;
    bool isExitDetailsOpen = false;
    int exitDetailsScroll = 0;
    bool isInspectorDragActive = false;
    Vector2 inspectorDragGrab = { 0.0f, 0.0f };
    bool isGlobalSettingsOpen = false;
    bool isGlobalSettingsPointerHeld = false;
    bool isAreaInspectorOpen = false;
    bool isAreaInspectorPointerHeld = false;

    while (!shouldExit) {
        if (draggedMapEventIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) draggedMapEventIndex = -1;
        bool blockEditedThisFrame = false;
        UpdateZipperLaunchPreview(&stage, GetFrameTime());
        // プレビューも実フォルダの集計値を使うが、消滅時に File.png は生成せず一時フォルダだけを破棄する。
        RpgObjectFolders_UpdateDataShotLifetimes(&attachmentPreviewShots, &attachments, NULL);
        RpgPreviewSystem_Dispatch(&previewSystem, &previewEvent);
        RpgSignalBlocks_Update(&signalBlocks, &stage, NULL, GetFrameTime());
        RpgDataShots_Update(&attachmentPreviewShots, &attachments, &stage, &receivers, &wires,
                             layout.electricCellDelay, GetFrameTime(), true);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            isBlockInventoryPointerHeld = false;
            isInspectorPointerHeld = false;
            isReferencePathPointerHeld = false;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) isAttachmentErasePointerHeld = false;
        if (isEditorCloseRequested && !isExitConfirmationOpen) {
            // ネイティブの閉じる通知を保留し、未保存データがある時だけ確認画面へ移る。
            isEditorCloseRequested = false;
            if (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                     &items, &savedItems)) {
                isExitConfirmationOpen = true;
                isExitDetailsOpen = false;
                exitDetailsScroll = 0;
            } else shouldExit = true;
        }
        if (shouldExit) break;
        Vector2 mousePosition = GetMousePosition();
        Vector2 inspectorMousePosition = selected >= 1 && selected <= 6 ?
            GetInspectorLocalPointer(mousePosition, selected) : mousePosition;
        bool wasExitConfirmationOpen = isExitConfirmationOpen;
        if (isExitConfirmationOpen && isExitDetailsOpen &&
            CheckCollisionPointRec(mousePosition, GetExitDetailsBounds())) {
            ExitDetailList details = BuildUnsavedDetails(&savedSnapshot, &player, &npc, &stage,
                                                          &dialogue, &stage3Event, &items, &savedItems);
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
                                                &items, &savedSnapshot, &savedItems)) shouldExit = true;
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
        int requestedMapIndex = !isAreaInspectorOpen ? GetRequestedMapIndex(&stage, mapIndex) : -1;
        if (!isDialogueEditing && requestedMapIndex >= 0) {
            mapIndex = requestedMapIndex; selected = 0; activeDialogueLine = -1; isDialogueEditorOpen = false;
            isSpeakerEditing = false; isStage3DialogueEditing = false;
            isInspectDialogueEditing = false; isExamineFunctionListOpen = false; isFunctionTypeListOpen = false; isMoveFunctionEditorOpen = false; isMovePreviewPlaying = false;
            isZipperPointerFeedbackSuppressed = true;
        }
        if (!isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing && IsKeyPressed(KEY_B)) { blockMode = !blockMode; selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; isGlobalSettingsOpen = false; isAreaInspectorOpen = false; isZipperPointerFeedbackSuppressed = true; isReferencePointerFeedbackSuppressed = true; }
        if (!isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing && IsKeyPressed(KEY_ESCAPE)) { selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; isZipperPointerFeedbackSuppressed = true; isReferencePointerFeedbackSuppressed = true; message = "Selection cleared"; }
        if (isDialogueEditorOpen && activeDialogueLine >= 0) {
            UpdateImeCandidateWindow(activeDialogueLine, dialogueScroll);
        }

        bool isPlayerInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 1 &&
                                        CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(1));
        bool isNpcSummaryClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 2 && !isDialogueEditorOpen && !isExamineFunctionListOpen &&
                                   CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(2));
        bool isZipperInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 3 &&
                                        CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(3));
        bool isItemInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 4 &&
                                      CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(4));
        bool isDoorInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 5 &&
                                      CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(5));
        bool isAttachmentInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 6 &&
                                            CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(6));
        bool isReferenceInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 7 &&
                                           CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(7));
        bool isDialogueEditorClicked = isDialogueEditorOpen &&
                                       CheckCollisionPointRec(mousePosition, dialogueEditorBounds);
        bool isInspectorClicked = isPlayerInspectorClicked || isNpcSummaryClicked ||
                                  isZipperInspectorClicked || isItemInspectorClicked || isDoorInspectorClicked || isDialogueEditorClicked;
        isInspectorClicked = isInspectorClicked || isAttachmentInspectorClicked || isReferenceInspectorClicked;
        bool isCloseInspectorClicked = (isPlayerInspectorClicked || isNpcSummaryClicked || isZipperInspectorClicked || isItemInspectorClicked || isDoorInspectorClicked || isAttachmentInspectorClicked || isReferenceInspectorClicked) &&
                                       CheckCollisionPointRec(inspectorMousePosition, GetInspectorCloseButton(selected));
        bool isCloseDialogueEditorClicked = isDialogueEditorClicked &&
                                            CheckCollisionPointRec(mousePosition, GetDialogueEditorCloseButton());
        bool isBackToExamineClicked = isDialogueEditorClicked && isInspectDialogueEditing &&
                                      CheckCollisionPointRec(mousePosition, (Rectangle){ 478, 64, 96, 22 });
        if (isInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) isInspectorPointerHeld = true;
        if (isInspectorDragActive && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isInspectorDragActive = false;
        if (!wasModalOpenAtFrameStart && isInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            !IsInspectorControlPoint(selected, inspectorMousePosition)) {
            // 空白から始めたドラッグだけをパネル移動として扱い、背後のマップへ伝搬させない。
            isInspectorDragActive = true;
            inspectorDragGrab = Vector2Subtract(mousePosition, inspectorOffsets[selected]);
        }
        if (isInspectorDragActive && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Rectangle baseBounds = GetInspectorBounds(selected);
            inspectorOffsets[selected] = Vector2Subtract(mousePosition, inspectorDragGrab);
            inspectorOffsets[selected].x = Clamp(inspectorOffsets[selected].x, -baseBounds.x,
                                                 RPG_EDITOR_WIDTH - baseBounds.x - baseBounds.width);
            inspectorOffsets[selected].y = Clamp(inspectorOffsets[selected].y, -baseBounds.y,
                                                 RPG_EDITOR_HEIGHT - baseBounds.y - baseBounds.height);
            inspectorMousePosition = GetInspectorLocalPointer(mousePosition, selected);
            isInspectorPointerHeld = true;
        }
        bool isStage3ToggleClicked = !wasModalOpenAtFrameStart && mapIndex == 2 && !blockMode && !isDialogueEditorOpen &&
                                      CheckCollisionPointRec(mousePosition, (Rectangle){ 832, 22, 100, 24 });
        bool isGlobalSettingsButtonClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                             CheckCollisionPointRec(mousePosition, globalSettingsButtonBounds);
        bool isGlobalSettingsPanelClicked = !blockMode && isGlobalSettingsOpen &&
                                            CheckCollisionPointRec(mousePosition, globalSettingsPanelBounds);
        bool isAreaInspectorButtonClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                            CheckCollisionPointRec(mousePosition, areaInspectorButtonBounds);
        bool isAreaInspectorPanelClicked = !blockMode && isAreaInspectorOpen &&
                                           CheckCollisionPointRec(mousePosition, areaInspectorPanelBounds);
        if (isGlobalSettingsButtonClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isGlobalSettingsOpen = !isGlobalSettingsOpen;
            isGlobalSettingsPointerHeld = true;
            isAreaInspectorOpen = false;
        } else if (isGlobalSettingsPanelClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isGlobalSettingsPointerHeld = true;
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 26, 142, 44, 26 }))
                layout.electricCellDelay = Clamp(layout.electricCellDelay - 0.01f, 0.01f, 2.0f);
            else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 82, 142, 44, 26 }))
                layout.electricCellDelay = Clamp(layout.electricCellDelay + 0.01f, 0.01f, 2.0f);
        } else if (isAreaInspectorButtonClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isAreaInspectorOpen = !isAreaInspectorOpen;
            isAreaInspectorPointerHeld = true;
            isGlobalSettingsOpen = false;
        } else if (isAreaInspectorPanelClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isAreaInspectorPointerHeld = true;
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 126, 108, 202, 26 })) {
                int nextMapIndex = FindAnyAdjacentMap(&stage, mapIndex);
                if (nextMapIndex >= 0 && RpgStage_RemoveMap(&stage, mapIndex)) {
                    RemoveMapOwnedObjects(mapIndex, &items, &mapEvents, &stage, &wires,
                                          &receivers, &attachments);
                    mapIndex = nextMapIndex;
                    selected = 0;
                    isAreaInspectorOpen = false;
                    message = "Area deleted";
                } else message = "Cannot delete the last area";
            }
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isGlobalSettingsPointerHeld = false;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isAreaInspectorPointerHeld = false;
        bool isRevertSavedClicked = !wasModalOpenAtFrameStart &&
                                    CheckCollisionPointRec(mousePosition, revertSavedBounds);
        bool isEventPlacementControlClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                              CheckCollisionPointRec(mousePosition, (Rectangle){ 280, 52, 112, 24 });
        if (isEventPlacementControlClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            eventPlacementMode = !eventPlacementMode;
            message = eventPlacementMode ? "Click map to place event" : "Event placement cancelled";
        }
        int clickedBlockType = 0;
        bool isBlockInventoryControlClicked = false;
        if (blockMode && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const RpgBlockInventory *inventory = RpgBlockInventory_Get(selectedBlockInventory);
            // ブロック枠はインベントリ名のクリック領域より先に判定し、選択クリックを一覧表示へ渡さない。
            for (int index = 0; index < inventory->count; index++) if (CheckCollisionPointRec(mousePosition, GetBlockInventoryCell(index))) {
                clickedBlockType = inventory->blockTypes[index];
                isBlockInventoryControlClicked = true;
                break;
            }
            if (isBlockInventoryControlClicked) {
                // ブロック選択済み。下のインベントリ切替処理は実行しない。
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 24, 486, 30, 48 })) {
                isBlockInventoryControlClicked = true;
                selectedBlockInventory = (selectedBlockInventory + RpgBlockInventory_Count() - 1) % RpgBlockInventory_Count();
                selectedBlockType = RpgBlockInventory_Get(selectedBlockInventory)->blockTypes[0];
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 254, 486, 30, 48 })) {
                isBlockInventoryControlClicked = true;
                selectedBlockInventory = (selectedBlockInventory + 1) % RpgBlockInventory_Count();
                selectedBlockType = RpgBlockInventory_Get(selectedBlockInventory)->blockTypes[0];
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 54, 486, 200, 48 })) {
                isBlockInventoryControlClicked = true;
                isBlockInventoryListOpen = !isBlockInventoryListOpen;
            } else if (isBlockInventoryListOpen) {
                for (int index = 0; index < RpgBlockInventory_Count(); index++) if (CheckCollisionPointRec(mousePosition, GetBlockInventoryListItem(index))) {
                    selectedBlockInventory = index;
                    selectedBlockType = RpgBlockInventory_Get(index)->blockTypes[0];
                    isBlockInventoryListOpen = false;
                    isBlockInventoryControlClicked = true;
                }
            }
        }
        if (clickedBlockType > 0) {
            selectedBlockType = clickedBlockType;
            if (selectedBlockType == RPG_BLOCK_REFERENCE_FILE) {
                message = "FILE.png selected";
            } else message = TextFormat("Block %d selected", selectedBlockType);
        }
        Rectangle referencePathBounds = { 716.0f, 144.0f, 188.0f, 28.0f };
        bool isReferencePathClicked = isReferenceInspectorClicked &&
                                     CheckCollisionPointRec(inspectorMousePosition, referencePathBounds);
        bool isReferenceFileSelectClicked = isReferenceInspectorClicked &&
                                            CheckCollisionPointRec(inspectorMousePosition,
                                                                   (Rectangle){ 716.0f, 180.0f, 188.0f, 28.0f });
        if (isReferencePathClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const char *path = RpgStage_GetReferencePathAtCell(&stage, selectedReferenceRow, selectedReferenceColumn);
            referencePathCursorIndex = GetCursorIndexAtX(path, inspectorMousePosition.x - 724.0f, 15.0f);
            referencePathSelectionAnchor = referencePathCursorIndex;
            referencePathSelectionEnd = referencePathCursorIndex;
            isReferencePathEditing = true;
            isReferencePathPointerHeld = true;
        }
        if (isReferencePathEditing && selected == 7 && selectedReferenceRow >= 0 && selectedReferenceColumn >= 0) {
            char *path = stage.referencePaths[selectedReferenceRow][selectedReferenceColumn];
            UpdateShortText(path, sizeof(stage.referencePaths[selectedReferenceRow][selectedReferenceColumn]), &referencePathCursorIndex,
                            &referencePathSelectionAnchor, &referencePathSelectionEnd);
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isReferencePathPointerHeld) {
                referencePathSelectionEnd = GetCursorIndexAtX(path, inspectorMousePosition.x - 724.0f, 15.0f);
                referencePathCursorIndex = referencePathSelectionEnd;
            }
            UpdateImeCandidateWindowAt(724 + (int)inspectorOffsets[7].x, 172 + (int)inspectorOffsets[7].y);
        }
        if (isReferenceFileSelectClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selectedReferenceRow >= 0 && selectedReferenceColumn >= 0) {
            char selectedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            // プロジェクトで扱うテキストを集約したFilesフォルダを、選択画面の初期位置にする。
            if (FileDialog_SelectText(selectedPath, sizeof(selectedPath),
                                      TextFormat("%s../assets/Files", GetApplicationDirectory()))) {
                RpgStage_SetReferencePathAtCell(&stage, selectedReferenceRow, selectedReferenceColumn, selectedPath);
                referencePathCursorIndex = (int)strlen(selectedPath);
                referencePathSelectionAnchor = referencePathCursorIndex;
                referencePathSelectionEnd = referencePathCursorIndex;
                isReferencePathEditing = false;
                GameFont_AddText(selectedPath);
                message = "Text file selected";
            } else message = "Text file selection cancelled";
        }
        if (isBlockInventoryControlClicked) isBlockInventoryPointerHeld = true;
        if (isRevertSavedClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                 &items, &savedItems)) {
            RevertToSavedSnapshot(&savedSnapshot, &layout, &player, &npc, &stage, &dialogue, &stage3Event);
            items = savedItems;
            signalBlocks = savedSignalBlocks;
            if (selectedItemIndex >= items.count) {
                selected = 0;
                selectedItemIndex = -1;
                isItemNameEditing = false;
            }
            message = "Reverted to saved";
        }
        if (isStage3ToggleClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            stage3Event.enabled = !stage3Event.enabled;
            message = stage3Event.enabled ? "Stage 3 intro enabled" : "Stage 3 intro disabled";
        }
        if (!wasModalOpenAtFrameStart && mapIndex == 2 && !blockMode && !isDialogueEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
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
                titleSelectionAnchor = titleCursorIndex;
                titleSelectionEnd = titleCursorIndex;
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
            UpdateShortText(npcInspect.functions[inspectFunctionIndex].title, RPG_INSPECT_TITLE_LENGTH,
                            &titleCursorIndex, &titleSelectionAnchor, &titleSelectionEnd);
        }
        if (isExamineFunctionListOpen && isInspectTitleEditing && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 260, 132, 368, 24 })) {
            titleSelectionEnd = GetCursorIndexAtX(npcInspect.functions[inspectFunctionIndex].title,
                                                  mousePosition.x - 268.0f, 16.0f);
            titleCursorIndex = titleSelectionEnd;
        }
        if (isExamineFunctionListOpen && isInspectTitleEditing) UpdateImeCandidateWindowAt(268, 156);
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
            isItemNameEditing = false;
            isAttachmentPathEditing = false;
            isAttachmentPathDragActive = false;
            message = "Inspector closed";
        } else if (isPlayerInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 708, 138, 86, 28 })) player.moveSpeed -= 20.0f;
            else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 810, 138, 86, 28 })) player.moveSpeed += 20.0f;
            else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 800, 158, 48, 26 })) player.scale -= 0.1f;
            else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 852, 158, 48, 26 })) player.scale += 0.1f;
            else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 204, 90, 26 })) {
                message = SaveCharacterSettings(&layout, &player, true, &savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, 204, 90, 26 })) {
                RevertCharacterSettings(&savedSnapshot, &layout, &player, true);
                message = "Reverted to saved";
            }
            if (player.moveSpeed < 60.0f) player.moveSpeed = 60.0f;
            if (player.moveSpeed > 480.0f) player.moveSpeed = 480.0f;
            player.scale = Clamp(player.scale, 0.5f, 3.0f);
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 170, 188, 32 })) {
            isDialogueEditorOpen = true;
            activeDialogueLine = -1;
            isSpeakerEditing = false;
            message = "Dialogue editor opened";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 208, 188, 32 })) {
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
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 246, 90, 26 })) {
            message = SaveCharacterSettings(&layout, &npc, false, &savedSnapshot) ? "Saved" : "Save failed";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, 246, 90, 26 })) {
            RevertCharacterSettings(&savedSnapshot, &layout, &npc, false);
            message = "Reverted to saved";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 800, 136, 48, 26 })) npc.scale = Clamp(npc.scale - 0.1f, 0.5f, 3.0f);
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 852, 136, 48, 26 })) npc.scale = Clamp(npc.scale + 0.1f, 0.5f, 3.0f);
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 800, 262, 48, 26 })) {
            zipperData.character.scale = Clamp(zipperData.character.scale - 0.1f, 0.5f, 3.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 852, 262, 48, 26 })) {
            zipperData.character.scale = Clamp(zipperData.character.scale + 0.1f, 0.5f, 3.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 808, 284, 48, 26 })) {
            zipperData.launchSpeed = Clamp(zipperData.launchSpeed - 60.0f, 120.0f, 2400.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 856, 284, 48, 26 })) {
            zipperData.launchSpeed = Clamp(zipperData.launchSpeed + 60.0f, 120.0f, 2400.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 808, 306, 48, 26 })) {
            zipperData.returnSpeed = Clamp(zipperData.returnSpeed - 30.0f, 60.0f, 1200.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 856, 306, 48, 26 })) {
            zipperData.returnSpeed = Clamp(zipperData.returnSpeed + 30.0f, 60.0f, 1200.0f);
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 330, 188, 28 })) {
            zipperData.launchPreviewEnabled = !zipperData.launchPreviewEnabled;
            isZipperLaunchPreviewVisible = false;
            isZipperLaunchPreviewReturning = false;
            zipperLaunchPreviewCooldown = 1.0f;
            message = zipperData.launchPreviewEnabled ? "Preview enabled" : "Preview disabled";
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 358, 188, 28 })) {
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
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 394, 90, 26 })) {
            message = SaveZipperSettings(&savedSnapshot) ? "Saved" : "Save failed";
        }
        if (isZipperInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, 394, 90, 26 })) {
            zipperData = savedSnapshot.zipper;
            message = "Reverted to saved";
        }
        if (isItemInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && selectedItemIndex >= 0) {
            if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 144, 188, 28 })) {
                isItemNameEditing = true;
                itemNameCursorIndex = GetCursorIndexAtX(items.entries[selectedItemIndex].name,
                                                        inspectorMousePosition.x - 724.0f, 17.0f);
                itemNameSelectionAnchor = itemNameCursorIndex;
                itemNameSelectionEnd = itemNameCursorIndex;
            }
        }
        if (isItemNameEditing && selectedItemIndex >= 0) {
            UpdateShortText(items.entries[selectedItemIndex].name, RPG_ITEM_NAME_LENGTH,
                            &itemNameCursorIndex, &itemNameSelectionAnchor, &itemNameSelectionEnd);
        }
        if (isItemNameEditing && selectedItemIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 144, 188, 28 })) {
            itemNameSelectionEnd = GetCursorIndexAtX(items.entries[selectedItemIndex].name,
                                                      inspectorMousePosition.x - 724.0f, 17.0f);
            itemNameCursorIndex = itemNameSelectionEnd;
        }
        if (isItemNameEditing && selectedItemIndex >= 0) UpdateImeCandidateWindowAt(724, 172);
        if (isDoorInspectorClicked && !isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage, selectedDoorRow, selectedDoorColumn);
            if (signalIndex >= 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, 144, 38, 24 })) {
                signalBlocks.entries[signalIndex].duration = Clamp(signalBlocks.entries[signalIndex].duration - 0.1f, 0.1f, 30.0f);
                message = "Signal shrink duration changed";
            } else if (signalIndex >= 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, 144, 38, 24 })) {
                signalBlocks.entries[signalIndex].duration = Clamp(signalBlocks.entries[signalIndex].duration + 0.1f, 0.1f, 30.0f);
                message = "Signal shrink duration changed";
            } else if (signalIndex >= 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 180, 188, 26 })) {
                bool nextStartsExpanded = !signalBlocks.entries[signalIndex].startsExpanded;
                if (RpgSignalBlocks_SetStartsExpanded(&signalBlocks, &stage, signalIndex, nextStartsExpanded))
                    message = nextStartsExpanded ? "Default changed to expanded" : "Default changed to shrunk";
                else message = "Wait until the signal state ends";
            } else if (signalIndex >= 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 212, 188, 26 })) {
                RpgPreviewEvent_Publish(&previewEvent, RPG_PREVIEW_TARGET_SIGNAL_BLOCK_BASE + signalIndex);
                message = "Signal preview started";
            } else if (signalIndex < 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 144, 86, 28 })) {
                message = SetDoorOpen(&stage, selectedDoorRow, selectedDoorColumn, true) ? "Door opened" : "Door is already open";
            } else if (signalIndex < 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 818, 144, 86, 28 })) {
                message = SetDoorOpen(&stage, selectedDoorRow, selectedDoorColumn, false) ? "Door closed" : "Door is already closed";
            }
        }
        if (isAttachmentInspectorClicked && !isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selectedAttachmentIndex >= 0 && selectedAttachmentIndex < attachments.count) {
            RpgAttachment *attachment = &attachments.entries[selectedAttachmentIndex];
            if (attachment->type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 176, 188, 26 })) {
                    RpgPreviewEvent_Publish(&previewEvent, RPG_PREVIEW_TARGET_ALL);
                    message = "Preview signal: all targets react once";
                }
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 226, 94, 24 })) {
                snprintf(attachmentSpeedInput, sizeof(attachmentSpeedInput), "%.1f",
                         attachment->dataSpeed);
                isAttachmentSpeedEditing = true;
                isAttachmentCapacityEditing = false;
                message = "Enter speed at 100B";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 326, 94, 24 })) {
                snprintf(attachmentCapacityInput, sizeof(attachmentCapacityInput), "%llu",
                         attachment->previewTotalBytes);
                isAttachmentCapacityEditing = true;
                isAttachmentSpeedEditing = false;
                message = "Enter preview capacity in B";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, 194, 38, 24 })) {
                attachment->sizePerFile = Clamp(attachment->sizePerFile - 1.0f, 1.0f, 64.0f);
                message = "Size per file changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, 194, 38, 24 })) {
                attachment->sizePerFile = Clamp(attachment->sizePerFile + 1.0f, 1.0f, 64.0f);
                message = "Size per file changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, 226, 38, 24 })) {
                attachment->dataSpeed = Clamp(attachment->dataSpeed - 10.0f, 1.0f, 480.0f);
                message = "Base speed changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, 226, 38, 24 })) {
                attachment->dataSpeed = Clamp(attachment->dataSpeed + 10.0f, 1.0f, 480.0f);
                message = "Base speed changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 258, 188, 26 })) {
                RpgPreviewEvent_Publish(&previewEvent, selectedAttachmentIndex);
                message = "Emitter preview shot";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, 294, 38, 24 })) {
                if (attachment->previewFileCount > 1) attachment->previewFileCount--;
                message = "Preview files changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, 294, 38, 24 })) {
                if (attachment->previewFileCount < 9999) attachment->previewFileCount++;
                message = "Preview files changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, 326, 38, 24 })) {
                if (attachment->previewTotalBytes >= RPG_DATA_SPEED_BASE_BYTES) attachment->previewTotalBytes -= RPG_DATA_SPEED_BASE_BYTES;
                message = "Preview capacity changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, 326, 38, 24 })) {
                attachment->previewTotalBytes += RPG_DATA_SPEED_BASE_BYTES;
                message = "Preview capacity changed";
            }
        }
        if (isAttachmentCapacityEditing) {
            if (selectedAttachmentIndex < 0 || selectedAttachmentIndex >= attachments.count ||
                attachments.entries[selectedAttachmentIndex].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
                isAttachmentCapacityEditing = false;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                isAttachmentCapacityEditing = false;
                message = "Preview capacity edit cancelled";
            } else if (IsKeyPressed(KEY_ENTER)) {
                unsigned long long bytes = strtoull(attachmentCapacityInput, NULL, 10);
                const unsigned long long maxBytes = 1024ULL * 1024ULL * 1024ULL;
                if (bytes > maxBytes) bytes = maxBytes;
                attachments.entries[selectedAttachmentIndex].previewTotalBytes = bytes;
                isAttachmentCapacityEditing = false;
                message = "Preview capacity changed";
            } else {
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    size_t length = strlen(attachmentCapacityInput);
                    if (length > 0) attachmentCapacityInput[length - 1] = '\0';
                }
                int codepoint = GetCharPressed();
                while (codepoint > 0) {
                    size_t length = strlen(attachmentCapacityInput);
                    if (codepoint >= '0' && codepoint <= '9' && length + 1 < sizeof(attachmentCapacityInput)) {
                        attachmentCapacityInput[length] = (char)codepoint;
                        attachmentCapacityInput[length + 1] = '\0';
                    }
                    codepoint = GetCharPressed();
                }
            }
        }
        if (isAttachmentSpeedEditing) {
            if (selectedAttachmentIndex < 0 || selectedAttachmentIndex >= attachments.count ||
                attachments.entries[selectedAttachmentIndex].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
                isAttachmentSpeedEditing = false;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                isAttachmentSpeedEditing = false;
                message = "Base speed edit cancelled";
            } else if (IsKeyPressed(KEY_ENTER)) {
                float value = strtof(attachmentSpeedInput, NULL);
                attachments.entries[selectedAttachmentIndex].dataSpeed = Clamp(value, 1.0f, 480.0f);
                isAttachmentSpeedEditing = false;
                message = "Base speed changed";
            } else {
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    size_t length = strlen(attachmentSpeedInput);
                    if (length > 0) attachmentSpeedInput[length - 1] = '\0';
                }
                int codepoint = GetCharPressed();
                while (codepoint > 0) {
                    size_t length = strlen(attachmentSpeedInput);
                    bool isDecimalPoint = codepoint == '.' && strchr(attachmentSpeedInput, '.') == NULL;
                    if ((codepoint >= '0' && codepoint <= '9') || isDecimalPoint) {
                        if (length + 1 < sizeof(attachmentSpeedInput)) {
                            attachmentSpeedInput[length] = (char)codepoint;
                            attachmentSpeedInput[length + 1] = '\0';
                        }
                    }
                    codepoint = GetCharPressed();
                }
            }
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

        RpgCharacter localZipperForInput = zipperData.character;
        localZipperForInput.position.x -= 2.0f * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
        Rectangle editorZipperBounds = RpgZipper_GetSpriteBounds(&localZipperForInput, 380.0f);
        bool shouldClearZipperSelection = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                          !isGlobalSettingsButtonClicked && !isGlobalSettingsPanelClicked &&
                                          !isAreaInspectorButtonClicked && !isAreaInspectorPanelClicked &&
                                          !CheckCollisionPointRec(mousePosition, editorZipperBounds);
        if (shouldClearZipperSelection) {
            isZipperPointerFeedbackSuppressed = true;
        }
        bool isUiBlockingMap = wasModalOpenAtFrameStart || isInspectorClicked || isDialogueEditorOpen ||
                               isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen ||
                               isStage3ToggleClicked || isRevertSavedClicked || isBlockInventoryControlClicked ||
                               isBlockInventoryPointerHeld || isInspectorPointerHeld || isEventPlacementControlClicked ||
                               isReferencePathClicked || isReferencePathPointerHeld ||
                               isGlobalSettingsButtonClicked || isGlobalSettingsPanelClicked ||
                               isGlobalSettingsPointerHeld || isAreaInspectorButtonClicked ||
                               isAreaInspectorPanelClicked || isAreaInspectorPointerHeld;
        bool isWirePropertySelected = selectedBlockType == RPG_BLOCK_PROPERTY_WIRE;
        bool isBlockPropertySelected = FindBlockPropertyPlacement(selectedBlockType) != NULL;
        bool isAttachmentSelected = RpgBlockInventory_IsAttachment(selectedBlockType);
        if (blockMode && !isUiBlockingMap && !isAttachmentErasePointerHeld &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && mousePosition.y < 480.0f) {
            Vector2 worldPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mousePosition.y };
            int attachmentIndex = RpgAttachments_FindAtPosition(&attachments, worldPosition, 30.0f);
            if (attachmentIndex >= 0) {
                RpgAttachment removedAttachment = attachments.entries[attachmentIndex];
                if (RpgAttachments_Remove(&attachments, removedAttachment)) {
                    RpgObjectFolder_RemoveAttachmentFolder(&removedAttachment);
                    RpgObjectFolders_PrepareAttachmentFolders(&attachments);
                    PushAttachmentRemovedHistory(&blockHistory, removedAttachment);
                    isAttachmentErasePointerHeld = true;
                    blockEditedThisFrame = true;
                    message = "Attachment removed";
                }
            }
        }
        if (!RpgEditorDrag_IsBusy(&attachmentDrag) && blockMode && !isUiBlockingMap &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            Vector2 worldPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mousePosition.y };
            int clickedAttachmentIndex = RpgAttachments_FindAtPosition(&attachments, worldPosition, 14.0f);
            if (clickedAttachmentIndex >= 0) {
                RpgEditorDrag_Begin(&attachmentDrag, mousePosition);
                draggedAttachmentIndex = clickedAttachmentIndex;
                draggedAttachmentBeforeEdit = attachments.entries[clickedAttachmentIndex];
                attachmentDragPreview = draggedAttachmentBeforeEdit;
                attachmentDragPointer = mousePosition;
                isAttachmentDragPreviewVisible = false;
                attachmentDragDrawSkipIndex = -1;
                message = "Click or drag attachment";
            }
        }
        if (RpgEditorDrag_IsBusy(&attachmentDrag) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            RpgEditorDrag_Update(&attachmentDrag, mousePosition)) {
            isAttachmentDragPreviewVisible = true;
            isAttachmentDragPreviewSnapped = true;
            attachmentDragDrawSkipIndex = draggedAttachmentIndex;
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag attachment near a block edge";
        }
        if (attachmentDrag.active && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                                       IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
            Vector2 worldPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mousePosition.y };
            RpgAttachment snappedAttachment;
            RpgEditorDrag_Update(&attachmentDrag, mousePosition);
            attachmentDragPointer = attachmentDrag.pointerPosition;
            if (RpgAttachments_FindSnap(&stage, draggedAttachmentBeforeEdit.type, worldPosition,
                                        &snappedAttachment)) {
                // スナップ候補は設置情報だけなので、発射設定と経路を移動元から引き継ぐ。
                snappedAttachment.dataSize = draggedAttachmentBeforeEdit.dataSize;
                snappedAttachment.dataSpeed = draggedAttachmentBeforeEdit.dataSpeed;
                snappedAttachment.dataInterval = draggedAttachmentBeforeEdit.dataInterval;
                snappedAttachment.dataPreviewEnabled = draggedAttachmentBeforeEdit.dataPreviewEnabled;
                snappedAttachment.sizePerFile = draggedAttachmentBeforeEdit.sizePerFile;
                snappedAttachment.speedPerKilobyte = draggedAttachmentBeforeEdit.speedPerKilobyte;
                snappedAttachment.previewFileCount = draggedAttachmentBeforeEdit.previewFileCount;
                snappedAttachment.previewTotalBytes = draggedAttachmentBeforeEdit.previewTotalBytes;
                snappedAttachment.dataPath = draggedAttachmentBeforeEdit.dataPath;
                RpgGridCell newStart = RpgGridPath_GetSideNeighbor(snappedAttachment.cell,
                                                                    snappedAttachment.side);
                RpgGridCell oldStart = draggedAttachmentBeforeEdit.dataPath.cells[0];
                int rowOffset = newStart.row - oldStart.row;
                int columnOffset = newStart.column - oldStart.column;
                bool canTranslatePath = true;
                for (int pathIndex = 0; pathIndex < snappedAttachment.dataPath.cellCount; pathIndex++) {
                    RpgGridCell *cell = &snappedAttachment.dataPath.cells[pathIndex];
                    cell->row += rowOffset;
                    cell->column += columnOffset;
                    if (cell->row < 0 || cell->row >= RPG_STAGE_ROWS || cell->column < 0 ||
                        cell->column >= RPG_STAGE_WORLD_COLUMNS)
                        canTranslatePath = false;
                }
                if (!canTranslatePath)
                    snappedAttachment.dataPath = (RpgGridPath){ .cellCount = 1, .cells = { newStart } };
                attachmentDragPreview = snappedAttachment;
                isAttachmentDragPreviewSnapped = true;
            } else {
                attachmentDragPreview = draggedAttachmentBeforeEdit;
                isAttachmentDragPreviewSnapped = false;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (isAttachmentDragPreviewSnapped &&
                    memcmp(&attachments.entries[draggedAttachmentIndex], &attachmentDragPreview,
                           sizeof(RpgAttachment)) != 0) {
                    PushAttachmentChangedHistory(&blockHistory, draggedAttachmentIndex,
                                                 attachments.entries[draggedAttachmentIndex]);
                    RpgObjectFolder_MoveAttachmentFolder(&attachments.entries[draggedAttachmentIndex],
                                                         &attachmentDragPreview);
                    attachments.entries[draggedAttachmentIndex] = attachmentDragPreview;
                    RpgObjectFolders_PrepareAttachmentFolders(&attachments);
                    blockEditedThisFrame = true;
                    message = "Attachment moved";
                } else if (!isAttachmentDragPreviewSnapped) message = "Attachment move cancelled";
                RpgEditorDrag_End(&attachmentDrag);
                draggedAttachmentIndex = -1;
                attachmentDragDrawSkipIndex = -1;
                isAttachmentDragPreviewVisible = false;
            }
        } else if (attachmentDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_End(&attachmentDrag);
            draggedAttachmentIndex = -1;
        }
        if (blockMode && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isUiBlockingMap && mousePosition.y < 480.0f) {
            int pathColumn = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                                   RPG_STAGE_TILE_SIZE);
            int pathRow = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int endpointAttachmentIndex = -1;
                isAttachmentPathDragActive = RpgAttachments_FindDataPathEndpoint(&attachments, pathRow,
                                                                                  pathColumn, &endpointAttachmentIndex);
                if (isAttachmentPathDragActive) {
                    selectedAttachmentIndex = endpointAttachmentIndex;
                    isAttachmentPathDragVisualActive = true;
                }
            }
            if (isAttachmentPathDragActive && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                                                IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
                if (MoveAttachmentPathEndpointToward(&attachments, &stage, selectedAttachmentIndex,
                                                     pathRow, pathColumn)) {
                    blockEditedThisFrame = true;
                    message = "Trajectory endpoint updated";
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    isAttachmentPathDragActive = false;
                    isAttachmentPathDragVisualActive = false;
                }
            }
        }
        // 受容体はブロック配置より先に扱い、クリックと導線ドラッグを常に受け取る。
        if (!isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isReceiverClickPending && !isWireEndpointDragActive && !isUiBlockingMap &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            int receiverColumn = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                                       RPG_STAGE_TILE_SIZE);
            int receiverRow = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            int clickedReceiverIndex = RpgReceivers_FindAtCell(&receivers,
                                                                (RpgGridCell){ receiverRow, receiverColumn });
            if (clickedReceiverIndex >= 0) {
                isReceiverClickPending = true;
                pendingReceiverIndex = clickedReceiverIndex;
                receiverPressPosition = mousePosition;
                message = "Release to change side, drag to edit wire";
            } else if (RpgWires_FindEndpoint(&wires, receiverRow, receiverColumn,
                                               &draggedWireIndex, &draggedWireStart)) {
                // 既存の導線端点は選択中のパレットに関係なく、再ドラッグできる。
                isWireEndpointDragActive = true;
                draggedWireBeforeEdit = wires.entries[draggedWireIndex];
                hasRecordedDraggedWireHistory = false;
                draggedWireLastRow = receiverRow;
                draggedWireLastColumn = receiverColumn;
                message = "Drag wire endpoint to a block";
            }
        }
        // 参照オブジェクトは通常ブロックの設置処理へ渡す前に、単体ドラッグを優先する。
        if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isReceiverClickPending &&
            !isUiBlockingMap && !RpgEditorDrag_IsBusy(&referenceDrag) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            int column = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
                stage.blocks[row][column] == RPG_BLOCK_REFERENCE_FILE) {
                RpgEditorDrag_Begin(&referenceDrag, mousePosition);
                draggedReferenceRow = row;
                draggedReferenceColumn = column;
                isReferenceDragPreviewVisible = false;
                message = "Click or drag reference object";
            }
        }
        // 特殊ブロックの押下は保留し、クリックとドラッグを同じ入口から分ける。
        if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isBlockPropertySelected && !isReceiverClickPending &&
            !isUiBlockingMap && !RpgEditorDrag_IsBusy(&referenceDrag) && !effectBlockDrag.active && !effectBlockDrag.pending && !isWireEndpointDragActive &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            int column = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
                RpgBlockInventory_IsEffectBlock(stage.blocks[row][column])) {
                int rootRow = row;
                int rootColumn = column;
                int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage, row, column);
                if (signalIndex >= 0) {
                    rootRow = signalBlocks.entries[signalIndex].row;
                    rootColumn = signalBlocks.entries[signalIndex].column;
                } else if (!FindEffectBlockRoot(&stage, row, column, &rootRow, &rootColumn, NULL)) {
                    rootRow = -1;
                }
                if (rootRow >= 0) {
                    RpgEditorDrag_Begin(&effectBlockDrag, mousePosition);
                    pendingEffectBlockRow = rootRow;
                    pendingEffectBlockColumn = rootColumn;
                    message = "Click to rotate, drag to move";
                }
            }
        }
        if (effectBlockDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&effectBlockDrag, mousePosition)) {
            draggedEffectBlockRow = pendingEffectBlockRow;
            draggedEffectBlockColumn = pendingEffectBlockColumn;
            pendingEffectBlockRow = -1;
            pendingEffectBlockColumn = -1;
            isEffectBlockDragPreviewVisible = true;
            effectBlockDragPreviewRow = draggedEffectBlockRow;
            effectBlockDragPreviewColumn = draggedEffectBlockColumn;
            effectBlockDragPointer = mousePosition;
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag special block to an empty cell";
        }
        if (effectBlockDrag.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_Update(&effectBlockDrag, mousePosition);
            effectBlockDragPointer = effectBlockDrag.pointerPosition;
        }
        if (referenceDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&referenceDrag, mousePosition)) {
            isReferenceDragPreviewVisible = true;
            referenceDragPointer = mousePosition;
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag reference object to an empty cell";
        }
        if (referenceDrag.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_Update(&referenceDrag, mousePosition);
            referenceDragPointer = referenceDrag.pointerPosition;
        }
        if (isReceiverClickPending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            !isUiBlockingMap &&
            Vector2Distance(mousePosition, receiverPressPosition) >= 5.0f) {
            int receiverWireIndex = FindWireStartingAtReceiver(&wires,
                                                                 &receivers.entries[pendingReceiverIndex]);
            if (receiverWireIndex >= 0) {
                draggedWireIndex = receiverWireIndex;
                draggedWireStart = false;
                draggedWireBeforeEdit = wires.entries[draggedWireIndex];
                hasRecordedDraggedWireHistory = false;
                draggedWireLastRow = wires.entries[draggedWireIndex].path.cells[
                    wires.entries[draggedWireIndex].path.cellCount - 1].row;
                draggedWireLastColumn = wires.entries[draggedWireIndex].path.cells[
                    wires.entries[draggedWireIndex].path.cellCount - 1].column;
                isWireEndpointDragActive = true;
                message = "Drag receiver wire endpoint to a block";
            }
            isReceiverClickPending = false;
            pendingReceiverIndex = -1;
        }
        if (isReceiverClickPending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgReceiver *receiver = &receivers.entries[pendingReceiverIndex];
            RpgGridSide previousSide = receiver->side;
            BlockHistoryEntry receiverHistory = CreateReceiverChangedHistory(pendingReceiverIndex,
                                                                              &receivers, &wires);
            if (RpgReceivers_CycleSide(&receivers, pendingReceiverIndex)) {
                AppendBlockHistory(&blockHistory, receiverHistory);
                for (int wireIndex = 0; wireIndex < wires.count; wireIndex++) {
                    RpgWire *wire = &wires.entries[wireIndex];
                    if (wire->hasReceiverSource && wire->receiverCell.row == receiver->cell.row &&
                        wire->receiverCell.column == receiver->cell.column &&
                        wire->receiverSide == previousSide) wire->receiverSide = receiver->side;
                }
                blockEditedThisFrame = true;
                message = "Receiver side changed";
            }
            isReceiverClickPending = false;
            pendingReceiverIndex = -1;
        }
        if (isWireEndpointDragActive && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            !isUiBlockingMap && mousePosition.y < 480.0f) {
            int destinationColumn = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int destinationRow = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (MoveWireEndpointToward(&wires, &stage, draggedWireIndex, draggedWireStart,
                                       destinationRow, destinationColumn,
                                       &draggedWireLastRow, &draggedWireLastColumn)) {
                if (!hasRecordedDraggedWireHistory) {
                    PushWireChangedHistory(&blockHistory, draggedWireIndex, draggedWireBeforeEdit);
                    hasRecordedDraggedWireHistory = true;
                }
                blockEditedThisFrame = true;
                message = "Wire endpoint updated";
            }
        }
        if (effectBlockDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage,
                                                         pendingEffectBlockRow, pendingEffectBlockColumn);
            if (signalIndex >= 0) {
                if (RpgSignalBlocks_Rotate(&signalBlocks, &stage, signalIndex))
                    message = "Signal shrink block rotated";
                else message = "Rotation needs an empty cell or an idle signal block";
            }
            RpgEditorDrag_End(&effectBlockDrag);
            pendingEffectBlockRow = -1;
            pendingEffectBlockColumn = -1;
        } else if (referenceDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_End(&referenceDrag);
            draggedReferenceRow = -1;
            draggedReferenceColumn = -1;
        } else if (referenceDrag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            int destinationColumn = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int destinationRow = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (!isUiBlockingMap && destinationRow >= 0 && destinationRow < RPG_STAGE_ROWS &&
                destinationColumn >= 0 && destinationColumn < RPG_STAGE_WORLD_COLUMNS &&
                stage.blocks[destinationRow][destinationColumn] == 0 &&
                (destinationRow != draggedReferenceRow || destinationColumn != draggedReferenceColumn)) {
                char movedPath[RPG_STAGE_REFERENCE_PATH_LENGTH];
                snprintf(movedPath, sizeof(movedPath), "%s", RpgStage_GetReferencePathAtCell(&stage, draggedReferenceRow, draggedReferenceColumn));
                PushBlockHistory(&blockHistory, draggedReferenceRow, draggedReferenceColumn,
                                 stage.blocks[draggedReferenceRow][draggedReferenceColumn], NULL);
                PushBlockHistory(&blockHistory, destinationRow, destinationColumn, 0, NULL);
                stage.blocks[draggedReferenceRow][draggedReferenceColumn] = 0;
                stage.blocks[destinationRow][destinationColumn] = RPG_BLOCK_REFERENCE_FILE;
                RpgStage_SetReferencePathAtCell(&stage, destinationRow, destinationColumn, movedPath);
                blockEditedThisFrame = true;
                message = "Reference object moved";
            }
            RpgEditorDrag_End(&referenceDrag);
            draggedReferenceRow = -1;
            draggedReferenceColumn = -1;
            isReferenceDragPreviewVisible = false;
        } else if (effectBlockDrag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (!isUiBlockingMap && mousePosition.y < 480.0f) {
                int destinationColumn = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
                int destinationRow = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
                if (destinationRow >= 0 && destinationRow < RPG_STAGE_ROWS && destinationColumn >= 0 &&
                    destinationColumn < RPG_STAGE_WORLD_COLUMNS &&
                    (destinationRow != draggedEffectBlockRow || destinationColumn != draggedEffectBlockColumn)) {
                    if (MoveSpecialBlock(&stage, &items, &blockHistory, &signalBlocks,
                                         draggedEffectBlockRow, draggedEffectBlockColumn,
                                         destinationRow, destinationColumn)) {
                        RpgWires_RemoveBroken(&wires, &stage);
                        RpgReceivers_RemoveBroken(&receivers, &stage);
                        RpgAttachments_RemoveBroken(&attachments, &stage);
                        blockEditedThisFrame = true;
                        message = "Effect block moved";
                    } else message = "Effect blocks need an empty destination";
                }
            }
            RpgEditorDrag_End(&effectBlockDrag);
            draggedEffectBlockRow = -1;
            draggedEffectBlockColumn = -1;
            isEffectBlockDragPreviewVisible = false;
            effectBlockDragPreviewRow = -1;
            effectBlockDragPreviewColumn = -1;
        } else if (isWireEndpointDragActive && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (!isUiBlockingMap && mousePosition.y < 480.0f) {
                int destinationColumn = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
                int destinationRow = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
                if (MoveWireEndpointToward(&wires, &stage, draggedWireIndex, draggedWireStart,
                                           destinationRow, destinationColumn,
                                           &draggedWireLastRow, &draggedWireLastColumn)) {
                    if (!hasRecordedDraggedWireHistory) {
                        PushWireChangedHistory(&blockHistory, draggedWireIndex, draggedWireBeforeEdit);
                        hasRecordedDraggedWireHistory = true;
                    }
                    blockEditedThisFrame = true;
                    message = "Wire endpoint updated";
                }
            }
            isWireEndpointDragActive = false;
            hasRecordedDraggedWireHistory = false;
            draggedWireIndex = -1;
            draggedWireLastRow = -1;
            draggedWireLastColumn = -1;
        // 導線は始点を選ぶと隣接ブロックへ終点を置き、端点のドラッグで経路を作り直す。
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && isWirePropertySelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending &&
                   !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            int column = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (RpgWires_FindEndpoint(&wires, row, column, &draggedWireIndex, &draggedWireStart)) {
                isWireEndpointDragActive = true;
                draggedWireBeforeEdit = wires.entries[draggedWireIndex];
                hasRecordedDraggedWireHistory = false;
                draggedWireLastRow = row;
                draggedWireLastColumn = column;
                message = "Drag wire endpoint to a block";
            } else {
                isPropertyPlacementPending = true;
                pendingPropertyBlockType = selectedBlockType;
            }
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && isBlockPropertySelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending &&
                   !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            isPropertyPlacementPending = true;
            pendingPropertyBlockType = selectedBlockType;
        }
        if (blockMode && isPropertyPlacementPending &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !isUiBlockingMap) {
            int column = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            int propertyBlockType = pendingPropertyBlockType;
            isPropertyPlacementPending = false;
            pendingPropertyBlockType = 0;
            if (PlaceSelectedBlockProperty(propertyBlockType, (RpgGridCell){ row, column }, &stage,
                                           &items, &wires, &receivers, &blockHistory, &message))
                blockEditedThisFrame = true;
            else message = "Property needs a valid block position";
                    // 配置直後は編集を開かず、既存アイテムと同じくクリック選択で開く。
                    selected = 0; isItemNameEditing = false;
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && isAttachmentSelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending && !isUiBlockingMap &&
                   IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mousePosition.y < 480.0f) {
            int column = (int)((mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                               RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            float localX = mousePosition.x - (float)((int)(mousePosition.x / RPG_STAGE_TILE_SIZE)) * RPG_STAGE_TILE_SIZE;
            float localY = mousePosition.y - (float)((int)(mousePosition.y / RPG_STAGE_TILE_SIZE)) * RPG_STAGE_TILE_SIZE;
            RpgGridSide side = RPG_GRID_SIDE_TOP;
            float nearest = localY;
            if (RPG_STAGE_TILE_SIZE - localX < nearest) { nearest = RPG_STAGE_TILE_SIZE - localX; side = RPG_GRID_SIDE_RIGHT; }
            if (RPG_STAGE_TILE_SIZE - localY < nearest) { nearest = RPG_STAGE_TILE_SIZE - localY; side = RPG_GRID_SIDE_BOTTOM; }
            if (localX < nearest) side = RPG_GRID_SIDE_LEFT;
            if (RpgAttachments_Add(&attachments, &stage, selectedBlockType, (RpgGridCell){ row, column }, side)) {
                RpgObjectFolders_PrepareAttachmentFolders(&attachments);
                PushAttachmentAddedHistory(&blockHistory, attachments.entries[attachments.count - 1]);
                blockEditedThisFrame = true;
                message = selectedBlockType == RPG_BLOCK_ATTACHMENT_DATA_BUTTON ?
                          "Data button attached" : "Radio emitter attached";
            } else message = "Attach to an unused block edge";
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isBlockPropertySelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !RpgEditorDrag_IsBusy(&referenceDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending && !isAttachmentSelected &&
                   clickedBlockType == 0 && !isUiBlockingMap && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            mousePosition.x += mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            int column = (int)(mousePosition.x / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            if (PlaceBlockType(&stage, &blockHistory, row, column, selectedBlockType)) {
                if (selectedBlockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL)
                    RpgSignalBlocks_Add(&signalBlocks, row, column);
                blockEditedThisFrame = true;
            }
            }
        } else if (blockMode && !isAttachmentErasePointerHeld && !isUiBlockingMap && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            mousePosition.x += mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            int column = (int)(mousePosition.x / RPG_STAGE_TILE_SIZE);
            int row = (int)(mousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            if (RemoveBlockAt(&stage, &items, &blockHistory, row, column)) {
                RpgWires_RemoveBroken(&wires, &stage);
                RpgReceivers_RemoveBroken(&receivers, &stage);
                RpgAttachments_RemoveBroken(&attachments, &stage);
                RpgSignalBlocks_RemoveBroken(&signalBlocks, &stage);
                blockEditedThisFrame = true;
                }
            }
        } else if (!isAttachmentPathEditing && !RpgEditorDrag_IsBusy(&attachmentDrag) && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !RpgEditorDrag_IsBusy(&referenceDrag) && !isWireEndpointDragActive && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgCharacter localPlayer = GetLocalCharacter(&player, mapIndex);
            RpgCharacter localNpc = GetLocalCharacter(&npc, mapIndex);
            int clickedItemIndex = GetClickedItemIndex(&items, mapIndex, mousePosition);
            Vector2 eventClickPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE, mousePosition.y };
            int clickedEventIndex = RpgMapEvents_FindAtPosition(&mapEvents, eventClickPosition, 48.0f);
            int clickedDoorRow = -1;
            int clickedDoorColumn = -1;
            int clickedColumn = (int)(eventClickPosition.x / RPG_STAGE_TILE_SIZE);
            int clickedRow = (int)(eventClickPosition.y / RPG_STAGE_TILE_SIZE);
            bool isReferenceClicked = clickedRow >= 0 && clickedRow < RPG_STAGE_ROWS &&
                                      clickedColumn >= 0 && clickedColumn < RPG_STAGE_WORLD_COLUMNS &&
                                      stage.blocks[clickedRow][clickedColumn] == RPG_BLOCK_REFERENCE_FILE;
            int clickedAttachmentIndex = RpgAttachments_FindAtPosition(&attachments, eventClickPosition, 30.0f);
            int clickedReceiverIndex = RpgReceivers_FindAtCell(&receivers,
                                                               (RpgGridCell){ clickedRow, clickedColumn });
            int clickedSignalBlockIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage, clickedRow, clickedColumn);
            if (clickedRow >= 0 && clickedRow < RPG_STAGE_ROWS && clickedColumn >= 0 &&
                clickedColumn < RPG_STAGE_WORLD_COLUMNS && RpgBlockInventory_IsDoorBlock(stage.blocks[clickedRow][clickedColumn])) {
                const RpgEffectShape *doorShape = NULL;
                if (FindEffectBlockRoot(&stage, clickedRow, clickedColumn, &clickedDoorRow, &clickedDoorColumn, &doorShape) &&
                    !RpgBlockInventory_IsDoorBlock(doorShape->rootType)) {
                    clickedDoorRow = -1;
                    clickedDoorColumn = -1;
                }
            }
            if (clickedAttachmentIndex >= 0) {
                selected = 6;
                selectedAttachmentIndex = clickedAttachmentIndex;
                isAttachmentPathEditing = false;
                message = attachments.entries[clickedAttachmentIndex].type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON ?
                          "Data button selected" : "Emitter selected";
            } else if (clickedReceiverIndex >= 0 && !isReceiverClickPending) {
                // 押下中は判定を保留し、離したクリックとドラッグを排他的に扱う。
                isReceiverClickPending = true;
                pendingReceiverIndex = clickedReceiverIndex;
                receiverPressPosition = mousePosition;
                message = "Release to change side, drag to edit wire";
            } else if (eventPlacementMode && CheckCollisionPointRec(mousePosition, area)) {
                Vector2 eventPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE, mousePosition.y };
                if (RpgMapEvents_Add(&mapEvents, eventPosition)) message = "Event placed";
                else message = "Event limit reached";
                eventPlacementMode = false;
            } else if (clickedEventIndex >= 0 && IsKeyDown(KEY_LEFT_SHIFT)) {
                draggedMapEventIndex = clickedEventIndex;
                message = "Drag event to move";
            } else if (clickedEventIndex >= 0) {
                activeInspect = &npcInspectData;
                inspectFunctionIndex = 0;
                modalHistory.count = 0;
                isExamineFunctionListOpen = true;
                isFunctionTypeListOpen = false;
                isMoveFunctionEditorOpen = false;
                isDialogueEditorOpen = false;
                isInspectDialogueEditing = false;
                isStage3DialogueEditing = false;
                message = "Event functions opened";
            } else if (clickedSignalBlockIndex >= 0 && !blockMode) {
                RpgSignalBlock *signalBlock = &signalBlocks.entries[clickedSignalBlockIndex];
                if (selected == 5 && selectedDoorRow == signalBlock->row && selectedDoorColumn == signalBlock->column) {
                    if (RpgSignalBlocks_Rotate(&signalBlocks, &stage, clickedSignalBlockIndex))
                        message = "Signal shrink block rotated";
                    else message = "Rotation needs an empty adjacent cell";
                } else {
                    selected = 5;
                    selectedDoorRow = signalBlock->row;
                    selectedDoorColumn = signalBlock->column;
                    message = "Signal shrink block selected";
                }
            } else if (clickedDoorRow >= 0) {
                selected = 5;
                selectedDoorRow = clickedDoorRow;
                selectedDoorColumn = clickedDoorColumn;
                isItemNameEditing = false;
                message = "Door selected";
            } else if (isReferenceClicked) {
                selected = 7;
                selectedReferenceRow = clickedRow;
                selectedReferenceColumn = clickedColumn;
                isReferencePathEditing = false;
                isReferencePointerFeedbackSuppressed = false;
                referencePathCursorIndex = (int)strlen(RpgStage_GetReferencePathAtCell(&stage, clickedRow, clickedColumn));
                referencePathSelectionAnchor = referencePathCursorIndex;
                referencePathSelectionEnd = referencePathCursorIndex;
                message = "FILE.png selected";
            } else if (clickedItemIndex >= 0) {
                selected = 4;
                selectedItemIndex = clickedItemIndex;
                isItemNameEditing = false;
                itemNameCursorIndex = (int)strlen(items.entries[clickedItemIndex].name);
                itemNameSelectionAnchor = itemNameCursorIndex;
                itemNameSelectionEnd = itemNameCursorIndex;
                message = "Item selected";
            } else if (!blockMode && IsCharacterInMap(&player, mapIndex) && IsCharacterClicked(&localPlayer, mousePosition)) {
                selected = 1;
                activeDialogueLine = -1;
                draggedCharacterKind = 1;
                RpgEditorDrag_Begin(&characterDrag, mousePosition);
                message = "Hero selected - drag to move";
            } else if (!blockMode && IsCharacterInMap(&npc, mapIndex) && IsCharacterClicked(&localNpc, mousePosition)) {
                selected = 2;
                activeDialogueLine = -1;
                draggedCharacterKind = 2;
                RpgEditorDrag_Begin(&characterDrag, mousePosition);
                message = "NPC selected - drag to move";
            } else if (!blockMode && mapIndex == 2 && CheckCollisionPointRec(mousePosition, editorZipperBounds)) {
                isZipperPointerFeedbackSuppressed = false;
                selected = 3;
                activeDialogueLine = -1;
                draggedCharacterKind = 3;
                RpgEditorDrag_Begin(&characterDrag, mousePosition);
                message = "Zipper selected - drag to move";
            }
            if (shouldClearZipperSelection && selected == 3) selected = 0;
            if (selected == 7 && !isReferenceClicked) {
                selected = 0;
                isReferencePointerFeedbackSuppressed = true;
            }
        }
        if (characterDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&characterDrag, mousePosition)) {
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Character moved by drag";
        }
        if (characterDrag.active && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
            RpgEditorDrag_Update(&characterDrag, mousePosition);
            if (draggedCharacterKind == 1) MoveCharacterToEditorPointer(&player, mapIndex, mousePosition);
            else if (draggedCharacterKind == 2) MoveCharacterToEditorPointer(&npc, mapIndex, mousePosition);
            else if (draggedCharacterKind == 3 && mapIndex == 2)
                MoveCharacterToEditorPointer(&zipperData.character, mapIndex, mousePosition);
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                RpgEditorDrag_End(&characterDrag);
                draggedCharacterKind = 0;
                message = "Character position updated";
            }
        } else if (characterDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_End(&characterDrag);
            draggedCharacterKind = 0;
        }
        if (draggedMapEventIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap) {
            mapEvents.entries[draggedMapEventIndex].position = (Vector2){ mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE, mousePosition.y };
        }
        if (!blockMode && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            Vector2 eventPosition = { mousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE, mousePosition.y };
            if (RpgMapEvents_RemoveAtPosition(&mapEvents, eventPosition, 48.0f)) message = "Event deleted";
        }
        if (IsKeyPressed(KEY_S) && activeDialogueLine < 0) {
            message = SaveEditorAndUpdateSnapshot(&layout, &player, &npc, &stage, &dialogue, &stage3Event,
                                                  &items, &savedSnapshot, &savedItems) ? "Saved" : "Save failed";
        }
        }
        (void)blockEditedThisFrame;
        DrawEditor(&player, &npc, &stage, &layout, &stage3Event, &zipperData, zipperTexture, fileTexture, &dialogue, selected, mapIndex, blockMode,
                   dialogueScroll, activeDialogueLine, dialogueCursorIndex, selectionAnchor,
                   selectionEnd, draggedDialogueLine, isDialogueEditorOpen, dialogueBlockHeight,
                   dialogueFontSize, isSpeakerEditing, isStage3DialogueEditing, isInspectDialogueEditing, isMoveFunctionEditorOpen, isMovePreviewPlaying, previewStartX, movePreviewSpriteX,
                   inspectFunctionIndex, isExamineFunctionListOpen, isFunctionTypeListOpen, draggedInspectFunction, isInspectTitleEditing, titleCursorIndex,
                   titleSelectionAnchor, titleSelectionEnd, speakerCursorIndex, speakerSelectionAnchor, speakerSelectionEnd, message, isExitConfirmationOpen,
                   isExitDetailsOpen,
                   exitDetailsScroll, &savedSnapshot, &items, &savedItems, eventPlacementMode, selectedItemIndex, isItemNameEditing, itemNameCursorIndex,
                   itemNameSelectionAnchor, itemNameSelectionEnd, selectedDoorRow, selectedDoorColumn,
                   selectedAttachmentIndex, isAttachmentPathEditing,
                   selectedBlockInventory, selectedBlockType,
                   isBlockInventoryListOpen,
                   isZipperPointerFeedbackSuppressed,
                    selectedReferenceRow, selectedReferenceColumn, isReferencePathEditing,
                    isReferencePointerFeedbackSuppressed,
                    referencePathCursorIndex, referencePathSelectionAnchor, referencePathSelectionEnd,
                    isGlobalSettingsOpen, isAreaInspectorOpen);
    }
    UnloadTexture(zipperTexture);
    UnloadTexture(fileTexture);
    GameFont_Unload();
    RestoreEditorCloseHandler();
    CloseWindow();
    return 0;
}
// 役割: RPG エディターの編集状態、UI、保存、プレビューを統合するエントリーポイント。
