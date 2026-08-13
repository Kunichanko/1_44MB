// 依存する自プロジェクト内ファイル: rpg_character.h
#include "rpg_character.h"

#include "raymath.h"

RpgCharacter RpgCharacter_Create(Vector2 position, Color shirtColor, Color hairColor)
{
    return (RpgCharacter){ .position = position, .isGrounded = true, .moveSpeed = 180.0f,
                           .scale = 1.0f,
                           .shirtColor = shirtColor, .hairColor = hairColor };
}

void RpgCharacter_UpdatePlayer(RpgCharacter *character, float deltaTime, float groundY,
                               float minimumX, float maximumX)
{
    float direction = 0.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) direction -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) direction += 1.0f;
    character->position.x = Clamp(character->position.x + direction * character->moveSpeed * deltaTime,
                                  minimumX, maximumX);
    if (character->isGrounded && (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) ||
                                  IsKeyPressed(KEY_SPACE))) {
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
