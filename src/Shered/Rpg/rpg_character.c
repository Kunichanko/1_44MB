// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_stage.h
#include "rpg_character.h"
#include "rpg_physics.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "raymath.h"

enum { RPG_CHARACTER_SPRITE_FRAME_SIZE = 32, RPG_CHARACTER_SPRITE_FRAME_COUNT = 9 };
static const float rpgCharacterAnimationFrameDuration = 0.10f;
static Texture2D playerWalkTexture = { 0 };
static Texture2D playerJumpTexture = { 0 };
static Texture2D playerZipgoTexture = { 0 };
static int playerWalkFrameCount = 1;
static int playerJumpFrameCount = 1;
static int playerZipgoFrameCount = 1;
static int playerWalkFrameIndices[RPG_CHARACTER_SPRITE_FRAME_COUNT] = { 0 };
static int playerJumpFrameIndices[RPG_CHARACTER_SPRITE_FRAME_COUNT] = { 0 };
static int playerZipgoFrameIndices[RPG_CHARACTER_SPRITE_FRAME_COUNT] = { 0 };
/* idle 表示に使う walk シート先頭の不透明領域。読み込みに失敗した時だけ控えめな既定値を使う。 */
static Rectangle playerOpaqueSourceBounds = { 4.0f, 1.0f, 24.0f, 31.0f };

