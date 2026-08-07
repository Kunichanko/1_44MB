// 依存: なし（raylib は外部ライブラリ）
#ifndef STAGE_H
#define STAGE_H

#include "raylib.h"

typedef struct Stage {
    float width;
    float groundY;
} Stage;

Stage Stage_Create(void);
void Stage_Draw(const Stage *stage);

#endif
