// 依存する自プロジェクト内ファイル: rpg_runtime.h, game_font.h, rpg_block_inventory.h, rpg_object_folder.h, rpg_runtime_update.h, rpg_explorer_shell.h
// 役割: 本編とエディター内プレイで共通のRPGフレーム更新と描画を実装する。
#include "rpg_runtime.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif
#include "game_font.h"
#include "rpg_block_inventory.h"
#include "rpg_object_folder.h"
#include "rpg_explorer_shell.h"
#include "rpg_runtime_update.h"
#include "rpg_scene.h"
enum { RPG_SCREEN_WIDTH = 960, RPG_SCREEN_HEIGHT = 540 };
static const float zipperImportAnimationDuration = 0.60f;
static Texture2D folderReturnIcon = { 0 };
static bool hasLoadedFolderReturnIcon = false;

static const char *npcTalkPrompt = u8"[E] \u8a71\u3057\u304b\u3051\u308b";
static const char *zipperInspectPrompt = u8"[I] \u8abf\u3079\u308b";
static const Rectangle referenceTextCloseButton = { 648.0f, 262.0f, 144.0f, 26.0f };

static bool LoadReferenceText(const char *filePath, char *text, size_t textSize)
{
    FILE *file = NULL;
#ifdef _WIN32
    wchar_t widePath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    if (MultiByteToWideChar(CP_UTF8, 0, filePath, -1, widePath,
                            RPG_STAGE_REFERENCE_PATH_LENGTH) > 0)
        file = _wfopen(widePath, L"rb");
#else
    file = fopen(filePath, "rb");
#endif
    if (file == NULL || textSize == 0) return false;
    size_t readSize = fread(text, 1, textSize - 1, file);
    text[readSize] = '\0';
    fclose(file);
    return true;
}

static const char *GetReferenceFileName(const char *filePath)
{
    const char *backslash = strrchr(filePath, '\\');
    const char *slash = strrchr(filePath, '/');
    const char *fileName = backslash != NULL ? backslash + 1 : filePath;
    if (slash != NULL && slash + 1 > fileName) fileName = slash + 1;
    return fileName[0] != '\0' ? fileName : "FILE.txt";
}

static void RegisterReferenceFileNames(const RpgStage *stage)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (stage->blocks[row][column] == RPG_BLOCK_REFERENCE_FILE)
            GameFont_AddText(GetReferenceFileName(RpgStage_GetReferencePathAtCell(stage, row, column)));
    }
}

static void OpenTextFile(const char *path, char *fileName, size_t fileNameSize,
                         char *text, size_t textSize, bool *isOpen)
{
    snprintf(fileName, fileNameSize, "%s", GetReferenceFileName(path));
    if (path[0] != '\0' && LoadReferenceText(path, text, textSize)) {
        GameFont_AddText(text);
    } else {
        snprintf(text, textSize, "Text file is not assigned or cannot be read.");
    }
    GameFont_AddText(fileName);
    *isOpen = true;
}

// File.png の配置元にかかわらず、選択表示とドラッグ表示を同じ矩形で扱う。
static Rectangle GetReferenceTargetBounds(const RpgReferenceObjects *objects, RpgReferenceTarget target)
{
    if (target.kind == RPG_REFERENCE_TARGET_CELL)
        return (Rectangle){ target.column * RPG_STAGE_TILE_SIZE, target.row * RPG_STAGE_TILE_SIZE,
                            RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
    if (target.kind == RPG_REFERENCE_TARGET_DROP && target.dropIndex >= 0 && target.dropIndex < objects->count) {
        Vector2 position = objects->entries[target.dropIndex].position;
        return (Rectangle){ position.x - 24.0f, position.y - 24.0f, 48.0f, 48.0f };
    }
    return (Rectangle){ 0 };
}

static void DrawReferenceTextPanel(const char *fileName, const char *text)
{
    Rectangle panel = { 150.0f, 110.0f, 660.0f, 190.0f };
    DrawRectangleRec(panel, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(panel, 2.0f, DARKBLUE);
    GameFont_Draw(fileName, 172.0f, 122.0f, 20.0f, DARKBLUE);
    int textOffset = 0;
    for (int row = 0; row < 7 && text[textOffset] != '\0'; row++) {
        char line[256] = { 0 };
        int length = 0;
        while (text[textOffset] != '\0' && text[textOffset] != '\n' && length < (int)sizeof(line) - 1)
            line[length++] = text[textOffset++];
        if (text[textOffset] == '\n') textOffset++;
        GameFont_Draw(line, 172.0f, 154.0f + row * 20.0f, 18.0f, DARKGRAY);
    }
    DrawRectangleRec(referenceTextCloseButton, Fade(DARKBLUE, 0.9f));
    DrawRectangleLinesEx(referenceTextCloseButton, 1.0f, RAYWHITE);
    GameFont_Draw("E: 閉じる", 674.0f, 267.0f, 16.0f, RAYWHITE);
}

/* Zipperへの取り込み成功は、cmd経由・FILE.pngドラッグ経由を問わず同じアニメーションで示す。 */
static void StartZipperImportAnimation(float *animationElapsed)
{
    if (animationElapsed != NULL) *animationElapsed = 0.0f;
}

static Texture2D GetFolderReturnIcon(void)
{
    if (!hasLoadedFolderReturnIcon) {
        folderReturnIcon = RpgExplorerShell_LoadFolderIconTexture();
        hasLoadedFolderReturnIcon = true;
    }
    return folderReturnIcon;
}

static Vector2 GetHeldObjectReturnDestination(const RpgRuntimeContext *context)
{
    const RpgZipperHeldObject *held = &(*context->zipper).heldObject;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_DATA_SHOT && held->dataShotIndex >= 0 &&
        held->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT)
        return (*context->dataShots).entries[held->dataShotIndex].position;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_ATTACHMENT && held->attachmentIndex >= 0 &&
        held->attachmentIndex < (*context->attachments).count)
        return RpgAttachments_GetPosition(&(*context->attachments).entries[held->attachmentIndex], 0);
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK)
        return (Vector2){ (held->blockCell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                          (held->blockCell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    return (*context->zipper).character.position;
}

static void StartZipperFolderReturnVisual(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    zipper->isFolderReturnAnimating = true;
    zipper->folderReturnElapsed = 0.0f;
    zipper->folderReturnDuration = context->layout != NULL ?
        Clamp((*context->layout).zipperFolderReturnDuration, 0.10f, 5.0f) : 0.45f;
    zipper->folderReturnStart = zipper->character.position;
    zipper->folderReturnDestination = GetHeldObjectReturnDestination(context);
}

/* 1マス占有アタッチメントのフォルダがInboxへ移動した間は、対応マスを赤い壁として扱う。 */
static void SetHeldAttachmentCellError(RpgStage *stage, RpgAttachment *attachment, bool isMissing)
{
    RpgGridCell occupiedCell;
    if (stage == NULL || attachment == NULL || !RpgAttachments_GetOccupiedCell(attachment, &occupiedCell)) return;
    if (isMissing) stage->blocks[occupiedCell.row][occupiedCell.column] = RPG_BLOCK_BUILD_MISSING;
    else if (stage->blocks[occupiedCell.row][occupiedCell.column] == RPG_BLOCK_BUILD_MISSING)
        stage->blocks[occupiedCell.row][occupiedCell.column] = 0;
}

/* 返却開始時は Inbox から StageN へ移すだけにする。演出中はフォルダを build に戻さない。 */
static bool BeginZipperHeldObjectReturn(RpgRuntimeContext *context)
{
    RpgZipperHeldObject *held = &(*context->zipper).heldObject;
    bool returned = false;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_NONE) return true;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_DATA_SHOT && held->dataShotIndex >= 0 &&
        held->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT)
        returned = RpgObjectFolder_BeginReturnDataShotFromZipper(&(*context->dataShots).entries[held->dataShotIndex]);
    else if (held->kind == RPG_ZIPPER_HELD_OBJECT_ATTACHMENT && held->attachmentIndex >= 0 &&
             held->attachmentIndex < (*context->attachments).count) {
        RpgAttachment *attachment = &(*context->attachments).entries[held->attachmentIndex];
        returned = RpgObjectFolder_BeginReturnAttachmentFromZipper(attachment);
    }
    else if (held->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK && held->blockCell.row >= 0 && held->blockCell.column >= 0) {
        RpgObjectFolder folder = { .cell = held->blockCell };
        returned = RpgObjectFolder_BeginReturnBlockFromZipper(&folder, held->blockType);
    }
    return returned;
}