static void LoadPlayerOpaqueSourceBounds(const char *texturePath, int sourceFrameIndex)
{
    Image image = LoadImage(texturePath);
    Color *pixels = image.data != NULL ? LoadImageColors(image) : NULL;
    int frameColumns = image.width / RPG_CHARACTER_SPRITE_FRAME_SIZE;
    if (pixels != NULL && frameColumns > 0) {
        int frameX = (sourceFrameIndex % frameColumns) * RPG_CHARACTER_SPRITE_FRAME_SIZE;
        int frameY = (sourceFrameIndex / frameColumns) * RPG_CHARACTER_SPRITE_FRAME_SIZE;
        int left = RPG_CHARACTER_SPRITE_FRAME_SIZE, top = RPG_CHARACTER_SPRITE_FRAME_SIZE;
        int right = -1, bottom = -1;
        for (int y = 0; y < RPG_CHARACTER_SPRITE_FRAME_SIZE; y++) for (int x = 0; x < RPG_CHARACTER_SPRITE_FRAME_SIZE; x++) {
            if (pixels[(frameY + y) * image.width + frameX + x].a == 0) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
        if (right >= left && bottom >= top)
            playerOpaqueSourceBounds = (Rectangle){ (float)left, (float)top,
                                                     (float)(right - left + 1),
                                                     (float)(bottom - top + 1) };
        UnloadImageColors(pixels);
    }
    if (image.data != NULL) UnloadImage(image);
}

/* 末尾に空フレームを含むSpriteシートでも透明フレームを再生しないよう、有効フレーム数を読込時に確定する。 */
static int GetSpriteVisibleFrameIndices(const char *texturePath, int *frameIndices, int capacity)
{
    Image image = LoadImage(texturePath);
    int columns = image.width / RPG_CHARACTER_SPRITE_FRAME_SIZE;
    int rows = image.height / RPG_CHARACTER_SPRITE_FRAME_SIZE;
    int availableCount = columns * rows;
    int visibleCount = 0;
    if (image.data == NULL || columns < 1 || rows < 1 || availableCount < 1) {
        if (image.data != NULL) UnloadImage(image);
        return 1;
    }
    Color *pixels = image.data != NULL ? LoadImageColors(image) : NULL;
    if (pixels != NULL) {
        for (int frame = 0; frame < availableCount; frame++) {
            int frameColumn = frame % columns;
            int frameRow = frame / columns;
            bool hasVisiblePixel = false;
            for (int y = 0; y < RPG_CHARACTER_SPRITE_FRAME_SIZE && !hasVisiblePixel; y++)
                for (int x = 0; x < RPG_CHARACTER_SPRITE_FRAME_SIZE; x++)
                    if (pixels[(frameRow * RPG_CHARACTER_SPRITE_FRAME_SIZE + y) * image.width +
                               frameColumn * RPG_CHARACTER_SPRITE_FRAME_SIZE + x].a != 0) {
                        hasVisiblePixel = true;
                        break;
                    }
            if (hasVisiblePixel && visibleCount < capacity) frameIndices[visibleCount++] = frame;
        }
        UnloadImageColors(pixels);
    }
    UnloadImage(image);
    /* 透明シートだけの場合も、先頭コマを使うため描画側の添字を常に有効にする。 */
    if (visibleCount < 1) {
        frameIndices[0] = 0;
        return 1;
    }
    return visibleCount;
}

/* 主人公用の外部スプライトを一度だけ読み込み、描画ごとのファイルアクセスを防ぐ。 */
bool RpgCharacter_LoadPlayerSprites(void)
{
    if (playerWalkTexture.id != 0 && playerJumpTexture.id != 0 && playerZipgoTexture.id != 0) return true;
    RpgCharacter_UnloadPlayerSprites();
    const char *applicationDirectory = GetApplicationDirectory();
    char walkPath[1024];
    char jumpPath[1024];
    char zipgoPath[1024];
    snprintf(walkPath, sizeof(walkPath), "%s../assets/Sprite/EXE/EXE_walk.png", applicationDirectory);
    snprintf(jumpPath, sizeof(jumpPath), "%s../assets/Sprite/EXE/EXE_jump.png", applicationDirectory);
    snprintf(zipgoPath, sizeof(zipgoPath), "%s../assets/Sprite/EXE/EXE_zipgo.png", applicationDirectory);
    playerWalkTexture = LoadTexture(walkPath);
    playerJumpTexture = LoadTexture(jumpPath);
    playerZipgoTexture = LoadTexture(zipgoPath);
    if (playerWalkTexture.id != 0 && playerJumpTexture.id != 0 && playerZipgoTexture.id != 0) {
        // マス拡大時もスプライトの輪郭をぼかさず、背景・ブロックと同じ表示ピクセルへそろえる。
        SetTextureFilter(playerWalkTexture, TEXTURE_FILTER_POINT);
        SetTextureFilter(playerJumpTexture, TEXTURE_FILTER_POINT);
        SetTextureFilter(playerZipgoTexture, TEXTURE_FILTER_POINT);
        /* GPU読み戻しではなく元PNGを走査し、透明フレームを一覧から完全に除外する。 */
        playerWalkFrameCount = GetSpriteVisibleFrameIndices(walkPath, playerWalkFrameIndices,
                                                             RPG_CHARACTER_SPRITE_FRAME_COUNT);
        playerJumpFrameCount = GetSpriteVisibleFrameIndices(jumpPath, playerJumpFrameIndices,
                                                             RPG_CHARACTER_SPRITE_FRAME_COUNT);
        playerZipgoFrameCount = GetSpriteVisibleFrameIndices(zipgoPath, playerZipgoFrameIndices,
                                                              RPG_CHARACTER_SPRITE_FRAME_COUNT);
        LoadPlayerOpaqueSourceBounds(walkPath, playerWalkFrameIndices[0]);
        return true;
    }
    RpgCharacter_UnloadPlayerSprites();
    return false;
}

void RpgCharacter_UnloadPlayerSprites(void)
{
    if (playerWalkTexture.id != 0) UnloadTexture(playerWalkTexture);
    if (playerJumpTexture.id != 0) UnloadTexture(playerJumpTexture);
    if (playerZipgoTexture.id != 0) UnloadTexture(playerZipgoTexture);
    playerWalkTexture = (Texture2D){ 0 };
    playerJumpTexture = (Texture2D){ 0 };
    playerZipgoTexture = (Texture2D){ 0 };
    playerWalkFrameCount = playerJumpFrameCount = playerZipgoFrameCount = 1;
}

RpgCharacter RpgCharacter_Create(Vector2 position, Color shirtColor, Color hairColor)
{
    return (RpgCharacter){ .position = position, .isGrounded = true, .moveSpeed = 180.0f,
                           .scale = 1.0f, .facingDirection = 1,
                           .shirtColor = shirtColor, .hairColor = hairColor };
}

void RpgCharacter_SetUsesPlayerSpriteCollision(RpgCharacter *character, bool enabled)
{
    if (character != NULL) character->usesPlayerSpriteCollision = enabled;
}

void RpgCharacter_ResetAnimation(RpgCharacter *character)
{
    if (character != NULL) character->animationElapsed = 0.0f;
}

/* 移動・跳躍開始時だけコマを先頭へ戻し、継続中は同じ時間軸でアニメーションする。 */
static void UpdateAnimationState(RpgCharacter *character, Vector2 previousPosition,
                                 bool wasGrounded, float deltaTime)
{
    bool wasMoving = character->isMoving;
    float horizontalMovement = character->position.x - previousPosition.x;
    character->isMoving = fabsf(horizontalMovement) > 0.01f;
    if (character->isMoving) character->facingDirection = horizontalMovement < 0.0f ? -1 : 1;
    if ((!wasMoving && character->isMoving) || (wasGrounded && !character->isGrounded))
        RpgCharacter_ResetAnimation(character);
    else character->animationElapsed += deltaTime;
}

Rectangle RpgCharacter_GetCollisionBounds(const RpgCharacter *character)
{
    if (character == NULL) return (Rectangle){ 0 };
    if (character->usesPlayerSpriteCollision) {
        float size = RPG_STAGE_TILE_SIZE * Clamp(character->scale, 0.5f, 1.0f);
        float pixelScale = size / RPG_CHARACTER_SPRITE_FRAME_SIZE;
        float left = playerOpaqueSourceBounds.x;
        if (character->facingDirection < 0)
            left = RPG_CHARACTER_SPRITE_FRAME_SIZE - playerOpaqueSourceBounds.x - playerOpaqueSourceBounds.width;
        return (Rectangle){ character->position.x - size * 0.5f + left * pixelScale,
                            character->position.y - size + playerOpaqueSourceBounds.y * pixelScale,
                            playerOpaqueSourceBounds.width * pixelScale,
                            playerOpaqueSourceBounds.height * pixelScale };
    }
    return (Rectangle){ character->position.x - RPG_STAGE_TILE_SIZE * 0.5f,
                        character->position.y - RPG_STAGE_TILE_SIZE,
                        RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
}

Rectangle RpgCharacter_GetVisualBounds(const RpgCharacter *character)
{
    if (character == NULL) return (Rectangle){ 0 };
    float size = RPG_STAGE_TILE_SIZE * Clamp(character->scale, 0.5f, 1.0f);
    /* 人型本体は幅54%、足元の影は幅76%。1マスの物理判定を流用せず、実際に描いている範囲だけを返す。 */
    float width = size * 0.76f;
    return (Rectangle){ character->position.x - width * 0.5f, character->position.y - size - 16.0f,
                        width, size + 21.0f };
}

static bool RpgCharacter_CheckMovingSolidCollision(Rectangle bounds,
                                                    const RpgMovingSolidSet *movingSolids)
{
    if (movingSolids == NULL || movingSolids->entries == NULL) return false;
    for (int index = 0; index < movingSolids->count; index++)
        if (CheckCollisionRecs(bounds, movingSolids->entries[index].bounds)) return true;
    return false;
}

static bool RpgCharacter_CheckMovingSolidCollisionCallback(void *context, Rectangle bounds)
{
    return RpgCharacter_CheckMovingSolidCollision(bounds, (const RpgMovingSolidSet *)context);
}

static bool RpgCharacter_MoveAxis(RpgCharacter *character, const RpgStage *stage,
                                  const RpgMovingSolidSet *movingSolids, float amount,
                                  bool isVertical)
{
    // 大きなフレーム時間でも壁を飛び越えないよう、移動を小さな単位に分けて判定する。
    const float maximumStep = 4.0f;
    float remaining = fabsf(amount);
    float direction = amount < 0.0f ? -1.0f : 1.0f;
    while (remaining > 0.0f) {
        float step = direction * (remaining > maximumStep ? maximumStep : remaining);
        Rectangle previousBounds = RpgCharacter_GetCollisionBounds(character);
        if (isVertical) character->position.y += step;
        else character->position.x += step;
        /* 一方向床は下向きに上辺を通過した時だけ、足元を床面へ正確にそろえる。 */
        if (isVertical && step > 0.0f) {
            float landingY;
            if (RpgStage_FindOneWayPlatformLanding(stage, previousBounds,
                                                    RpgCharacter_GetCollisionBounds(character), &landingY)) {
                character->position.y = landingY;
                return true;
            }
        }
        if (RpgStage_CheckSolidCollision(stage, RpgCharacter_GetCollisionBounds(character)) ||
            RpgCharacter_CheckMovingSolidCollision(RpgCharacter_GetCollisionBounds(character),
                                                    movingSolids)) {
            if (isVertical) character->position.y -= step;
            else character->position.x -= step;
            return true;
        }
        remaining -= fabsf(step);
    }
    return false;
}

static bool RpgCharacter_HasGroundBelow(const RpgCharacter *character, const RpgStage *stage,
                                        const RpgMovingSolidSet *movingSolids)
{
    // 接地中の静止状態でも、足元のブロックが削除されたら自然に落下へ移行する。
    RpgCharacter probe = *character;
    Rectangle currentBounds = RpgCharacter_GetCollisionBounds(character);
    probe.position.y += 1.0f;
    Rectangle probeBounds = RpgCharacter_GetCollisionBounds(&probe);
    return RpgStage_CheckSolidCollision(stage, probeBounds) ||
           RpgCharacter_CheckMovingSolidCollision(probeBounds, movingSolids) ||
           RpgStage_FindOneWayPlatformLanding(stage, currentBounds, probeBounds, NULL);
}

/* Characters and movable blocks now use this same shared axis/one-way physics. */
static bool RpgCharacter_MoveAxisShared(RpgCharacter *character, const RpgStage *stage,
                                        const RpgMovingSolidSet *movingSolids, float amount,
                                        bool isVertical)
{
    Rectangle bounds = RpgCharacter_GetCollisionBounds(character);
    Rectangle localBounds = { bounds.x - character->position.x, bounds.y - character->position.y,
                              bounds.width, bounds.height };
    return RpgPhysics_MoveAxis(stage, &character->position, localBounds, amount, isVertical,
                               RpgCharacter_CheckMovingSolidCollisionCallback, (void *)movingSolids);
}

static bool RpgCharacter_HasGroundBelowShared(const RpgCharacter *character, const RpgStage *stage,
                                              const RpgMovingSolidSet *movingSolids)
{
    Rectangle bounds = RpgCharacter_GetCollisionBounds(character);
    Rectangle localBounds = { bounds.x - character->position.x, bounds.y - character->position.y,
                              bounds.width, bounds.height };
    return RpgPhysics_HasGroundBelow(stage, character->position, localBounds,
                                     RpgCharacter_CheckMovingSolidCollisionCallback, (void *)movingSolids);
}

void RpgCharacter_UpdatePlayerWithStage(RpgCharacter *character, float deltaTime,
                                        const RpgStage *stage, float minimumX, float maximumX)
{
    RpgCharacter_UpdatePlayerWithStageAndMovingSolids(character, deltaTime, stage, NULL,
                                                       minimumX, maximumX);
}

void RpgCharacter_UpdatePlayerWithStageAndMovingSolids(RpgCharacter *character, float deltaTime,
                                                       const RpgStage *stage,
                                                       const RpgMovingSolidSet *movingSolids,
                                                       float minimumX, float maximumX)
{
    RpgCharacter_UpdatePlayerWithStageAndMovingSolidsControlled(character, deltaTime, stage,
                                                                 movingSolids, minimumX, maximumX,
                                                                 true, true);
}

void RpgCharacter_UpdatePlayerWithStageAndMovingSolidsControlled(RpgCharacter *character, float deltaTime,
                                                                 const RpgStage *stage,
                                                                 const RpgMovingSolidSet *movingSolids,
                                                                 float minimumX, float maximumX,
                                                                 bool allowHorizontalInput, bool allowJumpInput)
{
    Vector2 previousPosition = character->position;
    bool wasGrounded = character->isGrounded;
    float direction = 0.0f;
    if (allowHorizontalInput) {
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) direction -= 1.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) direction += 1.0f;
    }

    RpgCharacter_MoveAxisShared(character, stage, movingSolids,
                          direction * character->moveSpeed * deltaTime, false);
    character->position.x = Clamp(character->position.x, minimumX, maximumX);
    if (allowJumpInput && character->isGrounded && IsKeyPressed(KEY_W)) {
        character->verticalSpeed = -460.0f;
        character->isGrounded = false;
    }

    character->verticalSpeed += 1200.0f * deltaTime;
    bool collidedVertically = RpgCharacter_MoveAxisShared(character, stage, movingSolids,
                                                     character->verticalSpeed * deltaTime, true);
    if (collidedVertically) {
        character->isGrounded = character->verticalSpeed > 0.0f;
        character->verticalSpeed = 0.0f;
    } else if (character->verticalSpeed >= 0.0f &&
               RpgCharacter_HasGroundBelowShared(character, stage, movingSolids)) {
        character->isGrounded = true;
        character->verticalSpeed = 0.0f;
    } else {
        character->isGrounded = false;
    }
    UpdateAnimationState(character, previousPosition, wasGrounded, deltaTime);
}

