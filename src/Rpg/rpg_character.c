// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_stage.h
#include "rpg_character.h"

#include <math.h>

#include "raymath.h"

RpgCharacter RpgCharacter_Create(Vector2 position, Color shirtColor, Color hairColor)
{
    return (RpgCharacter){ .position = position, .isGrounded = true, .moveSpeed = 180.0f,
                           .scale = 1.0f,
                           .shirtColor = shirtColor, .hairColor = hairColor };
}

Rectangle RpgCharacter_GetCollisionBounds(const RpgCharacter *character)
{
    float scale = character->scale;
    return (Rectangle){ character->position.x - 10.0f * scale,
                        character->position.y - 70.0f * scale,
                        20.0f * scale, 70.0f * scale };
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
}

void RpgCharacter_UpdatePlayer(RpgCharacter *character, float deltaTime, float groundY,
                               float minimumX, float maximumX)
{
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
}

bool RpgCharacter_IsNear(const RpgCharacter *first, const RpgCharacter *second, float distance)
{
    return Vector2Distance(first->position, second->position) <= distance;
}

Rectangle RpgCharacter_GetFootBounds(const RpgCharacter *character)
{
    return (Rectangle){ character->position.x - 12.0f, character->position.y - 18.0f,
                        24.0f, 18.0f };
}

void RpgCharacter_Draw(const RpgCharacter *character, const char *name)
{
    Vector2 position = character->position;
    float scale = character->scale;
    DrawEllipse((int)position.x, (int)position.y + (int)(4 * scale), (int)(19 * scale), (int)(5 * scale), Fade(BLACK, 0.18f));
    DrawRectangle((int)(position.x - 12 * scale), (int)(position.y - 46 * scale), (int)(24 * scale), (int)(26 * scale), character->shirtColor);
    DrawRectangle((int)(position.x - 11 * scale), (int)(position.y - 20 * scale), (int)(9 * scale), (int)(20 * scale), DARKBLUE);
    DrawRectangle((int)(position.x + 2 * scale), (int)(position.y - 20 * scale), (int)(9 * scale), (int)(20 * scale), DARKBLUE);
    DrawCircle((int)position.x, (int)(position.y - 58 * scale), 13.0f * scale, (Color){ 255, 220, 185, 255 });
    DrawCircleSector((Vector2){ position.x, position.y - 58.0f * scale }, 14.0f * scale, 190.0f, 350.0f, 16,
                     character->hairColor);
    DrawText(name, (int)position.x - MeasureText(name, 16) / 2, (int)position.y - 90, 16, DARKGRAY);
}
// 役割: RPG キャラクターの移動、接地判定、描画を管理する。