/* 演出完了時にだけ StageN の待機フォルダを build へ確定し、対応するゲーム状態を復帰する。 */
static bool CompleteZipperHeldObjectReturn(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    RpgZipperHeldObject *held = &zipper->returningObject;
    bool returned = false;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_NONE) return true;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_DATA_SHOT && held->dataShotIndex >= 0 &&
        held->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT) {
        returned = RpgObjectFolder_ReturnDataShotFromZipper(&(*context->dataShots).entries[held->dataShotIndex]);
        if (returned) {
            RpgDataShot *shot = &(*context->dataShots).entries[held->dataShotIndex];
            (void)RpgObjectFolder_RestoreDataShotFromMetadata(shot);
            shot->isZipperHeld = false;
        }
    }
    else if (held->kind == RPG_ZIPPER_HELD_OBJECT_ATTACHMENT && held->attachmentIndex >= 0 &&
             held->attachmentIndex < (*context->attachments).count) {
        RpgAttachment *attachment = &(*context->attachments).entries[held->attachmentIndex];
        returned = RpgObjectFolder_ReturnAttachmentFromZipper(attachment);
        if (returned) {
            attachment->isZipperHeld = false;
            SetHeldAttachmentCellError(context->stage, attachment, false);
        }
    } else if (held->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK && held->blockCell.row >= 0 && held->blockCell.column >= 0) {
        RpgObjectFolder folder = { .cell = held->blockCell };
        returned = RpgObjectFolder_ReturnBlockFromZipper(&folder, held->blockType);
    }
    if (returned)
        *held = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                       .blockCell = { -1, -1 }, .attachmentIndex = -1, .dataShotIndex = -1 };
    if (returned) zipper->isFolderReturnCommitPending = false;
    return returned;
}

static bool CaptureCurrentZipperObject(RpgRuntimeContext *context)
{
    RpgZipperHeldObject held = { .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                 .blockCell = { -1, -1 }, .attachmentIndex = -1, .dataShotIndex = -1 };
    bool captured = false;
    if ((*context->attachedDataShotIndex) >= 0 && (*context->attachedDataShotIndex) < RPG_DATA_SHOT_MAX_COUNT &&
        (*context->dataShots).entries[(*context->attachedDataShotIndex)].active) {
        captured = RpgObjectFolder_MoveDataShotToZipper(&(*context->dataShots).entries[(*context->attachedDataShotIndex)]);
        if (captured) (*context->dataShots).entries[(*context->attachedDataShotIndex)].isZipperHeld = true;
        held.kind = RPG_ZIPPER_HELD_OBJECT_DATA_SHOT;
        held.dataShotIndex = (*context->attachedDataShotIndex);
    } else if ((*context->isZipperAttachedToBlock) && (*context->zipperAttachedBlockCell).row >= 0 &&
               (*context->zipperAttachedBlockCell).column >= 0) {
        if ((*context->attachedAttachmentIndex) >= 0 && (*context->attachedAttachmentIndex) < (*context->attachments).count) {
            RpgAttachment *attachment = &(*context->attachments).entries[(*context->attachedAttachmentIndex)];
            captured = RpgObjectFolder_MoveAttachmentToZipper(attachment);
            if (captured) {
                attachment->isZipperHeld = true;
                SetHeldAttachmentCellError(context->stage, attachment, true);
            }
            held.kind = RPG_ZIPPER_HELD_OBJECT_ATTACHMENT;
            held.attachmentIndex = (*context->attachedAttachmentIndex);
        } else {
            RpgObjectFolder blockFolder = { .cell = (*context->zipperAttachedBlockCell) };
            held.kind = RPG_ZIPPER_HELD_OBJECT_BLOCK;
            held.blockCell = (*context->zipperAttachedBlockCell);
            held.blockType = (*context->stage).blocks[held.blockCell.row][held.blockCell.column];
            captured = RpgObjectFolder_MoveBlockToZipper(&blockFolder, held.blockType);
        }
    }
    if (!captured) return false;
    (*context->zipper).heldObject = held;
    (*context->zipperFollowsPlayer) = true;
    (*context->isZipperLaunched) = false;
    (*context->attachedDataShotIndex) = -1;
    (*context->attachedAttachmentIndex) = -1;
    (*context->isZipperAttachedToBlock) = false;
    (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
    return true;
}

/* cmdのゲーム機能。返却フォルダがない時、または返却演出が完了した時に次の対象を取得する。 */
static void CaptureZipperCommandTarget(RpgRuntimeContext *context)
{
    if (CaptureCurrentZipperObject(context)) return;
    /* 取得対象がなくても、cmd完了後のZipperは必ず帰還状態に統一する。 */
    (*context->zipperFollowsPlayer) = true;
    (*context->isZipperLaunched) = false;
    (*context->attachedDataShotIndex) = -1;
    (*context->attachedAttachmentIndex) = -1;
    (*context->isZipperAttachedToBlock) = false;
    (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
}

static void RunZipperCommandFunction(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    if (zipper->heldObject.kind != RPG_ZIPPER_HELD_OBJECT_NONE) {
        StartZipperFolderReturnVisual(context);
        if (BeginZipperHeldObjectReturn(context)) {
            /* 返却開始が完了すれば、演出中でも次の対象取得と追従復帰は待たせない。 */
            zipper->returningObject = zipper->heldObject;
            zipper->heldObject = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                        .blockCell = { -1, -1 },
                                                        .attachmentIndex = -1, .dataShotIndex = -1 };
            zipper->isFolderReturnCommitPending = true;
            CaptureZipperCommandTarget(context);
        } else {
            zipper->isFolderReturnAnimating = false;
            zipper->folderReturnElapsed = 0.0f;
        }
        return;
    }
    CaptureZipperCommandTarget(context);
}

/* cmd起動時は演出だけを始め、ファイル操作はアニメーション終了まで待機する。 */
static void StartZipperCommand(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    StartZipperImportAnimation(context->zipperAnimationElapsed);
    (void)RpgObjectFolder_CompleteZipperCommandRequest();
    /* cmd入力と同時に旧フォルダを待機場所へ移し、演出後に次の取り込みを確定する。 */
    if (zipper->heldObject.kind != RPG_ZIPPER_HELD_OBJECT_NONE) {
        StartZipperFolderReturnVisual(context);
        if (BeginZipperHeldObjectReturn(context)) {
            zipper->returningObject = zipper->heldObject;
            zipper->heldObject = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                        .blockCell = { -1, -1 },
                                                        .attachmentIndex = -1, .dataShotIndex = -1 };
            zipper->isFolderReturnCommitPending = true;
        } else {
            zipper->isFolderReturnAnimating = false;
            zipper->folderReturnElapsed = 0.0f;
        }
    }
    zipper->isFolderReturnPending = true;
}

void RpgRuntime_ProcessZipperCommand(RpgRuntimeContext *context)
{
    if (context == NULL || context->zipperAnimationElapsed == NULL ||
        !RpgObjectFolder_BeginZipperCommandRequest()) return;
    /* 実行中のcmdは一つだけにして、状態に関係なく同じ完了手順へ送る。 */
    if ((*context->zipper).isFolderReturnPending || (*context->zipper).isFolderReturnAnimating ||
        (*context->zipper).isFolderReturnCommitPending) {
        (void)RpgObjectFolder_CompleteZipperCommandRequest();
        return;
    }
    StartZipperCommand(context);
}

