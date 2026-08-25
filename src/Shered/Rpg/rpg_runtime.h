// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_button_event.h, rpg_character.h, rpg_data_shot.h, rpg_dialogue.h, rpg_inspect.h, rpg_item.h, rpg_layout.h, rpg_map_event.h, rpg_receiver.h, rpg_signal_block.h, rpg_stage.h, rpg_stage3_event.h, rpg_stage_background.h, rpg_wire.h, rpg_zipper.h
// 役割: 本編とエディター内プレイが共有するRPGの一フレーム更新・描画状態を定義する。
#ifndef RPG_RUNTIME_H
#define RPG_RUNTIME_H

#include "raylib.h"
#include "rpg_attachment.h"
#include "rpg_button_event.h"
#include "rpg_character.h"
#include "rpg_data_shot.h"
#include "rpg_dialogue.h"
#include "rpg_inspect.h"
#include "rpg_item.h"
#include "rpg_layout.h"
#include "rpg_map_event.h"
#include "rpg_receiver.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_stage3_event.h"
#include "rpg_stage_background.h"
#include "rpg_wire.h"
#include "rpg_zipper.h"

typedef struct RpgSceneState RpgSceneState;

typedef struct RpgRuntimeContext {
    RpgLayout *layout; RpgStageBackground *stageBackground; RpgStage *stage; RpgItems *items; RpgReferenceObjects *referenceDrops; RpgWires *wires; RpgReceivers *receivers; RpgAttachments *attachments; RpgSignalBlocks *signalBlocks; RpgDataShots *dataShots; RpgButtonEvent *buttonEvent; RpgMapEvents *events; RpgDialogue *dialogue; RpgStage3Event *stage3Event; RpgAreaEntryEvents *areaEntryEvents; RpgZipper *zipper; RpgInspect *inspect; RpgCharacter *player; RpgCharacter *npc;
    int *dialogueIndex; int *stage3IntroIndex; int *inspectFunctionIndex; int *inspectLineIndex; int *inspectTarget; bool *isInspectMoveRunning; float *inspectMoveElapsed; float *inspectMoveStartX; float *inspectMoveStartY; RpgInspectMove **activeInspectMove; float *inspectMoveTransitionElapsed; int *activeWaitFunctionIndex; float *inspectWaitElapsed; bool *stage3IntroShown; bool *areaEntryShown; RpgStage3Event **activeEntryEvent; bool *zipperFollowsPlayer; bool *isZipperLaunched; Vector2 *zipperLaunchVelocity; int *attachedDataShotIndex; int *attachedAttachmentIndex; Vector2 *attachedDataShotOffset; bool *isZipperAttachedToBlock; RpgGridCell *zipperAttachedBlockCell; bool *zipperPointerSelected; bool *isZipperPointerFeedbackSuppressed; double *lastZipperPointerClickTime;
    RpgReferenceTarget *selectedReferencePointerTarget; bool *isReferencePointerFeedbackSuppressed; bool *isReferencePointerPressed; RpgReferenceTarget *pressedReferenceTarget; Vector2 *referencePressPosition; bool *isReferenceDragActive; RpgReferenceTarget *draggedReferenceTarget; Vector2 *referenceDragPosition; double *lastReferencePointerClickTime; float *zipperAnimationElapsed; bool *npcInspectCompleted; bool *zipperInspectCompleted; bool *isZipperControllable; bool *wasDataButtonPressed; int *previousMap; bool *cameraFollowsPlayer; char *itemMessage; int itemMessageSize; float *itemMessageTimer; char *referenceText; int referenceTextSize; char *referenceFileName; int referenceFileNameSize; bool *isReferenceTextOpen; Camera2D *camera; Texture2D zipperTexture; Texture2D fileTexture; bool showStopButton; RpgSceneState *scene;
} RpgRuntimeContext;

void RpgRuntime_UpdateAndDraw(RpgRuntimeContext *context);
/* Play開始・停止時に、継続中のFile→Folder演出だけを破棄する。 */
void RpgRuntime_ResetTransientState(void);
/* エディター内Playと描画で共有するStopボタンの表示・クリック範囲を返す。 */
Rectangle RpgRuntime_GetStopButtonBounds(void);
// Inbox のcmd要求を共通に処理する。保持中のフォルダをbuildへ返してから、現在の接触対象を取得する。
void RpgRuntime_ProcessZipperCommand(RpgRuntimeContext *context);
/* Folder内のCMD要求で、同じエリアのFolderをZipper出現元として追従状態へ切り替える。 */
void RpgRuntime_ProcessReferenceFolderZipperCommand(RpgRuntimeContext *context);
// 取り込み完了後のフォルダ返却演出を進行・描画する。
void RpgRuntime_UpdateZipperFolderReturn(RpgRuntimeContext *context, float deltaTime);
void RpgRuntime_DrawZipperFolderReturn(const RpgZipper *zipper);

#endif
