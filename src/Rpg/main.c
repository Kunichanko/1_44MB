// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_character.h, rpg_block_inventory.h, rpg_data_shot.h, rpg_map_event.h, rpg_object_folder.h, rpg_receiver.h, rpg_wire.h
// ステージ番号別の設定ロード: rpg_stage_storage.h
// 依存関係を更新: game_font.h, rpg_dialogue.h を追加した。
// 依存関係を更新: rpg_stage3_event.h を追加した。
// 依存関係を更新: rpg_viewport.h を追加した。
#include "raylib.h"
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
#include <shellapi.h>
#endif

#include "game_font.h"
#include "rpg_character.h"
#include "rpg_attachment.h"
#include "rpg_button_event.h"
#include "rpg_data_shot.h"
#include "rpg_block_inventory.h"
#include "rpg_build_cell_storage.h"
#include "rpg_game_save.h"
#include "rpg_dialogue.h"
#include "rpg_layout.h"
#include "rpg_stage_background.h"
#include "rpg_viewport.h"
#include "rpg_inspect.h"
#include "rpg_item.h"
#include "rpg_map_event.h"
#include "rpg_object_folder.h"
#include "rpg_receiver.h"
#include "rpg_signal_block.h"
#include "rpg_stage3_event.h"
#include "rpg_stage.h"
#include "rpg_stage_storage.h"
#include "rpg_stage_build.h"
#include "rpg_wire.h"
#include "rpg_zipper.h"
#include "rpg_runtime_update.h"
#include "rpg_runtime.h"
#include "rpg_scene.h"

/* NPC と Zipper は復元用の実装を保持したまま、現在のゲーム開始状態からだけ除外する。 */
static const bool isLegacyNpcAndZipperEnabled = false;

// 依存関係: 本編起動時に build の通常マス保存方式を読み込み、ビルド時に使用する。

enum { RPG_SCREEN_WIDTH = 960, RPG_SCREEN_HEIGHT = 540, RPG_PLAY_AREA_HEIGHT = 480 };

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

/* 旧直接実行経路もランタイムと同じく、テキスト以外のバイト列をゲーム内へ表示しない。 */
static bool IsTextReferenceFile(const char *filePath)
{
    const char *extension = strrchr(GetReferenceFileName(filePath), '.');
    static const char *textExtensions[] = { ".txt", ".md", ".csv", ".json", ".log", ".cfg", ".ini" };
    if (extension == NULL) return false;
    for (int index = 0; index < (int)(sizeof(textExtensions) / sizeof(textExtensions[0])); index++) {
        const char *left = extension;
        const char *right = textExtensions[index];
        while (*left != '\0' && *right != '\0') {
            char character = *left;
            if (character >= 'A' && character <= 'Z') character = (char)(character - 'A' + 'a');
            if (character != *right) break;
            left++; right++;
        }
        if (*left == '\0' && *right == '\0') return true;
    }
    return false;
}

static void RegisterReferenceFileNames(const RpgStage *stage)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (RpgBlockInventory_IsReferenceObject(stage->blocks[row][column]))
            GameFont_AddText(GetReferenceFileName(RpgStage_GetReferencePathAtCell(stage, row, column)));
    }
}

