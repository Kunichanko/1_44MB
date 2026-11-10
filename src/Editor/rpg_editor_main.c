// 依存する自プロジェクト内ファイル: ../Editor/file_dialog.h, rpg_attachment.h, rpg_block_inventory.h, rpg_character.h, rpg_data_shot.h, rpg_dialogue.h, rpg_layout.h, rpg_magnet.h, rpg_map_event.h, rpg_receiver.h, rpg_stage.h, rpg_stage_build.h, rpg_wire.h, game_font.h
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
#include <limits.h>

#define RPG_TEXT_ROUTE_RAYLIB_CALLS
#include "game_font.h"
#undef DrawText
static void Editor_DrawText(const char *text, int x, int y, int fontSize, Color color);
#define DrawText(text, x, y, fontSize, color) Editor_DrawText((text), (x), (y), (fontSize), (color))
#include "../Editor/file_dialog.h"
#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_item.h"
#include "rpg_editor_text.h"
#include "rpg_editor_drag.h"
#include "rpg_editor_pointer_interaction.h"
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
#include "rpg_ui_theme.h"
#include "rpg_inspect.h"
#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_preview_event.h"
#include "rpg_preview_system.h"
#include "rpg_block_inventory.h"
#include "rpg_build_cell_storage.h"
#include "rpg_map_event.h"
#include "rpg_object_folder.h"
#include "rpg_explorer_launcher.h"
#include "rpg_receiver.h"
#include "rpg_static_object_drop.h"
#include "rpg_stage3_event.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_stage_build.h"
#include "rpg_stage_authority.h"
#include "rpg_stage_storage.h"
#include "rpg_wire.h"
#include "rpg_zipper.h"

/* This file owns editor controls and palette previews.  Keep its primary UI
   blue in one place rather than altering raylib's gameplay/world colors. */
#undef DARKBLUE
#define DARKBLUE RPG_UI_PRIMARY_BLUE

// 依存関係: 全体設定の build 保存方式は rpg_build_cell_storage に保存する。

// 既存の編集 UI から共通テキストモジュールへ移行するための互換名。
#define GetCursorIndexAtX RpgEditorText_GetCursorIndexAtX
#define GetWrappedLineCount(text, fontSize) RpgEditorText_GetWrappedLineCount((text), (fontSize), 590.0f)
#define DrawTextCaret RpgEditorText_DrawCaret

enum { RPG_EDITOR_WIDTH = 960, RPG_EDITOR_HEIGHT = 540, NPC_VISIBLE_LINES = 9,
       RPG_EDITOR_INSPECTOR_HEADER_HEIGHT = 38 };
enum { MODAL_HISTORY_CAPACITY = 8 };

/* Play 開始の待ち時間を、セル生成とは別の同期処理ごとに記録する。
   この診断は静的ステージ・実行時フォルダを変更しない。 */
typedef struct RpgEditorPlayStartProfile {
    double resetPreviewMs;
    double runtimeCopyMs;
    double previewSyncMs;
    double resumeOrCreateMs;
    double ensureAreasMs;
    double zipperMs;
    double viewportMs;
    double runtimeTrackingMs;
    double totalMs;
    int generatedBeforeEnsure;
    int totalBeforeEnsure;
    bool pendingBeforeEnsure;
    bool usedPreparedPreview;
    bool succeeded;
} RpgEditorPlayStartProfile;

static RpgEditorPlayStartProfile lastPlayStartProfile;

static void WritePlayStartProfile(const RpgEditorPlayStartProfile *profile)
{
    char path[1024];
    FILE *file;
    RpgObjectFolderResumeProfile resumeProfile = { 0 };
    bool hasResumeProfile;
    if (profile == NULL ||
        snprintf(path, sizeof(path), "%splay_start_profile.log", GetApplicationDirectory()) <= 0)
        return;
    file = fopen(path, "wb");
    if (file == NULL) return;
    hasResumeProfile = RpgObjectFolders_GetLastResumeProfile(&resumeProfile);
    fprintf(file, "Play start profile (milliseconds)\n");
    fprintf(file, "result=%s\n", profile->succeeded ? "success" : "failed");
    fprintf(file, "reset_preview=%.3f\n", profile->resetPreviewMs);
    fprintf(file, "runtime_copy=%.3f\n", profile->runtimeCopyMs);
    fprintf(file, "preview_sync=%.3f\n", profile->previewSyncMs);
    fprintf(file, "resume_or_create=%.3f\n", profile->resumeOrCreateMs);
    fprintf(file, "ensure_all_areas=%.3f\n", profile->ensureAreasMs);
    fprintf(file, "zipper_directory=%.3f\n", profile->zipperMs);
    fprintf(file, "viewport_resize=%.3f\n", profile->viewportMs);
    fprintf(file, "runtime_tracking=%.3f\n", profile->runtimeTrackingMs);
    fprintf(file, "total=%.3f\n", profile->totalMs);
    fprintf(file, "cells_before_ensure=%d/%d pending=%s\n",
            profile->generatedBeforeEnsure, profile->totalBeforeEnsure,
            profile->pendingBeforeEnsure ? "true" : "false");
    fprintf(file, "used_prepared_preview=%s\n",
            profile->usedPreparedPreview ? "true" : "false");
    if (hasResumeProfile) {
        fprintf(file, "resume_stage_setup=%.3f\n", resumeProfile.stageSetupMs);
        fprintf(file, "resume_repair_reference_copies=%.3f\n", resumeProfile.repairReferenceCopiesMs);
        fprintf(file, "resume_prepare_runtime_reference_files=%.3f\n",
                resumeProfile.prepareReferenceFilesMs);
        fprintf(file, "resume_remove_transient_data_shots=%.3f\n",
                resumeProfile.removeTransientShotsMs);
        fprintf(file, "resume_apply_cell_progress=%.3f\n", resumeProfile.applyCellProgressMs);
        fprintf(file, "resume_collect_composite_folders=%.3f\n",
                resumeProfile.collectCompositeFoldersMs);
        fprintf(file, "resume_validate_special_folders=%.3f\n",
                resumeProfile.validateSpecialFoldersMs);
    }
    fflush(file);
    fclose(file);
}

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
/* 編集時は選択エリアの外側を1マスだけ表示する。エリアは一続きの
   ステージであり、この余白はカメラ表示専用で、セル座標や保存形式を
   変更しない。 */
static const float editorMapNeighborMargin = (float)RPG_STAGE_TILE_SIZE;
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
    RpgCellState previousCellState;
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
static bool isGlobalKeyDoorFailureEditing;
static int globalKeyDoorFailureCursorIndex;
static int globalKeyDoorFailureSelectionAnchor;
static int globalKeyDoorFailureSelectionEnd;
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
    /* The generation budget label and its input occupy separate rows. */
    ReserveInspectorContent(&reservation, 122.0f, 18.0f);
    ReserveInspectorContent(&reservation, 142.0f, 26.0f);
    return FinishInspectorReservation(&reservation, 14.0f);
}

static float MeasureGlobalRuntimeSettingsContent(void)
{
    RpgEditorInspectorReservation reservation = BeginInspectorReservation(0.0f);
    ReserveInspectorContent(&reservation, 6.0f, 18.0f);
    ReserveInspectorContent(&reservation, 28.0f, 26.0f);
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
    ReserveInspectorContent(&reservation, 140.0f, 26.0f);
    ReserveInspectorContent(&reservation, 158.0f, 26.0f);
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
    /* The two brightness labels use their own rows above the +/- controls. */
    ReserveInspectorContent(&reservation, 220.0f, 28.0f);
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
/* The debounce candidate is deliberately separate from the last published
   snapshot.  Comparing only with the published data makes an unchanged edit
   look "new" on every 200 ms observation and postpones publication forever. */
static RpgStageData pendingStageFolderSync;
static bool hasPendingStageFolderSync;
static int pendingStageFolderSyncNumber;
/* These are full-world comparison workspaces.  Keeping them static is
   deliberate: the idle stage-folder synchronizer runs after startup and a
   RpgStageData is too large for the editor thread stack. */
static RpgStageData stageDataComparisonFirst;
static RpgStageData stageDataComparisonSecond;
/* A full stage contains the world-sized reference-path matrix.  Comparing a
   fresh copy every render frame is needless work while the editor is idle. */
static double nextStageFolderComparisonTime;
/* The editor preview is generated from this isolated copy.  Build preparation
   rewrites runtime reference paths, so it must never mutate the edit model. */
static RpgStage editorPreviewBuildStage;
/* An edit is published as one operation.  Ordinary compact cells are the
   cheap leaf operation; anything that owns a folder remains a map repair.
   Keeping this list separate from the snapshot prevents a terrain brush
   stroke from restarting every area in the preview cache. */
static bool pendingPreviewCompactCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
/* The final stage snapshot has already replaced the old value by the time a
   preview write runs.  Retain the first old type of a coalesced edit so the
   cell writer can remove an old, cell-owned folder before materialising the
   replacement. */
static int pendingPreviewCompactPreviousBlockTypes[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
/* A cell write is only the first half for ownership-bearing edits.  These
   cells receive one final metadata/composite pass after all queued writes
   have completed, so one edit never escalates to rebuilding its whole map. */
static bool pendingPreviewOwnershipCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
static int pendingPreviewRemovedCompositeIds[RPG_COMPOSITE_OBJECT_MAX_COUNT];
static int pendingPreviewRemovedCompositeCount;
/* FILE/FOLDER/PNG are movable object folders rather than grid cells.  Keep
   the first pre-edit records while edits coalesce, then refresh only those
   object directories when the preview worker reaches its commit point. */
static bool pendingPreviewMovableObjectWork;
static RpgReferenceObjects pendingPreviewPreviousReferenceObjects;
static RpgImageObjects pendingPreviewPreviousImageObjects;
static bool editorPreviewHasDirectCellWork;
/* A local folder write can be briefly rejected while Explorer has a handle
   open.  Retry that exact delta a few times, but never leave the editor in an
   infinite "checking cache" state when the failure is permanent. */
static int editorPreviewDirectBatchRetryCount;
static int editorPreviewRepairRetryCount;
/* Keep the reason for an asynchronous preview update until its final commit.
   The readiness flag already waits for these jobs; this state makes that work
   visible instead of leaving the user with only "checking cache". */
enum {
    RPG_EDITOR_PREVIEW_PREPARATION_NONE = 0,
    RPG_EDITOR_PREVIEW_PREPARATION_CELLS = 1 << 0,
    RPG_EDITOR_PREVIEW_PREPARATION_ATTACHMENTS = 1 << 1,
    RPG_EDITOR_PREVIEW_PREPARATION_WIRES = 1 << 2,
    RPG_EDITOR_PREVIEW_PREPARATION_OBJECTS = 1 << 3,
    RPG_EDITOR_PREVIEW_PREPARATION_SIGNALS = 1 << 4,
    RPG_EDITOR_PREVIEW_PREPARATION_STAGE_LAYOUT = 1 << 5,
    RPG_EDITOR_PREVIEW_PREPARATION_RECOVERY = 1 << 6,
    /* Displayed one frame before a synchronous folder repair starts. */
    RPG_EDITOR_PREVIEW_PREPARATION_AREA_REPAIR = 1 << 7
};
static unsigned int editorPreviewPreparationKinds;
static bool editorPreviewAreaRepairAnnounced;
static bool HasPendingPreviewOwnershipCells(void);
/* Temporary, bounded diagnostic for PNG-only preview updates.  The log is
   intentionally append-only and flushed per event so a user can reproduce a
   single inspector edit without changing preview behaviour. */
static void TraceEditorPreviewRoute(const char *event)
{
    char path[1024];
    FILE *file;
    if (event == NULL ||
        snprintf(path, sizeof(path), "%seditor_preview_route.log", GetApplicationDirectory()) <= 0)
        return;
    file = fopen(path, "ab");
    if (file == NULL) return;
    fprintf(file,
            "%.3f %s direct=%d movable=%d static=%d pendingCells=%d pendingOwners=%d\n",
            GetTime(), event,
            editorPreviewHasDirectCellWork ? 1 : 0,
            pendingPreviewMovableObjectWork ? 1 : 0,
            RpgObjectFolders_HasEditorPreviewStaticMutations() ? 1 : 0,
            RpgBuildCellStorage_IsGenerationPending() ? 1 : 0,
            HasPendingPreviewOwnershipCells() ? 1 : 0);
    fflush(file);
    fclose(file);
}
/* A freshly-created preview intentionally completes distant cells over later
   frames.  Its completion is not an authored object edit.  Keep a separate
   baseline marker so the first comparison after that queue drains cannot
   manufacture a movable-object refresh from runtime-only preparation.
   `InvalidatePreparedEditorPreview()` clears this as soon as a real editor
   operation is recorded while the initial queue is still running. */
static bool editorPreviewInitialGenerationBaselinePending;

typedef enum EditorPreviewEditKind {
    RPG_EDITOR_PREVIEW_EDIT_NONE = 0,
    /* A cell, owner cell, object placement, or route topology changed. */
    RPG_EDITOR_PREVIEW_EDIT_CELLS = 1 << 0,
    /* A saved setting changed without directly changing a grid cell. */
    RPG_EDITOR_PREVIEW_EDIT_VARIABLES = 1 << 1
} EditorPreviewEditKind;

/* Keep the public edit kinds deliberately small.  The category and payload below
   are descriptive data for future preview work (targeted regeneration,
   diagnostics, and progress); they do not alter the current invalidation policy. */
typedef enum EditorPreviewEditCategory {
    RPG_EDITOR_PREVIEW_CATEGORY_UNSPECIFIED = 0,
    RPG_EDITOR_PREVIEW_CATEGORY_CELL,
    RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT,
    RPG_EDITOR_PREVIEW_CATEGORY_ROUTE,
    RPG_EDITOR_PREVIEW_CATEGORY_REFERENCE_OBJECT,
    RPG_EDITOR_PREVIEW_CATEGORY_STAGE_SETTING,
    RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT_SETTING,
    RPG_EDITOR_PREVIEW_CATEGORY_STAGE_LAYOUT
} EditorPreviewEditCategory;

typedef struct EditorPreviewEditCell {
    bool isSet;
    int areaX;
    int areaY;
    int cellX;
    int cellY;
} EditorPreviewEditCell;

/* The payload describes the relevant part of one cell.  A block and an
   attachment can coexist in one cell, so a cell edit need not mean that the
   terrain block itself was replaced. */
typedef struct EditorPreviewEditContent {
    int blockType;
    int attachmentId;
    int attachmentType;
    int attachmentSide;
    int routeId;
    int objectId;
} EditorPreviewEditContent;

typedef struct EditorPreviewEditNotification {
    EditorPreviewEditKind kind;
    EditorPreviewEditCategory category;
    /* Identifies a setting, route, or object when this is not a cell edit. */
    int subjectId;
    EditorPreviewEditCell cell;
    EditorPreviewEditContent beforeContent;
    EditorPreviewEditContent afterContent;
} EditorPreviewEditNotification;

#define RPG_EDITOR_PREVIEW_EDIT_NOTIFICATION_LIMIT 32
typedef struct EditorPreviewEditBatch {
    unsigned int kinds;
    int count;
    bool overflowed;
    EditorPreviewEditNotification entries[RPG_EDITOR_PREVIEW_EDIT_NOTIFICATION_LIMIT];
} EditorPreviewEditBatch;

/* Editor tools report only what they changed.  Snapshot comparison remains
   responsible for deciding which exact preview folders/cells must be written.
   The batch is intentionally frame-local: recording a target must never make
   editing slower or change the current cache update behavior. */
static void NotifyEditorPreviewEdit(EditorPreviewEditKind kind,
                                    const EditorPreviewEditNotification *notification,
                                    EditorPreviewEditBatch *batch)
{
    if (batch == NULL || kind == RPG_EDITOR_PREVIEW_EDIT_NONE) return;

    batch->kinds |= (unsigned int)kind;
    if (notification == NULL) return;
    if (batch->count >= RPG_EDITOR_PREVIEW_EDIT_NOTIFICATION_LIMIT) {
        batch->overflowed = true;
        return;
    }

    batch->entries[batch->count] = *notification;
    batch->entries[batch->count].kind = kind;
    batch->count++;
}

static void MarkPreviewOwnershipCell(RpgGridCell cell);
static void MarkEditorPreviewPreparation(unsigned int kinds);

/* A storage column identifies a map slot; notifications retain the persistent
   two-dimensional area coordinate plus the cell position inside that area. */
static void SetEditorPreviewEditCell(EditorPreviewEditNotification *notification,
                                     const RpgStage *stage, RpgGridCell cell)
{
    int mapIndex;
    if (notification == NULL || stage == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return;
    mapIndex = cell.column / RPG_STAGE_COLUMNS;
    if (!RpgStage_IsMapActive(stage, mapIndex)) return;
    notification->cell.isSet = true;
    notification->cell.areaX = stage->mapGridX[mapIndex];
    notification->cell.areaY = stage->mapGridY[mapIndex];
    notification->cell.cellX = cell.column % RPG_STAGE_COLUMNS;
    notification->cell.cellY = cell.row;
}

static EditorPreviewEditContent GetEditorPreviewAttachmentContent(const RpgAttachment *attachment)
{
    if (attachment == NULL) return (EditorPreviewEditContent){ 0 };
    return (EditorPreviewEditContent){
        .attachmentId = attachment->folderId,
        .attachmentType = attachment->type,
        .attachmentSide = attachment->side
    };
}

static void QueueEditorPreviewCellNotifications(const EditorPreviewEditBatch *batch,
                                                const RpgStage *stage)
{
    if (batch == NULL || stage == NULL) return;
    for (int index = 0; index < batch->count; index++) {
        const EditorPreviewEditNotification *notification = &batch->entries[index];
        int mapIndex;
        if ((notification->kind & RPG_EDITOR_PREVIEW_EDIT_CELLS) == 0 ||
            !notification->cell.isSet) continue;
        mapIndex = RpgStage_GetMapAtGrid(stage, notification->cell.areaX,
                                         notification->cell.areaY);
        if (mapIndex < 0 || notification->cell.cellX < 0 ||
            notification->cell.cellX >= RPG_STAGE_COLUMNS ||
            notification->cell.cellY < 0 || notification->cell.cellY >= RPG_STAGE_ROWS)
            continue;
        if (notification->category == RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT) {
            /* An attachment is serialized with its owner cell rather than as
               terrain.  Both independent edits of a drag therefore queue
               their owner metadata without requiring a special move path. */
            MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_ATTACHMENTS);
            MarkPreviewOwnershipCell((RpgGridCell){
                notification->cell.cellY,
                mapIndex * RPG_STAGE_COLUMNS + notification->cell.cellX
            });
        }
    }
}

static void InvalidatePreparedEditorPreview(const EditorPreviewEditBatch *batch,
                                            const RpgStage *stage,
                                            bool *previewBuildDirty,
                                            bool *previewBuildReady,
                                            double *nextComparisonTime,
                                            double *nextSyncTime)
{
    if (batch == NULL || batch->kinds == RPG_EDITOR_PREVIEW_EDIT_NONE || previewBuildDirty == NULL ||
        previewBuildReady == NULL || nextComparisonTime == NULL || nextSyncTime == NULL)
        return;
    /* An actual edit during initial background materialisation must be
       published normally after the queue drains.  Do not replace its
       snapshot with a new baseline at that point. */
    editorPreviewInitialGenerationBaselinePending = false;
    QueueEditorPreviewCellNotifications(batch, stage);
    *previewBuildDirty = true;
    *previewBuildReady = false;
    *nextComparisonTime = 0.0;
    *nextSyncTime = GetTime();
}

static void MarkEditorPreviewPreparation(unsigned int kinds)
{
    if (kinds == RPG_EDITOR_PREVIEW_PREPARATION_NONE) return;
    editorPreviewPreparationKinds |= kinds;
}

static void FinishEditorPreviewPreparationDisplay(void)
{
    editorPreviewPreparationKinds = RPG_EDITOR_PREVIEW_PREPARATION_NONE;
    editorPreviewAreaRepairAnnounced = false;
}
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
static int attachmentCapacityCursorIndex;
static int attachmentCapacitySelectionAnchor;
static int attachmentCapacitySelectionEnd;
static bool isAttachmentSpeedEditing;
static char attachmentSpeedInput[24];
static int attachmentSpeedCursorIndex;
static int attachmentSpeedSelectionAnchor;
static int attachmentSpeedSelectionEnd;
/* ステージ単位の Zipper 容量上限。既存の数値入力と同じく Enter で確定する。 */
static bool isZipperCapacityEditing;
static char zipperCapacityInput[16];
static bool isStageNameEditing;
static char stageNameInput[RPG_STAGE_NAME_LENGTH];
static int stageNameCursorIndex;
static int stageNameSelectionAnchor;
static int stageNameSelectionEnd;
static bool isBuildReadCountEditing;
static char buildReadCountInput[16];

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
static int effectBlockDragPreviewGrabRowOffset;
static int effectBlockDragPreviewGrabColumnOffset;
static bool isMagnetResizePreviewVisible;
static int magnetResizePreviewRootRow = -1;
static int magnetResizePreviewRootColumn = -1;
static int magnetResizeEdges;
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
    /* Area layouts are saved independently from the player layout.  Older
       stages can therefore retain a player coordinate from a former area
       grid.  Keep its local position, but put it in the first active area so
       the editor has a concrete map in which to draw and edit the player. */
    if (RpgStage_GetMapAtWorldPosition(stage, layout->playerPosition) < 0) {
        const int playerMap = RpgStage_FindNearestActiveMap(stage, 0);
        if (playerMap >= 0) {
            const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
            const float localX = layout->playerPosition.x -
                                 floorf(layout->playerPosition.x / mapWidth) * mapWidth;
            const float localY = layout->playerPosition.y -
                                 floorf(layout->playerPosition.y / mapHeight) * mapHeight;
            layout->playerPosition = (Vector2){
                stage->mapGridX[playerMap] * mapWidth + localX,
                -stage->mapGridY[playerMap] * mapHeight + localY
            };
        }
    }
    /* Static block object folders use authored area/cell coordinates.  This
       only renames the retired r/c/type folder names; stage data is unchanged. */
    (void)RpgStaticObjectDrop_MigrateLegacyFolders(stageNumber, stage);
    RegisterStageFilePickerText(stage);
    *items = RpgItems_Default();
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
    if (!RpgAttachments_Clone(&attachments, &stageLoadBuffer.attachments)) return false;
    RpgReceivers_Bind(&receivers, &attachments);
    signalBlocks = stageLoadBuffer.signalBlocks;
    mapEvents = stageLoadBuffer.mapEvents;
    npcInspectData = stageLoadBuffer.npcInspectData;
    activeInspect = &npcInspectData;
    *player = RpgCharacter_Create(layout->playerPosition, BLUE, BROWN);
    RpgCharacter_SetUsesPlayerSpriteCollision(player, true);
    player->moveSpeed = layout->playerMoveSpeed;
    player->scale = layout->playerScale;
    player->jumpHeight = layout->playerJumpHeight;
    *npc = RpgCharacter_Create(layout->npcPosition, PURPLE, DARKBROWN);
    npc->scale = layout->npcScale;
    /* NPC・Zipperは現行ステージから外す。構造体・スクリプトは将来の復元用に残す。 */
    npc->position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
    previewStage = stage;
    RpgObjectFolders_ClearSessionStorage();
    RpgObjectFolders_PrepareBlockOwnedMetadata(&attachments, &receivers, &wires);
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
        "導線遅延", "磁石速度", "移動速度", "拡大率", "ジャンプ高さ", "マス-", "マス+", "px-", "px+", "フォルダ返却", "返却アニメーション遅延",
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
    GameFont_AddText("鍵ドア失敗時の文章");
    GameFont_AddText("パレット設定");
    GameFont_AddText("パレット");
    GameFont_AddText("囲い色");
    GameFont_AddText("編集");
    GameFont_AddText("白");
    GameFont_AddText("赤");
    GameFont_AddText("青");
    GameFont_AddText("黄");
    /* Fixed text used by the upper-right background build progress panel. */
    GameFont_AddText("ロード: 0 / 0  完了");
    GameFont_AddText("処理中: エリア (0, 0) のマス (0, 0) を生成中");
    GameFont_AddText("処理中: 生成データを保存中");
    GameFont_AddText("処理中: エリア修復中");
    GameFont_AddText("処理: 生成完了");
    GameFont_AddText("処理中: エリア構成を更新中");
    GameFont_AddText("処理中: 導線・アタッチメントを更新中");
    GameFont_AddText("処理中: 導線・軌道を更新中");
    GameFont_AddText("処理中: アタッチメント設定を更新中");
    GameFont_AddText("処理中: オブジェクト設定を更新中");
    GameFont_AddText("処理中: 信号ブロック設定を更新中");
    GameFont_AddText("処理中: セルを更新中");
    GameFont_AddText("処理中: プレビューキャッシュを修復中");
    GameFont_AddText("処理中: プレイキャッシュを準備中");
    GameFont_AddText("プレイ前: 読込済み");
    GameFont_AddText("プレイ前: 残りのマスを読込中");
    GameFont_AddText("プレイ前: キャッシュを確認中");
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

/* The editor stores only authoring fields.  Socket activity, spatial lookup
   state, and movable-reference animation belong exclusively to Play and must
   never turn into an editor "save changes?" prompt. */
static bool AreStaticStagesDifferent(const RpgStage *left, const RpgStage *right)
{
    if (left == NULL || right == NULL) return left != right;
    if (memcmp(left->mapActive, right->mapActive, sizeof(left->mapActive)) != 0 ||
        memcmp(left->mapGridX, right->mapGridX, sizeof(left->mapGridX)) != 0 ||
        memcmp(left->mapGridY, right->mapGridY, sizeof(left->mapGridY)) != 0 ||
        memcmp(left->blocks, right->blocks, sizeof(left->blocks)) != 0 ||
        memcmp(left->cellStates, right->cellStates, sizeof(left->cellStates)) != 0 ||
        memcmp(left->referencePaths, right->referencePaths, sizeof(left->referencePaths)) != 0 ||
        left->keyDoorCount != right->keyDoorCount ||
        memcmp(left->keyDoors, right->keyDoors, sizeof(left->keyDoors)) != 0 ||
        memcmp(&left->imageObjects, &right->imageObjects, sizeof(left->imageObjects)) != 0 ||
        left->referenceObjects.count != right->referenceObjects.count ||
        left->referenceObjects.nextId != right->referenceObjects.nextId) return true;
    for (int index = 0; index < left->referenceObjects.count; index++) {
        const RpgReferenceObject *a = &left->referenceObjects.entries[index];
        const RpgReferenceObject *b = &right->referenceObjects.entries[index];
        if (a->id != b->id || a->objectKind != b->objectKind ||
            a->position.x != b->position.x || a->position.y != b->position.y ||
            a->legacySourceRow != b->legacySourceRow || a->legacySourceColumn != b->legacySourceColumn ||
            strcmp(a->sourcePath, b->sourcePath) != 0 || strcmp(a->path, b->path) != 0) return true;
    }
    return false;
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
    if (AreStaticStagesDifferent(&snapshot->stage, stage)) {
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

/* 通常ドラッグは見た目もマス中心へ合わせ、Alt中だけカーソル位置をそのまま使う。 */
static Vector2 GetImageObjectDragPreviewPointer(Vector2 pointer)
{
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) return pointer;
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

static Vector2 GetEditorMapWorldOrigin(const RpgStage *stage, int mapIndex);

/* Character dragging must use the same connected-world origin as the editor
 * camera and its ghost.  Storage-slot indices are not world coordinates once
 * areas have been moved on the 2D map. */
static void MoveCharacterToEditorPointer(RpgCharacter *character, const RpgStage *stage,
                                         int mapIndex, Vector2 pointer)
{
    if (character == NULL || stage == NULL) return;
    Vector2 destination = GetCharacterDragPreviewPointer(pointer);
    Vector2 origin = GetEditorMapWorldOrigin(stage, mapIndex);
    character->position = Vector2Add(destination, origin);
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
    if (flag->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG ||
        RpgAttachments_IsRuntimeUnavailable(flag)) return false;
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

/* Ctrl+P starts at this editor area's stable first save flag.  The chosen
 * attachment index is then fed into the same flag-start request as the
 * inspector button; it does not create a second play initialisation route. */
static int FindAreaSaveFlagForEditorPlay(const RpgAttachments *attachments, const RpgStage *stage,
                                         int mapIndex)
{
    int selectedIndex = -1;
    int selectedId = INT_MAX;
    if (attachments == NULL || stage == NULL || !RpgStage_IsMapActive(stage, mapIndex)) return -1;
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        Vector2 respawn;
        int flagMapIndex;
        if (attachment->type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG ||
            RpgAttachments_IsRuntimeUnavailable(attachment) ||
            !GetSaveFlagEditorPlayStart(attachments, stage, index, &respawn, &flagMapIndex) ||
            flagMapIndex != mapIndex) continue;
        if (attachment->folderId < selectedId) {
            selectedIndex = index;
            selectedId = attachment->folderId;
        }
    }
    return selectedIndex;
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
    (void)items;
    int writeIndex = 0;
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
    RpgShooterConfig *shooter = RpgAttachments_FindShooter(attachmentList,
        attachmentList->entries[attachmentIndex].folderId);
    if (shooter == NULL) return false;
    RpgGridPath *path = &shooter->path;
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

static bool IsCharacterInMap(const RpgCharacter *character, const RpgStage *stage, int mapIndex)
{
    return character != NULL && stage != NULL &&
           RpgStage_GetMapAtWorldPosition(stage, character->position) == mapIndex;
}

static RpgCharacter GetLocalCharacter(const RpgCharacter *character, const RpgStage *stage, int mapIndex)
{
    RpgCharacter localCharacter = *character;
    Vector2 origin = GetEditorMapWorldOrigin(stage, mapIndex);
    localCharacter.position.x -= origin.x;
    localCharacter.position.y -= origin.y;
    return localCharacter;
}

/* Position-based records are not stored inside an area's cell array.  Capture
 * their owning slot before a grid edit, then apply that slot's exact origin
 * delta afterwards.  This also handles the origin normalization performed by
 * a move to an empty cell. */
typedef struct EditorAreaOriginSnapshot {
    bool active[RPG_STAGE_MAP_COUNT];
    Vector2 origin[RPG_STAGE_MAP_COUNT];
} EditorAreaOriginSnapshot;

static EditorAreaOriginSnapshot CaptureEditorAreaOrigins(const RpgStage *stage)
{
    EditorAreaOriginSnapshot snapshot = { 0 };
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        snapshot.active[index] = RpgStage_IsMapActive(stage, index);
        if (snapshot.active[index]) snapshot.origin[index] = GetEditorMapWorldOrigin(stage, index);
    }
    return snapshot;
}

static void TranslatePositionWithAreaOriginDelta(Vector2 *position, int owner,
                                                  const EditorAreaOriginSnapshot *before,
                                                  const RpgStage *after)
{
    if (position == NULL || before == NULL || owner < 0 || owner >= RPG_STAGE_MAP_COUNT ||
        !before->active[owner] || !RpgStage_IsMapActive(after, owner)) return;
    *position = Vector2Add(*position,
                           Vector2Subtract(GetEditorMapWorldOrigin(after, owner), before->origin[owner]));
}

static bool RelocateGlobalMapAreas(RpgStage *stage, RpgLayout *layout,
                                   RpgCharacter *player, RpgCharacter *npc,
                                   RpgZipper *zipper, RpgItems *items,
                                   RpgMapEvents *events, int firstMapIndex,
                                   int secondMapIndex, bool swap)
{
    int referenceOwners[RPG_REFERENCE_OBJECT_MAX_COUNT];
    int eventOwners[RPG_MAP_EVENT_MAX_COUNT];
    int itemOwners[RPG_ITEM_MAX_COUNT];
    int playerOwner, npcOwner, zipperOwner, layoutPlayerOwner, layoutNpcOwner;
    EditorAreaOriginSnapshot before;
    if (stage == NULL || !RpgStage_IsMapActive(stage, firstMapIndex) ||
        (swap && (!RpgStage_IsMapActive(stage, secondMapIndex) || firstMapIndex == secondMapIndex))) return false;
    before = CaptureEditorAreaOrigins(stage);
    for (int index = 0; index < stage->referenceObjects.count; index++)
        referenceOwners[index] = RpgStage_GetMapAtWorldPosition(stage, stage->referenceObjects.entries[index].position);
    if (events != NULL)
        for (int index = 0; index < events->count; index++)
            eventOwners[index] = RpgStage_GetMapAtWorldPosition(stage, events->entries[index].position);
    if (items != NULL)
        for (int index = 0; index < items->count; index++)
            itemOwners[index] = RpgStage_GetMapAtWorldPosition(stage, items->entries[index].position);
    playerOwner = player == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, player->position);
    npcOwner = npc == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, npc->position);
    zipperOwner = zipper == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, zipper->character.position);
    layoutPlayerOwner = layout == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, layout->playerPosition);
    layoutNpcOwner = layout == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, layout->npcPosition);
    if (!swap || !RpgStage_SwapMapGridPositions(stage, firstMapIndex, secondMapIndex)) return false;
    for (int index = 0; index < stage->referenceObjects.count; index++)
        TranslatePositionWithAreaOriginDelta(&stage->referenceObjects.entries[index].position,
                                             referenceOwners[index], &before, stage);
    for (int index = 0; index < stage->imageObjects.count; index++) {
        RpgImageObject *object = &stage->imageObjects.entries[index];
        int owner = object->column / RPG_STAGE_COLUMNS;
        if (object->hasCustomPosition) {
            Vector2 position = { object->positionX, object->positionY };
            TranslatePositionWithAreaOriginDelta(&position, owner, &before, stage);
            object->positionX = position.x; object->positionY = position.y;
        }
        if (object->hasRuntimePosition) {
            Vector2 position = { object->runtimeX, object->runtimeY };
            TranslatePositionWithAreaOriginDelta(&position, owner, &before, stage);
            object->runtimeX = position.x; object->runtimeY = position.y;
        }
    }
    if (events != NULL)
        for (int index = 0; index < events->count; index++)
            TranslatePositionWithAreaOriginDelta(&events->entries[index].position, eventOwners[index], &before, stage);
    if (items != NULL)
        for (int index = 0; index < items->count; index++)
            TranslatePositionWithAreaOriginDelta(&items->entries[index].position, itemOwners[index], &before, stage);
    if (player != NULL) TranslatePositionWithAreaOriginDelta(&player->position, playerOwner, &before, stage);
    if (npc != NULL) TranslatePositionWithAreaOriginDelta(&npc->position, npcOwner, &before, stage);
    if (zipper != NULL) TranslatePositionWithAreaOriginDelta(&zipper->character.position, zipperOwner, &before, stage);
    if (layout != NULL) {
        TranslatePositionWithAreaOriginDelta(&layout->playerPosition, layoutPlayerOwner, &before, stage);
        TranslatePositionWithAreaOriginDelta(&layout->npcPosition, layoutNpcOwner, &before, stage);
    }
    return true;
}