void RpgRuntime_UpdateZipperFolderReturn(RpgRuntimeContext *context, float deltaTime)
{
    RpgZipper *zipper;
    if (context == NULL || context->zipper == NULL || context->zipperAnimationElapsed == NULL) return;
    zipper = context->zipper;
    /* 取り込みアニメーション完了後、返却対象を StageN へ待機させて返却演出を始める。 */
    /* 返却フォルダの実移動・演出は独立しているため、取り込み演出が終われば待たずに帰還・次の取得へ進む。 */
    if (zipper->isFolderReturnPending && *context->zipperAnimationElapsed < 0.0f) {
        zipper->isFolderReturnPending = false;
        RunZipperCommandFunction(context);
    }
    if (!zipper->isFolderReturnAnimating) return;
    zipper->folderReturnElapsed += deltaTime;
    if (zipper->folderReturnElapsed >= zipper->folderReturnDuration) {
        zipper->isFolderReturnAnimating = false;
        zipper->folderReturnElapsed = 0.0f;
        if (zipper->isFolderReturnCommitPending)
            (void)CompleteZipperHeldObjectReturn(context);
    }
}

void RpgRuntime_DrawZipperFolderReturn(const RpgZipper *zipper)
{
    Texture2D icon;
    float progress, eased, size;
    Vector2 position;
    Rectangle destination;
    if (zipper == NULL || !zipper->isFolderReturnAnimating) return;
    progress = Clamp(zipper->folderReturnElapsed / Clamp(zipper->folderReturnDuration, 0.10f, 5.0f), 0.0f, 1.0f);
    eased = progress * progress * (3.0f - 2.0f * progress);
    position = Vector2Lerp(zipper->folderReturnStart, zipper->folderReturnDestination, eased);
    size = 24.0f - 7.0f * progress;
    destination = (Rectangle){ position.x - size * 0.5f, position.y - size * 0.5f, size, size };
    icon = GetFolderReturnIcon();
    if (icon.id != 0)
        DrawTexturePro(icon, (Rectangle){ 0, 0, (float)icon.width, (float)icon.height }, destination,
                       (Vector2){ 0, 0 }, 0.0f, Fade(WHITE, 1.0f - progress * 0.20f));
    else {
        DrawRectangleRounded(destination, 0.16f, 4, Fade(GOLD, 1.0f - progress * 0.20f));
        DrawRectangleLinesEx(destination, 1.0f, ORANGE);
    }
}

static void DrawZipper(Texture2D zipperTexture, const RpgCharacter *zipper, float animationElapsed)
{
    int frameCount = zipperTexture.width / 32;
    int frameIndex = animationElapsed >= 0.0f && frameCount > 1 ?
        (int)Clamp(animationElapsed / zipperImportAnimationDuration * frameCount, 0.0f, (float)(frameCount - 1)) : 0;
    Rectangle source = { frameIndex * 32.0f, 0.0f, 32.0f, 40.0f };
    Rectangle destination = RpgZipper_GetSpriteBounds(zipper, 380.0f);
    DrawRectangleRounded((Rectangle){ destination.x - 3.0f, destination.y - 3.0f,
                                      destination.width + 6.0f, destination.height + 6.0f },
                         0.18f, 4, Fade(DARKBLUE, 0.28f));
    DrawRectangleLinesEx((Rectangle){ destination.x - 3.0f, destination.y - 3.0f,
                                      destination.width + 6.0f, destination.height + 6.0f },
                         1.0f, Fade(SKYBLUE, 0.85f));
    DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

static void DrawMoveSprite(Texture2D zipperTexture, const RpgCharacter *player, const RpgCharacter *npc,
                           const RpgZipper *zipper, RpgInspectMoveTarget target, float x)
{
    if (target == RPG_INSPECT_MOVE_ZIPPER) {
        Rectangle source = { 0, 0, 32, 40 };
        Rectangle destination = { x - 24.0f * zipper->character.scale, 340.0f - 60.0f * zipper->character.scale,
                                  48.0f * zipper->character.scale, 60.0f * zipper->character.scale };
        DrawTexturePro(zipperTexture, source, destination, (Vector2){0}, 0, Fade(WHITE, 0.75f));
    } else {
        RpgCharacter sprite = target == RPG_INSPECT_MOVE_PLAYER ? *player : *npc;
        sprite.position = (Vector2){ x, 400.0f };
        RpgCharacter_Draw(&sprite, "");
    }
}

static void UpdateRpgCamera(Camera2D *camera, float playerX, bool followsPlayer)
{
    if (followsPlayer) {
        camera->target.x = playerX;
        if (camera->target.x < RPG_SCREEN_WIDTH / 2.0f) camera->target.x = RPG_SCREEN_WIDTH / 2.0f;
        if (camera->target.x > RPG_STAGE_WORLD_WIDTH - RPG_SCREEN_WIDTH / 2.0f)
            camera->target.x = RPG_STAGE_WORLD_WIDTH - RPG_SCREEN_WIDTH / 2.0f;
    } else {
        int mapIndex = (int)(playerX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
        if (mapIndex >= RPG_STAGE_MAP_COUNT) mapIndex = RPG_STAGE_MAP_COUNT - 1;
        camera->target.x = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE +
                           RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f;
    }
    camera->target.y = RPG_SCREEN_HEIGHT / 2.0f;
}

static void UpdateZipperFollow(RpgZipper *zipper, const RpgCharacter *player, float deltaTime)
{
    // 帰還時は主人公の少し後ろ・同じ足元へ、X/Yをまとめて滑らかに追従させる。
    Vector2 target = { player->position.x - 48.0f * player->scale, player->position.y };
    Vector2 distance = Vector2Subtract(target, zipper->character.position);
    float maximumStep = zipper->followSpeed * deltaTime;
    float distanceLength = Vector2Length(distance);
    if (distanceLength <= maximumStep) zipper->character.position = target;
    else zipper->character.position = Vector2Add(zipper->character.position,
                                                  Vector2Scale(distance, maximumStep / distanceLength));
}

static Rectangle GetZipperCollisionBounds(const RpgZipper *zipper)
{
    return RpgCharacter_GetCollisionBounds(&zipper->character);
}

static Vector2 GetZipperCollisionCenter(const RpgZipper *zipper)
{
    Rectangle bounds = GetZipperCollisionBounds(zipper);
    return (Vector2){ bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
}

static void MoveZipperCollisionCenterTo(RpgZipper *zipper, Vector2 targetCenter)
{
    // Zipperの座標は足元基準のため、当たり判定の中心を対象中心へ合わせて見た目と判定の上下ずれを防ぐ。
    Vector2 currentCenter = GetZipperCollisionCenter(zipper);
    zipper->character.position = Vector2Add(zipper->character.position,
                                             Vector2Subtract(targetCenter, currentCenter));
}

static bool GetStageCellAtCenter(Vector2 center, RpgGridCell *cell)
{
    int row = (int)floorf(center.y / RPG_STAGE_TILE_SIZE);
    int column = (int)floorf(center.x / RPG_STAGE_TILE_SIZE);
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    *cell = (RpgGridCell){ row, column };
    return true;
}

static Rectangle GetZipperForwardCollisionBounds(Rectangle bounds, Vector2 velocity)
{
    // 射出方向の前半面を判定面にし、背面で重なった物体には衝突・追従しないようにする。
    if (fabsf(velocity.x) >= fabsf(velocity.y)) {
        bounds.width *= 0.5f;
        if (velocity.x >= 0.0f) bounds.x += bounds.width;
    } else {
        bounds.height *= 0.5f;
        if (velocity.y >= 0.0f) bounds.y += bounds.height;
    }
    return bounds;
}

static bool DoesZipperHitReferenceFile(const RpgStage *stage, Rectangle bounds, Vector2 *center)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        if (CheckCollisionRecs(bounds, cell)) {
            *center = (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f };
            return true;
        }
    }
    return false;
}

static bool DoesZipperHitAttachment(const RpgAttachments *attachments, Rectangle bounds, Vector2 *center,
                                    RpgGridCell *attachmentCell, int *attachmentIndex)
{
    for (int index = 0; index < attachments->count; index++) {
        if (attachments->entries[index].isZipperHeld) continue;
        Vector2 position = RpgAttachments_GetPosition(&attachments->entries[index], 0);
        Rectangle attachmentBounds = { position.x - 22.0f, position.y - 22.0f, 44.0f, 44.0f };
        if (CheckCollisionRecs(bounds, attachmentBounds)) {
            *center = position;
            *attachmentCell = attachments->entries[index].cell;
            *attachmentIndex = index;
            return true;
        }
    }
    return false;
}

static int FindDataShotHit(const RpgDataShots *shots, Rectangle bounds)
{
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        const RpgDataShot *shot = &shots->entries[index];
        if (shot->active && CheckCollisionCircleRec(shot->position, shot->size, bounds)) return index;
    }
    return -1;
}