static bool RpgCharacter_OverlapsHorizontally(Rectangle first, Rectangle second)
{
    return first.x < second.x + second.width && first.x + first.width > second.x;
}

static void RpgCharacter_ResolveSingleMovingSolid(RpgCharacter *character, const RpgStage *stage,
                                                   RpgMovingSolid solid)
{
    Rectangle playerBounds = RpgCharacter_GetCollisionBounds(character);
    float deltaX = solid.bounds.x - solid.previousBounds.x;
    float deltaY = solid.bounds.y - solid.previousBounds.y;
    bool wasStandingOnSolid = RpgCharacter_OverlapsHorizontally(playerBounds, solid.previousBounds) &&
        fabsf((playerBounds.y + playerBounds.height) - solid.previousBounds.y) <= 2.0f;

    /* 横に動いた足場へ接地している時だけ、足場と同じ量をプレイヤーへ渡す。 */
    if (wasStandingOnSolid && fabsf(deltaX) > 0.0001f) {
        character->position.x += deltaX;
        playerBounds = RpgCharacter_GetCollisionBounds(character);
        if (RpgStage_CheckSolidCollision(stage, playerBounds)) {
            character->position.x -= deltaX;
            playerBounds = RpgCharacter_GetCollisionBounds(character);
        }
    }

    if (!CheckCollisionRecs(playerBounds, solid.bounds)) return;

    float pushLeft = solid.bounds.x - (playerBounds.x + playerBounds.width);
    float pushRight = solid.bounds.x + solid.bounds.width - playerBounds.x;
    float pushUp = solid.bounds.y - (playerBounds.y + playerBounds.height);
    float pushDown = solid.bounds.y + solid.bounds.height - playerBounds.y;
    float horizontalPush = fabsf(pushLeft) < fabsf(pushRight) ? pushLeft : pushRight;
    float verticalPush = fabsf(pushUp) < fabsf(pushDown) ? pushUp : pushDown;

    /* 動いた方向を優先しつつ、常にXまたはYの一軸だけで重なりを外す。 */
    bool preferHorizontal = fabsf(deltaX) > fabsf(deltaY) ? true :
                            fabsf(deltaY) > fabsf(deltaX) ? false :
                            fabsf(horizontalPush) <= fabsf(verticalPush);
    const bool horizontalCandidates[2] = { preferHorizontal, !preferHorizontal };
    const float pushes[2] = { preferHorizontal ? horizontalPush : verticalPush,
                              preferHorizontal ? verticalPush : horizontalPush };
    /* 一軸だけで押し出す。優先方向が地形で塞がれた場合のみ、もう一方を試す。 */
    for (int candidate = 0; candidate < 2; candidate++) {
        bool horizontal = horizontalCandidates[candidate];
        float push = pushes[candidate];
        if (horizontal) character->position.x += push;
        else character->position.y += push;
        if (!RpgStage_CheckSolidCollision(stage, RpgCharacter_GetCollisionBounds(character))) {
            if (!horizontal) {
                if (push < 0.0f) {
                    character->isGrounded = true;
                    character->verticalSpeed = 0.0f;
                } else if (character->verticalSpeed < 0.0f) {
                    character->verticalSpeed = 0.0f;
                }
            }
            break;
        }
        if (horizontal) character->position.x -= push;
        else character->position.y -= push;
    }
}