static bool SwapGlobalMapAreas(RpgStage *stage, RpgLayout *layout,
                               RpgCharacter *player, RpgCharacter *npc,
                               RpgZipper *zipper, RpgItems *items,
                               RpgMapEvents *events, int firstMapIndex, int secondMapIndex)
{
    return RelocateGlobalMapAreas(stage, layout, player, npc, zipper, items, events,
                                  firstMapIndex, secondMapIndex, true);
}

static bool MoveGlobalMapAreaToGrid(RpgStage *stage, RpgLayout *layout,
                                    RpgCharacter *player, RpgCharacter *npc,
                                    RpgZipper *zipper, RpgItems *items,
                                    RpgMapEvents *events, int mapIndex, int gridX, int gridY)
{
    /* Reuse the same move transaction while preserving the existing public
       stage API's two-coordinate signature. */
    EditorAreaOriginSnapshot before;
    int referenceOwners[RPG_REFERENCE_OBJECT_MAX_COUNT];
    int eventOwners[RPG_MAP_EVENT_MAX_COUNT];
    int itemOwners[RPG_ITEM_MAX_COUNT];
    int playerOwner, npcOwner, zipperOwner, layoutPlayerOwner, layoutNpcOwner;
    if (stage == NULL || !RpgStage_IsMapActive(stage, mapIndex) ||
        RpgStage_GetMapAtGrid(stage, gridX, gridY) >= 0) return false;
    before = CaptureEditorAreaOrigins(stage);
    for (int index = 0; index < stage->referenceObjects.count; index++) referenceOwners[index] = RpgStage_GetMapAtWorldPosition(stage, stage->referenceObjects.entries[index].position);
    if (events != NULL) for (int index = 0; index < events->count; index++) eventOwners[index] = RpgStage_GetMapAtWorldPosition(stage, events->entries[index].position);
    if (items != NULL) for (int index = 0; index < items->count; index++) itemOwners[index] = RpgStage_GetMapAtWorldPosition(stage, items->entries[index].position);
    playerOwner = player == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, player->position);
    npcOwner = npc == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, npc->position);
    zipperOwner = zipper == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, zipper->character.position);
    layoutPlayerOwner = layout == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, layout->playerPosition);
    layoutNpcOwner = layout == NULL ? -1 : RpgStage_GetMapAtWorldPosition(stage, layout->npcPosition);
    if (!RpgStage_MoveMapToGrid(stage, mapIndex, gridX, gridY)) return false;
    for (int index = 0; index < stage->referenceObjects.count; index++) TranslatePositionWithAreaOriginDelta(&stage->referenceObjects.entries[index].position, referenceOwners[index], &before, stage);
    for (int index = 0; index < stage->imageObjects.count; index++) {
        RpgImageObject *object = &stage->imageObjects.entries[index]; int owner = object->column / RPG_STAGE_COLUMNS;
        if (object->hasCustomPosition) { Vector2 position = { object->positionX, object->positionY }; TranslatePositionWithAreaOriginDelta(&position, owner, &before, stage); object->positionX = position.x; object->positionY = position.y; }
        if (object->hasRuntimePosition) { Vector2 position = { object->runtimeX, object->runtimeY }; TranslatePositionWithAreaOriginDelta(&position, owner, &before, stage); object->runtimeX = position.x; object->runtimeY = position.y; }
    }
    if (events != NULL) for (int index = 0; index < events->count; index++) TranslatePositionWithAreaOriginDelta(&events->entries[index].position, eventOwners[index], &before, stage);
    if (items != NULL) for (int index = 0; index < items->count; index++) TranslatePositionWithAreaOriginDelta(&items->entries[index].position, itemOwners[index], &before, stage);
    if (player != NULL) TranslatePositionWithAreaOriginDelta(&player->position, playerOwner, &before, stage);
    if (npc != NULL) TranslatePositionWithAreaOriginDelta(&npc->position, npcOwner, &before, stage);
    if (zipper != NULL) TranslatePositionWithAreaOriginDelta(&zipper->character.position, zipperOwner, &before, stage);
    if (layout != NULL) { TranslatePositionWithAreaOriginDelta(&layout->playerPosition, layoutPlayerOwner, &before, stage); TranslatePositionWithAreaOriginDelta(&layout->npcPosition, layoutNpcOwner, &before, stage); }
    return true;
}

typedef enum EditorMapObjectHit {
    EDITOR_MAP_OBJECT_HIT_NONE,
    EDITOR_MAP_OBJECT_HIT_IMAGE,
    EDITOR_MAP_OBJECT_HIT_PLAYER,
    EDITOR_MAP_OBJECT_HIT_NPC
} EditorMapObjectHit;

/* 描画順を入力順にも使う。中心マスで所属エリアを決めつつ、選択だけは実際の描画範囲で行う。 */
static int FindImageObjectAtVisualPosition(const RpgStage *stage, const RpgImageObjects *objects, int mapIndex,
                                           Vector2 mapPosition, RpgImageObjectLayer layer)
{
    if (objects == NULL) return -1;
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int index = objects->count - 1; index >= 0; index--) {
        const RpgImageObject *object = &objects->entries[index];
        if (object->layer != layer || object->column < firstColumn ||
            object->column >= firstColumn + RPG_STAGE_COLUMNS) continue;
        if (CheckCollisionPointRec(mapPosition,
                                   RpgImageObjects_GetLocalBoundsAtWorldOrigin(
                                       object, mapIndex, RPG_STAGE_COLUMNS,
                                       GetEditorMapWorldOrigin(stage, mapIndex),
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
    int hitImage = FindImageObjectAtVisualPosition(stage, &stage->imageObjects, mapIndex, mapPosition,
                                                    RPG_IMAGE_OBJECT_LAYER_FRONT);
    if (hitImage >= 0) {
        if (imageIndex != NULL) *imageIndex = hitImage;
        return EDITOR_MAP_OBJECT_HIT_IMAGE;
    }
    if (IsCharacterInMap(player, stage, mapIndex)) {
        RpgCharacter localPlayer = GetLocalCharacter(player, stage, mapIndex);
        if (IsCharacterClicked(&localPlayer, mapPosition)) return EDITOR_MAP_OBJECT_HIT_PLAYER;
    }
    if (IsCharacterInMap(npc, stage, mapIndex)) {
        RpgCharacter localNpc = GetLocalCharacter(npc, stage, mapIndex);
        if (IsCharacterClicked(&localNpc, mapPosition)) return EDITOR_MAP_OBJECT_HIT_NPC;
    }
    hitImage = FindImageObjectAtVisualPosition(stage, &stage->imageObjects, mapIndex, mapPosition,
                                                RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
    if (hitImage >= 0) {
        if (imageIndex != NULL) *imageIndex = hitImage;
        return EDITOR_MAP_OBJECT_HIT_IMAGE;
    }
    hitImage = FindImageObjectAtVisualPosition(stage, &stage->imageObjects, mapIndex, mapPosition,
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
    /* Type-specific attachment modules are the only authored source of truth.
       The generic record is now a read-only compatibility facade for legacy
       drawing and hit-test code, so publication must never copy stale facade
       values back over a canonical shooter/flag/socket configuration. */
    RpgAttachments_RefreshEditorFacades(&attachments);
    memset(destination, 0, sizeof(*destination));
    destination->layout = *layout;
    destination->stage = *stage;
    destination->dialogue = *dialogue;
    destination->stage3Event = *stage3Event;
    destination->areaEntryEvents = areaEntryEvents;
    destination->npcInspectData = npcInspectData;
    destination->wires = wires;
    (void)RpgAttachments_Clone(&destination->attachments, &attachments);
    RpgReceivers_Bind(&destination->receivers, &destination->attachments);
    destination->signalBlocks = signalBlocks;
    destination->mapEvents = mapEvents;
}

/* RpgAttachments owns heap storage.  Stage-data snapshots therefore need an
 * explicit deep copy instead of a structure assignment. */
static bool CopyEditorStageData(RpgStageData *destination, const RpgStageData *source)
{
    RpgAttachments copied = RpgAttachments_Default();
    if (destination == NULL || source == NULL ||
        !RpgAttachments_Clone(&copied, &source->attachments)) return false;
    RpgAttachments_Destroy(&destination->attachments);
    *destination = *source;
    destination->attachments = copied;
    RpgReceivers_Bind(&destination->receivers, &destination->attachments);
    return true;
}

static bool AreReferenceObjectPreviewDefinitionsDifferent(const RpgReferenceObjects *left,
                                                          const RpgReferenceObjects *right);
static bool AreImageObjectPreviewDefinitionsDifferent(const RpgImageObjects *left,
                                                      const RpgImageObjects *right);

static bool IsEditorStageDataDifferent(const RpgStageData *first, const RpgStageData *second)
{
    if (first == NULL || second == NULL) return first != second;
    if (AreAttachmentsDifferent(&first->attachments, &second->attachments)) return true;
    if (AreReferenceObjectPreviewDefinitionsDifferent(&first->stage.referenceObjects,
                                                      &second->stage.referenceObjects) ||
        AreImageObjectPreviewDefinitionsDifferent(&first->stage.imageObjects,
                                                  &second->stage.imageObjects))
        return true;
    stageDataComparisonFirst = *first;
    stageDataComparisonSecond = *second;
    stageDataComparisonFirst.attachments = RpgAttachments_Default();
    stageDataComparisonSecond.attachments = RpgAttachments_Default();
    /* Receiver is only a view of attachments; its address must not make the
       static-preview comparison dirty every frame. */
    stageDataComparisonFirst.receivers = RpgReceivers_Default();
    stageDataComparisonSecond.receivers = RpgReceivers_Default();
    /* These arrays contain runtime fields and unused fixed-capacity slots;
       they were compared semantically above. */
    stageDataComparisonFirst.stage.referenceObjects = RpgReferenceObjects_Default();
    stageDataComparisonSecond.stage.referenceObjects = RpgReferenceObjects_Default();
    stageDataComparisonFirst.stage.imageObjects = RpgImageObjects_Default();
    stageDataComparisonSecond.stage.imageObjects = RpgImageObjects_Default();
    return memcmp(&stageDataComparisonFirst, &stageDataComparisonSecond,
                  sizeof(stageDataComparisonFirst)) != 0;
}

/* Ordinary attachments live in the already materialised owner-block folder.
   Their change does not alter cell storage, terrain collision, or the area
   cache.  Treat this separately from a map edit so adding a flag/socket/etc.
   cannot trigger a destructive 20 x 12 cell rebuild. */
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
        RpgFlagConfig *flag = RpgAttachments_FindFlag(&data->attachments, attachment->folderId);
        RpgShooterConfig *shooter = RpgAttachments_FindShooter(&data->attachments, attachment->folderId);
        if (flag != NULL) flag->raised = false;
        if (shooter != NULL) shooter->animationElapsed = 0.0f;
        attachment->isZipperHeld = false;
        attachment->isOwnerBlockZipperHeld = false;
    }
    for (int index = 0; index < data->signalBlocks.count; index++) {
        data->signalBlocks.entries[index].activeRemaining = 0.0f;
        data->signalBlocks.entries[index].previewRemaining = 0.0f;
    }
    data->signalBlocks.lastSignalSequence = 0;
    for (int index = 0; index < data->wires.count; index++)
        data->wires.entries[index].conveyorPreviewElapsed = 0.0f;
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
    RpgStageAuthority previousAuthority;
    if (layout == NULL || stage == NULL || stageNumber <= 0) return false;
    previousAuthority = RpgStageAuthority_Get();
    RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_RUNTIME);
    editorPreviewBuildStage = *stage;
    built = RpgStageBuild_CreateEditorPreview(stageNumber, &editorPreviewBuildStage, &attachments,
                                               &receivers, &wires, layout->playerPosition);
    /* Keep the preview build path while the editor is open: it owns the
       low-priority remaining-area queue.  RpgStageBuild_Close() still stops
       its watcher, so no filesystem watcher runs during editing. */
    RpgStageBuild_Close();
    RpgStageAuthority_Enter(previousAuthority);
    return built;
}

/* This is the one authoritative editor-preview publication path.  Startup's
 * idle publication, Stop, and editor-close recovery all use it, so adding a
 * required preview artifact to Stop cannot leave the initial preview behind.
 * It deliberately prepares only the disposable editor runtime cache; it does
 * not alter the saved static stage or the live play runtime. */
static bool RebuildEditorPreviewCache(int stageNumber, const RpgLayout *layout,
                                      const RpgStage *stage)
{
    if (!BuildEditorPreviewCache(stageNumber, layout, stage)) return false;
    RpgObjectFolders_PrepareBlockOwnedMetadata(&attachments, &receivers, &wires);
    RpgObjectFolder_PrepareZipperAnimationCommand();
    /* Creation has just written the complete authoritative preview baseline.
       It must not inherit the global "static files changed" flag used only
       after a running Play moves an object folder. */
    RpgObjectFolders_ConfirmEditorPreviewBaseline();
    return true;
}

/* The sole editor-preview publication route.  Startup has no existing cache
 * so it naturally falls through to creation; Stop repairs its existing cache
 * first.  Thus any future Stop-side preparation automatically applies to the
 * startup path as well. */
static bool EnsureEditorPreviewCache(int stageNumber, const RpgLayout *layout,
                                     const RpgStage *stage)
{
    RpgStageAuthority previousAuthority;
    bool repaired;
    if (layout == NULL || stage == NULL) return false;
    previousAuthority = RpgStageAuthority_Get();
    RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_RUNTIME);
    repaired = RpgStageBuild_RepairEditorPreview(stage, &attachments, &receivers, &wires,
                                                  layout->playerPosition);
    RpgStageAuthority_Enter(previousAuthority);
    if (repaired) {
        editorPreviewBuildStage = *stage;
        /* Keep the repaired editor preview attached.  RpgStageBuild_Close()
           already closes its watcher; retaining the build path lets the next
           edit repair this cache instead of discarding it and recreating the
           complete Stage/editor folder tree. */
        RpgObjectFolders_ConfirmEditorPreviewBaseline();
        return true;
    }
    RpgObjectFolders_AbandonStageBuild();
    return RebuildEditorPreviewCache(stageNumber, layout, stage);
}

/* The preview tree is partitioned by map.  Non-compact edits used to call the
   legacy no-location notifier, which conservatively dirtied every active map.
   Keep that fallback solely for a topology change; ordinary block, attachment
   and object edits can name their owning map exactly. */
static void MarkPreviewMapForColumn(int column)
{
    if (column >= 0 && column < RPG_STAGE_WORLD_COLUMNS)
        RpgObjectFolders_MarkEditorPreviewMapMutation(column / RPG_STAGE_COLUMNS);
}

static bool CanUsePreviewDirectCellStorage(int blockType)
{
    /* The compact cell writer owns both forms of independent cell storage:
       ordinary terrain is written into cells.csv and a folder-backed block
       is written through writeCellFolder.  Reference FILE/FOLDER cells are
       deliberately excluded: their external payload is not cell-owned and
       must remain on the reference-object update path.  Composite cells are
       queued separately before this test, together with every component. */
    return !RpgBlockInventory_IsReferenceObject(blockType);
}

static bool CanQueuePreviewLocalCellChange(const RpgStageData *before,
                                           const RpgStageData *after,
                                           int row, int column)
{
    if (before == NULL || after == NULL || row < 0 || row >= RPG_STAGE_ROWS ||
        column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    /* A direct update owns the cell record itself.  This includes a
       folder-backed single-cell block: the compact writer removes the former
       folder, rewrites object_info.txt when necessary, and updates cells.csv
       in the same batch.  Attachment/route metadata is still queued by its
       dedicated ownership batch below; it is not a reason to rebuild the
       complete area. */
    return strcmp(before->stage.referencePaths[row][column], after->stage.referencePaths[row][column]) == 0 &&
           CanUsePreviewDirectCellStorage(before->stage.blocks[row][column]) &&
           CanUsePreviewDirectCellStorage(after->stage.blocks[row][column]);
}

static void QueuePreviewCellUpdate(int row, int column, int previousBlockType)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return;
    if (!pendingPreviewCompactCells[row][column]) {
        pendingPreviewCompactCells[row][column] = true;
        pendingPreviewCompactPreviousBlockTypes[row][column] = previousBlockType;
    }
}

static void MarkPreviewOwnershipCell(RpgGridCell cell)
{
    if (cell.row >= 0 && cell.row < RPG_STAGE_ROWS &&
        cell.column >= 0 && cell.column < RPG_STAGE_WORLD_COLUMNS)
        pendingPreviewOwnershipCells[cell.row][cell.column] = true;
}

static void RememberPreviewRemovedCompositeId(int id)
{
    if (id <= 0) return;
    for (int index = 0; index < pendingPreviewRemovedCompositeCount; index++)
        if (pendingPreviewRemovedCompositeIds[index] == id) return;
    if (pendingPreviewRemovedCompositeCount < (int)(sizeof(pendingPreviewRemovedCompositeIds) /
                                                     sizeof(pendingPreviewRemovedCompositeIds[0])))
        pendingPreviewRemovedCompositeIds[pendingPreviewRemovedCompositeCount++] = id;
}

static bool IsCompositeDefinitionChangedForId(const RpgStage *before,
                                              const RpgStage *after, int id)
{
    const RpgCompositeObject *beforeObject = NULL, *afterObject = NULL;
    if (before == NULL || after == NULL || id <= 0) return false;
    for (int index = 0; index < before->compositeObjectCount; index++)
        if (before->compositeObjects[index].id == id) beforeObject = &before->compositeObjects[index];
    for (int index = 0; index < after->compositeObjectCount; index++)
        if (after->compositeObjects[index].id == id) afterObject = &after->compositeObjects[index];
    return beforeObject == NULL || afterObject == NULL ||
           memcmp(beforeObject, afterObject, sizeof(*beforeObject)) != 0;
}

/* A composite's runtime lookup grid is a cache.  Do not use that cache to
   decide the editor-preview delta: during a move/resize it is precisely the
   old and new component cells that changed.  Enumerate the persistent object
   definition instead, so both sides of a multi-cell move are always queued. */
static bool QueuePreviewCompositeDefinitionCells(const RpgStage *definition,
                                                 const RpgStage *previous,
                                                 int id)
{
    if (definition == NULL || previous == NULL || id <= 0) return false;
    for (int index = 0; index < definition->compositeObjectCount; index++) {
        const RpgCompositeObject *object = &definition->compositeObjects[index];
        RpgGridCell cells[RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS];
        int cellCount = 0;
        if (object->id != id) continue;
        if (!RpgStage_GetCompositeCellsAtCell(definition, object->originRow,
                                              object->originColumn, cells,
                                              RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS,
                                              &cellCount)) return false;
        for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
            const int row = cells[cellIndex].row;
            const int column = cells[cellIndex].column;
            if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
                column >= RPG_STAGE_WORLD_COLUMNS) continue;
            QueuePreviewCellUpdate(row, column, previous->blocks[row][column]);
            MarkPreviewOwnershipCell((RpgGridCell){ row, column });
        }
        return true;
    }
    return false;
}

static void QueuePreviewCompositeCellBatch(const RpgStage *before, const RpgStage *after)
{
    if (before == NULL || after == NULL) return;
    /* The old definition supplies cells that must be removed; the new one
       supplies cells that must be created.  Queuing both makes a resize and
       a cross-area drag a small set of cell changes, never a map rebuild. */
    for (int index = 0; index < before->compositeObjectCount; index++) {
        const int id = before->compositeObjects[index].id;
        if (!IsCompositeDefinitionChangedForId(before, after, id)) continue;
        RememberPreviewRemovedCompositeId(id);
        (void)QueuePreviewCompositeDefinitionCells(before, before, id);
        (void)QueuePreviewCompositeDefinitionCells(after, before, id);
    }
    /* New definitions have no old record, so only their destination cells
       need writing.  They are still compared against `before` to preserve
       the correct previous block type when an object is placed over a cell. */
    for (int index = 0; index < after->compositeObjectCount; index++) {
        const int id = after->compositeObjects[index].id;
        if (IsCompositeDefinitionChangedForId(before, after, id))
            (void)QueuePreviewCompositeDefinitionCells(after, before, id);
    }
}

static const RpgAttachment *FindPreviewAttachmentById(const RpgAttachments *attachments,
                                                       int attachmentId)
{
    if (attachments == NULL || attachmentId <= 0) return NULL;
    for (int index = 0; index < attachments->count; index++)
        if (attachments->entries[index].folderId == attachmentId)
            return &attachments->entries[index];
    return NULL;
}

static void MarkPreviewAttachmentOwnershipCell(const RpgAttachment *attachment)
{
    RpgGridCell owner;
    if (attachment == NULL) return;
    if (RpgAttachments_GetOwnerBlockCell(attachment, &owner))
        MarkPreviewOwnershipCell(owner);
    else
        MarkPreviewOwnershipCell(attachment->cell);
}

static void MarkPreviewAttachmentOwnerById(const RpgAttachments *attachments, int attachmentId)
{
    MarkPreviewAttachmentOwnershipCell(FindPreviewAttachmentById(attachments, attachmentId));
}

static bool ArePreviewAttachmentRecordsDifferent(const RpgAttachment *first,
                                                  const RpgAttachment *second)
{
    RpgAttachment a, b;
    if (first == NULL || second == NULL) return first != second;
    a = *first;
    b = *second;
    /* Runtime-only state never needs an editor-preview folder update. */
    a.isZipperHeld = b.isZipperHeld = false;
    a.isOwnerBlockZipperHeld = b.isOwnerBlockZipperHeld = false;
    a.flagRaised = b.flagRaised = false;
    a.shooterAnimationElapsed = b.shooterAnimationElapsed = 0.0f;
    return memcmp(&a, &b, sizeof(a)) != 0;
}

static const RpgAttachmentRoute *FindPreviewAttachmentRouteById(
    const RpgAttachments *attachments, int routeId)
{
    if (attachments == NULL || routeId <= 0) return NULL;
    for (int index = 0; index < attachments->routeCount; index++)
        if (attachments->routes[index].id == routeId) return &attachments->routes[index];
    return NULL;
}

static void MarkPreviewAttachmentRouteCells(const RpgAttachments *attachments,
                                            const RpgAttachmentRoute *route)
{
    if (route == NULL) return;
    if (route->ownerAttachmentId > 0) {
        MarkPreviewAttachmentOwnerById(attachments, route->ownerAttachmentId);
        return;
    }
    /* Owner-less topology (for example a conveyor) is represented by its
       path.  Queue only that path, not every attachment in the stage. */
    MarkPreviewOwnershipCell(route->ownerCell);
    for (int index = 0; index < route->cells.cellCount; index++)
        MarkPreviewOwnershipCell(route->cells.cells[index]);
}

static void MarkChangedPreviewAttachmentOwnershipCells(const RpgAttachments *before,
                                                        const RpgAttachments *after)
{
    if (before == NULL || after == NULL) return;
    for (int index = 0; index < before->count; index++) {
        const RpgAttachment *oldAttachment = &before->entries[index];
        const RpgAttachment *newAttachment = FindPreviewAttachmentById(after, oldAttachment->folderId);
        if (newAttachment == NULL || ArePreviewAttachmentRecordsDifferent(oldAttachment, newAttachment)) {
            MarkPreviewAttachmentOwnershipCell(oldAttachment);
            MarkPreviewAttachmentOwnershipCell(newAttachment);
        }
    }
    for (int index = 0; index < after->count; index++) {
        const RpgAttachment *newAttachment = &after->entries[index];
        if (FindPreviewAttachmentById(before, newAttachment->folderId) == NULL)
            MarkPreviewAttachmentOwnershipCell(newAttachment);
    }
    for (int index = 0; index < before->shooterCount; index++) {
        const RpgShooterConfig *oldConfig = &before->shooters[index];
        const RpgShooterConfig *newConfig = RpgAttachments_FindShooterConst(after, oldConfig->attachmentId);
        RpgShooterConfig a = *oldConfig, b = newConfig != NULL ? *newConfig : (RpgShooterConfig){ 0 };
        a.animationElapsed = b.animationElapsed = 0.0f;
        if (newConfig == NULL || memcmp(&a, &b, sizeof(a)) != 0) {
            MarkPreviewAttachmentOwnerById(before, oldConfig->attachmentId);
            MarkPreviewAttachmentOwnerById(after, oldConfig->attachmentId);
        }
    }
    for (int index = 0; index < after->shooterCount; index++)
        if (RpgAttachments_FindShooterConst(before, after->shooters[index].attachmentId) == NULL)
            MarkPreviewAttachmentOwnerById(after, after->shooters[index].attachmentId);
    for (int index = 0; index < before->flagCount; index++) {
        const RpgFlagConfig *oldConfig = &before->flags[index];
        const RpgFlagConfig *newConfig = RpgAttachments_FindFlagConst(after, oldConfig->attachmentId);
        RpgFlagConfig a = *oldConfig, b = newConfig != NULL ? *newConfig : (RpgFlagConfig){ 0 };
        a.raised = b.raised = false;
        if (newConfig == NULL || memcmp(&a, &b, sizeof(a)) != 0) {
            MarkPreviewAttachmentOwnerById(before, oldConfig->attachmentId);
            MarkPreviewAttachmentOwnerById(after, oldConfig->attachmentId);
        }
    }
    for (int index = 0; index < after->flagCount; index++)
        if (RpgAttachments_FindFlagConst(before, after->flags[index].attachmentId) == NULL)
            MarkPreviewAttachmentOwnerById(after, after->flags[index].attachmentId);
    for (int index = 0; index < before->socketCount; index++) {
        const RpgSocketConfig *oldConfig = &before->sockets[index];
        const RpgSocketConfig *newConfig = RpgAttachments_FindSocketConst(after, oldConfig->attachmentId);
        if (newConfig == NULL || memcmp(oldConfig, newConfig, sizeof(*oldConfig)) != 0) {
            MarkPreviewAttachmentOwnerById(before, oldConfig->attachmentId);
            MarkPreviewAttachmentOwnerById(after, oldConfig->attachmentId);
        }
    }
    for (int index = 0; index < after->socketCount; index++)
        if (RpgAttachments_FindSocketConst(before, after->sockets[index].attachmentId) == NULL)
            MarkPreviewAttachmentOwnerById(after, after->sockets[index].attachmentId);
    for (int index = 0; index < before->routeCount; index++) {
        const RpgAttachmentRoute *oldRoute = &before->routes[index];
        const RpgAttachmentRoute *newRoute = FindPreviewAttachmentRouteById(after, oldRoute->id);
        if (newRoute == NULL || memcmp(oldRoute, newRoute, sizeof(*oldRoute)) != 0) {
            MarkPreviewAttachmentRouteCells(before, oldRoute);
            MarkPreviewAttachmentRouteCells(after, newRoute);
        }
    }
    for (int index = 0; index < after->routeCount; index++)
        if (FindPreviewAttachmentRouteById(before, after->routes[index].id) == NULL)
            MarkPreviewAttachmentRouteCells(after, &after->routes[index]);
}

static void MarkPreviewWireOwnershipCells(const RpgWires *wires)
{
    if (wires == NULL) return;
    for (int index = 0; index < wires->count; index++)
        MarkPreviewOwnershipCell(wires->entries[index].ownerCell);
}

static bool HasPendingPreviewOwnershipCells(void)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (pendingPreviewOwnershipCells[row][column]) return true;
    return false;
}

static const char *GetEditorPreviewPreparationDetail(void)
{
    const unsigned int kinds = editorPreviewPreparationKinds;
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_AREA_REPAIR)
        return "処理中: エリア修復中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_STAGE_LAYOUT)
        return "処理中: エリア構成を更新中";
    if ((kinds & RPG_EDITOR_PREVIEW_PREPARATION_WIRES) &&
        (kinds & RPG_EDITOR_PREVIEW_PREPARATION_ATTACHMENTS))
        return "処理中: 導線・アタッチメントを更新中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_WIRES)
        return "処理中: 導線・軌道を更新中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_ATTACHMENTS)
        return "処理中: アタッチメント設定を更新中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_OBJECTS)
        return "処理中: オブジェクト設定を更新中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_SIGNALS)
        return "処理中: 信号ブロック設定を更新中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_CELLS)
        return "処理中: セルを更新中";
    if (kinds & RPG_EDITOR_PREVIEW_PREPARATION_RECOVERY)
        return "処理中: プレビューキャッシュを修復中";
    return "処理中: プレイキャッシュを準備中";
}

static void QueuePublishedPreviewCompactCells(const RpgStage *stage)
{
    bool queued = false;
    if (stage == NULL) return;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            if (!pendingPreviewCompactCells[row][column]) continue;
            pendingPreviewCompactCells[row][column] = false;
            if (RpgBuildCellStorage_QueueCellUpdate(stage, (RpgGridCell){ row, column },
                                                     pendingPreviewCompactPreviousBlockTypes[row][column], NULL))
                queued = true;
        }
    }
    if (queued) MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_CELLS);
    if (queued || pendingPreviewRemovedCompositeCount > 0 || HasPendingPreviewOwnershipCells() ||
        pendingPreviewMovableObjectWork)
        editorPreviewHasDirectCellWork = true;
}

static void ClearPendingPreviewMovableObjectBatch(void)
{
    pendingPreviewMovableObjectWork = false;
    pendingPreviewPreviousReferenceObjects = RpgReferenceObjects_Default();
    pendingPreviewPreviousImageObjects = RpgImageObjects_Default();
    editorPreviewDirectBatchRetryCount = 0;
}

/* A direct preview write can fail transiently while Explorer still owns a
 * moved object folder.  Keep the exact pending batch and retry it against
 * the already active preview; losing that batch and abandoning the complete
 * preview turns one object edit into a full-area rebuild.  A detached build
 * is handled separately by the normal resume/create branch below. */
static bool RetryFailedPreviewDirectBatch(bool *previewBuildReady,
                                          bool *previewBuildDirty,
                                          double *nextSyncTime)
{
    enum { RPG_EDITOR_PREVIEW_LOCAL_RETRY_LIMIT = 3 };
    if (previewBuildReady != NULL) *previewBuildReady = false;
    if (previewBuildDirty != NULL) *previewBuildDirty = true;
    MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_RECOVERY);
    if (nextSyncTime != NULL) *nextSyncTime = GetTime() + 0.100;
    editorPreviewDirectBatchRetryCount++;
    return editorPreviewDirectBatchRetryCount < RPG_EDITOR_PREVIEW_LOCAL_RETRY_LIMIT;
}

/* Last-resort recovery is deliberately separated from an ordinary local
   retry.  It is only used after bounded retries have proved that the active
   preview cannot accept the exact delta. */
static void RebuildAfterPreviewLocalFailure(bool *previewBuildReady,
                                            bool *previewBuildDirty,
                                            double *nextSyncTime)
{
    RpgBuildCellStorage_CancelGeneration();
    RpgObjectFolders_AbandonStageBuild();
    editorPreviewHasDirectCellWork = false;
    memset(pendingPreviewCompactCells, 0, sizeof(pendingPreviewCompactCells));
    memset(pendingPreviewCompactPreviousBlockTypes, 0,
           sizeof(pendingPreviewCompactPreviousBlockTypes));
    memset(pendingPreviewOwnershipCells, 0, sizeof(pendingPreviewOwnershipCells));
    memset(pendingPreviewRemovedCompositeIds, 0, sizeof(pendingPreviewRemovedCompositeIds));
    pendingPreviewRemovedCompositeCount = 0;
    ClearPendingPreviewMovableObjectBatch();
    editorPreviewRepairRetryCount = 0;
    if (previewBuildReady != NULL) *previewBuildReady = false;
    if (previewBuildDirty != NULL) *previewBuildDirty = true;
    /* This is an actual full preview recovery, so make it visible. */
    editorPreviewAreaRepairAnnounced = true;
    MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_AREA_REPAIR);
    if (nextSyncTime != NULL) *nextSyncTime = GetTime() + 0.100;
}

static bool FinalizePreviewOwnershipCellBatch(RpgStage *stage)
{
    bool refreshed;
    if (stage == NULL) return false;
    if (!HasPendingPreviewOwnershipCells() && pendingPreviewRemovedCompositeCount == 0 &&
        !pendingPreviewMovableObjectWork) return true;
    /* This writes the editor's own Stage/editor folders.  Do not let the
       preview's ReadDirectoryChangesW watcher mistake those expected writes
       for a user-side filesystem mutation and invalidate the whole area. */
    RpgStageBuild_BeginInternalPreviewWrite();
    refreshed = (!HasPendingPreviewOwnershipCells() && pendingPreviewRemovedCompositeCount == 0) ||
        RpgObjectFolders_RefreshEditorPreviewCellBatch(
            stage, &attachments, &receivers, &wires, pendingPreviewOwnershipCells,
            pendingPreviewRemovedCompositeIds, pendingPreviewRemovedCompositeCount);
    if (refreshed && pendingPreviewMovableObjectWork)
        refreshed = RpgObjectFolders_RefreshEditorPreviewMovableObjectBatch(
            stage, &pendingPreviewPreviousReferenceObjects,
            &pendingPreviewPreviousImageObjects);
    RpgStageBuild_EndInternalPreviewWrite();
    if (refreshed) {
        memset(pendingPreviewOwnershipCells, 0, sizeof(pendingPreviewOwnershipCells));
        memset(pendingPreviewRemovedCompositeIds, 0, sizeof(pendingPreviewRemovedCompositeIds));
        pendingPreviewRemovedCompositeCount = 0;
        ClearPendingPreviewMovableObjectBatch();
        return true;
    }
    /* Keep the exact batch intact for a retry.  A transient folder operation
       (for example Explorer holding a just-moved composite directory) must
       not widen a drag into a complete area regeneration. */
    return false;
}

