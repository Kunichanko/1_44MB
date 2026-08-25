// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_stage.h
#include "rpg_character.h"

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
    (void)character;
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

static bool RpgCharacter_MoveAxis(RpgCharacter *character, const RpgStage *stage,
                                  float amount, bool isVertical)
{
    // 大きなフレーム時間でも壁を飛び越えないよう、移動を小さな単位に分けて判定する。
    const float maximumStep = 4.0f;
    float remaining = fabsf(amount);
    float direction = amount < 0.0f ? -1.0f : 1.0f;
    while (remaining > 0.0f) {
        float step = direction * (remaining > maximumStep ? maximumStep : remaining);
        if (isVertical) character->position.y += step;
        else character->position.x += step;
        if (RpgStage_CheckSolidCollision(stage, RpgCharacter_GetCollisionBounds(character))) {
            if (isVertical) character->position.y -= step;
            else character->position.x -= step;
            return true;
        }
        remaining -= fabsf(step);
    }
    return false;
}

static bool RpgCharacter_HasGroundBelow(const RpgCharacter *character, const RpgStage *stage)
{
    // 接地中の静止状態でも、足元のブロックが削除されたら自然に落下へ移行する。
    RpgCharacter probe = *character;
    probe.position.y += 1.0f;
    return RpgStage_CheckSolidCollision(stage, RpgCharacter_GetCollisionBounds(&probe));
}

void RpgCharacter_UpdatePlayerWithStage(RpgCharacter *character, float deltaTime,
                                        const RpgStage *stage, float minimumX, float maximumX)
{
    Vector2 previousPosition = character->position;
    bool wasGrounded = character->isGrounded;
    float direction = 0.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) direction -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) direction += 1.0f;

    RpgCharacter_MoveAxis(character, stage, direction * character->moveSpeed * deltaTime, false);
    character->position.x = Clamp(character->position.x, minimumX, maximumX);
    if (character->isGrounded && IsKeyPressed(KEY_W)) {
        character->verticalSpeed = -460.0f;
        character->isGrounded = false;
    }

    character->verticalSpeed += 1200.0f * deltaTime;
    bool collidedVertically = RpgCharacter_MoveAxis(character, stage,
                                                     character->verticalSpeed * deltaTime, true);
    if (collidedVertically) {
        character->isGrounded = character->verticalSpeed > 0.0f;
        character->verticalSpeed = 0.0f;
    } else if (character->verticalSpeed >= 0.0f && RpgCharacter_HasGroundBelow(character, stage)) {
        character->isGrounded = true;
        character->verticalSpeed = 0.0f;
    } else {
        character->isGrounded = false;
    }
    UpdateAnimationState(character, previousPosition, wasGrounded, deltaTime);
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