void RpgCharacter_ResolveMovingSolidContacts(RpgCharacter *character, const RpgStage *stage,
                                             const RpgMovingSolidSet *movingSolids)
{
    if (character == NULL || stage == NULL || movingSolids == NULL || movingSolids->entries == NULL)
        return;
    for (int index = 0; index < movingSolids->count; index++)
        RpgCharacter_ResolveSingleMovingSolid(character, stage, movingSolids->entries[index]);
}

void RpgCharacter_UpdatePlayer(RpgCharacter *character, float deltaTime, float groundY,
                               float minimumX, float maximumX)
{
    Vector2 previousPosition = character->position;
    bool wasGrounded = character->isGrounded;
    float direction = 0.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) direction -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) direction += 1.0f;
    character->position.x = Clamp(character->position.x + direction * character->moveSpeed * deltaTime,
                                  minimumX, maximumX);
    if (character->isGrounded && IsKeyPressed(KEY_W)) {
        character->verticalSpeed = -460.0f;
        character->isGrounded = false;
    }
    character->verticalSpeed += 1200.0f * deltaTime;
    character->position.y += character->verticalSpeed * deltaTime;
    if (character->position.y >= groundY) {
        character->position.y = groundY;
        character->verticalSpeed = 0.0f;
        character->isGrounded = true;
    }
    UpdateAnimationState(character, previousPosition, wasGrounded, deltaTime);
}