static void UpdateLaunchedZipper(RpgZipper *zipper, Vector2 *velocity, const RpgStage *stage,
                                 const RpgAttachments *attachments, const RpgDataShots *shots,
                                 float deltaTime, bool *isLaunched, int *attachedDataShotIndex,
                                 Vector2 *attachedDataShotOffset,
                                 bool *isAttachedToBlock, RpgGridCell *attachedBlockCell,
                                 int *attachedAttachmentIndex)
{
    // 高速設定でも壁をすり抜けないよう、移動を小さな単位に分けて衝突を確認する。
    float distance = Vector2Length(*velocity) * deltaTime;
    int stepCount = (int)ceilf(distance / 4.0f);
    if (stepCount < 1) stepCount = 1;
    Vector2 step = Vector2Scale(*velocity, deltaTime / (float)stepCount);
    for (int index = 0; index < stepCount; index++) {
        RpgZipper candidate = *zipper;
        candidate.character.position = Vector2Add(candidate.character.position, step);
        Rectangle collisionBounds = GetZipperCollisionBounds(&candidate);
        Rectangle forwardCollisionBounds = GetZipperForwardCollisionBounds(collisionBounds, *velocity);
        int dataShotIndex = FindDataShotHit(shots, forwardCollisionBounds);
        if (dataShotIndex >= 0) {
            // 動くデータ弾には相対位置を記録してくっつき、弾が消えるまで一緒に動かす。
            MoveZipperCollisionCenterTo(&candidate, shots->entries[dataShotIndex].position);
            *attachedDataShotIndex = dataShotIndex;
            *attachedDataShotOffset = Vector2Subtract(candidate.character.position,
                                                       shots->entries[dataShotIndex].position);
            *isLaunched = false;
            *isAttachedToBlock = false;
            *attachedAttachmentIndex = -1;
            *zipper = candidate;
            return;
        }
        Vector2 collisionCenter;
        RpgGridCell attachmentCell = { -1, -1 };
        int hitAttachmentIndex = -1;
        bool hitReferenceFile = DoesZipperHitReferenceFile(stage, forwardCollisionBounds, &collisionCenter);
        bool hitAttachment = !hitReferenceFile &&
            DoesZipperHitAttachment(attachments, forwardCollisionBounds, &collisionCenter, &attachmentCell,
                                     &hitAttachmentIndex);
        if (hitReferenceFile || hitAttachment) {
            MoveZipperCollisionCenterTo(&candidate, collisionCenter);
            *isLaunched = false;
            *isAttachedToBlock = true;
            if (hitAttachment) {
                *attachedBlockCell = attachmentCell;
                *attachedAttachmentIndex = hitAttachmentIndex;
            } else {
                GetStageCellAtCenter(collisionCenter, attachedBlockCell);
                *attachedAttachmentIndex = -1;
            }
            *zipper = candidate;
            return;
        }
        bool hitWorldEdge = forwardCollisionBounds.x < 0.0f ||
            forwardCollisionBounds.x + forwardCollisionBounds.width > RPG_STAGE_WORLD_WIDTH ||
            forwardCollisionBounds.y < 0.0f ||
            forwardCollisionBounds.y + forwardCollisionBounds.height > RPG_SCREEN_HEIGHT;
        if (hitWorldEdge || RpgStage_FindSolidCollisionCenter(stage, forwardCollisionBounds, &collisionCenter)) {
            if (!hitWorldEdge) {
                MoveZipperCollisionCenterTo(&candidate, collisionCenter);
                GetStageCellAtCenter(collisionCenter, attachedBlockCell);
            }
            *velocity = (Vector2){ 0.0f, 0.0f };
            *isLaunched = false;
            *isAttachedToBlock = true;
            *attachedAttachmentIndex = -1;
            *zipper = candidate;
            return;
        }
        *zipper = candidate;
    }
}

static void UpdateZipperAttachedToDataShot(RpgZipper *zipper, const RpgDataShots *shots,
                                           int *attachedDataShotIndex,
                                           Vector2 attachedDataShotOffset,
                                           bool *zipperFollowsPlayer, bool *isAttachedToBlock,
                                           RpgGridCell *attachedBlockCell)
{
    if (*attachedDataShotIndex < 0) return;
    const RpgDataShot *shot = &shots->entries[*attachedDataShotIndex];
    // 電気化した時点で弾本体は壁衝突として消費済みなので、Zipper は追従へ戻す。
    if (shot->isElectric) {
        *zipperFollowsPlayer = true;
        *isAttachedToBlock = false;
        *attachedDataShotIndex = -1;
        return;
    }
    if (shot->active) {
        zipper->character.position = Vector2Add(shot->position, attachedDataShotOffset);
        return;
    }
    // 弾が壁へ当たって消えた場合は、その衝突位置へ残す。空中消滅なら主人公へ戻す。
    if (shot->hitWall) {
        MoveZipperCollisionCenterTo(zipper, shot->impactPosition);
        GetStageCellAtCenter(shot->impactPosition, attachedBlockCell);
        *isAttachedToBlock = true;
    } else {
        *zipperFollowsPlayer = true;
        *isAttachedToBlock = false;
    }
    *attachedDataShotIndex = -1;
}

