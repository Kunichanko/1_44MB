// 依存する自プロジェクト内ファイル: ../Editor/file_dialog.h, rpg_attachment.h, rpg_block_inventory.h, rpg_character.h, rpg_data_shot.h, rpg_dialogue.h, rpg_item.h, rpg_layout.h, rpg_magnet.h, rpg_map_event.h, rpg_receiver.h, rpg_stage.h, rpg_stage_build.h, rpg_wire.h, game_font.h
// ステージ番号別の編集データ管理: rpg_stage_storage.h
// 依存関係を更新: rpg_stage3_event.h を追加した。
// 依存関係を更新: PC上のテキストファイル選択を再利用するため ../Editor/file_dialog.h を追加した。
// 依存関係を更新: 設置物・キャラ・FILE.png の共通ドラッグ状態に rpg_editor_drag.h を追加した。
// 依存関係を更新: エディター内プレイの状態復元に rpg_editor_play.h を追加した。
// 依存関係を更新: rpg_viewport.h を追加した。
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
#include "rlgl.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RPG_TEXT_ROUTE_RAYLIB_CALLS
#include "game_font.h"
#undef DrawText
static void Editor_DrawText(const char *text, int x, int y, int fontSize, Color color);
#define DrawText(text, x, y, fontSize, color) Editor_DrawText((text), (x), (y), (fontSize), (color))
#include "../Editor/file_dialog.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_editor_text.h"
#include "rpg_editor_drag.h"
#include "rpg_editor_navigation.h"
#include "rpg_editor_play.h"
#include "rpg_runtime.h"
#include "rpg_scene.h"
#include "rpg_layout.h"
#include "rpg_magnet.h"
#include "rpg_stage_background.h"
#include "rpg_stage_ground_texture.h"
#include "rpg_gimic_sprites.h"
#include "rpg_viewport.h"
#include "rpg_game_window.h"
#include "rpg_inspect.h"
#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_preview_event.h"
#include "rpg_preview_system.h"
#include "rpg_block_inventory.h"
#include "rpg_build_cell_storage.h"
#include "rpg_item.h"
#include "rpg_map_event.h"
#include "rpg_object_folder.h"
#include "rpg_explorer_launcher.h"
#include "rpg_receiver.h"
#include "rpg_static_object_drop.h"
#include "rpg_stage3_event.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_stage_build.h"
#include "rpg_stage_storage.h"
#include "rpg_wire.h"
#include "rpg_zipper.h"

// 依存関係: 全体設定の build 保存方式は rpg_build_cell_storage に保存する。

// 既存の編集 UI から共通テキストモジュールへ移行するための互換名。
#define GetCursorIndexAtX RpgEditorText_GetCursorIndexAtX
#define GetWrappedLineCount(text, fontSize) RpgEditorText_GetWrappedLineCount((text), (fontSize), 590.0f)
#define DrawTextCaret RpgEditorText_DrawCaret

enum { RPG_EDITOR_WIDTH = 960, RPG_EDITOR_HEIGHT = 540, NPC_VISIBLE_LINES = 9,
       RPG_EDITOR_INSPECTOR_HEADER_HEIGHT = 38 };
enum { MODAL_HISTORY_CAPACITY = 8 };

/* 編集UIを先に予約し、残った矩形だけへマップを等比表示するための固定レイアウト。 */
typedef struct EditorWorkspaceLayout {
    Rectangle mapViewport;
    Rectangle sidePanel;
    Rectangle controlBar;
} EditorWorkspaceLayout;

static const EditorWorkspaceLayout editorWorkspace = {
    .mapViewport = { 0.0f, 0.0f, 680.0f, 480.0f },
    .sidePanel = { 700.0f, 0.0f, 260.0f, 480.0f },
    .controlBar = { 0.0f, 480.0f, 960.0f, 60.0f }
};
/* Ctrl+X中は編集UIを描画・入力ともに止め、マップだけを確認できる。 */
static bool isEditorGameFullscreen = false;

static EditorWorkspaceLayout GetEditorWorkspaceLayout(void)
{
    if (isEditorGameFullscreen) {
        return (EditorWorkspaceLayout){
            .mapViewport = { 0.0f, 0.0f, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT },
            .sidePanel = { 0.0f, 0.0f, 0.0f, 0.0f },
            .controlBar = { 0.0f, 0.0f, 0.0f, 0.0f }
        };
    }
    return editorWorkspace;
}

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
    RpgAreaEntryEvents areaEntryEvents;
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
static const Rectangle doorInspectorBounds = { 700.0f, 80.0f, 220.0f, 300.0f };
static bool isKeyDoorFailureEditing;
static int keyDoorFailureCursorIndex;
static int keyDoorFailureSelectionAnchor;
static int keyDoorFailureSelectionEnd;
static const Rectangle referenceInspectorBounds = { 700.0f, 80.0f, 220.0f, 336.0f };
// 各サイドインスペクターの表示位置。内容は共通の座標系で描画して移動量だけを加える。
enum { RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR = 8, RPG_EDITOR_AREA_SETTINGS_INSPECTOR = 9,
       RPG_EDITOR_STAGE_SETTINGS_INSPECTOR = 10, RPG_EDITOR_IMAGE_INSPECTOR = 11,
       RPG_EDITOR_CONVEYOR_INSPECTOR = 12, RPG_EDITOR_PALETTE_INSPECTOR = 13,
       RPG_EDITOR_INSPECTOR_COUNT = 14 };
static Vector2 inspectorOffsets[RPG_EDITOR_INSPECTOR_COUNT];
/* 共通インスペクターの見た目状態。内容座標は維持し、枠サイズとスクロールだけを共有する。 */
static Vector2 inspectorSizeAdjustments[RPG_EDITOR_INSPECTOR_COUNT];
static float inspectorScrollOffsets[RPG_EDITOR_INSPECTOR_COUNT];
/* 全体/ステージ設定は通常の個別オブジェクト用インスペクターとは分離する。
 * タブ定義へ名前と内容高を足すだけで、折りたたみ・並び・スクロール範囲を
 * 同じ方式で増やせる。設定値そのものの保存先や保存形式はここでは変えない。 */
typedef struct RpgEditorSettingsTabDefinition {
    const char *label;
    /* 描画とは別の測定パス。項目を増減しても、展開領域と実際の
       内容が別々の固定値にならないようにする。 */
    float (*measureContentHeight)(void);
} RpgEditorSettingsTabDefinition;

/* インスペクターは必ず「必要な矩形を予約する」段階を先に通す。
   描画側はこの結果を参照するだけであり、領域を押し広げない。 */
typedef struct RpgEditorInspectorReservation {
    float contentTop;
    float contentBottom;
} RpgEditorInspectorReservation;

static RpgEditorInspectorReservation BeginInspectorReservation(float contentTop)
{
    return (RpgEditorInspectorReservation){ contentTop, contentTop };
}

static void ReserveInspectorContent(RpgEditorInspectorReservation *reservation,
                                    float top, float height)
{
    if (reservation == NULL || height <= 0.0f) return;
    reservation->contentBottom = fmaxf(reservation->contentBottom, top + height);
}

static float FinishInspectorReservation(const RpgEditorInspectorReservation *reservation,
                                        float bottomPadding)
{
    if (reservation == NULL) return 0.0f;
    return reservation->contentBottom + fmaxf(0.0f, bottomPadding);
}

/* 各設定タブの描画と対応する領域予約。座標はタブ内容先頭からの相対値。 */
static float MeasureGlobalBuildSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 20.0f, 28.0f);
    ReserveInspectorContent(&reservation, 66.0f, 18.0f);
    ReserveInspectorContent(&reservation, 82.0f, 28.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureGlobalRuntimeSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
    ReserveInspectorContent(&reservation, 70.0f, 26.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureGlobalPlayerSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
    ReserveInspectorContent(&reservation, 54.0f, 18.0f);
    ReserveInspectorContent(&reservation, 76.0f, 26.0f);
    ReserveInspectorContent(&reservation, 108.0f, 26.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureGlobalZipperSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
    ReserveInspectorContent(&reservation, 60.0f, 26.0f);
    ReserveInspectorContent(&reservation, 92.0f, 26.0f);
    ReserveInspectorContent(&reservation, 124.0f, 26.0f);
    ReserveInspectorContent(&reservation, 156.0f, 26.0f);
    ReserveInspectorContent(&reservation, 188.0f, 26.0f);
    ReserveInspectorContent(&reservation, 220.0f, 26.0f);
    ReserveInspectorContent(&reservation, 252.0f, 26.0f);
    ReserveInspectorContent(&reservation, 284.0f, 26.0f);
    ReserveInspectorContent(&reservation, 316.0f, 26.0f);
    ReserveInspectorContent(&reservation, 346.0f, 24.0f);
    ReserveInspectorContent(&reservation, 378.0f, 26.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureStageSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
    ReserveInspectorContent(&reservation, 60.0f, 26.0f);
    ReserveInspectorContent(&reservation, 92.0f, 18.0f);
    ReserveInspectorContent(&reservation, 118.0f, 26.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureStageEntryEventSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 30.0f, 24.0f);
    ReserveInspectorContent(&reservation, 62.0f, 18.0f);
    ReserveInspectorContent(&reservation, 82.0f, 26.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureStageBackgroundSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
    ReserveInspectorContent(&reservation, 62.0f, 18.0f);
    ReserveInspectorContent(&reservation, 84.0f, 26.0f);
    ReserveInspectorContent(&reservation, 118.0f, 18.0f);
    /* 相対PNGパスは複数行へ展開される。可変欄の後続操作が次タブへ
       侵入しない上限を確保し、実際の短いパスでは余白だけになる。 */
    ReserveInspectorContent(&reservation, 292.0f, 24.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureStageGroundBlockSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
    ReserveInspectorContent(&reservation, 184.0f, 28.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureStageGroundToneSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 24.0f);
    ReserveInspectorContent(&reservation, 52.0f, 24.0f);
    ReserveInspectorContent(&reservation, 76.0f, 24.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

enum { RPG_GLOBAL_SETTINGS_TAB_BUILD, RPG_GLOBAL_SETTINGS_TAB_RUNTIME,
       RPG_GLOBAL_SETTINGS_TAB_PLAYER, RPG_GLOBAL_SETTINGS_TAB_ZIPPER,
       RPG_GLOBAL_SETTINGS_TAB_COUNT };
enum { RPG_STAGE_SETTINGS_TAB_STAGE, RPG_STAGE_SETTINGS_TAB_ENTRY_EVENT,
       RPG_STAGE_SETTINGS_TAB_BACKGROUND, RPG_STAGE_SETTINGS_TAB_GROUND_BLOCK,
       RPG_STAGE_SETTINGS_TAB_GROUND_TONE, RPG_STAGE_SETTINGS_TAB_COUNT };

static const RpgEditorSettingsTabDefinition globalSettingsTabs[RPG_GLOBAL_SETTINGS_TAB_COUNT] = {
    { "エクスプローラーとビルド", MeasureGlobalBuildSettingsContent },
    { "実行時", MeasureGlobalRuntimeSettingsContent },
    { "プレイヤー", MeasureGlobalPlayerSettingsContent },
    { "ジッパー", MeasureGlobalZipperSettingsContent }
};
static const RpgEditorSettingsTabDefinition stageSettingsTabs[RPG_STAGE_SETTINGS_TAB_COUNT] = {
    { "ステージ", MeasureStageSettingsContent },
    { "ビルド開始イベント", MeasureStageEntryEventSettingsContent },
    { "背景", MeasureStageBackgroundSettingsContent },
    { "地面ブロック", MeasureStageGroundBlockSettingsContent },
    { "地面色調", MeasureStageGroundToneSettingsContent }
};
/* 設定を開くたび初期状態へ戻す。普段はすべて折りたたまれた状態にする。 */
static bool globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_COUNT];
static bool stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_COUNT];
static bool isInspectorResizing;
static int resizingInspector = 0;
static bool isInspectorResizeHorizontal;
static bool isInspectorResizeVertical;
static Vector2 inspectorResizeStartMouse;
static Vector2 inspectorResizeStartAdjustment;
static int inspectorDrawingSelected = 0;
static Rectangle movePanelBounds = { 654.0f, 10.0f, 282.0f, 520.0f };
static const Rectangle exitConfirmationBounds = { 250.0f, 120.0f, 460.0f, 300.0f };
static const Rectangle revertSavedBounds = { 424.0f, 486.0f, 130.0f, 26.0f };
static const Rectangle globalSettingsButtonBounds = { 12.0f, 486.0f, 90.0f, 26.0f };
static const Rectangle globalSettingsPanelBounds = { 700.0f, 80.0f, 220.0f, 440.0f };
static const Rectangle stageSettingsButtonBounds = { 108.0f, 486.0f, 78.0f, 26.0f };
static const Rectangle stageSettingsPanelBounds = { 700.0f, 80.0f, 220.0f, 440.0f };
static const Rectangle areaInspectorButtonBounds = { 192.0f, 486.0f, 66.0f, 26.0f };
/* Zipper はステージ上の配置物ではなく、専用設定から管理する。 */
static const Rectangle areaInspectorPanelBounds = { 700.0f, 80.0f, 220.0f, 224.0f };
static const Rectangle editorPlayToggleBounds = { 346.0f, 486.0f, 72.0f, 26.0f };
static RpgInspect npcInspectData;
static RpgZipper zipperData;
static RpgMapEvents mapEvents;
static RpgMapEvents savedMapEvents;
static RpgAreaEntryEvents areaEntryEvents;
static RpgStage3Event *currentStageEntryEvent;
static RpgWires wires;
static RpgWires savedWires;
static int selectedConveyorIndex = -1;
static float conveyorPreviewRemaining = 0.0f;
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
static int currentStageNumber = 1;
static RpgStageCatalog stageCatalogData;
/* ステージは参照パスを含み大きいため、起動時スタックを圧迫しない静的領域で受け渡す。 */
static RpgStageData stageLoadBuffer;
static RpgStageData stageSaveBuffer;
/* StageData is deliberately kept out of the editor entry-point stack.  It
   contains the complete world and is used here only as the idle-sync diff
   snapshot/work buffer. */
static RpgStageData stageFolderSyncBuffer;
static RpgStageData lastStageFolderSync;
/* A full stage contains the world-sized reference-path matrix.  Comparing a
   fresh copy every render frame is needless work while the editor is idle. */
static double nextStageFolderComparisonTime;
/* The editor preview is generated from this isolated copy.  Build preparation
   rewrites runtime reference paths, so it must never mutate the edit model. */
static RpgStage editorPreviewBuildStage;
/* Function列プレビューの退避先。実行中だけ実オブジェクトを動かし、終了時に必ず復元する。 */
static RpgStage functionPreviewStageSnapshot;
static RpgCharacter functionPreviewPlayerSnapshot;
static RpgCharacter functionPreviewNpcSnapshot;
static RpgZipper functionPreviewZipperSnapshot;
typedef struct FunctionPreviewMoveState {
    RpgInspectMove *move;
    Vector2 start;
    float elapsed;
    float transitionElapsed;
    bool running;
    bool transitioned;
} FunctionPreviewMoveState;
static FunctionPreviewMoveState functionPreviewMoves[RPG_INSPECT_MAX_FUNCTIONS];
static RpgStage *previewStage;
static RpgStageBackground stageBackground;
static RpgStageGroundTexture groundBlockTexture;
/* 画像オブジェクトはFolderと独立して選択状態を保持する。 */
static int selectedImageObjectIndex = -1;
// 数値欄の編集中だけ入力を受け、ほかのエディター操作へ伝搬させない。
static bool isAttachmentCapacityEditing;
static char attachmentCapacityInput[24];
static bool isAttachmentSpeedEditing;
static char attachmentSpeedInput[24];
/* ステージ単位の Zipper 容量上限。既存の数値入力と同じく Enter で確定する。 */
static bool isZipperCapacityEditing;
static char zipperCapacityInput[16];

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
static bool isImageObjectDragPreviewVisible;
static Vector2 imageObjectDragPointer;
static unsigned int imageObjectDragPreviewId;

/* FILE/FOLDER are movable reference objects now.  Keep editor hit testing in
   the same world-space model used by rendering/runtime instead of reviving
   the old "reference block in a cell" representation. */
static Vector2 GetEditorReferenceWorldPosition(const RpgStage *stage, int mapIndex,
                                                Vector2 localPosition)
{
    if (stage == NULL || mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT)
        return localPosition;
    return (Vector2){ stage->mapGridX[mapIndex] * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE + localPosition.x,
                      -stage->mapGridY[mapIndex] * RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE + localPosition.y };
}

static int FindEditorReferenceObjectAtMapPoint(const RpgStage *stage, int mapIndex,
                                                Vector2 localPosition)
{
    Vector2 worldPosition;
    if (stage == NULL || !RpgStage_IsMapActive(stage, mapIndex)) return -1;
    worldPosition = GetEditorReferenceWorldPosition(stage, mapIndex, localPosition);
    /* The last entry is drawn last, so it is the topmost selectable object. */
    for (int index = stage->referenceObjects.count - 1; index >= 0; index--) {
        const RpgReferenceObject *object = &stage->referenceObjects.entries[index];
        float size = RPG_STAGE_TILE_SIZE * (object->drawScale > 0.0f ? object->drawScale : 1.0f);
        Rectangle bounds = { object->position.x - size * 0.5f, object->position.y - size * 0.5f,
                             size, size };
        if (CheckCollisionPointRec(worldPosition, bounds)) return index;
    }
    return -1;
}

static bool RemoveEditorReferenceObjectAt(RpgReferenceObjects *objects, int index)
{
    if (objects == NULL || index < 0 || index >= objects->count) return false;
    if (index + 1 < objects->count)
        memmove(&objects->entries[index], &objects->entries[index + 1],
                (size_t)(objects->count - index - 1) * sizeof(objects->entries[0]));
    objects->count--;
    return true;
}
/* キャラクターもPNGと同じく、実体を離すまで保持し半透明プレビューだけを追従させる。 */
static bool isCharacterDragPreviewVisible;
static Vector2 characterDragPointer;
static int characterDragPreviewKind;
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

/* Function モーダルはNPC・Zipper・ステージ・エリアで同じ編集状態を再利用する。 */
static const RpgInspect *GetSavedActiveInspect(const EditorSaveSnapshot *snapshot,
                                               const RpgStage3Event *stageEntryEvent)
{
    if (activeInspect == &npcInspectData) return &snapshot->npcInspectSnapshot;
    if (activeInspect == &zipperInspectData) return &snapshot->zipperInspectSnapshot;
    if (activeInspect == &stageEntryEvent->inspect) return &snapshot->stage3Event.inspect;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
        if (activeInspect == &areaEntryEvents.entries[index].inspect)
            return &snapshot->areaEntryEvents.entries[index].inspect;
    return &snapshot->npcInspectSnapshot;
}

/* Relative paths are data, so register them whenever a stage becomes current.
 * This keeps saved Japanese image/key/reference names out of the fallback '?'. */
static void RegisterStageFilePickerText(const RpgStage *stage)
{
    if (stage == NULL) return;
    GameFont_BeginTextBatch();
    for (int index = 0; index < stage->referenceObjects.count; index++) {
        GameFont_AddText(stage->referenceObjects.entries[index].path);
        GameFont_AddText(stage->referenceObjects.entries[index].sourcePath);
    }
    for (int index = 0; index < stage->imageObjects.count; index++)
        GameFont_AddText(stage->imageObjects.entries[index].path);
    for (int index = 0; index < stage->keyDoorCount; index++)
        GameFont_AddText(stage->keyDoors[index].keyPath);
    GameFont_EndTextBatch();
}

// ステージ番号だけを入口にして、編集対象の一式を同じ保存フォルダから切り替える。
static bool LoadEditorStageState(int stageNumber, RpgLayout *layout, RpgCharacter *player,
                                 RpgCharacter *npc, RpgStage *stage, RpgItems *items,
                                 RpgDialogue *dialogue, RpgStage3Event *stage3Event)
{
    if (!RpgStageStorage_LoadStage(stageNumber, &stageLoadBuffer)) return false;
    *layout = stageLoadBuffer.layout;
    RpgLayout_LoadGlobalRuntime(layout);
    RpgStageBackground_Load(&stageBackground, layout->backgroundPath);
    RpgStageGroundTexture_Load(&groundBlockTexture, layout->groundBlockPath);
    RpgStage_SetGroundTexture(groundBlockTexture.texture);
    RpgStage_SetGroundAppearance(layout->groundHue, layout->groundSaturation,
                                 layout->groundLightness);
    *stage = stageLoadBuffer.stage;
    RegisterStageFilePickerText(stage);
    *items = stageLoadBuffer.items;
    *dialogue = stageLoadBuffer.dialogue;
    *stage3Event = stageLoadBuffer.stage3Event;
    currentStageEntryEvent = stage3Event;
    areaEntryEvents = stageLoadBuffer.areaEntryEvents;
    for (int areaIndex = 0; areaIndex < RPG_STAGE_MAP_COUNT; areaIndex++)
        for (int functionIndex = 0; functionIndex < areaEntryEvents.entries[areaIndex].inspect.functionCount; functionIndex++)
            for (int lineIndex = 0; lineIndex < areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
                GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
                GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lines[lineIndex]);
            }
    wires = stageLoadBuffer.wires;
    receivers = stageLoadBuffer.receivers;
    attachments = stageLoadBuffer.attachments;
    signalBlocks = stageLoadBuffer.signalBlocks;
    mapEvents = stageLoadBuffer.mapEvents;
    npcInspectData = stageLoadBuffer.npcInspectData;
    activeInspect = &npcInspectData;
    *player = RpgCharacter_Create(layout->playerPosition, BLUE, BROWN);
    RpgCharacter_SetUsesPlayerSpriteCollision(player, true);
    player->moveSpeed = layout->playerMoveSpeed;
    player->scale = layout->playerScale;
    *npc = RpgCharacter_Create(layout->npcPosition, PURPLE, DARKBROWN);
    npc->scale = layout->npcScale;
    /* NPC・Zipperは現行ステージから外す。構造体・スクリプトは将来の復元用に残す。 */
    npc->position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
    previewStage = stage;
    RpgObjectFolders_ClearSessionStorage();
    RpgObjectFolders_PrepareAttachmentFolders(&attachments);
    RpgObjectFolder_PrepareZipperAnimationCommand();
    return true;
}

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

static Rectangle GetInspectorBounds(int selected);

static Rectangle GetInspectorBaseBounds(int selected)
{
    if (selected == 1) return playerInspectorBounds;
    if (selected == 2) return npcInspectorBounds;
    if (selected == 5) return doorInspectorBounds;
    if (selected == 7) return referenceInspectorBounds;
    if (selected == RPG_EDITOR_IMAGE_INSPECTOR) return referenceInspectorBounds;
    if (selected == RPG_EDITOR_CONVEYOR_INSPECTOR) return (Rectangle){ 700.0f, 80.0f, 220.0f, 372.0f };
    if (selected == RPG_EDITOR_PALETTE_INSPECTOR) return (Rectangle){ 700.0f, 80.0f, 220.0f, 268.0f };
    if (selected == 6) return (Rectangle){ 700.0f, 80.0f, 220.0f, 350.0f };
    if (selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR) return globalSettingsPanelBounds;
    if (selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR) return areaInspectorPanelBounds;
    if (selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR) return stageSettingsPanelBounds;
    return selected == 4 ? (Rectangle){ 700.0f, 80.0f, 220.0f, 150.0f } :
                           (Rectangle){ 700.0f, 80.0f, 220.0f, 220.0f };
}

enum { RPG_EDITOR_SETTINGS_TAB_HEADER_HEIGHT = 28 };
static const float settingsTabsTop = 118.0f;

static float GetSettingsTabTop(const RpgEditorSettingsTabDefinition *tabs, const bool *expanded,
                               int tabCount, int tabIndex)
{
    float y = settingsTabsTop;
    for (int index = 0; index < tabIndex && index < tabCount; index++) {
        y += RPG_EDITOR_SETTINGS_TAB_HEADER_HEIGHT;
        if (expanded[index] && tabs[index].measureContentHeight != NULL)
            y += tabs[index].measureContentHeight();
    }
    return y;
}

static Rectangle GetSettingsTabHeaderBounds(const RpgEditorSettingsTabDefinition *tabs, const bool *expanded,
                                            int tabCount, int tabIndex)
{
    return (Rectangle){ 708.0f, GetSettingsTabTop(tabs, expanded, tabCount, tabIndex),
                        196.0f, RPG_EDITOR_SETTINGS_TAB_HEADER_HEIGHT };
}

static float GetSettingsTabContentTop(const RpgEditorSettingsTabDefinition *tabs, const bool *expanded,
                                      int tabCount, int tabIndex)
{
    return GetSettingsTabTop(tabs, expanded, tabCount, tabIndex) + RPG_EDITOR_SETTINGS_TAB_HEADER_HEIGHT;
}

static float GetSettingsTabsBottom(const RpgEditorSettingsTabDefinition *tabs, const bool *expanded,
                                   int tabCount)
{
    float bottom = settingsTabsTop;
    for (int index = 0; index < tabCount; index++) {
        bottom += RPG_EDITOR_SETTINGS_TAB_HEADER_HEIGHT;
        if (expanded[index] && tabs[index].measureContentHeight != NULL)
            bottom += tabs[index].measureContentHeight();
    }
    return bottom;
}

static bool IsInspectorTextContext(void);
static void DrawSettingsText(const char *text, float x, float y, float fontSize, Color color);
static Color GetBlockInventoryBorderColor(RpgBlockInventoryBorderColor color);

static int FindSettingsTabAtPoint(const RpgEditorSettingsTabDefinition *tabs, const bool *expanded,
                                  int tabCount, Vector2 point)
{
    for (int index = 0; index < tabCount; index++)
        if (CheckCollisionPointRec(point, GetSettingsTabHeaderBounds(tabs, expanded, tabCount, index)))
            return index;
    return -1;
}

static void DrawSettingsTabHeader(const RpgEditorSettingsTabDefinition *tabs, const bool *expanded,
                                  int tabCount, int tabIndex)
{
    Rectangle bounds = GetSettingsTabHeaderBounds(tabs, expanded, tabCount, tabIndex);
    bool open = expanded[tabIndex];
    DrawRectangleRec(bounds, open ? Fade(DARKBLUE, 0.15f) : Fade(LIGHTGRAY, 0.62f));
    DrawRectangleLinesEx(bounds, 1.0f, Fade(DARKBLUE, open ? 0.78f : 0.42f));
    DrawText(open ? "v" : ">", (int)bounds.x + 8, (int)bounds.y + 5, 17, DARKBLUE);
    DrawSettingsText(tabs[tabIndex].label, bounds.x + 28.0f, bounds.y + 6.0f, 15.0f, DARKBLUE);
}

static void ResetSettingsTabs(bool *expanded, int tabCount)
{
    for (int index = 0; index < tabCount; index++) expanded[index] = false;
}

/* 設定パネルは日本語を含むため、raylib既定フォントではなく既存の動的Font atlasで描く。 */
static void DrawSettingsText(const char *text, float x, float y, float fontSize, Color color)
{
    (void)color;
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, text, x, y,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, fontSize));
}

/* インスペクターは背景色や状態色に依存せず、本文・操作ラベルを常に黒で描く。 */
static bool IsInspectorTextContext(void)
{
    return inspectorDrawingSelected > 0;
}

static void Editor_DrawText(const char *text, int x, int y, int fontSize, Color color)
{
    RpgTextPreset preset = IsInspectorTextContext() ? RPG_TEXT_PRESET_UI :
                           RpgText_PresetFromLegacyColor(color);
    GameFont_DrawPreset(preset, text, (float)x, (float)y,
                        GameFont_GetPresetScale(preset, (float)fontSize));
}

static void RegisterSettingsUiText(void)
{
    static const char *const labels[] = {
        "エクスプローラーとビルド", "実行時", "プレイヤー", "ジッパー",
        "ステージ", "ビルド開始イベント", "背景", "地面ブロック", "地面色調",
        "全体設定", "ステージ設定", "エリア設定", "仮想", "本物", "ビルド", "簡易", "全フォルダ",
        "導線遅延", "磁石速度", "移動速度", "拡大率", "フォルダ返却", "返却アニメーション遅延",
        "ファイル追従倍率", "射出速度", "帰還速度", "追従速度", "プレビュー：オン", "プレビュー：オフ",
        "ステージを追加", "ステージを削除", "ジッパー容量", "ビルド後に一度実行", "機能を編集",
        "PNG未選択", "PNGを選択", "背景を消去", "背景の明るさ", "ブロックの明るさ", "基底", "assets/Sprite", "assets/Files",
        "単色（PNGなし）", "地面PNGを選択", "地面PNGを消去", "色相", "彩度", "明度",
        "エリア", "このエリアを削除", "最初の入場イベント", "保存", "保存済み", "保存失敗", "戻す", "オン", "オフ", "機能",
        "キャラクター設定", "会話", "会話を編集", "アイテム設定", "名前", "未保存 - Sキーで全て保存", "Sキーで全て保存",
        "コンベア設定", "コンベアの終点を選択", "軌道ブロック", "回転速度", "方向", "軌道に沿って正方向", "軌道に沿って逆方向", "外側の床・壁", "有効", "無効", "最小滑落角", "秒プレビュー", "プレビュー",
        "フォルダー設定", "ファイル設定", "フォルダー名", "ファイル", "フォルダー", "パスを指定", "フォルダーを開く", "ファイルを選択", "フォルダー名を変更",
        "画像オブジェクト設定", "PNG未選択", "拡大率", "描画レイヤー", "背景", "中間", "ブロック前・キャラ後", "前景", "見た目",
        "ドア設定", "鍵ドア", "開放", "施錠", "鍵ファイル", "鍵ファイル未選択", "失敗時の文章", "状態", "閉鎖", "開く", "閉じる",
        "信号ブロック設定", "信号を受けるたび状態を切替", "時間", "初期：展開", "初期：縮小", "信号をプレビュー", "クリック：回転 / ドラッグ：移動",
        "旗設定", "ボタン設定", "ブロック設置部品", "射出装置設定", "旗ID", "この地点からエディタープレイ", "この旗からプレイ", "ジッパー：接続状態", "停止するとこのエリアへ戻る",
        "対象：射出装置", "ブロックの辺に接続", "一度だけ実行（プレビューのみ）", "上面ブロック設置部品", "可動ブロックを真上へ設置", "信号：ブロック設置（エリア内のみ）", "右側の光", "透明度", "左端は対応する負角度を使用", "設置したブロックは固定されます",
        "実際の弾：フォルダー連動", "起動：データボタン", "1ファイルの大きさ", "弾をプレビュー", "プレビュー用ファイル数", "ブロックモード：軌道をドラッグ", "プレイヤー設定", "移動速度",
        "調べる機能", "キャラクター設定 - 会話", "文字", "会話機能の設定", "話者", "会話行を選択", "ドラッグ：順序 右クリック：削除", "右クリック：削除 ホイール：スクロール", "削除済み", "会話", "行", "個の機能", "会話機能を追加", "会話行を追加",
        "タイトル", "移動", "待機", "描画レイヤー", "機能を追加", "プレビュー実行中", "全機能をプレビュー", "機能の種類を選択", "描画レイヤー変更",
        "移動機能", "対象", "対象を選択", "移動軸", "歩行", "速度", "歩行アニメーション：利用不可", "補間", "終点", "パネル外をクリックして設定", "プレビューは選択した対象から開始", "時間", "次の機能まで", "プレビューを停止", "待機機能", "待機時間", "待機が終わると次の機能を開始します。", "対象画像", "別の画像を選択", "画像オブジェクトを選択", "新しいレイヤー", "一番手前の画像オブジェクトをクリック", "機能の順番で直ちにレイヤーを変更します。"
    };
    GameFont_BeginTextBatch();
    for (int index = 0; index < (int)(sizeof(labels) / sizeof(labels[0])); index++)
        GameFont_AddText(labels[index]);
    GameFont_AddText("パレット設定");
    GameFont_AddText("パレット");
    GameFont_AddText("囲い色");
    GameFont_AddText("編集");
    GameFont_AddText("白");
    GameFont_AddText("赤");
    GameFont_AddText("青");
    GameFont_AddText("黄");
    GameFont_EndTextBatch();
}

/* 個別インスペクターの描画コードから独立した予約パス。
   ここに最後の項目の下端を登録すれば、枠・クリップ・スクロールが同じ
   必要高さを使う。可変長の設定タブは上の測定関数が都度高さを返す。 */
static float MeasureInspectorRequiredHeight(int selected)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    if (selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR)
        return GetSettingsTabsBottom(globalSettingsTabs, globalSettingsTabExpanded,
                                     RPG_GLOBAL_SETTINGS_TAB_COUNT) - globalSettingsPanelBounds.y + 16.0f;
    if (selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR)
        return GetSettingsTabsBottom(stageSettingsTabs, stageSettingsTabExpanded,
                                     RPG_STAGE_SETTINGS_TAB_COUNT) - stageSettingsPanelBounds.y + 16.0f;
    switch (selected) {
    case 1: ReserveInspectorContent(&reservation, 0.0f, 164.0f); break;
    case 2: ReserveInspectorContent(&reservation, 0.0f, 310.0f); break;
    case 4: ReserveInspectorContent(&reservation, 0.0f, 150.0f); break;
    case 5: ReserveInspectorContent(&reservation, 0.0f, 400.0f); break;
    case 6: ReserveInspectorContent(&reservation, 0.0f, 430.0f); break;
    case 7: ReserveInspectorContent(&reservation, 0.0f, 336.0f); break;
    case RPG_EDITOR_IMAGE_INSPECTOR:
        /* PNG相対パスは複数行になるため、最長表示でも後続の設定を
           クリップせず収められる高さを予約する。 */
        ReserveInspectorContent(&reservation, 0.0f, 450.0f); break;
    case RPG_EDITOR_CONVEYOR_INSPECTOR:
        ReserveInspectorContent(&reservation, 0.0f, 372.0f); break;
    case RPG_EDITOR_PALETTE_INSPECTOR:
        ReserveInspectorContent(&reservation, 0.0f, 268.0f); break;
    case RPG_EDITOR_AREA_SETTINGS_INSPECTOR:
        ReserveInspectorContent(&reservation, 0.0f, 320.0f); break;
    default:
        ReserveInspectorContent(&reservation, 0.0f, GetInspectorBaseBounds(selected).height); break;
    }
    return FinishInspectorReservation(&reservation, 0.0f);
}

static bool IsAutoHeightInspector(int selected)
{
    return selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR ||
           selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR;
}

static float GetInspectorContentHeight(int selected)
{
    return MeasureInspectorRequiredHeight(selected);
}

static float GetInspectorScrollMaximum(int selected)
{
    Rectangle bounds = GetInspectorBounds(selected);
    return fmaxf(0.0f, MeasureInspectorRequiredHeight(selected) - bounds.height);
}

static Rectangle GetInspectorCloseButton(int selected)
{
    Rectangle bounds = GetInspectorBounds(selected);
    return (Rectangle){ bounds.x + bounds.width - 26.0f,
                        bounds.y + 8.0f + inspectorScrollOffsets[selected], 18.0f, 18.0f };
}

static Rectangle GetInspectorBounds(int selected)
{
    Rectangle bounds = GetInspectorBaseBounds(selected);
    if (selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT) {
        float requiredHeight = MeasureInspectorRequiredHeight(selected);
        float automaticHeight = IsAutoHeightInspector(selected) ? requiredHeight :
                                fmaxf(bounds.height, requiredHeight);
        bounds.width = Clamp(bounds.width + inspectorSizeAdjustments[selected].x, 220.0f, 520.0f);
        bounds.height = Clamp(automaticHeight + inspectorSizeAdjustments[selected].y, 140.0f, 480.0f);
    }
    return bounds;
}

static Rectangle GetInspectorScreenBounds(int selected)
{
    Rectangle bounds = GetInspectorBounds(selected);
    bounds.x += inspectorOffsets[selected].x;
    bounds.y += inspectorOffsets[selected].y;
    return bounds;
}

// タイトルバーは操作を占有するだけにし、編集コントロールの判定からは除外する。
static bool IsInspectorContentScreenPoint(int selected, Vector2 point)
{
    Rectangle bounds = GetInspectorScreenBounds(selected);
    bounds.y += RPG_EDITOR_INSPECTOR_HEADER_HEIGHT;
    bounds.height -= RPG_EDITOR_INSPECTOR_HEADER_HEIGHT;
    return CheckCollisionPointRec(point, bounds);
}

static Vector2 GetInspectorLocalPointer(Vector2 screenPointer, int selected)
{
    Vector2 pointer = Vector2Subtract(screenPointer, inspectorOffsets[selected]);
    pointer.y += inspectorScrollOffsets[selected];
    return pointer;
}

static Rectangle GetInspectorResizeHandle(int selected)
{
    Rectangle bounds = GetInspectorBounds(selected);
    return (Rectangle){ bounds.x + bounds.width - 14.0f,
                        bounds.y + bounds.height - 14.0f + inspectorScrollOffsets[selected], 14.0f, 14.0f };
}

static bool IsInspectorResizeEdge(int selected, Vector2 point)
{
    Rectangle bounds = GetInspectorBounds(selected);
    bounds.y += inspectorScrollOffsets[selected];
    return (point.x >= bounds.x + bounds.width - 8.0f && point.x <= bounds.x + bounds.width &&
            point.y >= bounds.y && point.y <= bounds.y + bounds.height) ||
           (point.y >= bounds.y + bounds.height - 8.0f && point.y <= bounds.y + bounds.height &&
            point.x >= bounds.x && point.x <= bounds.x + bounds.width);
}

static bool IsInspectorControlPoint(int selected, Vector2 point)
{
    // 内容を操作する領域と閉じるボタンでは、パネル移動を始めない。
    if (CheckCollisionPointRec(point, GetInspectorCloseButton(selected))) return true;
    if (CheckCollisionPointRec(point, GetInspectorResizeHandle(selected)) ||
        IsInspectorResizeEdge(selected, point)) return true;
    if (selected == 1) return CheckCollisionPointRec(point, (Rectangle){ 708, 138, 188, 28 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 800, 158, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 204, 188, 26 });
    if (selected == 2) return CheckCollisionPointRec(point, (Rectangle){ 800, 136, 100, 26 }) ||
                              CheckCollisionPointRec(point, (Rectangle){ 716, 170, 188, 102 });
    if (selected == 4) return CheckCollisionPointRec(point, (Rectangle){ 716, 144, 188, 28 });
    if (selected == 5) return CheckCollisionPointRec(point, (Rectangle){ 716, 144, 188, 112 });
    if (selected == 7)
        return CheckCollisionPointRec(point, (Rectangle){ 716, 122, 188, 220 });
    if (selected == RPG_EDITOR_IMAGE_INSPECTOR)
        /* 可変長のファイル欄により後続行が下がっても、内容域をパネル
           ドラッグとして誤認しない。個別の実操作判定は同じlayoutから行う。 */
        return CheckCollisionPointRec(point, (Rectangle){ 716, 122, 188, 340 });
    if (selected == RPG_EDITOR_CONVEYOR_INSPECTOR)
        return CheckCollisionPointRec(point, (Rectangle){ 716, 182, 42, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 862, 182, 42, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 246, 188, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 306, 188, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 366, 42, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 862, 366, 42, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 406, 188, 26 });
    if (selected == RPG_EDITOR_PALETTE_INSPECTOR)
        return CheckCollisionPointRec(point, (Rectangle){ 716, 162, 188, 28 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 224, 188, 26 });
    if (selected == 6) return CheckCollisionPointRec(point, (Rectangle){ 716, 114, 188, 250 });
    /* 設定パネル内はタブ見出し・将来追加する項目を含めて常に操作領域。
       パネル移動はタイトルバーだけから開始する。 */
    if (selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR ||
        selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR)
        return CheckCollisionPointRec(point, (Rectangle){ 708, settingsTabsTop, 196, 1000 });
    if (selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR)
        return CheckCollisionPointRec(point, (Rectangle){ 716, 178, 188, 26 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 236, 188, 24 }) ||
               CheckCollisionPointRec(point, (Rectangle){ 716, 288, 188, 26 });
    return false;
}

// キャラクター・アイテム共通のインスペクター枠と閉じるボタンを描画する。
static void DrawInspectorFrame(Rectangle bounds, const char *title, Color accent, Rectangle closeButton)
{
    if (inspectorDrawingSelected >= 1 && inspectorDrawingSelected < RPG_EDITOR_INSPECTOR_COUNT) {
        bounds = GetInspectorBounds(inspectorDrawingSelected);
        bounds.y += inspectorScrollOffsets[inspectorDrawingSelected];
        closeButton = GetInspectorCloseButton(inspectorDrawingSelected);
    }
    // 先に全体クリップを閉じて固定ヘッダーを描画し、以降は編集領域だけをクリップする。
    EndScissorMode();
    DrawRectangleRec(bounds, Fade(RAYWHITE, 0.94f));
    DrawRectangleLinesEx(bounds, 2.0f, accent);
    // タイトルと閉じるボタン用の領域を先に確保し、内容UIが重ならない基準線を明示する。
    Rectangle titleBar = { bounds.x + 2.0f, bounds.y + 2.0f, bounds.width - 4.0f, 34.0f };
    DrawRectangleRec(titleBar, Fade(accent, 0.08f));
    DrawLine((int)titleBar.x, (int)(titleBar.y + titleBar.height),
             (int)(titleBar.x + titleBar.width), (int)(titleBar.y + titleBar.height), Fade(accent, 0.36f));
    GameFont_Draw(title, bounds.x + 16.0f, bounds.y + 9.0f, 19.0f, BLACK);
    DrawRectangleRec(closeButton, Fade(MAROON, 0.88f));
    DrawText("x", (int)closeButton.x + 5, (int)closeButton.y + 1, 16, RAYWHITE);
    Rectangle contentBounds = GetInspectorScreenBounds(inspectorDrawingSelected);
    contentBounds.y += RPG_EDITOR_INSPECTOR_HEADER_HEIGHT;
    contentBounds.height -= RPG_EDITOR_INSPECTOR_HEADER_HEIGHT;
    BeginScissorMode((int)contentBounds.x, (int)contentBounds.y,
                     (int)contentBounds.width, (int)contentBounds.height);
}

// 大分類は通常の項目より強く見せ、設定の階層をひと目で分ける。
static void DrawInspectorSectionTitle(const char *title, int x, int y, Color color)
{
    DrawText(title, x, y, 20, color);
    DrawText(title, x + 1, y, 20, color);
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
        snapshot->zipper.followSpeed != zipperData.followSpeed ||
        snapshot->zipper.launchPreviewEnabled != zipperData.launchPreviewEnabled) {
        AddExitDetail(&details, "- Zipper");
    }
    if (memcmp(&snapshot->stage, stage, sizeof(*stage)) != 0) {
        AddExitDetail(&details, "- Stage blocks");
    }
    if (IsDialogueDifferent(&snapshot->dialogue, dialogue)) AddExitDetail(&details, "- NPC dialogue");
    if (snapshot->stage3Event.enabled != stage3Event->enabled ||
        IsInspectDifferent(&snapshot->stage3Event.inspect, &stage3Event->inspect)) {
        AddExitDetail(&details, "- Stage entry event");
    }
    if (memcmp(&snapshot->areaEntryEvents, &areaEntryEvents, sizeof(areaEntryEvents)) != 0)
        AddExitDetail(&details, "- Area entry events");
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
    const char *label = saveState == EDITOR_SAVE_SUCCEEDED ? "保存済み" :
                        saveState == EDITOR_SAVE_FAILED ? "保存失敗" : "保存";
    Color color = saveState == EDITOR_SAVE_SUCCEEDED ? DARKGREEN :
                  saveState == EDITOR_SAVE_FAILED ? MAROON : DARKBLUE;
    int fontSize = 17;
    DrawRectangleRec(bounds, color);
    Vector2 labelSize = GameFont_MeasureText(label, (float)fontSize);
    GameFont_Draw(label, bounds.x + (bounds.width - labelSize.x) * 0.5f,
                  bounds.y + (bounds.height - labelSize.y) * 0.5f, (float)fontSize,
                  IsInspectorTextContext() ? BLACK : RAYWHITE);
}

static void DrawRevertButton(Rectangle bounds)
{
    const int fontSize = 15;
    float scale = GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, (float)fontSize);
    DrawRectangleRec(bounds, MAROON);
    Vector2 labelSize = GameFont_MeasurePreset(RPG_TEXT_PRESET_UI, "戻す", scale);
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "戻す", bounds.x + (bounds.width - labelSize.x) * 0.5f,
                        bounds.y + (bounds.height - labelSize.y) * 0.5f, scale);
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
    Rectangle characterBounds = RpgCharacter_GetVisualBounds(character);
    return CheckCollisionPointRec(mousePosition, characterBounds);
}

static const char *GetMoveTargetName(const RpgInspectMove *move)
{
    if (move == NULL) return "Player";
    if (move->target == RPG_INSPECT_MOVE_NPC) return "NPC";
    if (move->target == RPG_INSPECT_MOVE_ZIPPER) return "Zipper";
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT) return "Image Object";
    return "Player";
}

/* Functionのプレビューと実行開始位置は、仮値ではなく選択対象の現在位置へ統一する。 */
static float GetMoveTargetWorldX(const RpgInspectMove *move, const RpgCharacter *player,
                                 const RpgCharacter *npc, const RpgZipper *zipper,
                                 const RpgStage *stage)
{
    if (move == NULL || player == NULL) return 0.0f;
    if (move->target == RPG_INSPECT_MOVE_NPC && npc != NULL) return npc->position.x;
    if (move->target == RPG_INSPECT_MOVE_ZIPPER && zipper != NULL) return zipper->character.position.x;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && stage != NULL) {
        int index = RpgImageObjects_FindById(&stage->imageObjects, move->targetImageObjectId);
        if (index >= 0) return RpgImageObjects_GetWorldCenterX(&stage->imageObjects.entries[index], RPG_STAGE_TILE_SIZE);
    }
    return player->position.x;
}

static float GetMoveTargetWorldY(const RpgInspectMove *move, const RpgCharacter *player,
                                 const RpgCharacter *npc, const RpgZipper *zipper,
                                 const RpgStage *stage)
{
    if (move == NULL || player == NULL) return 0.0f;
    if (move->target == RPG_INSPECT_MOVE_NPC && npc != NULL) return npc->position.y;
    if (move->target == RPG_INSPECT_MOVE_ZIPPER && zipper != NULL) return zipper->character.position.y;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && stage != NULL) {
        int index = RpgImageObjects_FindById(&stage->imageObjects, move->targetImageObjectId);
        if (index >= 0) return RpgImageObjects_GetWorldCenterY(&stage->imageObjects.entries[index], RPG_STAGE_TILE_SIZE);
    }
    return player->position.y;
}

/* 軸設定を反映した実際の終点。無効軸は選択対象の現在位置を保つ。 */
static Vector2 GetMoveEndpoint(const RpgInspectMove *move, const RpgCharacter *player,
                               const RpgCharacter *npc, const RpgZipper *zipper, const RpgStage *stage)
{
    Vector2 current = { GetMoveTargetWorldX(move, player, npc, zipper, stage),
                        GetMoveTargetWorldY(move, player, npc, zipper, stage) };
    if (move == NULL) return current;
    if (RpgInspect_MoveAxisHasX(move->axis)) current.x = move->destinationX;
    if (RpgInspect_MoveAxisHasY(move->axis)) current.y = move->destinationY;
    return current;
}

static Vector2 SnapMoveDestination(Vector2 destination, const RpgInspectMove *move)
{
    if (move == NULL) return destination;
    /* Move終点も他のドラッグ操作と同じく、Shift中はマスへの補正を行わない。 */
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return destination;
    destination.x = floorf(destination.x / RPG_STAGE_TILE_SIZE) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    destination.y = floorf(destination.y / RPG_STAGE_TILE_SIZE) * RPG_STAGE_TILE_SIZE +
                    (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ? RPG_STAGE_TILE_SIZE * 0.5f : RPG_STAGE_TILE_SIZE);
    return destination;
}

/* 通常ドラッグは見た目もマス中心へ合わせ、Shift中だけカーソル位置をそのまま使う。 */
static Vector2 GetImageObjectDragPreviewPointer(Vector2 pointer)
{
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return pointer;
    pointer.x = floorf(pointer.x / RPG_STAGE_TILE_SIZE) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    pointer.y = floorf(pointer.y / RPG_STAGE_TILE_SIZE) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    return pointer;
}

/* PNGとキャラクターの通常ドラッグを同じ座標規則にし、Shift中だけ自由位置を保持する。 */
static Vector2 GetCharacterDragPreviewPointer(Vector2 pointer)
{
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return pointer;
    pointer.x = floorf(pointer.x / RPG_STAGE_TILE_SIZE) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f;
    pointer.y = floorf(pointer.y / RPG_STAGE_TILE_SIZE) * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE;
    return pointer;
}

/* 新しく有効にした軸は対象の現在位置で初期化し、未設定の座標へ飛ばないようにする。 */
static void SetMoveAxis(RpgInspectMove *move, RpgInspectMoveAxis axis, const RpgCharacter *player,
                        const RpgCharacter *npc, const RpgZipper *zipper, const RpgStage *stage)
{
    if (move == NULL || axis < RPG_INSPECT_MOVE_AXIS_X || axis >= RPG_INSPECT_MOVE_AXIS_COUNT) return;
    Vector2 current = { GetMoveTargetWorldX(move, player, npc, zipper, stage),
                        GetMoveTargetWorldY(move, player, npc, zipper, stage) };
    if (!RpgInspect_MoveAxisHasX(move->axis) && RpgInspect_MoveAxisHasX(axis)) move->destinationX = current.x;
    if (!RpgInspect_MoveAxisHasY(move->axis) && RpgInspect_MoveAxisHasY(axis)) move->destinationY = current.y;
    move->axis = axis;
}

/* Function列全体のプレビューでも本編と同じ補間式・対象指定を使う。 */
static void ApplyFunctionPreviewMove(RpgInspectMove *move, RpgCharacter *player, RpgCharacter *npc,
                                     RpgZipper *zipper, RpgStage *stage, Vector2 start, float elapsed)
{
    if (move == NULL || player == NULL || npc == NULL || zipper == NULL || stage == NULL) return;
    float progress = Clamp(elapsed / fmaxf(move->duration, 0.1f), 0.0f, 1.0f);
    float eased = RpgInspect_EaseMoveProgress(move->easing, progress);
    float x = RpgInspect_MoveAxisHasX(move->axis) ? start.x + (move->destinationX - start.x) * eased : start.x;
    float y = RpgInspect_MoveAxisHasY(move->axis) ? start.y + (move->destinationY - start.y) * eased : start.y;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT) {
        int imageIndex = RpgImageObjects_FindById(&stage->imageObjects, move->targetImageObjectId);
        if (imageIndex >= 0) RpgImageObjects_SetRuntimePosition(&stage->imageObjects.entries[imageIndex], (Vector2){ x, y });
        return;
    }
    RpgCharacter *target = move->target == RPG_INSPECT_MOVE_PLAYER ? player :
                           move->target == RPG_INSPECT_MOVE_NPC ? npc : &zipper->character;
    target->position = (Vector2){ x, y };
    if (move->target == RPG_INSPECT_MOVE_PLAYER) {
        target->isMoving = move->walkAnimationEnabled;
        if (move->walkAnimationEnabled) {
            target->animationElapsed = elapsed * move->walkAnimationSpeed;
            if (fabsf(x - start.x) > 0.01f) target->facingDirection = x >= start.x ? 1 : -1;
        }
    }
}

/* Function列プレビューも本編と同じく、複数Moveを並行して更新する。 */
static void ResetFunctionPreviewMoves(void)
{
    memset(functionPreviewMoves, 0, sizeof(functionPreviewMoves));
}

static FunctionPreviewMoveState *StartFunctionPreviewMove(RpgInspectMove *move, RpgCharacter *player,
                                                           RpgCharacter *npc, RpgZipper *zipper, RpgStage *stage)
{
    for (int index = 0; index < RPG_INSPECT_MAX_FUNCTIONS; index++)
        if (functionPreviewMoves[index].move == move && !functionPreviewMoves[index].transitioned)
            return &functionPreviewMoves[index];
    for (int index = 0; index < RPG_INSPECT_MAX_FUNCTIONS; index++) if (functionPreviewMoves[index].move == NULL) {
        functionPreviewMoves[index] = (FunctionPreviewMoveState){
            .move = move, .running = true,
            .start = { GetMoveTargetWorldX(move, player, npc, zipper, stage),
                       GetMoveTargetWorldY(move, player, npc, zipper, stage) }
        };
        return &functionPreviewMoves[index];
    }
    return NULL;
}

static void UpdateFunctionPreviewMoves(RpgCharacter *player, RpgCharacter *npc, RpgZipper *zipper,
                                       RpgStage *stage, float deltaTime)
{
    bool playerWalkActive = false;
    for (int index = 0; index < RPG_INSPECT_MAX_FUNCTIONS; index++) {
        FunctionPreviewMoveState *state = &functionPreviewMoves[index];
        if (state->move == NULL || !state->running) continue;
        state->elapsed += deltaTime;
        ApplyFunctionPreviewMove(state->move, player, npc, zipper, stage, state->start, state->elapsed);
        if (state->move->target == RPG_INSPECT_MOVE_PLAYER && state->move->walkAnimationEnabled)
            playerWalkActive = true;
        if (state->elapsed >= fmaxf(state->move->duration, 0.1f)) {
            state->running = false;
            if (state->transitioned) state->move = NULL;
        }
    }
    player->isMoving = playerWalkActive;
}

static bool HasRunningFunctionPreviewMoves(void)
{
    for (int index = 0; index < RPG_INSPECT_MAX_FUNCTIONS; index++)
        if (functionPreviewMoves[index].move != NULL && functionPreviewMoves[index].running) return true;
    return false;
}

static float GetFunctionPreviewDuration(const RpgInspectFunction *function)
{
    if (function->type == RPG_INSPECT_MOVE) return fmaxf(function->move.duration, 0.1f);
    if (function->type == RPG_INSPECT_WAIT) return fmaxf(function->wait.duration, 0.0f);
    /* 会話は読み上げの代わりに行数に応じた固定表示時間で全体の流れを確認する。 */
    if (function->type == RPG_INSPECT_DIALOGUE) return fmaxf(0.8f, function->dialogue.lineCount * 0.8f);
    return 0.0f;
}

/* Function列プレビューは編集データを一時利用するため、完了・中断で必ず同じ復元処理を通す。 */
static void RestoreFunctionPreviewState(RpgStage *stage, RpgCharacter *player, RpgCharacter *npc,
                                        RpgZipper *zipper, const RpgStage *savedStage,
                                        const RpgCharacter *savedPlayer, const RpgCharacter *savedNpc,
                                        const RpgZipper *savedZipper)
{
    *stage = *savedStage;
    *player = *savedPlayer;
    *npc = *savedNpc;
    *zipper = *savedZipper;
}

// キャラクター種別に依存しない、エディター上の横位置ドラッグ更新。
// 足元の高さは各キャラクターの設定を保ち、地面から浮かない編集にする。
static void MoveCharacterToEditorPointer(RpgCharacter *character, int mapIndex, Vector2 pointer)
{
    if (character == NULL) return;
    Vector2 destination = GetCharacterDragPreviewPointer(pointer);
    character->position.x = destination.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    character->position.y = destination.y;
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

// プレイ開始時は編集用プレビューだけを破棄し、編集データ自体はプレイ用スナップショットに保持する。
static void ResetEditorPreviews(RpgStage *stage, RpgSignalBlocks *signalBlocks,
                                RpgDataShots *previewShots, RpgPreviewEvent *previewEvent,
                                bool *isMovePreviewPlaying, bool *isZipperLaunchPreviewVisible,
                                bool *isZipperLaunchPreviewReturning)
{
    RpgSignalBlocks_EndPreviews(signalBlocks, stage);
    *previewShots = RpgDataShots_Default();
    *previewEvent = RpgPreviewEvent_Default();
    *isMovePreviewPlaying = false;
    *isZipperLaunchPreviewVisible = false;
    *isZipperLaunchPreviewReturning = false;
}

// エディター表示用に地面へ少し埋まっている場合だけ、プレイ用の足元座標へ戻す。
static void PrepareEditorPlayCharacter(RpgCharacter *character, const RpgStage *stage)
{
    while (character->position.y > 0.0f &&
           RpgStage_CheckSolidCollision(stage, RpgCharacter_GetCollisionBounds(character)))
        character->position.y -= 1.0f;
    character->verticalSpeed = 0.0f;
    character->isGrounded = true;
}

/* 保存旗の外側マスを、エディター内プレイの開始地点へ変換する。描画位置と同じ経路を使う。 */
static bool GetSaveFlagEditorPlayStart(const RpgAttachments *attachments, const RpgStage *stage,
                                       int attachmentIndex, Vector2 *startPosition, int *startMapIndex)
{
    if (attachments == NULL || stage == NULL || startPosition == NULL || startMapIndex == NULL ||
        attachmentIndex < 0 || attachmentIndex >= attachments->count) return false;
    const RpgAttachment *flag = &attachments->entries[attachmentIndex];
    if (flag->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG || flag->isZipperHeld) return false;
    /* The attachment is stored in a packed slot, but runtime characters use the
       connected two-dimensional world.  In particular, a flag on an area's edge
       must not select the neighbouring storage slot just to validate its start. */
    Vector2 respawn = RpgAttachments_GetSaveFlagRespawnPositionWorld(flag, stage);
    int mapIndex = RpgStage_GetMapAtWorldPosition(stage, respawn);
    if (mapIndex < 0) return false;
    *startPosition = respawn;
    *startMapIndex = mapIndex;
    return true;
}

#if 0 /* Retired editor-only runtime path. Editor Play uses RpgRuntime_UpdateAndDraw. */
// エディター内プレイは保存・編集UIを経由せず、ゲームと同じ足場判定とシグナル処理だけを実行する。
static void UpdateEditorPlay(RpgCharacter *player, const RpgCharacter *npc, RpgStage *stage,
                             RpgItems *items, RpgAttachments *attachments, RpgSignalBlocks *signalBlocks,
                             RpgDataShots *dataShots, RpgButtonEvent *buttonEvent,
                             RpgReceivers *receivers, RpgWires *wires, const RpgLayout *layout,
                             bool *wasButtonPressed, int *mapIndex, bool acceptsPlayerInput,
                             float deltaTime)
{
    RpgRuntimeUpdateContext context = {
        .player = player, .npc = npc, .stage = stage, .attachments = attachments,
        .signalBlocks = signalBlocks, .dataShots = dataShots, .buttonEvent = buttonEvent,
        .receivers = receivers, .wires = wires, .layout = layout, .magnetRuntime = NULL,
        .wasButtonPressed = wasButtonPressed, .currentMapIndex = *mapIndex,
        .acceptsPlayerInput = acceptsPlayerInput,
        .updatesWorldSystems = true
    };
    RpgRuntime_UpdateWorld(&context, deltaTime);

    for (int index = 0; index < items->count; index++) {
        if (!items->entries[index].collected &&
            fabsf(player->position.x - items->entries[index].position.x) <= 28.0f)
            items->entries[index].collected = true;
    }
    *mapIndex = Clamp((int)(player->position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)),
                      0, RpgStage_GetMapCount(stage) - 1);
}

static void CompleteEditorPlayInspect(int *inspectTarget, int *functionIndex, int *lineIndex,
                                      bool *isMoveRunning, bool *npcCompleted, bool *zipperCompleted,
                                      bool *zipperFollowsPlayer, const RpgInspect *runtimeNpcInspect,
                                      const RpgZipper *zipper)
{
    const RpgInspect *inspect = *inspectTarget == 2 ? &zipper->inspect : runtimeNpcInspect;
    (*functionIndex)++;
    *lineIndex = 0;
    *isMoveRunning = false;
    if (*functionIndex < inspect->functionCount) return;
    if (*inspectTarget == 2) {
        *zipperCompleted = true;
        *zipperFollowsPlayer = true;
    } else *npcCompleted = true;
    *inspectTarget = 0;
    *functionIndex = 0;
    *lineIndex = 0;
}

// 本編と同じ E / I の会話・調べる順序を、エディター内プレイでも使う。
static void UpdateEditorPlayInteraction(RpgCharacter *player, RpgCharacter *npc, RpgZipper *zipper,
                                        const RpgDialogue *dialogue, const RpgInspect *runtimeNpcInspect,
                                        int *dialogueIndex, int *inspectTarget, int *functionIndex,
                                        int *lineIndex, bool *isMoveRunning, float *moveElapsed,
                                        float *moveStartX, bool *npcCompleted, bool *zipperCompleted,
                                        bool *zipperFollowsPlayer, float deltaTime)
{
    bool canTalk = RpgCharacter_IsNear(player, npc, 72.0f);
    if (*inspectTarget != 0) {
        const RpgInspect *inspect = *inspectTarget == 2 ? &zipper->inspect : runtimeNpcInspect;
        const RpgInspectFunction *function = &inspect->functions[*functionIndex];
        if (function->type == RPG_INSPECT_MOVE) {
            float *targetX = function->move.target == RPG_INSPECT_MOVE_PLAYER ? &player->position.x :
                             function->move.target == RPG_INSPECT_MOVE_NPC ? &npc->position.x :
                             &zipper->character.position.x;
            if (!*isMoveRunning) {
                *isMoveRunning = true;
                *moveElapsed = 0.0f;
                *moveStartX = *targetX;
            }
            *moveElapsed += deltaTime;
            float progress = Clamp(*moveElapsed / function->move.duration, 0.0f, 1.0f);
            *targetX = *moveStartX + (function->move.destinationX - *moveStartX) *
                       RpgInspect_EaseMoveProgress(function->move.easing, progress);
            if (progress >= 1.0f)
                CompleteEditorPlayInspect(inspectTarget, functionIndex, lineIndex, isMoveRunning,
                                          npcCompleted, zipperCompleted, zipperFollowsPlayer,
                                          runtimeNpcInspect, zipper);
            return;
        }
    }
    if (IsKeyPressed(KEY_E)) {
        if (*dialogueIndex >= 0) {
            (*dialogueIndex)++;
            if (*dialogueIndex >= dialogue->lineCount) *dialogueIndex = -1;
        } else if (*inspectTarget != 0) {
            const RpgInspect *inspect = *inspectTarget == 2 ? &zipper->inspect : runtimeNpcInspect;
            const RpgInspectFunction *function = &inspect->functions[*functionIndex];
            if (function->type == RPG_INSPECT_DIALOGUE && ++*lineIndex >= function->dialogue.lineCount)
                CompleteEditorPlayInspect(inspectTarget, functionIndex, lineIndex, isMoveRunning,
                                          npcCompleted, zipperCompleted, zipperFollowsPlayer,
                                          runtimeNpcInspect, zipper);
        } else if (canTalk && dialogue->lineCount > 0) *dialogueIndex = 0;
    }
    /* エディター内Playも本編と同じく、キャラクター個別の調べる入力は受け付けない。 */
    if (*zipperFollowsPlayer && *inspectTarget == 0 && *dialogueIndex < 0) {
        Vector2 target = { player->position.x - RPG_STAGE_TILE_SIZE * player->scale, player->position.y };
        zipper->character.position = Vector2MoveTowards(zipper->character.position, target,
                                                         zipper->followSpeed * deltaTime);
    }
}

#endif

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

#if 0 /* Replaced by rpg_editor_navigation.c: editor-only input stays outside Play. */
// 矢印方向の隣接エリアへ移動し、未作成なら空のエリアを台帳へ追加する。
static int GetRequestedMapIndex(RpgStage *stage, int currentMapIndex)
{
    RpgAreaDirection direction;
    // Shift+矢印はステージ切替に予約し、エリア移動として処理しない。
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return -1;
    if (IsKeyPressed(KEY_LEFT)) direction = RPG_AREA_LEFT;
    else if (IsKeyPressed(KEY_RIGHT)) direction = RPG_AREA_RIGHT;
    else if (IsKeyPressed(KEY_UP)) direction = RPG_AREA_UP;
    else if (IsKeyPressed(KEY_DOWN)) direction = RPG_AREA_DOWN;
    else return -1;
    currentMapIndex = RpgStage_FindNearestActiveMap(stage, currentMapIndex);
    int nextMapIndex = RpgStage_GetAdjacentMap(stage, currentMapIndex, direction);
    if (nextMapIndex >= 0) return nextMapIndex;
    return -1;
}

static bool GetAreaInsertionDirection(RpgAreaDirection *direction)
{
    if (direction == NULL ||
        !(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) return false;
    if (IsKeyPressed(KEY_LEFT)) *direction = RPG_AREA_LEFT;
    else if (IsKeyPressed(KEY_RIGHT)) *direction = RPG_AREA_RIGHT;
    else if (IsKeyPressed(KEY_UP)) *direction = RPG_AREA_UP;
    else if (IsKeyPressed(KEY_DOWN)) *direction = RPG_AREA_DOWN;
    else return false;
    return true;
}
#endif

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

typedef enum EditorMapObjectHit {
    EDITOR_MAP_OBJECT_HIT_NONE,
    EDITOR_MAP_OBJECT_HIT_IMAGE,
    EDITOR_MAP_OBJECT_HIT_PLAYER,
    EDITOR_MAP_OBJECT_HIT_NPC
} EditorMapObjectHit;

/* 描画順を入力順にも使う。中心マスで所属エリアを決めつつ、選択だけは実際の描画範囲で行う。 */
static int FindImageObjectAtVisualPosition(const RpgImageObjects *objects, int mapIndex,
                                           Vector2 mapPosition, RpgImageObjectLayer layer)
{
    if (objects == NULL) return -1;
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int index = objects->count - 1; index >= 0; index--) {
        const RpgImageObject *object = &objects->entries[index];
        if (object->layer != layer || object->column < firstColumn ||
            object->column >= firstColumn + RPG_STAGE_COLUMNS) continue;
        if (CheckCollisionPointRec(mapPosition,
                                   RpgImageObjects_GetLocalBounds(object, mapIndex, RPG_STAGE_COLUMNS,
                                                                  RPG_STAGE_TILE_SIZE))) return index;
    }
    return -1;
}

static EditorMapObjectHit GetTopmostEditableObject(const RpgStage *stage,
                                                    const RpgCharacter *player,
                                                    const RpgCharacter *npc,
                                                    const RpgZipper *zipper,
                                                    int mapIndex, Vector2 mapPosition,
                                                    int *imageIndex)
{
    /* Zipper は設定対象であり、エリア内のクリック・移動・イベント対象にはしない。 */
    (void)zipper;
    if (imageIndex != NULL) *imageIndex = -1;
    int hitImage = FindImageObjectAtVisualPosition(&stage->imageObjects, mapIndex, mapPosition,
                                                    RPG_IMAGE_OBJECT_LAYER_FRONT);
    if (hitImage >= 0) {
        if (imageIndex != NULL) *imageIndex = hitImage;
        return EDITOR_MAP_OBJECT_HIT_IMAGE;
    }
    if (IsCharacterInMap(player, mapIndex)) {
        RpgCharacter localPlayer = GetLocalCharacter(player, mapIndex);
        if (IsCharacterClicked(&localPlayer, mapPosition)) return EDITOR_MAP_OBJECT_HIT_PLAYER;
    }
    if (IsCharacterInMap(npc, mapIndex)) {
        RpgCharacter localNpc = GetLocalCharacter(npc, mapIndex);
        if (IsCharacterClicked(&localNpc, mapPosition)) return EDITOR_MAP_OBJECT_HIT_NPC;
    }
    hitImage = FindImageObjectAtVisualPosition(&stage->imageObjects, mapIndex, mapPosition,
                                                RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
    if (hitImage >= 0) {
        if (imageIndex != NULL) *imageIndex = hitImage;
        return EDITOR_MAP_OBJECT_HIT_IMAGE;
    }
    hitImage = FindImageObjectAtVisualPosition(&stage->imageObjects, mapIndex, mapPosition,
                                                RPG_IMAGE_OBJECT_LAYER_BACK);
    if (hitImage >= 0 && imageIndex != NULL) *imageIndex = hitImage;
    return hitImage >= 0 ? EDITOR_MAP_OBJECT_HIT_IMAGE : EDITOR_MAP_OBJECT_HIT_NONE;
}

static void ComposeEditorStageData(RpgStageData *destination, const RpgLayout *layout,
                                   const RpgStage *stage, const RpgDialogue *dialogue,
                                   const RpgStage3Event *stage3Event, const RpgItems *items)
{
    if (destination == NULL || layout == NULL || stage == NULL || dialogue == NULL ||
        stage3Event == NULL || items == NULL) return;
    memset(destination, 0, sizeof(*destination));
    destination->layout = *layout;
    destination->stage = *stage;
    destination->dialogue = *dialogue;
    destination->stage3Event = *stage3Event;
    destination->areaEntryEvents = areaEntryEvents;
    destination->npcInspectData = npcInspectData;
    destination->items = *items;
    destination->wires = wires;
    destination->receivers = receivers;
    destination->attachments = attachments;
    destination->signalBlocks = signalBlocks;
    destination->mapEvents = mapEvents;
}

/* The editor preview cache is a copy of the static stage.  Do not let runtime
 * animation and play-progress fields make the idle synchronizer rebuild that
 * cache repeatedly.  These members are deliberately omitted by their normal
 * serializers as well; normalising them here makes the in-memory comparison
 * match the actual saved-stage contract. */
static void NormalizeEditorPreviewSyncData(RpgStageData *data)
{
    if (data == NULL) return;
    memset(data->stage.socketSignalActive, 0, sizeof(data->stage.socketSignalActive));
    data->stage.spatialReferenceMap = 0;
    for (int index = 0; index < data->stage.referenceObjects.count; index++) {
        RpgReferenceObject *object = &data->stage.referenceObjects.entries[index];
        object->legacySourceRow = -1;
        object->legacySourceColumn = -1;
        object->isFalling = false;
        object->fallSpeed = 0.0f;
        object->followsPlayer = false;
        object->drawScale = 0.0f;
        object->isCompressing = false;
        object->isCompressed = false;
        object->compressionElapsed = 0.0f;
    }
    for (int index = 0; index < data->attachments.count; index++) {
        RpgAttachment *attachment = &data->attachments.entries[index];
        attachment->flagRaised = false;
        attachment->shooterAnimationElapsed = 0.0f;
        attachment->isZipperHeld = false;
    }
    for (int index = 0; index < data->signalBlocks.count; index++) {
        data->signalBlocks.entries[index].activeRemaining = 0.0f;
        data->signalBlocks.entries[index].previewRemaining = 0.0f;
    }
    data->signalBlocks.lastSignalSequence = 0;
    for (int index = 0; index < data->wires.count; index++)
        data->wires.entries[index].conveyorPreviewElapsed = 0.0f;
    for (int index = 0; index < data->items.count; index++)
        data->items.entries[index].collected = false;
    for (int index = 0; index < data->mapEvents.count; index++)
        data->mapEvents.entries[index].triggered = false;
    for (int index = 0; index < data->stage.imageObjects.count; index++) {
        RpgImageObject *object = &data->stage.imageObjects.entries[index];
        object->runtimeX = 0.0f;
        object->runtimeY = 0.0f;
        object->hasRuntimePosition = false;
    }
}

/* Keep the expensive package/build preparation off the input frame.  The
 * editor records the current composed static stage and publishes it only
 * after input has been idle for this short debounce interval.  This does not
 * touch the user's saved/revert snapshot. */
static bool BuildEditorPreviewCache(int stageNumber, const RpgLayout *layout, const RpgStage *stage)
{
    bool built;
    if (layout == NULL || stage == NULL || stageNumber <= 0) return false;
    editorPreviewBuildStage = *stage;
    built = RpgStageBuild_CreateEditorPreview(stageNumber, &editorPreviewBuildStage, &attachments,
                                               layout->playerPosition);
    /* A cache must not own the active watcher while the user is editing.  The
       generated editor runtime directory remains and Play reconnects it. */
    RpgStageBuild_Close();
    RpgObjectFolders_AbandonStageBuild();
    return built;
}

static void SyncEditorStageFolderWhenIdle(const RpgLayout *layout, const RpgStage *stage,
                                          const RpgDialogue *dialogue, const RpgStage3Event *stage3Event,
                                          const RpgItems *items, int stageNumber,
                                          RpgStageData *lastPublished, bool *hasLastPublished,
                                          int *lastPublishedStageNumber, double *nextComparisonTime,
                                          double *nextSyncTime,
                                          bool *previewBuildDirty, bool *previewBuildReady,
                                          int *previewBuildStageNumber)
{
    enum { EDITOR_STAGE_FOLDER_COMPARE_INTERVAL_MS = 200,
           EDITOR_STAGE_FOLDER_SYNC_DELAY_MS = 700 };
    bool stageChanged;
    double now;
    if (layout == NULL || stage == NULL || dialogue == NULL || stage3Event == NULL || items == NULL ||
        lastPublished == NULL || hasLastPublished == NULL || nextComparisonTime == NULL ||
        nextSyncTime == NULL || previewBuildDirty == NULL ||
        previewBuildReady == NULL || previewBuildStageNumber == NULL || stageNumber <= 0) return;
    if (lastPublishedStageNumber == NULL) return;
    now = GetTime();
    /* Do not copy and memcmp the complete editor stage at the display refresh
       rate.  A 200 ms observation interval is still much shorter than the
       700 ms idle debounce used before publishing the preview package. */
    if (*hasLastPublished && now < *nextComparisonTime &&
        (!*previewBuildDirty || now < *nextSyncTime)) return;
    *nextComparisonTime = now + (double)EDITOR_STAGE_FOLDER_COMPARE_INTERVAL_MS / 1000.0;
    ComposeEditorStageData(&stageFolderSyncBuffer, layout, stage, dialogue, stage3Event, items);
    NormalizeEditorPreviewSyncData(&stageFolderSyncBuffer);
    if (!*hasLastPublished || *lastPublishedStageNumber != stageNumber) {
        *lastPublished = stageFolderSyncBuffer;
        *hasLastPublished = true;
        *lastPublishedStageNumber = stageNumber;
        *previewBuildDirty = true;
        *previewBuildReady = false;
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
        return;
    }
    stageChanged = memcmp(lastPublished, &stageFolderSyncBuffer, sizeof(stageFolderSyncBuffer)) != 0;
    if (stageChanged) {
        *previewBuildDirty = true;
        /* The package is built after edits have actually gone quiet, rather
           than competing with continuous editing input. */
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
    }
    if (!stageChanged && !*previewBuildDirty) { *nextSyncTime = 0.0; return; }
    if (*nextSyncTime <= 0.0) {
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
        return;
    }
    if (now < *nextSyncTime) return;
    if (stageChanged && !RpgStageStorage_SaveStage(stageNumber, &stageFolderSyncBuffer)) {
        /* Retry later without blocking every editing frame on a transient IO error. */
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
        return;
    }
    if (stageChanged) *lastPublished = stageFolderSyncBuffer;
    if (BuildEditorPreviewCache(stageNumber, layout, stage)) {
        *previewBuildDirty = false;
        *previewBuildReady = true;
        *previewBuildStageNumber = stageNumber;
        *nextSyncTime = 0.0;
    } else {
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
    }
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
    bool zipperSaved = RpgZipper_Save(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg",
                                                  GetApplicationDirectory()), &zipperData);
    bool zipperInspectSaved = RpgInspect_Save(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg",
                                                         GetApplicationDirectory()), &zipperInspectData);
    bool globalRuntimeSaved = RpgLayout_SaveGlobalRuntime(layout);
    // 保存バッファは静的領域にある。巨大な複合リテラルをスタックへ作らず、
    // 各要素を直接更新して保存時のスタックオーバーフローを防ぐ。
    ComposeEditorStageData(&stageSaveBuffer, layout, stage, dialogue, stage3Event, items);
    bool stageFolderSaved = RpgStageStorage_SaveStage(currentStageNumber, &stageSaveBuffer);
    if (stageFolderSaved) {
        savedMapEvents = mapEvents;
        savedWires = wires;
        savedReceivers = receivers;
        savedAttachments = attachments;
        savedSignalBlocks = signalBlocks;
    }
    return zipperSaved && zipperInspectSaved && globalRuntimeSaved && stageFolderSaved;
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
    snapshot->areaEntryEvents = areaEntryEvents;
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
// 構成マスのどこをクリックしても、形状定義から先頭マスを逆算する。
static bool FindEffectBlockRoot(const RpgStage *stage, int row, int column, int *rootRow,
                                int *rootColumn, const RpgEffectShape **shapeResult)
{
    return RpgStage_FindEffectRootCell(stage, row, column, rootRow, rootColumn, shapeResult);
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

static bool PlaceConveyorProperty(BlockPropertyPlacementContext *context, RpgGridCell cell,
                                  const char **message)
{
    if (!RpgWires_AddAdjacentConveyor(context->wires, context->stage, cell.row, cell.column)) return false;
    PushWireAddedHistory(context->history, context->wires->count - 1);
    *message = "Conveyor path start and adjacent endpoint added";
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
    { RPG_BLOCK_PROPERTY_CONVEYOR, PlaceConveyorProperty },
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
static bool PlaceBlockType(RpgStage *stage, const RpgAttachments *attachments, BlockHistory *history,
                           int row, int column, int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    if (shape == NULL) {
        if (stage->blocks[row][column] == blockType ||
            RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ row, column })) return false;
        PushBlockHistory(history, row, column, stage->blocks[row][column], NULL);
        stage->blocks[row][column] = blockType;
        return true;
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = row + cell->offsetY;
        int targetColumn = column + cell->offsetX;
        if (targetRow < 0 || targetRow >= RPG_STAGE_ROWS || targetColumn < 0 ||
            targetColumn >= RPG_STAGE_WORLD_COLUMNS || stage->blocks[targetRow][targetColumn] != 0 ||
            RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ targetRow, targetColumn })) return false;
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = row + cell->offsetY;
        int targetColumn = column + cell->offsetX;
        PushBlockHistory(history, targetRow, targetColumn, 0, NULL);
        stage->blocks[targetRow][targetColumn] = cell->blockType;
    }
    if (RpgBlockInventory_IsKeyDoorBlock(blockType)) (void)RpgStage_EnsureKeyDoor(stage, row, column);
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
static bool MoveEffectBlock(RpgStage *stage, const RpgAttachments *attachments, RpgItems *items,
                            BlockHistory *history,
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
        if (RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ targetRow, targetColumn }) ||
            (!isSourceCell && stage->blocks[targetRow][targetColumn] != 0 &&
             !RpgBlockInventory_IsOrdinaryBlock(stage->blocks[targetRow][targetColumn])) ||
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
static bool MoveSpecialBlock(RpgStage *stage, const RpgAttachments *attachments, RpgItems *items,
                             BlockHistory *history,
                             RpgSignalBlocks *signalBlockList, int sourceRow, int sourceColumn,
                             int destinationRow, int destinationColumn)
{
    int signalIndex = RpgSignalBlocks_FindAtCell(signalBlockList, stage, sourceRow, sourceColumn);
    if (signalIndex < 0)
        return MoveEffectBlock(stage, attachments, items, history, sourceRow, sourceColumn,
                               destinationRow, destinationColumn);
    if (signalBlockList->entries[signalIndex].startsExpanded) {
        if (!MoveEffectBlock(stage, attachments, items, history, sourceRow, sourceColumn,
                             destinationRow, destinationColumn))
            return false;
        return RpgSignalBlocks_Move(signalBlockList, signalIndex, destinationRow, destinationColumn);
    }

    // 通常時に縮んでいる伸縮ブロックは根元だけを形状として移動する。
    RpgSignalBlock *signalBlock = &signalBlockList->entries[signalIndex];
    if (destinationRow < 0 || destinationRow >= RPG_STAGE_ROWS || destinationColumn < 0 ||
        destinationColumn >= RPG_STAGE_WORLD_COLUMNS ||
        RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ destinationRow, destinationColumn }) ||
        (stage->blocks[destinationRow][destinationColumn] != 0 &&
         !RpgBlockInventory_IsOrdinaryBlock(stage->blocks[destinationRow][destinationColumn])))
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
    PushBlockHistory(history, destinationRow, destinationColumn,
                     stage->blocks[destinationRow][destinationColumn], NULL);
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
    areaEntryEvents = snapshot->areaEntryEvents;
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
    if (firstFunction->type == RPG_INSPECT_WAIT)
        return firstFunction->wait.duration != secondFunction->wait.duration;
    if (firstFunction->type == RPG_INSPECT_LAYER_CHANGE)
        return firstFunction->layerChange.targetImageObjectId != secondFunction->layerChange.targetImageObjectId ||
               firstFunction->layerChange.layer != secondFunction->layerChange.layer;
    return firstFunction->move.target != secondFunction->move.target ||
           firstFunction->move.destinationX != secondFunction->move.destinationX ||
           firstFunction->move.destinationY != secondFunction->move.destinationY ||
           firstFunction->move.duration != secondFunction->move.duration ||
           firstFunction->move.nextFunctionDelay != secondFunction->move.nextFunctionDelay ||
           firstFunction->move.walkAnimationEnabled != secondFunction->move.walkAnimationEnabled ||
           firstFunction->move.walkAnimationSpeed != secondFunction->move.walkAnimationSpeed ||
           firstFunction->move.easing != secondFunction->move.easing ||
           firstFunction->move.axis != secondFunction->move.axis ||
           firstFunction->move.snapToGrid != secondFunction->move.snapToGrid ||
           firstFunction->move.targetImageObjectId != secondFunction->move.targetImageObjectId;
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
           snapshot->zipper.followSpeed != zipperData.followSpeed ||
           snapshot->zipper.launchPreviewEnabled != zipperData.launchPreviewEnabled ||
           IsInspectDifferent(&snapshot->stage3Event.inspect, &stage3Event->inspect) ||
           memcmp(&snapshot->areaEntryEvents, &areaEntryEvents, sizeof(areaEntryEvents)) != 0 ||
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
    return strcmp(snapshot->layout.backgroundPath, layout->backgroundPath) != 0 ||
            strcmp(snapshot->layout.groundBlockPath, layout->groundBlockPath) != 0 ||
            snapshot->layout.backgroundBrightness != layout->backgroundBrightness ||
            snapshot->layout.blockBrightness != layout->blockBrightness ||
            snapshot->layout.groundHue != layout->groundHue ||
            snapshot->layout.groundSaturation != layout->groundSaturation ||
            snapshot->layout.groundLightness != layout->groundLightness ||
            snapshot->layout.electricCellDelay != layout->electricCellDelay ||
            snapshot->layout.magnetMetalSpeed != layout->magnetMetalSpeed ||
            snapshot->layout.zipperFolderReturnDuration != layout->zipperFolderReturnDuration ||
            snapshot->layout.zipperFolderReturnAnimationDelay != layout->zipperFolderReturnAnimationDelay ||
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
        beforeEdit->layout.magnetMetalSpeed != layout->magnetMetalSpeed ||
        beforeEdit->layout.zipperFolderReturnDuration != layout->zipperFolderReturnDuration ||
        beforeEdit->layout.zipperFolderReturnAnimationDelay != layout->zipperFolderReturnAnimationDelay ||
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
    bool areaTopologyChanged = memcmp(snapshot->stage.mapActive, stage->mapActive,
                                      sizeof(stage->mapActive)) != 0 ||
                               memcmp(snapshot->stage.mapGridX, stage->mapGridX,
                                      sizeof(stage->mapGridX)) != 0 ||
                               memcmp(snapshot->stage.mapGridY, stage->mapGridY,
                                      sizeof(stage->mapGridY)) != 0;
    if (!SaveEditorData(layout, player, npc, stage, dialogue, stage3Event, items)) return false;
    /* A continue snapshot describes the old world topology.  Never resume it after area insertion,
       deletion, or relocation; the next play rebuilds from the newly published static stage. */
    if (areaTopologyChanged && !RpgStageStorage_ClearRuntimeState(currentStageNumber)) return false;
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
    char stagePath[RPG_STAGE_PATH_LENGTH];
    if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
        !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_layout.cfg", stagePath,
                                     (int)sizeof(stagePath)) || !RpgLayout_Save(stagePath, &savedLayout))
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
    if (activeInspect == &npcInspectData) *activeInspect = snapshot->npcInspectSnapshot;
    else if (activeInspect == &zipperInspectData) *activeInspect = snapshot->zipperInspectSnapshot;
    else if (currentStageEntryEvent != NULL && activeInspect == &currentStageEntryEvent->inspect) {
        *activeInspect = snapshot->stage3Event.inspect;
        currentStageEntryEvent->enabled = activeInspect->enabled;
    }
    else for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
        if (activeInspect == &areaEntryEvents.entries[index].inspect) {
            *activeInspect = snapshot->areaEntryEvents.entries[index].inspect;
            break;
        }
}

static void RevertEditedDialogue(bool isInspectDialogueEditing, bool isStage3DialogueEditing,
                                 bool isAreaEntryDialogueEditing,
                                 RpgDialogue *dialogue, RpgStage3Event *stage3Event,
                                 const EditorSaveSnapshot *snapshot)
{
    if (isInspectDialogueEditing) RevertActiveInspect(snapshot);
    else if (isStage3DialogueEditing) *stage3Event = snapshot->stage3Event;
    else if (isAreaEntryDialogueEditing) areaEntryEvents = snapshot->areaEntryEvents;
    else *dialogue = snapshot->dialogue;
}

static bool SaveActiveInspect(const RpgStage *stage, EditorSaveSnapshot *snapshot)
{
    if (activeInspect == &npcInspectData) {
        char stagePath[RPG_STAGE_PATH_LENGTH];
        if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
            !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_inspect.cfg", stagePath,
                                         (int)sizeof(stagePath)) || !RpgInspect_Save(stagePath, activeInspect)) return false;
    } else if (activeInspect == &zipperInspectData) {
        if (!RpgInspect_Save(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg",
                                        GetApplicationDirectory()), activeInspect)) return false;
    } else if (currentStageEntryEvent != NULL && activeInspect == &currentStageEntryEvent->inspect) {
        char stagePath[RPG_STAGE_PATH_LENGTH];
        currentStageEntryEvent->enabled = activeInspect->enabled;
        if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
            !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_stage_entry_event.cfg", stagePath,
                                         (int)sizeof(stagePath)) ||
            !RpgStage3Event_Save(stagePath, currentStageEntryEvent)) return false;
    } else {
        bool isAreaInspect = false;
        for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
            if (activeInspect == &areaEntryEvents.entries[index].inspect) {
                areaEntryEvents.entries[index].enabled = activeInspect->enabled;
                isAreaInspect = true;
                break;
            }
        char stagePath[RPG_STAGE_PATH_LENGTH];
        if (!isAreaInspect || !RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
            !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_area_entry_events.cfg", stagePath,
                                         (int)sizeof(stagePath)) ||
            !RpgAreaEntryEvents_Save(stagePath, stage, &areaEntryEvents)) return false;
    }
    if (activeInspect == &npcInspectData) snapshot->npcInspectSnapshot = npcInspectData;
    else if (activeInspect == &zipperInspectData) snapshot->zipperInspectSnapshot = zipperInspectData;
    else if (currentStageEntryEvent != NULL && activeInspect == &currentStageEntryEvent->inspect)
        snapshot->stage3Event = *currentStageEntryEvent;
    else snapshot->areaEntryEvents = areaEntryEvents;
    return true;
}

static bool SaveEditedDialogue(bool isInspectDialogueEditing, bool isStage3DialogueEditing,
                               bool isAreaEntryDialogueEditing,
                               const RpgStage *stage, RpgDialogue *dialogue, RpgStage3Event *stage3Event,
                               EditorSaveSnapshot *snapshot)
{
    if (isInspectDialogueEditing) return SaveActiveInspect(stage, snapshot);
    if (isStage3DialogueEditing) {
        char stagePath[RPG_STAGE_PATH_LENGTH];
        if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
            !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_stage_entry_event.cfg", stagePath,
                                         (int)sizeof(stagePath)) || !RpgStage3Event_Save(stagePath, stage3Event)) return false;
        snapshot->stage3Event = *stage3Event;
        return true;
    }
    if (isAreaEntryDialogueEditing) {
        char stagePath[RPG_STAGE_PATH_LENGTH];
        if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
            !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_area_entry_events.cfg", stagePath,
                                         (int)sizeof(stagePath)) || !RpgAreaEntryEvents_Save(stagePath, stage, &areaEntryEvents)) return false;
        snapshot->areaEntryEvents = areaEntryEvents;
        return true;
    }
    char stagePath[RPG_STAGE_PATH_LENGTH];
    if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
        !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_dialogue.txt", stagePath,
                                     (int)sizeof(stagePath)) || !RpgDialogue_Save(stagePath, dialogue)) return false;
    snapshot->dialogue = *dialogue;
    return true;
}

static int GetNextUtf8Index(const char *text, int index)
{
    int byteCount = 0;
    if (text[index] == '\0') return index;
    GetCodepointNext(text + index, &byteCount);
    return index + byteCount;
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
            GameFont_Draw(part, (float)x, (float)drawY, fontSize,
                          IsInspectorTextContext() ? BLACK : color);
            drawY += (int)fontSize + 2;
            lineStart = index;
        } else index = nextIndex;
    }
    GameFont_Draw(text + lineStart, (float)x, (float)drawY, fontSize,
                  IsInspectorTextContext() ? BLACK : color);
}

// 会話・話者・短い名称で共通のUTF-8テキスト編集操作を使う。
static void UpdateTextInput(char *text, size_t capacity, int *cursorIndex,
                            int *selectionAnchor, int *selectionEnd)
{
    RpgEditorText_UpdateInput(text, capacity, cursorIndex, selectionAnchor, selectionEnd);
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
    DrawSettingsText(isExamineFunctionEditor ? "調べる機能" : "キャラクター設定 - 会話", 156, 68, 22, BLACK);
    DrawRectangleRec(GetDialogueEditorCloseButton(), Fade(MAROON, 0.88f));
    DrawText("x", 797, 65, 16, RAYWHITE);
    DrawSettingsText(TextFormat("文字: %d", fontSize), 590, 68, 16, BLACK);
    DrawRectangleLines(680, 64, 42, 22, MAROON);
    DrawRectangleLines(730, 64, 42, 22, DARKGREEN);
    DrawText("-", 695, 67, 18, MAROON);
    DrawText("+", 745, 67, 18, DARKGREEN);
    DrawSettingsText(isExamineFunctionEditor ? "会話機能の設定" : "話者", 156, 101, 16, BLACK);
    DrawRectangle(224, 96, 260, 26, Fade(LIGHTGRAY, 0.55f));
    DrawRectangleLines(224, 96, 260, 26, isSpeakerEditing ? BLUE : GRAY);
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
                      IsInspectorTextContext() ? BLACK : (isActiveLineDirty ? MAROON : DARKBLUE));
        if (isSpeakerEditing) {
            char prefix[RPG_DIALOGUE_SPEAKER_LENGTH];
            memcpy(prefix, dialogue->speakers[activeLine], (size_t)speakerCursorIndex);
            prefix[speakerCursorIndex] = '\0';
            int cursorX = 232 + (int)GameFont_MeasureText(prefix, 17.0f).x;
            DrawTextCaret(cursorX, 99, 21);
        }
    } else DrawSettingsText("会話行を選択", 232, 102, 16, GRAY);
    DrawSettingsText(isExamineFunctionEditor ? "ドラッグ：順序 右クリック：削除" : "右クリック：削除 ホイール：スクロール", 500, 101, 12, BLACK);

    int visibleLineCount = GetVisibleDialogueLines(blockHeight);
    for (int visibleIndex = 0; visibleIndex < visibleLineCount; visibleIndex++) {
        int lineIndex = scroll + visibleIndex;
        int lineY = 128 + visibleIndex * (blockHeight + 4);
        if (lineIndex >= dialogue->lineCount) {
            // 現在の同じインデックスが空の場合だけ、保存前に存在した削除行を示す。
            if (lineIndex < savedDialogue->lineCount) {
                DrawRectangle(154, lineY, 652, blockHeight, Fade(MAROON, 0.06f));
                DrawRectangleLines(154, lineY, 652, blockHeight, Fade(MAROON, 0.32f));
                DrawSettingsText(TextFormat("%02d  削除済み", lineIndex + 1), 160,
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
        DrawSettingsText(isExamineFunctionEditor ? TextFormat("%02d  会話", lineIndex + 1) : TextFormat("%02d", lineIndex + 1),
                         160, lineY + (blockHeight - 14) / 2, 14, BLACK);
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
            DrawSettingsText(TextFormat("削除済み #%d", deletedIndex + 1), 727, lineY + 4, 10, Fade(MAROON, 0.65f));
        }
        if (lineIndex == activeLine) {
            char prefix[RPG_DIALOGUE_LINE_LENGTH];
            memcpy(prefix, dialogue->lines[lineIndex], (size_t)cursorIndex);
            prefix[cursorIndex] = '\0';
            int cursorX = textStartX + (int)GameFont_MeasureText(prefix, (float)fontSize).x;
            DrawTextCaret(cursorX, lineY + 3, blockHeight - 6);
        }
    }
    DrawSettingsText(isExamineFunctionEditor ? TextFormat("%d 個の機能", dialogue->lineCount) : TextFormat("%d 行", dialogue->lineCount), 156, 386, 16, BLACK);
    DrawRectangle(156, 414, 210, 32, PURPLE);
    DrawSettingsText(isExamineFunctionEditor ? "会話機能を追加" : "会話行を追加", 182, 422, 18, RAYWHITE);
    DrawSaveButton((Rectangle){ 382, 414, 116, 32 }, saveState);
    DrawRevertButton((Rectangle){ 510, 414, 116, 32 });
}

static void DrawNpcSummaryInspector(const RpgDialogue *dialogue, const RpgCharacter *npc,
                                    const RpgCharacter *savedNpc, EditorSaveState saveState)
{
    DrawInspectorFrame(npcInspectorBounds, "キャラクター設定", PURPLE, GetInspectorCloseButton(2));
    DrawSettingsText(TextFormat("会話: %d行", dialogue->lineCount), 716, 122, 16, BLACK);
    DrawSettingsText(TextFormat("拡大率: %.1f", npc->scale), 716, 144, 16,
                     npc->scale != savedNpc->scale ? MAROON : BLACK);
    DrawText("[-]", 812, 144, 16, MAROON);
    DrawText("[+]", 864, 144, 16, DARKGREEN);
    DrawRectangle(716, 170, 188, 32, PURPLE);
    DrawSettingsText("会話を編集", 750, 178, 18, RAYWHITE);
    DrawSaveButton((Rectangle){ 716, 208, 90, 26 }, saveState);
    DrawRevertButton((Rectangle){ 814, 208, 90, 26 });
}

/* PNGゴーストは元のレイヤーでだけ描き、前景・キャラ背面・背景の見え方を崩さない。 */
static void DrawImageObjectDragPreviewForLayer(const RpgStage *stage, RpgImageObjectLayer layer)
{
    if (!isImageObjectDragPreviewVisible) return;
    int imageIndex = RpgImageObjects_FindById(&stage->imageObjects, imageObjectDragPreviewId);
    if (imageIndex < 0) return;
    const RpgImageObject *object = &stage->imageObjects.entries[imageIndex];
    if (object->layer != layer) return;
    float size = RPG_STAGE_TILE_SIZE * Clamp(object->scale, 0.25f, 8.0f);
    Rectangle ghostBounds = { imageObjectDragPointer.x - size * 0.5f,
                              imageObjectDragPointer.y - size * 0.5f, size, size };
    RpgImageObjects_DrawPreview(object, ghostBounds, Fade(WHITE, 0.70f));
}

/* キャラクターのゴーストも実体と同じ描画レイヤーへ置く。 */
static void DrawCharacterDragPreview(Texture2D zipperTexture, const RpgCharacter *player,
                                     const RpgCharacter *npc, const RpgZipper *zipper,
                                     int mapIndex, int kind)
{
    if (!isCharacterDragPreviewVisible || characterDragPreviewKind != kind) return;
    RpgCharacter preview = kind == EDITOR_MAP_OBJECT_HIT_PLAYER ? *player :
                           kind == EDITOR_MAP_OBJECT_HIT_NPC ? *npc : zipper->character;
    preview.position.x = characterDragPointer.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    preview.position.y = characterDragPointer.y;
    preview = GetLocalCharacter(&preview, mapIndex);
    if (kind == EDITOR_MAP_OBJECT_HIT_PLAYER) {
        RpgCharacter_DrawPlayerTinted(&preview, RPG_CHARACTER_ANIMATION_IDLE, Fade(WHITE, 0.65f));
    } else if (kind == EDITOR_MAP_OBJECT_HIT_NPC) {
        RpgCharacter_DrawTinted(&preview, "NPC", Fade(WHITE, 0.65f));
    } else {
        Rectangle source = { 0.0f, 0.0f, 32.0f, 40.0f };
        Rectangle destination = RpgZipper_GetPixelAlignedSpriteBounds(&preview, 380.0f);
        DrawRectangleRounded((Rectangle){ destination.x - 3.0f, destination.y - 3.0f,
                                          destination.width + 6.0f, destination.height + 6.0f },
                             0.18f, 4, Fade(DARKBLUE, 0.18f));
        DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f,
                       Fade(WHITE, 0.65f));
    }
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

static void DrawMovePreviewSprite(Texture2D zipperTexture, const RpgCharacter *player,
                                  const RpgCharacter *npc, const RpgZipper *zipper,
                                  const RpgStage *stage, const RpgInspectMove *move,
                                  Vector2 worldPosition, float previewElapsed, int mapIndex)
{
    if ((int)(worldPosition.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) != mapIndex) return;
    float localX = worldPosition.x - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && stage != NULL) {
        int imageIndex = RpgImageObjects_FindById(&stage->imageObjects, move->targetImageObjectId);
        if (imageIndex < 0) return;
        RpgImageObject preview = stage->imageObjects.entries[imageIndex];
        RpgImageObjects_SetRuntimePosition(&preview, worldPosition);
        RpgImageObjects_DrawPreview(&preview,
                                    RpgImageObjects_GetLocalBounds(&preview, mapIndex, RPG_STAGE_COLUMNS,
                                                                   RPG_STAGE_TILE_SIZE), Fade(WHITE, 0.75f));
    } else if (move->target == RPG_INSPECT_MOVE_ZIPPER) {
        Rectangle source = { 0, 0, 32, 40 };
        Rectangle destination = { localX - 24.0f * zipper->character.scale, worldPosition.y - 60.0f * zipper->character.scale,
                                  48.0f * zipper->character.scale, 60.0f * zipper->character.scale };
        DrawTexturePro(zipperTexture, source, destination, (Vector2){0}, 0, Fade(WHITE, 0.75f));
    } else {
        RpgCharacter sprite = move->target == RPG_INSPECT_MOVE_PLAYER ? *player : *npc;
        sprite.position = (Vector2){ localX, worldPosition.y };
        if (move->target == RPG_INSPECT_MOVE_PLAYER) {
            sprite.isMoving = move->walkAnimationEnabled;
            sprite.animationElapsed = previewElapsed * move->walkAnimationSpeed;
            RpgCharacter_DrawPlayer(&sprite, move->walkAnimationEnabled ? RPG_CHARACTER_ANIMATION_WALK : RPG_CHARACTER_ANIMATION_IDLE);
        }
        else RpgCharacter_Draw(&sprite, "");
    }
}

enum { EDITOR_FUNCTION_LIST_VISIBLE_ROWS = 6, EDITOR_FUNCTION_LIST_ROW_HEIGHT = 34 };

static void DrawExamineFunctionList(const RpgInspect *inspect, int selectedIndex, int scrollIndex,
                                    int draggedIndex, bool isTitleEditing, int titleCursorIndex,
                                    int titleSelectionAnchor, int titleSelectionEnd,
                                    const RpgInspect *savedInspect, bool isFunctionPreviewPlaying)
{
    const Rectangle panel = { 190.0f, 86.0f, 580.0f, 360.0f };
    DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
    DrawRectangleRec(panel, Fade(RAYWHITE, 0.98f));
    DrawRectangleLinesEx(panel, 2.0f, DARKBLUE);
    DrawSettingsText("調べる機能", 212, 104, 24, BLACK);
    DrawRectangle(648, 100, 78, 24, inspect->enabled ? DARKGREEN : GRAY);
    DrawText(inspect->enabled ? "ON" : "OFF", 672, 104, 16, RAYWHITE);
    DrawSettingsText("タイトル", 212, 136, 16, BLACK);
    DrawRectangle(260, 132, 368, 24, Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(260, 132, 368, 24, isTitleEditing ? BLUE : GRAY);
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
                  IsInspectorTextContext() ? BLACK : (isSelectedDirty ? MAROON : BLACK));
    if (isTitleEditing) {
        char prefix[RPG_INSPECT_TITLE_LENGTH];
        memcpy(prefix, inspect->functions[selectedIndex].title, (size_t)titleCursorIndex);
        prefix[titleCursorIndex] = '\0';
        int cursorX = 268 + (int)GameFont_MeasureText(prefix, 16.0f).x;
        DrawTextCaret(cursorX, 134, 20);
    }
    int firstVisibleIndex = Clamp(scrollIndex, 0,
                                  inspect->functionCount > EDITOR_FUNCTION_LIST_VISIBLE_ROWS ?
                                      inspect->functionCount - EDITOR_FUNCTION_LIST_VISIBLE_ROWS : 0);
    int lastVisibleIndex = firstVisibleIndex + EDITOR_FUNCTION_LIST_VISIBLE_ROWS;
    if (lastVisibleIndex > inspect->functionCount) lastVisibleIndex = inspect->functionCount;
    for (int index = firstVisibleIndex; index < lastVisibleIndex; index++) {
        int y = 166 + (index - firstVisibleIndex) * EDITOR_FUNCTION_LIST_ROW_HEIGHT;
        bool isFunctionDirty = index >= savedInspect->functionCount ||
                               IsInspectFunctionDifferent(&inspect->functions[index], &savedInspect->functions[index]);
        int deletedIndex = GetShiftedDeletedFunctionIndex(inspect, savedInspect, index);
        DrawRectangle(212, y, 536, 28, index == draggedIndex ? Fade(ORANGE, 0.45f) :
                      isFunctionDirty ? Fade(RED, 0.18f) : Fade(LIGHTGRAY, 0.6f));
        DrawRectangleLines(212, y, 536, 28, index == draggedIndex ? ORANGE : isFunctionDirty ? MAROON : GRAY);
        const RpgInspectFunction *function = &inspect->functions[index];
        char detail[48];
        if (function->type == RPG_INSPECT_MOVE) snprintf(detail, sizeof(detail), "移動");
        else if (function->type == RPG_INSPECT_WAIT) snprintf(detail, sizeof(detail), "待機 %.1f秒", function->wait.duration);
        else if (function->type == RPG_INSPECT_LAYER_CHANGE) snprintf(detail, sizeof(detail), "描画レイヤー");
        else snprintf(detail, sizeof(detail), "%d行", function->dialogue.lineCount);
        DrawSettingsText(TextFormat("%02d  %s  （%s）", index + 1, function->title, detail), 226, y + 6, 17,
                         isFunctionDirty ? MAROON : BLACK);
        if (deletedIndex >= 0) {
            DrawRectangle(662, y + 4, 82, 18, Fade(MAROON, 0.14f));
            DrawSettingsText(TextFormat("削除済み #%d", deletedIndex + 1), 665, y + 7, 10, Fade(MAROON, 0.65f));
        }
    }
    // 現在の同じインデックスが空の場合だけ、保存前に存在した削除Functionを表示する。
    for (int index = inspect->functionCount; index < savedInspect->functionCount; index++) {
        if (index < firstVisibleIndex || index >= lastVisibleIndex) continue;
        int y = 166 + (index - firstVisibleIndex) * EDITOR_FUNCTION_LIST_ROW_HEIGHT;
        DrawRectangle(212, y, 536, 28, Fade(MAROON, 0.06f));
        DrawRectangleLines(212, y, 536, 28, Fade(MAROON, 0.32f));
        DrawSettingsText(TextFormat("%02d  削除済み", index + 1), 226, y + 6, 17, Fade(MAROON, 0.45f));
    }
    DrawRectangle(212, 394, 210, 30, PURPLE);
    DrawSettingsText("機能を追加", 260, 401, 17, RAYWHITE);
    DrawRectangle(430, 394, 210, 30, isFunctionPreviewPlaying ? MAROON : DARKGREEN);
    DrawSettingsText(isFunctionPreviewPlaying ? "プレビュー実行中" : "全機能をプレビュー", 448, 401, 16, RAYWHITE);
    DrawText(TextFormat("%d-%d / %d", firstVisibleIndex + 1, lastVisibleIndex, inspect->functionCount),
             638, 139, 12, DARKGRAY);
    DrawRectangle(730, 96, 24, 24, MAROON);
    DrawText("x", 735, 99, 18, RAYWHITE);
}

static void DrawFunctionTypeList(void)
{
    const Rectangle panel = { 300.0f, 130.0f, 360.0f, 280.0f };
    DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
    DrawRectangleRec(panel, Fade(RAYWHITE, 0.98f));
    DrawRectangleLinesEx(panel, 2.0f, DARKBLUE);
    DrawSettingsText("機能を追加", 324, 172, 24, BLACK);
    DrawSettingsText("機能の種類を選択", 324, 204, 16, BLACK);
    DrawRectangle(324, 236, 312, 36, PURPLE);
    DrawSettingsText("会話", 344, 245, 20, RAYWHITE);
    DrawRectangle(324, 280, 312, 36, DARKBLUE);
    DrawSettingsText("移動", 344, 289, 20, RAYWHITE);
    DrawRectangle(324, 324, 312, 36, DARKGREEN);
    DrawSettingsText("待機", 344, 333, 20, RAYWHITE);
    DrawRectangle(324, 368, 312, 36, MAROON);
    DrawSettingsText("描画レイヤー変更", 344, 377, 18, RAYWHITE);
    DrawRectangle(620, 140, 22, 22, MAROON);
    DrawText("x", 625, 142, 17, RAYWHITE);
}

static Rectangle GetMoveEasingListItem(int index)
{
    return GetMovePanelControl(16.0f, 254.0f + index * 26.0f, 244.0f, 24.0f);
}

static void DrawMoveFunctionEditor(const RpgInspectMove *move, bool isPreviewPlaying,
                                   bool isEasingListOpen, EditorSaveState saveState, bool isMoveDirty)
{
    const char *targetName = GetMoveTargetName(move);
    const int x = (int)movePanelBounds.x, y = (int)movePanelBounds.y;
    DrawRectangleRec(movePanelBounds, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(movePanelBounds, 2.0f, PURPLE);
    DrawSettingsText("移動機能", x + 16, y + 16, 21, BLACK);
    DrawRectangleRec(GetMovePanelControl(250, 10, 20, 20), MAROON);
    DrawText("x", x + 255, y + 12, 16, RAYWHITE);
    DrawSettingsText("対象", x + 16, y + 50, 17, BLACK);
    DrawRectangleRec(GetMovePanelControl(16, 74, 244, 28), isMoveDirty ? MAROON : PURPLE);
    DrawSettingsText(TextFormat("対象を選択: %s", targetName), x + 30, y + 80, 16, RAYWHITE);
    DrawSettingsText("移動軸", x + 16, y + 112, 17, BLACK);
    for (int axis = 0; axis < RPG_INSPECT_MOVE_AXIS_COUNT; axis++) {
        Rectangle button = GetMovePanelControl(16.0f + axis * 82.0f, 136.0f, 78.0f, 24.0f);
        DrawRectangleRec(button, axis == (int)move->axis ? PURPLE : DARKBLUE);
        DrawText(RpgInspect_MoveAxisName((RpgInspectMoveAxis)axis), (int)button.x + 7, (int)button.y + 4, 13, RAYWHITE);
    }
    if (move->target == RPG_INSPECT_MOVE_PLAYER) {
        DrawSettingsText("歩行", x + 16, y + 168, 17, BLACK);
        DrawRectangleRec(GetMovePanelControl(66, 164, 62, 24), move->walkAnimationEnabled ? DARKGREEN : GRAY);
        DrawText(move->walkAnimationEnabled ? "ON" : "OFF", x + 82, y + 169, 14, RAYWHITE);
        DrawSettingsText("速度", x + 142, y + 168, 15, BLACK);
        DrawRectangleRec(GetMovePanelControl(194, 164, 26, 24), MAROON);
        DrawText("-", x + 203, y + 167, 18, RAYWHITE);
        DrawRectangleRec(GetMovePanelControl(230, 164, 26, 24), DARKGREEN);
        DrawText("+", x + 238, y + 167, 18, RAYWHITE);
        DrawSettingsText(TextFormat("%.1fx", move->walkAnimationSpeed), x + 145, y + 186, 14, BLACK);
    } else {
        DrawSettingsText("歩行アニメーション：利用不可", x + 16, y + 174, 14, GRAY);
    }
    DrawSettingsText("補間", x + 16, y + 204, 17, BLACK);
    DrawRectangleRec(GetMovePanelControl(16, 228, 244, 26), DARKBLUE);
    DrawText(RpgInspect_MoveEasingName(move->easing), x + 28, y + 233, 15, RAYWHITE);
    DrawSettingsText("終点", x + 16, y + 264, 17, BLACK);
    DrawSettingsText(TextFormat("X %.0f   Y %.0f", move->destinationX, move->destinationY),
                     x + 16, y + 286, 18, isMoveDirty ? MAROON : BLACK);
    DrawSettingsText("パネル外をクリックして設定", x + 16, y + 308, 14, BLACK);
    DrawSettingsText("プレビューは選択した対象から開始", x + 16, y + 326, 14, BLACK);
    DrawSettingsText("時間", x + 16, y + 342, 17, BLACK);
    DrawRectangleRec(GetMovePanelControl(16, 366, 38, 28), MAROON);
    DrawText("-", x + 29, y + 369, 21, RAYWHITE);
    DrawSettingsText(TextFormat("%.1f秒", move->duration), x + 66, y + 371, 19, isMoveDirty ? MAROON : BLACK);
    DrawRectangleRec(GetMovePanelControl(152, 366, 38, 28), DARKGREEN);
    DrawText("+", x + 165, y + 369, 21, RAYWHITE);
    DrawSettingsText("次の機能まで", x + 16, y + 402, 17, BLACK);
    DrawRectangleRec(GetMovePanelControl(16, 426, 38, 28), MAROON);
    DrawText("-", x + 29, y + 429, 21, RAYWHITE);
    DrawSettingsText(TextFormat("%.1f秒", move->nextFunctionDelay), x + 66, y + 431, 19,
                     isMoveDirty ? MAROON : BLACK);
    DrawRectangleRec(GetMovePanelControl(152, 426, 38, 28), DARKGREEN);
    DrawText("+", x + 165, y + 429, 21, RAYWHITE);
    DrawRectangleRec(GetMovePanelControl(16, 462, 244, 22), isPreviewPlaying ? MAROON : DARKGREEN);
    DrawSettingsText(isPreviewPlaying ? "プレビューを停止" : TextFormat("プレビュー: %s", targetName), x + 54, y + 464, 16, RAYWHITE);
    DrawSaveButton(GetMovePanelControl(16, 492, 116, 24), saveState);
    DrawRevertButton(GetMovePanelControl(144, 492, 116, 24));
    /* 補間一覧は最後に描画し、背後の設定表示に隠れないようにする。 */
    if (isEasingListOpen) {
        for (int index = 0; index < RPG_INSPECT_EASING_COUNT; index++) {
            Rectangle item = GetMoveEasingListItem(index);
            DrawRectangleRec(item, index == (int)move->easing ? PURPLE : Fade(DARKBLUE, 0.97f));
            DrawRectangleLinesEx(item, 1.0f, RAYWHITE);
            DrawText(RpgInspect_MoveEasingName((RpgInspectMoveEasing)index), (int)item.x + 12,
                     (int)item.y + 4, 14, RAYWHITE);
        }
    }
}

/* Wait/Layer ChangeはMoveと同じ可動パネルと保存操作を使い、Functionごとの差分だけを描画する。 */
static void DrawWaitFunctionEditor(const RpgInspectWait *wait, EditorSaveState saveState, bool isDirty)
{
    int x = (int)movePanelBounds.x, y = (int)movePanelBounds.y;
    DrawRectangleRec(movePanelBounds, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(movePanelBounds, 2.0f, DARKGREEN);
    DrawSettingsText("待機機能", x + 16, y + 16, 21, BLACK);
    DrawRectangleRec(GetMovePanelControl(250, 10, 20, 20), MAROON);
    DrawText("x", x + 255, y + 12, 16, RAYWHITE);
    DrawSettingsText("待機時間", x + 16, y + 70, 18, BLACK);
    DrawRectangleRec(GetMovePanelControl(16, 98, 42, 30), MAROON);
    DrawText("-", x + 31, y + 102, 22, RAYWHITE);
    DrawSettingsText(TextFormat("%.1f秒", wait->duration), x + 76, y + 104, 21, isDirty ? MAROON : BLACK);
    DrawRectangleRec(GetMovePanelControl(186, 98, 42, 30), DARKGREEN);
    DrawText("+", x + 199, y + 102, 22, RAYWHITE);
    DrawSettingsText("待機が終わると次の機能を開始します。", x + 16, y + 150, 14, BLACK);
    DrawSaveButton(GetMovePanelControl(16, 492, 116, 24), saveState);
    DrawRevertButton(GetMovePanelControl(144, 492, 116, 24));
}

static void DrawLayerChangeFunctionEditor(const RpgInspectLayerChange *change, const RpgStage *stage,
                                          bool isPicking, EditorSaveState saveState, bool isDirty)
{
    int x = (int)movePanelBounds.x, y = (int)movePanelBounds.y;
    const char *layerNames[] = { "Back", "In front of blocks", "Front" };
    int imageIndex = RpgImageObjects_FindById(&stage->imageObjects, change->targetImageObjectId);
    DrawRectangleRec(movePanelBounds, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(movePanelBounds, 2.0f, MAROON);
    DrawSettingsText("描画レイヤー変更", x + 16, y + 16, 21, BLACK);
    DrawRectangleRec(GetMovePanelControl(250, 10, 20, 20), MAROON);
    DrawText("x", x + 255, y + 12, 16, RAYWHITE);
    DrawSettingsText("対象画像", x + 16, y + 66, 17, BLACK);
    DrawRectangleRec(GetMovePanelControl(16, 90, 244, 28), isDirty ? MAROON : DARKBLUE);
    DrawSettingsText(imageIndex >= 0 ? "別の画像を選択" : "画像オブジェクトを選択", x + 56, y + 96, 16, RAYWHITE);
    DrawSettingsText("新しいレイヤー", x + 16, y + 146, 17, BLACK);
    for (int layer = 0; layer < 3; layer++) {
        Rectangle button = GetMovePanelControl(16, 174 + layer * 34, 244, 28);
        DrawRectangleRec(button, layer == change->layer ? MAROON : DARKBLUE);
        DrawText(layerNames[layer], x + 30, (int)button.y + 6, 16, RAYWHITE);
    }
    DrawSettingsText(isPicking ? "一番手前の画像オブジェクトをクリック" : "機能の順番で直ちにレイヤーを変更します。",
                     x + 16, y + 292, 14, BLACK);
    DrawSaveButton(GetMovePanelControl(16, 492, 116, 24), saveState);
    DrawRevertButton(GetMovePanelControl(144, 492, 116, 24));
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
            DrawCircleLines((int)localX, (int)mapEvents.entries[index].position.y, 11.0f, ORANGE);
            DrawText("E", (int)localX - 5, (int)mapEvents.entries[index].position.y - 7, 14, ORANGE);
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
    Color itemTextColor = isItemUnsaved ? MAROON : BLACK;
    DrawInspectorFrame((Rectangle){ 700, 80, 220, 150 }, "アイテム設定", GOLD, GetInspectorCloseButton(4));
    DrawSettingsText("名前", 716, 122, 16, isItemUnsaved ? MAROON : BLACK);
    DrawRectangle(716, 144, 188, 28, Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 144, 188, 28, isItemUnsaved ? MAROON : (isNameEditing ? BLUE : GRAY));
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
    GameFont_Draw(item->name, 724, 149, 17,
                  IsInspectorTextContext() ? BLACK : itemTextColor);
    if (isNameEditing) {
        char prefix[RPG_ITEM_NAME_LENGTH];
        memcpy(prefix, item->name, (size_t)cursorIndex);
        prefix[cursorIndex] = '\0';
        int cursorX = 724 + (int)GameFont_MeasureText(prefix, 17.0f).x;
        DrawTextCaret(cursorX, 147, 22);
    }
    DrawSettingsText(isItemUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 188, 16,
                     isItemUnsaved ? MAROON : BLACK);
}

static void DrawConveyorInspector(int wireIndex, float previewRemaining)
{
    Rectangle bounds = GetInspectorBounds(RPG_EDITOR_CONVEYOR_INSPECTOR);
    DrawInspectorFrame(bounds, "コンベア設定", GOLD,
                       GetInspectorCloseButton(RPG_EDITOR_CONVEYOR_INSPECTOR));
    if (wireIndex < 0 || wireIndex >= wires.count || !RpgWires_IsConveyor(&wires.entries[wireIndex])) {
        DrawSettingsText("コンベアの終点を選択", 716, 132, 15, BLACK);
        return;
    }
    const RpgWire *conveyor = &wires.entries[wireIndex];
    DrawSettingsText(TextFormat("軌道ブロック: %d", conveyor->path.cellCount), 716, 126, 16, BLACK);
    DrawSettingsText("回転速度", 716, 158, 16, BLACK);
    DrawRectangle(716, 182, 42, 26, DARKGRAY);
    DrawRectangle(764, 182, 92, 26, Fade(GOLD, 0.80f));
    DrawRectangle(862, 182, 42, 26, DARKGRAY);
    DrawText("-", 731, 186, 20, RAYWHITE);
    DrawSettingsText(TextFormat("%.0f px/秒", conveyor->conveyorSpeed), 774, 188, 15, BLACK);
    DrawText("+", 878, 186, 20, RAYWHITE);
    DrawSettingsText("方向", 716, 222, 16, BLACK);
    DrawRectangle(716, 246, 188, 26, conveyor->conveyorDirection >= 0 ? DARKBLUE : MAROON);
    DrawSettingsText(conveyor->conveyorDirection >= 0 ? "軌道に沿って正方向" : "軌道に沿って逆方向",
                     735, 252, 13, RAYWHITE);
    DrawSettingsText("外側の床・壁", 716, 282, 16, BLACK);
    DrawRectangle(716, 306, 188, 26, conveyor->conveyorHasFloor ? DARKGREEN : DARKGRAY);
    DrawSettingsText(conveyor->conveyorHasFloor ? "有効" : "無効", 774, 312, 16, RAYWHITE);
    DrawSettingsText("最小滑落角", 716, 342, 16, BLACK);
    DrawRectangle(716, 366, 42, 26, DARKGRAY);
    DrawRectangle(764, 366, 92, 26, Fade(GOLD, 0.80f));
    DrawRectangle(862, 366, 42, 26, DARKGRAY);
    DrawText("-", 731, 370, 20, RAYWHITE);
    DrawSettingsText(TextFormat("%.0f度", conveyor->conveyorSlideAngleDegrees), 779, 372, 15, BLACK);
    DrawText("+", 878, 370, 20, RAYWHITE);
    DrawRectangle(716, 406, 188, 26, previewRemaining > 0.0f ? MAROON : DARKGREEN);
    DrawSettingsText(previewRemaining > 0.0f ? TextFormat("プレビュー %.1f秒", previewRemaining) : "3秒プレビュー",
                     746, 412, 15, RAYWHITE);
}

/* パレットはエディター専用の表示設定として、ブロック選択UIから独立した
 * インスペクターで編集する。ブロック種別配列そのものは今回変更しない。 */
static void DrawPaletteInspector(int paletteIndex, bool isNameEditing, int nameCursorIndex,
                                 int nameSelectionAnchor, int nameSelectionEnd)
{
    const RpgBlockInventory *inventory = RpgBlockInventory_Get(paletteIndex);
    static const char *const colorLabels[RPG_BLOCK_INVENTORY_BORDER_COLOR_COUNT] =
        { "白", "赤", "青", "黄" };
    DrawInspectorFrame(GetInspectorBounds(RPG_EDITOR_PALETTE_INSPECTOR), "パレット設定",
                       GetBlockInventoryBorderColor(inventory->borderColor),
                       GetInspectorCloseButton(RPG_EDITOR_PALETTE_INSPECTOR));
    DrawSettingsText(TextFormat("パレット %d", paletteIndex + 1), 716.0f, 120.0f, 16.0f, BLACK);
    DrawSettingsText("名前", 716.0f, 142.0f, 16.0f, BLACK);
    Rectangle nameBounds = { 716.0f, 162.0f, 188.0f, 28.0f };
    DrawRectangleRec(nameBounds, Fade(RAYWHITE, 0.90f));
    DrawRectangleLinesEx(nameBounds, isNameEditing ? 2.0f : 1.0f,
                         isNameEditing ? BLUE : GetBlockInventoryBorderColor(inventory->borderColor));
    if (isNameEditing && nameSelectionAnchor != nameSelectionEnd) {
        int start = nameSelectionAnchor < nameSelectionEnd ? nameSelectionAnchor : nameSelectionEnd;
        int end = nameSelectionAnchor < nameSelectionEnd ? nameSelectionEnd : nameSelectionAnchor;
        char prefix[RPG_BLOCK_INVENTORY_NAME_LENGTH];
        memcpy(prefix, inventory->name, (size_t)start);
        prefix[start] = '\0';
        float startX = nameBounds.x + 8.0f + GameFont_MeasureText(prefix, 15.0f).x;
        memcpy(prefix, inventory->name, (size_t)end);
        prefix[end] = '\0';
        float endX = nameBounds.x + 8.0f + GameFont_MeasureText(prefix, 15.0f).x;
        DrawRectangleRec((Rectangle){ startX, nameBounds.y + 4.0f, endX - startX, 20.0f },
                         Fade(SKYBLUE, 0.65f));
    }
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, inventory->name, nameBounds.x + 8.0f, nameBounds.y + 6.0f,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 15.0f));
    if (isNameEditing) {
        char prefix[RPG_BLOCK_INVENTORY_NAME_LENGTH];
        int safeCursor = Clamp(nameCursorIndex, 0, (int)strlen(inventory->name));
        memcpy(prefix, inventory->name, (size_t)safeCursor);
        prefix[safeCursor] = '\0';
        DrawTextCaret((int)(nameBounds.x + 8.0f + GameFont_MeasureText(prefix, 15.0f).x),
                      (int)nameBounds.y + 4, 20);
    }
    DrawSettingsText("囲い色", 716.0f, 202.0f, 16.0f, BLACK);
    for (int colorIndex = 0; colorIndex < RPG_BLOCK_INVENTORY_BORDER_COLOR_COUNT; colorIndex++) {
        RpgBlockInventoryBorderColor color = (RpgBlockInventoryBorderColor)colorIndex;
        Rectangle colorBounds = { 716.0f + colorIndex * 47.0f, 224.0f, 43.0f, 26.0f };
        DrawRectangleRec(colorBounds, GetBlockInventoryBorderColor(color));
        DrawRectangleLinesEx(colorBounds, inventory->borderColor == color ? 3.0f : 1.0f,
                             inventory->borderColor == color ? BLACK : GRAY);
        DrawSettingsText(colorLabels[colorIndex], colorBounds.x + 13.0f, colorBounds.y + 5.0f,
                         14.0f, BLACK);
    }
}

static const char *GetReferenceLeafName(const char *path)
{
    const char *backslash = path == NULL ? NULL : strrchr(path, '\\');
    const char *slash = path == NULL ? NULL : strrchr(path, '/');
    const char *leaf = backslash != NULL ? backslash + 1 : path;
    return slash != NULL && slash + 1 > leaf ? slash + 1 : leaf;
}

/* Legacy objects predate sourcePath.  They have only a working build path, so
 * use this as a read-only fallback until the author selects a source again. */
static const char *GetReferenceRelativeDisplayPath(const char *path)
{
    const char *referenceFiles = path == NULL ? NULL : strstr(path, "\\reference_files\\");
    const char *folderDefinitions = path == NULL ? NULL : strstr(path, "\\folder_defs\\");
    if (referenceFiles != NULL) return referenceFiles + 1;
    if (folderDefinitions != NULL) return folderDefinitions + 1;
    if (path != NULL && ((path[0] != '\0' && path[1] == ':') ||
                         (path[0] == '\\' && path[1] == '\\')))
        return GetReferenceLeafName(path);
    return path != NULL ? path : "";
}

/* Every inspector uses the same picker model: the caller owns the base
 * directory/filter/title, while this shared view owns the surrounding UI. */
typedef struct RpgEditorFilePickerLayout {
    float baseY;
    Rectangle valueBounds;
    Rectangle selectBounds;
    float contentBottom;
} RpgEditorFilePickerLayout;

/* Folder の名前欄と操作ボタンは、描画と入力判定で必ず同じ矩形を使う。
 * ファイル用ピッカーが可変高になっても Folder の操作領域へ影響しない。 */
typedef struct RpgEditorFolderInspectorLayout {
    Rectangle nameBounds;
    Rectangle openBounds;
    Rectangle renameBounds;
    float statusY;
} RpgEditorFolderInspectorLayout;

static RpgEditorFolderInspectorLayout GetFolderInspectorLayout(void)
{
    return (RpgEditorFolderInspectorLayout){
        .nameBounds = { 716.0f, 144.0f, 188.0f, 28.0f },
        .openBounds = { 716.0f, 180.0f, 188.0f, 28.0f },
        .renameBounds = { 716.0f, 214.0f, 188.0f, 28.0f },
        .statusY = 258.0f
    };
}

/* 文字と操作ボタンを同じ行で奪い合わない共通行。長い説明はlabelBoundsの
   中だけを使い、操作は常にその下のactionBoundsへ置く。 */
typedef struct RpgEditorInspectorActionRow {
    Rectangle labelBounds;
    Rectangle actionBounds;
    float contentBottom;
} RpgEditorInspectorActionRow;

static RpgEditorInspectorActionRow MakeInspectorActionRow(float x, float y, float width,
                                                           float actionHeight)
{
    const float labelHeight = 18.0f;
    const float rowGap = 6.0f;
    RpgEditorInspectorActionRow row = {
        { x, y, width, labelHeight },
        { x, y + labelHeight + rowGap, width, actionHeight },
        y + labelHeight + rowGap + actionHeight
    };
    return row;
}

static int GetUtf8CharacterSize(unsigned char leadingByte)
{
    if ((leadingByte & 0x80u) == 0) return 1;
    if ((leadingByte & 0xe0u) == 0xc0u) return 2;
    if ((leadingByte & 0xf0u) == 0xe0u) return 3;
    if ((leadingByte & 0xf8u) == 0xf0u) return 4;
    return 1;
}

static int GetInspectorWrappedLineCount(const char *text, float fontSize, float availableWidth)
{
    if (text == NULL || text[0] == '\0') return 1;
    if (availableWidth <= 1.0f) return 1;
    char line[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
    int lineLength = 0;
    int lineCount = 1;
    for (int index = 0; text[index] != '\0'; ) {
        int charSize = GetUtf8CharacterSize((unsigned char)text[index]);
        if (charSize > (int)sizeof(line) - lineLength - 1) charSize = 1;
        memcpy(line + lineLength, text + index, (size_t)charSize);
        line[lineLength + charSize] = '\0';
        if (lineLength > 0 && GameFont_MeasureText(line, fontSize).x > availableWidth) {
            lineCount++;
            lineLength = 0;
            memcpy(line, text + index, (size_t)charSize);
            line[charSize] = '\0';
            lineLength = charSize;
        } else lineLength += charSize;
        index += charSize;
    }
    return lineCount;
}

static void DrawInspectorWrappedText(const char *text, Rectangle bounds, float fontSize, Color color)
{
    if (text == NULL || text[0] == '\0') return;
    const float lineHeight = fontSize + 3.0f;
    const float availableWidth = fmaxf(1.0f, bounds.width);
    char line[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
    int lineLength = 0;
    float y = bounds.y;
    for (int index = 0; ; ) {
        bool atEnd = text[index] == '\0';
        int charSize = atEnd ? 0 : GetUtf8CharacterSize((unsigned char)text[index]);
        if (!atEnd && charSize > (int)sizeof(line) - lineLength - 1) charSize = 1;
        if (!atEnd) {
            memcpy(line + lineLength, text + index, (size_t)charSize);
            line[lineLength + charSize] = '\0';
        }
        if (atEnd || (lineLength > 0 && GameFont_MeasureText(line, fontSize).x > availableWidth)) {
            if (!atEnd && lineLength > 0) {
                line[lineLength] = '\0';
                GameFont_Draw(line, bounds.x, y, fontSize, color);
                y += lineHeight;
                lineLength = 0;
                continue;
            }
            if (lineLength > 0) GameFont_Draw(line, bounds.x, y, fontSize, color);
            break;
        }
        lineLength += charSize;
        index += charSize;
    }
}

/* 呼び出し側は最小の入力欄だけ指定する。実際のパス表示量から欄高と
   選択ボタンの位置を決めるので、長い相対パスが後続UIへ重ならない。 */
static RpgEditorFilePickerLayout ResolveInspectorFilePickerLayout(
    const RpgEditorFilePickerLayout *requested, const char *displayText)
{
    RpgEditorFilePickerLayout resolved = *requested;
    float innerWidth = fmaxf(1.0f, requested->valueBounds.width - 16.0f);
    int lines = GetInspectorWrappedLineCount(displayText, 14.0f, innerWidth);
    float textHeight = 12.0f + (float)lines * 17.0f;
    float selectGap = requested->selectBounds.y -
                      (requested->valueBounds.y + requested->valueBounds.height);
    if (selectGap < 2.0f) selectGap = 8.0f;
    resolved.valueBounds.height = fmaxf(requested->valueBounds.height, textHeight);
    resolved.selectBounds.y = resolved.valueBounds.y + resolved.valueBounds.height + selectGap;
    resolved.contentBottom = resolved.selectBounds.y + resolved.selectBounds.height;
    return resolved;
}

static RpgEditorFilePickerLayout GetImageInspectorFilePickerLayout(const RpgImageObject *object)
{
    RpgEditorFilePickerLayout requested = { 122.0f, { 716.0f, 140.0f, 188.0f, 26.0f },
                                             { 716.0f, 174.0f, 188.0f, 28.0f }, 0.0f };
    return ResolveInspectorFilePickerLayout(&requested,
                                            object != NULL ? object->path : "PNG未選択");
}

static float GetImageInspectorControlsTop(const RpgImageObject *object)
{
    return GetImageInspectorFilePickerLayout(object).contentBottom + 10.0f;
}

static RpgEditorFilePickerLayout GetKeyDoorFilePickerLayout(const RpgKeyDoor *door)
{
    RpgEditorFilePickerLayout requested = { 148.0f, { 716.0f, 198.0f, 188.0f, 22.0f },
                                             { 716.0f, 166.0f, 188.0f, 26.0f }, 0.0f };
    return ResolveInspectorFilePickerLayout(&requested,
                                            door != NULL ? door->keyPath : "鍵ファイル未選択");
}

static FileDialogRequest MakeSpriteFileDialogRequest(const char *title)
{
    return (FileDialogRequest){ TextFormat("%s../assets/Sprite", GetApplicationDirectory()),
                                FILE_DIALOG_FILTER_PNG, title, "assets/Sprite" };
}

static FileDialogRequest MakeFilesFileDialogRequest(const char *title)
{
    return (FileDialogRequest){ TextFormat("%s../assets/Files", GetApplicationDirectory()),
                                FILE_DIALOG_FILTER_ALL_FILES, title, "assets/Files" };
}

static RpgEditorFilePickerLayout DrawInspectorFilePicker(const FileDialogRequest *request,
                                                         const RpgEditorFilePickerLayout *requestedLayout,
                                                         const char *relativePath, const char *emptyText,
                                                         const char *selectText, bool isUnsaved, bool isEditing)
{
    const char *displayText = relativePath != NULL && relativePath[0] != '\0' ? relativePath : emptyText;
    RpgEditorFilePickerLayout layout = ResolveInspectorFilePickerLayout(requestedLayout, displayText);
    Color fieldColor = Fade(LIGHTGRAY, 0.65f);
    Color borderColor = isUnsaved ? MAROON : (isEditing ? BLUE : GRAY);
    const char *baseLabel = request != NULL && request->baseLabel != NULL ? request->baseLabel : "";
    DrawSettingsText(TextFormat("基底: %s", baseLabel), layout.valueBounds.x,
                     layout.baseY, 11.0f, DARKGRAY);
    DrawRectangleRec(layout.valueBounds, fieldColor);
    DrawRectangleLinesEx(layout.valueBounds, 1.0f, borderColor);
    DrawInspectorWrappedText(displayText,
                             (Rectangle){ layout.valueBounds.x + 8.0f, layout.valueBounds.y + 6.0f,
                                          layout.valueBounds.width - 16.0f, layout.valueBounds.height - 10.0f },
                             14.0f, IsInspectorTextContext() ? BLACK :
                             (relativePath != NULL && relativePath[0] != '\0' ? (isUnsaved ? MAROON : BLACK) : GRAY));
    DrawRectangleRec(layout.selectBounds, DARKBLUE);
    Vector2 labelSize = GameFont_MeasureText(selectText, 15.0f);
    DrawSettingsText(selectText, layout.selectBounds.x + (layout.selectBounds.width - labelSize.x) * 0.5f,
                     layout.selectBounds.y + 5.0f, 15.0f, RAYWHITE);
    return layout;
}

// FILE.pngとFolderの参照先を、同じインスペクター表示・テキスト入力方式で編集する。
static void DrawReferenceInspector(const RpgStage *stage, const RpgStage *savedStage, int referenceIndex,
                                   bool isPathEditing, int cursorIndex, int selectionAnchor, int selectionEnd,
                                   const char *folderName)
{
    if (stage == NULL || referenceIndex < 0 || referenceIndex >= stage->referenceObjects.count) return;
    const RpgReferenceObject *object = &stage->referenceObjects.entries[referenceIndex];
    int savedIndex = -1;
    for (int index = 0; index < savedStage->referenceObjects.count; index++)
        if (savedStage->referenceObjects.entries[index].id == object->id) { savedIndex = index; break; }
    const char *path = object->path;
    bool isFolder = object->objectKind == RPG_REFERENCE_OBJECT_FOLDER;
    const char *displayText = isFolder ? folderName :
                              (object->sourcePath[0] != '\0' ? object->sourcePath :
                               GetReferenceRelativeDisplayPath(path));
    RpgEditorFolderInspectorLayout folderLayout = GetFolderInspectorLayout();
    bool isUnsaved = savedIndex < 0 ||
                     memcmp(object, &savedStage->referenceObjects.entries[savedIndex], sizeof(*object)) != 0;
    FileDialogRequest fileRequest = MakeFilesFileDialogRequest("ファイルを選択");
    RpgEditorFilePickerLayout filePicker = { 142.0f, { 716.0f, 156.0f, 188.0f, 28.0f },
                                              { 716.0f, 192.0f, 188.0f, 28.0f } };
    DrawInspectorFrame(referenceInspectorBounds, isFolder ? "フォルダー設定" : "ファイル設定",
                       DARKBLUE, GetInspectorCloseButton(7));
    DrawSettingsText(isFolder ? "フォルダー名" : "ファイル", 716, 122, 16, isUnsaved ? MAROON : BLACK);
    if (!isFolder)
        filePicker = DrawInspectorFilePicker(&fileRequest, &filePicker, displayText, "パスを指定", "ファイルを選択",
                                             isUnsaved, isPathEditing);
    else {
        DrawRectangleRec(folderLayout.nameBounds, Fade(LIGHTGRAY, 0.65f));
        DrawRectangleLinesEx(folderLayout.nameBounds, 1.0f,
                             isUnsaved ? MAROON : (isPathEditing ? BLUE : GRAY));
    }
    float pathTextY = isFolder ? 150.0f : 162.0f;
    float pathCaretY = isFolder ? 147.0f : 159.0f;
    if (isPathEditing && selectionAnchor != selectionEnd) {
        int start = selectionAnchor < selectionEnd ? selectionAnchor : selectionEnd;
        int end = selectionAnchor < selectionEnd ? selectionEnd : selectionAnchor;
        char prefix[RPG_STAGE_REFERENCE_PATH_LENGTH];
        char selectedText[RPG_STAGE_REFERENCE_PATH_LENGTH];
        memcpy(prefix, displayText, (size_t)start); prefix[start] = '\0';
        memcpy(selectedText, displayText + start, (size_t)(end - start)); selectedText[end - start] = '\0';
        float startX = 724.0f + GameFont_MeasureText(prefix, 15.0f).x;
        DrawRectangle((int)startX, (int)pathTextY - 1,
                      (int)GameFont_MeasureText(selectedText, 15.0f).x + 1, 17,
                      Fade(SKYBLUE, 0.7f));
    }
    if (isFolder || isPathEditing)
        GameFont_Draw(displayText[0] != '\0' ? displayText : (isFolder ? "フォルダー" : "パスを指定"), 724, pathTextY, 15,
                      IsInspectorTextContext() ? BLACK :
                      (displayText[0] != '\0' ? (isUnsaved ? MAROON : BLACK) : GRAY));
    if (isPathEditing) {
        char prefix[RPG_STAGE_REFERENCE_PATH_LENGTH];
        memcpy(prefix, displayText, (size_t)cursorIndex); prefix[cursorIndex] = '\0';
        DrawTextCaret(724 + (int)GameFont_MeasureText(prefix, 15.0f).x, (int)pathCaretY, 22);
    }
    if (isFolder) {
        DrawRectangleRec(folderLayout.openBounds, DARKBLUE);
        DrawSettingsText("フォルダーを開く", folderLayout.openBounds.x + 26.0f,
                         folderLayout.openBounds.y + 6.0f, 14, RAYWHITE);
    }
    if (isFolder) {
        DrawRectangleRec(folderLayout.renameBounds, DARKGREEN);
        DrawSettingsText("フォルダー名を変更", folderLayout.renameBounds.x + 22.0f,
                         folderLayout.renameBounds.y + 6.0f, 14, RAYWHITE);
    }
    DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716,
                     isFolder ? folderLayout.statusY : filePicker.contentBottom + 10.0f, 16,
                     isUnsaved ? MAROON : BLACK);
}

/* PNG見た目専用の画像オブジェクトを、Folderとは別のInspectorで編集する。 */
static void DrawImageObjectInspector(const RpgStage *stage, const RpgStage *savedStage, int imageIndex)
{
    if (stage == NULL || imageIndex < 0 || imageIndex >= stage->imageObjects.count) return;
    const RpgImageObject *object = &stage->imageObjects.entries[imageIndex];
    int savedIndex = RpgImageObjects_FindById(&savedStage->imageObjects, object->id);
    bool isUnsaved = savedIndex < 0 || memcmp(object, &savedStage->imageObjects.entries[savedIndex],
                                               sizeof(*object)) != 0;
    FileDialogRequest imageRequest = MakeSpriteFileDialogRequest("画像PNGを選択");
    RpgEditorFilePickerLayout imagePicker = GetImageInspectorFilePickerLayout(object);
    DrawInspectorFrame(referenceInspectorBounds, "画像オブジェクト設定", DARKPURPLE,
                       GetInspectorCloseButton(RPG_EDITOR_IMAGE_INSPECTOR));
    imagePicker = DrawInspectorFilePicker(&imageRequest, &imagePicker, object->path, "PNG未選択", "PNGを選択",
                                           isUnsaved, false);
    float controlsY = imagePicker.contentBottom + 10.0f;
    DrawSettingsText("拡大率", 716, controlsY + 8.0f, 16, isUnsaved ? MAROON : BLACK);
    DrawRectangle(816, (int)controlsY, 32, 26, MAROON);
    DrawText("-", 827, (int)controlsY + 1, 20, RAYWHITE);
    DrawRectangle(854, (int)controlsY, 50, 26, DARKGREEN);
    DrawSettingsText(TextFormat("%.2fx", object->scale), 858, controlsY + 6.0f, 14, RAYWHITE);
    DrawSettingsText("描画レイヤー", 716, controlsY + 34.0f, 16, isUnsaved ? MAROON : BLACK);
    DrawRectangle(716, (int)controlsY + 56, 60, 28, object->layer == RPG_IMAGE_OBJECT_LAYER_BACK ? DARKPURPLE : GRAY);
    DrawSettingsText("背景", 727, controlsY + 62.0f, 14, RAYWHITE);
    DrawRectangle(780, (int)controlsY + 56, 60, 28,
                  object->layer == RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK ? DARKPURPLE : GRAY);
    DrawSettingsText("中間", 785, controlsY + 62.0f, 13, RAYWHITE);
    DrawRectangle(844, (int)controlsY + 56, 60, 28, object->layer == RPG_IMAGE_OBJECT_LAYER_FRONT ? DARKPURPLE : GRAY);
    DrawSettingsText("前景", 852, controlsY + 62.0f, 14, RAYWHITE);
    DrawSettingsText("見た目", 716, controlsY + 102.0f, 16, isUnsaved ? MAROON : BLACK);
    DrawRectangle(716, (int)controlsY + 124, 58, 26, object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_PNG ? DARKPURPLE : GRAY);
    DrawSettingsText("PNG", 730, controlsY + 129.0f, 14, RAYWHITE);
    DrawRectangle(778, (int)controlsY + 124, 62, 26, object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FOLDER ? DARKPURPLE : GRAY);
    DrawSettingsText("フォルダー", 780, controlsY + 129.0f, 11, RAYWHITE);
    DrawRectangle(844, (int)controlsY + 124, 60, 26, object->appearance == RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FILE ? DARKPURPLE : GRAY);
    DrawSettingsText("ファイル", 849, controlsY + 129.0f, 12, RAYWHITE);
    DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, controlsY + 164.0f, 16,
                     isUnsaved ? MAROON : BLACK);
}

static void DrawDoorInspector(const RpgStage *stage, const RpgStage *savedStage, int row, int column)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsDoorBlock(stage->blocks[row][column])) return;
    bool isOpen = RpgBlockInventory_IsDoorOpen(stage->blocks[row][column]);
    bool isUnsaved = stage->blocks[row][column] != savedStage->blocks[row][column];
    const RpgKeyDoor *keyDoor = RpgStage_GetKeyDoorAtCellConst(stage, row, column);
    DrawInspectorFrame(doorInspectorBounds, "ドア設定", BROWN, GetInspectorCloseButton(5));
    if (keyDoor != NULL) {
        const RpgKeyDoor *savedDoor = RpgStage_GetKeyDoorAtCellConst(savedStage, row, column);
        bool isKeyUnsaved = savedDoor == NULL || strcmp(keyDoor->keyPath, savedDoor->keyPath) != 0 ||
                            strcmp(keyDoor->failureText, savedDoor->failureText) != 0;
        FileDialogRequest keyRequest = MakeFilesFileDialogRequest("鍵ファイルを選択");
        RpgEditorFilePickerLayout keyPicker = GetKeyDoorFilePickerLayout(keyDoor);
        DrawSettingsText(isOpen ? "鍵ドア: 開放" : "鍵ドア: 施錠", 716, 122, 16,
                         (isUnsaved || isKeyUnsaved) ? MAROON : BLACK);
        keyPicker = DrawInspectorFilePicker(&keyRequest, &keyPicker, keyDoor->keyPath, "鍵ファイル未選択",
                                             "ファイルを選択", isUnsaved || isKeyUnsaved, false);
        float failureLabelY = keyPicker.contentBottom + 12.0f;
        Rectangle failureBounds = { 716.0f, failureLabelY + 18.0f, 188.0f, 30.0f };
        DrawSettingsText("失敗時の文章", 716, failureLabelY, 14, BLACK);
        DrawRectangleRec(failureBounds, Fade(LIGHTGRAY, 0.65f));
        DrawRectangleLinesEx(failureBounds, 1.0f, isKeyDoorFailureEditing ? BLUE : GRAY);
        GameFont_Draw(keyDoor->failureText, 722, failureBounds.y + 7.0f, 15,
                      IsInspectorTextContext() ? BLACK : (isKeyUnsaved ? MAROON : BLACK));
        if (isKeyDoorFailureEditing) {
            char prefix[RPG_KEY_DOOR_FAILURE_TEXT_LENGTH];
            memcpy(prefix, keyDoor->failureText, (size_t)keyDoorFailureCursorIndex);
            prefix[keyDoorFailureCursorIndex] = '\0';
            DrawTextCaret(722 + (int)GameFont_MeasureText(prefix, 15.0f).x, (int)failureBounds.y + 4, 20);
        }
        DrawSettingsText((isUnsaved || isKeyUnsaved) ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716,
                         failureBounds.y + failureBounds.height + 10.0f, 16,
                         (isUnsaved || isKeyUnsaved) ? MAROON : BLACK);
        return;
    }
    DrawSettingsText(isOpen ? "状態: 開放" : "状態: 閉鎖", 716, 122, 17, isUnsaved ? MAROON : BLACK);
    DrawRectangle(716, 144, 86, 28, isOpen ? DARKGREEN : GRAY);
    DrawSettingsText("開く", 744, 150, 16, RAYWHITE);
    DrawRectangle(818, 144, 86, 28, isOpen ? GRAY : MAROON);
    DrawSettingsText("閉じる", 830, 150, 16, RAYWHITE);
    DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 188, 16,
                     isUnsaved ? MAROON : BLACK);
}

static void DrawSignalShrinkInspector(int index)
{
    if (index < 0 || index >= signalBlocks.count) return;
    const RpgSignalBlock *block = &signalBlocks.entries[index];
    bool isUnsaved = index >= savedSignalBlocks.count || block->row != savedSignalBlocks.entries[index].row ||
                     block->column != savedSignalBlocks.entries[index].column ||
                     block->duration != savedSignalBlocks.entries[index].duration ||
                     block->startsExpanded != savedSignalBlocks.entries[index].startsExpanded;
    DrawInspectorFrame(doorInspectorBounds, "信号ブロック設定", PURPLE, GetInspectorCloseButton(5));
    DrawSettingsText("信号を受けるたび状態を切替", 716, 122, 14, isUnsaved ? MAROON : BLACK);
    DrawSettingsText(TextFormat("時間: %.1f秒", block->duration), 716, 150, 16, isUnsaved ? MAROON : BLACK);
    DrawRectangle(816, 144, 38, 24, MAROON);
    DrawText("-", 830, 145, 20, RAYWHITE);
    DrawRectangle(864, 144, 38, 24, DARKGREEN);
    DrawText("+", 877, 145, 20, RAYWHITE);
    DrawRectangle(716, 180, 188, 26, block->startsExpanded ? DARKBLUE : DARKPURPLE);
    DrawSettingsText(block->startsExpanded ? "初期：展開" : "初期：縮小", 752, 185, 16, RAYWHITE);
    DrawRectangle(716, 212, 188, 26, DARKBLUE);
    DrawSettingsText("信号をプレビュー", 754, 217, 16, RAYWHITE);
    DrawSettingsText("クリック：回転 / ドラッグ：移動", 716, 250, 13, BLACK);
    DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 270, 15,
                     isUnsaved ? MAROON : BLACK);
}

static void DrawAttachmentInspector(int attachmentIndex, bool isPathEditing)
{
    if (attachmentIndex < 0 || attachmentIndex >= attachments.count) return;
    const RpgAttachment *attachment = &attachments.entries[attachmentIndex];
    bool isUnsaved = attachmentIndex >= savedAttachments.count ||
                     memcmp(attachment, &savedAttachments.entries[attachmentIndex], sizeof(*attachment)) != 0;
    Rectangle bounds = { 700, 80, 220, 350 };
    bool isButton = attachment->type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON;
    bool isSaveFlag = attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG;
    bool isBlockSocket = attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET;
    DrawInspectorFrame(bounds, isSaveFlag ? "旗設定" :
                       (isButton ? "ボタン設定" :
                        (isBlockSocket ? "ブロック設置部品" : "射出装置設定")), SKYBLUE,
                       GetInspectorCloseButton(6));
    if (isSaveFlag) {
        DrawSettingsText(TextFormat("旗ID: %d", attachment->folderId), 716, 122, 16,
                         isUnsaved ? MAROON : BLACK);
        DrawSettingsText("この地点からエディタープレイ", 716, 150, 15, BLACK);
        DrawRectangle(716, 176, 188, 28, DARKGREEN);
        DrawRectangleLinesEx((Rectangle){ 716, 176, 188, 28 }, 1.0f, RAYWHITE);
        DrawSettingsText("この旗からプレイ", 744, 181, 16, RAYWHITE);
        DrawSettingsText("ジッパー：接続状態", 716, 220, 15, BLACK);
        DrawSettingsText("停止するとこのエリアへ戻る", 716, 246, 14, BLACK);
        DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 278, 16,
                         isUnsaved ? MAROON : BLACK);
        return;
    }
    if (isButton) {
        DrawSettingsText("対象：射出装置", 716, 122, 16, isUnsaved ? MAROON : BLACK);
        DrawSettingsText("ブロックの辺に接続", 716, 150, 16, BLACK);
        DrawRectangle(716, 176, 188, 26, DARKBLUE);
        DrawSettingsText("信号をプレビュー", 756, 181, 16, RAYWHITE);
        DrawSettingsText("一度だけ実行（プレビューのみ）", 716, 214, 13, BLACK);
        DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 240, 16,
                         isUnsaved ? MAROON : BLACK);
        return;
    }
    if (isBlockSocket) {
        DrawSettingsText("上面ブロック設置部品", 716, 122, 16, isUnsaved ? MAROON : BLACK);
        DrawSettingsText("可動ブロックを真上へ設置", 716, 150, 14, BLACK);
        DrawSettingsText("信号：ブロック設置（エリア内のみ）", 716, 178, 14, BLACK);
        DrawSettingsText(TextFormat("右側の光: +%.0f度", attachment->socketRightLightAngle), 716, 212, 15, BLACK);
        DrawRectangle(716, 232, 42, 24, MAROON);
        DrawRectangle(772, 232, 42, 24, DARKGREEN);
        DrawText("-", 731, 235, 20, RAYWHITE);
        DrawText("+", 789, 235, 20, RAYWHITE);
        DrawSettingsText(TextFormat("透明度: %.0f%%", attachment->socketLightOpacity * 100.0f), 716, 276, 15, BLACK);
        DrawRectangle(716, 296, 42, 24, MAROON);
        DrawText("-", 731, 299, 20, RAYWHITE);
        DrawRectangle(772, 296, 42, 24, DARKGREEN);
        DrawText("+", 789, 299, 20, RAYWHITE);
        DrawSettingsText("左端は対応する負角度を使用", 716, 334, 13, BLACK);
        DrawSettingsText("設置したブロックは固定されます", 716, 358, 14, BLACK);
        DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 388, 16,
                         isUnsaved ? MAROON : BLACK);
        return;
    }
    DrawSettingsText("実際の弾：フォルダー連動", 716, 122, 15, isUnsaved ? MAROON : BLACK);
    DrawSettingsText("起動：データボタン", 716, 150, 16, isUnsaved ? MAROON : BLACK);
    DrawSettingsText(TextFormat("1ファイルの大きさ: %.0fpx（8px刻み）", attachment->sizePerFile),
                     716, 182, 14, isUnsaved ? MAROON : BLACK);
    DrawRectangle(816, 194, 38, 24, MAROON);
    DrawText("-", 830, 195, 20, RAYWHITE);
    DrawRectangle(864, 194, 38, 24, DARKGREEN);
    DrawText("+", 877, 195, 20, RAYWHITE);
    DrawRectangle(716, 226, 94, 24, Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 226, 94, 24, isAttachmentSpeedEditing ? BLUE : (isUnsaved ? MAROON : GRAY));
    DrawText(isAttachmentSpeedEditing ? attachmentSpeedInput :
             TextFormat("%.1f / 100B", attachment->dataSpeed), 720, 230, 15, BLACK);
    if (isAttachmentSpeedEditing)
        DrawRectangle(722 + MeasureText(attachmentSpeedInput, 15), 230, 2, 16, PURPLE);
    DrawRectangle(816, 226, 38, 24, MAROON);
    DrawText("-", 830, 227, 20, RAYWHITE);
    DrawRectangle(864, 226, 38, 24, DARKGREEN);
    DrawText("+", 877, 227, 20, RAYWHITE);
    DrawRectangle(716, 258, 188, 26, DARKBLUE);
    DrawSettingsText("弾をプレビュー", 760, 263, 16, RAYWHITE);
    DrawSettingsText(TextFormat("プレビュー用ファイル数: %d", attachment->previewFileCount), 716, 304, 14, isUnsaved ? MAROON : BLACK);
    DrawRectangle(816, 294, 38, 24, MAROON);
    DrawText("-", 830, 295, 20, RAYWHITE);
    DrawRectangle(864, 294, 38, 24, DARKGREEN);
    DrawText("+", 877, 295, 20, RAYWHITE);
    DrawRectangle(716, 326, 94, 24, Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLines(716, 326, 94, 24, isAttachmentCapacityEditing ? BLUE : (isUnsaved ? MAROON : GRAY));
    DrawText(isAttachmentCapacityEditing ? attachmentCapacityInput :
             TextFormat("%llu B", attachment->previewTotalBytes), 720, 330, 15, BLACK);
    if (isAttachmentCapacityEditing)
        DrawRectangle(722 + MeasureText(attachmentCapacityInput, 15), 330, 2, 16, PURPLE);
    DrawRectangle(816, 326, 38, 24, MAROON);
    DrawText("-", 830, 327, 20, RAYWHITE);
    DrawRectangle(864, 326, 38, 24, DARKGREEN);
    DrawText("+", 877, 327, 20, RAYWHITE);
    (void)isPathEditing;
    DrawSettingsText("ブロックモード：軌道をドラッグ", 716, 368, 14, BLACK);
    DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 396, 15,
                     isUnsaved ? MAROON : BLACK);
}

static void DrawGlobalSettingsPanel(const RpgLayout *layout, const RpgCharacter *player,
                                    const RpgZipper *zipper, const EditorSaveSnapshot *savedSnapshot,
                                    RpgExplorerMode explorerMode, RpgBuildCellStorageMode storageMode)
{
    DrawInspectorFrame(globalSettingsPanelBounds, "全体設定", DARKBLUE,
                       GetInspectorCloseButton(RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR));
    Color delayColor = layout->electricCellDelay != savedSnapshot->layout.electricCellDelay ? MAROON : DARKGRAY;
    Color magnetSpeedColor = layout->magnetMetalSpeed != savedSnapshot->layout.magnetMetalSpeed ? MAROON : DARKGRAY;
    Color returnDurationColor = layout->zipperFolderReturnDuration !=
        savedSnapshot->layout.zipperFolderReturnDuration ? MAROON : DARKGRAY;
    Color returnDelayColor = layout->zipperFolderReturnAnimationDelay !=
        savedSnapshot->layout.zipperFolderReturnAnimationDelay ? MAROON : DARKGRAY;
    Color referenceFollowerScaleColor = layout->referenceFollowerScale !=
        savedSnapshot->layout.referenceFollowerScale ? MAROON : DARKGRAY;
    Color playerSpeedColor = player->moveSpeed != savedSnapshot->player.moveSpeed ? MAROON : DARKGRAY;
    Color playerScaleColor = player->scale != savedSnapshot->player.scale ? MAROON : DARKGRAY;
    Color zipperLaunchColor = zipper->launchSpeed != savedSnapshot->zipper.launchSpeed ? MAROON : DARKGRAY;
    Color zipperReturnColor = zipper->returnSpeed != savedSnapshot->zipper.returnSpeed ? MAROON : DARKGRAY;
    Color zipperFollowColor = zipper->followSpeed != savedSnapshot->zipper.followSpeed ? MAROON : DARKGRAY;
    for (int index = 0; index < RPG_GLOBAL_SETTINGS_TAB_COUNT; index++)
        DrawSettingsTabHeader(globalSettingsTabs, globalSettingsTabExpanded,
                              RPG_GLOBAL_SETTINGS_TAB_COUNT, index);
    if (globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_BUILD]) {
        float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                           RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_BUILD);
        Rectangle virtualBounds = { 716.0f, y + 22.0f, 88.0f, 28.0f };
        Rectangle windowsBounds = { 812.0f, y + 22.0f, 92.0f, 28.0f };
        Rectangle compactBounds = { 716.0f, y + 82.0f, 88.0f, 28.0f };
        Rectangle foldersBounds = { 812.0f, y + 82.0f, 92.0f, 28.0f };
        DrawSettingsText("エクスプローラー", 716, y + 6, 16, DARKGRAY);
        DrawRectangleRec(virtualBounds, explorerMode == RPG_EXPLORER_MODE_VIRTUAL ? DARKBLUE : GRAY);
        DrawRectangleRec(windowsBounds, explorerMode == RPG_EXPLORER_MODE_WINDOWS ? DARKBLUE : GRAY);
        DrawRectangleLinesEx(virtualBounds, 1.0f, RAYWHITE); DrawRectangleLinesEx(windowsBounds, 1.0f, RAYWHITE);
        DrawSettingsText("仮想", 726, y + 28, 15, RAYWHITE); DrawSettingsText("本物", 820, y + 28, 15, RAYWHITE);
        DrawSettingsText("ビルド", 716, y + 66, 16, DARKGRAY);
        DrawRectangleRec(compactBounds, storageMode == RPG_BUILD_CELL_STORAGE_COMPACT ? DARKBLUE : GRAY);
        DrawRectangleRec(foldersBounds, Fade(GRAY, 0.45f));
        DrawRectangleLinesEx(compactBounds, 1.0f, RAYWHITE); DrawRectangleLinesEx(foldersBounds, 1.0f, RAYWHITE);
        DrawSettingsText("簡易", 724, y + 88, 14, RAYWHITE); DrawSettingsText("全フォルダ", 816, y + 88, 14, Fade(RAYWHITE, 0.45f));
    }
    if (globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_RUNTIME]) {
        float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                           RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_RUNTIME);
        DrawSettingsText(TextFormat("導線遅延: %.2f秒", layout->electricCellDelay), 716, y + 6, 16, delayColor);
        DrawRectangle(716, (int)y + 20, 44, 26, MAROON); DrawText("-", 733, (int)y + 24, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 20, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 24, 20, RAYWHITE);
        DrawSettingsText(TextFormat("磁石速度: %.0f px/秒", layout->magnetMetalSpeed), 716, y + 56, 16, magnetSpeedColor);
        DrawRectangle(716, (int)y + 70, 44, 26, MAROON); DrawText("-", 733, (int)y + 74, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 70, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 74, 20, RAYWHITE);
    }
    if (globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_PLAYER]) {
        float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                           RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_PLAYER);
        DrawSettingsText(TextFormat("移動速度: %.0f", player->moveSpeed), 716, y + 6, 16, playerSpeedColor);
        DrawRectangle(716, (int)y + 20, 44, 26, MAROON); DrawText("-", 733, (int)y + 24, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 20, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 24, 20, RAYWHITE);
        DrawSettingsText(TextFormat("拡大率: %.1f", player->scale), 716, y + 56, 16, playerScaleColor);
        DrawRectangle(716, (int)y + 70, 44, 26, MAROON); DrawText("-", 733, (int)y + 74, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 70, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 74, 20, RAYWHITE);
        DrawSaveButton((Rectangle){ 716, y + 108, 90, 26 }, EDITOR_SAVE_NONE);
        DrawRevertButton((Rectangle){ 814, y + 108, 90, 26 });
    }
    if (globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_ZIPPER]) {
        float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                           RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_ZIPPER);
        DrawSettingsText(TextFormat("フォルダ返却: %.2f秒", layout->zipperFolderReturnDuration), 716, y + 6, 16, returnDurationColor);
        DrawRectangle(716, (int)y + 20, 44, 26, MAROON); DrawText("-", 733, (int)y + 24, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 20, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 24, 20, RAYWHITE);
        DrawSettingsText(TextFormat("返却アニメーション遅延: %.2f秒", layout->zipperFolderReturnAnimationDelay), 716, y + 62, 16, returnDelayColor);
        DrawRectangle(716, (int)y + 76, 44, 26, MAROON); DrawText("-", 733, (int)y + 80, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 76, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 80, 20, RAYWHITE);
        DrawSettingsText(TextFormat("ファイル追従倍率: %.0f%%", layout->referenceFollowerScale * 100.0f), 716, y + 118, 16, referenceFollowerScaleColor);
        DrawRectangle(716, (int)y + 132, 44, 26, MAROON); DrawText("-", 733, (int)y + 136, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 132, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 136, 20, RAYWHITE);
        DrawSettingsText(TextFormat("射出速度: %.0f", zipper->launchSpeed), 716, y + 174, 16, zipperLaunchColor);
        DrawRectangle(716, (int)y + 188, 44, 26, MAROON); DrawText("-", 733, (int)y + 192, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 188, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 192, 20, RAYWHITE);
        DrawSettingsText(TextFormat("帰還速度: %.0f", zipper->returnSpeed), 716, y + 230, 16, zipperReturnColor);
        DrawRectangle(716, (int)y + 244, 44, 26, MAROON); DrawText("-", 733, (int)y + 248, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 244, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 248, 20, RAYWHITE);
        DrawSettingsText(TextFormat("追従速度: %.0f", zipper->followSpeed), 716, y + 286, 16, zipperFollowColor);
        DrawRectangle(716, (int)y + 300, 44, 26, MAROON); DrawText("-", 733, (int)y + 304, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 300, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 304, 20, RAYWHITE);
        DrawRectangle(716, (int)y + 344, 188, 28, zipper->launchPreviewEnabled ? DARKGREEN : GRAY);
        DrawSettingsText(zipper->launchPreviewEnabled ? "プレビュー：オン" : "プレビュー：オフ", 754, y + 350, 16, RAYWHITE);
        DrawSaveButton((Rectangle){ 716, y + 378, 90, 26 }, EDITOR_SAVE_NONE);
        DrawRevertButton((Rectangle){ 814, y + 378, 90, 26 });
    }
}

// ステージ固有の管理と背景はここへ集約し、全体設定と保存先を混在させない。
static void DrawStageSettingsPanel(const RpgLayout *layout, const RpgStage3Event *stageEntryEvent,
                                   const EditorSaveSnapshot *savedSnapshot)
{
    DrawInspectorFrame(stageSettingsPanelBounds, "ステージ設定", DARKBLUE,
                       GetInspectorCloseButton(RPG_EDITOR_STAGE_SETTINGS_INSPECTOR));
    int stageIndex = RpgStageCatalog_FindIndex(&stageCatalogData, currentStageNumber);
    bool backgroundChanged = strcmp(layout->backgroundPath, savedSnapshot->layout.backgroundPath) != 0;
    bool groundBlockChanged = strcmp(layout->groundBlockPath, savedSnapshot->layout.groundBlockPath) != 0;
    bool visualChanged = backgroundChanged || groundBlockChanged ||
                          layout->backgroundBrightness != savedSnapshot->layout.backgroundBrightness ||
                          layout->blockBrightness != savedSnapshot->layout.blockBrightness ||
                          layout->groundHue != savedSnapshot->layout.groundHue ||
                          layout->groundSaturation != savedSnapshot->layout.groundSaturation ||
                          layout->groundLightness != savedSnapshot->layout.groundLightness;
    bool capacityChanged = layout->zipperMaxCapacityKB != savedSnapshot->layout.zipperMaxCapacityKB;
    for (int index = 0; index < RPG_STAGE_SETTINGS_TAB_COUNT; index++)
        DrawSettingsTabHeader(stageSettingsTabs, stageSettingsTabExpanded,
                              RPG_STAGE_SETTINGS_TAB_COUNT, index);
    if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_STAGE]) {
        float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                           RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_STAGE);
        DrawRectangle(716, (int)y + 6, 28, 26, GRAY); DrawRectangle(876, (int)y + 6, 28, 26, GRAY);
        DrawText("<", 725, (int)y + 9, 20, RAYWHITE); DrawText(">", 885, (int)y + 9, 20, RAYWHITE);
        DrawText(TextFormat("Stage%d", currentStageNumber), 760, (int)y + 11, 18, DARKBLUE);
        DrawText(TextFormat("%d / %d", stageIndex + 1, stageCatalogData.count), 780, (int)y + 33, 13, DARKGRAY);
        DrawRectangle(716, (int)y + 52, 188, 26, DARKGREEN); DrawRectangleLinesEx((Rectangle){ 716, y + 52, 188, 26 }, 1.0f, RAYWHITE);
        DrawSettingsText("ステージを追加", 752, y + 57, 16, RAYWHITE);
        DrawRectangle(716, (int)y + 86, 188, 26, MAROON); DrawRectangleLinesEx((Rectangle){ 716, y + 86, 188, 26 }, 1.0f, RAYWHITE);
        DrawSettingsText("ステージを削除", 752, y + 91, 16, RAYWHITE);
        DrawSettingsText("ジッパー容量", 716, y + 124, 15, capacityChanged ? MAROON : DARKGRAY);
        DrawRectangle(796, (int)y + 118, 70, 26, RAYWHITE); DrawRectangleLinesEx((Rectangle){ 796, y + 118, 70, 26 }, 1.0f, capacityChanged ? MAROON : DARKGRAY);
        DrawText(isZipperCapacityEditing ? zipperCapacityInput : TextFormat("%u", layout->zipperMaxCapacityKB), 802, (int)y + 124, 16, capacityChanged ? MAROON : DARKBLUE);
        if (isZipperCapacityEditing) DrawRectangle(802 + MeasureText(zipperCapacityInput, 16), (int)y + 123, 2, 17, DARKBLUE);
        DrawSettingsText("KB", 872, y + 124, 15, DARKGRAY);
    }
    if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_ENTRY_EVENT]) {
        float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                           RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_ENTRY_EVENT);
        RpgEditorInspectorActionRow entryRow = MakeInspectorActionRow(716.0f, y + 6.0f, 188.0f, 24.0f);
        DrawSettingsText("ビルド後に一度実行", entryRow.labelBounds.x, entryRow.labelBounds.y, 15, DARKGRAY);
        DrawRectangleRec(entryRow.actionBounds, stageEntryEvent->inspect.enabled ? DARKGREEN : GRAY);
        DrawSettingsText(stageEntryEvent->inspect.enabled ? "オン" : "オフ",
                         entryRow.actionBounds.x + 38.0f, entryRow.actionBounds.y + 4.0f, 15, RAYWHITE);
        DrawSettingsText(TextFormat("機能: %d個", stageEntryEvent->inspect.functionCount), 716,
                         entryRow.contentBottom + 8.0f, 14, DARKGRAY);
        Rectangle editBounds = { 716.0f, entryRow.contentBottom + 28.0f, 188.0f, 26.0f };
        DrawRectangleRec(editBounds, PURPLE);
        DrawSettingsText("機能を編集", 752, editBounds.y + 5.0f, 16, RAYWHITE);
    }
    if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_BACKGROUND]) {
        float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                           RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_BACKGROUND);
        FileDialogRequest backgroundRequest = MakeSpriteFileDialogRequest("背景PNGを選択");
        RpgEditorFilePickerLayout backgroundPicker = { y + 1.0f, { 716.0f, y + 12.0f, 188.0f, 18.0f },
                                                        { 716.0f, y + 32.0f, 188.0f, 28.0f } };
        backgroundPicker = DrawInspectorFilePicker(&backgroundRequest, &backgroundPicker, layout->backgroundPath,
                                                    "PNG未選択", "PNGを選択", backgroundChanged, false);
        float clearY = backgroundPicker.contentBottom + 8.0f;
        DrawRectangle(716, (int)clearY, 188, 28, GRAY); DrawSettingsText("背景を消去", 744, clearY + 6.0f, 16, RAYWHITE);
        float backgroundControlY = clearY + 34.0f;
        DrawSettingsText(TextFormat("背景の明るさ: %.0f%%", layout->backgroundBrightness * 100.0f), 716,
                         backgroundControlY + 8.0f, 15, visualChanged ? MAROON : DARKGRAY);
        DrawRectangle(816, (int)backgroundControlY, 38, 24, MAROON); DrawText("-", 830, (int)backgroundControlY + 1, 20, RAYWHITE);
        DrawRectangle(864, (int)backgroundControlY, 38, 24, DARKGREEN); DrawText("+", 877, (int)backgroundControlY + 1, 20, RAYWHITE);
        backgroundControlY += 40.0f;
        DrawSettingsText(TextFormat("ブロックの明るさ: %.0f%%", layout->blockBrightness * 100.0f), 716,
                         backgroundControlY + 8.0f, 15, visualChanged ? MAROON : DARKGRAY);
        DrawRectangle(816, (int)backgroundControlY, 38, 24, MAROON); DrawText("-", 830, (int)backgroundControlY + 1, 20, RAYWHITE);
        DrawRectangle(864, (int)backgroundControlY, 38, 24, DARKGREEN); DrawText("+", 877, (int)backgroundControlY + 1, 20, RAYWHITE);
    }
    if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_GROUND_BLOCK]) {
        float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                           RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_GROUND_BLOCK);
        FileDialogRequest groundRequest = MakeSpriteFileDialogRequest("地面PNGを選択");
        RpgEditorFilePickerLayout groundPicker = { y + 1.0f, { 716.0f, y + 12.0f, 188.0f, 18.0f },
                                                    { 716.0f, y + 32.0f, 188.0f, 28.0f } };
        groundPicker = DrawInspectorFilePicker(&groundRequest, &groundPicker, layout->groundBlockPath,
                                                "単色（PNGなし）", "地面PNGを選択", groundBlockChanged, false);
        float clearY = groundPicker.contentBottom + 8.0f;
        DrawRectangle(716, (int)clearY, 188, 28, GRAY);
        DrawSettingsText("地面PNGを消去", 744, clearY + 6.0f, 16, RAYWHITE);
    }
    if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_GROUND_TONE]) {
        float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                           RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_GROUND_TONE);
        const char *labels[] = { "色相", "彩度", "明度" };
        float values[] = { layout->groundHue, layout->groundSaturation, layout->groundLightness };
        for (int row = 0; row < 3; row++) {
            int rowY = (int)y + 8 + row * 38;
            DrawSettingsText(TextFormat("%s: %.0f%%", labels[row], values[row] * 100.0f), 716, rowY, 15, visualChanged ? MAROON : DARKGRAY);
            DrawRectangle(816, rowY - 8, 38, 24, MAROON); DrawText("-", 830, rowY - 7, 20, RAYWHITE);
            DrawRectangle(864, rowY - 8, 38, 24, DARKGREEN); DrawText("+", 877, rowY - 7, 20, RAYWHITE);
        }
    }
}

// 現在のエリアだけを対象にした管理パネル。削除操作はここに閉じ込める。
static void DrawAreaInspectorPanel(const RpgStage *stage, int mapIndex, const RpgAreaEntryEvents *events)
{
    DrawInspectorFrame(areaInspectorPanelBounds, "エリア設定", DARKBLUE,
                       GetInspectorCloseButton(RPG_EDITOR_AREA_SETTINGS_INSPECTOR));
    DrawSettingsText(TextFormat("エリア[%d][%d]", stage->mapGridX[mapIndex], stage->mapGridY[mapIndex]),
                     716, 120, 18, DARKBLUE);
    DrawSettingsText(TextFormat("エリア数: %d", RpgStage_GetMapCount(stage)), 716, 146, 15, DARKGRAY);
    DrawRectangle(716, 178, 188, 26, MAROON);
    DrawSettingsText("このエリアを削除", 748, 183, 16, RAYWHITE);
    const RpgStage3Event *entryEvent = &events->entries[mapIndex];
    RpgEditorInspectorActionRow entryRow = MakeInspectorActionRow(716.0f, 212.0f, 188.0f, 24.0f);
    DrawSettingsText("最初の入場イベント", entryRow.labelBounds.x, entryRow.labelBounds.y, 16, MAROON);
    DrawRectangleRec(entryRow.actionBounds, entryEvent->inspect.enabled ? DARKGREEN : GRAY);
    DrawSettingsText(entryEvent->inspect.enabled ? "オン" : "オフ",
                     entryRow.actionBounds.x + 38.0f, entryRow.actionBounds.y + 4.0f, 15, RAYWHITE);
    DrawSettingsText(TextFormat("機能: %d個", entryEvent->inspect.functionCount), 716,
                     entryRow.contentBottom + 8.0f, 14, DARKGRAY);
    Rectangle editBounds = { 716.0f, entryRow.contentBottom + 28.0f, 188.0f, 26.0f };
    DrawRectangleRec(editBounds, PURPLE);
    DrawSettingsText("機能を編集", 752, editBounds.y + 5.0f, 16, RAYWHITE);
}

static void DrawSettingsButtons(bool isGlobalSettingsOpen, bool isStageSettingsOpen,
                                bool isAreaInspectorOpen)
{
    DrawRectangleRec(globalSettingsButtonBounds, isGlobalSettingsOpen ? DARKBLUE : GRAY);
    DrawRectangleLinesEx(globalSettingsButtonBounds, 1.0f, RAYWHITE);
    DrawSettingsText("全体設定", globalSettingsButtonBounds.x + 8.0f, globalSettingsButtonBounds.y + 5.0f, 16.0f, BLACK);
    DrawRectangleRec(stageSettingsButtonBounds, isStageSettingsOpen ? DARKBLUE : GRAY);
    DrawRectangleLinesEx(stageSettingsButtonBounds, 1.0f, RAYWHITE);
    DrawSettingsText("ステージ", stageSettingsButtonBounds.x + 8.0f, stageSettingsButtonBounds.y + 5.0f, 16.0f, BLACK);
    DrawRectangleRec(areaInspectorButtonBounds, isAreaInspectorOpen ? DARKBLUE : GRAY);
    DrawRectangleLinesEx(areaInspectorButtonBounds, 1.0f, RAYWHITE);
    DrawSettingsText("エリア", areaInspectorButtonBounds.x + 10.0f, areaInspectorButtonBounds.y + 5.0f, 16.0f, BLACK);
}

static Rectangle GetBlockInventoryCell(int index)
{
    return (Rectangle){ 305.0f + index * 33.0f, 494.0f, 32.0f, 32.0f };
}

static int GetBlockInventoryFirstBlockType(const RpgBlockInventory *inventory)
{
    return inventory != NULL && inventory->count > 0 ? inventory->blockTypes[0] : 0;
}

static Rectangle GetBlockInventoryListItem(int index)
{
    /* 展開中は名称だけでなく、そのパレットのブロック配列も同じ行へ置く。 */
    return (Rectangle){ 24.0f, 486.0f - (index + 1) * 32.0f, 616.0f, 32.0f };
}

/* Only this compact grip can begin a palette-order drag.  The number, name,
   edit button and block icons remain their own normal controls. */
static Rectangle GetBlockInventoryReorderHandleBounds(int index)
{
    Rectangle row = GetBlockInventoryListItem(index);
    return (Rectangle){ row.x + 4.0f, row.y + 5.0f, 12.0f, row.height - 10.0f };
}

static int RemapBlockInventoryIndexAfterMove(int index, int sourceIndex, int finalIndex)
{
    if (index < 0) return index;
    if (index == sourceIndex) return finalIndex;
    if (sourceIndex < finalIndex && index > sourceIndex && index <= finalIndex) return index - 1;
    if (finalIndex < sourceIndex && index >= finalIndex && index < sourceIndex) return index + 1;
    return index;
}

static Rectangle GetBlockInventoryNameBounds(int index)
{
    Rectangle row = GetBlockInventoryListItem(index);
    return (Rectangle){ row.x + 46.0f, row.y + 3.0f, 116.0f, 26.0f };
}

static Rectangle GetBlockInventoryEditButtonBounds(int index)
{
    Rectangle row = GetBlockInventoryListItem(index);
    return (Rectangle){ row.x + 170.0f, row.y + 3.0f, 78.0f, 26.0f };
}

static Rectangle GetBlockInventoryPreviewCellBounds(int paletteIndex, int slotIndex)
{
    Rectangle row = GetBlockInventoryListItem(paletteIndex);
    return (Rectangle){ 305.0f + slotIndex * 30.0f, row.y + 2.0f, 28.0f, 28.0f };
}

static Rectangle GetBlockInventoryAddButtonBounds(void)
{
    Rectangle row = GetBlockInventoryListItem(RpgBlockInventory_Count());
    return (Rectangle){ row.x, row.y, 136.0f, row.height };
}

static Rectangle GetBlockInventoryDeleteButtonBounds(void)
{
    Rectangle row = GetBlockInventoryListItem(RpgBlockInventory_Count());
    return (Rectangle){ row.x + 142.0f, row.y, 136.0f, row.height };
}

static Color GetBlockInventoryBorderColor(RpgBlockInventoryBorderColor color)
{
    switch (color) {
    case RPG_BLOCK_INVENTORY_BORDER_RED: return RED;
    case RPG_BLOCK_INVENTORY_BORDER_BLUE: return DARKBLUE;
    case RPG_BLOCK_INVENTORY_BORDER_YELLOW: return GOLD;
    case RPG_BLOCK_INVENTORY_BORDER_WHITE:
    default: return RAYWHITE;
    }
}

/* パレットは本編のStage/gameや静的ステージ情報ではなく、エディターだけの
 * 操作UI設定として assets/Settings/Editor に保存する。 */
static bool GetEditorPalettePreferencesPath(char *path, size_t pathSize)
{
    return path != NULL && pathSize > 0 &&
           snprintf(path, pathSize, "%s../assets/Settings/Editor/rpg_block_inventory.cfg",
                    GetApplicationDirectory()) > 0;
}

static bool EnsureEditorPalettePreferencesDirectory(void)
{
    char directory[RPG_STAGE_PATH_LENGTH];
    if (snprintf(directory, sizeof(directory), "%s../assets/Settings/Editor",
                 GetApplicationDirectory()) <= 0) return false;
#ifdef _WIN32
    if (CreateDirectoryA(directory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    return false;
#else
    (void)directory;
    return true;
#endif
}

static bool SaveBlockInventoryPreferences(void)
{
    char path[RPG_STAGE_PATH_LENGTH];
    return EnsureEditorPalettePreferencesDirectory() && GetEditorPalettePreferencesPath(path, sizeof(path)) &&
           RpgBlockInventory_SavePreferences(path);
}

static bool LoadBlockInventoryPreferences(void)
{
    char editorPath[RPG_STAGE_PATH_LENGTH];
    char legacyPath[RPG_STAGE_PATH_LENGTH];
    if (!GetEditorPalettePreferencesPath(editorPath, sizeof(editorPath))) return false;
    if (FileExists(editorPath)) return RpgBlockInventory_LoadPreferences(editorPath);
    /* 前回実装で作られた共通Settings直下の設定だけは、初回起動時に
       エディター専用領域へ移して既存の編集内容を失わないようにする。 */
    if (snprintf(legacyPath, sizeof(legacyPath), "%s../assets/Settings/rpg_block_inventory.cfg",
                 GetApplicationDirectory()) > 0 && FileExists(legacyPath) &&
        RpgBlockInventory_LoadPreferences(legacyPath)) {
        return SaveBlockInventoryPreferences();
    }
    return false;
}

/* Palette previews are deliberately self-contained.  World effect symbols
   may span several cells (doors are three cells tall), which is correct in a
   stage but must never overflow a one-cell palette icon. */
static void DrawBlockInventoryEffectSymbol(Rectangle cell, int blockType)
{
    if (blockType == RPG_BLOCK_DOOR_CLOSED_TOP || blockType == RPG_BLOCK_KEY_DOOR_CLOSED_TOP) {
        Color outline = blockType == RPG_BLOCK_KEY_DOOR_CLOSED_TOP ? VIOLET : GOLD;
        DrawRectangleLinesEx((Rectangle){ cell.x + cell.width * 0.28f, cell.y + cell.height * 0.12f,
                                           cell.width * 0.44f, cell.height * 0.76f }, 2.0f, outline);
        if (blockType == RPG_BLOCK_KEY_DOOR_CLOSED_TOP) {
            DrawCircle((int)(cell.x + cell.width * 0.5f), (int)(cell.y + cell.height * 0.40f),
                       cell.width * 0.13f, SKYBLUE);
            DrawRectangle((int)(cell.x + cell.width * 0.44f), (int)(cell.y + cell.height * 0.40f),
                          (int)(cell.width * 0.12f), (int)(cell.height * 0.25f), SKYBLUE);
        }
        return;
    }
    BeginScissorMode((int)cell.x, (int)cell.y, (int)cell.width, (int)cell.height);
    RpgStage_DrawEffectSymbol(cell, blockType);
    EndScissorMode();
}

static void DrawBlockInventoryPropertyOrAttachmentIcon(Rectangle cell, int blockType)
{
    if (blockType == RPG_BLOCK_PROPERTY_ITEM)
        DrawPoly((Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f }, 5,
                 cell.width * 0.34f, -90.0f, GOLD);
    else if (blockType == RPG_BLOCK_PROPERTY_CONVEYOR) {
        DrawRectangleRec((Rectangle){ cell.x + cell.width * 0.12f, cell.y + cell.height * 0.25f,
                                      cell.width * 0.76f, cell.height * 0.50f }, (Color){ 54, 66, 74, 255 });
        DrawLineEx((Vector2){ cell.x + cell.width * 0.25f, cell.y + cell.height * 0.50f },
                   (Vector2){ cell.x + cell.width * 0.72f, cell.y + cell.height * 0.50f }, 2.5f, GOLD);
        DrawTriangle((Vector2){ cell.x + cell.width * 0.84f, cell.y + cell.height * 0.50f },
                     (Vector2){ cell.x + cell.width * 0.63f, cell.y + cell.height * 0.31f },
                     (Vector2){ cell.x + cell.width * 0.63f, cell.y + cell.height * 0.69f }, GOLD);
    } else if (blockType == RPG_BLOCK_PROPERTY_MAP_EVENT) {
        DrawCircleLines((int)(cell.x + cell.width * 0.5f), (int)(cell.y + cell.height * 0.5f),
                        cell.width * 0.31f, ORANGE);
        DrawCircle((int)(cell.x + cell.width * 0.5f), (int)(cell.y + cell.height * 0.5f),
                   cell.width * 0.09f, ORANGE);
    } else if (blockType == RPG_BLOCK_PROPERTY_RECEIVER) {
        DrawRectangleRec((Rectangle){ cell.x + cell.width * 0.19f, cell.y + cell.height * 0.41f,
                                      cell.width * 0.62f, cell.height * 0.22f }, DARKBROWN);
        DrawRectangleLinesEx((Rectangle){ cell.x + cell.width * 0.19f, cell.y + cell.height * 0.41f,
                                           cell.width * 0.62f, cell.height * 0.22f }, 1.5f, GOLD);
        DrawLineEx((Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.63f },
                   (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.88f }, 2.0f, SKYBLUE);
        DrawCircle((int)(cell.x + cell.width * 0.5f), (int)(cell.y + cell.height * 0.88f), 2.0f, GOLD);
    } else if (blockType == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
        DrawCircle((int)(cell.x + cell.width * 0.5f), (int)(cell.y + cell.height * 0.58f),
                   cell.width * 0.23f, DARKBLUE);
        DrawLineEx((Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.58f },
                   (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.16f }, 2.0f, GOLD);
        DrawCircleLines((int)(cell.x + cell.width * 0.5f), (int)(cell.y + cell.height * 0.16f),
                        cell.width * 0.19f, SKYBLUE);
    } else if (blockType == RPG_BLOCK_ATTACHMENT_DATA_BUTTON) {
        DrawRectangleRounded((Rectangle){ cell.x + cell.width * 0.12f, cell.y + cell.height * 0.59f,
                                          cell.width * 0.76f, cell.height * 0.22f }, 0.25f, 4, DARKGRAY);
        DrawRectangleRounded((Rectangle){ cell.x + cell.width * 0.28f, cell.y + cell.height * 0.34f,
                                          cell.width * 0.44f, cell.height * 0.28f }, 0.55f, 6, RED);
    } else if (blockType == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
        DrawLineEx((Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.86f },
                   (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.16f }, 2.0f, DARKBROWN);
        DrawTriangle((Vector2){ cell.x + cell.width * 0.53f, cell.y + cell.height * 0.22f },
                     (Vector2){ cell.x + cell.width * 0.88f, cell.y + cell.height * 0.36f },
                     (Vector2){ cell.x + cell.width * 0.53f, cell.y + cell.height * 0.50f }, RED);
    } else if (blockType == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
        DrawLineEx((Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.10f },
                   (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.66f }, 2.0f, SKYBLUE);
        DrawRectangleLinesEx((Rectangle){ cell.x + cell.width * 0.20f, cell.y + cell.height * 0.66f,
                                           cell.width * 0.60f, cell.height * 0.20f }, 1.5f, SKYBLUE);
    }
}

/* Blocks may be moved between palettes, so visual classification belongs to
   the block type rather than the palette that happened to contain it. */
static bool IsBlockInventoryOverlayType(int blockType)
{
    return blockType == RPG_BLOCK_PROPERTY_ITEM ||
           blockType == RPG_BLOCK_PROPERTY_RECEIVER ||
           RpgBlockInventory_IsMapEventProperty(blockType) ||
           RpgBlockInventory_IsConveyorProperty(blockType) ||
           RpgBlockInventory_IsAttachment(blockType);
}

static void DrawBlockInventoryPreviewCell(Rectangle cell, const RpgBlockInventory *inventory,
                                          int blockType, int selectedBlockType, Texture2D fileTexture)
{
    (void)inventory;
    if (IsBlockInventoryOverlayType(blockType))
        DrawRectangleRec(cell, Fade(DARKGRAY, 0.9f));
    else RpgStage_DrawBlockCell(cell, blockType, cell.width / RPG_STAGE_TILE_SIZE);
    if (blockType == RPG_BLOCK_REFERENCE_FILE && fileTexture.id != 0)
        DrawTexturePro(fileTexture, (Rectangle){ 0.0f, 0.0f, (float)fileTexture.width, (float)fileTexture.height },
                       (Rectangle){ cell.x + 3.0f, cell.y + 3.0f, cell.width - 6.0f, cell.height - 6.0f },
                       (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    else if (blockType == RPG_BLOCK_REFERENCE_FOLDER)
        RpgStage_DrawReferenceFolder(cell, WHITE);
    else if (blockType == RPG_BLOCK_IMAGE_OBJECT)
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "PNG", cell.x + 3.0f, cell.y + 9.0f,
                            GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 11.0f));
    if (IsBlockInventoryOverlayType(blockType))
        DrawBlockInventoryPropertyOrAttachmentIcon(cell, blockType);
    else DrawBlockInventoryEffectSymbol(cell, blockType);
    DrawRectangleLinesEx(cell, blockType == selectedBlockType ? 2.0f : 1.0f,
                         blockType == selectedBlockType ? GOLD : RAYWHITE);
}

static void DrawBlockInventory(int selectedInventory, int selectedBlockType, bool isListOpen,
                               const RpgEditorDrag *drag, int dragSourcePalette,
                               int dragSourceSlot, const RpgEditorDrag *paletteRowDrag,
                               int paletteRowDragSource, Texture2D fileTexture)
{
    const RpgBlockInventory *inventory = RpgBlockInventory_Get(selectedInventory);
    Color inventoryBorder = GetBlockInventoryBorderColor(inventory->borderColor);
    // インベントリ名とブロック枠を分け、名前の操作がブロック選択へ干渉しないようにする。
    DrawRectangle(24, 486, 260, 48, Fade(BLACK, 0.55f));
    DrawRectangleLines(24, 486, 260, 48, inventoryBorder);
    DrawText("<", 34, 501, 18, RAYWHITE);
    /* パレット名は他の操作UIと同じUIプリセットで描画する。 */
    DrawRectangle(54, 487, 200, 46, Fade(RAYWHITE, 0.88f));
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, TextFormat("%d: %s", selectedInventory + 1, inventory->name),
                        62.0f, 502.0f, GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 16.0f));
    DrawText(">", 254, 501, 18, RAYWHITE);
    DrawRectangle(300, 486, 340, 48, Fade(BLACK, 0.55f));
    DrawRectangleLines(300, 486, 340, 48, inventoryBorder);
    for (int index = 0; index < inventory->count; index++) {
        Rectangle cell = GetBlockInventoryCell(index);
        int blockType = inventory->blockTypes[index];
        if (IsBlockInventoryOverlayType(blockType))
            DrawRectangleRec(cell, Fade(DARKGRAY, 0.9f));
        else
            // パレットもステージと同じ描画を使い、縦穴・横穴の向きを見分けられるようにする。
            RpgStage_DrawBlockCell(cell, blockType, 1.0f);
        if (blockType == RPG_BLOCK_REFERENCE_FILE && fileTexture.id != 0)
            DrawTexturePro(fileTexture, (Rectangle){ 0.0f, 0.0f, (float)fileTexture.width, (float)fileTexture.height },
                           (Rectangle){ cell.x + 3.0f, cell.y + 3.0f, cell.width - 6.0f, cell.height - 6.0f },
                           (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        if (blockType == RPG_BLOCK_REFERENCE_FOLDER)
            RpgStage_DrawReferenceFolder(cell, WHITE);
        if (blockType == RPG_BLOCK_IMAGE_OBJECT) {
            /* 外部画像が未割当でも用途を判別できる、固定のPNGパレット表記。 */
            DrawText("PNG", (int)cell.x + 3, (int)cell.y + 10, 12, RAYWHITE);
        }
        if (blockType == RPG_BLOCK_PROPERTY_ITEM)
            DrawPoly((Vector2){ cell.x + 16.0f, cell.y + 16.0f }, 5, 11.0f, -90.0f, GOLD);
        if (blockType == RPG_BLOCK_PROPERTY_CONVEYOR) {
            DrawRectangleRec((Rectangle){ cell.x + 4.0f, cell.y + 8.0f, 24.0f, 16.0f },
                             (Color){ 54, 66, 74, 255 });
            DrawLineEx((Vector2){ cell.x + 8.0f, cell.y + 16.0f },
                       (Vector2){ cell.x + 23.0f, cell.y + 16.0f }, 3.0f, GOLD);
            DrawTriangle((Vector2){ cell.x + 26.0f, cell.y + 16.0f },
                         (Vector2){ cell.x + 19.0f, cell.y + 11.0f },
                         (Vector2){ cell.x + 19.0f, cell.y + 21.0f }, GOLD);
        }
        if (blockType == RPG_BLOCK_PROPERTY_MAP_EVENT) {
            DrawCircleLines((int)cell.x + 16, (int)cell.y + 16, 10.0f, ORANGE);
            DrawCircle((int)cell.x + 16, (int)cell.y + 16, 3.0f, ORANGE);
        }
        if (blockType == RPG_BLOCK_PROPERTY_RECEIVER) {
            DrawRectangle((int)cell.x + 6, (int)cell.y + 13, 20, 7, DARKBROWN);
            DrawRectangleLinesEx((Rectangle){ cell.x + 6.0f, cell.y + 13.0f, 20.0f, 7.0f }, 2.0f, GOLD);
            DrawRectangle((int)cell.x + 9, (int)cell.y + 15, 14, 3, Fade(BLACK, 0.72f));
            /* 受容体が導線の始点であることを、導線と同じ色で示す。 */
            DrawLineEx((Vector2){ cell.x + 16.0f, cell.y + 20.0f },
                       (Vector2){ cell.x + 16.0f, cell.y + 28.0f }, 2.5f, SKYBLUE);
            DrawCircle((int)cell.x + 16, (int)cell.y + 28, 2.5f, GOLD);
        }
        if (blockType == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
            DrawCircle((int)cell.x + 16, (int)cell.y + 18, 7.0f, DARKBLUE);
            DrawLine((int)cell.x + 16, (int)cell.y + 18, (int)cell.x + 16, (int)cell.y + 5, GOLD);
            DrawCircleLines((int)cell.x + 16, (int)cell.y + 5, 6.0f, SKYBLUE);
        }
        if (blockType == RPG_BLOCK_ATTACHMENT_DATA_BUTTON) {
            /* 押し部だけでなく、支持台まで含めたボタン全体を縮小表示する。 */
            DrawRectangleRounded((Rectangle){ cell.x + 4.0f, cell.y + 19.0f, 24.0f, 7.0f },
                                 0.25f, 4, DARKGRAY);
            DrawRectangleLinesEx((Rectangle){ cell.x + 4.0f, cell.y + 19.0f, 24.0f, 7.0f }, 1.0f, RAYWHITE);
            DrawRectangleRounded((Rectangle){ cell.x + 9.0f, cell.y + 12.0f, 14.0f, 9.0f },
                                 0.55f, 6, RED);
            DrawRectangleLinesEx((Rectangle){ cell.x + 9.0f, cell.y + 12.0f, 14.0f, 9.0f }, 1.0f, MAROON);
        }
        if (blockType == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
            DrawLineEx((Vector2){ cell.x + 16.0f, cell.y + 27.0f },
                       (Vector2){ cell.x + 16.0f, cell.y + 5.0f }, 2.5f, DARKBROWN);
            DrawCircle((int)cell.x + 16, (int)cell.y + 5, 3.0f, GOLD);
            // パレットでは選択対象を判別しやすくするため、未設置時だけ旗印を表示する。
            DrawTriangle((Vector2){ cell.x + 17.0f, cell.y + 7.0f },
                          (Vector2){ cell.x + 28.0f, cell.y + 11.0f },
                          (Vector2){ cell.x + 17.0f, cell.y + 15.0f }, RED);
            DrawTriangleLines((Vector2){ cell.x + 17.0f, cell.y + 7.0f },
                              (Vector2){ cell.x + 28.0f, cell.y + 11.0f },
                              (Vector2){ cell.x + 17.0f, cell.y + 15.0f }, RAYWHITE);
        }
        if (blockType == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
            DrawLineEx((Vector2){ cell.x + 8.0f, cell.y + 3.0f },
                       (Vector2){ cell.x + 8.0f, cell.y + 20.0f }, 1.0f, Fade(SKYBLUE, 0.35f));
            DrawLineEx((Vector2){ cell.x + 16.0f, cell.y + 3.0f },
                       (Vector2){ cell.x + 16.0f, cell.y + 20.0f }, 1.5f, Fade(SKYBLUE, 0.68f));
            DrawLineEx((Vector2){ cell.x + 24.0f, cell.y + 3.0f },
                       (Vector2){ cell.x + 24.0f, cell.y + 20.0f }, 1.0f, Fade(SKYBLUE, 0.35f));
            DrawRectangleRec((Rectangle){ cell.x + 3.0f, cell.y + 20.0f, 2.0f, 8.0f },
                             (Color){ 43, 166, 235, 255 });
            DrawRectangleRec((Rectangle){ cell.x + 27.0f, cell.y + 20.0f, 2.0f, 8.0f },
                             (Color){ 43, 166, 235, 255 });
            DrawLineEx((Vector2){ cell.x + 5.0f, cell.y + 27.5f },
                       (Vector2){ cell.x + 27.0f, cell.y + 27.5f }, 1.0f, SKYBLUE);
        }
        /* World door symbols span three tiles; palette icons must remain in
           their own cell even in the compact bar. */
        DrawBlockInventoryEffectSymbol(cell, blockType);
        DrawRectangleLinesEx(cell, blockType == selectedBlockType ? 3.0f : 1.0f,
                             blockType == selectedBlockType ? GOLD : RAYWHITE);
    }
    if (isListOpen) for (int index = 0; index < RpgBlockInventory_Count(); index++) {
        Rectangle row = GetBlockInventoryListItem(index);
        const RpgBlockInventory *listedInventory = RpgBlockInventory_Get(index);
        DrawRectangleRec(row, index == selectedInventory ? Fade(SKYBLUE, 0.82f) : Fade(LIGHTGRAY, 0.92f));
        DrawRectangleLinesEx(row, 2.0f, GetBlockInventoryBorderColor(listedInventory->borderColor));
        Rectangle reorderHandle = GetBlockInventoryReorderHandleBounds(index);
        DrawRectangleRounded(reorderHandle, 0.35f, 4, Fade(DARKGRAY, 0.70f));
        for (int dot = 0; dot < 3; dot++) {
            float dotY = reorderHandle.y + 4.0f + dot * 7.0f;
            DrawCircleV((Vector2){ reorderHandle.x + 4.0f, dotY }, 1.15f, RAYWHITE);
            DrawCircleV((Vector2){ reorderHandle.x + 8.0f, dotY }, 1.15f, RAYWHITE);
        }
        DrawSettingsText(TextFormat("%d", index + 1), row.x + 22.0f, row.y + 7.0f, 15.0f, BLACK);
        Rectangle nameBounds = GetBlockInventoryNameBounds(index);
        DrawRectangleRec(nameBounds, Fade(RAYWHITE, 0.78f));
        DrawRectangleLinesEx(nameBounds, 1.0f, GetBlockInventoryBorderColor(listedInventory->borderColor));
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, listedInventory->name, nameBounds.x + 4.0f, nameBounds.y + 5.0f,
                            GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 15.0f));
        Rectangle editBounds = GetBlockInventoryEditButtonBounds(index);
        DrawRectangleRec(editBounds, DARKBLUE);
        DrawRectangleLinesEx(editBounds, 1.0f, RAYWHITE);
        DrawSettingsText("編集", editBounds.x + 23.0f, editBounds.y + 5.0f, 14.0f, RAYWHITE);
        for (int blockIndex = 0; blockIndex < listedInventory->count; blockIndex++) {
            Rectangle previewCell = GetBlockInventoryPreviewCellBounds(index, blockIndex);
            DrawBlockInventoryPreviewCell(previewCell, listedInventory,
                                          listedInventory->blockTypes[blockIndex], selectedBlockType, fileTexture);
        }
    }
    if (isListOpen) {
        Rectangle addBounds = GetBlockInventoryAddButtonBounds();
        Rectangle deleteBounds = GetBlockInventoryDeleteButtonBounds();
        bool canAdd = RpgBlockInventory_Count() < RPG_BLOCK_INVENTORY_MAX_PALETTES;
        bool canDelete = RpgBlockInventory_Count() > 1;
        DrawRectangleRec(addBounds, canAdd ? DARKBLUE : GRAY);
        DrawRectangleLinesEx(addBounds, 1.0f, RAYWHITE);
        DrawSettingsText("+ パレット", addBounds.x + 20.0f, addBounds.y + 7.0f, 14.0f, RAYWHITE);
        DrawRectangleRec(deleteBounds, canDelete ? MAROON : GRAY);
        DrawRectangleLinesEx(deleteBounds, 1.0f, RAYWHITE);
        DrawSettingsText("選択を削除", deleteBounds.x + 20.0f, deleteBounds.y + 7.0f, 14.0f, RAYWHITE);
    }
    if (drag != NULL && drag->active && dragSourcePalette >= 0 && dragSourceSlot >= 0) {
        const RpgBlockInventory *source = RpgBlockInventory_Get(dragSourcePalette);
        if (dragSourceSlot < source->count) {
            Rectangle preview = RpgEditorDrag_GetPreviewBounds(drag, 32.0f, 32.0f);
            DrawBlockInventoryPreviewCell(preview, source, source->blockTypes[dragSourceSlot],
                                          selectedBlockType, fileTexture);
            /* Shared drags retain their source until release; the overlay makes
               this preview explicitly ghost-like rather than a second solid icon. */
            DrawRectangleRec(preview, Fade(BLACK, 0.42f));
            DrawRectangleLinesEx(preview, 2.0f, Fade(SKYBLUE, 0.85f));
        }
    }
    if (drag != NULL && drag->active && dragSourcePalette >= 0 && dragSourceSlot >= 0) {
        for (int paletteIndex = 0; paletteIndex < RpgBlockInventory_Count(); paletteIndex++) {
            Rectangle row = GetBlockInventoryListItem(paletteIndex);
            if (!CheckCollisionPointRec(drag->pointerPosition, row) || drag->pointerPosition.x < 305.0f) continue;
            const RpgBlockInventory *destination = RpgBlockInventory_Get(paletteIndex);
            int slot = Clamp((int)((drag->pointerPosition.x - 305.0f) / 30.0f), 0, destination->count);
            Rectangle target = GetBlockInventoryPreviewCellBounds(paletteIndex, slot);
            DrawRectangleLinesEx(target, 2.0f, SKYBLUE);
            break;
        }
    }
    if (paletteRowDrag != NULL && paletteRowDrag->active &&
        paletteRowDragSource >= 0 && paletteRowDragSource < RpgBlockInventory_Count()) {
        const RpgBlockInventory *source = RpgBlockInventory_Get(paletteRowDragSource);
        Rectangle row = GetBlockInventoryListItem(paletteRowDragSource);
        Rectangle preview = RpgEditorDrag_GetPreviewBounds(paletteRowDrag, row.width, row.height);
        DrawRectangleRec(preview, Fade(SKYBLUE, 0.58f));
        DrawRectangleLinesEx(preview, 2.0f, DARKBLUE);
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, TextFormat("%d: %s", paletteRowDragSource + 1, source->name),
                            preview.x + 24.0f, preview.y + 8.0f,
                            GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 15.0f));
    }
}

/* Block-mode context menu.  Its bounds are shared by drawing and input so a
   map click cannot accidentally pass through the menu. */
static Rectangle GetBlockContextDeleteBounds(Rectangle bounds)
{
    return (Rectangle){ bounds.x + 3.0f, bounds.y + 67.0f, bounds.width - 6.0f, 29.0f };
}
static Rectangle GetBlockContextDropBounds(Rectangle bounds)
{ return (Rectangle){ bounds.x + 3.0f, bounds.y + 3.0f, bounds.width - 6.0f, 29.0f }; }
static Rectangle GetBlockContextRestoreBounds(Rectangle bounds)
{ return (Rectangle){ bounds.x + 3.0f, bounds.y + 35.0f, bounds.width - 6.0f, 29.0f }; }

static void DrawBlockContextMenu(Rectangle bounds)
{
    Rectangle dropBounds = GetBlockContextDropBounds(bounds);
    Rectangle restoreBounds = GetBlockContextRestoreBounds(bounds);
    Rectangle deleteBounds = GetBlockContextDeleteBounds(bounds);
    Vector2 pointer = RpgViewport_GetMousePosition();
    DrawRectangleRec(bounds, (Color){ 36, 39, 45, 250 });
    DrawRectangleLinesEx(bounds, 1.0f, (Color){ 175, 180, 188, 255 });
    DrawRectangleRec(dropBounds, CheckCollisionPointRec(pointer, dropBounds) ? (Color){ 72, 75, 82, 255 } : (Color){ 54, 57, 63, 255 });
    DrawRectangleRec(restoreBounds, CheckCollisionPointRec(pointer, restoreBounds) ? (Color){ 72, 75, 82, 255 } : (Color){ 54, 57, 63, 255 });
    DrawRectangleRec(deleteBounds, CheckCollisionPointRec(pointer, deleteBounds) ? (Color){ 182, 42, 52, 255 } : (Color){ 72, 75, 82, 255 });
    DrawText("Drop", (int)dropBounds.x + 12, (int)dropBounds.y + 6, 16, RAYWHITE);
    DrawText("Restore", (int)restoreBounds.x + 12, (int)restoreBounds.y + 6, 16, RAYWHITE);
    DrawText("Delete", (int)deleteBounds.x + 12, (int)deleteBounds.y + 7, 16, RAYWHITE);
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

// 編集画面のマップは16x8マスを960x480のゲーム領域へ等比で拡大する。
// UIはこの変換の外で描画・判定し、マップ操作だけを逆変換する。
static Rectangle GetEditorMapContentBounds(void)
{
    EditorWorkspaceLayout workspace = GetEditorWorkspaceLayout();
    float worldWidth = (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
    float worldHeight = (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE);
    float scale = fminf(workspace.mapViewport.width / worldWidth,
                         workspace.mapViewport.height / worldHeight);
    float width = worldWidth * scale;
    float height = worldHeight * scale;
    return (Rectangle){ workspace.mapViewport.x + (workspace.mapViewport.width - width) * 0.5f,
                        workspace.mapViewport.y + (workspace.mapViewport.height - height) * 0.5f,
                        width, height };
}

static float GetEditorMapScale(void)
{
    return GetEditorMapContentBounds().width / (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
}

static Vector2 GetEditorMapPointer(Vector2 screenPosition)
{
    Rectangle mapBounds = GetEditorMapContentBounds();
    float scale = GetEditorMapScale();
    return (Vector2){ (screenPosition.x - mapBounds.x) / scale,
                      (screenPosition.y - mapBounds.y) / scale };
}

/* Areas are camera sectors of one connected editor stage. */
static Vector2 GetEditorMapWorldOrigin(const RpgStage *stage, int mapIndex)
{
    if (stage == NULL || !RpgStage_IsMapActive(stage, mapIndex)) return (Vector2){ 0.0f, 0.0f };
    return (Vector2){ stage->mapGridX[mapIndex] * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                      -stage->mapGridY[mapIndex] * RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE };
}

/* 全エリアをグリッド座標のまま選べる、エディター専用の全体マップモーダル。 */
static Rectangle GetGlobalMapModalBounds(void)
{
    float width = fminf(640.0f, (float)RpgViewport_GetWidth() - 72.0f);
    float height = fminf(440.0f, (float)RpgViewport_GetHeight() - 72.0f);
    return (Rectangle){ ((float)RpgViewport_GetWidth() - width) * 0.5f,
                        ((float)RpgViewport_GetHeight() - height) * 0.5f,
                        width, height };
}

typedef struct GlobalMapGridLayout {
    int minX, maxX;
    int minY, maxY;
    float cellSize;
    float gap;
    float originX;
    float originY;
} GlobalMapGridLayout;

static bool GetGlobalMapGridLayout(const RpgStage *stage, Rectangle modalBounds,
                                   GlobalMapGridLayout *layout)
{
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool hasArea = false;
    if (stage == NULL || layout == NULL) return false;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        if (!RpgStage_IsMapActive(stage, index)) continue;
        int gridX = stage->mapGridX[index];
        int gridY = stage->mapGridY[index];
        if (!hasArea) {
            minX = maxX = gridX;
            minY = maxY = gridY;
            hasArea = true;
        } else {
            minX = gridX < minX ? gridX : minX;
            maxX = gridX > maxX ? gridX : maxX;
            minY = gridY < minY ? gridY : minY;
            maxY = gridY > maxY ? gridY : maxY;
        }
    }
    if (!hasArea) return false;
    const float headerHeight = 56.0f;
    const float footerHeight = 36.0f;
    const float padding = 32.0f;
    const float gap = 10.0f;
    int gridColumns = maxX - minX + 1;
    int gridRows = maxY - minY + 1;
    float availableWidth = modalBounds.width - padding * 2.0f - gap * (gridColumns - 1);
    float availableHeight = modalBounds.height - headerHeight - footerHeight - padding -
                            gap * (gridRows - 1);
    float cellSize = fminf(92.0f, fminf(availableWidth / (float)gridColumns,
                                        availableHeight / (float)gridRows));
    layout->minX = minX;
    layout->maxX = maxX;
    layout->minY = minY;
    layout->maxY = maxY;
    layout->cellSize = fmaxf(30.0f, cellSize);
    layout->gap = gap;
    float mapWidth = layout->cellSize * gridColumns + gap * (gridColumns - 1);
    layout->originX = modalBounds.x + (modalBounds.width - mapWidth) * 0.5f;
    layout->originY = modalBounds.y + headerHeight + padding +
                      (availableHeight - (layout->cellSize * gridRows + gap * (gridRows - 1))) * 0.5f;
    return true;
}

static bool GetGlobalMapAreaBounds(const RpgStage *stage, Rectangle modalBounds,
                                   int mapIndex, Rectangle *bounds)
{
    if (!RpgStage_IsMapActive(stage, mapIndex) || bounds == NULL) return false;
    GlobalMapGridLayout layout;
    if (!GetGlobalMapGridLayout(stage, modalBounds, &layout)) return false;
    *bounds = (Rectangle){ layout.originX + (stage->mapGridX[mapIndex] - layout.minX) *
                           (layout.cellSize + layout.gap),
                           layout.originY + (layout.maxY - stage->mapGridY[mapIndex]) *
                           (layout.cellSize + layout.gap),
                           layout.cellSize, layout.cellSize };
    return true;
}

static bool GetGlobalMapGridAtPointer(const RpgStage *stage, Vector2 pointer, int *gridX, int *gridY)
{
    GlobalMapGridLayout layout;
    Rectangle modalBounds = GetGlobalMapModalBounds();
    if (!GetGlobalMapGridLayout(stage, modalBounds, &layout) || gridX == NULL || gridY == NULL) return false;
    const float step = layout.cellSize + layout.gap;
    int column = (int)floorf((pointer.x - layout.originX) / step);
    int row = (int)floorf((pointer.y - layout.originY) / step);
    if (column < 0 || column > layout.maxX - layout.minX || row < 0 || row > layout.maxY - layout.minY)
        return false;
    Rectangle cell = { layout.originX + column * step, layout.originY + row * step,
                       layout.cellSize, layout.cellSize };
    if (!CheckCollisionPointRec(pointer, cell)) return false;
    *gridX = layout.minX + column;
    *gridY = layout.maxY - row;
    return true;
}

static int GetGlobalMapAreaAtPointer(const RpgStage *stage, Vector2 pointer)
{
    Rectangle modalBounds = GetGlobalMapModalBounds();
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        Rectangle areaBounds;
        if (GetGlobalMapAreaBounds(stage, modalBounds, index, &areaBounds) &&
            CheckCollisionPointRec(pointer, areaBounds)) return index;
    }
    return -1;
}

static void DrawGlobalMapModal(const RpgStage *stage, int currentMapIndex)
{
    int screenWidth = RpgViewport_GetWidth();
    int screenHeight = RpgViewport_GetHeight();
    Vector2 pointer = RpgViewport_GetMousePosition();
    Rectangle modalBounds = GetGlobalMapModalBounds();
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.62f));
    DrawRectangleRounded(modalBounds, 0.06f, 8, (Color){ 32, 38, 52, 255 });
    DrawRectangleLinesEx(modalBounds, 2.0f, SKYBLUE);
    DrawText("World Map", (int)modalBounds.x + 24, (int)modalBounds.y + 16, 24, RAYWHITE);
    DrawText("Ctrl+M: Close", (int)modalBounds.x + (int)modalBounds.width - 146,
             (int)modalBounds.y + 22, 15, LIGHTGRAY);

    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        Rectangle areaBounds;
        if (!GetGlobalMapAreaBounds(stage, modalBounds, index, &areaBounds)) continue;
        bool hovered = CheckCollisionPointRec(pointer, areaBounds);
        bool current = index == currentMapIndex;
        Color fill = current ? (Color){ 43, 111, 184, 255 } : (Color){ 71, 80, 98, 255 };
        if (hovered) fill = current ? (Color){ 62, 138, 214, 255 } : (Color){ 95, 112, 139, 255 };
        DrawRectangleRounded(areaBounds, 0.12f, 6, fill);
        DrawRectangleLinesEx(areaBounds, current ? 3.0f : 1.0f, current ? YELLOW : RAYWHITE);
        DrawText(TextFormat("Area[%d][%d]", stage->mapGridX[index], stage->mapGridY[index]),
                 (int)areaBounds.x + 8, (int)areaBounds.y + 18, 16, RAYWHITE);
    }
    DrawText("Drag: move   Arrow: move   Delete/right click: delete   Ctrl+Arrow: insert", (int)modalBounds.x + 24,
             (int)(modalBounds.y + modalBounds.height) - 24, 15, LIGHTGRAY);
}

static void DrawEditor(const RpgCharacter *player, const RpgCharacter *npc, const RpgStage *stage,
                       const RpgLayout *layout, const RpgStage3Event *stage3Event,
                       const RpgAreaEntryEvents *areaEvents,
                       const RpgZipper *zipper,
                       Texture2D zipperTexture, Texture2D fileTexture, const RpgDataShots *dataShots,
                       bool isEditorPlaying,
                       const RpgDialogue *dialogue, int selected, int mapIndex, bool blockMode,
                       int dialogueScroll, int activeDialogueLine, int dialogueCursorIndex,
                       int selectionAnchor, int selectionEnd, int draggedDialogueLine,
                       bool isDialogueEditorOpen, int dialogueBlockHeight,
                       int dialogueFontSize, bool isSpeakerEditing, bool isStage3DialogueEditing, bool isAreaEntryDialogueEditing, int entryDialogueAreaIndex,
                       bool isInspectDialogueEditing, bool isMoveFunctionEditorOpen, bool isWaitFunctionEditorOpen,
                       bool isLayerChangeFunctionEditorOpen, bool isLayerChangeTargetPicking, bool isMoveTargetPicking,
                       bool isMoveEasingListOpen, bool isMovePreviewPlaying, float movePreviewElapsed, Vector2 movePreviewSpritePosition,
                       int inspectFunctionIndex, int functionListScroll, bool isExamineFunctionListOpen, bool isFunctionTypeListOpen,
                       bool isFunctionPreviewPlaying, int functionPreviewIndex, float functionPreviewElapsed,
                       int draggedInspectFunction, bool isInspectTitleEditing, int titleCursorIndex, int titleSelectionAnchor, int titleSelectionEnd, int speakerCursorIndex, int speakerSelectionAnchor, int speakerSelectionEnd,
                       const char *message, bool isExitConfirmationOpen, bool isExitDetailsOpen,
                       int detailScroll, const EditorSaveSnapshot *savedSnapshot, const RpgItems *items,
                       const RpgItems *savedItems,
                       int selectedItemIndex, bool isItemNameEditing, int itemNameCursorIndex, int itemNameSelectionAnchor,
                       int itemNameSelectionEnd, int selectedDoorRow, int selectedDoorColumn,
                       int selectedAttachmentIndex, bool isAttachmentPathEditing,
                       int selectedInventory, int selectedBlockType,
                       bool isBlockInventoryListOpen,
                       const RpgEditorDrag *paletteBlockDrag,
                       int paletteBlockDragSourcePalette, int paletteBlockDragSourceSlot,
                       const RpgEditorDrag *paletteRowDrag, int paletteRowDragSource,
                       int selectedPaletteInspectorIndex,
                       bool isPaletteNameEditing, int paletteNameCursorIndex,
                       int paletteNameSelectionAnchor, int paletteNameSelectionEnd,
                       bool isZipperPointerFeedbackSuppressed,
                        int selectedReferenceObjectIndex, bool isReferencePathEditing,
                       bool isReferencePointerFeedbackSuppressed,
                        int referencePathCursorIndex, int referencePathSelectionAnchor,
                        int referencePathSelectionEnd, const char *referenceFolderNameInput, RpgExplorerMode explorerMode,
                        bool isGlobalSettingsOpen, bool isStageSettingsOpen, bool isAreaInspectorOpen,
                        int playDialogueIndex, int playInspectTarget, int playInspectFunctionIndex,
                        int playInspectLineIndex, bool playZipperFollowsPlayer,
                       const RpgSceneState *scene, bool isGlobalMapOpen,
                       bool isBlockContextMenuOpen, Rectangle blockContextMenuBounds)
{
    (void)isStage3DialogueEditing;
    (void)isAreaEntryDialogueEditing;
    (void)entryDialogueAreaIndex;
    (void)isZipperPointerFeedbackSuppressed;
    EditorSaveState saveState = GetSaveState(message);
    const RpgInspect *savedInspect = GetSavedActiveInspect(savedSnapshot, stage3Event);
    RpgDialogue emptySavedDialogue = { 0 };
    const RpgDialogue *savedEditedDialogue = isInspectDialogueEditing ?
        (inspectFunctionIndex < savedInspect->functionCount ?
         &savedInspect->functions[inspectFunctionIndex].dialogue : &emptySavedDialogue) :
        &savedSnapshot->dialogue;
    RpgViewport_BeginFrame();
    RpgStage_SetGroundAppearance(layout->groundHue, layout->groundSaturation,
                                 layout->groundLightness);
    ClearBackground(BLACK);
    Rectangle mapBounds = GetEditorMapContentBounds();
    Vector2 selectedMapWorldOrigin = GetEditorMapWorldOrigin(stage, mapIndex);
    Camera2D mapCamera = { .offset = { mapBounds.x, mapBounds.y },
                           .target = selectedMapWorldOrigin,
                           .zoom = GetEditorMapScale() };
    BeginMode2D(mapCamera);
    // エディターの選択エリアもゲームと同じ16x8マスの大きさで背景を配置する。
    /* Render every area into one continuous world before drawing selected-area
       editing overlays.  Area selection now changes the camera sector only. */
    for (int drawMapIndex = 0; drawMapIndex < RPG_STAGE_MAP_COUNT; drawMapIndex++) {
        if (!RpgStage_IsMapActive(stage, drawMapIndex)) continue;
        Vector2 worldOrigin = GetEditorMapWorldOrigin(stage, drawMapIndex);
        rlPushMatrix();
        rlTranslatef(worldOrigin.x, worldOrigin.y, 0.0f);
        RpgStageBackground_Draw(&stageBackground,
                                (Rectangle){ 0.0f, 0.0f,
                                             (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                             (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                                layout->backgroundBrightness);
        RpgImageObjects_DrawLayer(&stage->imageObjects, drawMapIndex, RPG_STAGE_COLUMNS,
                                  RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_BACK);
        RpgStage_DrawMapWithAttachments(stage, &attachments, drawMapIndex,
                                        blockMode && drawMapIndex == mapIndex,
                                        layout->blockBrightness);
        RpgAttachments_DrawBlockSocketRecesses(&attachments, drawMapIndex, &stageBackground,
                                                (Rectangle){ 0.0f, 0.0f,
                                                             (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                                             (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                                                layout->backgroundBrightness);
        RpgStage_DrawMapReferenceObjects(stage, drawMapIndex, fileTexture);
        rlPopMatrix();
    }
    rlPushMatrix();
    rlTranslatef(selectedMapWorldOrigin.x, selectedMapWorldOrigin.y, 0.0f);
    /* Selected-area overlays remain in local tile coordinates. */
    RpgStageBackground_Draw(&stageBackground,
                            (Rectangle){ 0.0f, 0.0f,
                                         (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                         (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                            layout->backgroundBrightness);
    RpgImageObjects_DrawLayer(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_BACK);
    DrawImageObjectDragPreviewForLayer(stage, RPG_IMAGE_OBJECT_LAYER_BACK);
    RpgStage_DrawMapWithAttachments(stage, &attachments, mapIndex, blockMode,
                                    layout->blockBrightness);
    RpgAttachments_DrawBlockSocketRecesses(&attachments, mapIndex, &stageBackground,
                                            (Rectangle){ 0.0f, 0.0f,
                                                         (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                                         (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                                            layout->backgroundBrightness);
    RpgStage_DrawMapReferenceObjects(stage, mapIndex, fileTexture);
    if (isReferenceDragPreviewVisible) {
        Rectangle ghostBounds = { referenceDragPointer.x - RPG_STAGE_TILE_SIZE * 0.5f,
                                  referenceDragPointer.y - RPG_STAGE_TILE_SIZE * 0.5f,
                                  RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        RpgStage_DrawReferenceObject(fileTexture, ghostBounds, Fade(WHITE, 0.70f));
    }
    DrawEditorItems(items, mapIndex);
    DrawEditorMapEvents(mapIndex);
    RpgStage_DrawMapEffects(stage, mapIndex);
    RpgMagnets_DrawFields(stage, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    RpgSignalBlocks_DrawPreview(&signalBlocks, stage, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    if (isEffectBlockDragPreviewVisible)
        DrawEffectBlockDragGhost(stage, effectBlockDragPreviewRow, effectBlockDragPreviewColumn,
                                 effectBlockDragPointer);
    RpgWires_DrawMap(&wires, stage, mapIndex);
    RpgWires_DrawElectric(&wires, dataShots,
                          mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    RpgReceivers_DrawMap(&receivers, mapIndex);
    RpgAttachments_DrawSocketLightsMap(&attachments, stage, mapIndex);
    RpgAttachments_DrawMapExcept(&attachments, mapIndex, attachmentDragDrawSkipIndex);
    RpgDataShots_DrawMap(dataShots, mapIndex);
    RpgImageObjects_DrawLayer(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE,
                              RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
    DrawImageObjectDragPreviewForLayer(stage, RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
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
    if (isMoveFunctionEditorOpen && !isMoveTargetPicking) {
        const RpgInspectMove *move = &npcInspect.functions[inspectFunctionIndex].move;
        Vector2 endpoint = GetMoveEndpoint(move, player, npc, zipper, stage);
        int destinationMap = (int)(endpoint.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
        if (destinationMap == mapIndex) {
            int destinationX = (int)(endpoint.x - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
            int destinationY = (int)endpoint.y;
            Rectangle markerBounds = { destinationX - 10.0f, destinationY - 10.0f, 20.0f, 20.0f };
            DrawRectangleRec(markerBounds, Fade(ORANGE, 0.28f));
            DrawRectangleLinesEx(markerBounds, 2.0f, ORANGE);
            DrawLine(destinationX - 15, destinationY, destinationX + 15, destinationY, ORANGE);
            DrawLine(destinationX, destinationY - 15, destinationX, destinationY + 15, ORANGE);
            DrawText("Destination", destinationX - 38, destinationY - 28, 14, MAROON);
        }
        if (isMovePreviewPlaying && (int)(movePreviewSpritePosition.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) == mapIndex) {
            int spriteX = (int)(movePreviewSpritePosition.x - mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
            DrawCircleLines(spriteX, (int)movePreviewSpritePosition.y, 14.0f, PURPLE);
        }
    }
    /* Zipper はエリアへ配置しないため、編集キャンバスには描画しない。 */
    if (isMovePreviewPlaying) DrawMovePreviewSprite(zipperTexture, player, npc, zipper, stage,
                                                     &npcInspect.functions[inspectFunctionIndex].move,
                                                     movePreviewSpritePosition, movePreviewElapsed, mapIndex);
    if (IsCharacterInMap(npc, mapIndex)) {
        RpgCharacter localNpc = GetLocalCharacter(npc, mapIndex);
        RpgCharacter_Draw(&localNpc, "NPC");
        if (selected == 2) DrawCircleLines((int)localNpc.position.x, (int)localNpc.position.y, 32, PURPLE);
    }
    DrawCharacterDragPreview(zipperTexture, player, npc, zipper, mapIndex, EDITOR_MAP_OBJECT_HIT_NPC);
    if (IsCharacterInMap(player, mapIndex)) {
        RpgCharacter localPlayer = GetLocalCharacter(player, mapIndex);
        /* 関数列プレビューで指定したwalkは、接地状態に左右されず必ずwalkシートを描く。 */
        RpgCharacter_DrawPlayer(&localPlayer,
                                isFunctionPreviewPlaying && localPlayer.isMoving ?
                                    RPG_CHARACTER_ANIMATION_WALK : RPG_CHARACTER_ANIMATION_AUTOMATIC);
        if (selected == 1) DrawCircleLines((int)localPlayer.position.x, (int)localPlayer.position.y, 32, BLUE);
    }
    DrawCharacterDragPreview(zipperTexture, player, npc, zipper, mapIndex, EDITOR_MAP_OBJECT_HIT_PLAYER);
    RpgImageObjects_DrawLayer(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_FRONT);
    DrawImageObjectDragPreviewForLayer(stage, RPG_IMAGE_OBJECT_LAYER_FRONT);
    // FILE.png もZipperと同じWindows風のホバー・選択表示を使う。
    for (int index = 0; index < stage->referenceObjects.count; index++) {
        const RpgReferenceObject *object = &stage->referenceObjects.entries[index];
        int objectRow = -1, objectColumn = -1;
        if (!RpgStage_GetWorldCellAtPosition(stage, object->position, &objectRow, &objectColumn) ||
            objectColumn < mapIndex * RPG_STAGE_COLUMNS ||
            objectColumn >= (mapIndex + 1) * RPG_STAGE_COLUMNS) continue;
        Vector2 origin = GetEditorMapWorldOrigin(stage, mapIndex);
        float size = RPG_STAGE_TILE_SIZE * (object->drawScale > 0.0f ? object->drawScale : 1.0f);
        Rectangle bounds = { object->position.x - origin.x - size * 0.5f,
                             object->position.y - origin.y - size * 0.5f, size, size };
        Rectangle worldBounds = { object->position.x - size * 0.5f,
                                  object->position.y - size * 0.5f, size, size };
        Vector2 pointer = GetEditorReferenceWorldPosition(stage, mapIndex,
                                                           GetEditorMapPointer(RpgViewport_GetMousePosition()));
        bool hovered = CheckCollisionPointRec(pointer, worldBounds) &&
                       !isReferencePointerFeedbackSuppressed;
        bool selectedReference = selected == 7 && selectedReferenceObjectIndex == index &&
                                 !isReferencePointerFeedbackSuppressed;
        if (hovered || selectedReference)
            RpgZipper_DrawPointerFeedback(bounds, hovered, selectedReference);
    }
    rlPopMatrix();
    EndMode2D();
    {
        int playerMapIndex = RpgStage_FindNearestActiveMap(stage,
            (int)(player->position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)));
        if (playerMapIndex >= 0) {
            Rectangle playerAreaLabel = { 10.0f, 10.0f, 190.0f, 26.0f };
            DrawRectangleRec(playerAreaLabel, Fade(BLACK, 0.72f));
            DrawRectangleLinesEx(playerAreaLabel, 1.0f, SKYBLUE);
            DrawText(TextFormat("Player Area[%d][%d]", stage->mapGridX[playerMapIndex],
                                stage->mapGridY[playerMapIndex]), 16, 16, 14, RAYWHITE);
        }
    }
    if (!isEditorGameFullscreen) {
    EditorWorkspaceLayout workspace = GetEditorWorkspaceLayout();
    // マップと編集領域を不透明な境界で分け、インスペクターの背後へワールドを描画しない。
    DrawRectangleRec(workspace.sidePanel, Fade((Color){ 24, 28, 36, 255 }, 0.98f));
    DrawLine((int)workspace.mapViewport.width, 0, (int)workspace.mapViewport.width,
             (int)workspace.mapViewport.height, LIGHTGRAY);
    if (isFunctionPreviewPlaying) {
        /* プレビュー中であることを地図上にだけ表示し、編集用インスペクターは描画しない。 */
        DrawRectangle(14, 14, 246, 36, Fade(BLACK, 0.72f));
        DrawRectangleLinesEx((Rectangle){ 14, 14, 246, 36 }, 1.0f, ORANGE);
        if (functionPreviewIndex >= 0 && functionPreviewIndex < npcInspect.functionCount) {
            const RpgInspectFunction *previewFunction = &npcInspect.functions[functionPreviewIndex];
            const char *type = previewFunction->type == RPG_INSPECT_MOVE ? "Move" :
                               previewFunction->type == RPG_INSPECT_WAIT ? "Wait" :
                               previewFunction->type == RPG_INSPECT_LAYER_CHANGE ? "Draw layer" : "Dialogue";
            DrawText(TextFormat("Preview %d/%d: %s", functionPreviewIndex + 1,
                                npcInspect.functionCount, type), 26, 24, 17, RAYWHITE);
            if (previewFunction->type == RPG_INSPECT_DIALOGUE && previewFunction->dialogue.lineCount > 0) {
                int line = Clamp((int)(functionPreviewElapsed / 0.8f), 0,
                                 previewFunction->dialogue.lineCount - 1);
                DrawRectangle(148, 348, 664, 132, Fade(RAYWHITE, 0.95f));
                DrawRectangleLinesEx((Rectangle){ 148, 348, 664, 132 }, 2.0f, DARKBLUE);
                DrawRectangle(172, 330, 168, 38, DARKBLUE);
                GameFont_Draw(previewFunction->dialogue.speakers[line], 188, 340, 21, RAYWHITE);
                GameFont_Draw(previewFunction->dialogue.lines[line], 176, 390, 24, DARKBLUE);
            }
        }
    }
    // ステージは20×10マスをすべて表示し、操作帯はマップ外の下部余白にだけ描画する。
    DrawRectangleRec(editorWorkspace.controlBar, Fade(BLACK, 0.76f));
    DrawLine(0, (int)editorWorkspace.controlBar.y, RPG_EDITOR_WIDTH,
             (int)editorWorkspace.controlBar.y, LIGHTGRAY);
    // ブロック編集中はキャンバス全面を導線・軌道の操作領域として使えるようにする。
    if (!blockMode && !isEditorPlaying)
        DrawSettingsButtons(isGlobalSettingsOpen, isStageSettingsOpen, isAreaInspectorOpen);
    if (!isEditorPlaying) {
        DrawRectangleRec(editorPlayToggleBounds, DARKGREEN);
        DrawRectangleLinesEx(editorPlayToggleBounds, 1.0f, RAYWHITE);
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "Play",
                            editorPlayToggleBounds.x + 18.0f, editorPlayToggleBounds.y + 5.0f,
                            GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 16.0f));
    }
    DrawText(TextFormat("Area (%d, %d)", stage->mapGridX[mapIndex], stage->mapGridY[mapIndex]),
             562, 491, 16, MAROON);
    if (isEditorPlaying)
        DrawText("PLAY: A/D move   W jump   F2 stop", 16, 518, 14, RAYWHITE);
    else if (!blockMode)
        DrawText("B: Block mode   S: Save all   Esc: Deselect", 16, 518, 14, RAYWHITE);
    /* Shiftを押している間は、クリックで開いた一覧状態とは独立して
       パレット一覧を一時展開する。離すと通常のコンパクト表示へ戻る。 */
    bool isPaletteListTemporarilyExpanded = blockMode &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    bool isPaletteInspectorOpen = selected == RPG_EDITOR_PALETTE_INSPECTOR &&
                                  selectedPaletteInspectorIndex >= 0 &&
                                  selectedPaletteInspectorIndex < RpgBlockInventory_Count();
    if (blockMode && !isEditorPlaying)
        DrawBlockInventory(selectedInventory, selectedBlockType,
                           isBlockInventoryListOpen || isPaletteListTemporarilyExpanded ||
                           isPaletteInspectorOpen, paletteBlockDrag,
                           paletteBlockDragSourcePalette, paletteBlockDragSourceSlot,
                           paletteRowDrag, paletteRowDragSource,
                           fileTexture);
    bool hasUnsavedChanges = HasAnyUnsavedChanges(savedSnapshot, player, npc, layout, stage, dialogue, stage3Event,
                                                   items, savedItems) || RpgStageCatalog_IsDirty(&stageCatalogData);
    if (!isEditorPlaying && !blockMode) {
        DrawRectangleRec(revertSavedBounds, hasUnsavedChanges ? MAROON : GRAY);
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "Revert saved",
                            revertSavedBounds.x + 8.0f, revertSavedBounds.y + 5.0f,
                            GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 16.0f));
        if (AreItemsDifferent(items, savedItems)) DrawText("Items: unsaved", 720, 518, 14, MAROON);
        else if (AreWiresDifferent(&wires, &savedWires)) DrawText("Wires: unsaved", 720, 518, 14, MAROON);
        else if (AreReceiversDifferent(&receivers, &savedReceivers)) DrawText("Receivers: unsaved", 720, 518, 14, MAROON);
        else if (AreAttachmentsDifferent(&attachments, &savedAttachments)) DrawText("Attachments: unsaved", 720, 518, 14, MAROON);
    }
    DrawText(message, 580, 518, 14, saveState == EDITOR_SAVE_SUCCEEDED ? DARKGREEN : MAROON);
    bool isModalOpen = isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen ||
                       isMoveFunctionEditorOpen || isWaitFunctionEditorOpen || isLayerChangeFunctionEditorOpen;
    if (!isEditorPlaying && (!blockMode || selected == RPG_EDITOR_CONVEYOR_INSPECTOR ||
                             selected == RPG_EDITOR_PALETTE_INSPECTOR) && !isModalOpen && selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT &&
        !(selected == 6 && isAttachmentPathDragVisualActive)) {
        Rectangle inspectorScreenBounds = GetInspectorScreenBounds(selected);
        inspectorDrawingSelected = selected;
        Camera2D inspectorCamera = { .offset = inspectorOffsets[selected],
                                     .target = { 0.0f, inspectorScrollOffsets[selected] }, .zoom = 1.0f };
        BeginMode2D(inspectorCamera);
        BeginScissorMode((int)inspectorScreenBounds.x, (int)inspectorScreenBounds.y,
                         (int)inspectorScreenBounds.width, (int)inspectorScreenBounds.height);
    if (selected == 1) {
        DrawInspectorFrame(playerInspectorBounds, "プレイヤー設定", DARKBLUE, GetInspectorCloseButton(1));
        DrawSettingsText(TextFormat("移動速度: %.0f", player->moveSpeed), 716, 120, 17,
                         player->moveSpeed != savedSnapshot->player.moveSpeed ? MAROON : BLACK);
        DrawText("[-] 20", 716, 144, 16, MAROON);
        DrawText("[+] 20", 820, 144, 16, DARKGREEN);
        DrawSettingsText(TextFormat("拡大率: %.1f", player->scale), 716, 166, 16,
                         player->scale != savedSnapshot->player.scale ? MAROON : BLACK);
        DrawText("[-]", 812, 166, 16, MAROON);
        DrawText("[+]", 864, 166, 16, DARKGREEN);
        DrawSaveButton((Rectangle){ 716, 204, 90, 26 }, saveState);
        DrawRevertButton((Rectangle){ 814, 204, 90, 26 });
    } else if (selected == 2) {
        DrawNpcSummaryInspector(dialogue, npc, &savedSnapshot->npc, saveState);
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
        DrawReferenceInspector(stage, &savedSnapshot->stage, selectedReferenceObjectIndex,
                               isReferencePathEditing, referencePathCursorIndex,
                               referencePathSelectionAnchor, referencePathSelectionEnd,
                               referenceFolderNameInput);
    } else if (selected == RPG_EDITOR_IMAGE_INSPECTOR) {
        DrawImageObjectInspector(stage, &savedSnapshot->stage, selectedImageObjectIndex);
    } else if (selected == RPG_EDITOR_CONVEYOR_INSPECTOR) {
        DrawConveyorInspector(selectedConveyorIndex, conveyorPreviewRemaining);
    } else if (selected == RPG_EDITOR_PALETTE_INSPECTOR &&
               selectedPaletteInspectorIndex >= 0 &&
               selectedPaletteInspectorIndex < RpgBlockInventory_Count()) {
        DrawPaletteInspector(selectedPaletteInspectorIndex,
                             isPaletteNameEditing,
                             paletteNameCursorIndex, paletteNameSelectionAnchor,
                             paletteNameSelectionEnd);
    } else if (selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR && isGlobalSettingsOpen) {
        DrawGlobalSettingsPanel(layout, player, zipper, savedSnapshot, explorerMode, RpgBuildCellStorage_GetMode());
    } else if (selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR && isStageSettingsOpen) {
        DrawStageSettingsPanel(layout, stage3Event, savedSnapshot);
    } else if (selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR && isAreaInspectorOpen) {
        DrawAreaInspectorPanel(stage, mapIndex, areaEvents);
    }
        EndScissorMode();
        EndMode2D();
        if (GetInspectorScrollMaximum(selected) > 0.0f) {
            float trackHeight = inspectorScreenBounds.height - 46.0f;
            float thumbHeight = fmaxf(24.0f, trackHeight * (inspectorScreenBounds.height - 38.0f) /
                                              GetInspectorContentHeight(selected));
            float maxScroll = GetInspectorScrollMaximum(selected);
            float thumbY = inspectorScreenBounds.y + 38.0f +
                           (trackHeight - thumbHeight) * inspectorScrollOffsets[selected] / maxScroll;
            DrawRectangle((int)inspectorScreenBounds.x + (int)inspectorScreenBounds.width - 7,
                          (int)thumbY, 4, (int)thumbHeight, Fade(DARKGRAY, 0.65f));
        }
        DrawTriangle((Vector2){ inspectorScreenBounds.x + inspectorScreenBounds.width - 3.0f,
                                inspectorScreenBounds.y + inspectorScreenBounds.height - 14.0f },
                     (Vector2){ inspectorScreenBounds.x + inspectorScreenBounds.width - 3.0f,
                                inspectorScreenBounds.y + inspectorScreenBounds.height - 3.0f },
                     (Vector2){ inspectorScreenBounds.x + inspectorScreenBounds.width - 14.0f,
                                inspectorScreenBounds.y + inspectorScreenBounds.height - 3.0f }, DARKBLUE);
        inspectorDrawingSelected = 0;
    }
    if (isMoveFunctionEditorOpen && !isMoveTargetPicking) {
        DrawMoveFunctionEditor(&npcInspect.functions[inspectFunctionIndex].move, isMovePreviewPlaying, isMoveEasingListOpen,
                               saveState, inspectFunctionIndex >= savedInspect->functionCount ||
                               IsInspectFunctionDifferent(&npcInspect.functions[inspectFunctionIndex],
                                                          &savedInspect->functions[inspectFunctionIndex]));
    } else if (isWaitFunctionEditorOpen) {
        DrawWaitFunctionEditor(&npcInspect.functions[inspectFunctionIndex].wait, saveState,
                               inspectFunctionIndex >= savedInspect->functionCount ||
                               IsInspectFunctionDifferent(&npcInspect.functions[inspectFunctionIndex],
                                                          &savedInspect->functions[inspectFunctionIndex]));
    } else if (isLayerChangeFunctionEditorOpen && !isLayerChangeTargetPicking) {
        DrawLayerChangeFunctionEditor(&npcInspect.functions[inspectFunctionIndex].layerChange, stage,
                                      false, saveState, inspectFunctionIndex >= savedInspect->functionCount ||
                                      IsInspectFunctionDifferent(&npcInspect.functions[inspectFunctionIndex],
                                                                 &savedInspect->functions[inspectFunctionIndex]));
    } else if (isFunctionTypeListOpen) {
        DrawFunctionTypeList();
    } else if (isExamineFunctionListOpen) {
        DrawExamineFunctionList(&npcInspect, inspectFunctionIndex, functionListScroll, draggedInspectFunction, isInspectTitleEditing,
                                titleCursorIndex, titleSelectionAnchor, titleSelectionEnd, savedInspect,
                                isFunctionPreviewPlaying);
    } else if (isDialogueEditorOpen) {
        DrawRectangle(0, 0, RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, Fade(BLACK, 0.62f));
        DrawNpcInspector(isInspectDialogueEditing ? &npcInspect.functions[inspectFunctionIndex].dialogue : dialogue, dialogueScroll, activeDialogueLine, dialogueCursorIndex,
                         selectionAnchor, selectionEnd, draggedDialogueLine,
                         dialogueBlockHeight, dialogueFontSize, isSpeakerEditing,
                         speakerCursorIndex, speakerSelectionAnchor, speakerSelectionEnd,
                         false, saveState, savedEditedDialogue);
        if (isInspectDialogueEditing) {
            DrawRectangle(478, 64, 96, 22, DARKBLUE);
            DrawText("Back (Tab)", 483, 67, 16, RAYWHITE);
        }
    }
    if (isEditorPlaying) {
        const RpgDialogue *activeDialogue = NULL;
        int activeLine = -1;
        if (playDialogueIndex >= 0) {
            activeDialogue = dialogue;
            activeLine = playDialogueIndex;
        } else if (playInspectTarget != 0) {
            const RpgInspect *inspect = playInspectTarget == 2 ? &zipper->inspect : &npcInspectData;
            if (playInspectFunctionIndex < inspect->functionCount &&
                inspect->functions[playInspectFunctionIndex].type == RPG_INSPECT_DIALOGUE) {
                activeDialogue = &inspect->functions[playInspectFunctionIndex].dialogue;
                activeLine = playInspectLineIndex;
            }
        }
        if (activeDialogue != NULL && activeLine >= 0 && activeLine < activeDialogue->lineCount) {
            Rectangle dialogueBounds = { 148.0f, 348.0f, 664.0f, 132.0f };
            DrawRectangleRec(dialogueBounds, Fade(RAYWHITE, 0.96f));
            DrawRectangleLinesEx(dialogueBounds, 2.0f, DARKBLUE);
            DrawRectangle(172, 330, 168, 38, DARKBLUE);
            GameFont_Draw(activeDialogue->speakers[activeLine], 188, 340, 21, RAYWHITE);
            GameFont_Draw(activeDialogue->lines[activeLine], 176, 390, 24, DARKBLUE);
            DrawText("E: next", 176, 436, 17, GRAY);
        } else if (playInspectTarget == 0) {
            if (RpgCharacter_IsNear(player, npc, 72.0f)) DrawText("[E] Talk", 24, 78, 17, MAROON);
            if (playZipperFollowsPlayer) DrawText("Double-click Zipper to open", 24, 100, 17, DARKBLUE);
        }
    }
    if (isExitConfirmationOpen) DrawExitConfirmation(isExitDetailsOpen, savedSnapshot, player, npc, stage,
                                                      dialogue, stage3Event, items, savedItems, detailScroll);
    if (RpgScene_IsGameSettings(scene)) RpgScene_DrawGameSettingsOverlay(scene);
    else if (scene != NULL) RpgScene_DrawGameSettingsButton();
    if (isBlockContextMenuOpen) DrawBlockContextMenu(blockContextMenuBounds);
    if (isGlobalMapOpen) DrawGlobalMapModal(stage, mapIndex);
    }
    RpgViewport_EndFrame();
}

int main(void)
{
    /* エディターは設計データを直接読み書きし、保存時に本編用パッケージを更新する。 */
    RpgStageStorage_SetDomain(RPG_STAGE_STORAGE_SETTINGS);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, "1_44MB - RPG Editor");
    ClearWindowState(FLAG_FULLSCREEN_MODE | FLAG_BORDERLESS_WINDOWED_MODE | FLAG_WINDOW_MAXIMIZED);
    SetWindowSize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
    SetWindowPosition(80, 80);
    RpgViewport_Initialize();
    SetExitKey(KEY_NULL);
    InstallEditorCloseHandler();
    (void)RpgGameWindow_Install(GetWindowHandle(), 40.0f);
    SetTargetFPS(60);
    // エディター起動時にも、プレイ中に残った一時objectフォルダとInboxを掃除する。
    RpgObjectFolders_ClearSessionStorage();
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));
    (void)LoadBlockInventoryPreferences();
    RegisterSettingsUiText();
    RpgSceneState editorScene = RpgScene_GameOnly();
    RpgStageCatalog_Load(&stageCatalogData);
    /* 起動時にも設計情報を本編用の静的パッケージへ同期する。実行中データは含めない。 */
    for (int stageIndex = 0; stageIndex < stageCatalogData.count; stageIndex++)
        RpgStageStorage_PublishStage(stageCatalogData.numbers[stageIndex]);
    RpgStageStorage_PublishCatalog(&stageCatalogData);
    currentStageNumber = RpgStageCatalog_GetCurrentNumber(&stageCatalogData);
    RpgStageStorage_LoadStage(currentStageNumber, &stageLoadBuffer);
    RpgLayout_LoadGlobalRuntime(&stageLoadBuffer.layout);
    RpgScene_RegisterText();
    Texture2D zipperTexture = LoadTexture(TextFormat("%s../assets/Sprite/ZIPPER.png",
                                                      GetApplicationDirectory()));
    Texture2D fileTexture = LoadTexture(TextFormat("%s../assets/Sprite/FILE.png",
                                                    GetApplicationDirectory()));
    if (zipperTexture.id != 0) SetTextureFilter(zipperTexture, TEXTURE_FILTER_POINT);
    if (fileTexture.id != 0) SetTextureFilter(fileTexture, TEXTURE_FILTER_POINT);
    RpgCharacter_LoadPlayerSprites();
    RpgLayout layout = stageLoadBuffer.layout;
    stageBackground = RpgStageBackground_Default();
    RpgStageBackground_Load(&stageBackground, layout.backgroundPath);
    groundBlockTexture = RpgStageGroundTexture_Default();
    RpgStageGroundTexture_Load(&groundBlockTexture, layout.groundBlockPath);
    RpgStage_SetGroundTexture(groundBlockTexture.texture);
    RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation,
                                 layout.groundLightness);
    static RpgStage stage;
    stage = stageLoadBuffer.stage;
    RegisterStageFilePickerText(&stage);
    // 保存済みFileの日本語パスも、選択前から ? にならないようフォントへ登録する。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (RpgBlockInventory_IsReferenceObject(stage.blocks[row][column])) {
            GameFont_AddText(RpgStage_GetReferencePathAtCell(&stage, row, column));
        }
    wires = stageLoadBuffer.wires;
    savedWires = wires;
    receivers = stageLoadBuffer.receivers;
    savedReceivers = receivers;
    attachments = stageLoadBuffer.attachments;
    RpgObjectFolders_PrepareAttachmentFolders(&attachments);
    RpgObjectFolder_PrepareZipperAnimationCommand();
    savedAttachments = attachments;
    signalBlocks = stageLoadBuffer.signalBlocks;
    savedSignalBlocks = signalBlocks;
    previewStage = &stage;
    attachmentPreviewShots = RpgDataShots_Default();
    previewEvent = RpgPreviewEvent_Default();
    previewSystem = RpgPreviewSystem_Default();
    RpgPreviewSystem_Register(&previewSystem, PreviewRadioEmitters, NULL);
    RpgPreviewSystem_Register(&previewSystem, PreviewSignalShrinkBlocks, &signalBlocks);
    RpgItems items = stageLoadBuffer.items;
    mapEvents = stageLoadBuffer.mapEvents;
    savedMapEvents = mapEvents;
    for (int itemIndex = 0; itemIndex < items.count; itemIndex++) GameFont_AddText(items.entries[itemIndex].name);
    RpgStage3Event stage3Event = stageLoadBuffer.stage3Event;
    currentStageEntryEvent = &stage3Event;
    areaEntryEvents = stageLoadBuffer.areaEntryEvents;
    for (int areaIndex = 0; areaIndex < RPG_STAGE_MAP_COUNT; areaIndex++)
        for (int functionIndex = 0; functionIndex < areaEntryEvents.entries[areaIndex].inspect.functionCount; functionIndex++)
            for (int lineIndex = 0; lineIndex < areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
                GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
                GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lines[lineIndex]);
            }
    zipperData = RpgZipper_Default();
    RpgZipper_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg", GetApplicationDirectory()), &zipperData);
    zipperData.character.position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
    npcInspect = stageLoadBuffer.npcInspectData;
    for (int functionIndex = 0; functionIndex < npcInspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < npcInspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(npcInspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    activeInspect = &zipperInspectData;
    RpgInspect_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg", GetApplicationDirectory()), &zipperInspectData);
    for (int functionIndex = 0; functionIndex < zipperInspectData.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < zipperInspectData.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(zipperInspectData.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(zipperInspectData.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    activeInspect = &npcInspectData;
    for (int functionIndex = 0; functionIndex < stage3Event.inspect.functionCount; functionIndex++)
        for (int lineIndex = 0; lineIndex < stage3Event.inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(stage3Event.inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(stage3Event.inspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    RpgDialogue dialogue = stageLoadBuffer.dialogue;
    for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
        GameFont_AddText(dialogue.lines[lineIndex]);
        GameFont_AddText(dialogue.speakers[lineIndex]);
    }
    RpgCharacter player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
    RpgCharacter_SetUsesPlayerSpriteCollision(&player, true);
    RpgCharacter npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
    npc.position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
    player.moveSpeed = layout.playerMoveSpeed;
    player.scale = layout.playerScale;
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
    /* A selected line is not necessarily being typed into.  Keep its focus
       separate so selection can remain visible without capturing shortcuts. */
    bool isDialogueTextEditing = false;
    bool isSpeakerEditing = false;
    int speakerCursorIndex = 0;
    int speakerSelectionAnchor = 0;
    int speakerSelectionEnd = 0;
    bool isStage3DialogueEditing = false;
    bool isAreaEntryDialogueEditing = false;
    int entryDialogueAreaIndex = -1;
    bool isInspectDialogueEditing = false;
    bool isExamineFunctionListOpen = false;
    bool isFunctionTypeListOpen = false;
    bool isMoveFunctionEditorOpen = false;
    bool isWaitFunctionEditorOpen = false;
    bool isLayerChangeFunctionEditorOpen = false;
    bool isLayerChangeTargetPicking = false;
    bool isDraggingMovePanel = false;
    Vector2 movePanelDragOffset = { 0.0f, 0.0f };
    bool isMovePreviewPlaying = false;
    float movePreviewElapsed = 0.0f;
    Vector2 movePreviewStartPosition = { 0.0f, 0.0f };
    Vector2 movePreviewSpritePosition = { 0.0f, 0.0f };
    bool isFunctionPreviewPlaying = false;
    int functionPreviewIndex = -1;
    float functionPreviewElapsed = 0.0f;
    int functionListScroll = 0;
    /* 対象指定中だけ、Moveパネル外のキャラクタークリックを専用に消費する。 */
    bool isMoveTargetPicking = false;
    bool isMoveEasingListOpen = false;
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
    bool isZipperPointerFeedbackSuppressed = false;
    bool isReferencePointerFeedbackSuppressed = false;
    int draggedMapEventIndex = -1;
    int selectedBlockInventory = 0;
    int selectedBlockType = 1;
    int selectedReferenceObjectIndex = -1;
    bool isReferencePathEditing = false;
    bool isReferencePathPointerHeld = false;
    char referenceFolderNameInput[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
    int referencePathCursorIndex = 0;
    int referencePathSelectionAnchor = 0;
    int referencePathSelectionEnd = 0;
    bool isBlockInventoryListOpen = false;
    RpgEditorDrag paletteBlockDrag = { 0 };
    int paletteBlockDragSourcePalette = -1;
    int paletteBlockDragSourceSlot = -1;
    RpgEditorDrag paletteRowDrag = { 0 };
    int paletteRowDragSource = -1;
    int selectedPaletteInspectorIndex = -1;
    int paletteNameEditingIndex = -1;
    int paletteNameCursorIndex = 0;
    int paletteNameSelectionAnchor = 0;
    int paletteNameSelectionEnd = 0;
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
    bool isBlockContextMenuOpen = false;
    int blockContextMenuRow = -1;
    int blockContextMenuColumn = -1;
    Rectangle blockContextMenuBounds = { 0 };
    RpgEditorDrag effectBlockDrag = { 0 };
    int draggedEffectBlockRow = -1;
    int draggedEffectBlockColumn = -1;
    int pendingEffectBlockRow = -1;
    int pendingEffectBlockColumn = -1;
    RpgEditorDrag characterDrag = { 0 };
    int draggedCharacterKind = 0;
    RpgEditorDrag referenceDrag = { 0 };
    int draggedReferenceObjectIndex = -1;
    RpgEditorDrag imageObjectDrag = { 0 };
    int draggedImageObjectIndex = -1;
    int selectedItemIndex = -1;
    int selectedAttachmentIndex = -1;
    /* 旗インスペクターの要求は次フレームで既存Play開始経路へ渡す。 */
    int pendingSaveFlagPlayIndex = -1;
    bool isAttachmentPathEditing = false;
    bool isAttachmentPathDragActive = false;
    int selectedDoorRow = -1;
    int selectedDoorColumn = -1;
    bool isItemNameEditing = false;
    int itemNameCursorIndex = 0;
    int itemNameSelectionAnchor = 0;
    int itemNameSelectionEnd = 0;
    const char *message = "Select a character";
    /* TextFormat() returns a rotating temporary buffer.  Palette notifications
       remain visible while the inventory UI formats many other labels, so they
       must own their text rather than retaining that temporary pointer. */
    char paletteStatusMessage[128] = "";
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
    bool isStageSettingsOpen = false;
    bool isStageSettingsPointerHeld = false;
    bool isGlobalMapOpen = false;
    int draggedGlobalMapIndex = -1;
    Vector2 globalMapDragStart = { 0.0f, 0.0f };
    bool isGlobalMapAreaDragActive = false;
    RpgExplorerMode explorerMode = RpgExplorerLauncher_LoadMode();
    RpgBuildCellStorage_LoadMode();
    bool isAreaInspectorOpen = false;
    bool isAreaInspectorPointerHeld = false;
    RpgEditorPlaySnapshot playSnapshot = { 0 };
    RpgReferenceObjects editorPlayReferenceDrops = RpgReferenceObjects_Default();
    int editorPlayStage3IntroIndex = -1;
    bool editorPlayStage3IntroShown = false;
    bool editorPlayAreaEntryShown[RPG_STAGE_MAP_COUNT] = { false };
    RpgStage3Event *editorPlayActiveEntryEvent = &stage3Event;
    bool editorPlayZipperLaunched = false;
    Vector2 editorPlayZipperLaunchVelocity = { 0.0f, 0.0f };
    int editorPlayAttachedDataShotIndex = -1;
    int editorPlayAttachedAttachmentIndex = -1;
    int editorPlayAttachedDynamicBlockIndex = -1;
    int editorPlayAttachedReferenceObjectIndex = -1;
    Vector2 editorPlayAttachedDataShotOffset = { 0.0f, 0.0f };
    bool editorPlayZipperAttachedToBlock = false;
    RpgGridCell editorPlayZipperAttachedBlockCell = { -1, -1 };
    bool editorPlayZipperPointerSelected = false;
    bool editorPlayZipperPointerFeedbackSuppressed = false;
    double editorPlayLastZipperPointerClickTime = -1.0;
    RpgReferenceTarget editorPlaySelectedReference = { .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 };
    bool editorPlayReferencePointerFeedbackSuppressed = false;
    bool editorPlayReferencePointerPressed = false;
    RpgReferenceTarget editorPlayPressedReference = { .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 };
    Vector2 editorPlayReferencePressPosition = { 0.0f, 0.0f };
    bool editorPlayReferenceDragActive = false;
    RpgReferenceTarget editorPlayDraggedReference = { .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 };
    Vector2 editorPlayReferenceDragPosition = { 0.0f, 0.0f };
    double editorPlayLastReferenceClickTime = -1.0;
    float editorPlayZipperAnimationElapsed = -1.0f;
    bool editorPlayZipperControllable = false;
    int editorPlayPreviousMap = 0;
    bool editorPlayWorldCoordinatesInitialized = false;
    bool editorPlayCameraFollowsPlayer = false;
    char editorPlayItemMessage[96] = { 0 };
    float editorPlayItemMessageTimer = 0.0f;
    char editorPlayReferenceText[2048] = { 0 };
    char editorPlayReferenceFileName[RPG_STAGE_REFERENCE_PATH_LENGTH] = "FILE.txt";
    bool editorPlayReferenceTextOpen = false;
    Camera2D editorPlayCamera = {
        .offset = { RPG_EDITOR_WIDTH / 2.0f, 240.0f },
        .target = { RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f, RPG_STAGE_WORLD_HEIGHT / 2.0f },
        .zoom = RPG_EDITOR_WIDTH / (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)
    };
    bool isEditorPlaying = false;
    RpgDataShots editorPlayShots = RpgDataShots_Default();
    RpgMagnetRuntime editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
    RpgButtonEvent editorPlayButtonEvent = RpgButtonEvent_Default();
    bool wasEditorPlayButtonPressed = false;
    int editorPlayDialogueIndex = -1;
    int editorPlayInspectTarget = 0;
    int editorPlayInspectFunctionIndex = 0;
    int editorPlayInspectLineIndex = 0;
    bool isEditorPlayInspectMoveRunning = false;
    float editorPlayInspectMoveElapsed = 0.0f;
    float editorPlayInspectMoveStartX = 0.0f;
    float editorPlayInspectMoveStartY = 0.0f;
    RpgInspectMove *editorPlayActiveInspectMove = NULL;
    float editorPlayInspectMoveTransitionElapsed = 0.0f;
    int editorPlayActiveWaitFunctionIndex = -1;
    float editorPlayInspectWaitElapsed = 0.0f;
    bool editorPlayNpcInspectCompleted = false;
    bool editorPlayZipperInspectCompleted = false;
    bool editorPlayZipperFollowsPlayer = false;
    double lastEditorPlayZipperClickTime = -1.0;
    bool hasStageFolderSync = false;
    int lastStageFolderSyncNumber = 0;
    double nextStageFolderSyncTime = 0.0;
    bool editorPreviewBuildDirty = true;
    bool editorPreviewBuildReady = false;
    int editorPreviewBuildStageNumber = 0;

    while (!shouldExit) {
        RpgGameWindow_UpdateAutoHide();
        RpgViewport_Update();
        /* テキスト編集と競合しない通常状態だけで、編集UIを隠すマップ確認表示を切り替える。 */
        bool isEditorFullscreenShortcut = IsKeyPressed(KEY_X) &&
                                          (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        /* Text focus is a single editor-wide state.  Keep this list limited to
           fields that can receive keyboard input; the shared text module then
           owns the actual capture flag used by every shortcut below. */
        bool isEditorTextEditing = (isDialogueEditorOpen && isDialogueTextEditing &&
                                    activeDialogueLine >= 0) ||
                                   isSpeakerEditing || isItemNameEditing ||
                                   isReferencePathEditing || isAttachmentPathEditing ||
                                   isInspectTitleEditing || isKeyDoorFailureEditing ||
                                   isAttachmentCapacityEditing || isAttachmentSpeedEditing ||
                                   isZipperCapacityEditing || paletteNameEditingIndex >= 0;
        RpgEditorText_SetKeyboardCapture(isEditorTextEditing);
        bool isKeyboardCaptured = RpgEditorText_IsKeyboardCaptured();
        if (!isEditorPlaying && !isKeyboardCaptured && !RpgScene_IsGameSettings(&editorScene) &&
            isEditorFullscreenShortcut) {
            isEditorGameFullscreen = !isEditorGameFullscreen;
        }
        // 編集中の設定は描画だけを残し、背面の編集操作へ入力を渡さない。
        bool suppressEditorInput = isEditorGameFullscreen;
        if (!isEditorPlaying && RpgScene_IsGameSettings(&editorScene)) {
            suppressEditorInput = true;
            RpgScene_UpdateGameSettings(&editorScene);
        } else if (!isEditorPlaying && RpgScene_TryOpenGameSettings(&editorScene)) {
            suppressEditorInput = true;
        }
        if (!suppressEditorInput) {
        // Revertや削除の直後でも、画面は常に有効な二次元ステージIDを参照する。
        if (!isEditorPlaying) mapIndex = RpgStage_FindNearestActiveMap(&stage, mapIndex);
        if (draggedMapEventIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) draggedMapEventIndex = -1;
        bool blockEditedThisFrame = false;
        if (isEditorPlaying) {
            RpgRuntimeContext runtime = {
                .layout=&layout, .stageBackground=&stageBackground, .stage=&stage, .items=&items, .referenceDrops=&editorPlayReferenceDrops, .wires=&wires, .receivers=&receivers, .attachments=&attachments, .signalBlocks=&signalBlocks, .dataShots=&editorPlayShots, .buttonEvent=&editorPlayButtonEvent, .events=&mapEvents, .dialogue=&dialogue, .stage3Event=&stage3Event, .areaEntryEvents=&areaEntryEvents, .zipper=&zipperData, .inspect=&npcInspectData, .player=&player, .npc=&npc, .magnetRuntime=&editorPlayMagnetRuntime,
                .dialogueIndex=&editorPlayDialogueIndex, .stage3IntroIndex=&editorPlayStage3IntroIndex, .inspectFunctionIndex=&editorPlayInspectFunctionIndex, .inspectLineIndex=&editorPlayInspectLineIndex, .inspectTarget=&editorPlayInspectTarget, .isInspectMoveRunning=&isEditorPlayInspectMoveRunning, .inspectMoveElapsed=&editorPlayInspectMoveElapsed, .inspectMoveStartX=&editorPlayInspectMoveStartX, .inspectMoveStartY=&editorPlayInspectMoveStartY, .activeInspectMove=&editorPlayActiveInspectMove, .inspectMoveTransitionElapsed=&editorPlayInspectMoveTransitionElapsed, .activeWaitFunctionIndex=&editorPlayActiveWaitFunctionIndex, .inspectWaitElapsed=&editorPlayInspectWaitElapsed, .stage3IntroShown=&editorPlayStage3IntroShown, .areaEntryShown=editorPlayAreaEntryShown, .activeEntryEvent=&editorPlayActiveEntryEvent, .zipperFollowsPlayer=&editorPlayZipperFollowsPlayer, .isZipperLaunched=&editorPlayZipperLaunched, .zipperLaunchVelocity=&editorPlayZipperLaunchVelocity, .attachedDataShotIndex=&editorPlayAttachedDataShotIndex, .attachedAttachmentIndex=&editorPlayAttachedAttachmentIndex, .attachedDataShotOffset=&editorPlayAttachedDataShotOffset, .isZipperAttachedToBlock=&editorPlayZipperAttachedToBlock, .zipperAttachedBlockCell=&editorPlayZipperAttachedBlockCell, .attachedDynamicBlockIndex=&editorPlayAttachedDynamicBlockIndex, .attachedReferenceObjectIndex=&editorPlayAttachedReferenceObjectIndex,
                .zipperPointerSelected=&editorPlayZipperPointerSelected, .isZipperPointerFeedbackSuppressed=&editorPlayZipperPointerFeedbackSuppressed, .lastZipperPointerClickTime=&editorPlayLastZipperPointerClickTime, .selectedReferencePointerTarget=&editorPlaySelectedReference, .isReferencePointerFeedbackSuppressed=&editorPlayReferencePointerFeedbackSuppressed, .isReferencePointerPressed=&editorPlayReferencePointerPressed, .pressedReferenceTarget=&editorPlayPressedReference, .referencePressPosition=&editorPlayReferencePressPosition, .isReferenceDragActive=&editorPlayReferenceDragActive, .draggedReferenceTarget=&editorPlayDraggedReference, .referenceDragPosition=&editorPlayReferenceDragPosition, .lastReferencePointerClickTime=&editorPlayLastReferenceClickTime, .zipperAnimationElapsed=&editorPlayZipperAnimationElapsed, .npcInspectCompleted=&editorPlayNpcInspectCompleted, .zipperInspectCompleted=&editorPlayZipperInspectCompleted, .isZipperControllable=&editorPlayZipperControllable, .wasDataButtonPressed=&wasEditorPlayButtonPressed, .previousMap=&editorPlayPreviousMap, .worldCoordinatesInitialized=&editorPlayWorldCoordinatesInitialized, .cameraFollowsPlayer=&editorPlayCameraFollowsPlayer, .itemMessage=editorPlayItemMessage, .itemMessageSize=(int)sizeof(editorPlayItemMessage), .itemMessageTimer=&editorPlayItemMessageTimer, .referenceText=editorPlayReferenceText, .referenceTextSize=(int)sizeof(editorPlayReferenceText), .referenceFileName=editorPlayReferenceFileName, .referenceFileNameSize=(int)sizeof(editorPlayReferenceFileName), .isReferenceTextOpen=&editorPlayReferenceTextOpen, .camera=&editorPlayCamera, .zipperTexture=zipperTexture, .fileTexture=fileTexture,
                // エディターではゲーム設定だけを共有し、タイトルへの遷移は許可しない。
                .scene=&editorScene
            };
            runtime.showStopButton = true;
            /* エディター内Playも本編と同じbuild監視を通し、作成済みマスのフォルダ変更を反映する。 */
            RpgStageBuild_Update(&stage);
            RpgRuntime_UpdateAndDraw(&runtime);
            // ランタイム側で設定オーバーレイを描いたフレームは、エディターの操作を続けない。
            if (RpgScene_IsGameSettings(&editorScene)) continue;
        } else UpdateZipperLaunchPreview(&stage, GetFrameTime());
        // プレビューも実フォルダの集計値を使うが、消滅時に File.png は生成せず一時フォルダだけを破棄する。
        if (!isEditorPlaying) {
            RpgObjectFolders_UpdateDataShotLifetimes(&attachmentPreviewShots, &attachments, NULL);
            RpgPreviewSystem_Dispatch(&previewSystem, &previewEvent);
            RpgSignalBlocks_Update(&signalBlocks, &stage, NULL, GetFrameTime());
            RpgDataShots_Update(&attachmentPreviewShots, &attachments, &stage, &receivers, &wires,
                                 layout.electricCellDelay, NULL, GetFrameTime(), true);
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            isBlockInventoryPointerHeld = false;
            isInspectorPointerHeld = false;
            isReferencePathPointerHeld = false;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) isAttachmentErasePointerHeld = false;
        if (isEditorCloseRequested && isEditorPlaying) {
            RpgEditorPlay_Stop(&playSnapshot, &mapIndex, &player, &npc, &stage, &items, &mapEvents,
                               &wires, &receivers, &attachments, &signalBlocks, &zipperData);
            (void)RpgViewport_Resize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
            RpgStageBuild_Close();
            RpgObjectFolders_AbandonStageBuild();
            /* Stop restores the editor snapshot.  Recreate its disposable
               runtime cache now, rather than making the first idle editor
               frame perform this work later. */
            editorPreviewBuildReady = BuildEditorPreviewCache(currentStageNumber, &layout, &stage);
            editorPreviewBuildDirty = !editorPreviewBuildReady;
            editorPreviewBuildStageNumber = editorPreviewBuildReady ? currentStageNumber : 0;
            nextStageFolderSyncTime = editorPreviewBuildReady ? 0.0 : GetTime() + 0.70;
            ResetEditorPreviews(&stage, &signalBlocks, &attachmentPreviewShots, &previewEvent,
                                &isMovePreviewPlaying, &isZipperLaunchPreviewVisible,
                                &isZipperLaunchPreviewReturning);
            editorPlayShots = RpgDataShots_Default();
            editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
            editorPlayButtonEvent = RpgButtonEvent_Default();
            wasEditorPlayButtonPressed = false;
            editorPlayDialogueIndex = -1; editorPlayStage3IntroIndex = -1; editorPlayStage3IntroShown = false; memset(editorPlayAreaEntryShown, 0, sizeof(editorPlayAreaEntryShown)); editorPlayActiveEntryEvent = &stage3Event; editorPlayInspectTarget = -1;
            editorPlayInspectFunctionIndex = -1; editorPlayInspectLineIndex = -1;
            isEditorPlayInspectMoveRunning = false; editorPlayInspectMoveElapsed = 0.0f;
            editorPlayActiveInspectMove = NULL; editorPlayInspectMoveTransitionElapsed = 0.0f;
            editorPlayActiveWaitFunctionIndex = -1; editorPlayInspectWaitElapsed = 0.0f;
            editorPlayNpcInspectCompleted = false; editorPlayZipperInspectCompleted = false;
            editorPlayZipperFollowsPlayer = false; editorPlayZipperControllable = false;
            editorPlayZipperLaunched = false; editorPlayAttachedDataShotIndex = -1; editorPlayAttachedDynamicBlockIndex = -1; editorPlayAttachedReferenceObjectIndex = -1;
            editorPlayAttachedAttachmentIndex = -1; editorPlayZipperAttachedToBlock = false;
            editorPlayZipperAttachedBlockCell = (RpgGridCell){ -1, -1 };
            editorPlayReferenceDragActive = false; editorPlayReferenceTextOpen = false;
            /* 終了確認経由でも、次のPlayへ一時追従Fileを持ち越さない。 */
            editorPlayReferenceDrops = RpgReferenceObjects_Default();
            RpgRuntime_ResetTransientState();
            isEditorPlaying = false;
        }
        if (isEditorCloseRequested && !isExitConfirmationOpen) {
            // ネイティブの閉じる通知を保留し、未保存データがある時だけ確認画面へ移る。
            isEditorCloseRequested = false;
            if (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                     &items, &savedItems) || RpgStageCatalog_IsDirty(&stageCatalogData)) {
                isExitConfirmationOpen = true;
                isExitDetailsOpen = false;
                exitDetailsScroll = 0;
            } else shouldExit = true;
        }
        if (shouldExit) break;
        Vector2 mousePosition = RpgViewport_GetMousePosition();
        Vector2 mapMousePosition = GetEditorMapPointer(mousePosition);
        bool globalMapInteractionThisFrame = false;
        bool isGlobalMapShortcut = !isKeyboardCaptured && !isEditorPlaying &&
            (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_M);
        if (isGlobalMapShortcut) {
            globalMapInteractionThisFrame = true;
            isGlobalMapOpen = !isGlobalMapOpen;
            draggedGlobalMapIndex = -1;
            isGlobalMapAreaDragActive = false;
            if (isGlobalMapOpen) {
                selected = 0;
                isGlobalSettingsOpen = false;
                isStageSettingsOpen = false;
                isAreaInspectorOpen = false;
                message = "World map opened";
            } else message = "World map closed";
        }
        if (!isKeyboardCaptured && isGlobalMapOpen && IsKeyPressed(KEY_ESCAPE)) {
            globalMapInteractionThisFrame = true;
            isGlobalMapOpen = false;
            draggedGlobalMapIndex = -1;
            isGlobalMapAreaDragActive = false;
            message = "World map closed";
        }
        if (!isEditorPlaying && isGlobalMapOpen) {
            int clickedMapIndex = GetGlobalMapAreaAtPointer(&stage, mousePosition);
            if (clickedMapIndex >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                globalMapInteractionThisFrame = true;
                draggedGlobalMapIndex = clickedMapIndex;
                globalMapDragStart = mousePosition;
                isGlobalMapAreaDragActive = false;
            }
            if (draggedGlobalMapIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 delta = Vector2Subtract(mousePosition, globalMapDragStart);
                if (delta.x * delta.x + delta.y * delta.y >= 16.0f)
                    isGlobalMapAreaDragActive = true;
                globalMapInteractionThisFrame = true;
            }
            if (draggedGlobalMapIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                const int releasedMapIndex = draggedGlobalMapIndex;
                const bool wasDrag = isGlobalMapAreaDragActive;
                draggedGlobalMapIndex = -1;
                isGlobalMapAreaDragActive = false;
                globalMapInteractionThisFrame = true;
                if (!wasDrag) {
                    mapIndex = releasedMapIndex;
                    message = TextFormat("Area (%d, %d) selected", stage.mapGridX[mapIndex],
                                         stage.mapGridY[mapIndex]);
                } else {
                    int targetGridX = 0;
                    int targetGridY = 0;
                    if (GetGlobalMapGridAtPointer(&stage, mousePosition, &targetGridX, &targetGridY) &&
                        RpgStage_GetMapAtGrid(&stage, targetGridX, targetGridY) < 0 &&
                        RpgStage_MoveMapToGrid(&stage, releasedMapIndex, targetGridX, targetGridY)) {
                        mapIndex = releasedMapIndex;
                        message = TextFormat("Area moved to (%d, %d)", stage.mapGridX[mapIndex],
                                             stage.mapGridY[mapIndex]);
                    } else message = "Drop on an empty area cell";
                }
            }
            if (draggedGlobalMapIndex < 0 && clickedMapIndex >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                globalMapInteractionThisFrame = true;
                int deletedGridX = stage.mapGridX[clickedMapIndex];
                int deletedGridY = stage.mapGridY[clickedMapIndex];
                if (RpgStage_RemoveMap(&stage, clickedMapIndex)) {
                    RemoveMapOwnedObjects(clickedMapIndex, &items, &mapEvents, &stage, &wires,
                                          &receivers, &attachments);
                    areaEntryEvents.entries[clickedMapIndex] = RpgStage3Event_Default();
                    mapIndex = RpgStage_FindNearestActiveMapAtGrid(&stage, deletedGridX, deletedGridY);
                    message = "Area deleted";
                } else message = "Cannot delete the last area";
            }
            if (!isKeyboardCaptured && IsKeyPressed(KEY_DELETE)) {
                globalMapInteractionThisFrame = true;
                int deletedGridX = stage.mapGridX[mapIndex];
                int deletedGridY = stage.mapGridY[mapIndex];
                if (RpgStage_RemoveMap(&stage, mapIndex)) {
                    RemoveMapOwnedObjects(mapIndex, &items, &mapEvents, &stage, &wires,
                                          &receivers, &attachments);
                    areaEntryEvents.entries[mapIndex] = RpgStage3Event_Default();
                    mapIndex = RpgStage_FindNearestActiveMapAtGrid(&stage, deletedGridX, deletedGridY);
                    message = "Area deleted";
                } else message = "Cannot delete the last area";
            }
            if (!(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
                int adjacentMapIndex = RpgEditorNavigation_RequestAreaMove(&stage, mapIndex);
                if (adjacentMapIndex >= 0) {
                    globalMapInteractionThisFrame = true;
                    mapIndex = adjacentMapIndex;
                    message = TextFormat("Area[%d][%d] selected", stage.mapGridX[mapIndex],
                                         stage.mapGridY[mapIndex]);
                }
            }
        }
        RpgAreaDirection insertionDirection;
        bool isAreaInsertRequested = !isKeyboardCaptured && !isEditorPlaying &&
                                     RpgEditorNavigation_RequestAreaInsertion(&insertionDirection);
        if (isAreaInsertRequested) {
            int insertedMapIndex = RpgStage_InsertAdjacentMap(&stage, mapIndex, insertionDirection);
            if (insertedMapIndex >= 0) {
                mapIndex = insertedMapIndex;
                areaEntryEvents.entries[mapIndex] = RpgStage3Event_Default();
                selected = 0;
                message = TextFormat("Area (%d, %d) inserted", stage.mapGridX[mapIndex],
                                     stage.mapGridY[mapIndex]);
            } else message = "Area limit reached";
        }
        if (selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT) {
            /* パネルの必要高が変化した後も、以前のスクロール位置を入力座標へ
               残さない。Folder は固定高なので常に0へ収束する。 */
            inspectorScrollOffsets[selected] = Clamp(inspectorScrollOffsets[selected],
                                                      0.0f, GetInspectorScrollMaximum(selected));
        }
        if (!isEditorPlaying && !blockMode && selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT) {
            Rectangle inspectorScreenBounds = GetInspectorScreenBounds(selected);
            bool resizeHorizontal = mousePosition.x >= inspectorScreenBounds.x + inspectorScreenBounds.width - 8.0f &&
                                    mousePosition.x <= inspectorScreenBounds.x + inspectorScreenBounds.width &&
                                    mousePosition.y >= inspectorScreenBounds.y &&
                                    mousePosition.y <= inspectorScreenBounds.y + inspectorScreenBounds.height;
            bool resizeVertical = mousePosition.y >= inspectorScreenBounds.y + inspectorScreenBounds.height - 8.0f &&
                                  mousePosition.y <= inspectorScreenBounds.y + inspectorScreenBounds.height &&
                                  mousePosition.x >= inspectorScreenBounds.x &&
                                  mousePosition.x <= inspectorScreenBounds.x + inspectorScreenBounds.width;
            if (!isInspectorResizing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                (resizeHorizontal || resizeVertical)) {
                isInspectorResizing = true;
                resizingInspector = selected;
                isInspectorResizeHorizontal = resizeHorizontal;
                isInspectorResizeVertical = resizeVertical;
                inspectorResizeStartMouse = mousePosition;
                inspectorResizeStartAdjustment = inspectorSizeAdjustments[selected];
            }
            if (isInspectorResizing && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Rectangle baseBounds = GetInspectorBaseBounds(resizingInspector);
                Vector2 delta = Vector2Subtract(mousePosition, inspectorResizeStartMouse);
                if (isInspectorResizeHorizontal)
                    inspectorSizeAdjustments[resizingInspector].x = Clamp(inspectorResizeStartAdjustment.x + delta.x,
                                                                           220.0f - baseBounds.width, 520.0f - baseBounds.width);
                if (isInspectorResizeVertical)
                    inspectorSizeAdjustments[resizingInspector].y = Clamp(inspectorResizeStartAdjustment.y + delta.y,
                                                                           140.0f - baseBounds.height, 480.0f - baseBounds.height);
                inspectorScrollOffsets[resizingInspector] = Clamp(inspectorScrollOffsets[resizingInspector],
                                                                   0.0f, GetInspectorScrollMaximum(resizingInspector));
            }
            if (isInspectorResizing && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                isInspectorResizing = false;
                isInspectorResizeHorizontal = false;
                isInspectorResizeVertical = false;
            }
            if (!isInspectorResizing && CheckCollisionPointRec(mousePosition, inspectorScreenBounds) &&
                fabsf(GetMouseWheelMove()) > 0.0f) {
                inspectorScrollOffsets[selected] = Clamp(inspectorScrollOffsets[selected] - GetMouseWheelMove() * 28.0f,
                                                          0.0f, GetInspectorScrollMaximum(selected));
            }
        }
        Vector2 inspectorMousePosition = selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT ?
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
                                                &items, &savedSnapshot, &savedItems) &&
                    RpgStageCatalog_Save(&stageCatalogData)) shouldExit = true;
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
        Rectangle editorPlayControlBounds = isEditorPlaying ? RpgRuntime_GetStopButtonBounds() : editorPlayToggleBounds;
        bool isEditorPlayToggleClicked = !isGlobalMapOpen && !globalMapInteractionThisFrame && !isExitConfirmationOpen && !blockMode &&
                                         (CheckCollisionPointRec(mousePosition, editorPlayControlBounds) ||
                                          (isEditorPlaying && IsKeyPressed(KEY_F2)));
        Vector2 saveFlagPlayStart = { 0.0f, 0.0f };
        int saveFlagPlayMapIndex = mapIndex;
        bool isSaveFlagPlayRequested = !isEditorPlaying && pendingSaveFlagPlayIndex >= 0;
        if (isSaveFlagPlayRequested &&
            !GetSaveFlagEditorPlayStart(&attachments, &stage, pendingSaveFlagPlayIndex,
                                        &saveFlagPlayStart, &saveFlagPlayMapIndex)) {
            pendingSaveFlagPlayIndex = -1;
            isSaveFlagPlayRequested = false;
            message = "Save flag is unavailable";
        }
        bool shouldToggleEditorPlay = isSaveFlagPlayRequested ||
                                      (isEditorPlayToggleClicked &&
                                       (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                                        (isEditorPlaying && IsKeyPressed(KEY_F2))));
        if (shouldToggleEditorPlay) {
            if (!isEditorPlaying) {
                // 実行前にプレビューだけを消してから現在の編集状態を保持する。
                ResetEditorPreviews(&stage, &signalBlocks, &attachmentPreviewShots, &previewEvent,
                                    &isMovePreviewPlaying, &isZipperLaunchPreviewVisible,
                                    &isZipperLaunchPreviewReturning);
                RpgEditorPlay_Begin(&playSnapshot, mapIndex, &player, &npc, &stage, &items, &mapEvents,
                                    &wires, &receivers, &attachments, &signalBlocks, &zipperData);
                if (isSaveFlagPlayRequested) {
                    /* スナップ済み旗の足元から始め、Zipperは設定対象のまま接続済み追従へ切り替える。 */
                    mapIndex = saveFlagPlayMapIndex;
                    player.position = saveFlagPlayStart;
                }
                pendingSaveFlagPlayIndex = -1;
                /* Copy editor-owned movable references into the isolated play
                   session.  Stop still discards this runtime copy. */
                editorPlayReferenceDrops = stage.referenceObjects;
                RpgRuntime_ResetTransientState();
                PrepareEditorPlayCharacter(&player, &stage);
                /* Runtime area transitions use the player's occupied storage
                   slot as their logical source.  The editor can be showing a
                   different area than the one where the player was placed, so
                   using mapIndex here would send the first transition through
                   an unrelated grid neighbour. */
                /* A normal editor play starts from its stored slot position and
                   needs one conversion.  A save flag already supplied a connected
                   world position, so converting it again sends Area[2][0] through
                   an unrelated packed slot. */
                editorPlayWorldCoordinatesInitialized = isSaveFlagPlayRequested;
                editorPlayPreviousMap = isSaveFlagPlayRequested ? saveFlagPlayMapIndex :
                    RpgStage_FindNearestActiveMap(
                        &stage, (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)));
                /* The idle editor sync pre-generates Stage/editor/StageN.  Reconnect
                   that cache when it still represents this exact edit state; a
                   just-edited or absent cache safely falls back to a normal build. */
                bool usingPreparedEditorPreview = editorPreviewBuildReady &&
                                                  !editorPreviewBuildDirty &&
                                                  editorPreviewBuildStageNumber == currentStageNumber &&
                                                  RpgStageBuild_ResumeEditorPreview(currentStageNumber, &stage);
                if (!usingPreparedEditorPreview &&
                    !RpgStageBuild_CreateEditorPreview(currentStageNumber, &stage, &attachments, player.position)) {
                    (void)RpgEditorPlay_Stop(&playSnapshot, &mapIndex, &player, &npc, &stage, &items,
                                              &mapEvents, &wires, &receivers, &attachments, &signalBlocks, &zipperData);
                    message = "Play build failed";
                } else if (isSaveFlagPlayRequested &&
                           !RpgObjectFolder_EnsureRuntimeZipperDirectory()) {
                    (void)RpgEditorPlay_Stop(&playSnapshot, &mapIndex, &player, &npc, &stage, &items,
                                              &mapEvents, &wires, &receivers, &attachments, &signalBlocks, &zipperData);
                    RpgStageBuild_Close();
                    message = "Save flag Zipper build failed";
                } else if (!RpgViewport_Resize(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                                RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE)) {
                    (void)RpgEditorPlay_Stop(&playSnapshot, &mapIndex, &player, &npc, &stage, &items,
                                              &mapEvents, &wires, &receivers, &attachments, &signalBlocks, &zipperData);
                    RpgStageBuild_Close();
                    message = "Play viewport failed";
                } else {
                editorPlayShots = RpgDataShots_Default();
                editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
                editorPlayButtonEvent = RpgButtonEvent_Default();
                wasEditorPlayButtonPressed = false;
                editorPlayDialogueIndex = -1;
                editorPlayInspectTarget = -1;
                editorPlayInspectFunctionIndex = 0;
                editorPlayInspectLineIndex = 0;
                isEditorPlayInspectMoveRunning = false;
                editorPlayActiveInspectMove = NULL; editorPlayInspectMoveTransitionElapsed = 0.0f;
                editorPlayActiveWaitFunctionIndex = -1; editorPlayInspectWaitElapsed = 0.0f;
                editorPlayNpcInspectCompleted = false;
                editorPlayZipperInspectCompleted = false;
                editorPlayZipperFollowsPlayer = false;
                editorPlayZipperControllable = false;
                RpgZipper_ClearHeldObject(&zipperData);
                lastEditorPlayZipperClickTime = -1.0;
                if (isSaveFlagPlayRequested) {
                    RpgRuntime_StartFollowingZipper(&zipperData, &player,
                                                     &editorPlayZipperFollowsPlayer,
                                                     &editorPlayZipperControllable,
                                                     &editorPlayZipperInspectCompleted,
                                                     &editorPlayZipperAnimationElapsed);
                }
                isEditorPlaying = true;
                isGlobalMapOpen = false;
                draggedGlobalMapIndex = -1;
                isGlobalMapAreaDragActive = false;
                blockMode = false;
                selected = 0;
                activeDialogueLine = -1;
                isDialogueEditorOpen = false;
                isExamineFunctionListOpen = false;
                isFunctionTypeListOpen = false;
                isMoveFunctionEditorOpen = false;
                isGlobalSettingsOpen = false;
                isStageSettingsOpen = false;
                isAreaInspectorOpen = false;
                message = isSaveFlagPlayRequested ? "Play started from save flag" : "Play started";
                }
            } else {
                RpgEditorPlay_Stop(&playSnapshot, &mapIndex, &player, &npc, &stage, &items, &mapEvents,
                                   &wires, &receivers, &attachments, &signalBlocks, &zipperData);
                (void)RpgViewport_Resize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
                RpgStageBuild_Close();
                RpgObjectFolders_AbandonStageBuild();
                /* The play snapshot has just restored the exact edit state.
                   Prepare its isolated runtime cache before normal editor
                   input resumes, so the next Play only reconnects it. */
                editorPreviewBuildReady = BuildEditorPreviewCache(currentStageNumber, &layout, &stage);
                editorPreviewBuildDirty = !editorPreviewBuildReady;
                editorPreviewBuildStageNumber = editorPreviewBuildReady ? currentStageNumber : 0;
                nextStageFolderSyncTime = editorPreviewBuildReady ? 0.0 : GetTime() + 0.70;
                RpgObjectFolders_PrepareAttachmentFolders(&attachments);
                RpgObjectFolder_PrepareZipperAnimationCommand();
                ResetEditorPreviews(&stage, &signalBlocks, &attachmentPreviewShots, &previewEvent,
                                    &isMovePreviewPlaying, &isZipperLaunchPreviewVisible,
                                    &isZipperLaunchPreviewReturning);
                editorPlayShots = RpgDataShots_Default();
                editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
                editorPlayButtonEvent = RpgButtonEvent_Default();
                wasEditorPlayButtonPressed = false;
                editorPlayDialogueIndex = -1; editorPlayStage3IntroIndex = -1; editorPlayStage3IntroShown = false; memset(editorPlayAreaEntryShown, 0, sizeof(editorPlayAreaEntryShown)); editorPlayActiveEntryEvent = &stage3Event; editorPlayInspectTarget = -1;
                editorPlayInspectFunctionIndex = -1; editorPlayInspectLineIndex = -1;
                isEditorPlayInspectMoveRunning = false; editorPlayInspectMoveElapsed = 0.0f;
                editorPlayActiveInspectMove = NULL; editorPlayInspectMoveTransitionElapsed = 0.0f;
                editorPlayActiveWaitFunctionIndex = -1; editorPlayInspectWaitElapsed = 0.0f;
                editorPlayNpcInspectCompleted = false; editorPlayZipperInspectCompleted = false;
                editorPlayZipperFollowsPlayer = false; editorPlayZipperControllable = false;
                editorPlayZipperLaunched = false; editorPlayAttachedDataShotIndex = -1; editorPlayAttachedDynamicBlockIndex = -1; editorPlayAttachedReferenceObjectIndex = -1;
                editorPlayAttachedAttachmentIndex = -1; editorPlayZipperAttachedToBlock = false;
                editorPlayZipperAttachedBlockCell = (RpgGridCell){ -1, -1 };
                editorPlayReferenceDragActive = false; editorPlayReferenceTextOpen = false;
                editorPlayReferenceDrops = RpgReferenceObjects_Default();
                RpgRuntime_ResetTransientState();
                isEditorPlaying = false;
                message = "Play stopped - editor state restored";
            }
        }
        /* エディター内プレイだけの確認操作。ゲーム本編の入力には追加しない。 */
        if (isEditorPlaying && IsKeyPressed(KEY_Q)) {
            message = RpgObjectFolder_OpenZipperDirectory() ?
                      "Zipper directory opened" : "Zipper directory is unavailable";
        }
        if (isEditorPlaying && editorPlayZipperFollowsPlayer &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgCharacter localZipper = zipperData.character;
            localZipper.position.x -= mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            if (CheckCollisionPointRec(mousePosition, RpgZipper_GetSpriteBounds(&localZipper, 380.0f))) {
                if (lastEditorPlayZipperClickTime >= 0.0 &&
                    GetTime() - lastEditorPlayZipperClickTime <= 0.35)
                    RpgObjectFolder_OpenZipperDirectory();
                lastEditorPlayZipperClickTime = GetTime();
            }
        }
        if (!wasExitConfirmationOpen && !isExitConfirmationOpen && !isEditorPlaying) {
        // 保存結果は、保存後の次の操作が始まった時点で通常のボタン表示へ戻す。
        if (GetSaveState(message) != EDITOR_SAVE_NONE &&
            (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
             GetKeyPressed() != KEY_NULL)) {
            message = "Editing";
        }
        // モーダルを閉じたクリックが、同じフレームの背面UIへ届かないように記憶する。
        bool wasModalOpenAtFrameStart = isGlobalMapOpen || globalMapInteractionThisFrame || isFunctionPreviewPlaying || isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen || isWaitFunctionEditorOpen || isLayerChangeFunctionEditorOpen;
        RpgDialogue *editedDialogue = isInspectDialogueEditing ? &npcInspect.functions[inspectFunctionIndex].dialogue :
                                      isStage3DialogueEditing ? &stage3Event.dialogue :
                                      isAreaEntryDialogueEditing && entryDialogueAreaIndex >= 0 ?
                                      &areaEntryEvents.entries[entryDialogueAreaIndex].dialogue : &dialogue;
        int visibleDialogueLines = GetVisibleDialogueLines(dialogueBlockHeight);
        bool isDialogueEditing = isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen || isWaitFunctionEditorOpen || isLayerChangeFunctionEditorOpen;
        if (isMovePreviewPlaying) {
            RpgInspectMove *previewMove = &npcInspect.functions[inspectFunctionIndex].move;
            movePreviewElapsed += GetFrameTime();
            float progress = Clamp(movePreviewElapsed / previewMove->duration, 0.0f, 1.0f);
            float easedProgress = RpgInspect_EaseMoveProgress(previewMove->easing, progress);
            Vector2 endpoint = GetMoveEndpoint(previewMove, &player, &npc, &zipperData, &stage);
            movePreviewSpritePosition.x = movePreviewStartPosition.x +
                (endpoint.x - movePreviewStartPosition.x) * easedProgress;
            movePreviewSpritePosition.y = movePreviewStartPosition.y +
                (endpoint.y - movePreviewStartPosition.y) * easedProgress;
            if (progress >= 1.0f) {
                isMovePreviewPlaying = false;
                message = "Move preview complete";
            }
        }
        /* 地図クリックはプレビュー専用に消費し、編集を始めたFunction一覧へ戻す。 */
        bool didCancelFunctionPreview = isFunctionPreviewPlaying && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (didCancelFunctionPreview) {
            RestoreFunctionPreviewState(&stage, &player, &npc, &zipperData,
                                        &functionPreviewStageSnapshot, &functionPreviewPlayerSnapshot,
                                        &functionPreviewNpcSnapshot, &functionPreviewZipperSnapshot);
            ResetFunctionPreviewMoves();
            isFunctionPreviewPlaying = false;
            functionPreviewIndex = -1;
            functionPreviewElapsed = 0.0f;
            isExamineFunctionListOpen = true;
            message = "Function preview cancelled";
        } else if (isFunctionPreviewPlaying) {
            if (functionPreviewIndex < 0 || functionPreviewIndex >= npcInspect.functionCount) {
                /* 最後に開始した並行Moveが終わるまで、復元せずプレビューを表示し続ける。 */
                if (HasRunningFunctionPreviewMoves()) {
                    UpdateFunctionPreviewMoves(&player, &npc, &zipperData, &stage, GetFrameTime());
                } else {
                    RestoreFunctionPreviewState(&stage, &player, &npc, &zipperData,
                                                &functionPreviewStageSnapshot, &functionPreviewPlayerSnapshot,
                                                &functionPreviewNpcSnapshot, &functionPreviewZipperSnapshot);
                    ResetFunctionPreviewMoves();
                    isFunctionPreviewPlaying = false;
                    functionPreviewIndex = -1;
                    functionPreviewElapsed = 0.0f;
                    isExamineFunctionListOpen = true;
                    message = "Function preview complete";
                }
            } else {
                float previewDeltaTime = GetFrameTime();
                UpdateFunctionPreviewMoves(&player, &npc, &zipperData, &stage, previewDeltaTime);
                functionPreviewElapsed += previewDeltaTime;
                for (int iteration = 0; iteration < RPG_INSPECT_MAX_FUNCTIONS &&
                     functionPreviewIndex >= 0 && functionPreviewIndex < npcInspect.functionCount; iteration++) {
                    RpgInspectFunction *previewFunction = &npcInspect.functions[functionPreviewIndex];
                    if (previewFunction->type == RPG_INSPECT_MOVE) {
                        FunctionPreviewMoveState *moveState = StartFunctionPreviewMove(&previewFunction->move,
                                                                                         &player, &npc, &zipperData, &stage);
                        if (moveState == NULL) break;
                        moveState->transitionElapsed += previewDeltaTime;
                        if (moveState->transitionElapsed < previewFunction->move.nextFunctionDelay) break;
                        moveState->transitioned = true;
                        if (!moveState->running) moveState->move = NULL;
                    } else if (previewFunction->type == RPG_INSPECT_LAYER_CHANGE) {
                        int imageIndex = RpgImageObjects_FindById(&stage.imageObjects,
                                                                  previewFunction->layerChange.targetImageObjectId);
                        if (imageIndex >= 0)
                            stage.imageObjects.entries[imageIndex].layer =
                                (RpgImageObjectLayer)Clamp(previewFunction->layerChange.layer, 0, 2);
                    } else if (functionPreviewElapsed < GetFunctionPreviewDuration(previewFunction)) break;
                    functionPreviewIndex++;
                    functionPreviewElapsed = 0.0f;
                }
            }
        }
        // 日本語IMEのローマ字入力中は、文字キーのエディター操作を受け付けない。
        // マップを切り替える時は、前のマップの選択・入力状態を必ず解除する。
        int requestedMapIndex = (!isKeyboardCaptured && !isEditorPlaying && !isGlobalMapOpen && !isAreaInsertRequested) ?
                                RpgEditorNavigation_RequestAreaMove(&stage, mapIndex) : -1;
        if (!isDialogueEditing && requestedMapIndex >= 0) {
            mapIndex = requestedMapIndex;
            // 設定パネルを開いたままでも、対象エリアだけを更新して表示を維持する。
            if (isGlobalSettingsOpen) selected = RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR;
            else if (isStageSettingsOpen) selected = RPG_EDITOR_STAGE_SETTINGS_INSPECTOR;
            else if (isAreaInspectorOpen) selected = RPG_EDITOR_AREA_SETTINGS_INSPECTOR;
            else selected = 0;
            activeDialogueLine = -1; isDialogueEditorOpen = false;
            isSpeakerEditing = false; isStage3DialogueEditing = false; isAreaEntryDialogueEditing = false; entryDialogueAreaIndex = -1;
            isInspectDialogueEditing = false; isExamineFunctionListOpen = false; isFunctionTypeListOpen = false; isMoveFunctionEditorOpen = false; isMovePreviewPlaying = false;
            isZipperPointerFeedbackSuppressed = true;
        }
        if (!isKeyboardCaptured && !isGlobalMapOpen && !isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing && !isZipperCapacityEditing && IsKeyPressed(KEY_B)) { blockMode = !blockMode; selected = 0; activeDialogueLine = -1; draggedDialogueLine = -1; isGlobalSettingsOpen = false; isStageSettingsOpen = false; isAreaInspectorOpen = false; isZipperPointerFeedbackSuppressed = true; isReferencePointerFeedbackSuppressed = true; }
        /* Shift+矢印はステージを切り替えない。ブロックモードだけで、現在の
           パレットを循環して切り替える。Ctrl+矢印のエリア挿入は従来どおり優先する。 */
        if (!isKeyboardCaptured && blockMode && !isEditorPlaying && !isGlobalMapOpen &&
            !isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing &&
            !isZipperCapacityEditing && !isItemNameEditing && !isReferencePathEditing &&
            !isInspectTitleEditing && !isKeyDoorFailureEditing && paletteNameEditingIndex < 0 &&
            (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
            !(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
            (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) ||
             IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN))) {
            /* 横は従来どおり 左=前 / 右=次。縦は見た目の一覧方向に合わせ、
               上=次 / 下=前へ反転する。 */
            int delta = (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN)) ? -1 : 1;
            int paletteCount = RpgBlockInventory_Count();
            selectedBlockInventory = (selectedBlockInventory + delta + paletteCount) % paletteCount;
            const RpgBlockInventory *inventory = RpgBlockInventory_Get(selectedBlockInventory);
            selectedBlockType = GetBlockInventoryFirstBlockType(inventory);
            isBlockInventoryListOpen = false;
            snprintf(paletteStatusMessage, sizeof(paletteStatusMessage), "Palette %d: %s",
                     selectedBlockInventory + 1, inventory->name);
            message = paletteStatusMessage;
        }
        if (!isKeyboardCaptured && blockMode && !isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing && !isZipperCapacityEditing &&
            paletteNameEditingIndex < 0 &&
            (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_D))) {
            const RpgBlockInventory *inventory = RpgBlockInventory_Get(selectedBlockInventory);
            int selectedSlot = 0;
            for (int index = 0; index < inventory->count; index++)
                if (inventory->blockTypes[index] == selectedBlockType) { selectedSlot = index; break; }
            if (IsKeyPressed(KEY_A)) selectedSlot = (selectedSlot + inventory->count - 1) % inventory->count;
            else selectedSlot = (selectedSlot + 1) % inventory->count;
            selectedBlockType = inventory->blockTypes[selectedSlot];
            snprintf(paletteStatusMessage, sizeof(paletteStatusMessage), "Palette: %s", inventory->name);
            message = paletteStatusMessage;
        }
        if (!isKeyboardCaptured && blockMode && !isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing &&
            !isZipperCapacityEditing && !isItemNameEditing && !isReferencePathEditing &&
            !isInspectTitleEditing && !isKeyDoorFailureEditing && paletteNameEditingIndex < 0) {
            int requestedPalette = -1;
            if (IsKeyPressed(KEY_ONE)) requestedPalette = 0;
            else if (IsKeyPressed(KEY_TWO)) requestedPalette = 1;
            else if (IsKeyPressed(KEY_THREE)) requestedPalette = 2;
            else if (IsKeyPressed(KEY_FOUR)) requestedPalette = 3;
            else if (IsKeyPressed(KEY_FIVE)) requestedPalette = 4;
            else if (IsKeyPressed(KEY_SIX)) requestedPalette = 5;
            else if (IsKeyPressed(KEY_SEVEN)) requestedPalette = 6;
            else if (IsKeyPressed(KEY_EIGHT)) requestedPalette = 7;
            else if (IsKeyPressed(KEY_NINE)) requestedPalette = 8;
            else if (IsKeyPressed(KEY_ZERO)) requestedPalette = 9;
            if (requestedPalette >= 0 && requestedPalette < RpgBlockInventory_Count()) {
                const RpgBlockInventory *inventory = RpgBlockInventory_Get(requestedPalette);
                selectedBlockInventory = requestedPalette;
                selectedBlockType = GetBlockInventoryFirstBlockType(inventory);
                isBlockInventoryListOpen = false;
                snprintf(paletteStatusMessage, sizeof(paletteStatusMessage), "Palette %d: %s",
                         requestedPalette + 1, inventory->name);
                message = paletteStatusMessage;
            }
        }
        if (!isKeyboardCaptured && !isGlobalMapOpen && !isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing && !isZipperCapacityEditing && IsKeyPressed(KEY_ESCAPE)) { selected = 0; isGlobalSettingsOpen = false; isStageSettingsOpen = false; isAreaInspectorOpen = false; activeDialogueLine = -1; draggedDialogueLine = -1; isZipperPointerFeedbackSuppressed = true; isReferencePointerFeedbackSuppressed = true; message = "Selection cleared"; }
        if (isDialogueEditorOpen && activeDialogueLine >= 0) {
            UpdateImeCandidateWindow(activeDialogueLine, dialogueScroll);
        }

        bool isInspectorSurfaceClicked = (!blockMode || selected == RPG_EDITOR_PALETTE_INSPECTOR) && !wasModalOpenAtFrameStart &&
                                         selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT &&
                                         CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(selected));
        bool isPlayerInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 1 &&
                                        IsInspectorContentScreenPoint(1, mousePosition);
        bool isNpcSummaryClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 2 && !isDialogueEditorOpen && !isExamineFunctionListOpen &&
                                   IsInspectorContentScreenPoint(2, mousePosition);
        bool isItemInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 4 &&
                                      IsInspectorContentScreenPoint(4, mousePosition);
        bool isDoorInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 5 &&
                                      IsInspectorContentScreenPoint(5, mousePosition);
        bool isAttachmentInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 6 &&
                                            IsInspectorContentScreenPoint(6, mousePosition);
        bool isReferenceInspectorClicked = !blockMode && !wasModalOpenAtFrameStart && selected == 7 &&
                                           IsInspectorContentScreenPoint(7, mousePosition);
        bool isImageObjectInspectorClicked = !wasModalOpenAtFrameStart &&
                                             selected == RPG_EDITOR_IMAGE_INSPECTOR &&
                                             IsInspectorContentScreenPoint(RPG_EDITOR_IMAGE_INSPECTOR, mousePosition);
        bool isPaletteInspectorClicked = !wasModalOpenAtFrameStart &&
                                         selected == RPG_EDITOR_PALETTE_INSPECTOR &&
                                         selectedPaletteInspectorIndex >= 0 &&
                                         selectedPaletteInspectorIndex < RpgBlockInventory_Count() &&
                                         IsInspectorContentScreenPoint(RPG_EDITOR_PALETTE_INSPECTOR, mousePosition);
        bool isGlobalSettingsInspectorClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                                isGlobalSettingsOpen && selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR &&
                                                IsInspectorContentScreenPoint(selected, mousePosition);
        bool isStageSettingsInspectorClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                               isStageSettingsOpen && selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR &&
                                               IsInspectorContentScreenPoint(selected, mousePosition);
        bool isAreaSettingsInspectorClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                             isAreaInspectorOpen && selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR &&
                                             IsInspectorContentScreenPoint(selected, mousePosition);
        bool isDialogueEditorClicked = isDialogueEditorOpen &&
                                       CheckCollisionPointRec(mousePosition, dialogueEditorBounds);
        bool isInspectorClicked = isPlayerInspectorClicked || isNpcSummaryClicked ||
                                  isItemInspectorClicked || isDoorInspectorClicked || isDialogueEditorClicked;
        isInspectorClicked = isInspectorClicked || isAttachmentInspectorClicked || isReferenceInspectorClicked ||
                             isImageObjectInspectorClicked || isPaletteInspectorClicked ||
                             isGlobalSettingsInspectorClicked || isStageSettingsInspectorClicked ||
                             isAreaSettingsInspectorClicked ||
                             isInspectorSurfaceClicked;
        // クリックの所有者をフレーム単位で確定し、重なった他UIやマップ選択へ伝搬させない。
        bool isInspectorClickCaptured = isInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool isCloseInspectorClicked = isInspectorSurfaceClicked &&
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
        bool isGlobalSettingsButtonClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                             CheckCollisionPointRec(mousePosition, globalSettingsButtonBounds);
        /* 最前面モーダルが開いている間は、キューに残る設定インスペクターを含む
           背面パネルへ入力を渡さない。モーダルだけがこのクリックを所有する。 */
        bool isGlobalSettingsPanelClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                            isGlobalSettingsOpen && selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR &&
                                            IsInspectorContentScreenPoint(selected, mousePosition);
        bool isStageSettingsButtonClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                            CheckCollisionPointRec(mousePosition, stageSettingsButtonBounds);
        bool isStageSettingsPanelClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                           isStageSettingsOpen && selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR &&
                                           IsInspectorContentScreenPoint(selected, mousePosition);
        bool isAreaInspectorButtonClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                            CheckCollisionPointRec(mousePosition, areaInspectorButtonBounds);
        bool isAreaInspectorPanelClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                            isAreaInspectorOpen && selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR &&
                                           IsInspectorContentScreenPoint(selected, mousePosition);
        if (isGlobalSettingsButtonClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isGlobalSettingsOpen = !isGlobalSettingsOpen;
            isGlobalSettingsPointerHeld = true;
            isStageSettingsOpen = false;
            isAreaInspectorOpen = false;
            if (isGlobalSettingsOpen) {
                ResetSettingsTabs(globalSettingsTabExpanded, RPG_GLOBAL_SETTINGS_TAB_COUNT);
                inspectorScrollOffsets[RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR] = 0.0f;
                selected = RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR;
            }
            else if (selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR) selected = 0;
        } else if (isGlobalSettingsPanelClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isGlobalSettingsPointerHeld = true;
            int tabIndex = FindSettingsTabAtPoint(globalSettingsTabs, globalSettingsTabExpanded,
                                                  RPG_GLOBAL_SETTINGS_TAB_COUNT, inspectorMousePosition);
            if (tabIndex >= 0) {
                globalSettingsTabExpanded[tabIndex] = !globalSettingsTabExpanded[tabIndex];
            } else if (globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_RUNTIME]) {
                float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                                   RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_RUNTIME);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 20, 44, 26 }))
                layout.electricCellDelay = Clamp(layout.electricCellDelay - 0.01f, 0.01f, 2.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 20, 44, 26 }))
                layout.electricCellDelay = Clamp(layout.electricCellDelay + 0.01f, 0.01f, 2.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 70, 44, 26 }))
                layout.magnetMetalSpeed = Clamp(layout.magnetMetalSpeed - 16.0f, 16.0f, 960.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 70, 44, 26 }))
                layout.magnetMetalSpeed = Clamp(layout.magnetMetalSpeed + 16.0f, 16.0f, 960.0f);
            }
            if (tabIndex < 0 && globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_PLAYER]) {
                float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                                   RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_PLAYER);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 20, 44, 26 }))
                    player.moveSpeed = Clamp(player.moveSpeed - 20.0f, 60.0f, 480.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 20, 44, 26 }))
                    player.moveSpeed = Clamp(player.moveSpeed + 20.0f, 60.0f, 480.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 70, 44, 26 }))
                    player.scale = Clamp(player.scale - 0.1f, 0.5f, 1.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 70, 44, 26 }))
                    player.scale = Clamp(player.scale + 0.1f, 0.5f, 1.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 108, 90, 26 }))
                    message = SaveCharacterSettings(&layout, &player, true, &savedSnapshot) ? "Saved" : "Save failed";
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, y + 108, 90, 26 })) {
                    RevertCharacterSettings(&savedSnapshot, &layout, &player, true);
                    message = "Reverted to saved";
                }
            }
            if (tabIndex < 0 && globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_ZIPPER]) {
                float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                                   RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_ZIPPER);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 20, 44, 26 }))
                layout.zipperFolderReturnDuration = Clamp(layout.zipperFolderReturnDuration - 0.05f, 0.10f, 5.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 20, 44, 26 }))
                layout.zipperFolderReturnDuration = Clamp(layout.zipperFolderReturnDuration + 0.05f, 0.10f, 5.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 76, 44, 26 }))
                layout.zipperFolderReturnAnimationDelay = Clamp(layout.zipperFolderReturnAnimationDelay - 0.05f, 0.0f, 5.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 76, 44, 26 }))
                layout.zipperFolderReturnAnimationDelay = Clamp(layout.zipperFolderReturnAnimationDelay + 0.05f, 0.0f, 5.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 132, 44, 26 }))
                layout.referenceFollowerScale = Clamp(layout.referenceFollowerScale - 0.05f, 0.15f, 1.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 132, 44, 26 }))
                layout.referenceFollowerScale = Clamp(layout.referenceFollowerScale + 0.05f, 0.15f, 1.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 188, 44, 26 }))
                    zipperData.launchSpeed = Clamp(zipperData.launchSpeed - 60.0f, 120.0f, 2400.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 188, 44, 26 }))
                    zipperData.launchSpeed = Clamp(zipperData.launchSpeed + 60.0f, 120.0f, 2400.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 244, 44, 26 }))
                    zipperData.returnSpeed = Clamp(zipperData.returnSpeed - 30.0f, 60.0f, 1200.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 244, 44, 26 }))
                    zipperData.returnSpeed = Clamp(zipperData.returnSpeed + 30.0f, 60.0f, 1200.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 300, 44, 26 }))
                    zipperData.followSpeed = Clamp(zipperData.followSpeed - 30.0f, 60.0f, 1200.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, y + 300, 44, 26 }))
                    zipperData.followSpeed = Clamp(zipperData.followSpeed + 30.0f, 60.0f, 1200.0f);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 344, 188, 28 })) {
                    zipperData.launchPreviewEnabled = !zipperData.launchPreviewEnabled;
                    isZipperLaunchPreviewVisible = false;
                    isZipperLaunchPreviewReturning = false;
                    zipperLaunchPreviewCooldown = 1.0f;
                    message = zipperData.launchPreviewEnabled ? "Preview enabled" : "Preview disabled";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 378, 90, 26 }))
                    message = SaveZipperSettings(&savedSnapshot) ? "Saved" : "Save failed";
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, y + 378, 90, 26 })) {
                    zipperData = savedSnapshot.zipper;
                    message = "Reverted to saved";
                }
            }
            if (tabIndex < 0 && globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_BUILD]) {
                float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                                   RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_BUILD);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 22, 88, 28 })) {
                explorerMode = RPG_EXPLORER_MODE_VIRTUAL;
                message = RpgExplorerLauncher_SaveMode(explorerMode) ? "Explorer: Virtual" : "Explorer setting failed";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 812, y + 22, 92, 28 })) {
                explorerMode = RPG_EXPLORER_MODE_WINDOWS;
                message = RpgExplorerLauncher_SaveMode(explorerMode) ? "Explorer: Windows" : "Explorer setting failed";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 82, 88, 28 })) {
                message = RpgBuildCellStorage_SaveMode(RPG_BUILD_CELL_STORAGE_COMPACT) ?
                          "Build: compact cells" : "Build setting failed";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 812, y + 82, 92, 28 })) {
                message = "All folders is disabled";
                }
            }
        } else if (isStageSettingsButtonClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isStageSettingsOpen = !isStageSettingsOpen;
            isStageSettingsPointerHeld = true;
            isGlobalSettingsOpen = false;
            isAreaInspectorOpen = false;
            if (isStageSettingsOpen) {
                ResetSettingsTabs(stageSettingsTabExpanded, RPG_STAGE_SETTINGS_TAB_COUNT);
                inspectorScrollOffsets[RPG_EDITOR_STAGE_SETTINGS_INSPECTOR] = 0.0f;
                selected = RPG_EDITOR_STAGE_SETTINGS_INSPECTOR;
            }
            else if (selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR) selected = 0;
        } else if (isStageSettingsPanelClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isStageSettingsPointerHeld = true;
            int tabIndex = FindSettingsTabAtPoint(stageSettingsTabs, stageSettingsTabExpanded,
                                                  RPG_STAGE_SETTINGS_TAB_COUNT, inspectorMousePosition);
            int stageIndex = RpgStageCatalog_FindIndex(&stageCatalogData, currentStageNumber);
            int targetStageNumber = 0;
            if (tabIndex >= 0) {
                stageSettingsTabExpanded[tabIndex] = !stageSettingsTabExpanded[tabIndex];
                if (tabIndex == RPG_STAGE_SETTINGS_TAB_STAGE && !stageSettingsTabExpanded[tabIndex])
                    isZipperCapacityEditing = false;
            } else if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_STAGE]) {
                float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                   RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_STAGE);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 6, 28, 26 }) && stageIndex > 0)
                    targetStageNumber = RpgStageCatalog_GetNumberAt(&stageCatalogData, stageIndex - 1);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 876, y + 6, 28, 26 }) &&
                         stageIndex >= 0 && stageIndex + 1 < stageCatalogData.count)
                    targetStageNumber = RpgStageCatalog_GetNumberAt(&stageCatalogData, stageIndex + 1);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 52, 188, 26 })) {
                if (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                         &items, &savedItems)) message = "Save or Revert before changing stage";
                else targetStageNumber = RpgStageCatalog_Add(&stageCatalogData);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 86, 188, 26 })) {
                if (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                         &items, &savedItems)) message = "Save or Revert before deleting stage";
                else if (RpgStageCatalog_DeleteCurrent(&stageCatalogData))
                    targetStageNumber = RpgStageCatalog_GetCurrentNumber(&stageCatalogData);
                else message = "Cannot delete the last stage";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 796, y + 118, 70, 26 })) {
                snprintf(zipperCapacityInput, sizeof(zipperCapacityInput), "%u", layout.zipperMaxCapacityKB);
                isZipperCapacityEditing = true;
                message = "Enter Zipper capacity in KB";
                }
            }
            if (tabIndex < 0 && stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_ENTRY_EVENT]) {
                float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                   RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_ENTRY_EVENT);
                RpgEditorInspectorActionRow entryRow = MakeInspectorActionRow(716.0f, y + 6.0f, 188.0f, 24.0f);
                Rectangle editBounds = { 716.0f, entryRow.contentBottom + 28.0f, 188.0f, 26.0f };
                if (CheckCollisionPointRec(inspectorMousePosition, entryRow.actionBounds)) {
                stage3Event.inspect.enabled = !stage3Event.inspect.enabled;
                stage3Event.enabled = stage3Event.inspect.enabled;
                message = stage3Event.inspect.enabled ? "Stage entry event enabled" : "Stage entry event disabled";
                } else if (CheckCollisionPointRec(inspectorMousePosition, editBounds)) {
                activeInspect = &stage3Event.inspect;
                inspectFunctionIndex = 0;
                modalHistory.count = 0;
                isExamineFunctionListOpen = true;
                isFunctionTypeListOpen = false;
                isMoveFunctionEditorOpen = false;
                isDialogueEditorOpen = false;
                isInspectDialogueEditing = false;
                isStage3DialogueEditing = false;
                isAreaEntryDialogueEditing = false;
                entryDialogueAreaIndex = -1;
                activeDialogueLine = -1;
                dialogueScroll = 0;
                message = "Stage entry functions opened";
                }
            }
            if (tabIndex < 0 && stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_BACKGROUND]) {
                float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                   RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_BACKGROUND);
                RpgEditorFilePickerLayout backgroundPicker = { y + 1.0f, { 716.0f, y + 12.0f, 188.0f, 18.0f },
                                                                { 716.0f, y + 32.0f, 188.0f, 28.0f }, 0.0f };
                backgroundPicker = ResolveInspectorFilePickerLayout(&backgroundPicker, layout.backgroundPath);
                float clearY = backgroundPicker.contentBottom + 8.0f;
                float backgroundControlY = clearY + 34.0f;
                if (CheckCollisionPointRec(inspectorMousePosition, backgroundPicker.selectBounds)) {
                char selectedBackgroundPath[RPG_LAYOUT_BACKGROUND_PATH_LENGTH] = { 0 };
                FileDialogRequest backgroundDialog = MakeSpriteFileDialogRequest("背景PNGを選択");
                if (FileDialog_Select(selectedBackgroundPath, sizeof(selectedBackgroundPath), &backgroundDialog)) {
                    if (RpgStageBackground_Load(&stageBackground, selectedBackgroundPath)) {
                        snprintf(layout.backgroundPath, sizeof(layout.backgroundPath), "%s", selectedBackgroundPath);
                        message = "Background PNG loaded";
                    } else message = "Background PNG load failed";
                }
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, clearY, 188, 28 })) {
                layout.backgroundPath[0] = '\0';
                RpgStageBackground_Load(&stageBackground, layout.backgroundPath);
                message = "Background cleared";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, backgroundControlY, 38, 24 })) {
                layout.backgroundBrightness = Clamp(layout.backgroundBrightness - 0.05f, 0.15f, 1.0f);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, backgroundControlY, 38, 24 })) {
                layout.backgroundBrightness = Clamp(layout.backgroundBrightness + 0.05f, 0.15f, 1.0f);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, backgroundControlY + 40.0f, 38, 24 })) {
                layout.blockBrightness = Clamp(layout.blockBrightness - 0.05f, 0.15f, 1.0f);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, backgroundControlY + 40.0f, 38, 24 })) {
                layout.blockBrightness = Clamp(layout.blockBrightness + 0.05f, 0.15f, 1.0f);
                }
            }
            if (tabIndex < 0 && stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_GROUND_BLOCK]) {
                float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                   RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_GROUND_BLOCK);
                RpgEditorFilePickerLayout groundPicker = { y + 1.0f, { 716.0f, y + 12.0f, 188.0f, 18.0f },
                                                            { 716.0f, y + 32.0f, 188.0f, 28.0f }, 0.0f };
                groundPicker = ResolveInspectorFilePickerLayout(&groundPicker, layout.groundBlockPath);
                float clearY = groundPicker.contentBottom + 8.0f;
                if (CheckCollisionPointRec(inspectorMousePosition, groundPicker.selectBounds)) {
                char selectedGroundPath[RPG_LAYOUT_BACKGROUND_PATH_LENGTH] = { 0 };
                FileDialogRequest groundDialog = MakeSpriteFileDialogRequest("地面PNGを選択");
                if (FileDialog_Select(selectedGroundPath, sizeof(selectedGroundPath), &groundDialog)) {
                    if (RpgStageGroundTexture_Load(&groundBlockTexture, selectedGroundPath)) {
                        snprintf(layout.groundBlockPath, sizeof(layout.groundBlockPath), "%s", selectedGroundPath);
                        RpgStage_SetGroundTexture(groundBlockTexture.texture);
                        RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation,
                                                     layout.groundLightness);
                        message = "Ground PNG loaded";
                    } else message = "Ground PNG load failed";
                }
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, clearY, 188, 28 })) {
                layout.groundBlockPath[0] = '\0';
                RpgStageGroundTexture_Load(&groundBlockTexture, layout.groundBlockPath);
                RpgStage_SetGroundTexture(groundBlockTexture.texture);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation,
                                             layout.groundLightness);
                message = "Ground PNG cleared";
                }
            }
            if (tabIndex < 0 && stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_GROUND_TONE]) {
                float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                   RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_GROUND_TONE);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, y, 38, 24 })) {
                layout.groundHue = Clamp(layout.groundHue - 0.05f, 0.0f, 1.0f);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation, layout.groundLightness);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, y, 38, 24 })) {
                layout.groundHue = Clamp(layout.groundHue + 0.05f, 0.0f, 1.0f);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation, layout.groundLightness);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, y + 38, 38, 24 })) {
                layout.groundSaturation = Clamp(layout.groundSaturation - 0.05f, 0.0f, 1.0f);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation, layout.groundLightness);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, y + 38, 38, 24 })) {
                layout.groundSaturation = Clamp(layout.groundSaturation + 0.05f, 0.0f, 1.0f);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation, layout.groundLightness);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, y + 76, 38, 24 })) {
                layout.groundLightness = Clamp(layout.groundLightness - 0.05f, 0.0f, 1.0f);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation, layout.groundLightness);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, y + 76, 38, 24 })) {
                layout.groundLightness = Clamp(layout.groundLightness + 0.05f, 0.0f, 1.0f);
                RpgStage_SetGroundAppearance(layout.groundHue, layout.groundSaturation, layout.groundLightness);
            }
            }
            if (targetStageNumber > 0 && LoadEditorStageState(targetStageNumber, &layout, &player, &npc,
                                                               &stage, &items, &dialogue, &stage3Event)) {
                currentStageNumber = targetStageNumber;
                RpgStageCatalog_Select(&stageCatalogData, currentStageNumber);
                savedItems = items;
                savedMapEvents = mapEvents; savedWires = wires; savedReceivers = receivers;
                savedAttachments = attachments; savedSignalBlocks = signalBlocks;
                UpdateSaveSnapshot(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event);
                mapIndex = RpgStage_FindNearestActiveMap(&stage, 0);
                selected = RPG_EDITOR_STAGE_SETTINGS_INSPECTOR;
                message = TextFormat("Stage%d selected", currentStageNumber);
            }
        } else if (isAreaInspectorButtonClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isAreaInspectorOpen = !isAreaInspectorOpen;
            isAreaInspectorPointerHeld = true;
            isGlobalSettingsOpen = false;
            isStageSettingsOpen = false;
            if (isAreaInspectorOpen) selected = RPG_EDITOR_AREA_SETTINGS_INSPECTOR;
            else if (selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR) selected = 0;
        } else if (isAreaInspectorPanelClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isAreaInspectorPointerHeld = true;
            if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 178, 188, 26 })) {
                int deletedGridX = stage.mapGridX[mapIndex];
                int deletedGridY = stage.mapGridY[mapIndex];
                if (RpgStage_RemoveMap(&stage, mapIndex)) {
                    RemoveMapOwnedObjects(mapIndex, &items, &mapEvents, &stage, &wires,
                                          &receivers, &attachments);
                    areaEntryEvents.entries[mapIndex] = RpgStage3Event_Default();
                    mapIndex = RpgStage_FindNearestActiveMapAtGrid(&stage, deletedGridX, deletedGridY);
                    selected = 0;
                    isAreaInspectorOpen = false;
                    message = "Area deleted";
                } else message = "Cannot delete the last area";
            } else {
                RpgEditorInspectorActionRow entryRow = MakeInspectorActionRow(716.0f, 212.0f, 188.0f, 24.0f);
                Rectangle editBounds = { 716.0f, entryRow.contentBottom + 28.0f, 188.0f, 26.0f };
                if (CheckCollisionPointRec(inspectorMousePosition, entryRow.actionBounds)) {
                RpgStage3Event *areaEvent = &areaEntryEvents.entries[mapIndex];
                areaEvent->inspect.enabled = !areaEvent->inspect.enabled;
                areaEvent->enabled = areaEvent->inspect.enabled;
                message = areaEvent->inspect.enabled ? "Area entry event enabled" : "Area entry event disabled";
                } else if (CheckCollisionPointRec(inspectorMousePosition, editBounds)) {
                activeInspect = &areaEntryEvents.entries[mapIndex].inspect;
                inspectFunctionIndex = 0;
                modalHistory.count = 0;
                isExamineFunctionListOpen = true;
                isFunctionTypeListOpen = false;
                isMoveFunctionEditorOpen = false;
                isDialogueEditorOpen = false;
                isStage3DialogueEditing = false;
                isAreaEntryDialogueEditing = false;
                entryDialogueAreaIndex = -1;
                isInspectDialogueEditing = false;
                activeDialogueLine = -1;
                dialogueScroll = 0;
                message = "Area entry functions opened";
                }
            }
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isGlobalSettingsPointerHeld = false;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isStageSettingsPointerHeld = false;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isAreaInspectorPointerHeld = false;
        bool isRevertSavedClicked = !blockMode && !wasModalOpenAtFrameStart &&
                                    CheckCollisionPointRec(mousePosition, revertSavedBounds);
        int clickedBlockType = 0;
        bool isBlockInventoryControlClicked = false;
        bool isPaletteListTemporarilyExpanded = blockMode &&
            (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
        bool isPaletteInspectorOpen = selected == RPG_EDITOR_PALETTE_INSPECTOR &&
                                      selectedPaletteInspectorIndex >= 0 &&
                                      selectedPaletteInspectorIndex < RpgBlockInventory_Count();
        bool isPaletteListExpanded = isBlockInventoryListOpen || isPaletteListTemporarilyExpanded ||
                                     isPaletteInspectorOpen;
        bool isPaletteIconPointer = false;
        bool isPaletteReorderHandlePointer = false;
        if (blockMode && isPaletteListExpanded && !isGlobalMapOpen) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                for (int paletteIndex = 0; paletteIndex < RpgBlockInventory_Count(); paletteIndex++) {
                    if (!CheckCollisionPointRec(mousePosition,
                                                GetBlockInventoryReorderHandleBounds(paletteIndex))) continue;
                    paletteRowDragSource = paletteIndex;
                    RpgEditorDrag_Begin(&paletteRowDrag, mousePosition);
                    isPaletteReorderHandlePointer = true;
                    isBlockInventoryControlClicked = true;
                    break;
                }
                if (!isPaletteReorderHandlePointer) {
                    for (int paletteIndex = 0; paletteIndex < RpgBlockInventory_Count() &&
                         !isPaletteIconPointer; paletteIndex++) {
                        const RpgBlockInventory *palette = RpgBlockInventory_Get(paletteIndex);
                        for (int slot = 0; slot < palette->count; slot++) {
                            if (!CheckCollisionPointRec(mousePosition,
                                                        GetBlockInventoryPreviewCellBounds(paletteIndex, slot))) continue;
                            paletteBlockDragSourcePalette = paletteIndex;
                            paletteBlockDragSourceSlot = slot;
                            RpgEditorDrag_Begin(&paletteBlockDrag, mousePosition);
                            isPaletteIconPointer = true;
                            isBlockInventoryControlClicked = true;
                            break;
                        }
                    }
                }
            }
            if (paletteRowDragSource >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                (void)RpgEditorDrag_Update(&paletteRowDrag, mousePosition);
            if (paletteRowDragSource >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                int sourcePalette = paletteRowDragSource;
                bool moved = false;
                if (paletteRowDrag.active) {
                    for (int paletteIndex = 0; paletteIndex < RpgBlockInventory_Count(); paletteIndex++) {
                        if (!CheckCollisionPointRec(mousePosition,
                                                    GetBlockInventoryListItem(paletteIndex))) continue;
                        int finalIndex = RpgBlockInventory_MovePalette(sourcePalette, paletteIndex);
                        if (finalIndex >= 0) {
                            selectedBlockInventory = RemapBlockInventoryIndexAfterMove(
                                selectedBlockInventory, sourcePalette, finalIndex);
                            selectedPaletteInspectorIndex = RemapBlockInventoryIndexAfterMove(
                                selectedPaletteInspectorIndex, sourcePalette, finalIndex);
                            paletteNameEditingIndex = RemapBlockInventoryIndexAfterMove(
                                paletteNameEditingIndex, sourcePalette, finalIndex);
                            moved = finalIndex != sourcePalette;
                        }
                        break;
                    }
                    if (moved) {
                        if (!SaveBlockInventoryPreferences()) message = "Palette save failed";
                        else message = "Palette order changed";
                    } else message = "Drop onto a palette row";
                }
                paletteRowDragSource = -1;
                RpgEditorDrag_End(&paletteRowDrag);
                isBlockInventoryControlClicked = true;
            }
            if (paletteRowDragSource < 0 && paletteBlockDragSourcePalette >= 0 &&
                IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                (void)RpgEditorDrag_Update(&paletteBlockDrag, mousePosition);
            if (paletteRowDragSource < 0 && paletteBlockDragSourcePalette >= 0 &&
                IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                int sourcePalette = paletteBlockDragSourcePalette;
                int sourceSlot = paletteBlockDragSourceSlot;
                const RpgBlockInventory *source = RpgBlockInventory_Get(sourcePalette);
                int sourceType = sourceSlot >= 0 && sourceSlot < source->count ?
                                 source->blockTypes[sourceSlot] : 0;
                bool moved = false;
                if (paletteBlockDrag.active) {
                    for (int paletteIndex = 0; paletteIndex < RpgBlockInventory_Count(); paletteIndex++) {
                        Rectangle row = GetBlockInventoryListItem(paletteIndex);
                        if (!CheckCollisionPointRec(mousePosition, row) || mousePosition.x < 305.0f) continue;
                        const RpgBlockInventory *destination = RpgBlockInventory_Get(paletteIndex);
                        int destinationSlot = (int)((mousePosition.x - 305.0f) / 30.0f);
                        destinationSlot = Clamp(destinationSlot, 0, destination->count);
                        moved = RpgBlockInventory_MoveBlock(sourcePalette, sourceSlot,
                                                            paletteIndex, destinationSlot);
                        if (moved) {
                            selectedBlockInventory = paletteIndex;
                            selectedBlockType = sourceType;
                        }
                        break;
                    }
                    if (moved) {
                        if (!SaveBlockInventoryPreferences()) message = "Palette save failed";
                        else message = "Block moved in palette";
                    } else message = "Drop onto a palette row";
                } else if (sourceType > 0) {
                    selectedBlockInventory = sourcePalette;
                    clickedBlockType = sourceType;
                }
                paletteBlockDragSourcePalette = -1;
                paletteBlockDragSourceSlot = -1;
                RpgEditorDrag_End(&paletteBlockDrag);
                isBlockInventoryControlClicked = true;
            }
        }
        /* Palette names are edited only from the dedicated inspector.  The
           compact palette list remains a selector, so it cannot accidentally
           consume normal block-mode placement input. */
        if (paletteNameEditingIndex >= 0 &&
            (selected != RPG_EDITOR_PALETTE_INSPECTOR ||
             paletteNameEditingIndex != selectedPaletteInspectorIndex)) {
            paletteNameEditingIndex = -1;
        }
        if (blockMode && !isGlobalMapOpen && !globalMapInteractionThisFrame && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
                selectedBlockType = GetBlockInventoryFirstBlockType(RpgBlockInventory_Get(selectedBlockInventory));
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 254, 486, 30, 48 })) {
                isBlockInventoryControlClicked = true;
                selectedBlockInventory = (selectedBlockInventory + 1) % RpgBlockInventory_Count();
                selectedBlockType = GetBlockInventoryFirstBlockType(RpgBlockInventory_Get(selectedBlockInventory));
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 54, 486, 200, 48 })) {
                isBlockInventoryControlClicked = true;
                isBlockInventoryListOpen = !isBlockInventoryListOpen;
            } else if (isPaletteListExpanded &&
                       CheckCollisionPointRec(mousePosition, GetBlockInventoryAddButtonBounds())) {
                int addedPalette = RpgBlockInventory_Add();
                isBlockInventoryControlClicked = true;
                if (addedPalette >= 0) {
                    selectedBlockInventory = addedPalette;
                    selectedPaletteInspectorIndex = addedPalette;
                    selected = RPG_EDITOR_PALETTE_INSPECTOR;
                    if (!SaveBlockInventoryPreferences()) message = "Palette save failed";
                    else message = "Palette added";
                } else message = "Palette limit reached";
            } else if (isPaletteListExpanded &&
                       CheckCollisionPointRec(mousePosition, GetBlockInventoryDeleteButtonBounds())) {
                int deletedPalette = selectedBlockInventory;
                isBlockInventoryControlClicked = true;
                if (RpgBlockInventory_Remove(deletedPalette)) {
                    selectedBlockInventory = Clamp(deletedPalette, 0, RpgBlockInventory_Count() - 1);
                    selectedBlockType = GetBlockInventoryFirstBlockType(
                        RpgBlockInventory_Get(selectedBlockInventory));
                    if (selectedPaletteInspectorIndex == deletedPalette) {
                        selected = 0;
                        selectedPaletteInspectorIndex = -1;
                    } else if (selectedPaletteInspectorIndex > deletedPalette) {
                        selectedPaletteInspectorIndex--;
                    }
                    paletteNameEditingIndex = -1;
                    if (!SaveBlockInventoryPreferences()) message = "Palette save failed";
                    else message = "Palette deleted";
                } else message = "Keep at least one palette";
            } else if (isPaletteListExpanded && !isPaletteIconPointer) {
                for (int index = 0; index < RpgBlockInventory_Count(); index++) {
                    const RpgBlockInventory *listedInventory = RpgBlockInventory_Get(index);
                    if (CheckCollisionPointRec(mousePosition, GetBlockInventoryEditButtonBounds(index))) {
                        selectedBlockInventory = index;
                        selectedBlockType = GetBlockInventoryFirstBlockType(listedInventory);
                        selectedPaletteInspectorIndex = index;
                        paletteNameEditingIndex = -1;
                        selected = RPG_EDITOR_PALETTE_INSPECTOR;
                        /* The expanded list is only a selector.  Closing this
                           inspector must restore the compact palette bar unless
                           Shift is currently held. */
                        isBlockInventoryListOpen = false;
                        isBlockInventoryControlClicked = true;
                        break;
                    }
                    if (CheckCollisionPointRec(mousePosition, GetBlockInventoryListItem(index))) {
                        selectedBlockInventory = index;
                        selectedBlockType = GetBlockInventoryFirstBlockType(listedInventory);
                        isBlockInventoryListOpen = false;
                        isBlockInventoryControlClicked = true;
                        break;
                    }
                }
            }
        }
        if (selected == RPG_EDITOR_PALETTE_INSPECTOR &&
            selectedPaletteInspectorIndex >= 0 &&
            selectedPaletteInspectorIndex < RpgBlockInventory_Count()) {
            RpgBlockInventory *paletteInspector =
                RpgBlockInventory_GetMutable(selectedPaletteInspectorIndex);
            Rectangle paletteNameBounds = { 716.0f, 162.0f, 188.0f, 28.0f };
            bool paletteNameClicked = isPaletteInspectorClicked &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(inspectorMousePosition, paletteNameBounds);
            if (paletteNameClicked && paletteInspector != NULL) {
                paletteNameEditingIndex = selectedPaletteInspectorIndex;
                RpgEditorText_BeginPointerSelection(paletteInspector->name,
                                                     inspectorMousePosition.x - paletteNameBounds.x - 8.0f, 15.0f,
                                                     &paletteNameCursorIndex, &paletteNameSelectionAnchor,
                                                     &paletteNameSelectionEnd);
            }
            if (paletteNameEditingIndex == selectedPaletteInspectorIndex && paletteInspector != NULL) {
                UpdateShortText(paletteInspector->name, sizeof(paletteInspector->name),
                                &paletteNameCursorIndex, &paletteNameSelectionAnchor,
                                &paletteNameSelectionEnd);
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(inspectorMousePosition, paletteNameBounds))
                    RpgEditorText_UpdatePointerSelection(paletteInspector->name,
                                                          inspectorMousePosition.x - paletteNameBounds.x - 8.0f, 15.0f,
                                                          &paletteNameCursorIndex, &paletteNameSelectionEnd);
                /* Reuse the same native IME placement path as the existing
                   inspector name fields, so Japanese input remains usable. */
                UpdateImeCandidateWindowAt(724 + (int)inspectorOffsets[RPG_EDITOR_PALETTE_INSPECTOR].x,
                                           190 + (int)inspectorOffsets[RPG_EDITOR_PALETTE_INSPECTOR].y);
                bool finishNameEdit = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) ||
                    RpgEditorText_ShouldEndEditingOnOutsideClick(paletteNameBounds,
                                                                  inspectorMousePosition);
                if (finishNameEdit) {
                    if (paletteInspector->name[0] == '\0')
                        snprintf(paletteInspector->name, sizeof(paletteInspector->name), "Palette %d",
                                 selectedPaletteInspectorIndex + 1);
                    if (!SaveBlockInventoryPreferences()) message = "Palette save failed";
                    else message = "Palette updated";
                    paletteNameEditingIndex = -1;
                }
            }
            if (isPaletteInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                for (int colorIndex = 0; colorIndex < RPG_BLOCK_INVENTORY_BORDER_COLOR_COUNT; colorIndex++) {
                    Rectangle colorBounds = { 716.0f + colorIndex * 47.0f, 224.0f, 43.0f, 26.0f };
                    if (CheckCollisionPointRec(inspectorMousePosition, colorBounds)) {
                        if (RpgBlockInventory_SetBorderColor(selectedPaletteInspectorIndex,
                                                             (RpgBlockInventoryBorderColor)colorIndex)) {
                            if (!SaveBlockInventoryPreferences()) message = "Palette save failed";
                            else message = "Palette border updated";
                        }
                        break;
                    }
                }
            }
        }
        if (clickedBlockType > 0) {
            selectedBlockType = clickedBlockType;
            if (selectedBlockType == RPG_BLOCK_REFERENCE_FILE) {
                message = "FILE.png selected";
            } else if (selectedBlockType == RPG_BLOCK_REFERENCE_FOLDER) {
                message = "Folder selected";
            } else if (selectedBlockType == RPG_BLOCK_IMAGE_OBJECT) {
                message = "Image object selected";
            } else message = TextFormat("Block %d selected", selectedBlockType);
        }
        RpgReferenceObject *selectedReferenceObject =
            selectedReferenceObjectIndex >= 0 && selectedReferenceObjectIndex < stage.referenceObjects.count ?
            &stage.referenceObjects.entries[selectedReferenceObjectIndex] : NULL;
        bool isReferenceFolderSelected = selectedReferenceObject != NULL &&
                                         selectedReferenceObject->objectKind == RPG_REFERENCE_OBJECT_FOLDER;
        RpgEditorFolderInspectorLayout referenceFolderLayout = GetFolderInspectorLayout();
        Rectangle referencePathBounds = isReferenceFolderSelected ? referenceFolderLayout.nameBounds :
                                        (Rectangle){ 716.0f, 156.0f, 188.0f, 28.0f };
        RpgEditorFilePickerLayout referencePickerLayout = { 142.0f, { 716.0f, 156.0f, 188.0f, 28.0f },
                                                              { 716.0f, 192.0f, 188.0f, 28.0f }, 0.0f };
        if (!isReferenceFolderSelected && selectedReferenceObject != NULL) {
            const char *referenceDisplay = selectedReferenceObject->sourcePath[0] != '\0' ?
                selectedReferenceObject->sourcePath : GetReferenceRelativeDisplayPath(selectedReferenceObject->path);
            referencePickerLayout = ResolveInspectorFilePickerLayout(&referencePickerLayout, referenceDisplay);
        }
        Rectangle referenceSelectBounds = isReferenceFolderSelected ? referenceFolderLayout.openBounds :
                                        referencePickerLayout.selectBounds;
        bool isReferencePathClicked = isReferenceInspectorClicked && isReferenceFolderSelected &&
                                     CheckCollisionPointRec(inspectorMousePosition, referencePathBounds);
        bool isReferenceFileSelectClicked = isReferenceInspectorClicked &&
                                            CheckCollisionPointRec(inspectorMousePosition, referenceSelectBounds);
        bool isReferenceFolderRenameClicked = isReferenceInspectorClicked && isReferenceFolderSelected &&
                                              CheckCollisionPointRec(inspectorMousePosition,
                                                                     referenceFolderLayout.renameBounds);
        const RpgImageObject *inspectorImageObject =
            selectedImageObjectIndex >= 0 && selectedImageObjectIndex < stage.imageObjects.count ?
            &stage.imageObjects.entries[selectedImageObjectIndex] : NULL;
        RpgEditorFilePickerLayout imagePickerLayout = GetImageInspectorFilePickerLayout(inspectorImageObject);
        float imageControlsY = GetImageInspectorControlsTop(inspectorImageObject);
        bool isImagePngSelectClicked = isImageObjectInspectorClicked &&
                                       CheckCollisionPointRec(inspectorMousePosition, imagePickerLayout.selectBounds);
        bool isImageScaleDownClicked = isImageObjectInspectorClicked &&
                                        CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816.0f, imageControlsY, 32.0f, 26.0f });
        bool isImageScaleUpClicked = isImageObjectInspectorClicked &&
                                      CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 854.0f, imageControlsY, 50.0f, 26.0f });
        bool isImageLayerBackClicked = isImageObjectInspectorClicked &&
                                       CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716.0f, imageControlsY + 56.0f, 60.0f, 28.0f });
        bool isImageLayerMiddleClicked = isImageObjectInspectorClicked &&
                                         CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 780.0f, imageControlsY + 56.0f, 60.0f, 28.0f });
        bool isImageLayerFrontClicked = isImageObjectInspectorClicked &&
                                       CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 844.0f, imageControlsY + 56.0f, 60.0f, 28.0f });
        bool isImageAppearancePngClicked = isImageObjectInspectorClicked &&
                                           CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716.0f, imageControlsY + 124.0f, 58.0f, 26.0f });
        bool isImageAppearanceFolderClicked = isImageObjectInspectorClicked &&
                                              CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 778.0f, imageControlsY + 124.0f, 62.0f, 26.0f });
        bool isImageAppearanceFileClicked = isImageObjectInspectorClicked &&
                                            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 844.0f, imageControlsY + 124.0f, 60.0f, 26.0f });
        if (isReferencePathClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const char *path = isReferenceFolderSelected ? referenceFolderNameInput :
                               selectedReferenceObject->path;
            RpgEditorText_BeginPointerSelection(path, inspectorMousePosition.x - 724.0f, 15.0f,
                                                 &referencePathCursorIndex, &referencePathSelectionAnchor,
                                                 &referencePathSelectionEnd);
            isReferencePathEditing = true;
            isReferencePathPointerHeld = true;
        }
        if (!isReferenceFolderSelected) isReferencePathEditing = false;
        if (isReferencePathEditing && selected == 7 && selectedReferenceObject != NULL) {
            char *path = isReferenceFolderSelected ? referenceFolderNameInput :
                          selectedReferenceObject->path;
            UpdateShortText(path, isReferenceFolderSelected ? sizeof(referenceFolderNameInput) :
                            sizeof(selectedReferenceObject->path), &referencePathCursorIndex,
                            &referencePathSelectionAnchor, &referencePathSelectionEnd);
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isReferencePathPointerHeld) {
                RpgEditorText_UpdatePointerSelection(path, inspectorMousePosition.x - 724.0f, 15.0f,
                                                      &referencePathCursorIndex, &referencePathSelectionEnd);
            }
            if (RpgEditorText_ShouldEndEditingOnOutsideClick(referencePathBounds,
                                                             inspectorMousePosition))
                isReferencePathEditing = false;
            UpdateImeCandidateWindowAt(724 + (int)inspectorOffsets[7].x,
                                       (isReferenceFolderSelected ? 172 : 184) + (int)inspectorOffsets[7].y);
        }
        if (isReferenceFileSelectClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selectedReferenceObject != NULL) {
            if (isReferenceFolderSelected) {
                message = RpgExplorerLauncher_OpenDirectory(selectedReferenceObject->path) ? "Folder opened" : "Folder open failed";
            } else {
            char selectedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            char resolvedSourcePath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            char copiedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            char previousCopiedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            FileDialogRequest referenceDialog = MakeFilesFileDialogRequest("ファイルを選択");
            // プロジェクトで扱う外部ファイルを集約したFilesフォルダを、選択画面の初期位置にする。
            if (FileDialog_Select(selectedPath, sizeof(selectedPath), &referenceDialog) &&
                FileDialog_ResolvePath(&referenceDialog, selectedPath, resolvedSourcePath,
                                       sizeof(resolvedSourcePath))) {
                /* 外部パスはステージに保存せず、種類を問わずbuild配下のマス専用コピーへ差し替える。 */
                int referenceRow = -1, referenceColumn = -1;
                snprintf(previousCopiedPath, sizeof(previousCopiedPath), "%s", selectedReferenceObject->path);
                if (RpgStage_GetWorldCellAtPosition(&stage, selectedReferenceObject->position,
                                                    &referenceRow, &referenceColumn) &&
                    RpgStageStorage_CopyReferenceFileToBuild(currentStageNumber, referenceRow,
                                                             referenceColumn, resolvedSourcePath, copiedPath,
                                                             (int)sizeof(copiedPath))) {
                    snprintf(selectedReferenceObject->path, sizeof(selectedReferenceObject->path), "%s", copiedPath);
                    /* Keep the real saved picker value.  The build copy above
                       remains an implementation detail used by runtime file
                       operations and must not replace this relative path. */
                    snprintf(selectedReferenceObject->sourcePath,
                             sizeof(selectedReferenceObject->sourcePath), "%s", selectedPath);
                    if (strcmp(previousCopiedPath, copiedPath) != 0)
                        RpgStageStorage_RemoveReferenceFileCopy(currentStageNumber, previousCopiedPath);
                    referencePathCursorIndex = (int)strlen(selectedPath);
                    referencePathSelectionAnchor = referencePathCursorIndex;
                    referencePathSelectionEnd = referencePathCursorIndex;
                    isReferencePathEditing = false;
                    GameFont_AddText(selectedPath);
                    message = "File copied to build";
                } else message = "File copy failed";
            } else message = "File selection cancelled";
            }
        }
        if (isReferenceFolderRenameClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            char renamedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            const char *oldPath = selectedReferenceObject != NULL ? selectedReferenceObject->path : "";
            if (RpgStageStorage_RenameBuildFolder(currentStageNumber, oldPath, referenceFolderNameInput,
                                                  renamedPath, (int)sizeof(renamedPath)) &&
                selectedReferenceObject != NULL) {
                snprintf(selectedReferenceObject->path, sizeof(selectedReferenceObject->path), "%s", renamedPath);
                isReferencePathEditing = false;
                message = "Folder renamed";
            } else message = "Folder rename failed";
        }
        if (selected == RPG_EDITOR_IMAGE_INSPECTOR && selectedImageObjectIndex >= 0 &&
            selectedImageObjectIndex < stage.imageObjects.count) {
            RpgImageObject *imageObject = &stage.imageObjects.entries[selectedImageObjectIndex];
            if (isImagePngSelectClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                char selectedPath[RPG_IMAGE_OBJECT_PATH_LENGTH] = { 0 };
                FileDialogRequest imageDialog = MakeSpriteFileDialogRequest("画像PNGを選択");
                if (FileDialog_Select(selectedPath, sizeof(selectedPath), &imageDialog)) {
                    snprintf(imageObject->path, sizeof(imageObject->path), "%s", selectedPath);
                    GameFont_AddText(selectedPath);
                    message = "Image PNG selected";
                } else message = "PNG selection cancelled";
            }
            if (isImageScaleDownClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->scale = Clamp(imageObject->scale - 0.25f, 0.25f, 8.0f);
                message = "Image scale changed";
            }
            if (isImageScaleUpClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->scale = Clamp(imageObject->scale + 0.25f, 0.25f, 8.0f);
                message = "Image scale changed";
            }
            if (isImageLayerBackClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->layer = RPG_IMAGE_OBJECT_LAYER_BACK;
                message = "Image layer: back";
            }
            if (isImageLayerMiddleClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->layer = RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK;
                message = "Image layer: block front / character back";
            }
            if (isImageLayerFrontClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->layer = RPG_IMAGE_OBJECT_LAYER_FRONT;
                message = "Image layer: front";
            }
            if (isImageAppearancePngClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->appearance = RPG_IMAGE_OBJECT_APPEARANCE_PNG;
                message = "Image appearance: PNG";
            }
            if (isImageAppearanceFolderClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->appearance = RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FOLDER;
                message = "Image appearance: Shell folder";
            }
            if (isImageAppearanceFileClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                imageObject->appearance = RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FILE;
                message = "Image appearance: Shell file";
            }
            if (!isKeyboardCaptured && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_D)) {
                int duplicate = RpgImageObjects_DuplicateRight(&stage.imageObjects, selectedImageObjectIndex,
                                                                RPG_STAGE_WORLD_COLUMNS);
                if (duplicate >= 0) { selectedImageObjectIndex = duplicate; message = "Image duplicated to the right"; }
                else message = "Image copy needs space on the right";
            }
        }
        /* Folder配置物は通常の選択状態からCtrl+Dで右隣へ複製し、見た目設定も同じ値を引き継ぐ。 */
        if (!isKeyboardCaptured && !isReferencePathEditing && selected == 7 && selectedReferenceObject != NULL &&
            selectedReferenceObject->objectKind == RPG_REFERENCE_OBJECT_FOLDER &&
            (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_D)) {
            int sourceRow = -1, sourceColumn = -1;
            char copyFolderName[64];
            char copyFolderPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
            if (!RpgStage_GetWorldCellAtPosition(&stage, selectedReferenceObject->position,
                                                 &sourceRow, &sourceColumn) ||
                sourceColumn + 1 >= RPG_STAGE_WORLD_COLUMNS) {
                message = "Folder copy needs a cell on the right";
            } else {
                int copyColumn = sourceColumn + 1;
                snprintf(copyFolderName, sizeof(copyFolderName), "Folder_%d_%d", sourceRow + 1, copyColumn + 1);
                if (RpgStageStorage_CreateBuildFolder(currentStageNumber, copyFolderName, copyFolderPath,
                                                      (int)sizeof(copyFolderPath))) {
                    if (RpgReferenceObjects_Add(&stage.referenceObjects, RPG_REFERENCE_OBJECT_FOLDER,
                                                RpgStage_GetWorldPositionForCell(&stage, sourceRow, copyColumn),
                                                copyFolderPath, 0)) {
                        selectedReferenceObjectIndex = stage.referenceObjects.count - 1;
                        snprintf(referenceFolderNameInput, sizeof(referenceFolderNameInput), "%s", copyFolderName);
                        blockEditedThisFrame = true;
                        message = "Folder copied to the right";
                    } else message = "Reference object limit reached";
                } else message = "Folder copy failed";
            }
        }
        if (isBlockInventoryControlClicked) isBlockInventoryPointerHeld = true;
        if (isRevertSavedClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                  &items, &savedItems) || RpgStageCatalog_IsDirty(&stageCatalogData))) {
            int previousGridX = stage.mapGridX[mapIndex];
            int previousGridY = stage.mapGridY[mapIndex];
            RevertToSavedSnapshot(&savedSnapshot, &layout, &player, &npc, &stage, &dialogue, &stage3Event);
            RpgStageCatalog_Revert(&stageCatalogData);
            if (currentStageNumber != RpgStageCatalog_GetCurrentNumber(&stageCatalogData)) {
                currentStageNumber = RpgStageCatalog_GetCurrentNumber(&stageCatalogData);
                LoadEditorStageState(currentStageNumber, &layout, &player, &npc, &stage, &items,
                                     &dialogue, &stage3Event);
                savedItems = items; savedMapEvents = mapEvents; savedWires = wires;
                savedReceivers = receivers; savedAttachments = attachments; savedSignalBlocks = signalBlocks;
                UpdateSaveSnapshot(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event);
            }
            mapIndex = RpgStage_FindNearestActiveMapAtGrid(&stage, previousGridX, previousGridY);
            items = savedItems;
            signalBlocks = savedSignalBlocks;
            if (selectedItemIndex >= items.count) {
                selected = 0;
                selectedItemIndex = -1;
                isItemNameEditing = false;
            }
            message = "Reverted to saved";
        }
        if (isDialogueEditorOpen && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int maxScroll = editedDialogue->lineCount > visibleDialogueLines ? editedDialogue->lineCount - visibleDialogueLines : 0;
            dialogueScroll -= (int)GetMouseWheelMove();
            if (dialogueScroll < 0) dialogueScroll = 0;
            if (dialogueScroll > maxScroll) dialogueScroll = maxScroll;
        }
        if (isMoveFunctionEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgInspectMove *move = &npcInspect.functions[inspectFunctionIndex].move;
            if (isMoveTargetPicking) {
                /* 対象選択中は設定パネルを描画・判定ともに隠し、マップだけがクリックを受け取る。 */
                if (CheckCollisionPointRec(mousePosition, GetEditorMapContentBounds())) {
                    int imageIndex = -1;
                    EditorMapObjectHit targetHit = GetTopmostEditableObject(&stage, &player, &npc, &zipperData,
                                                                             mapIndex, mapMousePosition, &imageIndex);
                    bool targetWasPicked = false;
                    /* 通常選択と同じ描画順のヒット判定を使い、重なり時も最前面だけを選ぶ。 */
                    if (targetHit == EDITOR_MAP_OBJECT_HIT_PLAYER) {
                        move->target = RPG_INSPECT_MOVE_PLAYER;
                        targetWasPicked = true;
                    } else if (targetHit == EDITOR_MAP_OBJECT_HIT_NPC) {
                        move->target = RPG_INSPECT_MOVE_NPC;
                        targetWasPicked = true;
                    } else if (targetHit == EDITOR_MAP_OBJECT_HIT_IMAGE && imageIndex >= 0) {
                        move->target = RPG_INSPECT_MOVE_IMAGE_OBJECT;
                        move->targetImageObjectId = stage.imageObjects.entries[imageIndex].id;
                        targetWasPicked = true;
                    } else {
                        message = "Click Player, NPC, or Image Object";
                    }
                    if (targetWasPicked) {
                        isMoveTargetPicking = false;
                        message = TextFormat("Move target: %s", GetMoveTargetName(move));
                    }
                }
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(250, 10, 20, 20))) {
                isMoveTargetPicking = false;
                isMoveEasingListOpen = false;
                if (isMovePreviewPlaying) {
                    isMovePreviewPlaying = false;
                }
                isMoveFunctionEditorOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 74, 244, 28))) {
                isMoveTargetPicking = true;
                isMoveEasingListOpen = false;
                message = "Click Player, NPC, or Image Object";
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 136, 78, 24))) {
                SetMoveAxis(move, RPG_INSPECT_MOVE_AXIS_X, &player, &npc, &zipperData, &stage);
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(98, 136, 78, 24))) {
                SetMoveAxis(move, RPG_INSPECT_MOVE_AXIS_Y, &player, &npc, &zipperData, &stage);
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(180, 136, 78, 24))) {
                SetMoveAxis(move, RPG_INSPECT_MOVE_AXIS_XY, &player, &npc, &zipperData, &stage);
            } else if (move->target == RPG_INSPECT_MOVE_PLAYER &&
                       CheckCollisionPointRec(mousePosition, GetMovePanelControl(66, 164, 62, 24))) {
                move->walkAnimationEnabled = !move->walkAnimationEnabled;
                message = move->walkAnimationEnabled ? "Walk animation enabled" : "Walk animation disabled";
            } else if (move->target == RPG_INSPECT_MOVE_PLAYER &&
                       CheckCollisionPointRec(mousePosition, GetMovePanelControl(194, 164, 26, 24))) {
                move->walkAnimationSpeed = fmaxf(0.1f, move->walkAnimationSpeed - 0.1f);
            } else if (move->target == RPG_INSPECT_MOVE_PLAYER &&
                       CheckCollisionPointRec(mousePosition, GetMovePanelControl(230, 164, 26, 24))) {
                move->walkAnimationSpeed = fminf(8.0f, move->walkAnimationSpeed + 0.1f);
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 228, 244, 26))) {
                isMoveEasingListOpen = !isMoveEasingListOpen;
            } else if (isMoveEasingListOpen) {
                bool selectedEasing = false;
                for (int index = 0; index < RPG_INSPECT_EASING_COUNT; index++) {
                    if (!CheckCollisionPointRec(mousePosition, GetMoveEasingListItem(index))) continue;
                    RpgInspectMoveEasing selected = (RpgInspectMoveEasing)index;
                    /* 同じ項目を選び直したフレームでは通知を増やさない。 */
                    if (move->easing != selected) {
                        move->easing = selected;
                        message = "Move easing updated";
                    }
                    selectedEasing = true;
                    break;
                }
                /* 一覧のクリックはここで消費し、背後の操作へ伝播させずに閉じる。 */
                (void)selectedEasing;
                isMoveEasingListOpen = false;
            } else if (CheckCollisionPointRec(mousePosition, GetEditorMapContentBounds()) && !CheckCollisionPointRec(mousePosition, movePanelBounds)) {
                Vector2 destination = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                        mapMousePosition.y };
                destination = SnapMoveDestination(destination, move);
                if (RpgInspect_MoveAxisHasX(move->axis)) move->destinationX = destination.x;
                if (RpgInspect_MoveAxisHasY(move->axis)) move->destinationY = destination.y;
                message = "Move destination set";
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 366, 38, 28))) move->duration = Clamp(move->duration - 0.1f, 0.1f, 30.0f);
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(152, 366, 38, 28))) move->duration = Clamp(move->duration + 0.1f, 0.1f, 30.0f);
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 426, 38, 28))) move->nextFunctionDelay = Clamp(move->nextFunctionDelay - 0.1f, 0.0f, 30.0f);
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(152, 426, 38, 28))) move->nextFunctionDelay = Clamp(move->nextFunctionDelay + 0.1f, 0.0f, 30.0f);
            else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 462, 244, 22))) {
                if (isMovePreviewPlaying) {
                    isMovePreviewPlaying = false;
                    message = "Move preview stopped";
                } else {
                    movePreviewStartPosition = (Vector2){ GetMoveTargetWorldX(move, &player, &npc, &zipperData, &stage),
                                                          GetMoveTargetWorldY(move, &player, &npc, &zipperData, &stage) };
                    movePreviewSpritePosition = movePreviewStartPosition;
                    movePreviewElapsed = 0.0f;
                    isMovePreviewPlaying = true;
                    message = "Move preview playing";
                }
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 492, 116, 24))) {
                message = SaveActiveInspect(&stage, &savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(144, 492, 116, 24))) {
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
        } else if (isWaitFunctionEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgInspectWait *wait = &npcInspect.functions[inspectFunctionIndex].wait;
            if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(250, 10, 20, 20))) {
                isWaitFunctionEditorOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 98, 42, 30))) {
                wait->duration = fmaxf(0.0f, wait->duration - 0.1f);
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(186, 98, 42, 30))) {
                wait->duration += 0.1f;
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 492, 116, 24))) {
                message = SaveActiveInspect(&stage, &savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(144, 492, 116, 24))) {
                RevertActiveInspect(&savedSnapshot);
                isWaitFunctionEditorOpen = false;
                isExamineFunctionListOpen = true;
                message = "Reverted to saved";
            } else if (CheckCollisionPointRec(mousePosition, movePanelBounds)) {
                isDraggingMovePanel = true;
                movePanelDragOffset = (Vector2){ mousePosition.x - movePanelBounds.x, mousePosition.y - movePanelBounds.y };
            }
        } else if (isLayerChangeFunctionEditorOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgInspectLayerChange *change = &npcInspect.functions[inspectFunctionIndex].layerChange;
            if (isLayerChangeTargetPicking) {
                int imageIndex = -1;
                Vector2 mapMousePosition = GetEditorMapPointer(RpgViewport_GetMousePosition());
                EditorMapObjectHit targetHit = GetTopmostEditableObject(&stage, &player, &npc, &zipperData,
                                                                         mapIndex, mapMousePosition, &imageIndex);
                if (targetHit == EDITOR_MAP_OBJECT_HIT_IMAGE && imageIndex >= 0) {
                    change->targetImageObjectId = stage.imageObjects.entries[imageIndex].id;
                    isLayerChangeTargetPicking = false;
                    message = "Layer target selected";
                } else message = "Select the topmost image object";
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(250, 10, 20, 20))) {
                isLayerChangeFunctionEditorOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 90, 244, 28))) {
                isLayerChangeTargetPicking = true;
                message = "Click the topmost image object";
            } else {
                for (int layer = 0; layer < 3; layer++) {
                    if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 174 + layer * 34, 244, 28))) {
                        change->layer = layer;
                        message = "Draw layer changed";
                    }
                }
                if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(16, 492, 116, 24)))
                    message = SaveActiveInspect(&stage, &savedSnapshot) ? "Saved" : "Save failed";
                else if (CheckCollisionPointRec(mousePosition, GetMovePanelControl(144, 492, 116, 24))) {
                    RevertActiveInspect(&savedSnapshot);
                    isLayerChangeFunctionEditorOpen = false;
                    isExamineFunctionListOpen = true;
                    message = "Reverted to saved";
                }
            }
        } else if (isFunctionTypeListOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 620, 140, 22, 22 })) {
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
                    npcInspect.functions[index].move = (RpgInspectMove){ .target = RPG_INSPECT_MOVE_PLAYER,
                                                                           .destinationX = 0.0f, .destinationY = 0.0f,
                                                                           .duration = 1.0f, .nextFunctionDelay = 0.0f,
                                                                           .walkAnimationEnabled = true, .walkAnimationSpeed = 1.0f,
                                                                           .easing = RPG_INSPECT_EASING_LINEAR,
                                                                           .axis = RPG_INSPECT_MOVE_AXIS_X, .snapToGrid = true };
                    snprintf(npcInspect.functions[index].title, RPG_INSPECT_TITLE_LENGTH, "Move%d", index + 1);
                    inspectFunctionIndex = index;
                    message = "Move function added";
                } else message = "Function limit reached";
                isFunctionTypeListOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 324, 324, 312, 36 })) {
                int index = npcInspect.functionCount;
                if (index < RPG_INSPECT_MAX_FUNCTIONS) {
                    npcInspect.functionCount++;
                    npcInspect.functions[index].type = RPG_INSPECT_WAIT;
                    npcInspect.functions[index].wait.duration = 1.0f;
                    snprintf(npcInspect.functions[index].title, RPG_INSPECT_TITLE_LENGTH, "Wait%d", index + 1);
                    inspectFunctionIndex = index;
                    message = "Wait function added (1.0 sec)";
                } else message = "Function limit reached";
                isFunctionTypeListOpen = false;
                isExamineFunctionListOpen = ModalHistory_Pop(&modalHistory) == EDITOR_MODAL_EXAMINE_LIST;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 324, 368, 312, 36 })) {
                int index = npcInspect.functionCount;
                if (index < RPG_INSPECT_MAX_FUNCTIONS) {
                    npcInspect.functionCount++;
                    npcInspect.functions[index].type = RPG_INSPECT_LAYER_CHANGE;
                    npcInspect.functions[index].layerChange = (RpgInspectLayerChange){ 0, RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK };
                    snprintf(npcInspect.functions[index].title, RPG_INSPECT_TITLE_LENGTH, "Layer%d", index + 1);
                    inspectFunctionIndex = index;
                    message = "Layer change function added";
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
                RpgEditorText_BeginPointerSelection(npcInspect.functions[inspectFunctionIndex].title,
                                                     mousePosition.x - 268.0f, 16.0f,
                                                     &titleCursorIndex, &titleSelectionAnchor,
                                                     &titleSelectionEnd);
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 430, 394, 210, 30 }) &&
                       !isFunctionPreviewPlaying) {
                /* プレビューは編集データを一時的に動かし、終了時にスナップショットへ戻す。 */
                functionPreviewStageSnapshot = stage;
                functionPreviewPlayerSnapshot = player;
                functionPreviewNpcSnapshot = npc;
                functionPreviewZipperSnapshot = zipperData;
                ResetFunctionPreviewMoves();
                functionPreviewIndex = 0;
                functionPreviewElapsed = 0.0f;
                isFunctionPreviewPlaying = npcInspect.functionCount > 0;
                isExamineFunctionListOpen = false;
                isFunctionTypeListOpen = false;
                isMoveFunctionEditorOpen = false;
                isWaitFunctionEditorOpen = false;
                isLayerChangeFunctionEditorOpen = false;
                isDialogueEditorOpen = false;
                isGlobalSettingsOpen = false;
                isStageSettingsOpen = false;
                isAreaInspectorOpen = false;
                selected = 0;
                message = "Function preview playing";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 212, 394, 210, 30 })) {
                ModalHistory_Push(&modalHistory, EDITOR_MODAL_EXAMINE_LIST);
                isFunctionTypeListOpen = true;
                isExamineFunctionListOpen = false;
                isInspectTitleEditing = false;
                message = "Select a function type";
            } else if (mousePosition.y >= 166 &&
                       mousePosition.y < 166 + EDITOR_FUNCTION_LIST_VISIBLE_ROWS * EDITOR_FUNCTION_LIST_ROW_HEIGHT &&
                       mousePosition.x >= 212 && mousePosition.x < 748 &&
                       functionListScroll + (int)((mousePosition.y - 166) / EDITOR_FUNCTION_LIST_ROW_HEIGHT) < npcInspect.functionCount) {
                int clickedFunction = functionListScroll +
                                      (int)((mousePosition.y - 166) / EDITOR_FUNCTION_LIST_ROW_HEIGHT);
                if (lastInspectFunctionClick == clickedFunction &&
                    GetTime() - lastInspectFunctionClickTime <= 0.35) {
                    inspectFunctionIndex = clickedFunction;
                    ModalHistory_Push(&modalHistory, EDITOR_MODAL_EXAMINE_LIST);
                    isExamineFunctionListOpen = false;
                    isDialogueEditorOpen = npcInspect.functions[clickedFunction].type == RPG_INSPECT_DIALOGUE;
                    isMoveFunctionEditorOpen = npcInspect.functions[clickedFunction].type == RPG_INSPECT_MOVE;
                    isWaitFunctionEditorOpen = npcInspect.functions[clickedFunction].type == RPG_INSPECT_WAIT;
                    isLayerChangeFunctionEditorOpen = npcInspect.functions[clickedFunction].type == RPG_INSPECT_LAYER_CHANGE;
                    isLayerChangeTargetPicking = false;
                    isInspectDialogueEditing = isDialogueEditorOpen;
                    if (isMoveFunctionEditorOpen) {
                        isMoveTargetPicking = false;
                        isMoveEasingListOpen = false;
                    }
                    isStage3DialogueEditing = false;
                    activeDialogueLine = -1;
                    dialogueScroll = 0;
                    lastInspectFunctionClick = -1;
                    message = "Function editor opened";
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
        if (isDraggingMovePanel && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            movePanelBounds.x = Clamp(mousePosition.x - movePanelDragOffset.x, 0.0f, RPG_EDITOR_WIDTH - movePanelBounds.width);
            movePanelBounds.y = Clamp(mousePosition.y - movePanelDragOffset.y, 0.0f, RPG_EDITOR_HEIGHT - movePanelBounds.height);
        }
        if (isDraggingMovePanel && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) isDraggingMovePanel = false;
        float functionListWheel = isExamineFunctionListOpen ? GetMouseWheelMove() : 0.0f;
        if (isExamineFunctionListOpen && CheckCollisionPointRec(mousePosition,
            (Rectangle){ 212, 166, 536, EDITOR_FUNCTION_LIST_VISIBLE_ROWS * EDITOR_FUNCTION_LIST_ROW_HEIGHT }) &&
            functionListWheel != 0.0f) {
            int maximumScroll = npcInspect.functionCount > EDITOR_FUNCTION_LIST_VISIBLE_ROWS ?
                                npcInspect.functionCount - EDITOR_FUNCTION_LIST_VISIBLE_ROWS : 0;
            functionListScroll = Clamp(functionListScroll - (int)functionListWheel, 0, maximumScroll);
        }
        if (isExamineFunctionListOpen && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            mousePosition.y >= 166 && mousePosition.y < 166 + EDITOR_FUNCTION_LIST_VISIBLE_ROWS * EDITOR_FUNCTION_LIST_ROW_HEIGHT &&
            mousePosition.x >= 212 && mousePosition.x < 748 &&
            functionListScroll + (int)((mousePosition.y - 166) / EDITOR_FUNCTION_LIST_ROW_HEIGHT) < npcInspect.functionCount) {
            int removeIndex = functionListScroll +
                              (int)((mousePosition.y - 166) / EDITOR_FUNCTION_LIST_ROW_HEIGHT);
            if (npcInspect.functionCount > 1) {
                for (int i = removeIndex; i < npcInspect.functionCount - 1; i++)
                    npcInspect.functions[i] = npcInspect.functions[i + 1];
                npcInspect.functionCount--;
                if (inspectFunctionIndex >= npcInspect.functionCount) inspectFunctionIndex = npcInspect.functionCount - 1;
                if (functionListScroll > npcInspect.functionCount - EDITOR_FUNCTION_LIST_VISIBLE_ROWS)
                    functionListScroll = npcInspect.functionCount > EDITOR_FUNCTION_LIST_VISIBLE_ROWS ?
                                         npcInspect.functionCount - EDITOR_FUNCTION_LIST_VISIBLE_ROWS : 0;
                message = "Dialogue function deleted";
            } else message = "Keep at least one dialogue function";
        }
        if (isExamineFunctionListOpen && draggedInspectFunction >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            int destination = functionListScroll +
                              (int)((mousePosition.y - 166) / EDITOR_FUNCTION_LIST_ROW_HEIGHT);
            if (destination < functionListScroll) destination = functionListScroll;
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
            if (RpgEditorText_ShouldEndEditingOnOutsideClick((Rectangle){ 260, 132, 368, 24 },
                                                             mousePosition))
                isInspectTitleEditing = false;
        }
        if (isExamineFunctionListOpen && isInspectTitleEditing && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 260, 132, 368, 24 })) {
            RpgEditorText_UpdatePointerSelection(npcInspect.functions[inspectFunctionIndex].title,
                                                  mousePosition.x - 268.0f, 16.0f,
                                                  &titleCursorIndex, &titleSelectionEnd);
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
            if (selected == RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR) isGlobalSettingsOpen = false;
            if (selected == RPG_EDITOR_STAGE_SETTINGS_INSPECTOR) isStageSettingsOpen = false;
            if (selected == RPG_EDITOR_AREA_SETTINGS_INSPECTOR) isAreaInspectorOpen = false;
            selected = 0;
            activeDialogueLine = -1;
            draggedDialogueLine = -1;
            isItemNameEditing = false;
            paletteNameEditingIndex = -1;
            selectedPaletteInspectorIndex = -1;
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
            player.scale = Clamp(player.scale, 0.5f, 1.0f);
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 170, 188, 32 })) {
            isDialogueEditorOpen = true;
            activeDialogueLine = -1;
            isSpeakerEditing = false;
            message = "Dialogue editor opened";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 208, 90, 26 })) {
            message = SaveCharacterSettings(&layout, &npc, false, &savedSnapshot) ? "Saved" : "Save failed";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, 208, 90, 26 })) {
            RevertCharacterSettings(&savedSnapshot, &layout, &npc, false);
            message = "Reverted to saved";
        }
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 800, 136, 48, 26 })) npc.scale = Clamp(npc.scale - 0.1f, 0.5f, 3.0f);
        if (isNpcSummaryClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 852, 136, 48, 26 })) npc.scale = Clamp(npc.scale + 0.1f, 0.5f, 1.0f);
        if (isItemInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && selectedItemIndex >= 0) {
            if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 144, 188, 28 })) {
                isItemNameEditing = true;
                RpgEditorText_BeginPointerSelection(items.entries[selectedItemIndex].name,
                                                     inspectorMousePosition.x - 724.0f, 17.0f,
                                                     &itemNameCursorIndex, &itemNameSelectionAnchor,
                                                     &itemNameSelectionEnd);
            }
        }
        if (isItemNameEditing && selectedItemIndex >= 0) {
            UpdateShortText(items.entries[selectedItemIndex].name, RPG_ITEM_NAME_LENGTH,
                            &itemNameCursorIndex, &itemNameSelectionAnchor, &itemNameSelectionEnd);
            if (RpgEditorText_ShouldEndEditingOnOutsideClick((Rectangle){ 716, 144, 188, 28 },
                                                             inspectorMousePosition))
                isItemNameEditing = false;
        }
        if (isItemNameEditing && selectedItemIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 144, 188, 28 })) {
            RpgEditorText_UpdatePointerSelection(items.entries[selectedItemIndex].name,
                                                  inspectorMousePosition.x - 724.0f, 17.0f,
                                                  &itemNameCursorIndex, &itemNameSelectionEnd);
        }
        if (isItemNameEditing && selectedItemIndex >= 0) UpdateImeCandidateWindowAt(724, 172);
        if (isDoorInspectorClicked && !isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage, selectedDoorRow, selectedDoorColumn);
            RpgKeyDoor *keyDoor = RpgStage_GetKeyDoorAtCell(&stage, selectedDoorRow, selectedDoorColumn);
            RpgEditorFilePickerLayout keyPickerLayout = GetKeyDoorFilePickerLayout(keyDoor);
            Rectangle keyFailureBounds = { 716.0f, keyPickerLayout.contentBottom + 30.0f, 188.0f, 30.0f };
            if (keyDoor != NULL && CheckCollisionPointRec(inspectorMousePosition, keyPickerLayout.selectBounds)) {
                char selectedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
                FileDialogRequest keyDialog = MakeFilesFileDialogRequest("鍵ファイルを選択");
                if (FileDialog_Select(selectedPath, sizeof(selectedPath), &keyDialog)) {
                    snprintf(keyDoor->keyPath, sizeof(keyDoor->keyPath), "%s", selectedPath);
                    GameFont_AddText(keyDoor->keyPath);
                    message = "Key file selected";
                }
            } else if (keyDoor != NULL && CheckCollisionPointRec(inspectorMousePosition, keyFailureBounds)) {
                isKeyDoorFailureEditing = true;
                RpgEditorText_BeginPointerSelection(keyDoor->failureText,
                                                     inspectorMousePosition.x - 722.0f, 15.0f,
                                                     &keyDoorFailureCursorIndex,
                                                     &keyDoorFailureSelectionAnchor,
                                                     &keyDoorFailureSelectionEnd);
            } else if (signalIndex >= 0 && CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 816, 144, 38, 24 })) {
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
        if (isKeyDoorFailureEditing) {
            RpgKeyDoor *keyDoor = RpgStage_GetKeyDoorAtCell(&stage, selectedDoorRow, selectedDoorColumn);
            if (keyDoor == NULL || selected != 5) isKeyDoorFailureEditing = false;
            else {
                UpdateShortText(keyDoor->failureText, sizeof(keyDoor->failureText), &keyDoorFailureCursorIndex,
                                &keyDoorFailureSelectionAnchor, &keyDoorFailureSelectionEnd);
                RpgEditorFilePickerLayout keyPickerLayout = GetKeyDoorFilePickerLayout(keyDoor);
                Rectangle failureBounds = { 716.0f, keyPickerLayout.contentBottom + 30.0f, 188.0f, 30.0f };
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(inspectorMousePosition, failureBounds))
                    RpgEditorText_UpdatePointerSelection(keyDoor->failureText,
                                                          inspectorMousePosition.x - 722.0f, 15.0f,
                                                          &keyDoorFailureCursorIndex,
                                                          &keyDoorFailureSelectionEnd);
                if (RpgEditorText_ShouldEndEditingOnOutsideClick(failureBounds,
                                                                 inspectorMousePosition))
                    isKeyDoorFailureEditing = false;
                UpdateImeCandidateWindowAt(722, 268);
            }
        }
        if (isAttachmentInspectorClicked && !isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selectedAttachmentIndex >= 0 && selectedAttachmentIndex < attachments.count) {
            RpgAttachment *attachment = &attachments.entries[selectedAttachmentIndex];
            if (attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 176, 188, 28 })) {
                    /* 旗の開始要求は既存のPlay初期化で処理し、クリックをマップへ伝搬させない。 */
                    pendingSaveFlagPlayIndex = selectedAttachmentIndex;
                    message = "Starting play from save flag";
                }
            } else if (attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 232, 42, 24 })) {
                    attachment->socketRightLightAngle = Clamp(attachment->socketRightLightAngle - 5.0f, 0.0f, 80.0f);
                    message = "Socket light angle changed";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, 232, 42, 24 })) {
                    attachment->socketRightLightAngle = Clamp(attachment->socketRightLightAngle + 5.0f, 0.0f, 80.0f);
                    message = "Socket light angle changed";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 296, 42, 24 })) {
                    attachment->socketLightOpacity = Clamp(attachment->socketLightOpacity - 0.05f, 0.05f, 0.95f);
                    message = "Socket light opacity changed";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, 296, 42, 24 })) {
                    attachment->socketLightOpacity = Clamp(attachment->socketLightOpacity + 0.05f, 0.05f, 0.95f);
                    message = "Socket light opacity changed";
                }
            } else if (attachment->type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
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
                attachment->sizePerFile = Clamp(attachment->sizePerFile - RPG_STAGE_TILE_SIZE * 0.25f,
                                RPG_STAGE_TILE_SIZE * 0.25f, RPG_STAGE_TILE_SIZE * 2.0f);
                message = "Size per file changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 864, 194, 38, 24 })) {
                attachment->sizePerFile = Clamp(attachment->sizePerFile + RPG_STAGE_TILE_SIZE * 0.25f,
                                RPG_STAGE_TILE_SIZE * 0.25f, RPG_STAGE_TILE_SIZE * 2.0f);
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
            RpgEditorText_ClaimKeyboard();
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
            RpgEditorText_ClaimKeyboard();
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
        if (isZipperCapacityEditing) {
            RpgEditorText_ClaimKeyboard();
            if (!isStageSettingsOpen || selected != RPG_EDITOR_STAGE_SETTINGS_INSPECTOR) {
                isZipperCapacityEditing = false;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                isZipperCapacityEditing = false;
                message = "Zipper capacity edit cancelled";
            } else if (IsKeyPressed(KEY_ENTER)) {
                unsigned long value = strtoul(zipperCapacityInput, NULL, 10);
                if (value == 0) value = 1;
                if (value > 1048576UL) value = 1048576UL;
                layout.zipperMaxCapacityKB = (unsigned int)value;
                isZipperCapacityEditing = false;
                message = "Zipper capacity changed";
            } else {
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    size_t length = strlen(zipperCapacityInput);
                    if (length > 0) zipperCapacityInput[length - 1] = '\0';
                }
                int codepoint = GetCharPressed();
                while (codepoint > 0) {
                    size_t length = strlen(zipperCapacityInput);
                    if (codepoint >= '0' && codepoint <= '9' && length + 1 < sizeof(zipperCapacityInput)) {
                        zipperCapacityInput[length] = (char)codepoint;
                        zipperCapacityInput[length + 1] = '\0';
                    }
                    codepoint = GetCharPressed();
                }
            }
        }
        if (!isCloseDialogueEditorClicked && isDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePosition, (Rectangle){ 224, 96, 260, 26 }) &&
                activeDialogueLine >= 0) {
                isDialogueTextEditing = false;
                isSpeakerEditing = true;
                RpgEditorText_BeginPointerSelection(editedDialogue->speakers[activeDialogueLine],
                                                     mousePosition.x - 232.0f, 17.0f,
                                                     &speakerCursorIndex, &speakerSelectionAnchor,
                                                     &speakerSelectionEnd);
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 680, 64, 42, 22 })) {
                isDialogueTextEditing = false;
                isSpeakerEditing = false;
                if (dialogueFontSize > 12) dialogueFontSize--;
                dialogueBlockHeight = dialogueFontSize + 10;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 730, 64, 42, 22 })) {
                isDialogueTextEditing = false;
                isSpeakerEditing = false;
                if (dialogueFontSize < 32) dialogueFontSize++;
                dialogueBlockHeight = dialogueFontSize + 10;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
                isSpeakerEditing = false;
                int line = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
                activeDialogueLine = line < editedDialogue->lineCount ? line : -1;
                if (activeDialogueLine >= 0) {
                    isDialogueTextEditing = true;
                    RpgEditorText_BeginPointerSelection(editedDialogue->lines[activeDialogueLine],
                                                         mousePosition.x - (isInspectDialogueEditing ? 280.0f : 200.0f),
                                                         (float)dialogueFontSize, &dialogueCursorIndex,
                                                         &selectionAnchor, &selectionEnd);
                }
                draggedDialogueLine = activeDialogueLine;
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 156, 414, 210, 32 })) {
                if (RpgDialogue_AddLine(editedDialogue)) {
                    activeDialogueLine = editedDialogue->lineCount - 1;
                    isDialogueTextEditing = true;
                    dialogueCursorIndex = 0;
                    selectionAnchor = 0;
                    selectionEnd = 0;
                    if (activeDialogueLine >= visibleDialogueLines) dialogueScroll = activeDialogueLine - visibleDialogueLines + 1;
                    message = "Dialogue line added";
                } else message = "Dialogue line limit reached";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 382, 414, 116, 32 })) {
                isDialogueTextEditing = false;
                isSpeakerEditing = false;
                message = SaveEditedDialogue(isInspectDialogueEditing, isStage3DialogueEditing, isAreaEntryDialogueEditing,
                                              &stage, &dialogue, &stage3Event, &savedSnapshot) ? "Saved" : "Save failed";
            } else if (CheckCollisionPointRec(mousePosition, (Rectangle){ 510, 414, 116, 32 })) {
                RevertEditedDialogue(isInspectDialogueEditing, isStage3DialogueEditing, isAreaEntryDialogueEditing,
                                     &dialogue, &stage3Event, &savedSnapshot);
                activeDialogueLine = -1;
                isDialogueTextEditing = false;
                isSpeakerEditing = false;
                message = "Reverted to saved";
            }
        }
        if (isDialogueEditorClicked && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int line = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
            if (RpgDialogue_DeleteLine(editedDialogue, line)) {
                activeDialogueLine = -1;
                isDialogueTextEditing = false;
                message = "Dialogue line deleted";
            } else message = "Keep at least one dialogue line";
        }
        if (isSpeakerEditing && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mousePosition, (Rectangle){ 224, 96, 260, 26 }) &&
            activeDialogueLine >= 0) {
            RpgEditorText_UpdatePointerSelection(editedDialogue->speakers[activeDialogueLine],
                                                  mousePosition.x - 232.0f, 17.0f,
                                                  &speakerCursorIndex, &speakerSelectionEnd);
        }
        if (draggedDialogueLine >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            isDialogueEditorClicked && CheckCollisionPointRec(mousePosition, (Rectangle){ 154, 128, 652, 252 })) {
            int hoveredLine = dialogueScroll + (int)((mousePosition.y - 128.0f) / (dialogueBlockHeight + 4));
            if (hoveredLine == draggedDialogueLine) {
                RpgEditorText_UpdatePointerSelection(editedDialogue->lines[activeDialogueLine],
                                                      mousePosition.x - (isInspectDialogueEditing ? 280.0f : 200.0f),
                                                      (float)dialogueFontSize, &dialogueCursorIndex,
                                                      &selectionEnd);
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
        /* Keep the selected row highlighted after committing, but release the
           shared keyboard focus.  Selection and text focus are intentionally
           independent. */
        if ((isDialogueTextEditing || isSpeakerEditing) &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE))) {
            isDialogueTextEditing = false;
            isSpeakerEditing = false;
        }
        if (isDialogueEditorOpen) {
            if (isSpeakerEditing) UpdateSpeakerText(editedDialogue, activeDialogueLine,
                                                    &speakerCursorIndex, &speakerSelectionAnchor,
                                                    &speakerSelectionEnd);
            else if (isDialogueTextEditing)
                UpdateDialogueText(editedDialogue, activeDialogueLine, &dialogueCursorIndex,
                                   &selectionAnchor, &selectionEnd);
        }
        if (!isKeyboardCaptured && IsKeyPressed(KEY_TAB) &&
            (isDialogueEditorOpen || isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen || isWaitFunctionEditorOpen || isLayerChangeFunctionEditorOpen)) {
            isMoveTargetPicking = false;
            isMoveEasingListOpen = false;
            if (isMovePreviewPlaying) {
                isMovePreviewPlaying = false;
            }
            EditorModal previousModal = ModalHistory_Pop(&modalHistory);
            isDialogueEditorOpen = false;
            isMoveFunctionEditorOpen = false;
            isWaitFunctionEditorOpen = false;
            isLayerChangeFunctionEditorOpen = false;
            isLayerChangeTargetPicking = false;
            isExamineFunctionListOpen = previousModal == EDITOR_MODAL_EXAMINE_LIST;
            isFunctionTypeListOpen = previousModal == EDITOR_MODAL_FUNCTION_TYPE_LIST;
            isInspectDialogueEditing = false;
            isStage3DialogueEditing = false;
            activeDialogueLine = -1;
            isSpeakerEditing = false;
            message = (isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen || isWaitFunctionEditorOpen || isLayerChangeFunctionEditorOpen) ? "Returned to previous modal" : "Modal closed";
        }
        int largestWrappedLineCount = 1;
        for (int lineIndex = 0; lineIndex < editedDialogue->lineCount; lineIndex++) {
            int wrappedLineCount = GetWrappedLineCount(editedDialogue->lines[lineIndex], (float)dialogueFontSize);
            if (wrappedLineCount > largestWrappedLineCount) largestWrappedLineCount = wrappedLineCount;
        }
        dialogueBlockHeight = largestWrappedLineCount * (dialogueFontSize + 2) + 8;

        // UIは画面座標、マップ操作はmapMousePositionを使い分けて入力の伝搬を防ぐ。
        /* マップ描画と同じ内容矩形だけをマップ入力として受け取り、編集UIのクリックを伝搬させない。 */
        bool isMapPointer = CheckCollisionPointRec(mousePosition, GetEditorMapContentBounds());
        bool isUiBlockingMap = isGlobalMapOpen || globalMapInteractionThisFrame || !isMapPointer || isFunctionPreviewPlaying || didCancelFunctionPreview ||
                               wasModalOpenAtFrameStart || isInspectorClicked || isInspectorClickCaptured ||
                               (selected >= 1 && selected < RPG_EDITOR_INSPECTOR_COUNT &&
                                CheckCollisionPointRec(mousePosition, GetInspectorScreenBounds(selected))) ||
                               isDialogueEditorOpen ||
                               isExamineFunctionListOpen || isFunctionTypeListOpen || isMoveFunctionEditorOpen ||
                               isRevertSavedClicked || isBlockInventoryControlClicked ||
                               isBlockInventoryPointerHeld || isInspectorPointerHeld ||
                               isReferencePathClicked || isReferencePathPointerHeld ||
                               isGlobalSettingsButtonClicked || isGlobalSettingsPanelClicked ||
                               isGlobalSettingsPointerHeld || isStageSettingsButtonClicked ||
                               isStageSettingsPanelClicked || isStageSettingsPointerHeld ||
                               isAreaInspectorButtonClicked ||
                               isAreaInspectorPanelClicked || isAreaInspectorPointerHeld;
        bool blockContextMenuPointerConsumed = false;
        if (isBlockContextMenuOpen && (isEditorPlaying || blockMode)) {
            isBlockContextMenuOpen = false;
            blockContextMenuRow = -1;
            blockContextMenuColumn = -1;
        }
        if (isBlockContextMenuOpen && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                                       IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            blockContextMenuPointerConsumed = true;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && blockContextMenuRow >= 0 &&
                blockContextMenuRow < RPG_STAGE_ROWS && blockContextMenuColumn >= 0 &&
                blockContextMenuColumn < RPG_STAGE_WORLD_COLUMNS) {
                if (CheckCollisionPointRec(mousePosition, GetBlockContextDropBounds(blockContextMenuBounds))) {
                    int dropped = 0;
                    if (RpgStaticObjectDrop_Expand(&stage, currentStageNumber, blockContextMenuRow,
                                                   blockContextMenuColumn, &dropped)) {
                        blockEditedThisFrame = true;
                        message = TextFormat("Dropped %d file%s", dropped, dropped == 1 ? "" : "s");
                    } else message = "Drop needs files in this object folder";
                } else if (CheckCollisionPointRec(mousePosition, GetBlockContextRestoreBounds(blockContextMenuBounds))) {
                    if (RpgStaticObjectDrop_Restore(&stage, currentStageNumber, blockContextMenuRow,
                                                    blockContextMenuColumn)) {
                        blockEditedThisFrame = true;
                        message = "Object restored";
                    } else message = "Restore needs a dropped object";
                } else if (CheckCollisionPointRec(mousePosition, GetBlockContextDeleteBounds(blockContextMenuBounds)) &&
                           RemoveBlockAt(&stage, &items, &blockHistory, blockContextMenuRow, blockContextMenuColumn)) {
                    RpgWires_RemoveBroken(&wires, &stage);
                    RpgReceivers_RemoveBroken(&receivers, &stage);
                    RpgAttachments_RemoveBroken(&attachments, &stage);
                    RpgSignalBlocks_RemoveBroken(&signalBlocks, &stage);
                    blockEditedThisFrame = true;
                    message = "Block deleted";
                }
            }
            isBlockContextMenuOpen = false;
            blockContextMenuRow = -1;
            blockContextMenuColumn = -1;
        }
        /* Context actions belong to the normal editor.  In block mode, right
           click is the existing direct erase gesture. */
        if (!blockMode && !isBlockContextMenuOpen && !isEditorPlaying && isMapPointer && !isGlobalMapOpen &&
            !globalMapInteractionThisFrame && !wasModalOpenAtFrameStart &&
            !isFunctionPreviewPlaying && !didCancelFunctionPreview &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            Vector2 worldPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mapMousePosition.y };
            int contextColumn = (int)(worldPosition.x / RPG_STAGE_TILE_SIZE);
            int contextRow = (int)(worldPosition.y / RPG_STAGE_TILE_SIZE);
            /* Drop/Restore belongs to a concrete cell object.  Do not open a
               menu for empty map space: it would look like the clicked block
               had been ignored and has no object folder to operate on. */
            if (contextRow >= 0 && contextRow < RPG_STAGE_ROWS && contextColumn >= 0 &&
                contextColumn < RPG_STAGE_WORLD_COLUMNS &&
                stage.blocks[contextRow][contextColumn] != 0) {
                Rectangle mapContentBounds = GetEditorMapContentBounds();
                const float menuWidth = 118.0f;
                const float menuHeight = 99.0f;
                blockContextMenuBounds = (Rectangle){
                    Clamp(mousePosition.x, mapContentBounds.x, mapContentBounds.x + mapContentBounds.width - menuWidth),
                    Clamp(mousePosition.y, mapContentBounds.y, mapContentBounds.y + mapContentBounds.height - menuHeight),
                    menuWidth, menuHeight
                };
                blockContextMenuRow = contextRow;
                blockContextMenuColumn = contextColumn;
                isBlockContextMenuOpen = true;
                blockContextMenuPointerConsumed = true;
            }
        }
        isUiBlockingMap = isUiBlockingMap || isBlockContextMenuOpen || blockContextMenuPointerConsumed;
        bool isWirePropertySelected = selectedBlockType == RPG_BLOCK_PROPERTY_CONVEYOR;
        bool isBlockPropertySelected = FindBlockPropertyPlacement(selectedBlockType) != NULL;
        bool isMapEventPropertySelected = RpgBlockInventory_IsMapEventProperty(selectedBlockType);
        bool isAttachmentSelected = RpgBlockInventory_IsAttachment(selectedBlockType);
        if (conveyorPreviewRemaining > 0.0f) {
            conveyorPreviewRemaining = fmaxf(0.0f, conveyorPreviewRemaining - GetFrameTime());
            if (selectedConveyorIndex >= 0 && selectedConveyorIndex < wires.count)
                wires.entries[selectedConveyorIndex].conveyorPreviewElapsed += GetFrameTime();
            if (conveyorPreviewRemaining <= 0.0f && selectedConveyorIndex >= 0 &&
                selectedConveyorIndex < wires.count)
                wires.entries[selectedConveyorIndex].conveyorPreviewElapsed = 0.0f;
        }
        if (!isEditorPlaying && selected == RPG_EDITOR_CONVEYOR_INSPECTOR &&
            selectedConveyorIndex >= 0 && selectedConveyorIndex < wires.count &&
            RpgWires_IsConveyor(&wires.entries[selectedConveyorIndex]) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            IsInspectorContentScreenPoint(RPG_EDITOR_CONVEYOR_INSPECTOR, mousePosition)) {
            Vector2 pointer = GetInspectorLocalPointer(mousePosition, RPG_EDITOR_CONVEYOR_INSPECTOR);
            RpgWire *conveyor = &wires.entries[selectedConveyorIndex];
            if (CheckCollisionPointRec(pointer, (Rectangle){ 716, 182, 42, 26 })) {
                PushWireChangedHistory(&blockHistory, selectedConveyorIndex, *conveyor);
                conveyor->conveyorSpeed = Clamp(conveyor->conveyorSpeed - 16.0f, 16.0f, 960.0f);
                blockEditedThisFrame = true;
            } else if (CheckCollisionPointRec(pointer, (Rectangle){ 862, 182, 42, 26 })) {
                PushWireChangedHistory(&blockHistory, selectedConveyorIndex, *conveyor);
                conveyor->conveyorSpeed = Clamp(conveyor->conveyorSpeed + 16.0f, 16.0f, 960.0f);
                blockEditedThisFrame = true;
            } else if (CheckCollisionPointRec(pointer, (Rectangle){ 716, 246, 188, 26 })) {
                PushWireChangedHistory(&blockHistory, selectedConveyorIndex, *conveyor);
                conveyor->conveyorDirection = -conveyor->conveyorDirection;
                blockEditedThisFrame = true;
            } else if (CheckCollisionPointRec(pointer, (Rectangle){ 716, 306, 188, 26 })) {
                PushWireChangedHistory(&blockHistory, selectedConveyorIndex, *conveyor);
                conveyor->conveyorHasFloor = !conveyor->conveyorHasFloor;
                blockEditedThisFrame = true;
                message = conveyor->conveyorHasFloor ? "Conveyor floor enabled" : "Conveyor floor disabled";
            } else if (CheckCollisionPointRec(pointer, (Rectangle){ 716, 366, 42, 26 })) {
                PushWireChangedHistory(&blockHistory, selectedConveyorIndex, *conveyor);
                conveyor->conveyorSlideAngleDegrees = Clamp(conveyor->conveyorSlideAngleDegrees - 5.0f,
                                                            0.0f, 85.0f);
                blockEditedThisFrame = true;
            } else if (CheckCollisionPointRec(pointer, (Rectangle){ 862, 366, 42, 26 })) {
                PushWireChangedHistory(&blockHistory, selectedConveyorIndex, *conveyor);
                conveyor->conveyorSlideAngleDegrees = Clamp(conveyor->conveyorSlideAngleDegrees + 5.0f,
                                                            0.0f, 85.0f);
                blockEditedThisFrame = true;
            } else if (CheckCollisionPointRec(pointer, (Rectangle){ 716, 406, 188, 26 })) {
                conveyorPreviewRemaining = 3.0f;
                conveyor->conveyorPreviewElapsed = 0.001f;
                message = "Conveyor preview started";
            }
        }
        if (blockMode && !isBlockContextMenuOpen && !isUiBlockingMap && !isAttachmentErasePointerHeld &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            Vector2 worldPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mapMousePosition.y };
            if (isMapEventPropertySelected) {
                if (RpgMapEvents_RemoveAtPosition(&mapEvents, worldPosition,
                                                   RPG_STAGE_TILE_SIZE * 0.5f))
                    message = "Event deleted";
            } else {
            int attachmentIndex = RpgAttachments_FindAtWorldPosition(&attachments, &stage,
                                                                       worldPosition, 30.0f);
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
        }
        if (!RpgEditorDrag_IsBusy(&attachmentDrag) && blockMode && !isUiBlockingMap &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 worldPosition = GetEditorReferenceWorldPosition(&stage, mapIndex, mapMousePosition);
            int clickedAttachmentIndex = RpgAttachments_FindAtWorldPosition(&attachments, &stage,
                                                                              worldPosition, 14.0f);
            if (clickedAttachmentIndex >= 0) {
                RpgEditorDrag_Begin(&attachmentDrag, mousePosition);
                draggedAttachmentIndex = clickedAttachmentIndex;
                draggedAttachmentBeforeEdit = attachments.entries[clickedAttachmentIndex];
                attachmentDragPreview = draggedAttachmentBeforeEdit;
                attachmentDragPointer = mapMousePosition;
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
            Vector2 worldPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mapMousePosition.y };
            RpgAttachment snappedAttachment;
            RpgEditorDrag_Update(&attachmentDrag, mousePosition);
            attachmentDragPointer = mapMousePosition;
            if (RpgAttachments_FindSnap(&attachments, &stage, draggedAttachmentBeforeEdit.type,
                                        worldPosition, draggedAttachmentIndex, &snappedAttachment)) {
                // スナップ候補は設置情報だけなので、発射設定と経路を移動元から引き継ぐ。
                snappedAttachment.folderId = draggedAttachmentBeforeEdit.folderId;
                snappedAttachment.flagRaised = draggedAttachmentBeforeEdit.flagRaised;
                snappedAttachment.dataSize = draggedAttachmentBeforeEdit.dataSize;
                snappedAttachment.dataSpeed = draggedAttachmentBeforeEdit.dataSpeed;
                snappedAttachment.dataInterval = draggedAttachmentBeforeEdit.dataInterval;
                snappedAttachment.dataPreviewEnabled = draggedAttachmentBeforeEdit.dataPreviewEnabled;
                snappedAttachment.sizePerFile = draggedAttachmentBeforeEdit.sizePerFile;
                snappedAttachment.speedPerKilobyte = draggedAttachmentBeforeEdit.speedPerKilobyte;
                snappedAttachment.previewFileCount = draggedAttachmentBeforeEdit.previewFileCount;
                snappedAttachment.previewTotalBytes = draggedAttachmentBeforeEdit.previewTotalBytes;
                snappedAttachment.socketRightLightAngle = draggedAttachmentBeforeEdit.socketRightLightAngle;
                snappedAttachment.socketLightOpacity = draggedAttachmentBeforeEdit.socketLightOpacity;
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
        if (blockMode && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isUiBlockingMap) {
            int pathColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                                   RPG_STAGE_TILE_SIZE);
            int pathRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int receiverColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                                       RPG_STAGE_TILE_SIZE);
            int receiverRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
        /* PNGは通常モード・ブロックモード共通のドラッグで移動し、他の選択より先に入力を所有する。 */
        if (!isMoveFunctionEditorOpen && !isAttachmentPathEditing && !isAttachmentPathDragActive &&
            !RpgEditorDrag_IsBusy(&attachmentDrag) &&
            !isReceiverClickPending && !isUiBlockingMap && !RpgEditorDrag_IsBusy(&imageObjectDrag) &&
            !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&effectBlockDrag) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int imageIndex = -1;
            EditorMapObjectHit topmost = GetTopmostEditableObject(&stage, &player, &npc, &zipperData,
                                                                    mapIndex, mapMousePosition, &imageIndex);
            if (topmost == EDITOR_MAP_OBJECT_HIT_IMAGE && imageIndex >= 0) {
                /* A normal click and a drag share this entry point.  Select the
                   object before the drag state owns the pointer, so a simple
                   click always opens its inspector; the drag promotion below
                   still hides it once the pointer actually moves. */
                if (!blockMode) {
                    selected = RPG_EDITOR_IMAGE_INSPECTOR;
                    selectedImageObjectIndex = imageIndex;
                    activeDialogueLine = -1;
                    isItemNameEditing = false;
                }
                RpgEditorDrag_Begin(&imageObjectDrag, mousePosition);
                draggedImageObjectIndex = imageIndex;
                imageObjectDragPreviewId = stage.imageObjects.entries[imageIndex].id;
                isImageObjectDragPreviewVisible = false;
                message = "Click or drag image object";
            } else if (topmost == EDITOR_MAP_OBJECT_HIT_PLAYER ||
                       topmost == EDITOR_MAP_OBJECT_HIT_NPC) {
                /* ブロックモードでもPNGと同じ入口でキャラクターを先に取得し、背後への配置を防ぐ。 */
                draggedCharacterKind = topmost == EDITOR_MAP_OBJECT_HIT_PLAYER ? 1 : 2;
                characterDragPreviewKind = topmost;
                isCharacterDragPreviewVisible = false;
                if (!blockMode) {
                    /* Player parameters live in 全体設定 > プレイヤー. */
                    selected = draggedCharacterKind == 1 ? 0 : draggedCharacterKind;
                    activeDialogueLine = -1;
                    if (draggedCharacterKind == 3) isZipperPointerFeedbackSuppressed = false;
                } else selected = 0;
                RpgEditorDrag_Begin(&characterDrag, mousePosition);
                if (draggedCharacterKind != 1) message = "Click or drag character";
            }
        }
        // 参照オブジェクトは通常ブロックの設置処理へ渡す前に、単体ドラッグを優先する。
        if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isReceiverClickPending &&
            !isUiBlockingMap && !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&imageObjectDrag) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int referenceIndex = FindEditorReferenceObjectAtMapPoint(&stage, mapIndex, mapMousePosition);
            if (referenceIndex >= 0) {
                RpgEditorDrag_Begin(&referenceDrag, mousePosition);
                draggedReferenceObjectIndex = referenceIndex;
                isReferenceDragPreviewVisible = false;
                message = "Click or drag reference object";
            }
        }
        // 特殊ブロックの押下は保留し、クリックとドラッグを同じ入口から分ける。
        if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isBlockPropertySelected && !isReceiverClickPending &&
            !isUiBlockingMap && !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&imageObjectDrag) &&
            !effectBlockDrag.active && !effectBlockDrag.pending && !isWireEndpointDragActive &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
            effectBlockDragPointer = mapMousePosition;
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag special block to an empty cell";
        }
        if (effectBlockDrag.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_Update(&effectBlockDrag, mousePosition);
            effectBlockDragPointer = mapMousePosition;
        }
        if (referenceDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&referenceDrag, mousePosition)) {
            isReferenceDragPreviewVisible = true;
            referenceDragPointer = mapMousePosition;
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag reference object to an empty cell";
        }
        if (referenceDrag.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_Update(&referenceDrag, mousePosition);
            referenceDragPointer = mapMousePosition;
        }
        if (imageObjectDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&imageObjectDrag, mousePosition)) {
            isImageObjectDragPreviewVisible = true;
            imageObjectDragPointer = GetImageObjectDragPreviewPointer(mapMousePosition);
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag image object (hold Shift for free position)";
        }
        if (imageObjectDrag.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap) {
            RpgEditorDrag_Update(&imageObjectDrag, mousePosition);
            imageObjectDragPointer = GetImageObjectDragPreviewPointer(mapMousePosition);
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
            !isUiBlockingMap) {
            int destinationColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int destinationRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
        } else if (imageObjectDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            selected = RPG_EDITOR_IMAGE_INSPECTOR;
            selectedImageObjectIndex = draggedImageObjectIndex;
            RpgEditorDrag_End(&imageObjectDrag);
            draggedImageObjectIndex = -1;
            imageObjectDragPreviewId = 0;
        } else if (imageObjectDrag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Vector2 destinationPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                            mapMousePosition.y };
            int destinationColumn = (int)(destinationPosition.x / RPG_STAGE_TILE_SIZE);
            int destinationRow = (int)(destinationPosition.y / RPG_STAGE_TILE_SIZE);
            bool freePosition = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            RpgImageObject *draggedObject = draggedImageObjectIndex >= 0 &&
                                             draggedImageObjectIndex < stage.imageObjects.count ?
                                             &stage.imageObjects.entries[draggedImageObjectIndex] : NULL;
            if (!isUiBlockingMap && draggedObject != NULL &&
                ((freePosition && RpgImageObjects_MoveToPosition(&stage.imageObjects, draggedImageObjectIndex,
                                                                  destinationPosition, RPG_STAGE_TILE_SIZE,
                                                                  RPG_STAGE_WORLD_COLUMNS, RPG_STAGE_ROWS)) ||
                 (!freePosition && destinationRow >= 0 && destinationRow < RPG_STAGE_ROWS &&
                  destinationColumn >= 0 && destinationColumn < RPG_STAGE_WORLD_COLUMNS &&
                  RpgImageObjects_MoveToCell(&stage.imageObjects, draggedImageObjectIndex,
                                              destinationRow, destinationColumn)))) {
                blockEditedThisFrame = true;
                message = "Image object moved";
            } else if (draggedObject != NULL) {
                message = "Image position is outside the stage";
            }
            selected = RPG_EDITOR_IMAGE_INSPECTOR;
            selectedImageObjectIndex = draggedImageObjectIndex;
            RpgEditorDrag_End(&imageObjectDrag);
            draggedImageObjectIndex = -1;
            isImageObjectDragPreviewVisible = false;
            imageObjectDragPreviewId = 0;
        } else if (referenceDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_End(&referenceDrag);
            draggedReferenceObjectIndex = -1;
        } else if (referenceDrag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Vector2 destination = GetEditorReferenceWorldPosition(&stage, mapIndex, mapMousePosition);
            int destinationRow = -1, destinationColumn = -1;
            if (!isUiBlockingMap && draggedReferenceObjectIndex >= 0 &&
                draggedReferenceObjectIndex < stage.referenceObjects.count &&
                RpgStage_GetWorldCellAtPosition(&stage, destination, &destinationRow, &destinationColumn)) {
                stage.referenceObjects.entries[draggedReferenceObjectIndex].position =
                    RpgStage_GetWorldPositionForCell(&stage, destinationRow, destinationColumn);
                selectedReferenceObjectIndex = draggedReferenceObjectIndex;
                blockEditedThisFrame = true;
                message = "Reference object moved";
            }
            RpgEditorDrag_End(&referenceDrag);
            draggedReferenceObjectIndex = -1;
            isReferenceDragPreviewVisible = false;
        } else if (effectBlockDrag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (!isUiBlockingMap) {
                int destinationColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
                int destinationRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
                if (destinationRow >= 0 && destinationRow < RPG_STAGE_ROWS && destinationColumn >= 0 &&
                    destinationColumn < RPG_STAGE_WORLD_COLUMNS &&
                    (destinationRow != draggedEffectBlockRow || destinationColumn != draggedEffectBlockColumn)) {
                    if (MoveSpecialBlock(&stage, &attachments, &items, &blockHistory, &signalBlocks,
                                         draggedEffectBlockRow, draggedEffectBlockColumn,
                                         destinationRow, destinationColumn)) {
                        RpgWires_RemoveBroken(&wires, &stage);
                        RpgReceivers_RemoveBroken(&receivers, &stage);
                        RpgAttachments_RemoveBroken(&attachments, &stage);
                        blockEditedThisFrame = true;
                        message = "Effect block moved";
                    } else message = "Effect blocks need ordinary blocks or empty cells";
                }
            }
            RpgEditorDrag_End(&effectBlockDrag);
            draggedEffectBlockRow = -1;
            draggedEffectBlockColumn = -1;
            isEffectBlockDragPreviewVisible = false;
            effectBlockDragPreviewRow = -1;
            effectBlockDragPreviewColumn = -1;
        } else if (isWireEndpointDragActive && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (!isUiBlockingMap) {
                int destinationColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
                int destinationRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
            if (!hasRecordedDraggedWireHistory && draggedWireIndex >= 0 &&
                draggedWireIndex < wires.count && RpgWires_IsConveyor(&wires.entries[draggedWireIndex])) {
                selectedConveyorIndex = draggedWireIndex;
                selected = RPG_EDITOR_CONVEYOR_INSPECTOR;
                conveyorPreviewRemaining = 0.0f;
                message = "Conveyor Inspector opened";
            }
            isWireEndpointDragActive = false;
            hasRecordedDraggedWireHistory = false;
            draggedWireIndex = -1;
            draggedWireLastRow = -1;
            draggedWireLastColumn = -1;
        // 導線は始点を選ぶと隣接ブロックへ終点を置き、端点のドラッグで経路を作り直す。
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && isWirePropertySelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending &&
                   !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
        // 通常イベントもパレット選択後のマス配置として扱い、通常モードの追加操作を持たせない。
        } else if (blockMode && isMapEventPropertySelected && !isAttachmentPathDragActive &&
                   !RpgEditorDrag_IsBusy(&attachmentDrag) && !RpgEditorDrag_IsBusy(&effectBlockDrag) &&
                   !isWireEndpointDragActive && !isReceiverClickPending && !isUiBlockingMap &&
                   IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 eventPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mapMousePosition.y };
            int existingEvent = RpgMapEvents_FindAtPosition(&mapEvents, eventPosition,
                                                             RPG_STAGE_TILE_SIZE * 0.5f);
            if (existingEvent >= 0) {
                // Event選択中は既存マスをドラッグ移動、空きマスを新規配置として分ける。
                draggedMapEventIndex = existingEvent;
                message = "Drag event to move";
            } else if (RpgMapEvents_Add(&mapEvents, eventPosition)) message = "Event placed";
            else message = "Event limit reached";
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && isBlockPropertySelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending &&
                   !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isPropertyPlacementPending = true;
            pendingPropertyBlockType = selectedBlockType;
        }
        if (blockMode && isPropertyPlacementPending &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !isUiBlockingMap) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
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
                   IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                               RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
            float localX = mapMousePosition.x - (float)((int)(mapMousePosition.x / RPG_STAGE_TILE_SIZE)) * RPG_STAGE_TILE_SIZE;
            float localY = mapMousePosition.y - (float)((int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE)) * RPG_STAGE_TILE_SIZE;
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
                          "Data button attached" :
                          selectedBlockType == RPG_BLOCK_ATTACHMENT_SAVE_FLAG ?
                          "Save flag attached" : "Radio emitter attached";
            } else message = "Attach to an unused block edge";
        } else if (blockMode && selectedBlockType == RPG_BLOCK_IMAGE_OBJECT && !isUiBlockingMap &&
                   !RpgEditorDrag_IsBusy(&imageObjectDrag) &&
                   IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
            int imageIndex = row >= 0 && row < RPG_STAGE_ROWS && column >= 0 &&
                             column < RPG_STAGE_WORLD_COLUMNS ?
                             RpgImageObjects_Add(&stage.imageObjects, row, column) : -1;
            if (imageIndex < 0) message = "Image object limit reached";
            else {
                char imagePath[RPG_IMAGE_OBJECT_PATH_LENGTH] = { 0 };
                FileDialogRequest imageDialog = MakeSpriteFileDialogRequest("画像PNGを選択");
                if (FileDialog_Select(imagePath, sizeof(imagePath), &imageDialog)) {
                    snprintf(stage.imageObjects.entries[imageIndex].path,
                             sizeof(stage.imageObjects.entries[imageIndex].path), "%s", imagePath);
                    GameFont_AddText(imagePath);
                    selected = RPG_EDITOR_IMAGE_INSPECTOR;
                    selectedImageObjectIndex = imageIndex;
                    message = "Image object created";
                } else {
                    selected = RPG_EDITOR_IMAGE_INSPECTOR;
                    selectedImageObjectIndex = imageIndex;
                    message = "Image object created - select PNG from Inspector";
                }
            }
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) &&
                   !isBlockPropertySelected && !isMapEventPropertySelected &&
                   !RpgEditorDrag_IsBusy(&effectBlockDrag) && !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&imageObjectDrag) && !isWireEndpointDragActive &&
                   !isReceiverClickPending && !isAttachmentSelected &&
                   clickedBlockType == 0 && !isUiBlockingMap && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 worldPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mapMousePosition.y };
            int column = (int)(worldPosition.x / RPG_STAGE_TILE_SIZE);
            int row = (int)(worldPosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            if (RpgBlockInventory_IsReferenceObject(selectedBlockType)) {
                RpgReferenceObjectKind referenceKind =
                    RpgBlockInventory_IsReferenceFolder(selectedBlockType) ?
                    RPG_REFERENCE_OBJECT_FOLDER : RPG_REFERENCE_OBJECT_FILE;
                char referencePath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
                if (referenceKind == RPG_REFERENCE_OBJECT_FOLDER) {
                    char folderName[64];
                    snprintf(folderName, sizeof(folderName), "Folder_%d_%d", row + 1, column + 1);
                    if (!RpgStageStorage_CreateBuildFolder(currentStageNumber, folderName, referencePath,
                                                           (int)sizeof(referencePath))) {
                        message = "Folder creation failed";
                        continue;
                    }
                }
                if (!RpgReferenceObjects_Add(&stage.referenceObjects, referenceKind,
                                             RpgStage_GetWorldPositionForCell(&stage, row, column),
                                             referencePath, 0)) message = "Reference object limit reached";
                else {
                    blockEditedThisFrame = true;
                    message = referenceKind == RPG_REFERENCE_OBJECT_FOLDER ? "Folder created" : "File object created";
                }
            } else if (PlaceBlockType(&stage, &attachments, &blockHistory, row, column, selectedBlockType)) {
                if (!RpgBlockInventory_IsReferenceObject(selectedBlockType) &&
                    !RpgBlockInventory_IsAttachment(selectedBlockType) &&
                    selectedBlockType < RPG_BLOCK_PROPERTY_ITEM) {
                    char objectFolder[RPG_STAGE_PATH_LENGTH];
                    (void)RpgStaticObjectDrop_EnsureFolder(currentStageNumber, row, column,
                                                           selectedBlockType, objectFolder,
                                                           (int)sizeof(objectFolder));
                }
                if (selectedBlockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL)
                    RpgSignalBlocks_Add(&signalBlocks, row, column);
                blockEditedThisFrame = true;
            }
            }
        } else if (blockMode && !isBlockContextMenuOpen && !isMapEventPropertySelected &&
                   !isAttachmentErasePointerHeld && !isUiBlockingMap && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 worldPosition = GetEditorReferenceWorldPosition(&stage, mapIndex, mapMousePosition);
            int column = (int)(worldPosition.x / RPG_STAGE_TILE_SIZE);
            int row = (int)(worldPosition.y / RPG_STAGE_TILE_SIZE);
            int referenceIndex = FindEditorReferenceObjectAtMapPoint(&stage, mapIndex, mapMousePosition);
            if (referenceIndex >= 0 && RemoveEditorReferenceObjectAt(&stage.referenceObjects, referenceIndex)) {
                if (selectedReferenceObjectIndex == referenceIndex) {
                    selected = 0;
                    selectedReferenceObjectIndex = -1;
                } else if (selectedReferenceObjectIndex > referenceIndex) selectedReferenceObjectIndex--;
                blockEditedThisFrame = true;
                message = "Reference object removed";
            } else if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            int removedImageIndex = RpgImageObjects_FindAtCell(&stage.imageObjects, row, column);
            if (removedImageIndex >= 0 && RpgImageObjects_RemoveAtCell(&stage.imageObjects, row, column)) {
                if (selected == RPG_EDITOR_IMAGE_INSPECTOR) {
                    if (selectedImageObjectIndex == removedImageIndex) {
                        selected = 0;
                        selectedImageObjectIndex = -1;
                    } else if (selectedImageObjectIndex > removedImageIndex) selectedImageObjectIndex--;
                }
                blockEditedThisFrame = true;
                message = "Image object removed";
            } else if (RemoveBlockAt(&stage, &items, &blockHistory, row, column)) {
                RpgWires_RemoveBroken(&wires, &stage);
                RpgReceivers_RemoveBroken(&receivers, &stage);
                RpgAttachments_RemoveBroken(&attachments, &stage);
                RpgSignalBlocks_RemoveBroken(&signalBlocks, &stage);
                blockEditedThisFrame = true;
                }
            }
        /* Move編集のクリックは専用パネルと対象選択だけで消費する。通常のマップ選択へ
           伝播させると、終点設定や対象選択と同時にPNGインスペクターが開いてしまう。 */
        } else if (!isMoveFunctionEditorOpen && !isAttachmentPathEditing && !RpgEditorDrag_IsBusy(&attachmentDrag) && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&imageObjectDrag) && !RpgEditorDrag_IsBusy(&characterDrag) && !isWireEndpointDragActive && !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int clickedItemIndex = GetClickedItemIndex(&items, mapIndex, mapMousePosition);
            Vector2 eventClickPosition = GetEditorReferenceWorldPosition(&stage, mapIndex, mapMousePosition);
            int clickedEventIndex = RpgMapEvents_FindAtPosition(&mapEvents, eventClickPosition,
                                                                 RPG_STAGE_TILE_SIZE * 0.5f);
            int clickedDoorRow = -1;
            int clickedDoorColumn = -1;
            int clickedColumn = -1;
            int clickedRow = -1;
            (void)RpgStage_GetWorldCellAtPosition(&stage, eventClickPosition,
                                                   &clickedRow, &clickedColumn);
            int clickedImageObjectIndex = -1;
            EditorMapObjectHit topmostObject = GetTopmostEditableObject(&stage, &player, &npc, &zipperData,
                                                                          mapIndex, mapMousePosition,
                                                                          &clickedImageObjectIndex);
            int clickedReferenceObjectIndex = FindEditorReferenceObjectAtMapPoint(&stage, mapIndex,
                                                                                     mapMousePosition);
            bool isReferenceClicked = clickedReferenceObjectIndex >= 0 ||
                                      (clickedRow >= 0 && clickedRow < RPG_STAGE_ROWS &&
                                       clickedColumn >= 0 && clickedColumn < RPG_STAGE_WORLD_COLUMNS &&
                                       RpgBlockInventory_IsReferenceObject(stage.blocks[clickedRow][clickedColumn]));
            int clickedAttachmentIndex = RpgAttachments_FindAtWorldPosition(&attachments, &stage,
                                                                              eventClickPosition, 30.0f);
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
            if (!blockMode && topmostObject == EDITOR_MAP_OBJECT_HIT_PLAYER) {
                /* Player parameters are edited only through 全体設定 > プレイヤー.
                 * Keep direct map dragging, but do not open the retired player inspector. */
                selected = 0;
                activeDialogueLine = -1;
                draggedCharacterKind = 1;
                characterDragPreviewKind = EDITOR_MAP_OBJECT_HIT_PLAYER;
                isCharacterDragPreviewVisible = false;
                RpgEditorDrag_Begin(&characterDrag, mousePosition);
            } else if (!blockMode && topmostObject == EDITOR_MAP_OBJECT_HIT_NPC) {
                selected = 2;
                activeDialogueLine = -1;
                draggedCharacterKind = 2;
                characterDragPreviewKind = EDITOR_MAP_OBJECT_HIT_NPC;
                isCharacterDragPreviewVisible = false;
                RpgEditorDrag_Begin(&characterDrag, mousePosition);
                message = "NPC selected - drag to move";
            } else if (clickedAttachmentIndex >= 0) {
                selected = 6;
                selectedAttachmentIndex = clickedAttachmentIndex;
                isAttachmentPathEditing = false;
                message = attachments.entries[clickedAttachmentIndex].type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON ?
                          "Data button selected" : "Emitter selected";
            } else if (!blockMode && clickedImageObjectIndex >= 0) {
                selected = RPG_EDITOR_IMAGE_INSPECTOR;
                selectedImageObjectIndex = clickedImageObjectIndex;
                activeDialogueLine = -1;
                isItemNameEditing = false;
                message = "Image object selected";
            } else if (clickedReceiverIndex >= 0 && !isReceiverClickPending) {
                // 押下中は判定を保留し、離したクリックとドラッグを排他的に扱う。
                isReceiverClickPending = true;
                pendingReceiverIndex = clickedReceiverIndex;
                receiverPressPosition = mousePosition;
                message = "Release to change side, drag to edit wire";
            } else if (!blockMode && clickedEventIndex >= 0 && IsKeyDown(KEY_LEFT_SHIFT)) {
                draggedMapEventIndex = clickedEventIndex;
                message = "Drag event to move";
            } else if (!blockMode && clickedEventIndex >= 0) {
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
            } else if (clickedReferenceObjectIndex >= 0) {
                selected = 7;
                selectedReferenceObjectIndex = clickedReferenceObjectIndex;
                isReferencePathEditing = false;
                isReferencePointerFeedbackSuppressed = false;
                const RpgReferenceObject *reference = &stage.referenceObjects.entries[clickedReferenceObjectIndex];
                if (reference->objectKind == RPG_REFERENCE_OBJECT_FOLDER)
                    snprintf(referenceFolderNameInput, sizeof(referenceFolderNameInput), "%s",
                             GetReferenceLeafName(reference->path));
                else referenceFolderNameInput[0] = '\0';
                referencePathCursorIndex = (int)strlen(reference->objectKind == RPG_REFERENCE_OBJECT_FOLDER ?
                                                        referenceFolderNameInput : reference->path);
                referencePathSelectionAnchor = referencePathCursorIndex;
                referencePathSelectionEnd = referencePathCursorIndex;
                message = reference->objectKind == RPG_REFERENCE_OBJECT_FOLDER ?
                          "Folder selected" : "FILE.png selected";
            } else if (clickedItemIndex >= 0) {
                selected = 4;
                selectedItemIndex = clickedItemIndex;
                isItemNameEditing = false;
                itemNameCursorIndex = (int)strlen(items.entries[clickedItemIndex].name);
                itemNameSelectionAnchor = itemNameCursorIndex;
                itemNameSelectionEnd = itemNameCursorIndex;
                message = "Item selected";
            }
            if (selected == 7 && !isReferenceClicked) {
                selected = 0;
                isReferencePointerFeedbackSuppressed = true;
            }
            if (selected == RPG_EDITOR_IMAGE_INSPECTOR && clickedImageObjectIndex < 0) {
                selected = 0;
                selectedImageObjectIndex = -1;
            }
        }
        if (characterDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&characterDrag, mousePosition)) {
            isCharacterDragPreviewVisible = true;
            characterDragPointer = GetCharacterDragPreviewPointer(mapMousePosition);
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = "Drag character (hold Shift for free position)";
        }
        if (characterDrag.active && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
            RpgEditorDrag_Update(&characterDrag, mousePosition);
            characterDragPointer = GetCharacterDragPreviewPointer(mapMousePosition);
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (draggedCharacterKind == 1) MoveCharacterToEditorPointer(&player, mapIndex, mapMousePosition);
                else if (draggedCharacterKind == 2) MoveCharacterToEditorPointer(&npc, mapIndex, mapMousePosition);
                RpgEditorDrag_End(&characterDrag);
                draggedCharacterKind = 0;
                characterDragPreviewKind = EDITOR_MAP_OBJECT_HIT_NONE;
                isCharacterDragPreviewVisible = false;
                message = "Character position updated";
            }
        } else if (characterDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgEditorDrag_End(&characterDrag);
            draggedCharacterKind = 0;
            characterDragPreviewKind = EDITOR_MAP_OBJECT_HIT_NONE;
            isCharacterDragPreviewVisible = false;
        }
        if (draggedMapEventIndex >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap) {
            Vector2 eventPosition = { mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                      mapMousePosition.y };
            (void)RpgMapEvents_Move(&mapEvents, draggedMapEventIndex, eventPosition);
        }
        if (!isKeyboardCaptured && !isEditorPlaying && !isReferencePathEditing && selected == 7 &&
            IsKeyPressed(KEY_DELETE) &&
            RemoveEditorReferenceObjectAt(&stage.referenceObjects, selectedReferenceObjectIndex)) {
            selected = 0;
            selectedReferenceObjectIndex = -1;
            blockEditedThisFrame = true;
            message = "Reference object removed";
        }
        if (!isKeyboardCaptured && IsKeyPressed(KEY_S) &&
            (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && activeDialogueLine < 0) {
            bool saved = SaveEditorAndUpdateSnapshot(&layout, &player, &npc, &stage, &dialogue, &stage3Event,
                                                      &items, &savedSnapshot, &savedItems) &&
                         RpgStageCatalog_Save(&stageCatalogData);
            message = saved ? "Saved" : "Save failed";
        }
        }
        (void)blockEditedThisFrame;
        }
        /* Keep editing entirely memory-first.  A quiet editor frame publishes
           only a changed static stage package, so Play can consume the recent
           package without every placement/removal performing file IO. */
        if (!isEditorPlaying) {
            SyncEditorStageFolderWhenIdle(&layout, &stage, &dialogue, &stage3Event, &items,
                                          currentStageNumber, &lastStageFolderSync,
                                          &hasStageFolderSync, &lastStageFolderSyncNumber,
                                          &nextStageFolderComparisonTime,
                                          &nextStageFolderSyncTime, &editorPreviewBuildDirty,
                                          &editorPreviewBuildReady, &editorPreviewBuildStageNumber);
        }
        if (!isEditorPlaying)
        DrawEditor(&player, &npc, &stage, &layout, &stage3Event, &areaEntryEvents, &zipperData, zipperTexture, fileTexture,
                   isEditorPlaying ? &editorPlayShots : &attachmentPreviewShots, isEditorPlaying,
                   &dialogue, selected, mapIndex, blockMode,
                   dialogueScroll, activeDialogueLine, dialogueCursorIndex, selectionAnchor,
                   selectionEnd, draggedDialogueLine, isDialogueEditorOpen, dialogueBlockHeight,
                   dialogueFontSize, isSpeakerEditing, isStage3DialogueEditing, isAreaEntryDialogueEditing, entryDialogueAreaIndex, isInspectDialogueEditing, isMoveFunctionEditorOpen, isWaitFunctionEditorOpen, isLayerChangeFunctionEditorOpen, isLayerChangeTargetPicking, isMoveTargetPicking, isMoveEasingListOpen, isMovePreviewPlaying, movePreviewElapsed, movePreviewSpritePosition,
                   inspectFunctionIndex, functionListScroll, isExamineFunctionListOpen, isFunctionTypeListOpen,
                   isFunctionPreviewPlaying, functionPreviewIndex, functionPreviewElapsed,
                   draggedInspectFunction, isInspectTitleEditing, titleCursorIndex,
                   titleSelectionAnchor, titleSelectionEnd, speakerCursorIndex, speakerSelectionAnchor, speakerSelectionEnd, message, isExitConfirmationOpen,
                   isExitDetailsOpen,
                   exitDetailsScroll, &savedSnapshot, &items, &savedItems, selectedItemIndex, isItemNameEditing, itemNameCursorIndex,
                   itemNameSelectionAnchor, itemNameSelectionEnd, selectedDoorRow, selectedDoorColumn,
                   selectedAttachmentIndex, isAttachmentPathEditing,
                   selectedBlockInventory, selectedBlockType,
                   isBlockInventoryListOpen,
                   &paletteBlockDrag, paletteBlockDragSourcePalette, paletteBlockDragSourceSlot,
                   &paletteRowDrag, paletteRowDragSource,
                   selectedPaletteInspectorIndex,
                   paletteNameEditingIndex == selectedPaletteInspectorIndex,
                   paletteNameCursorIndex, paletteNameSelectionAnchor, paletteNameSelectionEnd,
                   isZipperPointerFeedbackSuppressed,
                     selectedReferenceObjectIndex, isReferencePathEditing,
                    isReferencePointerFeedbackSuppressed,
                   referencePathCursorIndex, referencePathSelectionAnchor, referencePathSelectionEnd, referenceFolderNameInput, explorerMode,
                   isGlobalSettingsOpen, isStageSettingsOpen, isAreaInspectorOpen,
                    editorPlayDialogueIndex, editorPlayInspectTarget, editorPlayInspectFunctionIndex,
                     editorPlayInspectLineIndex, editorPlayZipperFollowsPlayer, &editorScene,
                     isGlobalMapOpen, isBlockContextMenuOpen, blockContextMenuBounds);
    }
    RpgCharacter_UnloadPlayerSprites();
    RpgImageObjects_UnloadTextures();
    UnloadTexture(zipperTexture);
    UnloadTexture(fileTexture);
    RpgStageBackground_Unload(&stageBackground);
    RpgStage_SetGroundTexture((Texture2D){ 0 });
    RpgStageGroundTexture_Unload(&groundBlockTexture);
    RpgGimicSprites_Unload();
    RpgGameWindow_Uninstall();
    RpgViewport_Shutdown();
    RpgScene_Release(&editorScene);
    GameFont_Unload();
    RestoreEditorCloseHandler();
    CloseWindow();
    return 0;
}
// 役割: RPG エディターの編集状態、UI、保存、プレビューを統合するエントリーポイント。
