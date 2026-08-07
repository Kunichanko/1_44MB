// 依存: player.h
#include "player.h"
#include "raymath.h"

enum {
    ANIMATION_FRAME_WIDTH = 32,
    ANIMATION_FRAME_HEIGHT = 40,
    ANIMATION_FRAME_COUNT = 5,
};

static const float ANIMATION_FRAME_DURATION = 0.12f;

Player Player_Create(Vector2 startPosition)
{
    Player player = {
        .position = startPosition,
        .width = 48.0f,
        .height = 60.0f,
        .moveSpeed = 300.0f,
        .appearance = {0},
        .hasAppearance = false,
        .defaultAppearance = {0},
        .hasDefaultAppearance = false,
        .animationFrame = 0,
        .animationElapsed = 0.0f,
    };

    return player;
}

bool Player_LoadDefaultAppearance(Player *player, const char *filePath)
{
    Texture2D texture = LoadTexture(filePath);
    if (!IsTextureValid(texture)) {
        return false;
    }

    Player_UnloadDefaultAppearance(player);
    player->defaultAppearance = texture;
    player->hasDefaultAppearance = true;
    return true;
}

void Player_Update(Player *player, float deltaTime, float worldWidth)
{
    float direction = 0.0f;

    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
        direction -= 1.0f;
    }
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
        direction += 1.0f;
    }

    player->position.x += direction * player->moveSpeed * deltaTime;

    // 移動中だけスプライトシートの5フレームを順番に進め、停止時は先頭フレームへ戻す。
    if (direction != 0.0f) {
        player->animationElapsed += deltaTime;
        if (player->animationElapsed >= ANIMATION_FRAME_DURATION) {
            player->animationElapsed = 0.0f;
            player->animationFrame = (player->animationFrame + 1) % ANIMATION_FRAME_COUNT;
        }
    } else {
        player->animationFrame = 0;
        player->animationElapsed = 0.0f;
    }

    // プレイヤーがステージ外へ出ないよう、中心座標を移動可能な範囲に制限する。
    player->position.x = Clamp(player->position.x, player->width / 2.0f,
                               worldWidth - player->width / 2.0f);
}

void Player_Draw(const Player *player, float groundY)
{
    Rectangle body = Player_GetBounds(player, groundY);

    if (player->hasAppearance) {
        Rectangle textureSource = { 0.0f, 0.0f, (float)player->appearance.width,
                                    (float)player->appearance.height };
        DrawTexturePro(player->appearance, textureSource, body, Vector2Zero(), 0.0f, WHITE);
        return;
    }

    if (player->hasDefaultAppearance) {
        int column = player->animationFrame % 3;
        int row = player->animationFrame / 3;
        Rectangle textureSource = {
            (float)(column * ANIMATION_FRAME_WIDTH),
            (float)(row * ANIMATION_FRAME_HEIGHT),
            ANIMATION_FRAME_WIDTH,
            ANIMATION_FRAME_HEIGHT,
        };
        DrawTexturePro(player->defaultAppearance, textureSource, body, Vector2Zero(), 0.0f, WHITE);
        return;
    }

    // 仮キャラクターは差し替えやすいよう、単純な図形だけで描画する。
    DrawRectangleRec(body, ORANGE);
    DrawCircle((int)player->position.x, (int)(body.y + 15.0f), 14.0f, YELLOW);
    DrawCircle((int)(player->position.x - 5.0f), (int)(body.y + 12.0f), 2.0f, DARKBROWN);
    DrawCircle((int)(player->position.x + 5.0f), (int)(body.y + 12.0f), 2.0f, DARKBROWN);
}

bool Player_SetAppearance(Player *player, const char *filePath)
{
    Texture2D newAppearance = LoadTexture(filePath);
    if (!IsTextureValid(newAppearance)) {
        return false;
    }

    Player_UnloadAppearance(player);
    player->appearance = newAppearance;
    player->hasAppearance = true;
    return true;
}

void Player_UnloadAppearance(Player *player)
{
    if (player->hasAppearance) {
        UnloadTexture(player->appearance);
        player->appearance = (Texture2D){0};
        player->hasAppearance = false;
    }
}

void Player_UnloadDefaultAppearance(Player *player)
{
    if (player->hasDefaultAppearance) {
        UnloadTexture(player->defaultAppearance);
        player->defaultAppearance = (Texture2D){0};
        player->hasDefaultAppearance = false;
    }
}

Rectangle Player_GetBounds(const Player *player, float groundY)
{
    return (Rectangle){
        player->position.x - player->width / 2.0f,
        groundY - player->height,
        player->width,
        player->height,
    };
}