static bool HasPreviewMapTopologyChanged(const RpgStage *before, const RpgStage *after)
{
    if (before == NULL || after == NULL) return before != after;
    return memcmp(before->mapActive, after->mapActive, sizeof(before->mapActive)) != 0 ||
           memcmp(before->mapGridX, after->mapGridX, sizeof(before->mapGridX)) != 0 ||
           memcmp(before->mapGridY, after->mapGridY, sizeof(before->mapGridY)) != 0;
}

/* The runtime preview rewrites object paths and the fixed-capacity arrays
   retain unused slots.  Comparing those raw arrays makes an unrelated editor
   action look like a movable-object edit forever.  These comparisons contain
   only authored properties that own a preview directory. */
static const char *GetReferenceObjectPreviewIdentity(const RpgReferenceObject *object)
{
    if (object == NULL) return "";
    return object->sourcePath[0] != '\0' ? object->sourcePath : object->path;
}

static bool AreReferenceObjectPreviewDefinitionsDifferent(const RpgReferenceObjects *left,
                                                          const RpgReferenceObjects *right)
{
    if (left == NULL || right == NULL) return left != right;
    if (left->count != right->count || left->nextId != right->nextId) return true;
    for (int leftIndex = 0; leftIndex < left->count; leftIndex++) {
        const RpgReferenceObject *a = &left->entries[leftIndex];
        const RpgReferenceObject *b = NULL;
        for (int rightIndex = 0; rightIndex < right->count; rightIndex++)
            if (right->entries[rightIndex].id == a->id) {
                b = &right->entries[rightIndex];
                break;
            }
        if (b == NULL || a->objectKind != b->objectKind ||
            a->position.x != b->position.x || a->position.y != b->position.y ||
            a->drawScale != b->drawScale ||
            strcmp(GetReferenceObjectPreviewIdentity(a),
                   GetReferenceObjectPreviewIdentity(b)) != 0)
            return true;
    }
    return false;
}

static bool AreImageObjectPreviewDefinitionsDifferent(const RpgImageObjects *left,
                                                      const RpgImageObjects *right)
{
    if (left == NULL || right == NULL) return left != right;
    if (left->count != right->count || left->nextId != right->nextId) return true;
    for (int leftIndex = 0; leftIndex < left->count; leftIndex++) {
        const RpgImageObject *a = &left->entries[leftIndex];
        const RpgImageObject *b = NULL;
        for (int rightIndex = 0; rightIndex < right->count; rightIndex++)
            if (right->entries[rightIndex].id == a->id) {
                b = &right->entries[rightIndex];
                break;
            }
        if (b == NULL || a->row != b->row || a->column != b->column ||
            a->scale != b->scale || a->layer != b->layer ||
            a->appearance != b->appearance || a->positionX != b->positionX ||
            a->positionY != b->positionY ||
            a->hasCustomPosition != b->hasCustomPosition ||
            strcmp(a->path, b->path) != 0)
            return true;
    }
    return false;
}

/* Signal blocks are cell-owned settings, including the expanded variant.
   Their shape cells are already placed in the compact-cell queue by the
   composite delta.  Queue the owner cells here as well so moving a signal
   block never promotes the edit to a whole-area regeneration merely because
   rpg_signal_blocks.cfg changed with it. */
static void QueuePreviewSignalBlockOwnershipCells(const RpgStageData *snapshot,
                                                  const RpgSignalBlocks *blocks)
{
    if (snapshot == NULL || blocks == NULL) return;
    for (int index = 0; index < blocks->count; index++) {
        RpgGridCell cell = { blocks->entries[index].row, blocks->entries[index].column };
        if (cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
            cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) continue;
        QueuePreviewCellUpdate(cell.row, cell.column,
                               snapshot->stage.blocks[cell.row][cell.column]);
        MarkPreviewOwnershipCell(cell);
    }
}

static void QueuePreviewMovableObjectBatch(const RpgStage *before)
{
    if (before == NULL || pendingPreviewMovableObjectWork) return;
    pendingPreviewPreviousReferenceObjects = before->referenceObjects;
    pendingPreviewPreviousImageObjects = before->imageObjects;
    pendingPreviewMovableObjectWork = true;
    /* FILE/FOLDER has a terrain-cell delta as well, but PNG is a purely
       movable object.  Treat both through the same direct-batch worker;
       otherwise a PNG-only inspector edit skips this queue and falls through
       to the whole-area repair path. */
    editorPreviewHasDirectCellWork = true;
    TraceEditorPreviewRoute("queue-movable-object-batch");
}

static void MarkPreviewKeyDoorOwnershipCells(const RpgStage *stage)
{
    if (stage == NULL) return;
    for (int index = 0; index < stage->keyDoorCount; index++)
        MarkPreviewOwnershipCell((RpgGridCell){ stage->keyDoors[index].rootRow,
                                                stage->keyDoors[index].rootColumn });
}

static void MarkEditorPreviewChangedMaps(const RpgStageData *before, const RpgStageData *after)
{
    bool compositeChanged;
    bool referenceOrImageObjectsChanged;
    bool referenceCellChanged = false;
    bool keyDoorsChanged;
    /* A newly published authoring edit owns a fresh retry budget. */
    editorPreviewDirectBatchRetryCount = 0;
    editorPreviewRepairRetryCount = 0;
    if (before == NULL || after == NULL || HasPreviewMapTopologyChanged(&before->stage, &after->stage)) {
        /* Area topology is not a request to rebuild the whole stage.  Keep
           only the changed slots: the preview worker first removes their old
           coordinate identities, then republishes their new identities. */
        memset(pendingPreviewCompactCells, 0, sizeof(pendingPreviewCompactCells));
        memset(pendingPreviewCompactPreviousBlockTypes, 0,
               sizeof(pendingPreviewCompactPreviousBlockTypes));
        memset(pendingPreviewOwnershipCells, 0, sizeof(pendingPreviewOwnershipCells));
        memset(pendingPreviewRemovedCompositeIds, 0, sizeof(pendingPreviewRemovedCompositeIds));
        pendingPreviewRemovedCompositeCount = 0;
        ClearPendingPreviewMovableObjectBatch();
        editorPreviewHasDirectCellWork = false;
        FinishEditorPreviewPreparationDisplay();
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_STAGE_LAYOUT);
        if (before != NULL && after != NULL)
            RpgObjectFolders_MarkEditorPreviewTopologyMutation(&before->stage, &after->stage);
        else
            RpgObjectFolders_MarkEditorPreviewStaticMutation();
        return;
    }
    compositeChanged = before->stage.compositeObjectCount != after->stage.compositeObjectCount ||
        memcmp(before->stage.compositeObjects, after->stage.compositeObjects,
               sizeof(before->stage.compositeObjects)) != 0;
    if (compositeChanged) {
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_CELLS);
        QueuePreviewCompositeCellBatch(&before->stage, &after->stage);
    }

    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (before->stage.blocks[row][column] != after->stage.blocks[row][column] ||
            before->stage.cellStates[row][column] != after->stage.cellStates[row][column] ||
            strcmp(before->stage.referencePaths[row][column], after->stage.referencePaths[row][column]) != 0) {
            if (CanQueuePreviewLocalCellChange(before, after, row, column))
            {
                MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_CELLS);
                QueuePreviewCellUpdate(row, column, before->stage.blocks[row][column]);
            }
            else if (pendingPreviewCompactCells[row][column] ||
                     pendingPreviewOwnershipCells[row][column]) {
                /* This is one component of an already queued composite or
                   cell-owned special object.  Its object-folder refresh is
                   performed by the ownership batch; do not widen it into a
                   complete map regeneration. */
            }
            else if (RpgBlockInventory_IsReferenceObject(before->stage.blocks[row][column]) ||
                     RpgBlockInventory_IsReferenceObject(after->stage.blocks[row][column])) {
                /* FILE/FOLDER cells merely point at a movable object record.
                   Their ID-owned directory is refreshed below together with
                   that record.  Marking its map here creates a second,
                   whole-area repair after every object move or inspector
                   edit. */
                referenceCellChanged = true;
            }
            else
                MarkPreviewMapForColumn(column);
        }
    }

    referenceOrImageObjectsChanged =
        AreReferenceObjectPreviewDefinitionsDifferent(&before->stage.referenceObjects,
                                                      &after->stage.referenceObjects) ||
        AreImageObjectPreviewDefinitionsDifferent(&before->stage.imageObjects,
                                                  &after->stage.imageObjects);
    keyDoorsChanged =
        memcmp(before->stage.keyDoors, after->stage.keyDoors,
               sizeof(before->stage.keyDoors)) != 0 ||
        before->stage.keyDoorCount != after->stage.keyDoorCount;
    if (referenceOrImageObjectsChanged || referenceCellChanged) {
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_OBJECTS);
        /* Movable FILE/FOLDER/PNG objects have ID-owned runtime folders.
           Their old and new records are enough to refresh just those folders;
           marking an entire area dirty here turns a single drag or inspector
           setting edit into an unnecessary area repair. */
        QueuePreviewMovableObjectBatch(&before->stage);
        TraceEditorPreviewRoute(referenceOrImageObjectsChanged ?
                                      "publish-reference-or-image-change" :
                                      "publish-reference-cell-change");
    }
    /* A key door is a fixed composite.  Its component cells have already
       been queued above when moved; changing its key settings needs only the
       owning composite folder, not either entire area. */
    if (keyDoorsChanged) {
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_OBJECTS);
        MarkPreviewKeyDoorOwnershipCells(&before->stage);
        MarkPreviewKeyDoorOwnershipCells(&after->stage);
    }

    if (AreAttachmentsDifferent(&before->attachments, &after->attachments)) {
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_ATTACHMENTS);
        MarkChangedPreviewAttachmentOwnershipCells(&before->attachments, &after->attachments);
    }
    if (memcmp(&before->wires, &after->wires, sizeof(before->wires)) != 0) {
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_WIRES);
        MarkPreviewWireOwnershipCells(&before->wires);
        MarkPreviewWireOwnershipCells(&after->wires);
    }
    if (memcmp(&before->signalBlocks, &after->signalBlocks, sizeof(before->signalBlocks)) != 0) {
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_SIGNALS);
        QueuePreviewSignalBlockOwnershipCells(before, &before->signalBlocks);
        QueuePreviewSignalBlockOwnershipCells(after, &after->signalBlocks);
    }
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
           EDITOR_STAGE_FOLDER_SYNC_DELAY_MS = 0 };
    bool stageChanged;
    const RpgStageData *candidate;
    double now;
    if (layout == NULL || stage == NULL || dialogue == NULL || stage3Event == NULL ||
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
        /* Do not mark an incomplete snapshot as published.  Otherwise the
           worker has neither a candidate nor a valid previous stage to
           repair, and the UI remains at "checking cache" indefinitely. */
        if (!CopyEditorStageData(lastPublished, &stageFolderSyncBuffer)) {
            *hasLastPublished = false;
            *previewBuildDirty = true;
            *previewBuildReady = false;
            *nextSyncTime = now + 0.050;
            return;
        }
        *hasLastPublished = true;
        *lastPublishedStageNumber = stageNumber;
        hasPendingStageFolderSync = false;
        pendingStageFolderSyncNumber = 0;
        *previewBuildDirty = true;
        *previewBuildReady = false;
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
        return;
    }
    stageChanged = IsEditorStageDataDifferent(lastPublished, &stageFolderSyncBuffer);
    if (stageChanged) {
        /* Reset the debounce only when the editor contents changed since the
           previous observed candidate.  `lastPublished` is intentionally
           left untouched until publication, so it cannot answer this. */
        if (!hasPendingStageFolderSync || pendingStageFolderSyncNumber != stageNumber ||
            IsEditorStageDataDifferent(&pendingStageFolderSync, &stageFolderSyncBuffer)) {
            if (!CopyEditorStageData(&pendingStageFolderSync, &stageFolderSyncBuffer)) {
                *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
                return;
            }
            hasPendingStageFolderSync = true;
            pendingStageFolderSyncNumber = stageNumber;
            *previewBuildDirty = true;
            /* Publish only after the latest actual edit has been quiet. */
            *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
        }
    }
    if (!stageChanged && !*previewBuildDirty) { *nextSyncTime = 0.0; return; }
    /* A previously published edit is being rebuilt cooperatively below the
       main loop.  Do not invoke the old synchronous repair again on every
       comparison tick while that job is in flight. */
    /* `previewBuildDirty` is also used while an edit waits for its debounce.
       Only suppress this synchronizer once a concrete candidate is already
       queued; otherwise an initial failed preview still needs a retry. */
    /* A pending snapshot only suppresses duplicate comparisons during its
       debounce.  Once its deadline is reached it must be published to the
       cooperative repair worker; the former unconditional return here left
       Stop permanently at "checking cache". */
    if (!stageChanged && *previewBuildDirty && hasPendingStageFolderSync &&
        pendingStageFolderSyncNumber == stageNumber && now < *nextSyncTime) return;
    /* A published snapshot is now owned by the cooperative repair worker.
       Do not create a new debounce candidate every comparison interval while
       that worker is processing it. */
    if (!stageChanged && *previewBuildDirty && *nextSyncTime <= 0.0 &&
        RpgObjectFolders_IsStageBuildActive()) return;
    /* A failed startup/repair has no changed snapshot, but it still needs one
       retry candidate. */
    if (!hasPendingStageFolderSync || pendingStageFolderSyncNumber != stageNumber) {
        if (!CopyEditorStageData(&pendingStageFolderSync, &stageFolderSyncBuffer)) {
            *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
            return;
        }
        hasPendingStageFolderSync = true;
        pendingStageFolderSyncNumber = stageNumber;
    }
    if (*nextSyncTime <= 0.0) {
        *nextSyncTime = now + (double)EDITOR_STAGE_FOLDER_SYNC_DELAY_MS / 1000.0;
        return;
    }
    if (now < *nextSyncTime) return;
    candidate = &pendingStageFolderSync;
    /* The authored Stage folder is published by explicit Save.  Rewriting its
       complete static layout after every idle edit runs hundreds of file
       operations on this UI thread and makes clicks disappear, especially for
       attachment placement.  The editor-preview cache below is the only
       disposable runtime output that needs this automatic update. */
    /* Derive the affected preview maps while `lastPublished` still contains
       the previous static snapshot.  Do not turn a local special-block edit
       into a full Stage/editor rebuild. */
    if (stageChanged)
        MarkEditorPreviewChangedMaps(lastPublished, candidate);
    if (stageChanged && !CopyEditorStageData(lastPublished, candidate)) {
        /* Keep the candidate queued for a retry; never claim that its cache
           corresponds to an unpublished static snapshot. */
        hasPendingStageFolderSync = true;
        pendingStageFolderSyncNumber = stageNumber;
        *nextSyncTime = now + 0.050;
        return;
    }
    /* The old route called EnsureEditorPreviewCache() here.  That function
       removes and recreates every dirty map in one frame, which made the
       editor appear hung after an edit.  Queue the same affected-map work;
       the main loop materialises one map/phase per frame instead. */
    editorPreviewBuildStage = candidate->stage;
    QueuePublishedPreviewCompactCells(&editorPreviewBuildStage);
    *previewBuildDirty = true;
    *previewBuildReady = false;
    *previewBuildStageNumber = stageNumber;
    /* This candidate has been handed to the repair worker.  It is no longer
       an edit waiting for debounce; leaving it pending makes the next frame
       schedule another 700 ms debounce and prevents the worker completing. */
    hasPendingStageFolderSync = false;
    pendingStageFolderSyncNumber = 0;
    *nextSyncTime = 0.0;
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
    layout->playerJumpHeight = player->jumpHeight;
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
        (void)RpgAttachments_Clone(&savedAttachments, &attachments);
        RpgReceivers_Bind(&savedReceivers, &savedAttachments);
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

static void PushBlockHistory(BlockHistory *history, int row, int column, int previousValue)
{
    BlockHistoryEntry entry = {
        .kind = BLOCK_HISTORY_CELL_CHANGE,
        .row = row,
        .column = column,
        .previousValue = previousValue,
        .previousCellState = previousValue == 0 ? RPG_CELL_AIR : RPG_CELL_BLOCK
    };
    AppendBlockHistory(history, entry);
}

static void PushBlockHistoryFromStage(BlockHistory *history, const RpgStage *stage,
                                      int row, int column)
{
    BlockHistoryEntry entry = {
        .kind = BLOCK_HISTORY_CELL_CHANGE,
        .row = row,
        .column = column,
        .previousValue = stage->blocks[row][column],
        .previousCellState = RpgStage_GetCellState(stage, row, column)
    };
    AppendBlockHistory(history, entry);
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
                                .receiver = { 0 } };
    if (!RpgReceivers_Get(receiverList, receiverIndex, &entry.receiver)) entry.propertyIndex = -1;
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

static bool PlaceReceiverProperty(BlockPropertyPlacementContext *context, RpgGridCell cell,
                                  const char **message)
{
    if (!RpgReceivers_Add(context->receivers, context->stage, cell)) return false;
    PushReceiverAddedHistory(context->history, RpgReceivers_Count(context->receivers) - 1);
    if (RpgWires_AddFromReceiver(context->wires, context->stage, cell, RPG_GRID_SIDE_TOP))
        *message = "Receiver and default wire added";
    else *message = "Receiver added, but wire limit reached";
    return true;
}

static const BlockPropertyPlacementDefinition blockPropertyPlacements[] = {
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
    (void)items;
    const BlockPropertyPlacementDefinition *definition = FindBlockPropertyPlacement(blockType);
    if (definition == NULL) return false;
    BlockPropertyPlacementContext context = { stage, wireList, receiverList, history };
    return definition->place(&context, cell, message);
}

// 複数マス特殊ブロックを配置する前に、必要な全マスが空いていることを確認する。
static bool PlaceBlockType(RpgStage *stage, const RpgAttachments *attachments, BlockHistory *history,
                           int row, int column, int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    if (shape == NULL) {
        bool isStaticError = RpgBlockInventory_IsStaticErrorMarker(blockType);
        if ((isStaticError ? RpgStage_IsCellError(stage, row, column) :
                             stage->blocks[row][column] == blockType) ||
            RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ row, column })) return false;
        PushBlockHistoryFromStage(history, stage, row, column);
        RpgStage_SetCellBlock(stage, row, column, blockType);
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
        PushBlockHistoryFromStage(history, stage, targetRow, targetColumn);
        RpgStage_SetCellBlock(stage, targetRow, targetColumn, cell->blockType);
    }
    if (RpgBlockInventory_IsKeyDoorBlock(blockType)) (void)RpgStage_EnsureKeyDoor(stage, row, column);
    return true;
}

/* The component cells remain in the terrain array, while RpgStage owns the
 * rectangle as one object.  History deliberately records the affected cells,
 * so existing undo/rebuild infrastructure remains valid. */
static bool PlaceMagnetRectangle(RpgStage *stage, const RpgAttachments *attachments,
                                 BlockHistory *history, int firstRow, int firstColumn,
                                 int lastRow, int lastColumn, int blockType)
{
    int minimumRow = firstRow < lastRow ? firstRow : lastRow;
    int maximumRow = firstRow > lastRow ? firstRow : lastRow;
    int minimumColumn = firstColumn < lastColumn ? firstColumn : lastColumn;
    int maximumColumn = firstColumn > lastColumn ? firstColumn : lastColumn;
    if (!RpgBlockInventory_IsMagnetBlock(blockType) || minimumRow < 0 || minimumColumn < 0 ||
        maximumRow >= RPG_STAGE_ROWS || maximumColumn >= RPG_STAGE_WORLD_COLUMNS) return false;
    for (int row = minimumRow; row <= maximumRow; row++) for (int column = minimumColumn;
         column <= maximumColumn; column++) {
        int existing = stage->blocks[row][column];
        if (existing != 0 ||
            RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ row, column })) return false;
    }
    for (int row = minimumRow; row <= maximumRow; row++) for (int column = minimumColumn;
         column <= maximumColumn; column++) {
        PushBlockHistory(history, row, column, stage->blocks[row][column]);
    }
    return RpgStage_AddMagnet(stage, minimumRow, minimumColumn,
                              maximumColumn - minimumColumn + 1,
                              maximumRow - minimumRow + 1, blockType);
}

enum {
    RPG_EDITOR_MAGNET_RESIZE_LEFT = 1 << 0,
    RPG_EDITOR_MAGNET_RESIZE_RIGHT = 1 << 1,
    RPG_EDITOR_MAGNET_RESIZE_TOP = 1 << 2,
    RPG_EDITOR_MAGNET_RESIZE_BOTTOM = 1 << 3,
    RPG_EDITOR_MAGNET_RESIZE_HANDLE_PIXELS = 5
};

static int GetMagnetResizeEdges(const RpgMagnetObject *magnet, int mapIndex, Vector2 pointer)
{
    Rectangle bounds;
    float horizontal;
    float vertical;
    int edges = 0;
    if (magnet == NULL) return 0;
    bounds = (Rectangle){
        (magnet->rootColumn - mapIndex * RPG_STAGE_COLUMNS) * RPG_STAGE_TILE_SIZE,
        magnet->rootRow * RPG_STAGE_TILE_SIZE,
        magnet->width * RPG_STAGE_TILE_SIZE,
        magnet->height * RPG_STAGE_TILE_SIZE
    };
    if (!CheckCollisionPointRec(pointer, bounds)) return 0;
    horizontal = pointer.x - bounds.x;
    vertical = pointer.y - bounds.y;
    if (horizontal <= RPG_EDITOR_MAGNET_RESIZE_HANDLE_PIXELS)
        edges |= RPG_EDITOR_MAGNET_RESIZE_LEFT;
    else if (bounds.width - horizontal <= RPG_EDITOR_MAGNET_RESIZE_HANDLE_PIXELS)
        edges |= RPG_EDITOR_MAGNET_RESIZE_RIGHT;
    if (vertical <= RPG_EDITOR_MAGNET_RESIZE_HANDLE_PIXELS)
        edges |= RPG_EDITOR_MAGNET_RESIZE_TOP;
    else if (bounds.height - vertical <= RPG_EDITOR_MAGNET_RESIZE_HANDLE_PIXELS)
        edges |= RPG_EDITOR_MAGNET_RESIZE_BOTTOM;
    return edges;
}

static void GetMagnetResizeBounds(const RpgMagnetObject *magnet, int resizeEdges,
                                  int targetRow, int targetColumn,
                                  int *minimumRow, int *minimumColumn,
                                  int *maximumRow, int *maximumColumn)
{
    int minRow = magnet->rootRow;
    int maxRow = magnet->rootRow + magnet->height - 1;
    int minColumn = magnet->rootColumn;
    int maxColumn = magnet->rootColumn + magnet->width - 1;
    if (resizeEdges & RPG_EDITOR_MAGNET_RESIZE_LEFT) minColumn = targetColumn;
    if (resizeEdges & RPG_EDITOR_MAGNET_RESIZE_RIGHT) maxColumn = targetColumn;
    if (resizeEdges & RPG_EDITOR_MAGNET_RESIZE_TOP) minRow = targetRow;
    if (resizeEdges & RPG_EDITOR_MAGNET_RESIZE_BOTTOM) maxRow = targetRow;
    if (minRow > maxRow) { int swap = minRow; minRow = maxRow; maxRow = swap; }
    if (minColumn > maxColumn) { int swap = minColumn; minColumn = maxColumn; maxColumn = swap; }
    *minimumRow = minRow;
    *minimumColumn = minColumn;
    *maximumRow = maxRow;
    *maximumColumn = maxColumn;
}

/* Shared anchor for moving any object represented by multiple cells. */
typedef struct EditorObjectMoveAnchor {
    bool valid;
    int rootRow;
    int rootColumn;
    int grabbedRowOffset;
    int grabbedColumnOffset;
} EditorObjectMoveAnchor;

static void BeginObjectMoveAnchor(EditorObjectMoveAnchor *anchor, int rootRow,
                                  int rootColumn, int grabbedRow, int grabbedColumn)
{
    if (anchor == NULL) return;
    *anchor = (EditorObjectMoveAnchor){ true, rootRow, rootColumn,
                                        grabbedRow - rootRow,
                                        grabbedColumn - rootColumn };
}

static bool GetObjectMoveAnchorDestination(const EditorObjectMoveAnchor *anchor,
                                           int targetRow, int targetColumn,
                                           int *destinationRow, int *destinationColumn)
{
    if (anchor == NULL || !anchor->valid || destinationRow == NULL || destinationColumn == NULL)
        return false;
    *destinationRow = targetRow - anchor->grabbedRowOffset;
    *destinationColumn = targetColumn - anchor->grabbedColumnOffset;
    return true;
}

static bool ResizeMagnetObject(RpgStage *stage, const RpgAttachments *attachments,
                               BlockHistory *history, int sourceRow, int sourceColumn,
                               int resizeEdges, int targetRow, int targetColumn)
{
    const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(stage, sourceRow, sourceColumn);
    if (stage == NULL || magnet == NULL) return false;
    int minimumRow, maximumRow, minimumColumn, maximumColumn;
    GetMagnetResizeBounds(magnet, resizeEdges, targetRow, targetColumn,
                          &minimumRow, &minimumColumn, &maximumRow, &maximumColumn);
    if (minimumRow < 0 || minimumColumn < 0 || maximumRow >= RPG_STAGE_ROWS ||
        maximumColumn >= RPG_STAGE_WORLD_COLUMNS ||
        minimumColumn / RPG_STAGE_COLUMNS != maximumColumn / RPG_STAGE_COLUMNS) return false;
    for (int row = minimumRow; row <= maximumRow; row++) for (int column = minimumColumn;
         column <= maximumColumn; column++) {
        bool wasOwned = row >= magnet->rootRow && row < magnet->rootRow + magnet->height &&
                        column >= magnet->rootColumn && column < magnet->rootColumn + magnet->width;
        if ((!wasOwned && stage->blocks[row][column] != 0) ||
            (!wasOwned && RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ row, column })))
            return false;
    }
    for (int row = magnet->rootRow; row < magnet->rootRow + magnet->height; row++)
        for (int column = magnet->rootColumn; column < magnet->rootColumn + magnet->width; column++)
            PushBlockHistory(history, row, column, stage->blocks[row][column]);
    for (int row = minimumRow; row <= maximumRow; row++) for (int column = minimumColumn;
         column <= maximumColumn; column++) {
        bool wasOwned = row >= magnet->rootRow && row < magnet->rootRow + magnet->height &&
                        column >= magnet->rootColumn && column < magnet->rootColumn + magnet->width;
        if (!wasOwned) PushBlockHistory(history, row, column, stage->blocks[row][column]);
    }
    return RpgStage_ResizeMagnetBoundsAtCell(stage, sourceRow, sourceColumn,
                                             minimumRow, minimumColumn,
                                             maximumRow, maximumColumn);
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
static bool MoveEffectBlock(RpgStage *stage, const RpgAttachments *attachments,
                            BlockHistory *history,
                            int sourceRow, int sourceColumn, int destinationRow, int destinationColumn)
{
    int sourceRootRow;
    int sourceRootColumn;
    int compositeObjectId;
    const RpgEffectShape *shape;
    if (!FindEffectBlockRoot(stage, sourceRow, sourceColumn, &sourceRootRow, &sourceRootColumn, &shape)) return false;

    /* Fixed shapes must retain their stable object record while moving.  The
       preview delta writer uses that record to queue old and new component
       cells; without it every changed component falls back to an area rebuild. */
    compositeObjectId = RpgStage_GetCompositeObjectIdAtCell(stage, sourceRootRow, sourceRootColumn);

    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = destinationRow + cell->offsetY;
        int targetColumn = destinationColumn + cell->offsetX;
        if (targetRow < 0 || targetRow >= RPG_STAGE_ROWS || targetColumn < 0 || targetColumn >= RPG_STAGE_WORLD_COLUMNS)
            return false;
        bool isSourceCell = GetEffectShapeBlockTypeAt(shape, sourceRootRow, sourceRootColumn, targetRow, targetColumn) != 0;
        if (RpgAttachments_IsCellOccupied(attachments, (RpgGridCell){ targetRow, targetColumn }) ||
            (!isSourceCell && stage->blocks[targetRow][targetColumn] != 0 &&
             !RpgBlockInventory_IsOrdinaryBlock(stage->blocks[targetRow][targetColumn])) ) return false;
    }

    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int sourceCellRow = sourceRootRow + cell->offsetY;
        int sourceCellColumn = sourceRootColumn + cell->offsetX;
        int finalType = GetEffectShapeBlockTypeAt(shape, destinationRow, destinationColumn, sourceCellRow, sourceCellColumn);
        if (stage->blocks[sourceCellRow][sourceCellColumn] != finalType) {
            PushBlockHistory(history, sourceCellRow, sourceCellColumn,
                             stage->blocks[sourceCellRow][sourceCellColumn]);
        }
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        int targetRow = destinationRow + cell->offsetY;
        int targetColumn = destinationColumn + cell->offsetX;
        if (GetEffectShapeBlockTypeAt(shape, sourceRootRow, sourceRootColumn, targetRow, targetColumn) == 0)
            PushBlockHistory(history, targetRow, targetColumn, stage->blocks[targetRow][targetColumn]);
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        RpgStage_SetCellBlock(stage, sourceRootRow + cell->offsetY,
                              sourceRootColumn + cell->offsetX, 0);
    }
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        RpgStage_SetCellBlock(stage, destinationRow + cell->offsetY,
                              destinationColumn + cell->offsetX, cell->blockType);
    }
    if (compositeObjectId > 0)
        (void)RpgStage_RelocateCompositeObjectById(stage, compositeObjectId,
                                                    destinationRow, destinationColumn);
    return true;
}

/* Metal and carry blocks are runtime-movable, but in authoring they are still
 * ordinary one-cell stage blocks.  Keep their editor move on the same
 * history/cell path as composite objects instead of giving them a separate
 * placement-only implementation. */
static bool MoveMovableBlock(RpgStage *stage, const RpgAttachments *attachments,
                             BlockHistory *history, int sourceRow, int sourceColumn,
                             int destinationRow, int destinationColumn)
{
    int blockType;
    if (stage == NULL || sourceRow < 0 || sourceRow >= RPG_STAGE_ROWS ||
        sourceColumn < 0 || sourceColumn >= RPG_STAGE_WORLD_COLUMNS ||
        destinationRow < 0 || destinationRow >= RPG_STAGE_ROWS ||
        destinationColumn < 0 || destinationColumn >= RPG_STAGE_WORLD_COLUMNS)
        return false;
    blockType = stage->blocks[sourceRow][sourceColumn];
    if (!RpgBlockInventory_IsMetalBlock(blockType) &&
        !RpgBlockInventory_IsPushBlock(blockType))
        return false;
    if (stage->blocks[destinationRow][destinationColumn] != 0 ||
        RpgAttachments_IsCellOccupied(attachments,
                                       (RpgGridCell){ destinationRow, destinationColumn }))
        return false;
    PushBlockHistoryFromStage(history, stage, sourceRow, sourceColumn);
    PushBlockHistoryFromStage(history, stage, destinationRow, destinationColumn);
    RpgStage_SetCellBlock(stage, sourceRow, sourceColumn, 0);
    RpgStage_SetCellBlock(stage, destinationRow, destinationColumn, blockType);
    return true;
}