bool RpgCharacter_IsNear(const RpgCharacter *first, const RpgCharacter *second, float distance)
{
    return Vector2Distance(first->position, second->position) <= distance;
}

Rectangle RpgCharacter_GetFootBounds(const RpgCharacter *character)
{
    return RpgCharacter_GetCollisionBounds(character);
}

/* 主人公はEXEスプライトを状態別に使い、通常時はwalkシート先頭コマを固定表示する。 */
void RpgCharacter_DrawPlayerTinted(const RpgCharacter *character, RpgCharacterAnimation animation, Color tint)
{
    if (character == NULL || playerWalkTexture.id == 0) {
        if (character != NULL) RpgCharacter_DrawTinted(character, "Hero", tint);
        return;
    }
    if (animation == RPG_CHARACTER_ANIMATION_AUTOMATIC)
        animation = !character->isGrounded ? RPG_CHARACTER_ANIMATION_JUMP :
                    character->isMoving ? RPG_CHARACTER_ANIMATION_WALK : RPG_CHARACTER_ANIMATION_IDLE;

    Texture2D texture = playerWalkTexture;
    int animationFrameCount = playerWalkFrameCount;
    const int *visibleFrameIndices = playerWalkFrameIndices;
    if (animation == RPG_CHARACTER_ANIMATION_JUMP) {
        texture = playerJumpTexture;
        animationFrameCount = playerJumpFrameCount;
        visibleFrameIndices = playerJumpFrameIndices;
    } else if (animation == RPG_CHARACTER_ANIMATION_ZIPGO) {
        texture = playerZipgoTexture;
        animationFrameCount = playerZipgoFrameCount;
        visibleFrameIndices = playerZipgoFrameIndices;
    }

    /* シートの実寸から有効フレーム数を求め、範囲外の透明領域を一瞬描かないようにする。 */
    int frameColumns = texture.width / RPG_CHARACTER_SPRITE_FRAME_SIZE;
    int frameRows = texture.height / RPG_CHARACTER_SPRITE_FRAME_SIZE;
    int frameCount = frameColumns * frameRows;
    if (frameColumns < 1 || frameRows < 1 || frameCount < 1) {
        RpgCharacter_DrawTinted(character, "Hero", tint);
        return;
    }
    frameCount = Clamp(animationFrameCount, 1, frameCount);
    int frameIndex = 0;
    if (animation != RPG_CHARACTER_ANIMATION_IDLE) {
        int elapsedFrame = (int)floorf(fmaxf(0.0f, character->animationElapsed) /
                                       rpgCharacterAnimationFrameDuration);
        frameIndex = elapsedFrame % frameCount;
    }
    /* frameIndexは有効フレーム列の添字。元シート上の透明コマは決して選択しない。 */
    int sourceFrameIndex = visibleFrameIndices[frameIndex];
    Rectangle source = { (float)((sourceFrameIndex % frameColumns) * RPG_CHARACTER_SPRITE_FRAME_SIZE),
                         (float)((sourceFrameIndex / frameColumns) * RPG_CHARACTER_SPRITE_FRAME_SIZE),
                         RPG_CHARACTER_SPRITE_FRAME_SIZE, RPG_CHARACTER_SPRITE_FRAME_SIZE };
    // スプライト原画を複製せず、最後に進めた方向に合わせてソース矩形だけを左右反転する。
    float size = RpgStage_SnapRenderCoordinate(RPG_STAGE_TILE_SIZE * Clamp(character->scale, 0.5f, 1.0f));
    Vector2 position = RpgStage_SnapRenderPoint(character->position);
    Rectangle destination = RpgStage_SnapRenderRectangle(
        (Rectangle){ position.x - size * 0.5f, position.y - size, size, size });
    /* raylibは負幅ソースを同じx位置から左右反転する。フレーム境界を越えないため、
       xをずらさず幅だけを反転する。 */
    if (character->facingDirection < 0) {
        source.width = -source.width;
    }
    DrawEllipse((int)position.x, (int)position.y + 2,
                (int)(size * 0.38f), (int)(size * 0.10f), Fade(BLACK, 0.18f * tint.a / 255.0f));
    DrawTexturePro(texture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, tint);
    DrawText("Hero", (int)position.x - MeasureText("Hero", 16) / 2,
             (int)destination.y - 16, 16, Fade(DARKGRAY, tint.a / 255.0f));
}