static void DrawRpgWorld(const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgStage *stage,
                         Camera2D camera, bool followsPlayer,
                         int currentMap, bool canTalk, const RpgDialogue *dialogue,
                         int dialogueIndex, int stage3IntroIndex, const RpgStage3Event *stage3Event, const RpgZipper *zipper,
                         Texture2D zipperTexture, Texture2D fileTexture, const RpgInspect *inspect, const RpgInspect *zipperInspect,
                         int inspectTarget, int inspectFunctionIndex, int inspectLineIndex,
                          bool isMoveSpriteVisible, float moveSpriteX, RpgInspectMoveTarget moveSpriteTarget,
                          bool npcInspectCompleted, bool zipperInspectCompleted,
                          bool isZipperPointerHovered, bool isZipperPointerSelected,
                          const RpgItems *items,
                          const RpgReferenceObjects *referenceDrops,
                          const RpgWires *wires,
                          const RpgReceivers *receivers,
                          const RpgAttachments *attachments,
                          const RpgDataShots *dataShots,
                          const RpgMapEvents *events, const char *itemMessage, float itemMessageTimer,
                          RpgReferenceTarget nearbyReferenceTarget,
                          const char *referenceFileName, const char *referenceText, bool isReferenceTextOpen,
                          RpgReferenceTarget hoveredReferenceTarget,
                          RpgReferenceTarget selectedReferenceTarget,
                          bool isReferencePointerFeedbackSuppressed,
                          bool isReferenceDragActive, RpgReferenceTarget draggedReferenceTarget,
                          Vector2 referenceDragPosition,
                          float zipperAnimationElapsed, bool showStopButton, RpgSceneState *scene)
{
    (void)npcInspectCompleted;
    BeginDrawing();
    ClearBackground((Color){ 135, 206, 235, 255 });
    DrawCircle(780, 95, 42, Fade(YELLOW, 0.9f));
    DrawEllipse(180, 105, 80, 20, Fade(RAYWHITE, 0.85f));
    DrawEllipse(510, 160, 110, 24, Fade(RAYWHITE, 0.8f));
    BeginMode2D(camera);
    DrawRectangle(0, 400, RPG_STAGE_WORLD_WIDTH, 140, (Color){ 103, 161, 70, 255 });
    RpgStage_Draw(stage, false);
    // 通常ブロックへ紐づけたファイルは、そのブロック自体を強調する。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgObjectFolder objectFolder = { .cell = { row, column } };
        if (!RpgObjectFolder_BlockHasLinkedFiles(&objectFolder, stage->blocks[row][column])) continue;
        Rectangle objectBounds = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                   RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        DrawRectangleRec(objectBounds, Fade(GOLD, 0.30f));
        DrawRectangleLinesEx(objectBounds, 2.0f, ORANGE);
    }
    // ファイルを紐づけた設置物だけを、実際の描画位置に合わせて強調する。
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (attachment->isZipperHeld) continue;
        if (!RpgObjectFolder_AttachmentHasLinkedFiles(attachment)) continue;
        Vector2 position = RpgAttachments_GetPosition(attachment, 0);
        DrawCircleV(position, 27.0f, Fade(GOLD, 0.30f));
        DrawCircleLines((int)position.x, (int)position.y, 27.0f, ORANGE);
    }
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        const RpgDataShot *shot = &dataShots->entries[index];
        if (!shot->active || !RpgObjectFolder_DataShotHasLinkedFiles(shot)) continue;
        DrawCircleV(shot->position, shot->size + 8.0f, Fade(GOLD, 0.30f));
        DrawCircleLines((int)shot->position.x, (int)shot->position.y, shot->size + 8.0f, ORANGE);
    }
    RpgStage_DrawReferenceObjectsExcept(stage, fileTexture,
                                        isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_CELL ?
                                            draggedReferenceTarget.row : -1,
                                        isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_CELL ?
                                            draggedReferenceTarget.column : -1);
    RpgReferenceObjects_DrawExcept(referenceDrops, fileTexture,
                                   isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_DROP ?
                                       draggedReferenceTarget.dropIndex : -1);
    if (isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_CELL)
        RpgStage_DrawReferenceObject(fileTexture, GetReferenceTargetBounds(referenceDrops, draggedReferenceTarget),
                                     Fade(WHITE, 0.28f));
    RpgItems_Draw(items);
    RpgMapEvents_Draw(events);
    DrawRectangle(0, 400, RPG_STAGE_WORLD_WIDTH, 14, DARKGREEN);
    RpgStage_DrawEffects(stage);
    RpgWires_Draw(wires, stage);
    RpgWires_DrawElectric(wires, dataShots, 0, RPG_STAGE_WORLD_COLUMNS);
    RpgReceivers_Draw(receivers);
    RpgAttachments_Draw(attachments);
    RpgDataShots_Draw(dataShots);
    DrawZipper(zipperTexture, &zipper->character, zipperAnimationElapsed);
    RpgRuntime_DrawZipperFolderReturn(zipper);
    if (isMoveSpriteVisible) DrawMoveSprite(zipperTexture, player, npc, zipper, moveSpriteTarget, moveSpriteX);
    RpgCharacter_Draw(npc, "NPC");
    RpgCharacter_Draw(player, "Hero");
    RpgZipper_DrawPointerFeedback(RpgZipper_GetSpriteBounds(&zipper->character, 380.0f),
                                  isZipperPointerHovered, isZipperPointerSelected);
    if (!isReferencePointerFeedbackSuppressed && selectedReferenceTarget.kind != RPG_REFERENCE_TARGET_NONE) {
        Rectangle selectedBounds = GetReferenceTargetBounds(referenceDrops, selectedReferenceTarget);
        RpgZipper_DrawPointerFeedback(selectedBounds, false, true);
    }
    if (!isReferencePointerFeedbackSuppressed && hoveredReferenceTarget.kind != RPG_REFERENCE_TARGET_NONE) {
        Rectangle hoveredBounds = GetReferenceTargetBounds(referenceDrops, hoveredReferenceTarget);
        bool isSelected = hoveredReferenceTarget.kind == selectedReferenceTarget.kind &&
            hoveredReferenceTarget.row == selectedReferenceTarget.row &&
            hoveredReferenceTarget.column == selectedReferenceTarget.column &&
            hoveredReferenceTarget.dropIndex == selectedReferenceTarget.dropIndex;
        RpgZipper_DrawPointerFeedback(hoveredBounds, true, isSelected);
    }
    if (isReferenceDragActive)
        RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ referenceDragPosition.x - 24.0f,
                                                                referenceDragPosition.y - 24.0f,
                                                                RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 48, (int)npc->position.y - 116, 96, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw("[E] 話しかける", npc->position.x - 48, npc->position.y - 112, 16, MAROON);
    }
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 58, (int)npc->position.y - 116, 116, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw(npcTalkPrompt, npc->position.x - 54, npc->position.y - 112, 16, MAROON);
    }
    if (RpgCharacter_IsNear(player, &zipper->character, 72.0f) && inspectTarget < 0 &&
        zipperInspect->enabled && !zipperInspectCompleted) {
        GameFont_Draw(zipperInspectPrompt, zipper->character.position.x - 44.0f, 270.0f, 16, MAROON);
    }
    if (nearbyReferenceTarget.kind != RPG_REFERENCE_TARGET_NONE && !isReferenceTextOpen) {
        Rectangle referenceBounds = GetReferenceTargetBounds(referenceDrops, nearbyReferenceTarget);
        float x = referenceBounds.x + referenceBounds.width * 0.5f;
        float y = referenceBounds.y - 38.0f;
        float fileNameWidth = GameFont_MeasureText(referenceFileName, 16.0f).x;
        DrawRectangle((int)(x - fileNameWidth / 2.0f - 6.0f), (int)y, (int)fileNameWidth + 12, 44,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw(referenceFileName, x - fileNameWidth / 2.0f, y + 3.0f, 16.0f, DARKBLUE);
        GameFont_Draw("[E] 開く", x - 24.0f, y + 22.0f, 15.0f, MAROON);
    }
    EndMode2D();
    DrawText("RPG Version  -  Move: A/D or Arrow keys  Jump: W  Launch Zipper: Space", 24, 22, 22, DARKGRAY);
    DrawText("Approach the NPC and press E", 24, 48, 18, DARKGRAY);
    DrawText(followsPlayer ? "Camera: Follow [C]" : "Camera: Map Pivot [C]", 700, 48, 17,
             DARKBLUE);
    DrawText(TextFormat("Area (%d, %d)", stage->mapGridX[currentMap], stage->mapGridY[currentMap]),
             24, 500, 20, RAYWHITE);
    if (itemMessageTimer > 0.0f) GameFont_Draw(itemMessage, 300, 90, 24, MAROON);
    if (isReferenceTextOpen) DrawReferenceTextPanel(referenceFileName, referenceText);
    const RpgInspect *activeInspect = inspectTarget == 2 ? zipperInspect : inspect;
    bool isInspectDialogue = inspectTarget >= 0 &&
                             activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_DIALOGUE;
    if (dialogueIndex >= 0 || stage3IntroIndex >= 0 || isInspectDialogue) {
        const Rectangle dialogBounds = { 150.0f, 350.0f, 660.0f, 130.0f };
        const Rectangle speakerBounds = { 174.0f, 332.0f, 150.0f, 36.0f };
        DrawRectangleRec(dialogBounds, Fade(RAYWHITE, 0.96f));
        DrawRectangleLinesEx(dialogBounds, 2.0f, DARKBLUE);
        DrawRectangleRec(speakerBounds, DARKBLUE);
        const RpgDialogue *inspectDialogue = isInspectDialogue ? &activeInspect->functions[inspectFunctionIndex].dialogue : NULL;
        const char *speaker = inspectDialogue != NULL ? inspectDialogue->speakers[inspectLineIndex] : stage3IntroIndex >= 0 ? stage3Event->dialogue.speakers[stage3IntroIndex] : dialogue->speakers[dialogueIndex];
        const char *text = inspectDialogue != NULL ? inspectDialogue->lines[inspectLineIndex] : stage3IntroIndex >= 0 ? stage3Event->dialogue.lines[stage3IntroIndex] : dialogue->lines[dialogueIndex];
        GameFont_Draw(speaker, 190, 340, 21, RAYWHITE);
        GameFont_Draw(text, 178, 390, 24, DARKBLUE);
        GameFont_Draw(inspectDialogue != NULL ? TextFormat("E: next  function %d / %d, line %d / %d", inspectFunctionIndex + 1, activeInspect->functionCount, inspectLineIndex + 1, inspectDialogue->lineCount) : stage3IntroIndex >= 0 ? TextFormat("E: 次へ  %d / %d", stage3IntroIndex + 1, stage3Event->dialogue.lineCount) : TextFormat("E: 次へ  %d / %d", dialogueIndex + 1, dialogue->lineCount),
                      178, 432, 17, GRAY);
    }
    if (showStopButton) {
        DrawRectangle(198, 10, 92, 26, MAROON);
        DrawRectangleLines(198, 10, 92, 26, RAYWHITE);
        DrawText("Stop [F2]", 205, 16, 15, RAYWHITE);
    }
    if (RpgScene_IsGameSettings(scene)) RpgScene_DrawGameSettingsOverlay(scene);
    else if (scene != NULL) RpgScene_DrawGameSettingsButton();
    EndDrawing();
}