// 特殊ブロックの移動入口を一つに集約する。通常の形状ブロックは共通形状移動を使い、
// 状態によって一部のマスが存在しないブロックだけが専用処理を追加できる。
static bool MoveSpecialBlock(RpgStage *stage, const RpgAttachments *attachments, RpgItems *items,
                             BlockHistory *history,
                             RpgSignalBlocks *signalBlockList, int sourceRow, int sourceColumn,
                             int destinationRow, int destinationColumn)
{
    (void)items;
    if (stage != NULL && sourceRow >= 0 && sourceRow < RPG_STAGE_ROWS &&
        sourceColumn >= 0 && sourceColumn < RPG_STAGE_WORLD_COLUMNS &&
        (RpgBlockInventory_IsMetalBlock(stage->blocks[sourceRow][sourceColumn]) ||
         RpgBlockInventory_IsPushBlock(stage->blocks[sourceRow][sourceColumn])))
        return MoveMovableBlock(stage, attachments, history, sourceRow, sourceColumn,
                                destinationRow, destinationColumn);
    const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(stage, sourceRow, sourceColumn);
    if (magnet != NULL) {
        for (int row = magnet->rootRow; row < magnet->rootRow + magnet->height; row++)
            for (int column = magnet->rootColumn; column < magnet->rootColumn + magnet->width; column++)
                PushBlockHistory(history, row, column, stage->blocks[row][column]);
        for (int row = destinationRow; row < destinationRow + magnet->height; row++)
            for (int column = destinationColumn; column < destinationColumn + magnet->width; column++)
                if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
                    !(row >= magnet->rootRow && row < magnet->rootRow + magnet->height &&
                      column >= magnet->rootColumn && column < magnet->rootColumn + magnet->width))
                    PushBlockHistory(history, row, column, stage->blocks[row][column]);
        return RpgStage_MoveMagnetAtCell(stage, sourceRow, sourceColumn, destinationRow, destinationColumn);
    }
    int signalIndex = RpgSignalBlocks_FindAtCell(signalBlockList, stage, sourceRow, sourceColumn);
    if (signalIndex < 0)
        return MoveEffectBlock(stage, attachments, history, sourceRow, sourceColumn,
                               destinationRow, destinationColumn);
    if (signalBlockList->entries[signalIndex].startsExpanded) {
        if (!MoveEffectBlock(stage, attachments, history, sourceRow, sourceColumn,
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
    PushBlockHistory(history, signalBlock->row, signalBlock->column, rootType);
    PushBlockHistory(history, destinationRow, destinationColumn,
                     stage->blocks[destinationRow][destinationColumn]);
    RpgStage_SetCellBlock(stage, signalBlock->row, signalBlock->column, 0);
    RpgStage_SetCellBlock(stage, destinationRow, destinationColumn, rootType);
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
    (void)items;
    if (stage->blocks[row][column] == 0 && !RpgStage_IsCellError(stage, row, column)) return false;
    const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(stage, row, column);
    if (magnet != NULL) {
        for (int targetRow = magnet->rootRow; targetRow < magnet->rootRow + magnet->height; targetRow++)
            for (int targetColumn = magnet->rootColumn;
                 targetColumn < magnet->rootColumn + magnet->width; targetColumn++)
                PushBlockHistory(history, targetRow, targetColumn, stage->blocks[targetRow][targetColumn]);
        return RpgStage_RemoveMagnetAtCell(stage, row, column);
    }
    int rootRow = row;
    int rootColumn = column;
    const RpgEffectShape *shape = NULL;
    bool isEffect = FindEffectBlockRoot(stage, row, column, &rootRow, &rootColumn, &shape);
    int cellCount = isEffect ? shape->cellCount : 1;
    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        int targetRow = isEffect ? rootRow + shape->cells[cellIndex].offsetY : row;
        int targetColumn = isEffect ? rootColumn + shape->cells[cellIndex].offsetX : column;
        PushBlockHistoryFromStage(history, stage, targetRow, targetColumn);
        RpgStage_SetCellBlock(stage, targetRow, targetColumn, 0);
    }
    return true;
}

// ブロックと一体の星アイテムは、変更したマスとアイテム一件だけを記録して復元する。
static RPG_UNUSED bool UndoBlockChange(BlockHistory *history, RpgStage *stage,
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
        RpgReceiver receiver;
        if (entry.propertyIndex < 0 || !RpgReceivers_Get(receiverList, entry.propertyIndex, &receiver)) return false;
        // 受容体の作成時に自動生成した導線も、同じ一回のUndoで取り除く。
        for (int wireIndex = wireList->count - 1; wireIndex >= 0; wireIndex--) {
            RpgWire *wire = &wireList->entries[wireIndex];
            if (!wire->hasReceiverSource || wire->receiverCell.row != receiver.cell.row ||
                wire->receiverCell.column != receiver.cell.column) continue;
            for (int next = wireIndex; next < wireList->count - 1; next++)
                wireList->entries[next] = wireList->entries[next + 1];
            wireList->count--;
        }
        return RpgReceivers_Remove(receiverList, entry.propertyIndex);
    }
    if (entry.kind == BLOCK_HISTORY_RECEIVER_CHANGED) {
        if (entry.propertyIndex < 0 || !RpgReceivers_Set(receiverList, entry.propertyIndex, entry.receiver)) return false;
        for (int index = 0; index < entry.receiverWireCount; index++) {
            int wireIndex = entry.receiverWireIndices[index];
            if (wireIndex >= 0 && wireIndex < wireList->count)
                wireList->entries[wireIndex].receiverSide = entry.receiverWireSides[index];
        }
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_ATTACHMENT_ADDED)
        return RpgAttachments_Remove(attachmentList, stage, entry.attachment);
    if (entry.kind == BLOCK_HISTORY_ATTACHMENT_CHANGED) {
        if (entry.propertyIndex < 0 || entry.propertyIndex >= attachmentList->count) return false;
        attachmentList->entries[entry.propertyIndex] = entry.attachment;
        return true;
    }
    if (entry.kind == BLOCK_HISTORY_ATTACHMENT_REMOVED) {
        return RpgAttachments_Append(attachmentList, &entry.attachment);
    }

    if (entry.previousCellState == RPG_CELL_ERROR)
        RpgStage_SetCellError(stage, entry.row, entry.column, entry.previousValue);
    else if (entry.previousCellState == RPG_CELL_RUNTIME_ERROR)
        RpgStage_SetCellRuntimeError(stage, entry.row, entry.column, entry.previousValue);
    else RpgStage_SetCellBlock(stage, entry.row, entry.column, entry.previousValue);
    /* Magnet ownership is derived from the materialized component rectangle
       after each legacy cell-history undo step. */
    RpgStage_NormalizeCompositeObjects(stage);
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
    (void)RpgAttachments_Clone(&attachments, &savedAttachments);
    RpgReceivers_Bind(&receivers, &attachments);
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


static bool AreWiresDifferent(const RpgWires *first, const RpgWires *second)
{
    return memcmp(first, second, sizeof(*first)) != 0;
}

static bool AreReceiversDifferent(const RpgReceivers *first, const RpgReceivers *second)
{
    if (first == NULL || second == NULL) return first != second;
    if (RpgReceivers_Count(first) != RpgReceivers_Count(second)) return true;
    /* isOwnerBlockZipperHeld is runtime-only state: eating a parent block in
       a preview must never make the editor's static receiver data dirty. */
    for (int index = 0; index < RpgReceivers_Count(first); index++) {
        RpgReceiver a, b;
        if (!RpgReceivers_Get(first, index, &a) || !RpgReceivers_Get(second, index, &b) ||
            a.cell.row != b.cell.row || a.cell.column != b.cell.column || a.side != b.side) return true;
    }
    return false;
}

static bool AreAttachmentsDifferent(const RpgAttachments *first, const RpgAttachments *second)
{
    if (first == NULL || second == NULL) return first != second;
    if (first->count != second->count || first->routeCount != second->routeCount ||
        first->shooterCount != second->shooterCount || first->flagCount != second->flagCount ||
        first->socketCount != second->socketCount ||
        (first->count > 0 && (first->entries == NULL || second->entries == NULL)) ||
        (first->routeCount > 0 && (first->routes == NULL || second->routes == NULL)) ||
        (first->shooterCount > 0 && (first->shooters == NULL || second->shooters == NULL)) ||
        (first->flagCount > 0 && (first->flags == NULL || second->flags == NULL)) ||
        (first->socketCount > 0 && (first->sockets == NULL || second->sockets == NULL))) return true;
    for (int index = 0; index < first->count; index++) {
        RpgAttachment a = first->entries[index], b = second->entries[index];
        /* These values only describe a running preview, never authored data. */
        a.isZipperHeld = b.isZipperHeld = false;
        a.isOwnerBlockZipperHeld = b.isOwnerBlockZipperHeld = false;
        a.flagRaised = b.flagRaised = false;
        a.shooterAnimationElapsed = b.shooterAnimationElapsed = 0.0f;
        if (memcmp(&a, &b, sizeof(a)) != 0) return true;
    }
    if (first->routeCount > 0 && memcmp(first->routes, second->routes,
                                         (size_t)first->routeCount * sizeof(*first->routes)) != 0) return true;
    for (int index = 0; index < first->shooterCount; index++) {
        RpgShooterConfig a = first->shooters[index], b = second->shooters[index];
        a.animationElapsed = b.animationElapsed = 0.0f;
        if (memcmp(&a, &b, sizeof(a)) != 0) return true;
    }
    for (int index = 0; index < first->flagCount; index++) {
        RpgFlagConfig a = first->flags[index], b = second->flags[index];
        a.raised = b.raised = false;
        if (memcmp(&a, &b, sizeof(a)) != 0) return true;
    }
    return first->socketCount > 0 && memcmp(first->sockets, second->sockets,
                                              (size_t)first->socketCount * sizeof(*first->sockets)) != 0;
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
           snapshot->player.scale != player->scale || snapshot->player.jumpHeight != player->jumpHeight ||
           snapshot->npc.position.x != npc->position.x ||
           snapshot->npc.scale != npc->scale || AreStaticStagesDifferent(&snapshot->stage, stage) ||
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
            snapshot->layout.socketRightLightAngle != layout->socketRightLightAngle ||
            snapshot->layout.socketLightOpacity != layout->socketLightOpacity ||
            snapshot->layout.electricCellDelay != layout->electricCellDelay ||
            snapshot->layout.magnetMetalSpeed != layout->magnetMetalSpeed ||
            snapshot->layout.zipperFolderReturnDuration != layout->zipperFolderReturnDuration ||
            snapshot->layout.zipperFolderReturnAnimationDelay != layout->zipperFolderReturnAnimationDelay ||
            snapshot->layout.playerJumpHeight != layout->playerJumpHeight ||
            strcmp(snapshot->layout.keyDoorFailureText, layout->keyDoorFailureText) != 0 ||
            HasUnsavedChanges(snapshot, player, npc, stage, dialogue, stage3Event) ||
            AreWiresDifferent(&wires, &savedWires) ||
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
        beforeEdit->layout.socketRightLightAngle != layout->socketRightLightAngle ||
        beforeEdit->layout.socketLightOpacity != layout->socketLightOpacity ||
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
    if (isPlayer) savedLayout.playerJumpHeight = character->jumpHeight;
    savedLayout.npcPosition = isPlayer ? snapshot->npc.position : character->position;
    savedLayout.npcScale = isPlayer ? snapshot->npc.scale : character->scale;
    char stagePath[RPG_STAGE_PATH_LENGTH];
    if (!RpgStageStorage_EnsureStageDirectory(currentStageNumber) ||
        !RpgStageStorage_GetFilePath(currentStageNumber, "rpg_layout.cfg", stagePath,
                                     (int)sizeof(stagePath)) || !RpgLayout_Save(stagePath, &savedLayout))
        return false;
    if (isPlayer && !RpgLayout_SaveGlobalRuntime(&savedLayout)) return false;
    *layout = savedLayout;
    if (isPlayer) {
        snapshot->player = *character;
        snapshot->layout.playerJumpHeight = savedLayout.playerJumpHeight;
    }
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
        layout->playerJumpHeight = snapshot->layout.playerJumpHeight;
        character->jumpHeight = layout->playerJumpHeight;
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

/* Numeric inspector fields share editor_text.c's UTF-8 cursor and selection
   state.  This is deliberately presentation-only: the caller still commits
   the parsed value on Enter. */
static void DrawEditableTextField(Rectangle bounds, const char *text, bool isEditing,
                                     int cursorIndex, int selectionAnchor, int selectionEnd,
                                     bool isUnsaved)
{
    const float fontSize = 15.0f;
    const float textX = bounds.x + 4.0f;
    int length = text != NULL ? (int)strlen(text) : 0;
    cursorIndex = Clamp(cursorIndex, 0, length);
    selectionAnchor = Clamp(selectionAnchor, 0, length);
    selectionEnd = Clamp(selectionEnd, 0, length);
    DrawRectangleRec(bounds, Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLinesEx(bounds, 1.0f, isEditing ? BLUE : (isUnsaved ? MAROON : GRAY));
    if (isEditing && selectionAnchor != selectionEnd) {
        int start = selectionAnchor < selectionEnd ? selectionAnchor : selectionEnd;
        int end = selectionAnchor < selectionEnd ? selectionEnd : selectionAnchor;
        char prefix[64];
        char selectedText[64];
        memcpy(prefix, text, (size_t)start);
        prefix[start] = '\0';
        memcpy(selectedText, text + start, (size_t)(end - start));
        selectedText[end - start] = '\0';
        DrawRectangleRec((Rectangle){ textX + GameFont_MeasureText(prefix, fontSize).x,
                                      bounds.y + 4.0f,
                                      GameFont_MeasureText(selectedText, fontSize).x,
                                      18.0f },
                         Fade(SKYBLUE, 0.65f));
    }
    GameFont_Draw(text, textX, bounds.y + 4.0f, fontSize, BLACK);
    if (isEditing) {
        char prefix[64];
        memcpy(prefix, text, (size_t)cursorIndex);
        prefix[cursorIndex] = '\0';
        RpgEditorText_DrawCaret((int)(textX + GameFont_MeasureText(prefix, fontSize).x),
                                (int)bounds.y + 4, 18);
    }
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
                                     const RpgStage *stage, int mapIndex, int kind)
{
    if (!isCharacterDragPreviewVisible || characterDragPreviewKind != kind) return;
    RpgCharacter preview = kind == EDITOR_MAP_OBJECT_HIT_PLAYER ? *player :
                           kind == EDITOR_MAP_OBJECT_HIT_NPC ? *npc : zipper->character;
    Vector2 origin = GetEditorMapWorldOrigin(stage, mapIndex);
    preview.position.x = characterDragPointer.x + origin.x;
    preview.position.y = characterDragPointer.y + origin.y;
    preview = GetLocalCharacter(&preview, stage, mapIndex);
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

static void DrawEditorItems(const RpgItems *items, const RpgStage *stage, int mapIndex)
{
    Vector2 origin = GetEditorMapWorldOrigin(stage, mapIndex);
    for (int index = 0; index < items->count; index++) {
        if (RpgStage_GetMapAtWorldPosition(stage, items->entries[index].position) == mapIndex)
            DrawPoly(Vector2Subtract(items->entries[index].position, origin), 5, 15.0f, -90.0f, GOLD);
    }
}

static void DrawEditorMapEvents(const RpgStage *stage, int mapIndex)
{
    Vector2 origin = GetEditorMapWorldOrigin(stage, mapIndex);
    for (int index = 0; index < mapEvents.count; index++) {
        if (RpgStage_GetMapAtWorldPosition(stage, mapEvents.entries[index].position) == mapIndex) {
            Vector2 local = Vector2Subtract(mapEvents.entries[index].position, origin);
            DrawCircleLines((int)local.x, (int)local.y, 11.0f, ORANGE);
            DrawText("E", (int)local.x - 5, (int)local.y - 7, 14, ORANGE);
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

#if 0 /* Conveyor inspector retired with the conveyor system. */
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

#endif
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

/* `path` is a disposable build/runtime location.  A Folder's authored name
   is sourcePath, just as a FILE's picker selection is sourcePath. */
static const char *GetReferenceFolderAuthoredName(const RpgReferenceObject *object)
{
    if (object == NULL) return "";
    return object->sourcePath[0] != '\0' ? object->sourcePath :
           GetReferenceLeafName(object->path);
}

/* Temporary, write-through trace for the Folder name persistence path.
   This does not alter the rename result; it records whether input reached
   the commit, the physical directory move, and the following static save. */
static void TraceReferenceFolderRename(const char *eventName, int objectId,
                                       const char *oldName, const char *newName,
                                       bool succeeded, unsigned long systemError)
{
    char tracePath[RPG_STAGE_PATH_LENGTH];
    FILE *file;
    if (snprintf(tracePath, sizeof(tracePath), "%sfolder_rename_trace.log",
                 GetApplicationDirectory()) <= 0) return;
    file = fopen(tracePath, "ab");
    if (file == NULL) return;
    fprintf(file, "%0.3f %s id=%d old=[%s] new=[%s] ok=%d error=%lu\n",
            GetTime(), eventName != NULL ? eventName : "?", objectId,
            oldName != NULL ? oldName : "", newName != NULL ? newName : "",
            succeeded ? 1 : 0, systemError);
    fflush(file);
    fclose(file);
}

/* Several legacy saves contain multiple Folder records on one cell.  A
   physical folder can only have one name, so a rename updates every record
   sharing its old authored name, then retains the selected (topmost) record
   for that cell.  This prevents an older overlapping record from appearing
   to restore the old name after reload. */
static int ApplyReferenceFolderRename(RpgReferenceObjects *objects, int selectedId,
                                      const char *oldName, const char *newName,
                                      const char *newPath)
{
    Vector2 selectedPosition = { 0.0f, 0.0f };
    int writeIndex = 0;
    int selectedIndex = -1;
    bool foundSelected = false;
    if (objects == NULL || oldName == NULL || newName == NULL || newPath == NULL) return -1;
    for (int index = 0; index < objects->count; index++)
        if (objects->entries[index].id == selectedId) {
            selectedPosition = objects->entries[index].position;
            foundSelected = true;
            break;
        }
    if (!foundSelected) return -1;
    for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *object = &objects->entries[index];
        if (object->objectKind == RPG_REFERENCE_OBJECT_FOLDER &&
            strcmp(GetReferenceFolderAuthoredName(object), oldName) == 0) {
            snprintf(object->sourcePath, sizeof(object->sourcePath), "%s", newName);
            snprintf(object->path, sizeof(object->path), "%s", newPath);
        }
    }
    for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *object = &objects->entries[index];
        bool isOverlappingFolder = object->objectKind == RPG_REFERENCE_OBJECT_FOLDER &&
                                   object->position.x == selectedPosition.x &&
                                   object->position.y == selectedPosition.y &&
                                   object->id != selectedId;
        if (isOverlappingFolder) continue;
        if (writeIndex != index) objects->entries[writeIndex] = objects->entries[index];
        if (objects->entries[writeIndex].id == selectedId) selectedIndex = writeIndex;
        writeIndex++;
    }
    objects->count = writeIndex;
    return selectedIndex;
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
} RpgEditorFolderInspectorLayout;

static RpgEditorFolderInspectorLayout GetFolderInspectorLayout(void)
{
    return (RpgEditorFolderInspectorLayout){
        .nameBounds = { 716.0f, 144.0f, 188.0f, 28.0f },
        .openBounds = { 716.0f, 180.0f, 188.0f, 28.0f }
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
        /* Folder name changes are committed with Enter in the text field. */
    } else {
        DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716,
                         filePicker.contentBottom + 10.0f, 16, isUnsaved ? MAROON : BLACK);
    }
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

/* Shared settings are deliberately drawn from the object which uses them.
 * The values remain in RpgLayout: an object inspector is only another host
 * for the same setting, never a per-object copy. */
static void DrawSharedSettingsHeading(const char *scope, float y)
{
    DrawRectangle(712, (int)y - 4, 196, 22, Fade(SKYBLUE, 0.30f));
    DrawSettingsText(scope, 716, y, 14, DARKBLUE);
}

static void DrawSharedMagnetSpeedSetting(const RpgLayout *layout,
                                         const EditorSaveSnapshot *savedSnapshot, float y)
{
    bool changed = layout->magnetMetalSpeed != savedSnapshot->layout.magnetMetalSpeed;
    DrawSharedSettingsHeading("全体設定: 磁石", y);
    DrawSettingsText(TextFormat("磁石速度: %.0f px/秒", layout->magnetMetalSpeed),
                     716, y + 28, 16, changed ? MAROON : DARKGRAY);
    DrawRectangle(716, (int)y + 42, 44, 26, MAROON);
    DrawRectangle(772, (int)y + 42, 44, 26, DARKGREEN);
    DrawText("-", 733, (int)y + 46, 20, RAYWHITE);
    DrawText("+", 788, (int)y + 46, 20, RAYWHITE);
}

static bool HandleSharedMagnetSpeedSetting(RpgLayout *layout, Vector2 point, float y)
{
    if (CheckCollisionPointRec(point, (Rectangle){ 716, y + 42, 44, 26 })) {
        layout->magnetMetalSpeed = Clamp(layout->magnetMetalSpeed - 16.0f, 16.0f, 960.0f);
        return true;
    }
    if (CheckCollisionPointRec(point, (Rectangle){ 772, y + 42, 44, 26 })) {
        layout->magnetMetalSpeed = Clamp(layout->magnetMetalSpeed + 16.0f, 16.0f, 960.0f);
        return true;
    }
    return false;
}

static Rectangle GetSharedKeyDoorFailureBounds(float y)
{
    return (Rectangle){ 716.0f, y + 42.0f, 188.0f, 30.0f };
}

static void DrawSharedKeyDoorFailureSetting(const RpgLayout *layout,
                                            const EditorSaveSnapshot *savedSnapshot, float y)
{
    Rectangle bounds = GetSharedKeyDoorFailureBounds(y);
    bool changed = strcmp(layout->keyDoorFailureText, savedSnapshot->layout.keyDoorFailureText) != 0;
    DrawSharedSettingsHeading("全体設定: 鍵ドア", y);
    DrawSettingsText("鍵ドア失敗時の文章", 716, y + 24, 14, changed ? MAROON : DARKGRAY);
    DrawRectangleRec(bounds, Fade(LIGHTGRAY, 0.65f));
    DrawRectangleLinesEx(bounds, 1.0f, isGlobalKeyDoorFailureEditing ? BLUE :
                         (changed ? MAROON : GRAY));
    if (isGlobalKeyDoorFailureEditing &&
        globalKeyDoorFailureSelectionAnchor != globalKeyDoorFailureSelectionEnd) {
        int start = globalKeyDoorFailureSelectionAnchor < globalKeyDoorFailureSelectionEnd ?
                    globalKeyDoorFailureSelectionAnchor : globalKeyDoorFailureSelectionEnd;
        int end = globalKeyDoorFailureSelectionAnchor < globalKeyDoorFailureSelectionEnd ?
                  globalKeyDoorFailureSelectionEnd : globalKeyDoorFailureSelectionAnchor;
        char prefix[RPG_LAYOUT_KEY_DOOR_FAILURE_TEXT_LENGTH];
        memcpy(prefix, layout->keyDoorFailureText, (size_t)start);
        prefix[start] = '\0';
        float startX = 722.0f + GameFont_MeasureText(prefix, 15.0f).x;
        memcpy(prefix, layout->keyDoorFailureText, (size_t)end);
        prefix[end] = '\0';
        float endX = 722.0f + GameFont_MeasureText(prefix, 15.0f).x;
        DrawRectangleRec((Rectangle){ startX, bounds.y + 4.0f, endX - startX, 20.0f },
                         Fade(SKYBLUE, 0.65f));
    }
    GameFont_Draw(layout->keyDoorFailureText, 722, bounds.y + 7.0f, 15, BLACK);
    if (isGlobalKeyDoorFailureEditing) {
        char prefix[RPG_LAYOUT_KEY_DOOR_FAILURE_TEXT_LENGTH];
        int cursor = Clamp(globalKeyDoorFailureCursorIndex, 0, (int)strlen(layout->keyDoorFailureText));
        memcpy(prefix, layout->keyDoorFailureText, (size_t)cursor);
        prefix[cursor] = '\0';
        DrawTextCaret(722 + (int)GameFont_MeasureText(prefix, 15.0f).x,
                      (int)bounds.y + 4, 20);
    }
}

static bool BeginSharedKeyDoorFailureEdit(RpgLayout *layout, Vector2 point, float y)
{
    Rectangle bounds = GetSharedKeyDoorFailureBounds(y);
    if (!CheckCollisionPointRec(point, bounds)) return false;
    isGlobalKeyDoorFailureEditing = true;
    RpgEditorText_BeginPointerSelection(layout->keyDoorFailureText, point.x - 722.0f, 15.0f,
                                         &globalKeyDoorFailureCursorIndex,
                                         &globalKeyDoorFailureSelectionAnchor,
                                         &globalKeyDoorFailureSelectionEnd);
    return true;
}

static void DrawSharedSocketLightSetting(const RpgLayout *layout,
                                         const EditorSaveSnapshot *savedSnapshot, float y)
{
    bool changed = layout->socketRightLightAngle != savedSnapshot->layout.socketRightLightAngle ||
                   layout->socketLightOpacity != savedSnapshot->layout.socketLightOpacity;
    Color color = changed ? MAROON : DARKGRAY;
    DrawSharedSettingsHeading("ステージ設定: ソケット光", y);
    DrawSettingsText(TextFormat("右側の光: +%.0f度", layout->socketRightLightAngle),
                     716, y + 28, 15, color);
    DrawRectangle(816, (int)y + 42, 38, 24, MAROON);
    DrawRectangle(864, (int)y + 42, 38, 24, DARKGREEN);
    DrawText("-", 830, (int)y + 43, 20, RAYWHITE);
    DrawText("+", 877, (int)y + 43, 20, RAYWHITE);
    DrawSettingsText(TextFormat("光の透明度: %.0f%%", layout->socketLightOpacity * 100.0f),
                     716, y + 80, 15, color);
    DrawRectangle(816, (int)y + 94, 38, 24, MAROON);
    DrawRectangle(864, (int)y + 94, 38, 24, DARKGREEN);
    DrawText("-", 830, (int)y + 95, 20, RAYWHITE);
    DrawText("+", 877, (int)y + 95, 20, RAYWHITE);
}

static bool HandleSharedSocketLightSetting(RpgLayout *layout, Vector2 point, float y)
{
    if (CheckCollisionPointRec(point, (Rectangle){ 816, y + 42, 38, 24 })) {
        layout->socketRightLightAngle = Clamp(layout->socketRightLightAngle - 5.0f, 0.0f, 80.0f);
        return true;
    }
    if (CheckCollisionPointRec(point, (Rectangle){ 864, y + 42, 38, 24 })) {
        layout->socketRightLightAngle = Clamp(layout->socketRightLightAngle + 5.0f, 0.0f, 80.0f);
        return true;
    }
    if (CheckCollisionPointRec(point, (Rectangle){ 816, y + 94, 38, 24 })) {
        layout->socketLightOpacity = Clamp(layout->socketLightOpacity - 0.05f, 0.05f, 0.95f);
        return true;
    }
    if (CheckCollisionPointRec(point, (Rectangle){ 864, y + 94, 38, 24 })) {
        layout->socketLightOpacity = Clamp(layout->socketLightOpacity + 0.05f, 0.05f, 0.95f);
        return true;
    }
    return false;
}

static void DrawMagnetInspector(const RpgStage *stage, const RpgLayout *layout,
                                const EditorSaveSnapshot *savedSnapshot, int row, int column)
{
    const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(stage, row, column);
    if (magnet == NULL) return;
    DrawInspectorFrame(doorInspectorBounds, "磁石設定", DARKBLUE, GetInspectorCloseButton(5));
    DrawSettingsText(TextFormat("大きさ: %d x %d", magnet->width, magnet->height), 716, 122, 16, BLACK);
    DrawSettingsText("この値はステージ内の全磁石に共通です", 716, 150, 13, DARKGRAY);
    DrawSharedMagnetSpeedSetting(layout, savedSnapshot, 180.0f);
}

static void DrawDoorInspector(const RpgStage *stage, const RpgStage *savedStage,
                              const RpgLayout *layout, const EditorSaveSnapshot *savedSnapshot,
                              int row, int column)
{
    /* The key-file picker can grow for a wrapped path.  The shared global
       setting below it must live inside the same inspector surface rather
       than below the old fixed 300px door frame. */
    Rectangle inspectorBounds = GetInspectorBounds(5);
    if (RpgStage_GetMagnetAtCellConst(stage, row, column) != NULL) {
        DrawMagnetInspector(stage, layout, savedSnapshot, row, column);
        return;
    }
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsDoorBlock(stage->blocks[row][column])) return;
    bool isOpen = RpgBlockInventory_IsDoorOpen(stage->blocks[row][column]);
    bool isUnsaved = stage->blocks[row][column] != savedStage->blocks[row][column];
    const RpgKeyDoor *keyDoor = RpgStage_GetKeyDoorAtCellConst(stage, row, column);
    DrawInspectorFrame(inspectorBounds, "ドア設定", BROWN, GetInspectorCloseButton(5));
    if (keyDoor != NULL) {
        const RpgKeyDoor *savedDoor = RpgStage_GetKeyDoorAtCellConst(savedStage, row, column);
        bool isKeyUnsaved = savedDoor == NULL || strcmp(keyDoor->keyPath, savedDoor->keyPath) != 0;
        FileDialogRequest keyRequest = MakeFilesFileDialogRequest("鍵ファイルを選択");
        RpgEditorFilePickerLayout keyPicker = GetKeyDoorFilePickerLayout(keyDoor);
        DrawSettingsText(isOpen ? "鍵ドア: 開放" : "鍵ドア: 施錠", 716, 122, 16,
                         (isUnsaved || isKeyUnsaved) ? MAROON : BLACK);
        keyPicker = DrawInspectorFilePicker(&keyRequest, &keyPicker, keyDoor->keyPath, "鍵ファイル未選択",
                                             "ファイルを選択", isUnsaved || isKeyUnsaved, false);
        DrawSharedKeyDoorFailureSetting(layout, savedSnapshot, keyPicker.contentBottom + 38.0f);
        DrawSettingsText((isUnsaved || isKeyUnsaved) ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716,
                         keyPicker.contentBottom + 122.0f, 16,
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

/* Keep the shooter inspector's draw and input rectangles in one layout.
   Numeric fields used to be moved independently from their labels, which made
   the preview-file label overlap its +/- controls. */
typedef struct RpgShooterInspectorLayout {
    float sizeLabelY;
    float speedLabelY;
    float previewCountLabelY;
    float capacityLabelY;
    float pathHintY;
    float saveHintY;
    Rectangle sizeDecrease;
    Rectangle sizeIncrease;
    Rectangle speedInput;
    Rectangle speedDecrease;
    Rectangle speedIncrease;
    Rectangle previewButton;
    Rectangle previewCountDecrease;
    Rectangle previewCountIncrease;
    Rectangle capacityInput;
    Rectangle capacityDecrease;
    Rectangle capacityIncrease;
} RpgShooterInspectorLayout;

static RpgShooterInspectorLayout GetShooterInspectorLayout(void)
{
    return (RpgShooterInspectorLayout){
        .sizeLabelY = 174.0f,
        .speedLabelY = 226.0f,
        .previewCountLabelY = 318.0f,
        .capacityLabelY = 368.0f,
        .pathHintY = 420.0f,
        .saveHintY = 448.0f,
        .sizeDecrease = { 816.0f, 194.0f, 38.0f, 24.0f },
        .sizeIncrease = { 864.0f, 194.0f, 38.0f, 24.0f },
        .speedInput = { 716.0f, 244.0f, 94.0f, 24.0f },
        .speedDecrease = { 816.0f, 244.0f, 38.0f, 24.0f },
        .speedIncrease = { 864.0f, 244.0f, 38.0f, 24.0f },
        .previewButton = { 716.0f, 278.0f, 188.0f, 26.0f },
        .previewCountDecrease = { 816.0f, 336.0f, 38.0f, 24.0f },
        .previewCountIncrease = { 864.0f, 336.0f, 38.0f, 24.0f },
        .capacityInput = { 716.0f, 386.0f, 94.0f, 24.0f },
        .capacityDecrease = { 816.0f, 386.0f, 38.0f, 24.0f },
        .capacityIncrease = { 864.0f, 386.0f, 38.0f, 24.0f }
    };
}

static void DrawAttachmentInspector(const RpgLayout *layout, const EditorSaveSnapshot *savedSnapshot,
                                    int attachmentIndex, bool isPathEditing)
{
    if (attachmentIndex < 0 || attachmentIndex >= attachments.count) return;
    const RpgAttachment *attachment = &attachments.entries[attachmentIndex];
    bool isUnsaved = attachmentIndex >= savedAttachments.count ||
                     memcmp(attachment, &savedAttachments.entries[attachmentIndex], sizeof(*attachment)) != 0;
    Rectangle bounds = GetInspectorBounds(6);
    bool isButton = attachment->type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON;
    bool isSaveFlag = attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG;
    bool isBlockSocket = attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET;
    /* The values are stage-wide, but the socket is their natural editing
       context.  This host does not own a second copy of those values. */
    if (isBlockSocket) {
        Rectangle socketBounds = { 700, 80, 220, 270 };
        DrawInspectorFrame(socketBounds, "ブロック設置部品", SKYBLUE, GetInspectorCloseButton(6));
        DrawSettingsText("ブロックを固定する設置部品", 716, 122, 16, isUnsaved ? MAROON : BLACK);
        DrawSharedSocketLightSetting(layout, savedSnapshot, 154.0f);
        return;
    }
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
        DrawSettingsText("開始時のジッパー", 716, 220, 15, BLACK);
        Rectangle connectedBounds = { 716, 242, 90, 26 };
        Rectangle disconnectedBounds = { 814, 242, 90, 26 };
        const RpgFlagConfig *flag = RpgAttachments_FindFlagConst(&attachments, attachment->folderId);
        DrawRectangleRec(connectedBounds, flag != NULL && flag->startZipperConnected ? DARKBLUE : GRAY);
        DrawRectangleRec(disconnectedBounds, flag != NULL && flag->startZipperConnected ? GRAY : DARKBLUE);
        DrawRectangleLinesEx(connectedBounds, 1.0f, RAYWHITE);
        DrawRectangleLinesEx(disconnectedBounds, 1.0f, RAYWHITE);
        DrawSettingsText("接続", 742, 247, 15, RAYWHITE);
        DrawSettingsText("非接続", 831, 247, 15, RAYWHITE);
        DrawSettingsText("停止するとこのエリアへ戻る", 716, 278, 14, BLACK);
        DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, 306, 16,
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
    DrawSettingsText("実際の弾：フォルダー連動", 716, 122, 15, isUnsaved ? MAROON : BLACK);
    DrawSettingsText("起動：データボタン", 716, 150, 16, isUnsaved ? MAROON : BLACK);
    const RpgShooterInspectorLayout shooterLayout = GetShooterInspectorLayout();
    DrawSettingsText(TextFormat("1ファイルの大きさ: %.0fpx（8px刻み）", attachment->sizePerFile),
                     716, shooterLayout.sizeLabelY, 14, isUnsaved ? MAROON : BLACK);
    DrawRectangleRec(shooterLayout.sizeDecrease, MAROON);
    DrawText("-", 830, 195, 20, RAYWHITE);
    DrawRectangleRec(shooterLayout.sizeIncrease, DARKGREEN);
    DrawText("+", 877, 195, 20, RAYWHITE);
    DrawSettingsText("射出速度（100B）", 716, shooterLayout.speedLabelY, 14,
                     isUnsaved ? MAROON : BLACK);
    DrawEditableTextField(shooterLayout.speedInput,
                             isAttachmentSpeedEditing ? attachmentSpeedInput :
                             TextFormat("%.1f / 100B", attachment->dataSpeed),
                             isAttachmentSpeedEditing, attachmentSpeedCursorIndex,
                             attachmentSpeedSelectionAnchor, attachmentSpeedSelectionEnd, isUnsaved);
    DrawRectangleRec(shooterLayout.speedDecrease, MAROON);
    DrawText("-", 830, 245, 20, RAYWHITE);
    DrawRectangleRec(shooterLayout.speedIncrease, DARKGREEN);
    DrawText("+", 877, 245, 20, RAYWHITE);
    DrawRectangleRec(shooterLayout.previewButton, DARKBLUE);
    DrawSettingsText("弾をプレビュー", 760, 283, 16, RAYWHITE);
    DrawSettingsText(TextFormat("プレビュー用ファイル数: %d", attachment->previewFileCount),
                     716, shooterLayout.previewCountLabelY, 14, isUnsaved ? MAROON : BLACK);
    DrawRectangleRec(shooterLayout.previewCountDecrease, MAROON);
    DrawText("-", 830, 337, 20, RAYWHITE);
    DrawRectangleRec(shooterLayout.previewCountIncrease, DARKGREEN);
    DrawText("+", 877, 337, 20, RAYWHITE);
    DrawSettingsText("プレビュー容量", 716, shooterLayout.capacityLabelY, 14,
                     isUnsaved ? MAROON : BLACK);
    DrawEditableTextField(shooterLayout.capacityInput,
                             isAttachmentCapacityEditing ? attachmentCapacityInput :
                             TextFormat("%llu B", attachment->previewTotalBytes),
                             isAttachmentCapacityEditing, attachmentCapacityCursorIndex,
                             attachmentCapacitySelectionAnchor, attachmentCapacitySelectionEnd, isUnsaved);
    DrawRectangleRec(shooterLayout.capacityDecrease, MAROON);
    DrawText("-", 830, 387, 20, RAYWHITE);
    DrawRectangleRec(shooterLayout.capacityIncrease, DARKGREEN);
    DrawText("+", 877, 387, 20, RAYWHITE);
    (void)isPathEditing;
    DrawSettingsText("ブロックモード：軌道をドラッグ", 716, shooterLayout.pathHintY, 14, BLACK);
    DrawSettingsText(isUnsaved ? "未保存 - Sキーで全て保存" : "Sキーで全て保存", 716, shooterLayout.saveHintY, 15,
                     isUnsaved ? MAROON : BLACK);
}

static void DrawGlobalSettingsPanel(const RpgLayout *layout, const RpgCharacter *player,
                                    const RpgZipper *zipper, const EditorSaveSnapshot *savedSnapshot,
                                    RpgExplorerMode explorerMode, RpgBuildCellStorageMode storageMode)
{
    DrawInspectorFrame(globalSettingsPanelBounds, "全体設定", DARKBLUE,
                       GetInspectorCloseButton(RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR));
    Color delayColor = layout->electricCellDelay != savedSnapshot->layout.electricCellDelay ? MAROON : DARKGRAY;
    Color returnDurationColor = layout->zipperFolderReturnDuration !=
        savedSnapshot->layout.zipperFolderReturnDuration ? MAROON : DARKGRAY;
    Color returnDelayColor = layout->zipperFolderReturnAnimationDelay !=
        savedSnapshot->layout.zipperFolderReturnAnimationDelay ? MAROON : DARKGRAY;
    Color referenceFollowerScaleColor = layout->referenceFollowerScale !=
        savedSnapshot->layout.referenceFollowerScale ? MAROON : DARKGRAY;
    Color playerSpeedColor = player->moveSpeed != savedSnapshot->player.moveSpeed ? MAROON : DARKGRAY;
    Color playerScaleColor = player->scale != savedSnapshot->player.scale ? MAROON : DARKGRAY;
    Color playerJumpColor = layout->playerJumpHeight != savedSnapshot->layout.playerJumpHeight ?
        MAROON : DARKGRAY;
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
        DrawSettingsText("読み込み数 / フレーム", 716, y + 122, 15, DARKGRAY);
        DrawRectangle(716, (int)y + 142, 188, 26, RAYWHITE);
        DrawRectangleLinesEx((Rectangle){ 716, y + 142, 188, 26 }, 1.0f, DARKGRAY);
        DrawText(isBuildReadCountEditing ? buildReadCountInput :
                 TextFormat("%d", RpgBuildCellStorage_GetGenerationWorkPerUpdate()),
                 722, (int)y + 148, 16, DARKBLUE);
        if (isBuildReadCountEditing)
            DrawRectangle(722 + MeasureText(buildReadCountInput, 16), (int)y + 147, 2, 17, DARKBLUE);
    }
    if (globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_RUNTIME]) {
        float y = GetSettingsTabContentTop(globalSettingsTabs, globalSettingsTabExpanded,
                                           RPG_GLOBAL_SETTINGS_TAB_COUNT, RPG_GLOBAL_SETTINGS_TAB_RUNTIME);
        DrawSettingsText(TextFormat("導線遅延: %.2f秒", layout->electricCellDelay), 716, y + 6, 16, delayColor);
        DrawRectangle(716, (int)y + 20, 44, 26, MAROON); DrawText("-", 733, (int)y + 24, 20, RAYWHITE);
        DrawRectangle(772, (int)y + 20, 44, 26, DARKGREEN); DrawText("+", 788, (int)y + 24, 20, RAYWHITE);
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
        int jumpPixels = (int)roundf(layout->playerJumpHeight);
        int jumpTiles = jumpPixels / RPG_STAGE_TILE_SIZE;
        int jumpRemainder = jumpPixels % RPG_STAGE_TILE_SIZE;
        DrawSettingsText(TextFormat("ジャンプ高さ: %d マス + %d px", jumpTiles, jumpRemainder),
                         716, y + 106, 16, playerJumpColor);
        DrawRectangle(716, (int)y + 120, 42, 26, MAROON);
        DrawSettingsText("マス-", 719, y + 126, 12, RAYWHITE);
        DrawRectangle(762, (int)y + 120, 42, 26, DARKGREEN);
        DrawSettingsText("マス+", 765, y + 126, 12, RAYWHITE);
        DrawRectangle(814, (int)y + 120, 42, 26, MAROON);
        DrawSettingsText("px-", 821, y + 126, 12, RAYWHITE);
        DrawRectangle(860, (int)y + 120, 42, 26, DARKGREEN);
        DrawSettingsText("px+", 867, y + 126, 12, RAYWHITE);
        DrawSaveButton((Rectangle){ 716, y + 158, 90, 26 }, EDITOR_SAVE_NONE);
        DrawRevertButton((Rectangle){ 814, y + 158, 90, 26 });
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
        DrawEditableTextField((Rectangle){ 752.0f, y + 6.0f, 118.0f, 26.0f },
                              isStageNameEditing ? stageNameInput :
                              RpgStageCatalog_GetDisplayName(&stageCatalogData, currentStageNumber),
                              isStageNameEditing, stageNameCursorIndex,
                              stageNameSelectionAnchor, stageNameSelectionEnd, false);
        DrawSettingsText(TextFormat("%d / %d", stageIndex + 1, stageCatalogData.count), 780, y + 36, 13, DARKGRAY);
        DrawRectangle(716, (int)y + 56, 188, 26, DARKGREEN); DrawRectangleLinesEx((Rectangle){ 716, y + 56, 188, 26 }, 1.0f, RAYWHITE);
        DrawSettingsText("ステージを追加", 752, y + 61, 16, RAYWHITE);
        DrawRectangle(716, (int)y + 90, 188, 26, MAROON); DrawRectangleLinesEx((Rectangle){ 716, y + 90, 188, 26 }, 1.0f, RAYWHITE);
        DrawSettingsText("ステージを削除", 752, y + 95, 16, RAYWHITE);
        DrawSettingsText("ジッパー容量", 716, y + 128, 15, capacityChanged ? MAROON : DARKGRAY);
        DrawRectangle(796, (int)y + 122, 70, 26, RAYWHITE); DrawRectangleLinesEx((Rectangle){ 796, y + 122, 70, 26 }, 1.0f, capacityChanged ? MAROON : DARKGRAY);
        DrawText(isZipperCapacityEditing ? zipperCapacityInput : TextFormat("%u", layout->zipperMaxCapacityKB), 802, (int)y + 128, 16, capacityChanged ? MAROON : DARKBLUE);
        if (isZipperCapacityEditing) DrawRectangle(802 + MeasureText(zipperCapacityInput, 16), (int)y + 127, 2, 17, DARKBLUE);
        DrawSettingsText("KB", 872, y + 128, 15, DARKGRAY);
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
        DrawRectangle(716, (int)backgroundControlY + 22, 44, 24, MAROON); DrawText("-", 733, (int)backgroundControlY + 23, 20, RAYWHITE);
        DrawRectangle(772, (int)backgroundControlY + 22, 44, 24, DARKGREEN); DrawText("+", 788, (int)backgroundControlY + 23, 20, RAYWHITE);
        backgroundControlY += 56.0f;
        DrawSettingsText(TextFormat("ブロックの明るさ: %.0f%%", layout->blockBrightness * 100.0f), 716,
                         backgroundControlY + 8.0f, 15, visualChanged ? MAROON : DARKGRAY);
        DrawRectangle(716, (int)backgroundControlY + 22, 44, 24, MAROON); DrawText("-", 733, (int)backgroundControlY + 23, 20, RAYWHITE);
        DrawRectangle(772, (int)backgroundControlY + 22, 44, 24, DARKGREEN); DrawText("+", 788, (int)backgroundControlY + 23, 20, RAYWHITE);
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

/* Kept above the map rather than in the control bar: the palette can cover
 * that bar in block mode, and the UI text preset is intentionally black. */
static void DrawAreaShortcutHint(Rectangle mapViewport)
{
    Rectangle bounds = { mapViewport.x + mapViewport.width - 250.0f,
                         mapViewport.y + 10.0f, 240.0f, 46.0f };
    DrawRectangleRounded(bounds, 0.16f, 6, Fade(RAYWHITE, 0.94f));
    DrawRectangleLinesEx(bounds, 1.0f, DARKBLUE);
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "Ctrl+M: Map",
                        bounds.x + 10.0f, bounds.y + 6.0f,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 14.0f));
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "Ctrl+Arrow: Add area",
                        bounds.x + 10.0f, bounds.y + 25.0f,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 14.0f));
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

/* Palette names are editor-owned dynamic text.  They must join the font atlas
   after their preferences are loaded; otherwise a Japanese name saved to the
   config is restored correctly but renders as missing-glyph question marks on
   the next launch. */
static void RegisterBlockInventoryText(void)
{
    for (int index = 0; index < RpgBlockInventory_Count(); index++)
        (void)GameFont_AddText(RpgBlockInventory_Get(index)->name);
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
    if (blockType == RPG_BLOCK_PROPERTY_LEGACY_ITEM)
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
    return blockType == RPG_BLOCK_PROPERTY_LEGACY_ITEM ||
           blockType == RPG_BLOCK_PROPERTY_RECEIVER ||
           RpgBlockInventory_IsMapEventProperty(blockType) ||
           RpgBlockInventory_IsAttachment(blockType);
}

static void DrawBlockInventoryStaticErrorMarker(Rectangle cell)
{
    DrawRectangleRec(cell, (Color){ 210, 45, 45, 255 });
    DrawLineEx((Vector2){ cell.x + 6.0f, cell.y + 6.0f },
               (Vector2){ cell.x + cell.width - 6.0f, cell.y + cell.height - 6.0f }, 2.0f, RAYWHITE);
    DrawLineEx((Vector2){ cell.x + cell.width - 6.0f, cell.y + 6.0f },
               (Vector2){ cell.x + 6.0f, cell.y + cell.height - 6.0f }, 2.0f, RAYWHITE);
}

static void DrawBlockInventoryPreviewCell(Rectangle cell, const RpgBlockInventory *inventory,
                                          int blockType, int selectedBlockType, Texture2D fileTexture)
{
    (void)inventory;
    if (IsBlockInventoryOverlayType(blockType))
        DrawRectangleRec(cell, Fade(DARKGRAY, 0.9f));
    else RpgStage_DrawBlockCell(cell, blockType, cell.width / RPG_STAGE_TILE_SIZE);
    if (RpgBlockInventory_IsStaticErrorMarker(blockType))
        DrawBlockInventoryStaticErrorMarker(cell);
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
        if (RpgBlockInventory_IsStaticErrorMarker(blockType))
            DrawBlockInventoryStaticErrorMarker(cell);
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
        if (blockType == RPG_BLOCK_PROPERTY_LEGACY_ITEM)
            DrawPoly((Vector2){ cell.x + 16.0f, cell.y + 16.0f }, 5, 11.0f, -90.0f, GOLD);
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
static void DrawBlockContextMenu(Rectangle bounds)
{
    Rectangle dropBounds = GetBlockContextDropBounds(bounds);
    Rectangle deleteBounds = GetBlockContextDeleteBounds(bounds);
    Vector2 pointer = RpgViewport_GetMousePosition();
    DrawRectangleRec(bounds, (Color){ 36, 39, 45, 250 });
    DrawRectangleLinesEx(bounds, 1.0f, (Color){ 175, 180, 188, 255 });
    DrawRectangleRec(dropBounds, CheckCollisionPointRec(pointer, dropBounds) ? (Color){ 72, 75, 82, 255 } : (Color){ 54, 57, 63, 255 });
    DrawRectangleRec(deleteBounds, CheckCollisionPointRec(pointer, deleteBounds) ? (Color){ 182, 42, 52, 255 } : (Color){ 72, 75, 82, 255 });
    DrawText("Drop", (int)dropBounds.x + 12, (int)dropBounds.y + 6, 16, RAYWHITE);
    DrawText("Delete", (int)deleteBounds.x + 12, (int)deleteBounds.y + 7, 16, RAYWHITE);
}

// 既存の設置物ゴーストと同様、特殊ブロックは配置確定前にカーソル位置へ半透明で表示する。
static void DrawEffectBlockDragGhost(const RpgStage *stage, int mapIndex, int rootRow,
                                     int rootColumn, int grabRowOffset,
                                     int grabColumnOffset, Vector2 pointer)
{
    const RpgEffectShape *shape;
    const RpgMagnetObject *magnet;
    int destinationRow;
    int destinationColumn;
    if (stage == NULL || rootRow < 0 || rootColumn < 0 ||
        rootRow >= RPG_STAGE_ROWS || rootColumn >= RPG_STAGE_WORLD_COLUMNS) return;
    destinationColumn = (int)(pointer.x / RPG_STAGE_TILE_SIZE) + mapIndex * RPG_STAGE_COLUMNS -
                        grabColumnOffset;
    destinationRow = (int)(pointer.y / RPG_STAGE_TILE_SIZE) - grabRowOffset;
    shape = RpgBlockInventory_GetEffectShape(stage->blocks[rootRow][rootColumn]);
    Vector2 origin = { (destinationColumn - mapIndex * RPG_STAGE_COLUMNS) * RPG_STAGE_TILE_SIZE,
                       destinationRow * RPG_STAGE_TILE_SIZE };
    magnet = RpgStage_GetMagnetAtCellConst(stage, rootRow, rootColumn);
    if (magnet != NULL) {
        Rectangle bounds = { origin.x, origin.y,
                             magnet->width * RPG_STAGE_TILE_SIZE,
                             magnet->height * RPG_STAGE_TILE_SIZE };
        DrawRectangleRec(bounds, Fade(RpgStage_GetBlockColor(magnet->blockType), 0.55f));
        DrawRectangleLinesEx(bounds, 2.0f, PURPLE);
        DrawText("MOVE", (int)origin.x + 3, (int)origin.y + 17, 12, RAYWHITE);
        return;
    }
    if (shape == NULL) {
        /* A runtime-movable block is a one-cell object.  Draw it through the
           stage renderer so its editor ghost uses the same sprite as the
           placed object, then dim it to make the drag state explicit. */
        Rectangle cellBounds = { origin.x, origin.y, RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        RpgStage_DrawBlockCell(cellBounds, stage->blocks[rootRow][rootColumn], 1.0f);
        DrawRectangleRec(cellBounds, Fade(BLACK, 0.34f));
        DrawRectangleLinesEx(cellBounds, 2.0f, PURPLE);
        DrawText("MOVE", (int)origin.x + 3, (int)origin.y + 17, 12, RAYWHITE);
        return;
    }
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

static void DrawMagnetResizeGhost(const RpgStage *stage, int mapIndex, int rootRow,
                                  int rootColumn, int resizeEdges, Vector2 pointer)
{
    int targetColumn;
    int targetRow;
    int minimumRow;
    int maximumRow;
    int minimumColumn;
    int maximumColumn;
    const RpgMagnetObject *magnet;
    Rectangle bounds;
    if (stage == NULL || rootRow < 0 || rootColumn < 0) return;
    magnet = RpgStage_GetMagnetAtCellConst(stage, rootRow, rootColumn);
    if (magnet == NULL) return;
    targetColumn = (int)(pointer.x / RPG_STAGE_TILE_SIZE) + mapIndex * RPG_STAGE_COLUMNS;
    targetRow = (int)(pointer.y / RPG_STAGE_TILE_SIZE);
    GetMagnetResizeBounds(magnet, resizeEdges, targetRow, targetColumn,
                          &minimumRow, &minimumColumn, &maximumRow, &maximumColumn);
    if (minimumRow < 0 || maximumRow >= RPG_STAGE_ROWS ||
        minimumColumn / RPG_STAGE_COLUMNS != mapIndex ||
        maximumColumn / RPG_STAGE_COLUMNS != mapIndex) return;
    bounds = (Rectangle){ (minimumColumn - mapIndex * RPG_STAGE_COLUMNS) * RPG_STAGE_TILE_SIZE,
                          minimumRow * RPG_STAGE_TILE_SIZE,
                          (maximumColumn - minimumColumn + 1) * RPG_STAGE_TILE_SIZE,
                          (maximumRow - minimumRow + 1) * RPG_STAGE_TILE_SIZE };
    DrawRectangleRec(bounds, Fade(RpgStage_GetBlockColor(magnet->blockType), 0.42f));
    DrawRectangleLinesEx((Rectangle){ bounds.x + 1.0f, bounds.y + 1.0f,
                                      bounds.width - 2.0f, bounds.height - 2.0f },
                         2.0f, Fade(SKYBLUE, 0.94f));
}

// 編集画面のマップは16x8マスを960x480のゲーム領域へ等比で拡大する。
// UIはこの変換の外で描画・判定し、マップ操作だけを逆変換する。
static Rectangle GetEditorMapContentBounds(void)
{
    EditorWorkspaceLayout workspace = GetEditorWorkspaceLayout();
    float worldWidth = (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
    float worldHeight = (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE);
    float displayWidth = worldWidth + editorMapNeighborMargin * 2.0f;
    float displayHeight = worldHeight + editorMapNeighborMargin * 2.0f;
    float scale = fminf(workspace.mapViewport.width / displayWidth,
                         workspace.mapViewport.height / displayHeight);
    float width = displayWidth * scale;
    float height = displayHeight * scale;
    return (Rectangle){ workspace.mapViewport.x + (workspace.mapViewport.width - width) * 0.5f,
                        workspace.mapViewport.y + (workspace.mapViewport.height - height) * 0.5f,
                        width, height };
}

static float GetEditorMapScale(void)
{
    return GetEditorMapContentBounds().width /
           ((float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) + editorMapNeighborMargin * 2.0f);
}

static Vector2 GetEditorMapPointer(Vector2 screenPosition)
{
    Rectangle mapBounds = GetEditorMapContentBounds();
    float scale = GetEditorMapScale();
    return (Vector2){ (screenPosition.x - mapBounds.x) / scale - editorMapNeighborMargin,
                      (screenPosition.y - mapBounds.y) / scale - editorMapNeighborMargin };
}

/* Areas are camera sectors of one connected editor stage. */
static Vector2 GetEditorMapWorldOrigin(const RpgStage *stage, int mapIndex)
{
    if (stage == NULL || !RpgStage_IsMapActive(stage, mapIndex)) return (Vector2){ 0.0f, 0.0f };
    return (Vector2){ stage->mapGridX[mapIndex] * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                      -stage->mapGridY[mapIndex] * RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE };
}

/* The regular map rendering already draws every active area.  These guides
   make the immediately adjacent camera sectors explicit while the extra
   one-tile margin above keeps each shared border visible on every side. */
static void DrawEditorNeighborAreaGuides(const RpgStage *stage, int selectedMapIndex,
                                         float zoom)
{
    const float areaWidth = (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
    const float areaHeight = (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE);
    const float lineWidth = 1.0f / fmaxf(zoom, 0.01f);
    int selectedGridX;
    int selectedGridY;
    if (stage == NULL || !RpgStage_IsMapActive(stage, selectedMapIndex)) return;
    selectedGridX = stage->mapGridX[selectedMapIndex];
    selectedGridY = stage->mapGridY[selectedMapIndex];
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        Vector2 origin;
        int dx;
        int dy;
        if (!RpgStage_IsMapActive(stage, index)) continue;
        dx = stage->mapGridX[index] - selectedGridX;
        dy = stage->mapGridY[index] - selectedGridY;
        if (abs(dx) > 1 || abs(dy) > 1) continue;
        origin = GetEditorMapWorldOrigin(stage, index);
        DrawRectangleLinesEx((Rectangle){ origin.x, origin.y, areaWidth, areaHeight },
                             index == selectedMapIndex ? lineWidth * 2.0f : lineWidth,
                             index == selectedMapIndex ? Fade(SKYBLUE, 0.92f) : Fade(RAYWHITE, 0.70f));
    }
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
    DrawText("Drag to area: swap   Drag to empty: move   Arrow: move   Delete/right click: delete   Ctrl+Arrow: insert", (int)modalBounds.x + 24,
             (int)(modalBounds.y + modalBounds.height) - 24, 15, LIGHTGRAY);
}

static void DrawStageGenerationProgress(const RpgStage *stage, bool previewReady,
                                        bool previewDirty, int previewStageNumber,
                                        int currentStageNumber)
{
    const RpgStage *generationStage;
    int generated = 0, total = 0;
    int queuedCompleted = 0, queuedTotal = 0;
    bool pending = false;
    bool fullGenerationPending;
    bool generationWorkerActive;
    bool hasQueuedCellUpdate;
    bool previewIsCurrent;
    bool areaRepairActive;
    RpgGridCell targetCell = { 0 };
    bool hasTarget;
    const char *playStartStatus;
    char label[96], detail[160];
    Vector2 labelSize, detailSize, playStartSize;
    Rectangle bounds;
    float textWidth;
    if (stage == NULL) return;
    /* The background generator owns the isolated preview-stage copy.  The
       editable stage may already contain a newer unsynchronised edit, so use
       the same stage object for the progress count and target label. */
    generationStage = previewStageNumber == currentStageNumber ?
        &editorPreviewBuildStage : stage;
    RpgBuildCellStorage_GetGenerationProgress(generationStage, &generated, &total, &pending);
    /* A one-cell authoring delta may be queued while the initial full preview
       is still being materialised.  The full queue remains the authoritative
       readiness prerequisite in that case; never replace its progress with
       the later 1/1 delta display. */
    fullGenerationPending = pending;
    generationWorkerActive = RpgBuildCellStorage_IsGenerationPending();
    hasQueuedCellUpdate = RpgBuildCellStorage_GetQueuedCellUpdateProgress(&queuedCompleted,
                                                                            &queuedTotal);
    /* An ordinary terrain edit rewrites an existing compact cell.  Its
       materialization count remains full, so show the actual delta batch
       while it is running rather than a misleading unchanged stage total. */
    /* Keep a just-finished one-cell update visible until the preview batch is
       committed.  Otherwise an 8-cell worker budget can finish 0/1 -> 1/1
       before the next frame is drawn, making the counter appear frozen. */
    if (hasQueuedCellUpdate && !fullGenerationPending &&
        (queuedCompleted < queuedTotal || (previewDirty && !previewReady))) {
        generated = queuedCompleted;
        total = queuedTotal;
        pending = queuedCompleted < queuedTotal;
    }
    /* The compact generator can legitimately report no count while a
       previously prepared preview is still valid (for example an
       attachment-only metadata update).  The editor-preview state is the
       authority for readiness; counters are informational only. */
    previewIsCurrent = previewReady && !previewDirty &&
                       previewStageNumber == currentStageNumber;
    /* Area repair is exceptional: retain an unmistakable border for the
       whole repair job, not only for the one frame used to announce it. */
    areaRepairActive = editorPreviewAreaRepairAnnounced ||
        (editorPreviewPreparationKinds & RPG_EDITOR_PREVIEW_PREPARATION_AREA_REPAIR) != 0;
    hasTarget = pending && RpgBuildCellStorage_GetGenerationTarget(&targetCell);
    if (total > 0)
        snprintf(label, sizeof(label), "ロード: %d / %d%s", generated, total,
                 pending ? "" : "  完了");
    else
        snprintf(label, sizeof(label), "ロード: %s", previewIsCurrent ? "完了" : "待機中");
    if (hasTarget) {
        int mapIndex = targetCell.column / RPG_STAGE_COLUMNS;
        snprintf(detail, sizeof(detail), "処理中: エリア (%d, %d) のマス (%d, %d) を生成中",
                 generationStage->mapGridX[mapIndex], generationStage->mapGridY[mapIndex],
                 targetCell.column % RPG_STAGE_COLUMNS, targetCell.row);
    } else {
        const bool preparing = !previewIsCurrent;
        snprintf(detail, sizeof(detail), "%s", pending ? "処理中: 生成データを保存中" :
                 (preparing ? GetEditorPreviewPreparationDetail() : "処理: 生成完了"));
    }
    /* Cell generation is only one prerequisite.  Do not advertise readiness
       until the clean preview cache also covers every active cell. */
    if (pending || generationWorkerActive)
        playStartStatus = "プレイ前: セル生成中";
    else if (previewIsCurrent)
        playStartStatus = "プレイ前: 準備完了";
    else
        playStartStatus = "プレイ前: キャッシュを確認中";
    labelSize = GameFont_MeasurePreset(RPG_TEXT_PRESET_UI, label,
                                       GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 14.0f));
    detailSize = GameFont_MeasurePreset(RPG_TEXT_PRESET_UI, detail,
                                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 12.0f));
    playStartSize = GameFont_MeasurePreset(RPG_TEXT_PRESET_UI, playStartStatus,
                                           GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 12.0f));
    textWidth = labelSize.x > detailSize.x ? labelSize.x : detailSize.x;
    if (playStartSize.x > textWidth) textWidth = playStartSize.x;
    bounds = (Rectangle){ (float)RpgViewport_GetWidth() - textWidth - 26.0f,
                          8.0f, textWidth + 18.0f, 62.0f };
    /* Match the editor's ordinary UI: grey panel with the shared UI preset. */
    DrawRectangleRec(bounds, Fade(LIGHTGRAY, 0.96f));
    DrawRectangleLinesEx(bounds, areaRepairActive ? 2.0f : 1.0f,
                         areaRepairActive ? RED : (pending ? GRAY : DARKGRAY));
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, label, bounds.x + 9.0f, bounds.y + 5.0f,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 14.0f));
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, detail, bounds.x + 9.0f, bounds.y + 24.0f,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 12.0f));
    GameFont_DrawPreset(RPG_TEXT_PRESET_UI, playStartStatus, bounds.x + 9.0f, bounds.y + 42.0f,
                        GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 12.0f));
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
                       bool isBlockContextMenuOpen, Rectangle blockContextMenuBounds,
                       bool previewBuildReady, bool previewBuildDirty,
                       int previewBuildStageNumber, int currentStageNumber)
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
                           .target = { selectedMapWorldOrigin.x - editorMapNeighborMargin,
                                       selectedMapWorldOrigin.y - editorMapNeighborMargin },
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
        RpgImageObjects_DrawLayerAtWorldOrigin(&stage->imageObjects, drawMapIndex, RPG_STAGE_COLUMNS,
                                               RPG_STAGE_TILE_SIZE, worldOrigin, WHITE,
                                               RPG_IMAGE_OBJECT_LAYER_BACK);
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
    RpgImageObjects_DrawLayerAtWorldOrigin(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                                           RPG_STAGE_TILE_SIZE, selectedMapWorldOrigin, WHITE,
                                           RPG_IMAGE_OBJECT_LAYER_BACK);
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
    DrawEditorItems(items, stage, mapIndex);
    DrawEditorMapEvents(stage, mapIndex);
    RpgStage_DrawMapEffects(stage, mapIndex);
    RpgMagnets_DrawFields(stage, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    RpgSignalBlocks_DrawPreview(&signalBlocks, stage, mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    if (isEffectBlockDragPreviewVisible)
        DrawEffectBlockDragGhost(stage, mapIndex, effectBlockDragPreviewRow,
                                 effectBlockDragPreviewColumn, effectBlockDragPreviewGrabRowOffset,
                                 effectBlockDragPreviewGrabColumnOffset, effectBlockDragPointer);
    if (isMagnetResizePreviewVisible)
        DrawMagnetResizeGhost(stage, mapIndex, magnetResizePreviewRootRow,
                              magnetResizePreviewRootColumn, magnetResizeEdges,
                              effectBlockDragPointer);
    RpgWires_DrawMap(&wires, stage, &receivers, mapIndex);
    RpgWires_DrawElectric(&wires, dataShots, &receivers,
                          mapIndex * RPG_STAGE_COLUMNS, RPG_STAGE_COLUMNS);
    RpgReceivers_DrawMap(&receivers, mapIndex);
    /* Edit mode has no runtime movable-block set.  Editor Play renders via
       the shared runtime path, which supplies the live blockers. */
    RpgAttachments_DrawSocketLightsMap(&attachments, stage, mapIndex, NULL,
                                       layout->socketRightLightAngle, layout->socketLightOpacity);
    RpgAttachments_DrawMapExcept(&attachments, mapIndex, attachmentDragDrawSkipIndex);
    RpgDataShots_DrawMap(dataShots, mapIndex);
    RpgImageObjects_DrawLayerAtWorldOrigin(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                                           RPG_STAGE_TILE_SIZE, selectedMapWorldOrigin, WHITE,
                                           RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
    DrawImageObjectDragPreviewForLayer(stage, RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
    if (blockMode) RpgAttachments_DrawDataPaths(&attachments, mapIndex);
    /* Keep wires and shooter paths persistent while their owner is held, but
       make a missing/error cell visibly cut every authored route. */
    RpgStage_DrawMapUnavailableOverlay(stage, mapIndex, blockMode, layout->blockBrightness);
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
    if (IsCharacterInMap(npc, stage, mapIndex)) {
        RpgCharacter localNpc = GetLocalCharacter(npc, stage, mapIndex);
        RpgCharacter_Draw(&localNpc, "NPC");
        if (selected == 2) DrawCircleLines((int)localNpc.position.x, (int)localNpc.position.y, 32, PURPLE);
    }
    DrawCharacterDragPreview(zipperTexture, player, npc, zipper, stage, mapIndex, EDITOR_MAP_OBJECT_HIT_NPC);
    if (IsCharacterInMap(player, stage, mapIndex)) {
        RpgCharacter localPlayer = GetLocalCharacter(player, stage, mapIndex);
        /* 関数列プレビューで指定したwalkは、接地状態に左右されず必ずwalkシートを描く。 */
        RpgCharacter_DrawPlayer(&localPlayer,
                                isFunctionPreviewPlaying && localPlayer.isMoving ?
                                    RPG_CHARACTER_ANIMATION_WALK : RPG_CHARACTER_ANIMATION_AUTOMATIC);
        if (selected == 1) DrawCircleLines((int)localPlayer.position.x, (int)localPlayer.position.y, 32, BLUE);
    }
    DrawCharacterDragPreview(zipperTexture, player, npc, zipper, stage, mapIndex, EDITOR_MAP_OBJECT_HIT_PLAYER);
    RpgImageObjects_DrawLayerAtWorldOrigin(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                                           RPG_STAGE_TILE_SIZE, selectedMapWorldOrigin, WHITE,
                                           RPG_IMAGE_OBJECT_LAYER_FRONT);
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
    DrawEditorNeighborAreaGuides(stage, mapIndex, mapCamera.zoom);
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
    if (!isEditorPlaying)
        DrawText(TextFormat("Area (%d, %d)", stage->mapGridX[mapIndex], stage->mapGridY[mapIndex]),
                 562, 491, 16, MAROON);
    if (isEditorPlaying)
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "プレイ中：A/D 移動　W ジャンプ　Ctrl+P 停止",
                            16.0f, 518.0f, GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 14.0f));
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
    if (!isEditorPlaying)
        DrawAreaShortcutHint(workspace.mapViewport);
    bool hasUnsavedChanges = HasAnyUnsavedChanges(savedSnapshot, player, npc, layout, stage, dialogue, stage3Event,
                                                   items, savedItems) || RpgStageCatalog_IsDirty(&stageCatalogData);
    if (!isEditorPlaying && !blockMode) {
        DrawRectangleRec(revertSavedBounds, hasUnsavedChanges ? MAROON : GRAY);
        GameFont_DrawPreset(RPG_TEXT_PRESET_UI, "Revert saved",
                            revertSavedBounds.x + 8.0f, revertSavedBounds.y + 5.0f,
                            GameFont_GetPresetScale(RPG_TEXT_PRESET_UI, 16.0f));
        if (AreWiresDifferent(&wires, &savedWires)) DrawText("Wires: unsaved", 720, 518, 14, MAROON);
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
        else DrawDoorInspector(stage, &savedSnapshot->stage, layout, savedSnapshot,
                               selectedDoorRow, selectedDoorColumn);
    } else if (selected == 6) {
        DrawAttachmentInspector(layout, savedSnapshot, selectedAttachmentIndex, isAttachmentPathEditing);
    } else if (selected == 7) {
        DrawReferenceInspector(stage, &savedSnapshot->stage, selectedReferenceObjectIndex,
                               isReferencePathEditing, referencePathCursorIndex,
                               referencePathSelectionAnchor, referencePathSelectionEnd,
                               referenceFolderNameInput);
    } else if (selected == RPG_EDITOR_IMAGE_INSPECTOR) {
        DrawImageObjectInspector(stage, &savedSnapshot->stage, selectedImageObjectIndex);
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
    /* Keep the Play-cache state visible after completion as well.  A blank
       corner made "ready" indistinguishable from a stalled or unavailable
       cache. */
    if (isEditorPlaying || previewBuildDirty ||
        previewBuildStageNumber == currentStageNumber)
        DrawStageGenerationProgress(stage, previewBuildReady, previewBuildDirty,
                                    previewBuildStageNumber, currentStageNumber);
    if (isExitConfirmationOpen) DrawExitConfirmation(isExitDetailsOpen, savedSnapshot, player, npc, stage,
                                                      dialogue, stage3Event, items, savedItems, detailScroll);
    /* The shared game settings affordance belongs to the running game only.
       Editor configuration continues to use its own inspectors, so it must
       not leave a clickable game-settings button over the editor canvas. */
    if (isEditorPlaying && RpgScene_IsGameSettings(scene))
        RpgScene_DrawGameSettingsOverlay(scene);
    else if (isEditorPlaying && scene != NULL)
        RpgScene_DrawGameSettingsButton();
    if (isBlockContextMenuOpen) DrawBlockContextMenu(blockContextMenuBounds);
    if (isGlobalMapOpen) DrawGlobalMapModal(stage, mapIndex);
    }
    RpgViewport_EndFrame();
}

int main(void)
{
    /* エディターは設計データを直接読み書きし、保存時に本編用パッケージを更新する。 */
    RpgStageStorage_SetDomain(RPG_STAGE_STORAGE_SETTINGS);
    RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_STATIC);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT, "1_44MB - RPG Editor");
    ClearWindowState(FLAG_FULLSCREEN_MODE | FLAG_BORDERLESS_WINDOWED_MODE | FLAG_WINDOW_MAXIMIZED);
    SetWindowSize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
    SetWindowPosition(80, 80);
    RpgViewport_Initialize();
    SetExitKey(KEY_NULL);
    InstallEditorCloseHandler();
    (void)RpgGameWindow_Install(GetWindowHandle(), 40.0f);
    /* A same-size request emits no WM_SIZE.  Make one invisible, temporary
       resize before the first frame so raylib and the custom chrome agree. */
    SetWindowSize(RPG_EDITOR_WIDTH + 1, RPG_EDITOR_HEIGHT);
    SetWindowSize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
    SetTargetFPS(60);
    // エディター起動時にも、プレイ中に残った一時objectフォルダとInboxを掃除する。
    RpgObjectFolders_ClearSessionStorage();
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));
    GameFont_BeginTextBatch();
    (void)LoadBlockInventoryPreferences();
    RegisterBlockInventoryText();
    RegisterSettingsUiText();
    (void)GameFont_EndTextBatch();
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
    /* See LoadEditorStageState(): preserve the local authoring position when
       an old player coordinate no longer belongs to the current 2D area grid. */
    if (RpgStage_GetMapAtWorldPosition(&stage, layout.playerPosition) < 0) {
        const int playerMap = RpgStage_FindNearestActiveMap(&stage, 0);
        if (playerMap >= 0) {
            const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
            const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
            const float localX = layout.playerPosition.x -
                                 floorf(layout.playerPosition.x / mapWidth) * mapWidth;
            const float localY = layout.playerPosition.y -
                                 floorf(layout.playerPosition.y / mapHeight) * mapHeight;
            layout.playerPosition = (Vector2){
                stage.mapGridX[playerMap] * mapWidth + localX,
                -stage.mapGridY[playerMap] * mapHeight + localY
            };
        }
    }
    RegisterStageFilePickerText(&stage);
    // 保存済みFileの日本語パスも、選択前から ? にならないようフォントへ登録する。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (RpgBlockInventory_IsReferenceObject(stage.blocks[row][column])) {
            GameFont_AddText(RpgStage_GetReferencePathAtCell(&stage, row, column));
        }
    wires = stageLoadBuffer.wires;
    savedWires = wires;
    (void)RpgAttachments_Clone(&attachments, &stageLoadBuffer.attachments);
    RpgReceivers_Bind(&receivers, &attachments);
    RpgObjectFolders_PrepareBlockOwnedMetadata(&attachments, &receivers, &wires);
    RpgObjectFolder_PrepareZipperAnimationCommand();
    (void)RpgAttachments_Clone(&savedAttachments, &attachments);
    RpgReceivers_Bind(&savedReceivers, &savedAttachments);
    signalBlocks = stageLoadBuffer.signalBlocks;
    savedSignalBlocks = signalBlocks;
    previewStage = &stage;
    attachmentPreviewShots = RpgDataShots_Default();
    previewEvent = RpgPreviewEvent_Default();
    previewSystem = RpgPreviewSystem_Default();
    RpgPreviewSystem_Register(&previewSystem, PreviewRadioEmitters, NULL);
    RpgPreviewSystem_Register(&previewSystem, PreviewSignalShrinkBlocks, &signalBlocks);
    /* Star items are retired.  Keep an empty legacy editor buffer only so
       old undo/UI paths cannot materialize an item from a saved stage. */
    RpgItems items = RpgItems_Default();
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
    player.jumpHeight = layout.playerJumpHeight;
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
    enum {
        RPG_EDITOR_POINTER_TARGET_NONE,
        RPG_EDITOR_POINTER_TARGET_RECEIVER,
        RPG_EDITOR_POINTER_TARGET_ATTACHMENT_PATH,
        RPG_EDITOR_POINTER_TARGET_WIRE_ENDPOINT
    };
    /* Click and drag candidates are deliberately independent: a receiver may
       share its cell with a draggable trajectory endpoint. */
    RpgEditorPointerInteraction mapObjectPointerInteraction = { 0 };
    RpgEditorDrag attachmentDrag = { 0 };
    int draggedAttachmentIndex = -1;
    RpgAttachment draggedAttachmentBeforeEdit = { 0 };
    bool isAttachmentErasePointerHeld = false;
    bool isBlockContextMenuOpen = false;
    int blockContextMenuRow = -1;
    int blockContextMenuColumn = -1;
    Rectangle blockContextMenuBounds = { 0 };
    RpgEditorDrag effectBlockDrag = { 0 };
    bool isMagnetResizeDrag = false;
    int draggedEffectBlockRow = -1;
    int draggedEffectBlockColumn = -1;
    int pendingEffectBlockRow = -1;
    int pendingEffectBlockColumn = -1;
    EditorObjectMoveAnchor effectBlockMoveAnchor = { 0 };
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
    /* Area-entry events make a complete Play snapshot too large for the
       executable's main-thread stack.  Keep the one editor snapshot in static
       storage so Stop always restores the editor instead of terminating it. */
    /* Play owns this separate dynamic copy.  The editor variables below stay
       the static authoring source throughout a Play session. */
    static RpgEditorPlayRuntime editorPlayRuntime = { 0 };
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
    RpgButtonEvent editorPlaySocketCommunicationEvent = RpgButtonEvent_Default();
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
    double nextEditorPreviewWorkTime = 0.0;
    /* Publishing a new editor snapshot must wait for a pointer release, but
       advancing an already-published disposable cell queue is independent of
       the live authoring model.  Keep its small per-frame budget separate so
       a held drag cannot leave "cell generation" stalled. */
    double nextEditorPreviewGenerationTime = 0.0;
    bool wasEditorPreviewInputBusy = false;
    bool editorPreviewBuildDirty = true;
    bool editorPreviewBuildReady = false;
    /* Fresh preview creation deliberately materialises distant object-folder
       cells over subsequent editor frames.  This is neither an authored edit
       nor an area repair: keep it out of the snapshot/repair worker until the
       cell generator has drained. */
    bool editorPreviewAwaitingCellGeneration = false;
    int editorPreviewBuildStageNumber = 0;

    /* Prepare the disposable Play tree ahead of time.  Subsequent work is
       cooperative and only runs while no editor pointer operation is active. */
    editorPreviewBuildReady = EnsureEditorPreviewCache(currentStageNumber, &layout, &stage);
    editorPreviewAwaitingCellGeneration = editorPreviewBuildReady &&
        RpgBuildCellStorage_IsGenerationPending();
    editorPreviewInitialGenerationBaselinePending = editorPreviewAwaitingCellGeneration;
    editorPreviewBuildReady = editorPreviewBuildReady && !editorPreviewAwaitingCellGeneration;
    editorPreviewBuildDirty = !editorPreviewBuildReady;
    editorPreviewBuildStageNumber = (editorPreviewBuildReady || editorPreviewAwaitingCellGeneration) ?
        currentStageNumber : 0;
    if (editorPreviewAwaitingCellGeneration)
        MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_CELLS);
    ComposeEditorStageData(&stageFolderSyncBuffer, &layout, &stage, &dialogue, &stage3Event, &items);
    NormalizeEditorPreviewSyncData(&stageFolderSyncBuffer);
    (void)CopyEditorStageData(&lastStageFolderSync, &stageFolderSyncBuffer);
    hasStageFolderSync = true;
    lastStageFolderSyncNumber = currentStageNumber;
    nextStageFolderComparisonTime = GetTime();
    nextStageFolderSyncTime = (editorPreviewBuildReady || editorPreviewAwaitingCellGeneration) ?
        0.0 : GetTime();

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
                                   isInspectTitleEditing || isGlobalKeyDoorFailureEditing ||
                                   isAttachmentCapacityEditing || isAttachmentSpeedEditing ||
                                   isZipperCapacityEditing || isStageNameEditing || isBuildReadCountEditing ||
                                   paletteNameEditingIndex >= 0;
        RpgEditorText_SetKeyboardCapture(isEditorTextEditing);
        /* Escape leaves text-entry mode only.  The capture state deliberately
           remains true until the next frame, so this same Escape cannot also
           close a modal or clear the editor selection. */
        if (RpgEditorText_ConsumeEscape()) {
            isDialogueTextEditing = false;
            isSpeakerEditing = false;
            isItemNameEditing = false;
            isReferencePathEditing = false;
            isAttachmentPathEditing = false;
            isInspectTitleEditing = false;
            isGlobalKeyDoorFailureEditing = false;
            isAttachmentCapacityEditing = false;
            isAttachmentSpeedEditing = false;
            isZipperCapacityEditing = false;
            isStageNameEditing = false;
            isBuildReadCountEditing = false;
            paletteNameEditingIndex = -1;
        }
        bool isKeyboardCaptured = RpgEditorText_IsKeyboardCaptured();
        if (!isEditorPlaying && !isKeyboardCaptured && !RpgScene_IsGameSettings(&editorScene) &&
            isEditorFullscreenShortcut) {
            isEditorGameFullscreen = !isEditorGameFullscreen;
        }
        // エディター内Playの設定ボタンは RpgRuntime_UpdateAndDraw() が
        // 開く・更新する・描画する。ここで先に入力を消費すると、その
        // ランタイム更新自体を含む下のブロックを飛ばしてしまう。
        bool suppressEditorInput = isEditorGameFullscreen;
        if (!suppressEditorInput) {
        // Revertや削除の直後でも、画面は常に有効な二次元ステージIDを参照する。
        if (!isEditorPlaying) mapIndex = RpgStage_FindNearestActiveMap(&stage, mapIndex);
        if (draggedMapEventIndex >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) draggedMapEventIndex = -1;
        bool blockEditedThisFrame = false;
        EditorPreviewEditBatch editorPreviewEditsThisFrame = { 0 };
        if (isEditorPlaying) {
            RpgRuntimeContext runtime = {
                .layout=&editorPlayRuntime.layout, .stageBackground=&stageBackground, .stage=&editorPlayRuntime.stage, .referenceDrops=&editorPlayReferenceDrops, .wires=&editorPlayRuntime.wires, .receivers=&editorPlayRuntime.receivers, .attachments=&editorPlayRuntime.attachments, .signalBlocks=&editorPlayRuntime.signalBlocks, .dataShots=&editorPlayShots, .buttonEvent=&editorPlayButtonEvent, .socketCommunicationEvent=&editorPlaySocketCommunicationEvent, .events=&editorPlayRuntime.mapEvents, .dialogue=&editorPlayRuntime.dialogue, .stage3Event=&editorPlayRuntime.stage3Event, .areaEntryEvents=&editorPlayRuntime.areaEntryEvents, .zipper=&editorPlayRuntime.zipper, .inspect=&editorPlayRuntime.runtimeNpcInspect, .player=&editorPlayRuntime.player, .npc=&editorPlayRuntime.npc, .magnetRuntime=&editorPlayMagnetRuntime,
                .dialogueIndex=&editorPlayDialogueIndex, .stage3IntroIndex=&editorPlayStage3IntroIndex, .inspectFunctionIndex=&editorPlayInspectFunctionIndex, .inspectLineIndex=&editorPlayInspectLineIndex, .inspectTarget=&editorPlayInspectTarget, .isInspectMoveRunning=&isEditorPlayInspectMoveRunning, .inspectMoveElapsed=&editorPlayInspectMoveElapsed, .inspectMoveStartX=&editorPlayInspectMoveStartX, .inspectMoveStartY=&editorPlayInspectMoveStartY, .activeInspectMove=&editorPlayActiveInspectMove, .inspectMoveTransitionElapsed=&editorPlayInspectMoveTransitionElapsed, .activeWaitFunctionIndex=&editorPlayActiveWaitFunctionIndex, .inspectWaitElapsed=&editorPlayInspectWaitElapsed, .stage3IntroShown=&editorPlayStage3IntroShown, .areaEntryShown=editorPlayAreaEntryShown, .activeEntryEvent=&editorPlayActiveEntryEvent, .zipperFollowsPlayer=&editorPlayZipperFollowsPlayer, .isZipperLaunched=&editorPlayZipperLaunched, .zipperLaunchVelocity=&editorPlayZipperLaunchVelocity, .attachedDataShotIndex=&editorPlayAttachedDataShotIndex, .attachedAttachmentIndex=&editorPlayAttachedAttachmentIndex, .attachedDataShotOffset=&editorPlayAttachedDataShotOffset, .attachedDynamicBlockIndex=&editorPlayAttachedDynamicBlockIndex, .attachedReferenceObjectIndex=&editorPlayAttachedReferenceObjectIndex,
                .zipperPointerSelected=&editorPlayZipperPointerSelected, .isZipperPointerFeedbackSuppressed=&editorPlayZipperPointerFeedbackSuppressed, .lastZipperPointerClickTime=&editorPlayLastZipperPointerClickTime, .selectedReferencePointerTarget=&editorPlaySelectedReference, .isReferencePointerFeedbackSuppressed=&editorPlayReferencePointerFeedbackSuppressed, .isReferencePointerPressed=&editorPlayReferencePointerPressed, .pressedReferenceTarget=&editorPlayPressedReference, .referencePressPosition=&editorPlayReferencePressPosition, .isReferenceDragActive=&editorPlayReferenceDragActive, .draggedReferenceTarget=&editorPlayDraggedReference, .referenceDragPosition=&editorPlayReferenceDragPosition, .lastReferencePointerClickTime=&editorPlayLastReferenceClickTime, .zipperAnimationElapsed=&editorPlayZipperAnimationElapsed, .npcInspectCompleted=&editorPlayNpcInspectCompleted, .zipperInspectCompleted=&editorPlayZipperInspectCompleted, .isZipperControllable=&editorPlayZipperControllable, .wasDataButtonPressed=&wasEditorPlayButtonPressed, .previousMap=&editorPlayPreviousMap, .worldCoordinatesInitialized=&editorPlayWorldCoordinatesInitialized, .cameraFollowsPlayer=&editorPlayCameraFollowsPlayer, .itemMessage=editorPlayItemMessage, .itemMessageSize=(int)sizeof(editorPlayItemMessage), .itemMessageTimer=&editorPlayItemMessageTimer, .referenceText=editorPlayReferenceText, .referenceTextSize=(int)sizeof(editorPlayReferenceText), .referenceFileName=editorPlayReferenceFileName, .referenceFileNameSize=(int)sizeof(editorPlayReferenceFileName), .isReferenceTextOpen=&editorPlayReferenceTextOpen, .camera=&editorPlayCamera, .zipperTexture=zipperTexture, .fileTexture=fileTexture,
                // エディターではゲーム設定だけを共有し、タイトルへの遷移は許可しない。
                .scene=&editorScene
            };
            runtime.showStopButton = true;
            /* エディター内Playも本編と同じbuild監視を通し、作成済みマスのフォルダ変更を反映する。 */
            RpgStageBuild_Update(&editorPlayRuntime.stage);
            RpgRuntime_UpdateAndDraw(&runtime);
            // ランタイム側で設定オーバーレイを描いたフレームは、エディターの操作を続けない。
            if (RpgScene_IsGameSettings(&editorScene)) continue;
        } else UpdateZipperLaunchPreview(&stage, GetFrameTime());
        // プレビューも実フォルダの集計値を使うが、消滅時に File.png は生成せず一時フォルダだけを破棄する。
        if (!isEditorPlaying) {
            int payloadAttachmentId = 0;
            if (RpgObjectFolders_UpdateShooterPayloadCaches(&attachments, &payloadAttachmentId))
                RpgDataShots_ApplyShooterPayloadCache(&attachmentPreviewShots, &attachments,
                                                      payloadAttachmentId);
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
            RpgEditorPlay_End(&editorPlayRuntime);
            (void)RpgViewport_Resize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
            RpgStageBuild_Close();
            /* Stop returns immediately.  The existing preview is repaired
               later by the idle-only cooperative editor cache worker. */
            editorPreviewBuildReady = false;
            editorPreviewBuildDirty = true;
            editorPreviewAwaitingCellGeneration = false;
            editorPreviewInitialGenerationBaselinePending = false;
            editorPreviewHasDirectCellWork = false;
            FinishEditorPreviewPreparationDisplay();
            MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_RECOVERY);
            memset(pendingPreviewCompactCells, 0, sizeof(pendingPreviewCompactCells));
            memset(pendingPreviewCompactPreviousBlockTypes, 0,
                   sizeof(pendingPreviewCompactPreviousBlockTypes));
            memset(pendingPreviewOwnershipCells, 0, sizeof(pendingPreviewOwnershipCells));
            memset(pendingPreviewRemovedCompositeIds, 0, sizeof(pendingPreviewRemovedCompositeIds));
            pendingPreviewRemovedCompositeCount = 0;
            ClearPendingPreviewMovableObjectBatch();
            editorPreviewBuildStage = stage;
            editorPreviewBuildStageNumber = currentStageNumber;
            nextStageFolderSyncTime = GetTime();
            ResetEditorPreviews(&stage, &signalBlocks, &attachmentPreviewShots, &previewEvent,
                                &isMovePreviewPlaying, &isZipperLaunchPreviewVisible,
                                &isZipperLaunchPreviewReturning);
            editorPlayShots = RpgDataShots_Default();
            editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
            editorPlayButtonEvent = RpgButtonEvent_Default();
            editorPlaySocketCommunicationEvent = RpgButtonEvent_Default();
            wasEditorPlayButtonPressed = false;
            editorPlayDialogueIndex = -1; editorPlayStage3IntroIndex = -1; editorPlayStage3IntroShown = false; memset(editorPlayAreaEntryShown, 0, sizeof(editorPlayAreaEntryShown)); editorPlayActiveEntryEvent = &stage3Event; editorPlayInspectTarget = -1;
            editorPlayInspectFunctionIndex = -1; editorPlayInspectLineIndex = -1;
            isEditorPlayInspectMoveRunning = false; editorPlayInspectMoveElapsed = 0.0f;
            editorPlayActiveInspectMove = NULL; editorPlayInspectMoveTransitionElapsed = 0.0f;
            editorPlayActiveWaitFunctionIndex = -1; editorPlayInspectWaitElapsed = 0.0f;
            editorPlayNpcInspectCompleted = false; editorPlayZipperInspectCompleted = false;
            editorPlayZipperFollowsPlayer = false; editorPlayZipperControllable = false;
            editorPlayZipperLaunched = false; editorPlayAttachedDataShotIndex = -1; editorPlayAttachedDynamicBlockIndex = -1; editorPlayAttachedReferenceObjectIndex = -1;
            editorPlayAttachedAttachmentIndex = -1;
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
                    int targetMapIndex = GetGlobalMapGridAtPointer(&stage, mousePosition,
                                                                    &targetGridX, &targetGridY) ?
                                            RpgStage_GetMapAtGrid(&stage, targetGridX, targetGridY) : -2;
                    bool areaChanged = false;
                    if (targetMapIndex >= 0 && targetMapIndex != releasedMapIndex) {
                        areaChanged = SwapGlobalMapAreas(&stage, &layout, &player, &npc, &zipperData,
                                                         &items, &mapEvents, releasedMapIndex, targetMapIndex);
                        if (areaChanged) {
                            mapIndex = releasedMapIndex;
                            message = TextFormat("Areas (%d, %d) and (%d, %d) swapped",
                                                 stage.mapGridX[releasedMapIndex],
                                                 stage.mapGridY[releasedMapIndex],
                                                 stage.mapGridX[targetMapIndex],
                                                 stage.mapGridY[targetMapIndex]);
                        }
                    } else if (targetMapIndex == -1 &&
                               MoveGlobalMapAreaToGrid(&stage, &layout, &player, &npc, &zipperData,
                                                       &items, &mapEvents, releasedMapIndex,
                                                       targetGridX, targetGridY)) {
                        areaChanged = true;
                        mapIndex = releasedMapIndex;
                        message = TextFormat("Area moved to (%d, %d)", stage.mapGridX[mapIndex],
                                             stage.mapGridY[mapIndex]);
                    }
                    if (!areaChanged)
                        message = targetMapIndex == releasedMapIndex ? "Area position unchanged" :
                                  "Drop on an area cell or an empty grid cell";
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
        bool isEditorStopShortcut = isEditorPlaying && !isKeyboardCaptured &&
                                    (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
                                    IsKeyPressed(KEY_P);
        bool isEditorPlayToggleClicked = !isGlobalMapOpen && !globalMapInteractionThisFrame && !isExitConfirmationOpen && !blockMode &&
                                         (CheckCollisionPointRec(mousePosition, editorPlayControlBounds) ||
                                          isEditorStopShortcut);
        bool isCtrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        /* Ctrl+P is an editor-wide play command.  Block mode only changes
         * canvas editing and must not suppress the same play entry route. */
        bool isCtrlPlayShortcut = !isEditorPlaying && !isKeyboardCaptured && !isGlobalMapOpen &&
                                  !isExitConfirmationOpen && isCtrlHeld && IsKeyPressed(KEY_P);
        bool isInitialEditorPlayRequested = isCtrlPlayShortcut &&
                                            (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
        if (isCtrlPlayShortcut && !isInitialEditorPlayRequested) {
            int flagIndex = FindAreaSaveFlagForEditorPlay(&attachments, &stage, mapIndex);
            if (flagIndex >= 0) {
                pendingSaveFlagPlayIndex = flagIndex;
                message = "Starting play from this area's save flag";
            } else {
                message = "This area has no save flag";
            }
        }
        Vector2 saveFlagPlayStart = { 0.0f, 0.0f };
        int saveFlagPlayMapIndex = mapIndex;
        bool saveFlagStartZipperConnected = false;
        bool isSaveFlagPlayRequested = !isEditorPlaying && pendingSaveFlagPlayIndex >= 0;
        if (isSaveFlagPlayRequested &&
            !GetSaveFlagEditorPlayStart(&attachments, &stage, pendingSaveFlagPlayIndex,
                                        &saveFlagPlayStart, &saveFlagPlayMapIndex)) {
            pendingSaveFlagPlayIndex = -1;
            isSaveFlagPlayRequested = false;
            message = "Save flag is unavailable";
        }
        if (isSaveFlagPlayRequested)
            saveFlagStartZipperConnected = attachments.entries[pendingSaveFlagPlayIndex].flagStartZipperConnected;
        bool shouldToggleEditorPlay = isSaveFlagPlayRequested || isInitialEditorPlayRequested ||
                                      (isEditorPlayToggleClicked &&
                                       (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                                        isEditorStopShortcut));
        if (shouldToggleEditorPlay) {
            if (!isEditorPlaying) {
                double playStartProfileBegin = GetTime();
                double playStartProfilePhase = playStartProfileBegin;
                memset(&lastPlayStartProfile, 0, sizeof(lastPlayStartProfile));
                // 実行前にプレビューだけを消してから現在の編集状態を保持する。
                ResetEditorPreviews(&stage, &signalBlocks, &attachmentPreviewShots, &previewEvent,
                                    &isMovePreviewPlaying, &isZipperLaunchPreviewVisible,
                                    &isZipperLaunchPreviewReturning);
                lastPlayStartProfile.resetPreviewMs = (GetTime() - playStartProfilePhase) * 1000.0;
                playStartProfilePhase = GetTime();
                RpgEditorPlay_Begin(&editorPlayRuntime, mapIndex, &layout, &player, &npc, &npcInspectData, &stage, &dialogue, &stage3Event,
                                    &areaEntryEvents, &items, &mapEvents,
                                    &wires, &receivers, &attachments, &signalBlocks, &zipperData);
                if (isSaveFlagPlayRequested) {
                    /* スナップ済み旗の足元から始め、Zipperは設定対象のまま接続済み追従へ切り替える。 */
                    editorPlayRuntime.mapIndex = saveFlagPlayMapIndex;
                    editorPlayRuntime.player.position = saveFlagPlayStart;
                }
                pendingSaveFlagPlayIndex = -1;
                /* Copy editor-owned movable references into the isolated play
                   session.  Stop still discards this runtime copy. */
                editorPlayReferenceDrops = editorPlayRuntime.stage.referenceObjects;
                RpgRuntime_ResetTransientState();
                PrepareEditorPlayCharacter(&editorPlayRuntime.player, &editorPlayRuntime.stage);
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
                        &editorPlayRuntime.stage, (int)(editorPlayRuntime.player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)));
                lastPlayStartProfile.runtimeCopyMs = (GetTime() - playStartProfilePhase) * 1000.0;
                playStartProfilePhase = GetTime();
                /* The current editor state has already been copied into
                   editorPlayRuntime.  From this point onward all folder/cell
                   creation is exclusively Play preparation. */
                lastPlayStartProfile.previewSyncMs = 0.0;
                playStartProfilePhase = GetTime();
                bool playBuildReady = false;
                bool allAreasReady = false;
                int preparedCells = 0, preparedTotal = 0;
                bool preparedPending = false;
                bool previewCacheEligible = editorPreviewBuildReady && !editorPreviewBuildDirty &&
                                            editorPreviewBuildStageNumber == currentStageNumber;
                RpgBuildCellStorage_GetGenerationProgress(&editorPreviewBuildStage,
                                                           &preparedCells, &preparedTotal, &preparedPending);
                lastPlayStartProfile.usedPreparedPreview = false;
                if (previewCacheEligible && !preparedPending && preparedCells >= preparedTotal) {
                    playBuildReady = RpgStageBuild_ResumePreparedEditorPreview(currentStageNumber,
                                                                                  &editorPlayRuntime.stage);
                    lastPlayStartProfile.usedPreparedPreview = playBuildReady;
                } else if (previewCacheEligible) {
                    /* This reconnects the partially prepared tree.  Play may
                       need to finish only the remaining cells below. */
                    playBuildReady = RpgStageBuild_ResumeEditorPreview(currentStageNumber,
                                                                          &editorPlayRuntime.stage);
                    lastPlayStartProfile.usedPreparedPreview = playBuildReady;
                }
                if (!playBuildReady)
                    playBuildReady = RpgStageBuild_CreateEditorPreview(currentStageNumber,
                                                                         &editorPlayRuntime.stage,
                                                                         &editorPlayRuntime.attachments,
                                                                         &editorPlayRuntime.receivers,
                                                                         &editorPlayRuntime.wires,
                                                                         editorPlayRuntime.player.position);
                lastPlayStartProfile.resumeOrCreateMs = (GetTime() - playStartProfilePhase) * 1000.0;
                if (playBuildReady) {
                    RpgBuildCellStorage_GetGenerationProgress(&editorPlayRuntime.stage,
                                                               &lastPlayStartProfile.generatedBeforeEnsure,
                                                               &lastPlayStartProfile.totalBeforeEnsure,
                                                               &lastPlayStartProfile.pendingBeforeEnsure);
                    playStartProfilePhase = GetTime();
                    allAreasReady = RpgStageBuild_EnsureAllAreasGenerated(&editorPlayRuntime.stage);
                    lastPlayStartProfile.ensureAreasMs = (GetTime() - playStartProfilePhase) * 1000.0;
                }
                if (!playBuildReady) {
                    RpgEditorPlay_End(&editorPlayRuntime);
                    message = "Play build failed";
                } else if (!allAreasReady) {
                    RpgEditorPlay_End(&editorPlayRuntime);
                    RpgStageBuild_Close();
                    message = "Play area build failed";
                }
                playStartProfilePhase = GetTime();
                bool zipperDirectoryReady = !playBuildReady || !allAreasReady ||
                                            !isSaveFlagPlayRequested || !saveFlagStartZipperConnected ||
                                            RpgObjectFolder_EnsureRuntimeZipperDirectory();
                lastPlayStartProfile.zipperMs = (GetTime() - playStartProfilePhase) * 1000.0;
                if (playBuildReady && allAreasReady && !zipperDirectoryReady) {
                    RpgEditorPlay_End(&editorPlayRuntime);
                    RpgStageBuild_Close();
                    message = "Save flag Zipper build failed";
                }
                playStartProfilePhase = GetTime();
                bool viewportReady = playBuildReady && allAreasReady && zipperDirectoryReady &&
                                     RpgViewport_Resize(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
                                                        RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE);
                lastPlayStartProfile.viewportMs = (GetTime() - playStartProfilePhase) * 1000.0;
                if (playBuildReady && allAreasReady && zipperDirectoryReady && !viewportReady) {
                    RpgEditorPlay_End(&editorPlayRuntime);
                    RpgStageBuild_Close();
                    message = "Play viewport failed";
                } else if (playBuildReady && allAreasReady && zipperDirectoryReady && viewportReady) {
                editorPlayShots = RpgDataShots_Default();
                editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
                editorPlayButtonEvent = RpgButtonEvent_Default();
                editorPlaySocketCommunicationEvent = RpgButtonEvent_Default();
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
                RpgZipper_ClearHeldObject(&editorPlayRuntime.zipper);
                lastEditorPlayZipperClickTime = -1.0;
                editorPlayActiveEntryEvent = &editorPlayRuntime.stage3Event;
                if (isSaveFlagPlayRequested && saveFlagStartZipperConnected) {
                    RpgRuntime_StartFollowingZipper(&editorPlayRuntime.zipper, &editorPlayRuntime.player,
                                                     &editorPlayZipperFollowsPlayer,
                                                     &editorPlayZipperControllable,
                                                     &editorPlayZipperInspectCompleted,
                                                     &editorPlayZipperAnimationElapsed);
                }
                /* From here to Stop, only an actual static folder move marks
                   its affected preview areas for a targeted repair. */
                playStartProfilePhase = GetTime();
                if (!RpgObjectFolders_BeginEditorPreviewRuntimeTracking()) {
                    /* Runtime tracking does not copy the preview tree.  If
                       the runtime folder cannot be tracked, do not start. */
                    RpgEditorPlay_End(&editorPlayRuntime);
                    (void)RpgViewport_Resize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
                    RpgStageBuild_Close();
                    RpgRuntime_ResetTransientState();
                    message = "Play runtime tracking could not start";
                } else {
                lastPlayStartProfile.runtimeTrackingMs = (GetTime() - playStartProfilePhase) * 1000.0;
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
                lastPlayStartProfile.succeeded = true;
                }
                if (!lastPlayStartProfile.succeeded)
                    lastPlayStartProfile.runtimeTrackingMs = (GetTime() - playStartProfilePhase) * 1000.0;
                lastPlayStartProfile.totalMs = (GetTime() - playStartProfileBegin) * 1000.0;
                WritePlayStartProfile(&lastPlayStartProfile);
                }
            } else {
                RpgEditorPlay_End(&editorPlayRuntime);
                (void)RpgViewport_Resize(RPG_EDITOR_WIDTH, RPG_EDITOR_HEIGHT);
                RpgStageBuild_Close();
                /* Stop never blocks on regeneration.  It only makes the
                   prepared preview eligible for the idle cache worker. */
                editorPreviewBuildReady = false;
                editorPreviewBuildDirty = true;
                editorPreviewAwaitingCellGeneration = false;
                editorPreviewInitialGenerationBaselinePending = false;
                editorPreviewHasDirectCellWork = false;
                FinishEditorPreviewPreparationDisplay();
                MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_RECOVERY);
                memset(pendingPreviewCompactCells, 0, sizeof(pendingPreviewCompactCells));
                memset(pendingPreviewCompactPreviousBlockTypes, 0,
                       sizeof(pendingPreviewCompactPreviousBlockTypes));
                memset(pendingPreviewOwnershipCells, 0, sizeof(pendingPreviewOwnershipCells));
                memset(pendingPreviewRemovedCompositeIds, 0, sizeof(pendingPreviewRemovedCompositeIds));
                pendingPreviewRemovedCompositeCount = 0;
                ClearPendingPreviewMovableObjectBatch();
                editorPreviewBuildStage = stage;
                editorPreviewBuildStageNumber = currentStageNumber;
                nextStageFolderSyncTime = GetTime();
                ResetEditorPreviews(&stage, &signalBlocks, &attachmentPreviewShots, &previewEvent,
                                    &isMovePreviewPlaying, &isZipperLaunchPreviewVisible,
                                    &isZipperLaunchPreviewReturning);
                editorPlayShots = RpgDataShots_Default();
                editorPlayMagnetRuntime = RpgMagnetRuntime_Default();
                editorPlayButtonEvent = RpgButtonEvent_Default();
                editorPlaySocketCommunicationEvent = RpgButtonEvent_Default();
                wasEditorPlayButtonPressed = false;
                editorPlayDialogueIndex = -1; editorPlayStage3IntroIndex = -1; editorPlayStage3IntroShown = false; memset(editorPlayAreaEntryShown, 0, sizeof(editorPlayAreaEntryShown)); editorPlayActiveEntryEvent = &stage3Event; editorPlayInspectTarget = -1;
                editorPlayInspectFunctionIndex = -1; editorPlayInspectLineIndex = -1;
                isEditorPlayInspectMoveRunning = false; editorPlayInspectMoveElapsed = 0.0f;
                editorPlayActiveInspectMove = NULL; editorPlayInspectMoveTransitionElapsed = 0.0f;
                editorPlayActiveWaitFunctionIndex = -1; editorPlayInspectWaitElapsed = 0.0f;
                editorPlayNpcInspectCompleted = false; editorPlayZipperInspectCompleted = false;
                editorPlayZipperFollowsPlayer = false; editorPlayZipperControllable = false;
                editorPlayZipperLaunched = false; editorPlayAttachedDataShotIndex = -1; editorPlayAttachedDynamicBlockIndex = -1; editorPlayAttachedReferenceObjectIndex = -1;
                editorPlayAttachedAttachmentIndex = -1;
                editorPlayReferenceDragActive = false; editorPlayReferenceTextOpen = false;
                editorPlayReferenceDrops = RpgReferenceObjects_Default();
                RpgRuntime_ResetTransientState();
                isEditorPlaying = false;
                message = "Play stopped";
            }
        }
        if (isEditorPlaying && editorPlayZipperFollowsPlayer &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            RpgCharacter localZipper = editorPlayRuntime.zipper.character;
            localZipper.position.x -= editorPlayRuntime.mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
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
            !isInspectTitleEditing && !isGlobalKeyDoorFailureEditing && paletteNameEditingIndex < 0 &&
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
            /* Palette selection is reflected by its highlighted icon; do not
               emit a persistent lower-right notification. */
            message = "";
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
            message = "";
        }
        if (!isKeyboardCaptured && blockMode && !isDialogueEditing && !isAttachmentCapacityEditing && !isAttachmentSpeedEditing &&
            !isZipperCapacityEditing && !isItemNameEditing && !isReferencePathEditing &&
            !isInspectTitleEditing && !isGlobalKeyDoorFailureEditing && paletteNameEditingIndex < 0) {
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
                message = "";
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
        Rectangle inspectorTitleBarBounds = GetInspectorBounds(selected);
        inspectorTitleBarBounds.height = RPG_EDITOR_INSPECTOR_HEADER_HEIGHT;
        bool isInspectorTitleBarClicked = isInspectorClicked &&
                                           CheckCollisionPointRec(inspectorMousePosition,
                                                                  inspectorTitleBarBounds);
        if (isInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) isInspectorPointerHeld = true;
        /* テキスト欄はドラッグ選択の開始点にもなる。入力中に内容領域の
           ドラッグをパネル移動へ奪わせないため、移動開始はタイトルバーだけに
           限定し、既に入力フォーカスがある場合は進行中の移動も止める。 */
        if (isEditorTextEditing) isInspectorDragActive = false;
        if (isInspectorDragActive && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isInspectorDragActive = false;
        if (!wasModalOpenAtFrameStart && !isEditorTextEditing && isInspectorTitleBarClicked &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            !IsInspectorControlPoint(selected, inspectorMousePosition)) {
            // パネル移動はタイトルバーからのみ開始する。内容欄は入力・範囲選択を優先する。
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
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 120, 42, 26 }))
                    layout.playerJumpHeight = Clamp(layout.playerJumpHeight - RPG_STAGE_TILE_SIZE,
                                                    1.0f, (float)(RPG_STAGE_TILE_SIZE * 12));
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 762, y + 120, 42, 26 }))
                    layout.playerJumpHeight = Clamp(layout.playerJumpHeight + RPG_STAGE_TILE_SIZE,
                                                    1.0f, (float)(RPG_STAGE_TILE_SIZE * 12));
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, y + 120, 42, 26 }))
                    layout.playerJumpHeight = Clamp(layout.playerJumpHeight - 1.0f,
                                                    1.0f, (float)(RPG_STAGE_TILE_SIZE * 12));
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 860, y + 120, 42, 26 }))
                    layout.playerJumpHeight = Clamp(layout.playerJumpHeight + 1.0f,
                                                    1.0f, (float)(RPG_STAGE_TILE_SIZE * 12));
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 158, 90, 26 }))
                    message = SaveCharacterSettings(&layout, &player, true, &savedSnapshot) ? "Saved" : "Save failed";
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, y + 158, 90, 26 })) {
                    RevertCharacterSettings(&savedSnapshot, &layout, &player, true);
                    message = "Reverted to saved";
                }
                player.jumpHeight = layout.playerJumpHeight;
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
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 142, 188, 26 })) {
                snprintf(buildReadCountInput, sizeof(buildReadCountInput), "%d",
                         RpgBuildCellStorage_GetGenerationWorkPerUpdate());
                isBuildReadCountEditing = true;
                message = "Enter build reads per frame (1-64)";
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
                if (tabIndex == RPG_STAGE_SETTINGS_TAB_STAGE && !stageSettingsTabExpanded[tabIndex]) {
                    isZipperCapacityEditing = false;
                    isStageNameEditing = false;
                }
            } else if (stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_STAGE]) {
                float y = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                   RPG_STAGE_SETTINGS_TAB_COUNT, RPG_STAGE_SETTINGS_TAB_STAGE);
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 6, 28, 26 }) && stageIndex > 0)
                    targetStageNumber = RpgStageCatalog_GetNumberAt(&stageCatalogData, stageIndex - 1);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 876, y + 6, 28, 26 }) &&
                         stageIndex >= 0 && stageIndex + 1 < stageCatalogData.count)
                    targetStageNumber = RpgStageCatalog_GetNumberAt(&stageCatalogData, stageIndex + 1);
                else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 752, y + 6, 118, 26 })) {
                    snprintf(stageNameInput, sizeof(stageNameInput), "%s",
                             RpgStageCatalog_GetDisplayName(&stageCatalogData, currentStageNumber));
                    RpgEditorText_BeginPointerSelection(stageNameInput, inspectorMousePosition.x - 756.0f,
                                                         15.0f, &stageNameCursorIndex,
                                                         &stageNameSelectionAnchor, &stageNameSelectionEnd);
                    isStageNameEditing = true;
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 56, 188, 26 })) {
                if (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                         &items, &savedItems)) message = "Save or Revert before changing stage";
                else {
                    int addedStageNumber = RpgStageCatalog_Add(&stageCatalogData);
                    if (addedStageNumber > 0 && RpgStageStorage_CreateDefaultStage(addedStageNumber))
                        targetStageNumber = addedStageNumber;
                    else {
                        if (addedStageNumber > 0) (void)RpgStageCatalog_DeleteCurrent(&stageCatalogData);
                        message = "Stage creation failed";
                    }
                }
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, y + 90, 188, 26 })) {
                if (HasAnyUnsavedChanges(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event,
                                         &items, &savedItems)) message = "Save or Revert before deleting stage";
                else if (RpgStageCatalog_DeleteCurrent(&stageCatalogData))
                    targetStageNumber = RpgStageCatalog_GetCurrentNumber(&stageCatalogData);
                else message = "Cannot delete the last stage";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 796, y + 122, 70, 26 })) {
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
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, backgroundControlY + 22, 44, 24 })) {
                layout.backgroundBrightness = Clamp(layout.backgroundBrightness - 0.05f, 0.15f, 1.0f);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, backgroundControlY + 22, 44, 24 })) {
                layout.backgroundBrightness = Clamp(layout.backgroundBrightness + 0.05f, 0.15f, 1.0f);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, backgroundControlY + 78, 44, 24 })) {
                layout.blockBrightness = Clamp(layout.blockBrightness - 0.05f, 0.15f, 1.0f);
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 772, backgroundControlY + 78, 44, 24 })) {
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
                isStageNameEditing = false;
                savedItems = items;
                savedMapEvents = mapEvents; savedWires = wires;
                (void)RpgAttachments_Clone(&savedAttachments, &attachments); RpgReceivers_Bind(&savedReceivers, &savedAttachments); savedSignalBlocks = signalBlocks;
                UpdateSaveSnapshot(&savedSnapshot, &player, &npc, &layout, &stage, &dialogue, &stage3Event);
                mapIndex = RpgStage_FindNearestActiveMap(&stage, 0);
                selected = RPG_EDITOR_STAGE_SETTINGS_INSPECTOR;
                message = TextFormat("%s selected",
                                     RpgStageCatalog_GetDisplayName(&stageCatalogData, currentStageNumber));
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
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isInspectorPointerHeld)
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
            /* A highlighted palette icon is sufficient feedback.  Keeping a
               selection message here left red text at the lower right. */
            message = "";
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
            bool isReferenceNameOutsideClick = RpgEditorText_ShouldEndEditingOnOutsideClick(
                referencePathBounds, inspectorMousePosition);
            /* Treat both main and keypad Enter as a folder-name commit.  IME
               confirmation can reach raylib on key release.  Leaving a
               changed name field commits through exactly the same path, so a
               close-button click cannot discard a completed edit. */
            bool isReferenceFolderNameCommit = isReferenceFolderSelected &&
                (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                 IsKeyReleased(KEY_ENTER) || IsKeyReleased(KEY_KP_ENTER) ||
                 (isReferenceNameOutsideClick &&
                  strcmp(referenceFolderNameInput,
                         GetReferenceFolderAuthoredName(selectedReferenceObject)) != 0));
            if (isReferenceFolderNameCommit) {
                char renamedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
                char oldName[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
                int selectedFolderId = selectedReferenceObject->id;
                snprintf(oldName, sizeof(oldName), "%s",
                         GetReferenceFolderAuthoredName(selectedReferenceObject));
                TraceReferenceFolderRename("commit", selectedFolderId, oldName,
                                           referenceFolderNameInput, true, 0);
#ifdef _WIN32
                SetLastError(ERROR_SUCCESS);
#endif
                bool movedFolder = RpgStageStorage_RenameBuildFolder(
                    currentStageNumber, oldName, referenceFolderNameInput,
                    renamedPath, (int)sizeof(renamedPath));
#ifdef _WIN32
                unsigned long renameSystemError = movedFolder ? 0ul : (unsigned long)GetLastError();
#else
                unsigned long renameSystemError = 0ul;
#endif
                TraceReferenceFolderRename("physical-move", selectedFolderId, oldName,
                                           referenceFolderNameInput, movedFolder,
                                           renameSystemError);
                if (movedFolder) {
                    int renamedIndex = ApplyReferenceFolderRename(&stage.referenceObjects, selectedFolderId,
                                                                  oldName, referenceFolderNameInput,
                                                                  renamedPath);
                    if (renamedIndex >= 0) {
                        selectedReferenceObjectIndex = renamedIndex;
                        selectedReferenceObject = &stage.referenceObjects.entries[renamedIndex];
                        blockEditedThisFrame = true;
                    }
                    bool savedFolderName = renamedIndex >= 0 &&
                        SaveEditorAndUpdateSnapshot(&layout, &player, &npc, &stage, &dialogue, &stage3Event,
                                                    &items, &savedSnapshot, &savedItems);
                    TraceReferenceFolderRename("static-save", selectedFolderId, oldName,
                                               referenceFolderNameInput, savedFolderName, 0);
                    if (savedFolderName) {
                        message = "Folder renamed";
                    } else if (renamedIndex < 0) {
                        message = "Folder rename state failed";
                    } else {
                        /* The directory has already moved.  Keep the edited
                           in-memory value so the idle publisher can retry the
                           static-stage write instead of restoring stale text. */
                        message = "Folder renamed; stage save pending";
                    }
                } else message = "Folder rename failed";
                isReferencePathEditing = false;
            } else if (isReferenceNameOutsideClick) {
                isReferencePathEditing = false;
            }
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
                    /* The runtime file is disposable.  Save a stable copy
                       keyed by this movable ID so later Play sessions do not
                       require opening this picker again. */
                    if (!RpgStageStorage_CopyReferenceFileToStaticSource(
                            currentStageNumber, selectedReferenceObject->id, resolvedSourcePath)) {
                        message = "Could not preserve selected source file";
                    }
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
                (void)RpgAttachments_Clone(&savedAttachments, &attachments); RpgReceivers_Bind(&savedReceivers, &savedAttachments); savedSignalBlocks = signalBlocks;
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
            isInspectorPointerHeld) {
            RpgEditorText_UpdatePointerSelection(items.entries[selectedItemIndex].name,
                                                  inspectorMousePosition.x - 724.0f, 17.0f,
                                                  &itemNameCursorIndex, &itemNameSelectionEnd);
        }
        if (isItemNameEditing && selectedItemIndex >= 0) UpdateImeCandidateWindowAt(724, 172);
        if (isDoorInspectorClicked && !isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage, selectedDoorRow, selectedDoorColumn);
            RpgKeyDoor *keyDoor = RpgStage_GetKeyDoorAtCell(&stage, selectedDoorRow, selectedDoorColumn);
            const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(&stage, selectedDoorRow, selectedDoorColumn);
            RpgEditorFilePickerLayout keyPickerLayout = GetKeyDoorFilePickerLayout(keyDoor);
            if (magnet != NULL && HandleSharedMagnetSpeedSetting(&layout, inspectorMousePosition, 180.0f)) {
                message = "Magnet speed changed";
            } else if (keyDoor != NULL && BeginSharedKeyDoorFailureEdit(&layout, inspectorMousePosition,
                                                                         keyPickerLayout.contentBottom + 38.0f)) {
                message = "Key door failure text selected";
            } else if (keyDoor != NULL && CheckCollisionPointRec(inspectorMousePosition, keyPickerLayout.selectBounds)) {
                char selectedPath[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
                FileDialogRequest keyDialog = MakeFilesFileDialogRequest("鍵ファイルを選択");
                if (FileDialog_Select(selectedPath, sizeof(selectedPath), &keyDialog)) {
                    snprintf(keyDoor->keyPath, sizeof(keyDoor->keyPath), "%s", selectedPath);
                    GameFont_AddText(keyDoor->keyPath);
                    message = "Key file selected";
                }
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
        if (isGlobalKeyDoorFailureEditing) {
            bool isKeyDoorHost = selected == 5 &&
                RpgStage_GetKeyDoorAtCellConst(&stage, selectedDoorRow, selectedDoorColumn) != NULL;
            if (!isKeyDoorHost) {
                isGlobalKeyDoorFailureEditing = false;
            } else {
                Rectangle failureBounds;
                if (isKeyDoorHost) {
                    const RpgKeyDoor *keyDoor = RpgStage_GetKeyDoorAtCellConst(&stage, selectedDoorRow, selectedDoorColumn);
                    RpgEditorFilePickerLayout picker = GetKeyDoorFilePickerLayout(keyDoor);
                    failureBounds = GetSharedKeyDoorFailureBounds(picker.contentBottom + 38.0f);
                }
                UpdateShortText(layout.keyDoorFailureText, sizeof(layout.keyDoorFailureText),
                                &globalKeyDoorFailureCursorIndex,
                                &globalKeyDoorFailureSelectionAnchor,
                                &globalKeyDoorFailureSelectionEnd);
                GameFont_AddText(layout.keyDoorFailureText);
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isInspectorPointerHeld)
                    RpgEditorText_UpdatePointerSelection(layout.keyDoorFailureText,
                                                          inspectorMousePosition.x - 722.0f, 15.0f,
                                                          &globalKeyDoorFailureCursorIndex,
                                                          &globalKeyDoorFailureSelectionEnd);
                if (RpgEditorText_ShouldEndEditingOnOutsideClick(failureBounds, inspectorMousePosition))
                    isGlobalKeyDoorFailureEditing = false;
                UpdateImeCandidateWindowAt(722, (int)failureBounds.y + 28);
            }
        }
        if (isAttachmentInspectorClicked && !isCloseInspectorClicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selectedAttachmentIndex >= 0 && selectedAttachmentIndex < attachments.count) {
            RpgAttachment *attachment = &attachments.entries[selectedAttachmentIndex];
            bool attachmentSettingsChanged = false;
            if (attachment->type == RPG_BLOCK_ATTACHMENT_SAVE_FLAG) {
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 176, 188, 28 })) {
                    /* 旗の開始要求は既存のPlay初期化で処理し、クリックをマップへ伝搬させない。 */
                    pendingSaveFlagPlayIndex = selectedAttachmentIndex;
                    message = "Starting play from save flag";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 242, 90, 26 })) {
                    RpgFlagConfig *flag = RpgAttachments_FindFlag(&attachments, attachment->folderId);
                    if (flag != NULL) { flag->startZipperConnected = true; attachmentSettingsChanged = true; }
                    message = "Flag start Zipper set to connected";
                } else if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 814, 242, 90, 26 })) {
                    RpgFlagConfig *flag = RpgAttachments_FindFlag(&attachments, attachment->folderId);
                    if (flag != NULL) { flag->startZipperConnected = false; attachmentSettingsChanged = true; }
                    message = "Flag start Zipper set to disconnected";
                }
            } else if (attachment->type == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET) {
                if (HandleSharedSocketLightSetting(&layout, inspectorMousePosition, 154.0f)) {
                    /* Socket-light values are stage settings rather than
                       attachment-owned metadata, but they still change the
                       static preview snapshot.  Invalidate it at the click
                       boundary just like every other authored setting. */
                    NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_VARIABLES,
                                            &(EditorPreviewEditNotification){
                                                .category = RPG_EDITOR_PREVIEW_CATEGORY_STAGE_SETTING
                                            },
                                            &editorPreviewEditsThisFrame);
                    message = "Socket light setting changed";
                }
            } else if (attachment->type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
                if (CheckCollisionPointRec(inspectorMousePosition, (Rectangle){ 716, 176, 188, 26 })) {
                    RpgPreviewEvent_Publish(&previewEvent, RPG_PREVIEW_TARGET_ALL);
                    message = "Preview signal: all targets react once";
                }
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().speedInput)) {
                snprintf(attachmentSpeedInput, sizeof(attachmentSpeedInput), "%.1f",
                         attachment->dataSpeed);
                isAttachmentSpeedEditing = true;
                isAttachmentCapacityEditing = false;
                RpgEditorText_BeginPointerSelection(attachmentSpeedInput,
                                                     inspectorMousePosition.x - 720.0f, 15.0f,
                                                     &attachmentSpeedCursorIndex,
                                                     &attachmentSpeedSelectionAnchor,
                                                     &attachmentSpeedSelectionEnd);
                message = "Enter speed at 100B";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().capacityInput)) {
                snprintf(attachmentCapacityInput, sizeof(attachmentCapacityInput), "%llu",
                         attachment->previewTotalBytes);
                isAttachmentCapacityEditing = true;
                isAttachmentSpeedEditing = false;
                RpgEditorText_BeginPointerSelection(attachmentCapacityInput,
                                                     inspectorMousePosition.x - 720.0f, 15.0f,
                                                     &attachmentCapacityCursorIndex,
                                                     &attachmentCapacitySelectionAnchor,
                                                     &attachmentCapacitySelectionEnd);
                message = "Enter preview capacity in B";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().sizeDecrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL) {
                    shooter->sizePerFile = Clamp(shooter->sizePerFile - RPG_STAGE_TILE_SIZE * 0.25f,
                                                 RPG_STAGE_TILE_SIZE * 0.25f, RPG_STAGE_TILE_SIZE * 2.0f);
                    attachmentSettingsChanged = true;
                }
                message = "Size per file changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().sizeIncrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL) {
                    shooter->sizePerFile = Clamp(shooter->sizePerFile + RPG_STAGE_TILE_SIZE * 0.25f,
                                                 RPG_STAGE_TILE_SIZE * 0.25f, RPG_STAGE_TILE_SIZE * 2.0f);
                    attachmentSettingsChanged = true;
                }
                message = "Size per file changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().speedDecrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL) { shooter->dataSpeed = Clamp(shooter->dataSpeed - 10.0f, 1.0f, 480.0f); attachmentSettingsChanged = true; }
                message = "Base speed changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().speedIncrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL) { shooter->dataSpeed = Clamp(shooter->dataSpeed + 10.0f, 1.0f, 480.0f); attachmentSettingsChanged = true; }
                message = "Base speed changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().previewButton)) {
                RpgPreviewEvent_Publish(&previewEvent, selectedAttachmentIndex);
                message = "Emitter preview shot";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().previewCountDecrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL && shooter->previewFileCount > 1) { shooter->previewFileCount--; attachmentSettingsChanged = true; }
                message = "Preview files changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().previewCountIncrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL && shooter->previewFileCount < 9999) { shooter->previewFileCount++; attachmentSettingsChanged = true; }
                message = "Preview files changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().capacityDecrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL && shooter->previewTotalBytes >= RPG_DATA_SPEED_BASE_BYTES) { shooter->previewTotalBytes -= RPG_DATA_SPEED_BASE_BYTES; attachmentSettingsChanged = true; }
                message = "Preview capacity changed";
            } else if (CheckCollisionPointRec(inspectorMousePosition,
                                               GetShooterInspectorLayout().capacityIncrease)) {
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(&attachments, attachment->folderId);
                if (shooter != NULL) { shooter->previewTotalBytes += RPG_DATA_SPEED_BASE_BYTES; attachmentSettingsChanged = true; }
                message = "Preview capacity changed";
            }
            if (attachmentSettingsChanged) {
                /* The inspector mutates the type-specific source of truth.
                   Rebuild the read-only legacy facade for existing drawing
                   code before the preview snapshot observes the edit. */
                RpgAttachments_RefreshEditorFacades(&attachments);
                NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_VARIABLES,
                                        &(EditorPreviewEditNotification){
                                            .category = RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT_SETTING,
                                            .subjectId = attachment->folderId
                                        },
                                        &editorPreviewEditsThisFrame);
            }
        }
        if (isAttachmentCapacityEditing) {
            if (selectedAttachmentIndex < 0 || selectedAttachmentIndex >= attachments.count ||
                attachments.entries[selectedAttachmentIndex].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
                isAttachmentCapacityEditing = false;
            } else if (RpgEditorText_ShouldEndEditingOnOutsideClick(
                           GetShooterInspectorLayout().capacityInput, inspectorMousePosition)) {
                isAttachmentCapacityEditing = false;
            } else if (IsKeyPressed(KEY_ENTER)) {
                unsigned long long bytes = strtoull(attachmentCapacityInput, NULL, 10);
                const unsigned long long maxBytes = 1024ULL * 1024ULL * 1024ULL;
                if (bytes > maxBytes) bytes = maxBytes;
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(
                    &attachments, attachments.entries[selectedAttachmentIndex].folderId);
                if (shooter != NULL) {
                    shooter->previewTotalBytes = bytes;
                    RpgAttachments_RefreshEditorFacades(&attachments);
                    NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_VARIABLES,
                                            &(EditorPreviewEditNotification){
                                                .category = RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT_SETTING,
                                                .subjectId = attachments.entries[selectedAttachmentIndex].folderId
                                            },
                                            &editorPreviewEditsThisFrame);
                }
                isAttachmentCapacityEditing = false;
                message = "Preview capacity changed";
            } else {
                RpgEditorText_UpdateNumericInput(attachmentCapacityInput,
                                                 sizeof(attachmentCapacityInput),
                                                 &attachmentCapacityCursorIndex,
                                                 &attachmentCapacitySelectionAnchor,
                                                 &attachmentCapacitySelectionEnd, false);
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isInspectorPointerHeld)
                    RpgEditorText_UpdatePointerSelection(attachmentCapacityInput,
                                                         inspectorMousePosition.x - 720.0f, 15.0f,
                                                         &attachmentCapacityCursorIndex,
                                                         &attachmentCapacitySelectionEnd);
            }
        }
        if (isAttachmentSpeedEditing) {
            if (selectedAttachmentIndex < 0 || selectedAttachmentIndex >= attachments.count ||
                attachments.entries[selectedAttachmentIndex].type != RPG_BLOCK_ATTACHMENT_RADIO_EMITTER) {
                isAttachmentSpeedEditing = false;
            } else if (RpgEditorText_ShouldEndEditingOnOutsideClick(
                           GetShooterInspectorLayout().speedInput, inspectorMousePosition)) {
                isAttachmentSpeedEditing = false;
            } else if (IsKeyPressed(KEY_ENTER)) {
                float value = strtof(attachmentSpeedInput, NULL);
                RpgShooterConfig *shooter = RpgAttachments_FindShooter(
                    &attachments, attachments.entries[selectedAttachmentIndex].folderId);
                if (shooter != NULL) {
                    shooter->dataSpeed = Clamp(value, 1.0f, 480.0f);
                    RpgAttachments_RefreshEditorFacades(&attachments);
                    NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_VARIABLES,
                                            &(EditorPreviewEditNotification){
                                                .category = RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT_SETTING,
                                                .subjectId = attachments.entries[selectedAttachmentIndex].folderId
                                            },
                                            &editorPreviewEditsThisFrame);
                }
                isAttachmentSpeedEditing = false;
                message = "Base speed changed";
            } else {
                RpgEditorText_UpdateNumericInput(attachmentSpeedInput,
                                                 sizeof(attachmentSpeedInput),
                                                 &attachmentSpeedCursorIndex,
                                                 &attachmentSpeedSelectionAnchor,
                                                 &attachmentSpeedSelectionEnd, true);
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isInspectorPointerHeld)
                    RpgEditorText_UpdatePointerSelection(attachmentSpeedInput,
                                                         inspectorMousePosition.x - 720.0f, 15.0f,
                                                         &attachmentSpeedCursorIndex,
                                                         &attachmentSpeedSelectionEnd);
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
        if (isStageNameEditing) {
            Rectangle nameBounds;
            float nameY = GetSettingsTabContentTop(stageSettingsTabs, stageSettingsTabExpanded,
                                                    RPG_STAGE_SETTINGS_TAB_COUNT,
                                                    RPG_STAGE_SETTINGS_TAB_STAGE);
            nameBounds = (Rectangle){ 752.0f, nameY + 6.0f, 118.0f, 26.0f };
            RpgEditorText_ClaimKeyboard();
            if (!isStageSettingsOpen || selected != RPG_EDITOR_STAGE_SETTINGS_INSPECTOR ||
                !stageSettingsTabExpanded[RPG_STAGE_SETTINGS_TAB_STAGE]) {
                isStageNameEditing = false;
            } else if (IsKeyPressed(KEY_ENTER) ||
                       RpgEditorText_ShouldEndEditingOnOutsideClick(nameBounds, inspectorMousePosition)) {
                if (RpgStageCatalog_SetDisplayName(&stageCatalogData, currentStageNumber, stageNameInput))
                    message = "Stage name changed";
                else
                    message = "Stage name cannot be empty";
                isStageNameEditing = false;
            } else {
                UpdateShortText(stageNameInput, sizeof(stageNameInput), &stageNameCursorIndex,
                                &stageNameSelectionAnchor, &stageNameSelectionEnd);
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isInspectorPointerHeld)
                    RpgEditorText_UpdatePointerSelection(stageNameInput,
                                                          inspectorMousePosition.x - nameBounds.x - 4.0f,
                                                          15.0f, &stageNameCursorIndex,
                                                          &stageNameSelectionEnd);
            }
        }
        if (isBuildReadCountEditing) {
            RpgEditorText_ClaimKeyboard();
            if (!isGlobalSettingsOpen || selected != RPG_EDITOR_GLOBAL_SETTINGS_INSPECTOR ||
                !globalSettingsTabExpanded[RPG_GLOBAL_SETTINGS_TAB_BUILD]) {
                isBuildReadCountEditing = false;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                isBuildReadCountEditing = false;
                message = "Build read count edit cancelled";
            } else if (IsKeyPressed(KEY_ENTER)) {
                unsigned long value = strtoul(buildReadCountInput, NULL, 10);
                if (value < 1) value = 1;
                if (value > 64) value = 64;
                isBuildReadCountEditing = false;
                message = RpgBuildCellStorage_SaveGenerationWorkPerUpdate((int)value) ?
                          "Build reads per frame saved" : "Build setting failed";
            } else {
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    size_t length = strlen(buildReadCountInput);
                    if (length > 0) buildReadCountInput[length - 1] = '\0';
                }
                int codepoint = GetCharPressed();
                while (codepoint > 0) {
                    size_t length = strlen(buildReadCountInput);
                    if (codepoint >= '0' && codepoint <= '9' &&
                        length + 1 < sizeof(buildReadCountInput)) {
                        buildReadCountInput[length] = (char)codepoint;
                        buildReadCountInput[length + 1] = '\0';
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
                                                   blockContextMenuColumn, &attachments, &dropped)) {
                        blockEditedThisFrame = true;
                        message = TextFormat("Dropped %d object%s", dropped, dropped == 1 ? "" : "s");
                    } else message = "Drop needs room for its files and folder";
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
            /* The context menu belongs to a map cell, not only to terrain.
               An air/error cell may still own an authored object folder that
               can be released with Drop.  Drop itself verifies that such a
               folder exists, while Delete remains a harmless no-op for air. */
            if (contextRow >= 0 && contextRow < RPG_STAGE_ROWS && contextColumn >= 0 &&
                contextColumn < RPG_STAGE_WORLD_COLUMNS) {
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
        /* Standalone electric routes are now owned by receiver attachments;
           the retired conveyor palette can no longer enter wire edit mode. */
        bool isWirePropertySelected = false;
        bool isBlockPropertySelected = FindBlockPropertyPlacement(selectedBlockType) != NULL;
        bool isMapEventPropertySelected = RpgBlockInventory_IsMapEventProperty(selectedBlockType);
        bool isAttachmentSelected = RpgBlockInventory_IsAttachment(selectedBlockType);
#if 0 /* Retired conveyor preview and inspector controls. */
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
#endif
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
                if (RpgAttachments_Remove(&attachments, &stage, removedAttachment)) {
                    PushAttachmentRemovedHistory(&blockHistory, removedAttachment);
                    isAttachmentErasePointerHeld = true;
                    blockEditedThisFrame = true;
                    message = "Attachment removed";
                }
            }
            }
        }
        if (!RpgEditorDrag_IsBusy(&attachmentDrag) && !isEditorPlaying &&
            !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
            !isAttachmentPathDragActive && !isWireEndpointDragActive && !isUiBlockingMap &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 worldPosition = GetEditorReferenceWorldPosition(&stage, mapIndex, mapMousePosition);
            int clickedAttachmentIndex = RpgAttachments_FindAtWorldPosition(&attachments, &stage,
                                                                              worldPosition, 14.0f);
            int clickedRow = -1;
            int clickedColumn = -1;
            int pathAttachmentIndex = -1;
            int wireIndex = -1;
            bool wireStart = false;
            (void)RpgStage_GetWorldCellAtPosition(&stage, worldPosition,
                                                   &clickedRow, &clickedColumn);
            bool hasTrajectoryHandle =
                clickedRow >= 0 && clickedColumn >= 0 &&
                (RpgAttachments_FindDataPathEndpoint(&attachments, clickedRow, clickedColumn,
                                                      &pathAttachmentIndex) ||
                 RpgWires_FindEndpoint(&wires, clickedRow, clickedColumn, &wireIndex, &wireStart));
            /* Only a trajectory endpoint at the pressed cell wins the gesture.
               A receiver may own a wire whose other endpoint is elsewhere;
               that must not turn a receiver-move drag into an endpoint drag. */
            if (clickedAttachmentIndex >= 0 &&
                !hasTrajectoryHandle) {
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
                snappedAttachment.flagStartZipperConnected = draggedAttachmentBeforeEdit.flagStartZipperConnected;
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
                    RpgGridCell previousOwnerCell = { 0 };
                    RpgGridCell nextOwnerCell = { 0 };
                    bool hasPreviousOwnerCell = RpgAttachments_GetOwnerBlockCell(
                        &draggedAttachmentBeforeEdit, &previousOwnerCell);
                    bool hasNextOwnerCell = RpgAttachments_GetOwnerBlockCell(
                        &attachmentDragPreview, &nextOwnerCell);
                    if (RpgAttachments_Replace(&attachments, &stage,
                                                draggedAttachmentIndex, attachmentDragPreview)) {
                        PushAttachmentChangedHistory(&blockHistory, draggedAttachmentIndex,
                                                     draggedAttachmentBeforeEdit);
                        /* The drag result is represented only as ordinary cell
                           edits.  There is no special move notification: the old
                           owner loses the attachment and the new owner gains it. */
                        if (hasPreviousOwnerCell && hasNextOwnerCell &&
                        previousOwnerCell.row == nextOwnerCell.row &&
                        previousOwnerCell.column == nextOwnerCell.column) {
                        EditorPreviewEditNotification cellEdit = {
                            .category = RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT,
                            .subjectId = attachmentDragPreview.folderId,
                            .beforeContent = GetEditorPreviewAttachmentContent(
                                &draggedAttachmentBeforeEdit),
                            .afterContent = GetEditorPreviewAttachmentContent(
                                &attachmentDragPreview)
                        };
                        SetEditorPreviewEditCell(&cellEdit, &stage, previousOwnerCell);
                        NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_CELLS, &cellEdit,
                                                &editorPreviewEditsThisFrame);
                    } else {
                        if (hasPreviousOwnerCell) {
                            EditorPreviewEditNotification cellEdit = {
                                .category = RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT,
                                .subjectId = draggedAttachmentBeforeEdit.folderId,
                                .beforeContent = GetEditorPreviewAttachmentContent(
                                    &draggedAttachmentBeforeEdit)
                            };
                            SetEditorPreviewEditCell(&cellEdit, &stage, previousOwnerCell);
                            NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_CELLS, &cellEdit,
                                                    &editorPreviewEditsThisFrame);
                        }
                        if (hasNextOwnerCell) {
                            EditorPreviewEditNotification cellEdit = {
                                .category = RPG_EDITOR_PREVIEW_CATEGORY_ATTACHMENT,
                                .subjectId = attachmentDragPreview.folderId,
                                .afterContent = GetEditorPreviewAttachmentContent(
                                    &attachmentDragPreview)
                            };
                            SetEditorPreviewEditCell(&cellEdit, &stage, nextOwnerCell);
                            NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_CELLS, &cellEdit,
                                                    &editorPreviewEditsThisFrame);
                        }
                        }
                        blockEditedThisFrame = true;
                        message = "Attachment moved";
                    } else message = "Attachment move failed";
                } else if (!isAttachmentDragPreviewSnapped) message = "Attachment move cancelled";
                RpgEditorDrag_End(&attachmentDrag);
                draggedAttachmentIndex = -1;
                attachmentDragDrawSkipIndex = -1;
                isAttachmentDragPreviewVisible = false;
            }
        } else if (attachmentDrag.pending && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            /* Starting an attachment drag must not consume a normal-mode
               click.  In that case retain the pre-existing inspector
               behaviour; only a promoted drag hides it and moves the mount. */
            if (!blockMode && draggedAttachmentIndex >= 0 &&
                draggedAttachmentIndex < attachments.count) {
                RpgAttachment *attachment = &attachments.entries[draggedAttachmentIndex];
                if (RpgAttachments_IsReceiver(attachment)) {
                    int receiverIndex = RpgReceivers_FindAtCell(&receivers, attachment->cell);
                    RpgReceiver receiver;
                    if (receiverIndex >= 0 && RpgReceivers_Get(&receivers, receiverIndex, &receiver)) {
                        RpgGridSide previousSide = receiver.side;
                        BlockHistoryEntry receiverHistory = CreateReceiverChangedHistory(
                            receiverIndex, &receivers, &wires);
                        if (RpgReceivers_CycleSide(&receivers, receiverIndex)) {
                            AppendBlockHistory(&blockHistory, receiverHistory);
                            for (int wireIndex = 0; wireIndex < wires.count; wireIndex++) {
                                RpgWire *wire = &wires.entries[wireIndex];
                                if (wire->hasReceiverSource &&
                                    wire->receiverCell.row == receiver.cell.row &&
                                    wire->receiverCell.column == receiver.cell.column &&
                                    wire->receiverSide == previousSide)
                                    wire->receiverSide = (RpgGridSide)((previousSide + 1) % 4);
                            }
                            blockEditedThisFrame = true;
                            message = "Receiver side changed";
                        }
                    }
                } else {
                    selected = 6;
                    selectedAttachmentIndex = draggedAttachmentIndex;
                    isAttachmentPathEditing = false;
                    message = attachment->type == RPG_BLOCK_ATTACHMENT_DATA_BUTTON ?
                                  "Data button selected" : "Attachment selected";
                }
            }
            RpgEditorDrag_End(&attachmentDrag);
            draggedAttachmentIndex = -1;
        }
        /* Register both possibilities at pointer-down.  The path is drag-only;
           the receiver is click-capable.  They may occupy the same cell. */
        if (!RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
            !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) &&
            !isWireEndpointDragActive && !isUiBlockingMap &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                                     RPG_STAGE_TILE_SIZE);
            const int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
            RpgEditorPointerTarget clickTarget = { 0 };
            RpgEditorPointerTarget dragTarget = { 0 };
            int receiverIndex = RpgReceivers_FindAtCell(&receivers, (RpgGridCell){ row, column });
            int pathAttachmentIndex = -1;
            int wireIndex = -1;
            bool wireStart = false;

            if (receiverIndex >= 0)
                clickTarget = (RpgEditorPointerTarget){ RPG_EDITOR_POINTER_TARGET_RECEIVER,
                                                         receiverIndex, true, false };
            if (RpgAttachments_FindDataPathEndpoint(&attachments, row, column,
                                                     &pathAttachmentIndex)) {
                dragTarget = (RpgEditorPointerTarget){ RPG_EDITOR_POINTER_TARGET_ATTACHMENT_PATH,
                                                        pathAttachmentIndex, false, true };
            } else if (RpgWires_FindEndpoint(&wires, row, column, &wireIndex, &wireStart)) {
                dragTarget = (RpgEditorPointerTarget){ RPG_EDITOR_POINTER_TARGET_WIRE_ENDPOINT,
                                                        wireIndex * 2 + (wireStart ? 1 : 0), false, true };
            }
            if (clickTarget.acceptsClick || dragTarget.acceptsDrag)
                RpgEditorPointerInteraction_Begin(&mapObjectPointerInteraction, mousePosition,
                                                   clickTarget, dragTarget);
        }
        {
            RpgEditorPointerTarget dragTarget = RpgEditorPointerInteraction_Update(
                &mapObjectPointerInteraction, mousePosition);
            if (dragTarget.kind == RPG_EDITOR_POINTER_TARGET_ATTACHMENT_PATH) {
                selectedAttachmentIndex = dragTarget.index;
                isAttachmentPathDragActive = true;
                isAttachmentPathDragVisualActive = true;
                message = "Drag trajectory endpoint";
            } else if (dragTarget.kind == RPG_EDITOR_POINTER_TARGET_WIRE_ENDPOINT) {
                draggedWireIndex = dragTarget.index / 2;
                draggedWireStart = (dragTarget.index % 2) != 0;
                draggedWireBeforeEdit = wires.entries[draggedWireIndex];
                hasRecordedDraggedWireHistory = false;
                draggedWireLastRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
                draggedWireLastColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS *
                                                RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
                isWireEndpointDragActive = true;
                message = "Drag wire endpoint to a block";
            }
        }
        if (isAttachmentPathDragActive && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                                            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
            int pathColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                                   RPG_STAGE_TILE_SIZE);
            int pathRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
            if (!isUiBlockingMap && MoveAttachmentPathEndpointToward(&attachments, &stage,
                                                                       selectedAttachmentIndex,
                                                                       pathRow, pathColumn)) {
                blockEditedThisFrame = true;
                message = "Trajectory endpoint updated";
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                isAttachmentPathDragActive = false;
                isAttachmentPathDragVisualActive = false;
            }
        }
        /* PNGは通常モード・ブロックモード共通のドラッグで移動し、他の選択より先に入力を所有する。 */
        if (!isMoveFunctionEditorOpen && !isAttachmentPathEditing && !isAttachmentPathDragActive &&
            !RpgEditorDrag_IsBusy(&attachmentDrag) &&
            !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) && !isUiBlockingMap && !RpgEditorDrag_IsBusy(&imageObjectDrag) &&
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
        if (!isEditorPlaying && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
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
        /* A magnet starts as one cell.  Its existing object is resized through
           the same pending/active drag state used by special-block movement. */
        if (blockMode && RpgBlockInventory_IsMagnetBlock(selectedBlockType) &&
            !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) &&
            !isBlockPropertySelected && !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) && !isUiBlockingMap &&
            !RpgEditorDrag_IsBusy(&effectBlockDrag) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) /
                               RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
            const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(&stage, row, column);
            if (magnet != NULL) {
                RpgEditorDrag_Begin(&effectBlockDrag, mousePosition);
                pendingEffectBlockRow = magnet->rootRow;
                pendingEffectBlockColumn = magnet->rootColumn;
                BeginObjectMoveAnchor(&effectBlockMoveAnchor, magnet->rootRow,
                                      magnet->rootColumn, row, column);
                magnetResizeEdges = GetMagnetResizeEdges(magnet, mapIndex, mapMousePosition);
                isMagnetResizeDrag = magnetResizeEdges != 0;
                message = isMagnetResizeDrag ? "Drag magnet edge to resize" :
                                               "Drag magnet to move";
            } else if (PlaceMagnetRectangle(&stage, &attachments, &blockHistory,
                                             row, column, row, column, selectedBlockType)) {
                blockEditedThisFrame = true;
                message = "Magnet placed";
            } else message = "Magnet needs an empty cell";
        } else if (!isEditorPlaying &&
            !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && !isBlockPropertySelected && !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
            !isUiBlockingMap && !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&imageObjectDrag) &&
            !effectBlockDrag.active && !effectBlockDrag.pending && !isWireEndpointDragActive &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int column = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
            int row = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
            if (row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
                (RpgBlockInventory_IsEffectBlock(stage.blocks[row][column]) ||
                 RpgBlockInventory_IsMetalBlock(stage.blocks[row][column]) ||
                 RpgBlockInventory_IsPushBlock(stage.blocks[row][column]))) {
                int rootRow = row;
                int rootColumn = column;
                /* Magnets are variable-size rectangles.  Although their
                 * individual tiles are also registered as a legacy one-cell
                 * effect shape, that shape must never choose the drag root:
                 * doing so makes every clicked component look like offset
                 * (0,0) and snaps it to the rectangle's left/top corner.
                 * Resolve the dynamic owner before fixed-shape lookup so all
                 * multi-cell objects feed the common move anchor correctly. */
                const RpgMagnetObject *magnet =
                    RpgStage_GetMagnetAtCellConst(&stage, row, column);
                int signalIndex = RpgSignalBlocks_FindAtCell(&signalBlocks, &stage, row, column);
                if (magnet != NULL) {
                    rootRow = magnet->rootRow;
                    rootColumn = magnet->rootColumn;
                } else if (signalIndex >= 0) {
                    rootRow = signalBlocks.entries[signalIndex].row;
                    rootColumn = signalBlocks.entries[signalIndex].column;
                } else if (!FindEffectBlockRoot(&stage, row, column, &rootRow, &rootColumn, NULL) &&
                           !RpgBlockInventory_IsMetalBlock(stage.blocks[row][column]) &&
                           !RpgBlockInventory_IsPushBlock(stage.blocks[row][column])) {
                    rootRow = -1;
                }
                if (rootRow >= 0) {
                    RpgEditorDrag_Begin(&effectBlockDrag, mousePosition);
                    pendingEffectBlockRow = rootRow;
                    pendingEffectBlockColumn = rootColumn;
                    BeginObjectMoveAnchor(&effectBlockMoveAnchor, rootRow, rootColumn,
                                          row, column);
                    magnetResizeEdges = GetMagnetResizeEdges(magnet, mapIndex,
                                                              mapMousePosition);
                    isMagnetResizeDrag = magnetResizeEdges != 0;
                    message = isMagnetResizeDrag ? "Drag magnet edge to resize" :
                                                    "Click to rotate, drag to move";
                }
            }
        }
        if (effectBlockDrag.pending && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap &&
            RpgEditorDrag_Update(&effectBlockDrag, mousePosition)) {
            draggedEffectBlockRow = pendingEffectBlockRow;
            draggedEffectBlockColumn = pendingEffectBlockColumn;
            pendingEffectBlockRow = -1;
            pendingEffectBlockColumn = -1;
            if (isMagnetResizeDrag) {
                isMagnetResizePreviewVisible = true;
                magnetResizePreviewRootRow = draggedEffectBlockRow;
                magnetResizePreviewRootColumn = draggedEffectBlockColumn;
            } else {
                isEffectBlockDragPreviewVisible = true;
                effectBlockDragPreviewRow = draggedEffectBlockRow;
                effectBlockDragPreviewColumn = draggedEffectBlockColumn;
                effectBlockDragPreviewGrabRowOffset = effectBlockMoveAnchor.grabbedRowOffset;
                effectBlockDragPreviewGrabColumnOffset = effectBlockMoveAnchor.grabbedColumnOffset;
            }
            effectBlockDragPointer = mapMousePosition;
            HideInspectorDuringObjectDrag(&selected, &activeDialogueLine, &isItemNameEditing,
                                          &isAttachmentPathEditing, &isReferencePathEditing);
            message = isMagnetResizeDrag ? "Drag to resize magnet" :
                                            "Drag special block to an empty cell";
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
            message = "Drag image object (hold Alt for free position)";
        }
        if (imageObjectDrag.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !isUiBlockingMap) {
            RpgEditorDrag_Update(&imageObjectDrag, mousePosition);
            imageObjectDragPointer = GetImageObjectDragPreviewPointer(mapMousePosition);
        }
        if (RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            RpgEditorPointerTarget clickTarget = RpgEditorPointerInteraction_End(
                &mapObjectPointerInteraction);
            if (clickTarget.kind == RPG_EDITOR_POINTER_TARGET_RECEIVER) {
                RpgReceiver receiver;
                if (RpgReceivers_Get(&receivers, clickTarget.index, &receiver)) {
                    RpgGridSide previousSide = receiver.side;
                    BlockHistoryEntry receiverHistory = CreateReceiverChangedHistory(clickTarget.index,
                                                                                      &receivers, &wires);
                    if (RpgReceivers_CycleSide(&receivers, clickTarget.index)) {
                        AppendBlockHistory(&blockHistory, receiverHistory);
                        for (int wireIndex = 0; wireIndex < wires.count; wireIndex++) {
                            RpgWire *wire = &wires.entries[wireIndex];
                            if (wire->hasReceiverSource && wire->receiverCell.row == receiver.cell.row &&
                                wire->receiverCell.column == receiver.cell.column &&
                                wire->receiverSide == previousSide)
                                wire->receiverSide = (RpgGridSide)((previousSide + 1) % 4);
                        }
                        blockEditedThisFrame = true;
                        message = "Receiver side changed";
                    }
                }
            }
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
            /* A plain click in normal mode selects the signal block below.
             * Rotation remains the block-mode click behaviour only. */
            if (blockMode && !isMagnetResizeDrag && signalIndex >= 0) {
                if (RpgSignalBlocks_Rotate(&signalBlocks, &stage, signalIndex))
                    message = "Signal shrink block rotated";
                else message = "Rotation needs an empty cell or an idle signal block";
            }
            RpgEditorDrag_End(&effectBlockDrag);
            pendingEffectBlockRow = -1;
            pendingEffectBlockColumn = -1;
            isMagnetResizeDrag = false;
            magnetResizeEdges = 0;
            effectBlockMoveAnchor = (EditorObjectMoveAnchor){ 0 };
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
            bool freePosition = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
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
                int pointerColumn = (int)((mapMousePosition.x + mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) / RPG_STAGE_TILE_SIZE);
                int pointerRow = (int)(mapMousePosition.y / RPG_STAGE_TILE_SIZE);
                int destinationColumn = pointerColumn;
                int destinationRow = pointerRow;
                /* Only resizing consumes the raw pointer cell.  All object
                   moves resolve the same captured object-relative anchor. */
                if (!isMagnetResizeDrag)
                    (void)GetObjectMoveAnchorDestination(&effectBlockMoveAnchor,
                                                         pointerRow, pointerColumn,
                                                         &destinationRow, &destinationColumn);
                if (destinationRow >= 0 && destinationRow < RPG_STAGE_ROWS && destinationColumn >= 0 &&
                    destinationColumn < RPG_STAGE_WORLD_COLUMNS &&
                    (isMagnetResizeDrag || destinationRow != draggedEffectBlockRow ||
                     destinationColumn != draggedEffectBlockColumn)) {
                    bool changed = isMagnetResizeDrag ?
                        ResizeMagnetObject(&stage, &attachments, &blockHistory,
                                           draggedEffectBlockRow, draggedEffectBlockColumn,
                                           magnetResizeEdges, destinationRow, destinationColumn) :
                        MoveSpecialBlock(&stage, &attachments, &items, &blockHistory, &signalBlocks,
                                         draggedEffectBlockRow, draggedEffectBlockColumn,
                                         destinationRow, destinationColumn);
                    if (changed) {
                        RpgWires_RemoveBroken(&wires, &stage);
                        RpgReceivers_RemoveBroken(&receivers, &stage);
                        RpgAttachments_RemoveBroken(&attachments, &stage);
                        blockEditedThisFrame = true;
                        message = isMagnetResizeDrag ? "Magnet resized" : "Object moved";
                    } else message = isMagnetResizeDrag ? "Magnet needs empty cells" :
                                                          "Effect blocks need ordinary blocks or empty cells";
                }
            }
            RpgEditorDrag_End(&effectBlockDrag);
            draggedEffectBlockRow = -1;
            draggedEffectBlockColumn = -1;
            isEffectBlockDragPreviewVisible = false;
            effectBlockDragPreviewRow = -1;
            effectBlockDragPreviewColumn = -1;
            effectBlockDragPreviewGrabRowOffset = 0;
            effectBlockDragPreviewGrabColumnOffset = 0;
            isMagnetResizePreviewVisible = false;
            magnetResizePreviewRootRow = -1;
            magnetResizePreviewRootColumn = -1;
            isMagnetResizeDrag = false;
            magnetResizeEdges = 0;
            effectBlockMoveAnchor = (EditorObjectMoveAnchor){ 0 };
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
            isWireEndpointDragActive = false;
            hasRecordedDraggedWireHistory = false;
            draggedWireIndex = -1;
            draggedWireLastRow = -1;
            draggedWireLastColumn = -1;
        // 導線は始点を選ぶと隣接ブロックへ終点を置き、端点のドラッグで経路を作り直す。
        } else if (blockMode && !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) && isWirePropertySelected && !RpgEditorDrag_IsBusy(&effectBlockDrag) && !isWireEndpointDragActive &&
                   !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
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
                   !isWireEndpointDragActive && !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) && !isUiBlockingMap &&
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
                   !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
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
                   !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) && !isUiBlockingMap &&
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
        } else if (blockMode && !RpgBlockInventory_IsMagnetBlock(selectedBlockType) &&
                   !isAttachmentPathDragActive && !RpgEditorDrag_IsBusy(&attachmentDrag) &&
                   !isBlockPropertySelected && !isMapEventPropertySelected &&
                   !RpgEditorDrag_IsBusy(&effectBlockDrag) && !RpgEditorDrag_IsBusy(&referenceDrag) && !RpgEditorDrag_IsBusy(&imageObjectDrag) && !isWireEndpointDragActive &&
                   !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) && !isAttachmentSelected &&
                   clickedBlockType == 0 && !isUiBlockingMap &&
                   ((RpgBlockInventory_IsOrdinaryBlock(selectedBlockType) &&
                     IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
                    (!RpgBlockInventory_IsOrdinaryBlock(selectedBlockType) &&
                     IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
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
                    selectedBlockType < RPG_BLOCK_PROPERTY_LEGACY_ITEM) {
                    char objectFolder[RPG_STAGE_PATH_LENGTH];
                    (void)RpgStaticObjectDrop_EnsureFolder(currentStageNumber, &stage, row, column,
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
            /* worldPosition is arranged by the editor's two-dimensional area grid.
               Convert it back to the stage's storage cell before removing a block;
               direct tile division only worked while grid positions happened to
               match their storage-area indices. */
            int column = -1;
            int row = -1;
            bool hasTargetCell = RpgStage_GetWorldCellAtPosition(&stage, worldPosition,
                                                                  &row, &column);
            int referenceIndex = FindEditorReferenceObjectAtMapPoint(&stage, mapIndex, mapMousePosition);
            if (referenceIndex >= 0 && RemoveEditorReferenceObjectAt(&stage.referenceObjects, referenceIndex)) {
                if (selectedReferenceObjectIndex == referenceIndex) {
                    selected = 0;
                    selectedReferenceObjectIndex = -1;
                } else if (selectedReferenceObjectIndex > referenceIndex) selectedReferenceObjectIndex--;
                blockEditedThisFrame = true;
                message = "Reference object removed";
            } else if (hasTargetCell) {
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
        } else if (!isMoveFunctionEditorOpen && !isAttachmentPathEditing && !RpgEditorDrag_IsBusy(&attachmentDrag) &&
                   !effectBlockDrag.active && !referenceDrag.active &&
                   !RpgEditorDrag_IsBusy(&imageObjectDrag) && !RpgEditorDrag_IsBusy(&characterDrag) &&
                   !isWireEndpointDragActive && !RpgEditorPointerInteraction_IsBusy(&mapObjectPointerInteraction) &&
                   !isUiBlockingMap && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
            } else if (!blockMode && clickedRow >= 0 && clickedColumn >= 0 &&
                       RpgStage_GetMagnetAtCellConst(&stage, clickedRow, clickedColumn) != NULL) {
                const RpgMagnetObject *clickedMagnet = RpgStage_GetMagnetAtCellConst(&stage, clickedRow, clickedColumn);
                selected = 5;
                selectedDoorRow = clickedMagnet->rootRow;
                selectedDoorColumn = clickedMagnet->rootColumn;
                isItemNameEditing = false;
                message = "Magnet selected";
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
                             GetReferenceFolderAuthoredName(reference));
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
                if (draggedCharacterKind == 1)
                    MoveCharacterToEditorPointer(&player, &stage, mapIndex, mapMousePosition);
                else if (draggedCharacterKind == 2)
                    MoveCharacterToEditorPointer(&npc, &stage, mapIndex, mapMousePosition);
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
        /* Existing placement/move tools report through the legacy frame flag;
           classify that path as a cell/topology edit.  Inspector settings use
           the variable notification directly above.  Both meet here, so the
           cache invalidation policy is defined only once. */
        if (blockEditedThisFrame &&
            (editorPreviewEditsThisFrame.kinds & RPG_EDITOR_PREVIEW_EDIT_CELLS) == 0)
            NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_CELLS,
                                    &(EditorPreviewEditNotification){
                                        .category = RPG_EDITOR_PREVIEW_CATEGORY_CELL
                                    },
                                    &editorPreviewEditsThisFrame);
        if (!isEditorPlaying)
            InvalidatePreparedEditorPreview(&editorPreviewEditsThisFrame, &stage,
                                             &editorPreviewBuildDirty,
                                             &editorPreviewBuildReady,
                                             &nextStageFolderComparisonTime,
                                             &nextStageFolderSyncTime);
        }
        /* Keep all authoring input memory-first.  Preview preparation resumes
           only during a quiet frame, with the cell writer itself limited to
           a tiny cooperative batch. */
        if (!isEditorPlaying) {
            static bool staticDropChangedCells[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS];
            if (RpgStaticObjectDrop_UpdateFolderAvailability(currentStageNumber, &stage,
                                                             staticDropChangedCells)) {
                /* This monitor runs after the ordinary per-frame editor edit
                   batch has left scope.  Keep its notifications local, then
                   hand that exact changed-cell set to the preview invalidator. */
                EditorPreviewEditBatch staticDropEdits = { 0 };
                /* Folder disappearance is the authoritative event.  Apply
                   every dependent in-memory state immediately, rather than
                   leaving an attachment visible until a later reload. */
                RpgAttachments_RemoveBroken(&attachments, &stage);
                RpgReceivers_RemoveBroken(&receivers, &stage);
                RpgSignalBlocks_RemoveBroken(&signalBlocks, &stage);
                RpgAttachments_BuildRuntimeWires(&attachments, &wires);
                for (int changedRow = 0; changedRow < RPG_STAGE_ROWS; changedRow++)
                    for (int changedColumn = 0; changedColumn < RPG_STAGE_WORLD_COLUMNS; changedColumn++) {
                        if (!staticDropChangedCells[changedRow][changedColumn]) continue;
                        EditorPreviewEditNotification dropEdit = {
                            .category = RPG_EDITOR_PREVIEW_CATEGORY_CELL
                        };
                        SetEditorPreviewEditCell(&dropEdit, &stage,
                                                  (RpgGridCell){ changedRow, changedColumn });
                        NotifyEditorPreviewEdit(RPG_EDITOR_PREVIEW_EDIT_CELLS, &dropEdit,
                                                &staticDropEdits);
                    }
                InvalidatePreparedEditorPreview(&staticDropEdits, &stage,
                                                 &editorPreviewBuildDirty,
                                                 &editorPreviewBuildReady,
                                                 &nextStageFolderComparisonTime,
                                                 &nextStageFolderSyncTime);
                message = "Dropped object folder became error cells";
            }
            bool editorPointerBusy = IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                                    IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                                    IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
            /* Cache generation changes only the disposable Play tree.  It
               must wait for pointer drags, because they can still mutate the
               stage each frame, but a focused text box is not a mutation in
               progress.  Treating keyboard capture as a hard stop let a
               stale text-focus flag leave "checking cache" on screen
               forever.  The worker remains cooperatively budgeted below. */
            bool editorPreviewInputBusy = editorPointerBusy;
            /* Input release is a publication boundary: inspect the edited
               stage now, not after an arbitrary debounce delay. */
            if (!editorPreviewInputBusy && wasEditorPreviewInputBusy)
                nextStageFolderComparisonTime = 0.0;
            if (!editorPreviewInputBusy && GetTime() >= nextEditorPreviewWorkTime) {
                bool previewRepairComplete = false;
                /* A fresh preview already has an authoritative snapshot.  Its
                   remaining cells are normal low-priority materialisation,
                   not a new editor change.  Running the snapshot synchronizer
                   here used to manufacture a candidate, then route it through
                   the area-repair worker before the first cache was complete. */
                if (editorPreviewAwaitingCellGeneration) {
                    if (!RpgObjectFolders_IsStageBuildActive()) {
                        /* The disposable build vanished; leave the initial
                           wait state and let the ordinary recovery path
                           reconnect/recreate it below. */
                        editorPreviewAwaitingCellGeneration = false;
                        editorPreviewInitialGenerationBaselinePending = false;
                        nextStageFolderSyncTime = 0.0;
                    } else if (!RpgBuildCellStorage_IsGenerationPending()) {
                        const bool canPublishFreshBaseline =
                            editorPreviewInitialGenerationBaselinePending;
                        editorPreviewAwaitingCellGeneration = false;
                        editorPreviewInitialGenerationBaselinePending = false;
                        if (canPublishFreshBaseline) {
                            /* The snapshot taken before CreatePreview may
                               contain runtime-normalised object fields that
                               are settled only while the initial cells are
                               materialised.  Establish the authoritative
                               editor state now, rather than interpreting the
                               first comparison as an object-folder edit. */
                            ComposeEditorStageData(&stageFolderSyncBuffer, &layout, &stage,
                                                   &dialogue, &stage3Event, &items);
                            NormalizeEditorPreviewSyncData(&stageFolderSyncBuffer);
                            if (CopyEditorStageData(&lastStageFolderSync,
                                                    &stageFolderSyncBuffer)) {
                                hasStageFolderSync = true;
                                lastStageFolderSyncNumber = currentStageNumber;
                                hasPendingStageFolderSync = false;
                                pendingStageFolderSyncNumber = 0;
                                FinishEditorPreviewPreparationDisplay();
                                editorPreviewBuildReady = true;
                                editorPreviewBuildDirty = false;
                                nextStageFolderSyncTime = 0.0;
                            } else {
                                /* A snapshot copy failure is recoverable,
                                   but it is not an object edit.  Retry the
                                   ordinary synchronizer instead of leaving
                                   the status at an old preparation label. */
                                editorPreviewBuildReady = false;
                                editorPreviewBuildDirty = true;
                                nextStageFolderSyncTime = GetTime() + 0.050;
                            }
                        } else {
                            /* A user edited while cells were materialising.
                               Preserve that pending diff and let the normal
                               worker publish its exact local update. */
                            nextStageFolderSyncTime = GetTime();
                        }
                    }
                }
                if (!editorPreviewAwaitingCellGeneration) {
                /* Snapshot comparison and repair queuing happen only after
                   the interaction handlers above have finished. */
                SyncEditorStageFolderWhenIdle(&layout, &stage, &dialogue, &stage3Event, &items,
                                               currentStageNumber, &lastStageFolderSync, &hasStageFolderSync,
                                               &lastStageFolderSyncNumber, &nextStageFolderComparisonTime,
                                               &nextStageFolderSyncTime, &editorPreviewBuildDirty,
                                               &editorPreviewBuildReady, &editorPreviewBuildStageNumber);
                /* Attachment and wire edits have no cells.csv write to start
                   a build job.  They still own a small, explicit metadata
                   batch.  Complete that batch first regardless of whether a
                   terrain-cell generation job happens to be active. */
                if (editorPreviewBuildDirty && nextStageFolderSyncTime <= 0.0 &&
                    editorPreviewBuildStageNumber == currentStageNumber &&
                    editorPreviewHasDirectCellWork &&
                    !RpgBuildCellStorage_IsGenerationPending() &&
                    RpgObjectFolders_IsStageBuildActive()) {
                    TraceEditorPreviewRoute("direct-batch-attempt");
                    if (FinalizePreviewOwnershipCellBatch(&editorPreviewBuildStage)) {
                        TraceEditorPreviewRoute("direct-batch-succeeded");
                        editorPreviewHasDirectCellWork = false;
                        RpgBuildCellStorage_ClearQueuedCellUpdateProgress();
                        /* A stale map-repair request must not prevent an
                           attachment owner from receiving its metadata.
                           Preserve that request for the next cooperative
                           frame; only claim overall readiness once it is
                           clear. */
                        if (!RpgObjectFolders_HasEditorPreviewStaticMutations()) {
                            FinishEditorPreviewPreparationDisplay();
                            editorPreviewBuildReady = true;
                            editorPreviewBuildDirty = false;
                        }
                    } else {
                        TraceEditorPreviewRoute("direct-batch-failed");
                        /* Preserve the exact FILE/FOLDER/PNG batch.  A
                           transient directory conflict is retried locally;
                           only a genuinely detached preview is recreated by
                           the separate inactive-build branch. */
                        if (!RetryFailedPreviewDirectBatch(&editorPreviewBuildReady,
                                                           &editorPreviewBuildDirty,
                                                           &nextStageFolderSyncTime))
                            RebuildAfterPreviewLocalFailure(&editorPreviewBuildReady,
                                                            &editorPreviewBuildDirty,
                                                            &nextStageFolderSyncTime);
                    }
                } else if (editorPreviewBuildDirty && nextStageFolderSyncTime <= 0.0 &&
                    editorPreviewBuildStageNumber == currentStageNumber &&
                    !RpgObjectFolders_IsStageBuildActive()) {
                    /* A failed or externally detached preview must not leave
                       the status permanently at "checking cache".  The
                       preview is disposable, so recover it from the
                       authoritative editor stage on an idle frame. */
                    if (EnsureEditorPreviewCache(currentStageNumber, &layout, &stage)) {
                        editorPreviewBuildStage = stage;
                        editorPreviewBuildStageNumber = currentStageNumber;
                        editorPreviewHasDirectCellWork = false;
                        memset(pendingPreviewCompactCells, 0,
                               sizeof(pendingPreviewCompactCells));
                        memset(pendingPreviewOwnershipCells, 0,
                               sizeof(pendingPreviewOwnershipCells));
                        pendingPreviewRemovedCompositeCount = 0;
                        ClearPendingPreviewMovableObjectBatch();
                        /* CreatePreview intentionally leaves distant areas in
                           the background queue.  Do not report the cache as
                           ready merely because its local repair succeeded:
                           that would hide a detached worker and make the
                           displayed cell target appear frozen. */
                        if (RpgBuildCellStorage_IsGenerationPending()) {
                            editorPreviewAwaitingCellGeneration = true;
                            editorPreviewInitialGenerationBaselinePending = true;
                            MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_CELLS);
                            editorPreviewBuildReady = false;
                            editorPreviewBuildDirty = true;
                        } else {
                            FinishEditorPreviewPreparationDisplay();
                            editorPreviewBuildReady = true;
                            editorPreviewBuildDirty = false;
                        }
                    } else {
                        /* Keep the editor responsive if the filesystem is
                           temporarily unavailable; retry after a short idle
                           interval instead of retrying every frame. */
                        nextStageFolderSyncTime = GetTime() + 0.250;
                    }
                } else if (editorPreviewBuildDirty && nextStageFolderSyncTime <= 0.0 &&
                           editorPreviewBuildStageNumber == currentStageNumber &&
                           RpgObjectFolders_IsStageBuildActive()) {
                    /* A compact terrain-only operation has no block/object
                       folders to repair.  Finish its queued cells directly;
                       routing it through the clean repair path would do
                       unrelated preview cleanup for every brush stroke. */
                    if (editorPreviewHasDirectCellWork &&
                        !RpgObjectFolders_HasEditorPreviewStaticMutations()) {
                        TraceEditorPreviewRoute("local-batch-attempt");
                        if (!RpgBuildCellStorage_IsGenerationPending() &&
                            FinalizePreviewOwnershipCellBatch(&editorPreviewBuildStage)) {
                            TraceEditorPreviewRoute("local-batch-succeeded");
                            editorPreviewHasDirectCellWork = false;
                            RpgBuildCellStorage_ClearQueuedCellUpdateProgress();
                            FinishEditorPreviewPreparationDisplay();
                            editorPreviewBuildReady = true;
                            editorPreviewBuildDirty = false;
                        } else if (!RpgBuildCellStorage_IsGenerationPending()) {
                            if (!RetryFailedPreviewDirectBatch(&editorPreviewBuildReady,
                                                               &editorPreviewBuildDirty,
                                                               &nextStageFolderSyncTime))
                                RebuildAfterPreviewLocalFailure(&editorPreviewBuildReady,
                                                                &editorPreviewBuildDirty,
                                                                &nextStageFolderSyncTime);
                        }
                    } else {
                        TraceEditorPreviewRoute("area-repair-selected");
                        /* Folder repair can still have one synchronous phase.
                           Publish its state to the right-top log for a full
                           rendered frame before entering that phase. */
                        if (!editorPreviewAreaRepairAnnounced) {
                            editorPreviewAreaRepairAnnounced = true;
                            MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_AREA_REPAIR);
                        } else {
                            RpgStageAuthority previousAuthority = RpgStageAuthority_Get();
                            RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_RUNTIME);
                            /* The cooperative map-repair path removes and
                               recreates preview-owned folders over several
                               frames.  Those writes are authored by this editor,
                               not an external edit.  Without the same watcher
                               guard used by the direct-cell batch, the next
                               ReadDirectoryChangesW event can mark the map dirty
                               again and keep the loading state alive forever. */
                            RpgStageBuild_BeginInternalPreviewWrite();
                            bool previewRepairSucceeded =
                                RpgObjectFolders_UpdateEditorPreviewRepair(&editorPreviewBuildStage,
                                                                            &attachments, &receivers, &wires,
                                                                            layout.playerPosition,
                                                                            &previewRepairComplete);
                            RpgStageBuild_EndInternalPreviewWrite();
                            RpgStageAuthority_Enter(previousAuthority);
                            if (!previewRepairSucceeded) {
                                /* The repair job has restored its selected
                                   maps to the dirty set.  Retry that local
                                   repair without abandoning the active
                                   preview; abandoning here was the remaining
                                   route from one failed metadata write to a
                                   complete area rebuild. */
                                editorPreviewBuildReady = false;
                                editorPreviewBuildDirty = true;
                                MarkEditorPreviewPreparation(RPG_EDITOR_PREVIEW_PREPARATION_RECOVERY);
                                nextStageFolderSyncTime = GetTime() + 0.100;
                                editorPreviewRepairRetryCount++;
                                if (editorPreviewRepairRetryCount >= 3)
                                    RebuildAfterPreviewLocalFailure(&editorPreviewBuildReady,
                                                                    &editorPreviewBuildDirty,
                                                                    &nextStageFolderSyncTime);
                            } else if (previewRepairComplete &&
                                       !RpgBuildCellStorage_IsGenerationPending()) {
                                editorPreviewHasDirectCellWork = false;
                                editorPreviewRepairRetryCount = 0;
                                FinishEditorPreviewPreparationDisplay();
                                editorPreviewBuildReady = true;
                                editorPreviewBuildDirty = false;
                            }
                        }
                    }
                }
                }
                /* Never let cache work dominate the update loop, even while
                   the mouse is resting. */
                nextEditorPreviewWorkTime = GetTime() + 0.016;
            }
            wasEditorPreviewInputBusy = editorPreviewInputBusy;
        }
        /* This works solely on editorPreviewBuildStage, an already-published
           snapshot.  Unlike comparison/publication above, it cannot observe a
           half-finished pointer edit, so it may progress while input is held. */
        if (!isEditorPlaying && RpgBuildCellStorage_IsGenerationPending() &&
            GetTime() >= nextEditorPreviewGenerationTime) {
            /* Go through the stage-build wrapper so this editor-generated
               cell batch is not fed back through ReadDirectoryChangesW as a
               filesystem-side area mutation.  The editor's ordinary frame
               runs with no persistent stage authority; without entering the
               disposable editor-runtime authority here, RpgStageBuild_Update
               rejects the work at its first guard and the same queued cell is
               displayed forever. */
            RpgStageAuthority previousAuthority = RpgStageAuthority_Get();
            RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_RUNTIME);
            RpgStageBuild_Update(&editorPreviewBuildStage);
            RpgStageAuthority_Enter(previousAuthority);
            nextEditorPreviewGenerationTime = GetTime() + 0.016;
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
                     isGlobalMapOpen, isBlockContextMenuOpen, blockContextMenuBounds,
                     editorPreviewBuildReady, editorPreviewBuildDirty,
                     editorPreviewBuildStageNumber, currentStageNumber);
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