void RpgCharacter_DrawPlayer(const RpgCharacter *character, RpgCharacterAnimation animation)
{
    RpgCharacter_DrawPlayerTinted(character, animation, WHITE);
}

void RpgCharacter_DrawTinted(const RpgCharacter *character, const char *name, Color tint)
{
    Vector2 position = RpgStage_SnapRenderPoint(character->position);
    float scale = Clamp(character->scale, 0.5f, 1.0f);
    float size = RpgStage_SnapRenderCoordinate(RPG_STAGE_TILE_SIZE * scale);
    float top = position.y - size;
    DrawEllipse((int)position.x, (int)position.y + 2, (int)(size * 0.38f), (int)(size * 0.10f), Fade(BLACK, 0.18f * tint.a / 255.0f));
    DrawRectangle((int)(position.x - size * 0.27f), (int)(top + size * 0.42f), (int)(size * 0.54f), (int)(size * 0.31f), Fade(character->shirtColor, tint.a / 255.0f));
    DrawRectangle((int)(position.x - size * 0.24f), (int)(top + size * 0.73f), (int)(size * 0.20f), (int)(size * 0.27f), Fade(DARKBLUE, tint.a / 255.0f));
    DrawRectangle((int)(position.x + size * 0.04f), (int)(top + size * 0.73f), (int)(size * 0.20f), (int)(size * 0.27f), Fade(DARKBLUE, tint.a / 255.0f));
    DrawCircle((int)position.x, (int)(top + size * 0.25f), size * 0.22f, Fade((Color){ 255, 220, 185, 255 }, tint.a / 255.0f));
    DrawCircleSector((Vector2){ position.x, top + size * 0.25f }, size * 0.24f, 190.0f, 350.0f, 16,
                     Fade(character->hairColor, tint.a / 255.0f));
    DrawText(name, (int)position.x - MeasureText(name, 16) / 2, (int)top - 16, 16,
             Fade(DARKGRAY, tint.a / 255.0f));
}

void RpgCharacter_Draw(const RpgCharacter *character, const char *name)
{
    RpgCharacter_DrawTinted(character, name, WHITE);
}
// 役割: RPG キャラクターの移動、接地判定、描画を管理する。