void RpgRuntime_UpdateAndDraw(RpgRuntimeContext *context)
{
    if (context == NULL) return;
    // 設定シーンでは本編の入力・物理・描画更新を実行せず、シーンUIだけを処理する。
    if (context->scene != NULL && RpgScene_IsGameSettings(context->scene)) {
        RpgScene_UpdateGameSettings(context->scene);
        // 戻る先が本編でもタイトルでも、このフレームで遷移先を必ず描画する。
        RpgScene_UpdateAndDraw(context->scene);
        return;
    }
    if (context->scene != NULL && RpgScene_TryOpenGameSettings(context->scene)) {
        RpgScene_UpdateAndDraw(context->scene);
        return;
    }
    (void)RegisterReferenceFileNames;
#define zipperPointerSelected (*context->zipperPointerSelected)
#define itemMessage (context->itemMessage)
#define zipperTexture (context->zipperTexture)
#define fileTexture (context->fileTexture)
    {
        if (IsKeyPressed(KEY_C)) (*context->cameraFollowsPlayer) = !(*context->cameraFollowsPlayer);
        bool hasPlayerMoveInput = IsKeyDown(KEY_A) || IsKeyDown(KEY_D) || IsKeyDown(KEY_LEFT) ||
                                  IsKeyDown(KEY_RIGHT) || IsKeyPressed(KEY_W);
        if (hasPlayerMoveInput && !(*context->isReferenceTextOpen)) {
            (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
            (*context->isReferencePointerFeedbackSuppressed) = true;
        }
        if ((*context->zipperFollowsPlayer) && hasPlayerMoveInput) {
            // 移動を始めた時は、Zipperの選択表示を通常状態へ戻す。
            zipperPointerSelected = false;
            (*context->isZipperPointerFeedbackSuppressed) = true;
        }
        /* 共通ランタイムへ移行済みの移動処理。信号・データ弾の更新は後段で従来どおり実行する。 */
#if 0
        if ((*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && (*context->inspectTarget) < 0 && !(*context->isReferenceTextOpen)) {
            Vector2 previousPosition = (*context->player).position;
            int standingBlockType = RpgStage_GetBlockTypeAtPosition(&(*context->stage), (*context->player).position);
            float savedMoveSpeed = (*context->player).moveSpeed;
            // 遅延ブロックの減速はプレイヤー設定値を変更せず、このフレームだけに適用する。
            if (standingBlockType == RPG_BLOCK_EFFECT_SLOW) (*context->player).moveSpeed *= 0.55f;
            RpgCharacter_UpdatePlayerWithStage(&(*context->player), GetFrameTime(), &(*context->stage), 32.0f,
                                               RPG_STAGE_WORLD_WIDTH - 32.0f);
            (*context->player).moveSpeed = savedMoveSpeed;
            // 跳ねるブロックは接地した瞬間に再び上向きの速度を与える。
            if (RpgBlockInventory_IsBounceEffect(standingBlockType) && (*context->player).isGrounded) {
                (*context->player).verticalSpeed = -620.0f;
                (*context->player).isGrounded = false;
            }
            if (CheckCollisionRecs(RpgCharacter_GetFootBounds(&(*context->player)),
                                   RpgCharacter_GetFootBounds(&(*context->npc)))) {
                (*context->player).position = previousPosition;
            }
        }
#endif
        RpgRuntimeUpdateContext runtimeMovementContext = {
            .player = &(*context->player), .npc = &(*context->npc), .stage = &(*context->stage), .attachments = &(*context->attachments),
            .signalBlocks = &(*context->signalBlocks), .dataShots = &(*context->dataShots), .buttonEvent = &(*context->buttonEvent),
            .receivers = &(*context->receivers), .wires = &(*context->wires), .layout = &(*context->layout),
            .wasButtonPressed = &(*context->wasDataButtonPressed),
            .acceptsPlayerInput = (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && (*context->inspectTarget) < 0 && !(*context->isReferenceTextOpen),
            .updatesWorldSystems = false
        };
        // 信号・データ弾を含む更新は後段で1回だけ実行する。
        if (IsKeyPressed(KEY_SPACE) && (*context->isZipperControllable) && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && (*context->inspectTarget) < 0 &&
            !(*context->isReferenceTextOpen) && !(*context->isReferenceDragActive)) {
            if ((*context->zipperFollowsPlayer)) {
                // 射出開始位置は常に主人公。カーソルへ向かう単位ベクトルを固定して直進させる。
                (*context->zipper).character.position = (*context->player).position;
                Vector2 direction = Vector2Subtract(GetScreenToWorld2D(GetMousePosition(), (*context->camera)),
                                                    GetZipperCollisionCenter(&(*context->zipper)));
                if (Vector2LengthSqr(direction) < 0.001f) direction = (Vector2){ 1.0f, 0.0f };
                (*context->zipperLaunchVelocity) = Vector2Scale(Vector2Normalize(direction), (*context->zipper).launchSpeed);
                (*context->isZipperLaunched) = true;
                (*context->zipperFollowsPlayer) = false;
                (*context->attachedDataShotIndex) = -1;
                (*context->attachedAttachmentIndex) = -1;
                (*context->isZipperAttachedToBlock) = false;
                (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
                zipperPointerSelected = false;
            } else if (!(*context->isZipperLaunched)) {
                /* 帰還は接触状態だけを解除する。Inboxの所持フォルダは次のcmdまで保持する。 */
                (*context->zipperFollowsPlayer) = true;
                (*context->attachedDataShotIndex) = -1;
                (*context->attachedAttachmentIndex) = -1;
                (*context->isZipperAttachedToBlock) = false;
                (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
                zipperPointerSelected = false;
            }
        }
        /* 共通ランタイム側が信号・データ弾まで更新するため、旧更新列は残さない。 */
#if 0
        bool isDataButtonPressed = (*context->player).isGrounded &&
                                   RpgAttachments_IsButtonPressed(&(*context->attachments), (*context->player).position);
        if (isDataButtonPressed && !(*context->wasDataButtonPressed)) {
            // ボタンは用途を決めず、押された事実だけを全体通知として発行する。
            RpgButtonEvent_Publish(&(*context->buttonEvent));
        }
        (*context->wasDataButtonPressed) = isDataButtonPressed;
        RpgDataShots_ConsumeButtonEvent(&(*context->dataShots), &(*context->attachments), &(*context->buttonEvent));
        RpgSignalBlocks_Update(&(*context->signalBlocks), &(*context->stage), &(*context->buttonEvent), GetFrameTime());
        // フォルダの変更を移動前に反映し、ファイル数と容量が弾の見た目・速度へ直ちに反映されるようにする。
        RpgObjectFolders_UpdateDataShotLifetimes(&(*context->dataShots), &(*context->attachments), &(*context->referenceDrops));
        RpgDataShots_Update(&(*context->dataShots), &(*context->attachments), &(*context->stage), &(*context->receivers), &(*context->wires),
                             (*context->layout).electricCellDelay, GetFrameTime(), false);
#endif
        runtimeMovementContext.updatesWorldSystems = true;
        RpgRuntime_UpdateWorld(&runtimeMovementContext, GetFrameTime());
        RpgObjectFolders_UpdateDataShotLifetimes(&(*context->dataShots), &(*context->attachments), &(*context->referenceDrops));
        // 電気化で失われた弾本体のフォルダを同フレームで処理し、追加ファイルをドロップする。
        RpgObjectFolders_UpdateDataShotLifetimes(&(*context->dataShots), &(*context->attachments), &(*context->referenceDrops));
        UpdateZipperAttachedToDataShot(&(*context->zipper), &(*context->dataShots), &(*context->attachedDataShotIndex),
                                       (*context->attachedDataShotOffset),
                                       &(*context->zipperFollowsPlayer), &(*context->isZipperAttachedToBlock),
                                       &(*context->zipperAttachedBlockCell));
        if ((*context->isZipperLaunched))
            UpdateLaunchedZipper(&(*context->zipper), &(*context->zipperLaunchVelocity), &(*context->stage), &(*context->attachments), &(*context->dataShots),
                                 GetFrameTime(), &(*context->isZipperLaunched), &(*context->attachedDataShotIndex),
                                 &(*context->attachedDataShotOffset),
                                 &(*context->isZipperAttachedToBlock), &(*context->zipperAttachedBlockCell),
                                 &(*context->attachedAttachmentIndex));
        else if ((*context->zipperFollowsPlayer) && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen))
            UpdateZipperFollow(&(*context->zipper), &(*context->player), GetFrameTime());
        RpgRuntime_ProcessZipperCommand(context);
        if ((*context->zipperAnimationElapsed) >= 0.0f) {
            (*context->zipperAnimationElapsed) += GetFrameTime();
            if ((*context->zipperAnimationElapsed) >= zipperImportAnimationDuration) (*context->zipperAnimationElapsed) = -1.0f;
        }
        RpgRuntime_UpdateZipperFolderReturn(context, GetFrameTime());
        bool canTalk = RpgCharacter_IsNear(&(*context->player), &(*context->npc), 72.0f);
        RpgReferenceTarget nearbyReferenceTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                       .row = -1, .column = -1, .dropIndex = -1 };
        bool canReadReference = RpgReferenceObjects_FindNearbyTarget(&(*context->stage), &(*context->referenceDrops),
                                                                       (*context->player).position, 72.0f,
                                                                       &nearbyReferenceTarget);
        if (canReadReference) {
            const char *referencePath = RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                           nearbyReferenceTarget);
            snprintf(context->referenceFileName, (size_t)context->referenceFileNameSize, "%s", GetReferenceFileName(referencePath));
            GameFont_AddText(context->referenceFileName);
        }
        if ((*context->itemMessageTimer) > 0.0f) (*context->itemMessageTimer) -= GetFrameTime();
        RpgReferenceObjects_Update(&(*context->referenceDrops), GetFrameTime());
        for (int index = 0; index < (*context->items).count; index++) if (!(*context->items).entries[index].collected &&
            fabsf((*context->player).position.x - (*context->items).entries[index].position.x) <= 28.0f) {
            (*context->items).entries[index].collected = true;
            snprintf(itemMessage, (size_t)context->itemMessageSize, "%s を手に入れた", (*context->items).entries[index].name);
            GameFont_AddText(itemMessage);
            (*context->itemMessageTimer) = 2.5f;
        }
        for (int index = 0; index < (*context->events).count && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen); index++) if (!(*context->events).entries[index].triggered &&
            Vector2Distance((*context->player).position, (*context->events).entries[index].position) <= 36.0f) {
            (*context->events).entries[index].triggered = true;
            // 位置イベントは既存の調べるFunction列を起動し、会話・移動を同じ実装で再利用する。
            (*context->inspectTarget) = 1;
            (*context->inspectFunctionIndex) = 0;
            (*context->inspectLineIndex) = 0;
        }
        // 調べる機能列のMoveは、対象を指定時間で補間して完了後に次の機能へ進める。
        if ((*context->inspectTarget) >= 0) {
            RpgInspect *activeInspect = (*context->inspectTarget) == 2 ? &(*context->zipper).inspect : &(*context->inspect);
            if (activeInspect->functions[(*context->inspectFunctionIndex)].type == RPG_INSPECT_MOVE) {
                RpgInspectMove *move = &activeInspect->functions[(*context->inspectFunctionIndex)].move;
                if (!(*context->isInspectMoveRunning)) {
                    (*context->isInspectMoveRunning) = true;
                    (*context->inspectMoveElapsed) = 0.0f;
                    (*context->inspectMoveStartX) = move->target == RPG_INSPECT_MOVE_PLAYER ? (*context->player).position.x :
                                        move->target == RPG_INSPECT_MOVE_NPC ? (*context->npc).position.x : (*context->zipper).character.position.x;
                }
                (*context->inspectMoveElapsed) += GetFrameTime();
                float progress = Clamp((*context->inspectMoveElapsed) / move->duration, 0.0f, 1.0f);
                float *targetX = move->target == RPG_INSPECT_MOVE_PLAYER ? &(*context->player).position.x :
                                 move->target == RPG_INSPECT_MOVE_NPC ? &(*context->npc).position.x : &(*context->zipper).character.position.x;
                *targetX = (*context->inspectMoveStartX) + (move->destinationX - (*context->inspectMoveStartX)) * progress;
                if (progress >= 1.0f) {
                    (*context->isInspectMoveRunning) = false;
                    (*context->inspectFunctionIndex)++;
                    if ((*context->inspectFunctionIndex) >= activeInspect->functionCount) {
                        if ((*context->inspectTarget) == 2) {
                            (*context->zipperFollowsPlayer) = true;
                            (*context->isZipperControllable) = true;
                            (*context->zipperInspectCompleted) = true;
                        } else (*context->npcInspectCompleted) = true;
                        (*context->inspectTarget) = -1;
                        (*context->inspectFunctionIndex) = -1;
                        (*context->inspectLineIndex) = -1;
                    } else (*context->inspectLineIndex) = 0;
                }
            }
        }
        int currentMap = (int)((*context->player).position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
        if (currentMap == 3 && (*context->previousMap) != 3 && (*context->stage3Event).enabled && !(*context->stage3IntroShown)) {
            (*context->stage3IntroIndex) = 0;
            (*context->stage3IntroShown) = true;
        }
        (*context->previousMap) = currentMap;
        bool isReferenceCloseClicked = (*context->isReferenceTextOpen) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                       CheckCollisionPointRec(GetMousePosition(), referenceTextCloseButton);
        if (isReferenceCloseClicked) {
            (*context->isReferenceTextOpen) = false;
        } else if (IsKeyPressed(KEY_E)) {
            if ((*context->isReferenceTextOpen)) {
                // ファイル表示も会話と同じく、Eで閉じるまでプレイヤー操作を止める。
                (*context->isReferenceTextOpen) = false;
            } else if ((*context->inspectTarget) >= 0 && !(*context->isInspectMoveRunning)) {
                const RpgInspect *activeInspect = (*context->inspectTarget) == 2 ? &(*context->zipper).inspect : &(*context->inspect);
                if (activeInspect->functions[(*context->inspectFunctionIndex)].type == RPG_INSPECT_DIALOGUE) {
                    (*context->inspectLineIndex)++;
                    if ((*context->inspectLineIndex) >= activeInspect->functions[(*context->inspectFunctionIndex)].dialogue.lineCount) {
                        (*context->inspectFunctionIndex)++;
                        (*context->inspectLineIndex) = 0;
                        if ((*context->inspectFunctionIndex) >= activeInspect->functionCount) {
                            if ((*context->inspectTarget) == 2) {
                                (*context->zipperFollowsPlayer) = true;
                                (*context->isZipperControllable) = true;
                                (*context->zipperInspectCompleted) = true;
                            } else (*context->npcInspectCompleted) = true;
                            (*context->inspectTarget) = -1;
                            (*context->inspectFunctionIndex) = -1;
                            (*context->inspectLineIndex) = -1;
                        }
                    }
                }
            } else if ((*context->stage3IntroIndex) >= 0) {
                (*context->stage3IntroIndex)++;
                if ((*context->stage3IntroIndex) >= (*context->stage3Event).dialogue.lineCount) (*context->stage3IntroIndex) = -1;
            } else
            if ((*context->dialogueIndex) >= 0) {
                (*context->dialogueIndex)++;
                if ((*context->dialogueIndex) >= (*context->dialogue).lineCount) (*context->dialogueIndex) = -1;
            } else if (canTalk) {
                (*context->dialogueIndex) = 0;
            } else if (canReadReference) {
                OpenTextFile(RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                nearbyReferenceTarget),
                             context->referenceFileName, (size_t)context->referenceFileNameSize, context->referenceText,
                             (size_t)context->referenceTextSize, &(*context->isReferenceTextOpen));
                (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                (*context->isReferencePointerFeedbackSuppressed) = true;
            }
        }
        bool canInspectZipper = RpgCharacter_IsNear(&(*context->player), &(*context->zipper).character, 72.0f);
        if (IsKeyPressed(KEY_I) && (*context->inspectTarget) < 0 && (*context->inspect).enabled && !(*context->npcInspectCompleted) && canTalk &&
            (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen)) {
            (*context->inspectTarget) = 1;
            (*context->inspectFunctionIndex) = 0;
            (*context->inspectLineIndex) = 0;
        } else if (IsKeyPressed(KEY_I) && (*context->inspectTarget) < 0 && (*context->zipper).inspect.enabled && !(*context->zipperInspectCompleted) && canInspectZipper &&
                   (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen)) {
            (*context->inspectTarget) = 2;
            (*context->inspectFunctionIndex) = 0;
            (*context->inspectLineIndex) = 0;
        }
        UpdateRpgCamera(&(*context->camera), (*context->player).position.x, (*context->cameraFollowsPlayer));
        RpgReferenceTarget hoveredReferencePointerTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                               .row = -1, .column = -1, .dropIndex = -1 };
        bool isReferencePointerHovered = false;
        if (!(*context->isReferenceTextOpen) && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0) {
            Vector2 pointerWorldPosition = GetScreenToWorld2D(GetMousePosition(), (*context->camera));
            isReferencePointerHovered = !(*context->isReferenceDragActive) &&
                                        RpgReferenceObjects_FindTarget(&(*context->stage), &(*context->referenceDrops),
                                                                       pointerWorldPosition,
                                                                       &hoveredReferencePointerTarget);
            if (!isReferencePointerHovered) (*context->isReferencePointerFeedbackSuppressed) = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isReferencePointerHovered) {
                    if ((*context->lastReferencePointerClickTime) >= 0.0 &&
                        GetTime() - (*context->lastReferencePointerClickTime) <= 0.35) {
                        OpenTextFile(RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                        hoveredReferencePointerTarget),
                                     context->referenceFileName, (size_t)context->referenceFileNameSize,
                                     context->referenceText, (size_t)context->referenceTextSize, &(*context->isReferenceTextOpen));
                        (*context->isReferencePointerPressed) = false;
                        (*context->lastReferencePointerClickTime) = -1.0;
                    } else {
                        (*context->selectedReferencePointerTarget) = hoveredReferencePointerTarget;
                        (*context->isReferencePointerFeedbackSuppressed) = false;
                        (*context->isReferencePointerPressed) = true;
                        (*context->pressedReferenceTarget) = hoveredReferencePointerTarget;
                        (*context->referencePressPosition) = pointerWorldPosition;
                        (*context->lastReferencePointerClickTime) = GetTime();
                    }
                } else {
                    (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                    (*context->isReferencePointerFeedbackSuppressed) = true;
                }
            }
            if ((*context->isReferencePointerPressed) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                Vector2Distance((*context->referencePressPosition), pointerWorldPosition) > 6.0f) {
                // クリックとドラッグを距離で分離し、ドラッグ中だけ元のマスから非表示にする。
                (*context->isReferenceDragActive) = true;
                (*context->draggedReferenceTarget) = (*context->pressedReferenceTarget);
                (*context->isReferencePointerPressed) = false;
                (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                (*context->isReferencePointerFeedbackSuppressed) = true;
            }
            if ((*context->isReferenceDragActive)) {
                (*context->referenceDragPosition) = pointerWorldPosition;
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&(*context->zipper).character, 380.0f);
                    if (CheckCollisionPointRec(pointerWorldPosition, zipperBounds)) {
                        const char *path = RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                               (*context->draggedReferenceTarget));
                        if (RpgObjectFolder_CopyFileToZipperInbox(path)) {
                            /* FILE.pngの取り込みもcmdと同じ成功通知としてZipperをアニメーションさせる。 */
                            StartZipperImportAnimation(context->zipperAnimationElapsed);
                            GameFont_AddText(GetReferenceFileName(path));
                            RpgReferenceObjects_RemoveTarget(&(*context->stage), &(*context->referenceDrops), (*context->draggedReferenceTarget));
                        }
                    }
                    (*context->isReferenceDragActive) = false;
                    (*context->draggedReferenceTarget).kind = RPG_REFERENCE_TARGET_NONE;
                }
            }
            if ((*context->isReferencePointerPressed) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                (*context->isReferencePointerPressed) = false;
        }
        bool isZipperPointerHovered = false;
        if (((*context->zipperFollowsPlayer) || (*context->isZipperAttachedToBlock) || (*context->attachedDataShotIndex) >= 0) && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 &&
            (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen)) {
            Vector2 pointerWorldPosition = GetScreenToWorld2D(GetMousePosition(), (*context->camera));
            Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&(*context->zipper).character, 380.0f);
            isZipperPointerHovered = CheckCollisionPointRec(pointerWorldPosition, zipperBounds);
            if (!isZipperPointerHovered) (*context->isZipperPointerFeedbackSuppressed) = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isZipperPointerHovered && GetTime() - (*context->lastZipperPointerClickTime) <= 0.35) {
                    RpgObjectFolder_OpenZipperDirectory();
                    (*context->lastZipperPointerClickTime) = -1.0;
                } else if (isZipperPointerHovered) {
                    (*context->lastZipperPointerClickTime) = GetTime();
                    (*context->isZipperPointerFeedbackSuppressed) = false;
                }
                else {
                    zipperPointerSelected = false;
                    (*context->isZipperPointerFeedbackSuppressed) = true;
                }
                if (isZipperPointerHovered) zipperPointerSelected = true;
            }
        } else {
            zipperPointerSelected = false;
        }
        DrawRpgWorld(&(*context->player), &(*context->npc), &(*context->stage), (*context->camera), (*context->cameraFollowsPlayer), currentMap, canTalk,
                     &(*context->dialogue), (*context->dialogueIndex), (*context->stage3IntroIndex), &(*context->stage3Event), &(*context->zipper), zipperTexture, fileTexture, &(*context->inspect), &(*context->zipper).inspect,
                     (*context->inspectTarget), (*context->inspectFunctionIndex), (*context->inspectLineIndex), false, 0.0f, RPG_INSPECT_MOVE_PLAYER,
                     (*context->npcInspectCompleted), (*context->zipperInspectCompleted),
                     isZipperPointerHovered && !(*context->isZipperPointerFeedbackSuppressed), zipperPointerSelected,
                     &(*context->items), &(*context->referenceDrops), &(*context->wires), &(*context->receivers), &(*context->attachments), &(*context->dataShots), &(*context->events),
                     itemMessage, (*context->itemMessageTimer), nearbyReferenceTarget,
                     context->referenceFileName, context->referenceText, (*context->isReferenceTextOpen),
                     hoveredReferencePointerTarget, (*context->selectedReferencePointerTarget),
                     (*context->isReferencePointerFeedbackSuppressed), (*context->isReferenceDragActive),
                     (*context->draggedReferenceTarget), (*context->referenceDragPosition),
                     (*context->zipperAnimationElapsed), context->showStopButton, context->scene);
    }

#undef fileTexture
#undef zipperTexture
#undef itemMessage
#undef zipperPointerSelected
}