static void OpenTextFile(const char *path, char *fileName, size_t fileNameSize,
                         char *text, size_t textSize, bool *isOpen)
{
    snprintf(fileName, fileNameSize, "%s", GetReferenceFileName(path));
    if (!IsTextReferenceFile(path)) {
        snprintf(text, textSize, "This file type cannot be displayed in the game.");
    } else if (path[0] != '\0' && LoadReferenceText(path, text, textSize)) {
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

static void DrawZipper(Texture2D zipperTexture, const RpgCharacter *zipper, float animationElapsed)
{
    int frameCount = zipperTexture.width / 32;
    int frameIndex = animationElapsed >= 0.0f && frameCount > 1 ?
        (int)Clamp(animationElapsed / 0.60f * frameCount, 0.0f, (float)(frameCount - 1)) : 0;
    Rectangle source = { frameIndex * 32.0f, 0.0f, 32.0f, 40.0f };
    Rectangle destination = RpgZipper_GetPixelAlignedSpriteBounds(zipper, 380.0f);
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
        if (target == RPG_INSPECT_MOVE_PLAYER) RpgCharacter_DrawPlayer(&sprite, RPG_CHARACTER_ANIMATION_IDLE);
        else RpgCharacter_Draw(&sprite, "");
    }
}

static float GetStageViewportZoom(void)
{
    return (float)RPG_SCREEN_WIDTH / (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
}

static void UpdateRpgCamera(Camera2D *camera, float playerX, bool followsPlayer)
{
    // 16x8マスを下部UI以外のゲーム領域（960x480）へ等倍比率で拡大する。
    camera->zoom = GetStageViewportZoom();
    camera->offset = (Vector2){ RPG_SCREEN_WIDTH / 2.0f, RPG_PLAY_AREA_HEIGHT / 2.0f };
    float halfViewWidth = RPG_SCREEN_WIDTH / (2.0f * camera->zoom);
    if (followsPlayer) {
        camera->target.x = playerX;
        if (camera->target.x < halfViewWidth) camera->target.x = halfViewWidth;
        if (camera->target.x > RPG_STAGE_WORLD_WIDTH - halfViewWidth)
            camera->target.x = RPG_STAGE_WORLD_WIDTH - halfViewWidth;
    } else {
        int mapIndex = (int)(playerX / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE));
        if (mapIndex >= RPG_STAGE_MAP_COUNT) mapIndex = RPG_STAGE_MAP_COUNT - 1;
        camera->target.x = mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE +
                           RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f;
    }
    camera->target.y = RPG_STAGE_WORLD_HEIGHT / 2.0f;
}

static void UpdateZipperFollow(RpgZipper *zipper, const RpgCharacter *player, float deltaTime)
{
    // 帰還時は主人公の少し後ろ・同じ足元へ、X/Yをまとめて滑らかに追従させる。
    Vector2 target = { player->position.x - RPG_STAGE_TILE_SIZE * player->scale, player->position.y };
    Vector2 distance = Vector2Subtract(target, zipper->character.position);
    float maximumStep = zipper->returnSpeed * deltaTime;
    float distanceLength = Vector2Length(distance);
    if (distanceLength <= maximumStep) zipper->character.position = target;
    else zipper->character.position = Vector2Add(zipper->character.position,
                                                  Vector2Scale(distance, maximumStep / distanceLength));
}

static Rectangle GetZipperCollisionBounds(const RpgZipper *zipper)
{
    // 見た目全体ではなく本体部分だけをブロック判定にし、接触時に確実に停止させる。
    Rectangle spriteBounds = RpgZipper_GetSpriteBounds(&zipper->character, 380.0f);
    return (Rectangle){ spriteBounds.x + spriteBounds.width * 0.20f,
                        spriteBounds.y + spriteBounds.height * 0.18f,
                        spriteBounds.width * 0.60f, spriteBounds.height * 0.82f };
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
        if (!RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) continue;
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
            forwardCollisionBounds.y + forwardCollisionBounds.height > RPG_STAGE_WORLD_HEIGHT;
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
                         float zipperAnimationElapsed, const RpgStageBackground *stageBackground,
                         float backgroundBrightness, float blockBrightness, bool isZipperLaunched)
{
    (void)npcInspectCompleted;
    BeginDrawing();
    ClearBackground(BLACK);
    BeginMode2D(camera);
    // 背景もブロックと同じ16x8マスのワールド座標で描画して、カメラ追従時もずれないようにする。
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        if (!RpgStage_IsMapActive(stage, mapIndex)) continue;
        RpgStageBackground_Draw(stageBackground,
                                (Rectangle){ (float)(mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                             0.0f,
                                             (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                             (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                                backgroundBrightness);
    }
    /* 最背面PNGは背景の後、ブロックより前に描画する。 */
    RpgImageObjects_DrawLayer(&stage->imageObjects, 0, RPG_STAGE_WORLD_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_BACK);
    RpgStage_Draw(stage, false, blockBrightness);
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
        Vector2 position = RpgStage_SnapRenderPoint(RpgAttachments_GetPosition(attachment, 0));
        DrawCircleV(position, 15.0f, Fade(GOLD, 0.30f));
        DrawCircleLines((int)position.x, (int)position.y, 15.0f, ORANGE);
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
    RpgStage_DrawEffects(stage);
    RpgWires_Draw(wires, stage);
    RpgWires_DrawElectric(wires, dataShots, 0, RPG_STAGE_WORLD_COLUMNS);
    RpgReceivers_Draw(receivers);
    RpgAttachments_Draw(attachments);
    RpgDataShots_Draw(dataShots);
    /* 中間PNGはブロックより前、キャラクターより後に固定する。 */
    RpgImageObjects_DrawLayer(&stage->imageObjects, 0, RPG_STAGE_WORLD_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE,
                              RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
    DrawZipper(zipperTexture, &zipper->character, zipperAnimationElapsed);
    RpgRuntime_DrawZipperFolderReturn(zipper);
    if (isMoveSpriteVisible) DrawMoveSprite(zipperTexture, player, npc, zipper, moveSpriteTarget, moveSpriteX);
    RpgCharacter_Draw(npc, "NPC");
    RpgCharacter_DrawPlayer(player, isZipperLaunched ? RPG_CHARACTER_ANIMATION_ZIPGO :
                                                    RPG_CHARACTER_ANIMATION_AUTOMATIC);
    /* 最前面PNGはキャラクター描画後に重ねる。 */
    RpgImageObjects_DrawLayer(&stage->imageObjects, 0, RPG_STAGE_WORLD_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_FRONT);
    RpgZipper_DrawPointerFeedback(RpgZipper_GetPixelAlignedSpriteBounds(&zipper->character, 380.0f),
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
    if (fabsf(player->position.x - zipper->character.position.x) <= 72.0f && inspectTarget < 0 &&
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
    // ステージは20×10マスをすべて表示し、常設UIはマップ外の下部余白だけに描画する。
    DrawRectangle(0, 480, RPG_SCREEN_WIDTH, RPG_SCREEN_HEIGHT - 480, Fade(RAYWHITE, 0.97f));
    DrawLine(0, 480, RPG_SCREEN_WIDTH, 480, LIGHTGRAY);
    DrawText(TextFormat("Map %d / %d", currentMap, RpgStage_GetMapCount(stage)), 18, 490, 16, DARKGRAY);
    DrawText("Move: A/D or arrows   Jump: W   Launch Zipper: Space", 160, 490, 14, DARKGRAY);
    DrawText(followsPlayer ? "Camera: Follow [C]" : "Camera: Map Pivot [C]", 510, 515, 14,
             DARKBLUE);
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
    EndDrawing();
}

int main(void)
{
    /* 本編はビルド済みパッケージだけを読む。編集用 Settings を直接参照しない。 */
    RpgStageStorage_SetDomain(RPG_STAGE_STORAGE_GAME_PACKAGE);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(RPG_SCREEN_WIDTH, RPG_SCREEN_HEIGHT, "1_44MB - RPG Version");
    ClearWindowState(FLAG_FULLSCREEN_MODE | FLAG_BORDERLESS_WINDOWED_MODE | FLAG_WINDOW_MAXIMIZED);
    SetWindowSize(RPG_SCREEN_WIDTH, RPG_SCREEN_HEIGHT);
    SetWindowPosition(80, 80);
    RpgViewport_Initialize();
    RpgBuildCellStorage_LoadMode();
    SetTargetFPS(60);
    GameFont_Load(TextFormat("%s../assets/Fonts/NotoSansJP-VF.ttf", GetApplicationDirectory()));
    RpgSceneState scene = RpgScene_Default();
    RpgStageCatalog stageCatalog;
    RpgStageCatalog_Load(&stageCatalog);
    int currentStageNumber = RpgStageCatalog_GetCurrentNumber(&stageCatalog);
    RpgScene_SetStageNumber(&scene, currentStageNumber);
    RpgScene_SetStageList(&scene, stageCatalog.numbers, stageCatalog.count);
    static RpgStageData stageData;
    RpgStageStorage_LoadStage(currentStageNumber, &stageData);
    RpgLayout_LoadGlobalRuntime(&stageData.layout);
    RpgScene_RegisterText();
    GameFont_AddText(npcTalkPrompt);
    GameFont_AddText(zipperInspectPrompt);
    GameFont_AddText("開く閉じる");
    Texture2D zipperTexture = LoadTexture(TextFormat("%s../assets/Sprite/ZIPPER.png",
                                                      GetApplicationDirectory()));
    Texture2D fileTexture = LoadTexture(TextFormat("%s../assets/Sprite/FILE.png",
                                                    GetApplicationDirectory()));
    if (zipperTexture.id != 0) SetTextureFilter(zipperTexture, TEXTURE_FILTER_POINT);
    if (fileTexture.id != 0) SetTextureFilter(fileTexture, TEXTURE_FILTER_POINT);
    RpgCharacter_LoadPlayerSprites();

    RpgLayout layout = stageData.layout;
    RpgStageBackground stageBackground = RpgStageBackground_Default();
    RpgStageBackground_Load(&stageBackground, layout.backgroundPath);
    RpgStage stage = stageData.stage;
    // 保存済みの日本語ファイル名を描画より先にフォントへ登録し、? 表示を防ぐ。
    RegisterReferenceFileNames(&stage);
    RpgItems items = stageData.items;
    RpgReferenceObjects referenceDrops = RpgReferenceObjects_Default();
    RpgWires wires = stageData.wires;
    RpgReceivers receivers = stageData.receivers;
    RpgAttachments attachments = stageData.attachments;
    RpgSignalBlocks signalBlocks = stageData.signalBlocks;
    RpgDataShots dataShots = RpgDataShots_Default();
    RpgButtonEvent buttonEvent = RpgButtonEvent_Default();
    // 前回が異常終了しても、プレイ中だけ使う実フォルダとInboxを残さない。
    RpgObjectFolders_ClearSessionStorage();
    RpgMapEvents events = stageData.mapEvents;
    for (int index = 0; index < items.count; index++) GameFont_AddText(items.entries[index].name);
    RpgDialogue dialogue = stageData.dialogue;
    for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
        GameFont_AddText(dialogue.lines[lineIndex]);
        GameFont_AddText(dialogue.speakers[lineIndex]);
    }
    RpgStage3Event stage3Event = stageData.stage3Event;
    /* 24エリア分のFunction列は大きいため、スタックで複製しない。 */
    static RpgAreaEntryEvents areaEntryEvents;
    areaEntryEvents = stageData.areaEntryEvents;
    for (int areaIndex = 0; areaIndex < RPG_STAGE_MAP_COUNT; areaIndex++)
        for (int functionIndex = 0; functionIndex < areaEntryEvents.entries[areaIndex].inspect.functionCount; functionIndex++)
            for (int lineIndex = 0; lineIndex < areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
                GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
                GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lines[lineIndex]);
            }
    RpgZipper zipper = RpgZipper_Default();
    RpgZipper_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg", GetApplicationDirectory()), &zipper);
    /* 現在はNPC・Zipperをゲームから外す。各システムのコードと保存形式は復元用に維持する。 */
    zipper.character.position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
    for (int functionIndex = 0; functionIndex < stage3Event.inspect.functionCount; functionIndex++)
        for (int lineIndex = 0; lineIndex < stage3Event.inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(stage3Event.inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(stage3Event.inspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    RpgInspect inspect = stageData.npcInspectData;
    for (int functionIndex = 0; functionIndex < inspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(inspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    RpgInspect_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg", GetApplicationDirectory()), &zipper.inspect);
    for (int functionIndex = 0; functionIndex < zipper.inspect.functionCount; functionIndex++) {
        for (int lineIndex = 0; lineIndex < zipper.inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
            GameFont_AddText(zipper.inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
            GameFont_AddText(zipper.inspect.functions[functionIndex].dialogue.lines[lineIndex]);
        }
    }
    RpgCharacter player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
    RpgCharacter npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
    npc.position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
    player.moveSpeed = layout.playerMoveSpeed;
    player.scale = layout.playerScale;
    npc.scale = layout.npcScale;
    int dialogueIndex = -1;
    int stage3IntroIndex = -1;
    int inspectFunctionIndex = -1;
    int inspectLineIndex = -1;
    int inspectTarget = -1;
    bool isInspectMoveRunning = false;
    float inspectMoveElapsed = 0.0f;
    float inspectMoveStartX = 0.0f;
    float inspectMoveStartY = 0.0f;
    RpgInspectMove *activeInspectMove = NULL;
    float inspectMoveTransitionElapsed = 0.0f;
    int activeWaitFunctionIndex = -1;
    float inspectWaitElapsed = 0.0f;
    bool stage3IntroShown = false;
    bool areaEntryShown[RPG_STAGE_MAP_COUNT] = { false };
    RpgStage3Event *activeEntryEvent = &stage3Event;
    bool zipperFollowsPlayer = false;
    bool isZipperLaunched = false;
    Vector2 zipperLaunchVelocity = { 0.0f, 0.0f };
    int attachedDataShotIndex = -1;
    int attachedAttachmentIndex = -1;
    Vector2 attachedDataShotOffset = { 0.0f, 0.0f };
    bool isZipperAttachedToBlock = false;
    RpgGridCell zipperAttachedBlockCell = { -1, -1 };
    bool zipperPointerSelected = false;
    bool isZipperPointerFeedbackSuppressed = false;
    double lastZipperPointerClickTime = -1.0;
    RpgReferenceTarget selectedReferencePointerTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                            .row = -1, .column = -1, .dropIndex = -1 };
    bool isReferencePointerFeedbackSuppressed = false;
    bool isReferencePointerPressed = false;
    RpgReferenceTarget pressedReferenceTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                   .row = -1, .column = -1, .dropIndex = -1 };
    Vector2 referencePressPosition = { 0.0f, 0.0f };
    bool isReferenceDragActive = false;
    RpgReferenceTarget draggedReferenceTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                   .row = -1, .column = -1, .dropIndex = -1 };
    Vector2 referenceDragPosition = { 0.0f, 0.0f };
    double lastReferencePointerClickTime = -1.0;
    float zipperAnimationElapsed = -1.0f;
    bool npcInspectCompleted = false;
    bool zipperInspectCompleted = false;
    // 追従状態とは別に、調べる完了後だけ射出・帰還を許可する。
    bool isZipperControllable = false;
    // Zipper の接続済み状態は、現在追従しているかどうかとは独立して復帰用に保持する。
    bool isZipperConnected = false;
    int activeSaveFlagId = 0;
    bool wasDataButtonPressed = false;
    int previousMap = (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
    bool cameraFollowsPlayer = false;
    char itemMessage[96] = { 0 };
    float itemMessageTimer = 0.0f;
    char referenceText[2048] = { 0 };
    char referenceFileName[RPG_STAGE_REFERENCE_PATH_LENGTH] = "FILE.txt";
    bool isReferenceTextOpen = false;
    Camera2D camera = { .offset = { RPG_SCREEN_WIDTH / 2.0f, RPG_PLAY_AREA_HEIGHT / 2.0f },
                        .target = { RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f,
                                    RPG_STAGE_WORLD_HEIGHT / 2.0f },
                        .zoom = RPG_SCREEN_WIDTH / (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE) };
    RpgRuntimeContext runtime = {
        .layout=&layout, .stageBackground=&stageBackground, .stage=&stage, .items=&items, .referenceDrops=&referenceDrops, .wires=&wires, .receivers=&receivers, .attachments=&attachments, .signalBlocks=&signalBlocks, .dataShots=&dataShots, .buttonEvent=&buttonEvent, .events=&events, .dialogue=&dialogue, .stage3Event=&stage3Event, .areaEntryEvents=&areaEntryEvents, .zipper=&zipper, .inspect=&inspect, .player=&player, .npc=&npc,
        .dialogueIndex=&dialogueIndex, .stage3IntroIndex=&stage3IntroIndex, .inspectFunctionIndex=&inspectFunctionIndex, .inspectLineIndex=&inspectLineIndex, .inspectTarget=&inspectTarget, .isInspectMoveRunning=&isInspectMoveRunning, .inspectMoveElapsed=&inspectMoveElapsed, .inspectMoveStartX=&inspectMoveStartX, .inspectMoveStartY=&inspectMoveStartY, .activeInspectMove=&activeInspectMove, .inspectMoveTransitionElapsed=&inspectMoveTransitionElapsed, .activeWaitFunctionIndex=&activeWaitFunctionIndex, .inspectWaitElapsed=&inspectWaitElapsed, .stage3IntroShown=&stage3IntroShown, .areaEntryShown=areaEntryShown, .activeEntryEvent=&activeEntryEvent, .zipperFollowsPlayer=&zipperFollowsPlayer, .isZipperLaunched=&isZipperLaunched, .zipperLaunchVelocity=&zipperLaunchVelocity, .attachedDataShotIndex=&attachedDataShotIndex, .attachedAttachmentIndex=&attachedAttachmentIndex, .attachedDataShotOffset=&attachedDataShotOffset, .isZipperAttachedToBlock=&isZipperAttachedToBlock, .zipperAttachedBlockCell=&zipperAttachedBlockCell,
        .zipperPointerSelected=&zipperPointerSelected, .isZipperPointerFeedbackSuppressed=&isZipperPointerFeedbackSuppressed, .lastZipperPointerClickTime=&lastZipperPointerClickTime, .selectedReferencePointerTarget=&selectedReferencePointerTarget, .isReferencePointerFeedbackSuppressed=&isReferencePointerFeedbackSuppressed, .isReferencePointerPressed=&isReferencePointerPressed, .pressedReferenceTarget=&pressedReferenceTarget, .referencePressPosition=&referencePressPosition, .isReferenceDragActive=&isReferenceDragActive, .draggedReferenceTarget=&draggedReferenceTarget, .referenceDragPosition=&referenceDragPosition, .lastReferencePointerClickTime=&lastReferencePointerClickTime, .zipperAnimationElapsed=&zipperAnimationElapsed, .npcInspectCompleted=&npcInspectCompleted, .zipperInspectCompleted=&zipperInspectCompleted, .isZipperControllable=&isZipperControllable, .wasDataButtonPressed=&wasDataButtonPressed, .previousMap=&previousMap, .cameraFollowsPlayer=&cameraFollowsPlayer, .itemMessage=itemMessage, .itemMessageSize=(int)sizeof(itemMessage), .itemMessageTimer=&itemMessageTimer, .referenceText=referenceText, .referenceTextSize=(int)sizeof(referenceText), .referenceFileName=referenceFileName, .referenceFileNameSize=(int)sizeof(referenceFileName), .isReferenceTextOpen=&isReferenceTextOpen, .camera=&camera, .zipperTexture=zipperTexture, .fileTexture=fileTexture, .scene=&scene
    };
    (void)OpenTextFile;
    (void)UpdateRpgCamera;
    (void)UpdateZipperFollow;
    (void)UpdateLaunchedZipper;
    (void)UpdateZipperAttachedToDataShot;
    (void)DrawRpgWorld;
    while (!WindowShouldClose()) {
        RpgViewport_Update();
        // 設定画面も本編ランタイムが入力を処理する。タイトル系シーンとは更新経路を分ける。
        if (RpgScene_IsGameScene(&scene) || RpgScene_IsGameSettings(&scene)) {
            RpgGameSave continueSave = RpgGameSave_Default();
            bool shouldContinue = RpgScene_ConsumeContinueLoad(&scene) && RpgGameSave_Load(&continueSave);
            if (shouldContinue) RpgScene_SetStageNumber(&scene, continueSave.stageNumber);
            // タイトル画面で選択した番号だけを入口に、実行中の全ステージ状態を読み直す。
            bool isBuildRequested = RpgScene_ConsumeGameReset(&scene);
            bool shouldReloadStage = currentStageNumber != scene.selectedStageNumber || isBuildRequested || shouldContinue;
            if (shouldReloadStage &&
                RpgStageStorage_LoadStage(scene.selectedStageNumber, &stageData)) {
                currentStageNumber = scene.selectedStageNumber;
                layout = stageData.layout;
                RpgLayout_LoadGlobalRuntime(&layout);
                RpgStageBackground_Load(&stageBackground, layout.backgroundPath);
                stage = stageData.stage;
                RegisterReferenceFileNames(&stage);
                items = stageData.items;
                referenceDrops = RpgReferenceObjects_Default();
                wires = stageData.wires;
                receivers = stageData.receivers;
                attachments = stageData.attachments;
                signalBlocks = stageData.signalBlocks;
                dataShots = RpgDataShots_Default();
                buttonEvent = RpgButtonEvent_Default();
                events = stageData.mapEvents;
                dialogue = stageData.dialogue;
                stage3Event = stageData.stage3Event;
                areaEntryEvents = stageData.areaEntryEvents;
                inspect = stageData.npcInspectData;
                RpgZipper_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper.cfg", GetApplicationDirectory()), &zipper);
                RpgInspect_Load(TextFormat("%s../assets/Settings/Zipper/rpg_zipper_inspect.cfg", GetApplicationDirectory()), &zipper.inspect);
                zipper.character.position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
                /* ステージを読み直す時は、一時的なInbox所持情報を次の実行へ持ち越さない。 */
                RpgZipper_ClearHeldObject(&zipper);
                player = RpgCharacter_Create(layout.playerPosition, BLUE, BROWN);
                player.moveSpeed = layout.playerMoveSpeed;
                player.scale = layout.playerScale;
                npc = RpgCharacter_Create(layout.npcPosition, PURPLE, DARKBROWN);
                npc.position = (Vector2){ -RPG_STAGE_WORLD_COLUMNS * RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE };
                npc.scale = layout.npcScale;
                dialogueIndex = -1; stage3IntroIndex = -1; inspectFunctionIndex = -1; inspectLineIndex = -1;
                inspectTarget = -1; isInspectMoveRunning = false; inspectMoveElapsed = 0.0f;
                activeInspectMove = NULL; inspectMoveTransitionElapsed = 0.0f;
                activeWaitFunctionIndex = -1; inspectWaitElapsed = 0.0f;
                inspectMoveStartX = 0.0f; inspectMoveStartY = 0.0f; stage3IntroShown = false; memset(areaEntryShown, 0, sizeof(areaEntryShown)); activeEntryEvent = &stage3Event; zipperFollowsPlayer = false;
                isZipperConnected = false; activeSaveFlagId = 0;
                isZipperLaunched = false; zipperLaunchVelocity = (Vector2){ 0.0f, 0.0f };
                attachedDataShotIndex = -1; attachedAttachmentIndex = -1;
                attachedDataShotOffset = (Vector2){ 0.0f, 0.0f }; isZipperAttachedToBlock = false;
                zipperAttachedBlockCell = (RpgGridCell){ -1, -1 }; zipperPointerSelected = false;
                isReferenceTextOpen = false; itemMessage[0] = '\0'; itemMessageTimer = 0.0f;
                // 続きからは保存旗の土台上へ復帰し、接続済みZipperは追従状態まで同時に戻す。
                if (shouldContinue && RpgAttachments_SetRaisedSaveFlag(&attachments, continueSave.flagId)) {
                    for (int index = 0; index < attachments.count; index++) {
                        if (attachments.entries[index].type != RPG_BLOCK_ATTACHMENT_SAVE_FLAG ||
                            attachments.entries[index].folderId != continueSave.flagId) continue;
                        player.position = RpgAttachments_GetSaveFlagRespawnPosition(&attachments.entries[index]);
                        player.verticalSpeed = 0.0f;
                        player.isGrounded = true;
                        activeSaveFlagId = continueSave.flagId;
                        break;
                    }
                    isZipperConnected = isLegacyNpcAndZipperEnabled && continueSave.zipperConnected;
                    if (isZipperConnected) {
                        // エリア3の Zipper は調べ済みとして接続状態を再開する。
                        zipperInspectCompleted = true;
                        isZipperControllable = true;
                        zipperFollowsPlayer = true;
                        zipper.character.position = player.position;
                        zipper.character.verticalSpeed = 0.0f;
                        zipper.character.isGrounded = true;
                    }
                }
                previousMap = (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
                if (isBuildRequested) {
                    if (!RpgStageBuild_Create(currentStageNumber, &stage, &attachments, player.position))
                        RpgObjectFolders_EndStageBuild();
                } else {
                    RpgObjectFolders_EndStageBuild();
                    RpgObjectFolders_ClearSessionStorage();
                    RpgObjectFolders_PrepareAttachmentFolders(&attachments);
                    RpgObjectFolder_PrepareZipperAnimationCommand();
                }
                for (int index = 0; index < items.count; index++) GameFont_AddText(items.entries[index].name);
                for (int lineIndex = 0; lineIndex < dialogue.lineCount; lineIndex++) {
                    GameFont_AddText(dialogue.lines[lineIndex]);
                    GameFont_AddText(dialogue.speakers[lineIndex]);
                }
                for (int functionIndex = 0; functionIndex < stage3Event.inspect.functionCount; functionIndex++)
                    for (int lineIndex = 0; lineIndex < stage3Event.inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
                        GameFont_AddText(stage3Event.inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
                        GameFont_AddText(stage3Event.inspect.functions[functionIndex].dialogue.lines[lineIndex]);
                    }
                for (int areaIndex = 0; areaIndex < RPG_STAGE_MAP_COUNT; areaIndex++)
                    for (int functionIndex = 0; functionIndex < areaEntryEvents.entries[areaIndex].inspect.functionCount; functionIndex++)
                        for (int lineIndex = 0; lineIndex < areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lineCount; lineIndex++) {
                            GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.speakers[lineIndex]);
                            GameFont_AddText(areaEntryEvents.entries[areaIndex].inspect.functions[functionIndex].dialogue.lines[lineIndex]);
                        }
            }
            RpgStageBuild_Update(&stage);
            RpgRuntime_UpdateAndDraw(&runtime);
            if (RpgScene_IsGameScene(&scene)) {
                int touchedFlagIndex = RpgAttachments_FindTouchedSaveFlag(&attachments, player.position);
                if (zipperInspectCompleted) isZipperConnected = true;
                if (touchedFlagIndex >= 0 && attachments.entries[touchedFlagIndex].folderId != activeSaveFlagId) {
                    RpgGameSave gameSave = {
                        .isValid = true,
                        .stageNumber = currentStageNumber,
                        .flagId = attachments.entries[touchedFlagIndex].folderId,
                        .zipperConnected = isZipperConnected
                    };
                    if (RpgGameSave_Save(&gameSave)) {
                        activeSaveFlagId = gameSave.flagId;
                        RpgAttachments_SetRaisedSaveFlag(&attachments, activeSaveFlagId);
                        snprintf(itemMessage, sizeof(itemMessage), "Saved at flag %d", activeSaveFlagId);
                        itemMessageTimer = 2.0f;
                    }
                }
            }
        }
        else RpgScene_UpdateAndDraw(&scene);
    }
#if 0
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_C)) cameraFollowsPlayer = !cameraFollowsPlayer;
        bool hasPlayerMoveInput = IsKeyDown(KEY_A) || IsKeyDown(KEY_D) || IsKeyDown(KEY_LEFT) ||
                                  IsKeyDown(KEY_RIGHT) || IsKeyPressed(KEY_W);
        if (hasPlayerMoveInput && !isReferenceTextOpen) {
            selectedReferencePointerTarget.kind = RPG_REFERENCE_TARGET_NONE;
            isReferencePointerFeedbackSuppressed = true;
        }
        if (zipperFollowsPlayer && hasPlayerMoveInput) {
            // 移動を始めた時は、Zipperの選択表示を通常状態へ戻す。
            zipperPointerSelected = false;
            isZipperPointerFeedbackSuppressed = true;
        }
        /* 共通ランタイムへ移行済みの移動処理。信号・データ弾の更新は後段で従来どおり実行する。 */
#if 0
        if (dialogueIndex < 0 && stage3IntroIndex < 0 && inspectTarget < 0 && !isReferenceTextOpen) {
            Vector2 previousPosition = player.position;
            int standingBlockType = RpgStage_GetBlockTypeAtPosition(&stage, player.position);
            float savedMoveSpeed = player.moveSpeed;
            // 遅延ブロックの減速はプレイヤー設定値を変更せず、このフレームだけに適用する。
            if (standingBlockType == RPG_BLOCK_EFFECT_SLOW) player.moveSpeed *= 0.55f;
            RpgCharacter_UpdatePlayerWithStage(&player, GetFrameTime(), &stage, 32.0f,
                                               RPG_STAGE_WORLD_WIDTH - 32.0f);
            player.moveSpeed = savedMoveSpeed;
            // 跳ねるブロックは接地した瞬間に再び上向きの速度を与える。
            if (RpgBlockInventory_IsBounceEffect(standingBlockType) && player.isGrounded) {
                player.verticalSpeed = -620.0f;
                player.isGrounded = false;
            }
            if (CheckCollisionRecs(RpgCharacter_GetFootBounds(&player),
                                   RpgCharacter_GetFootBounds(&npc))) {
                player.position = previousPosition;
            }
        }
#endif
        RpgRuntimeUpdateContext runtimeMovementContext = {
            .player = &player, .npc = &npc, .stage = &stage, .attachments = &attachments,
            .signalBlocks = &signalBlocks, .dataShots = &dataShots, .buttonEvent = &buttonEvent,
            .receivers = &receivers, .wires = &wires, .layout = &layout,
            .wasButtonPressed = &wasDataButtonPressed,
            .acceptsPlayerInput = dialogueIndex < 0 && stage3IntroIndex < 0 && inspectTarget < 0 && !isReferenceTextOpen,
            .updatesWorldSystems = false
        };
        RpgRuntime_UpdateWorld(&runtimeMovementContext, GetFrameTime());
        if (IsKeyPressed(KEY_SPACE) && isZipperControllable && dialogueIndex < 0 && stage3IntroIndex < 0 && inspectTarget < 0 &&
            !isReferenceTextOpen && !isReferenceDragActive) {
            if (zipperFollowsPlayer) {
                // 射出開始位置は常に主人公。カーソルへ向かう単位ベクトルを固定して直進させる。
                zipper.character.position = player.position;
                Vector2 direction = Vector2Subtract(GetScreenToWorld2D(RpgViewport_GetMousePosition(), camera),
                                                    GetZipperCollisionCenter(&zipper));
                if (Vector2LengthSqr(direction) < 0.001f) direction = (Vector2){ 1.0f, 0.0f };
                zipperLaunchVelocity = Vector2Scale(Vector2Normalize(direction), zipper.launchSpeed);
                isZipperLaunched = true;
                RpgCharacter_ResetAnimation(&player);
                zipperFollowsPlayer = false;
                attachedDataShotIndex = -1;
                attachedAttachmentIndex = -1;
                isZipperAttachedToBlock = false;
                zipperAttachedBlockCell = (RpgGridCell){ -1, -1 };
                zipperPointerSelected = false;
            } else if (!isZipperLaunched) {
                // 帰還操作を受け付けた時点を画面に示し、Explorerの更新待ちと処理開始を区別できるようにする。
                snprintf(itemMessage, sizeof(itemMessage), "帰還開始：Inboxのobjectフォルダを保持");
                GameFont_AddText(itemMessage);
                itemMessageTimer = 2.5f;
                // ブロックへ停止した射出後は、同じキーで追従状態へ復帰できる。
                zipperFollowsPlayer = true;
                attachedDataShotIndex = -1;
                attachedAttachmentIndex = -1;
                isZipperAttachedToBlock = false;
                zipperAttachedBlockCell = (RpgGridCell){ -1, -1 };
                // 帰還後はZipper内に見せていた対象フォルダのコピーだけを片付ける。
                zipperPointerSelected = false;
            }
        }
        /* 共通ランタイム側が信号・データ弾まで更新するため、旧更新列は残さない。 */
#if 0
        bool isDataButtonPressed = player.isGrounded &&
                                   RpgAttachments_IsButtonPressed(&attachments, player.position);
        if (isDataButtonPressed && !wasDataButtonPressed) {
            // ボタンは用途を決めず、押された事実だけを全体通知として発行する。
            RpgButtonEvent_Publish(&buttonEvent);
        }
        wasDataButtonPressed = isDataButtonPressed;
        RpgDataShots_ConsumeButtonEvent(&dataShots, &attachments, &buttonEvent);
        RpgSignalBlocks_Update(&signalBlocks, &stage, &buttonEvent, GetFrameTime());
        // フォルダの変更を移動前に反映し、ファイル数と容量が弾の見た目・速度へ直ちに反映されるようにする。
        RpgObjectFolders_UpdateDataShotLifetimes(&dataShots, &attachments, &referenceDrops);
        RpgDataShots_Update(&dataShots, &attachments, &stage, &receivers, &wires,
                             layout.electricCellDelay, GetFrameTime(), false);
#endif
        runtimeMovementContext.updatesWorldSystems = true;
        RpgRuntime_UpdateWorld(&runtimeMovementContext, GetFrameTime());
        RpgObjectFolders_UpdateDataShotLifetimes(&dataShots, &attachments, &referenceDrops);
        // 電気化で失われた弾本体のフォルダを同フレームで処理し、追加ファイルをドロップする。
        RpgObjectFolders_UpdateDataShotLifetimes(&dataShots, &attachments, &referenceDrops);
        UpdateZipperAttachedToDataShot(&zipper, &dataShots, &attachedDataShotIndex,
                                       attachedDataShotOffset,
                                       &zipperFollowsPlayer, &isZipperAttachedToBlock,
                                       &zipperAttachedBlockCell);
        if (isZipperLaunched)
            UpdateLaunchedZipper(&zipper, &zipperLaunchVelocity, &stage, &attachments, &dataShots,
                                 GetFrameTime(), &isZipperLaunched, &attachedDataShotIndex,
                                 &attachedDataShotOffset,
                                 &isZipperAttachedToBlock, &zipperAttachedBlockCell,
                                 &attachedAttachmentIndex);
        else if (zipperFollowsPlayer && inspectTarget < 0 && dialogueIndex < 0 && stage3IntroIndex < 0 && !isReferenceTextOpen)
            UpdateZipperFollow(&zipper, &player, GetFrameTime());
        /* 旧実装は同じ入力を二重処理しないよう無効化し、下の共通ランタイムへ一本化する。 */
#if 0
        if (RpgObjectFolder_BeginZipperCommandRequest()) {
            bool moved = true;
            if (attachedDataShotIndex >= 0 && attachedDataShotIndex < RPG_DATA_SHOT_MAX_COUNT &&
                dataShots.entries[attachedDataShotIndex].active) {
                moved = RpgObjectFolder_MoveDataShotToZipper(&dataShots.entries[attachedDataShotIndex]);
            } else if (isZipperAttachedToBlock && zipperAttachedBlockCell.row >= 0 &&
                zipperAttachedBlockCell.column >= 0) {
                if (attachedAttachmentIndex >= 0 && attachedAttachmentIndex < attachments.count)
                    moved = RpgObjectFolder_MoveAttachmentToZipper(&attachments.entries[attachedAttachmentIndex]);
                else {
                    RpgObjectFolder blockFolder = { .cell = zipperAttachedBlockCell };
                    moved = RpgObjectFolder_MoveBlockToZipper(&blockFolder,
                        stage.blocks[zipperAttachedBlockCell.row][zipperAttachedBlockCell.column]);
                }
            }
            /* 取り込み成功時点で必ず開始し、要求ファイルの掃除は表示と切り離す。 */
            if (moved) {
                zipperAnimationElapsed = 0.0f;
                (void)RpgObjectFolder_CompleteZipperCommandRequest();
            }
        }
#endif
        RpgRuntime_ProcessZipperCommand(&runtime);
        if (zipperAnimationElapsed >= 0.0f) {
            zipperAnimationElapsed += GetFrameTime();
            if (zipperAnimationElapsed >= 0.60f) zipperAnimationElapsed = -1.0f;
        }
        RpgRuntime_UpdateZipperFolderReturn(&runtime, GetFrameTime());
        bool canTalk = RpgCharacter_IsNear(&player, &npc, 72.0f);
        RpgReferenceTarget nearbyReferenceTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                       .row = -1, .column = -1, .dropIndex = -1 };
        bool canReadReference = RpgReferenceObjects_FindNearbyTarget(&stage, &referenceDrops,
                                                                       player.position, 72.0f,
                                                                       &nearbyReferenceTarget);
        if (canReadReference) {
            const char *referencePath = RpgReferenceObjects_GetTargetPath(&stage, &referenceDrops,
                                                                           nearbyReferenceTarget);
            snprintf(referenceFileName, sizeof(referenceFileName), "%s", GetReferenceFileName(referencePath));
            GameFont_AddText(referenceFileName);
        }
        if (itemMessageTimer > 0.0f) itemMessageTimer -= GetFrameTime();
        RpgReferenceObjects_Update(&referenceDrops, GetFrameTime());
        for (int index = 0; index < items.count; index++) if (!items.entries[index].collected &&
            fabsf(player.position.x - items.entries[index].position.x) <= 28.0f) {
            items.entries[index].collected = true;
            snprintf(itemMessage, sizeof(itemMessage), "%s を手に入れた", items.entries[index].name);
            GameFont_AddText(itemMessage);
            itemMessageTimer = 2.5f;
        }
        for (int index = 0; index < events.count && inspectTarget < 0 && dialogueIndex < 0 && stage3IntroIndex < 0 && !isReferenceTextOpen; index++) if (!events.entries[index].triggered &&
            Vector2Distance(player.position, events.entries[index].position) <= 36.0f) {
            events.entries[index].triggered = true;
            // 位置イベントは既存の調べるFunction列を起動し、会話・移動を同じ実装で再利用する。
            inspectTarget = 1;
            inspectFunctionIndex = 0;
            inspectLineIndex = 0;
        }
        // 調べる機能列のMoveは、対象を指定時間で補間して完了後に次の機能へ進める。
        if (inspectTarget >= 0) {
            RpgInspect *activeInspect = inspectTarget == 2 ? &zipper.inspect : &inspect;
            if (activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_MOVE) {
                RpgInspectMove *move = &activeInspect->functions[inspectFunctionIndex].move;
                int imageIndex = move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ?
                                 RpgImageObjects_FindById(&stage.imageObjects, move->targetImageObjectId) : -1;
                RpgImageObject *imageTarget = imageIndex >= 0 ? &stage.imageObjects.entries[imageIndex] : NULL;
                if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && imageTarget == NULL)
                    move->target = RPG_INSPECT_MOVE_PLAYER;
                if (!isInspectMoveRunning) {
                    isInspectMoveRunning = true;
                    inspectMoveElapsed = 0.0f;
                    inspectMoveStartX = imageTarget != NULL ? RpgImageObjects_GetWorldCenterX(imageTarget, RPG_STAGE_TILE_SIZE) :
                                        move->target == RPG_INSPECT_MOVE_PLAYER ? player.position.x :
                                        move->target == RPG_INSPECT_MOVE_NPC ? npc.position.x : zipper.character.position.x;
                    inspectMoveStartY = imageTarget != NULL ? RpgImageObjects_GetWorldCenterY(imageTarget, RPG_STAGE_TILE_SIZE) :
                                        move->target == RPG_INSPECT_MOVE_PLAYER ? player.position.y :
                                        move->target == RPG_INSPECT_MOVE_NPC ? npc.position.y : zipper.character.position.y;
                }
                inspectMoveElapsed += GetFrameTime();
                float progress = Clamp(inspectMoveElapsed / move->duration, 0.0f, 1.0f);
                float easedProgress = RpgInspect_EaseMoveProgress(move->easing, progress);
                float currentX = RpgInspect_MoveAxisHasX(move->axis) ?
                    inspectMoveStartX + (move->destinationX - inspectMoveStartX) * easedProgress : inspectMoveStartX;
                float currentY = RpgInspect_MoveAxisHasY(move->axis) ?
                    inspectMoveStartY + (move->destinationY - inspectMoveStartY) * easedProgress : inspectMoveStartY;
                if (imageTarget != NULL) RpgImageObjects_SetRuntimePosition(imageTarget, (Vector2){ currentX, currentY });
                else {
                    float *targetX = move->target == RPG_INSPECT_MOVE_PLAYER ? &player.position.x :
                                     move->target == RPG_INSPECT_MOVE_NPC ? &npc.position.x : &zipper.character.position.x;
                    float *targetY = move->target == RPG_INSPECT_MOVE_PLAYER ? &player.position.y :
                                     move->target == RPG_INSPECT_MOVE_NPC ? &npc.position.y : &zipper.character.position.y;
                    *targetX = currentX;
                    *targetY = currentY;
                }
                if (progress >= 1.0f) {
                    if (imageTarget != NULL)
                        RpgImageObjects_CommitRuntimePosition(imageTarget, RPG_STAGE_TILE_SIZE, RPG_STAGE_WORLD_COLUMNS,
                                                              RPG_STAGE_ROWS);
                    isInspectMoveRunning = false;
                    inspectFunctionIndex++;
                    if (inspectFunctionIndex >= activeInspect->functionCount) {
                        if (inspectTarget == 2) {
                            zipperFollowsPlayer = true;
                            isZipperControllable = true;
                            zipperInspectCompleted = true;
                        } else npcInspectCompleted = true;
                        inspectTarget = -1;
                        inspectFunctionIndex = -1;
                        inspectLineIndex = -1;
                    } else inspectLineIndex = 0;
                }
            }
        }
        int currentMap = (int)(player.position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)) + 1;
        if (currentMap == 3 && previousMap != 3 && stage3Event.enabled && !stage3IntroShown) {
            stage3IntroIndex = 0;
            stage3IntroShown = true;
        }
        previousMap = currentMap;
        bool isReferenceCloseClicked = isReferenceTextOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                       CheckCollisionPointRec(RpgViewport_GetMousePosition(), referenceTextCloseButton);
        if (isReferenceCloseClicked) {
            isReferenceTextOpen = false;
        } else if (IsKeyPressed(KEY_E)) {
            if (isReferenceTextOpen) {
                // ファイル表示も会話と同じく、Eで閉じるまでプレイヤー操作を止める。
                isReferenceTextOpen = false;
            } else if (inspectTarget >= 0 && !isInspectMoveRunning) {
                const RpgInspect *activeInspect = inspectTarget == 2 ? &zipper.inspect : &inspect;
                if (activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_DIALOGUE) {
                    inspectLineIndex++;
                    if (inspectLineIndex >= activeInspect->functions[inspectFunctionIndex].dialogue.lineCount) {
                        inspectFunctionIndex++;
                        inspectLineIndex = 0;
                        if (inspectFunctionIndex >= activeInspect->functionCount) {
                            if (inspectTarget == 2) {
                                zipperFollowsPlayer = true;
                                isZipperControllable = true;
                                zipperInspectCompleted = true;
                            } else npcInspectCompleted = true;
                            inspectTarget = -1;
                            inspectFunctionIndex = -1;
                            inspectLineIndex = -1;
                        }
                    }
                }
            } else if (stage3IntroIndex >= 0) {
                stage3IntroIndex++;
                if (stage3IntroIndex >= stage3Event.dialogue.lineCount) stage3IntroIndex = -1;
            } else
            if (dialogueIndex >= 0) {
                dialogueIndex++;
                if (dialogueIndex >= dialogue.lineCount) dialogueIndex = -1;
            } else if (canTalk) {
                dialogueIndex = 0;
            } else if (canReadReference) {
                OpenTextFile(RpgReferenceObjects_GetTargetPath(&stage, &referenceDrops,
                                                                nearbyReferenceTarget),
                             referenceFileName, sizeof(referenceFileName), referenceText,
                             sizeof(referenceText), &isReferenceTextOpen);
                selectedReferencePointerTarget.kind = RPG_REFERENCE_TARGET_NONE;
                isReferencePointerFeedbackSuppressed = true;
            }
        }
        bool canInspectZipper = fabsf(player.position.x - zipper.character.position.x) <= 72.0f;
        if (IsKeyPressed(KEY_I) && inspectTarget < 0 && inspect.enabled && !npcInspectCompleted && canTalk &&
            dialogueIndex < 0 && stage3IntroIndex < 0 && !isReferenceTextOpen) {
            inspectTarget = 1;
            inspectFunctionIndex = 0;
            inspectLineIndex = 0;
        } else if (IsKeyPressed(KEY_I) && inspectTarget < 0 && zipper.inspect.enabled && !zipperInspectCompleted && canInspectZipper &&
                   dialogueIndex < 0 && stage3IntroIndex < 0 && !isReferenceTextOpen) {
            inspectTarget = 2;
            inspectFunctionIndex = 0;
            inspectLineIndex = 0;
        }
        UpdateRpgCamera(&camera, player.position.x, cameraFollowsPlayer);
        RpgReferenceTarget hoveredReferencePointerTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                               .row = -1, .column = -1, .dropIndex = -1 };
        bool isReferencePointerHovered = false;
        if (!isReferenceTextOpen && inspectTarget < 0 && dialogueIndex < 0 && stage3IntroIndex < 0) {
            Vector2 pointerWorldPosition = GetScreenToWorld2D(RpgViewport_GetMousePosition(), camera);
            isReferencePointerHovered = !isReferenceDragActive &&
                                        RpgReferenceObjects_FindTarget(&stage, &referenceDrops,
                                                                       pointerWorldPosition,
                                                                       &hoveredReferencePointerTarget);
            if (!isReferencePointerHovered) isReferencePointerFeedbackSuppressed = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isReferencePointerHovered) {
                    if (lastReferencePointerClickTime >= 0.0 &&
                        GetTime() - lastReferencePointerClickTime <= 0.35) {
                        OpenTextFile(RpgReferenceObjects_GetTargetPath(&stage, &referenceDrops,
                                                                        hoveredReferencePointerTarget),
                                     referenceFileName, sizeof(referenceFileName),
                                     referenceText, sizeof(referenceText), &isReferenceTextOpen);
                        isReferencePointerPressed = false;
                        lastReferencePointerClickTime = -1.0;
                    } else {
                        selectedReferencePointerTarget = hoveredReferencePointerTarget;
                        isReferencePointerFeedbackSuppressed = false;
                        isReferencePointerPressed = true;
                        pressedReferenceTarget = hoveredReferencePointerTarget;
                        referencePressPosition = pointerWorldPosition;
                        lastReferencePointerClickTime = GetTime();
                    }
                } else {
                    selectedReferencePointerTarget.kind = RPG_REFERENCE_TARGET_NONE;
                    isReferencePointerFeedbackSuppressed = true;
                }
            }
            if (isReferencePointerPressed && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                Vector2Distance(referencePressPosition, pointerWorldPosition) > 6.0f) {
                // クリックとドラッグを距離で分離し、ドラッグ中だけ元のマスから非表示にする。
                isReferenceDragActive = true;
                draggedReferenceTarget = pressedReferenceTarget;
                isReferencePointerPressed = false;
                selectedReferencePointerTarget.kind = RPG_REFERENCE_TARGET_NONE;
                isReferencePointerFeedbackSuppressed = true;
            }
            if (isReferenceDragActive) {
                referenceDragPosition = pointerWorldPosition;
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&zipper.character, 380.0f);
                    if (CheckCollisionPointRec(pointerWorldPosition, zipperBounds)) {
                        const char *path = RpgReferenceObjects_GetTargetPath(&stage, &referenceDrops,
                                                                               draggedReferenceTarget);
                        if (RpgObjectFolder_CopyFileToZipperInbox(path)) {
                            GameFont_AddText(GetReferenceFileName(path));
                            RpgReferenceObjects_RemoveTarget(&stage, &referenceDrops, draggedReferenceTarget);
                        }
                    }
                    isReferenceDragActive = false;
                    draggedReferenceTarget.kind = RPG_REFERENCE_TARGET_NONE;
                }
            }
            if (isReferencePointerPressed && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                isReferencePointerPressed = false;
        }
        bool isZipperPointerHovered = false;
        if ((zipperFollowsPlayer || isZipperAttachedToBlock || attachedDataShotIndex >= 0) && inspectTarget < 0 && dialogueIndex < 0 &&
            stage3IntroIndex < 0 && !isReferenceTextOpen) {
            Vector2 pointerWorldPosition = GetScreenToWorld2D(RpgViewport_GetMousePosition(), camera);
            Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&zipper.character, 380.0f);
            isZipperPointerHovered = CheckCollisionPointRec(pointerWorldPosition, zipperBounds);
            if (!isZipperPointerHovered) isZipperPointerFeedbackSuppressed = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isZipperPointerHovered && GetTime() - lastZipperPointerClickTime <= 0.35) {
                    RpgObjectFolder_OpenZipperDirectory();
                    lastZipperPointerClickTime = -1.0;
                } else if (isZipperPointerHovered) {
                    lastZipperPointerClickTime = GetTime();
                    isZipperPointerFeedbackSuppressed = false;
                }
                else {
                    zipperPointerSelected = false;
                    isZipperPointerFeedbackSuppressed = true;
                }
                if (isZipperPointerHovered) zipperPointerSelected = true;
            }
        } else {
            zipperPointerSelected = false;
        }
        DrawRpgWorld(&player, &npc, &stage, camera, cameraFollowsPlayer, currentMap, canTalk,
                     &dialogue, dialogueIndex, stage3IntroIndex, &stage3Event, &zipper, zipperTexture, fileTexture, &inspect, &zipper.inspect,
                     inspectTarget, inspectFunctionIndex, inspectLineIndex, false, 0.0f, RPG_INSPECT_MOVE_PLAYER,
                     npcInspectCompleted, zipperInspectCompleted,
                     isZipperPointerHovered && !isZipperPointerFeedbackSuppressed, zipperPointerSelected,
                     &items, &referenceDrops, &wires, &receivers, &attachments, &dataShots, &events,
                     itemMessage, itemMessageTimer, nearbyReferenceTarget,
                     referenceFileName, referenceText, isReferenceTextOpen,
                     hoveredReferencePointerTarget, selectedReferencePointerTarget,
                     isReferencePointerFeedbackSuppressed, isReferenceDragActive,
                     draggedReferenceTarget, referenceDragPosition,
                     zipperAnimationElapsed, &stageBackground,
                     layout.backgroundBrightness, layout.blockBrightness, isZipperLaunched);
    }
#endif
    RpgStageBuild_Close();
    RpgObjectFolders_ClearSessionStorage();
    RpgObjectFolders_EndStageBuild();
    RpgImageObjects_UnloadTextures();
    RpgCharacter_UnloadPlayerSprites();
    UnloadTexture(zipperTexture);
    UnloadTexture(fileTexture);
    RpgStageBackground_Unload(&stageBackground);
    RpgViewport_Shutdown();
    RpgScene_Release(&scene);
    GameFont_Unload();
    CloseWindow();
    return 0;
}
// 役割: RPG 本編の初期化、ゲーム更新、描画、Zipper 操作を統合するエントリーポイント。
