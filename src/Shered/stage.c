// 依存: stage.h
#include "stage.h"

Stage Stage_Create(void)
{
    Stage stage = {
        .width = 3200.0f,
        .groundY = 400.0f,
    };

    return stage;
}

void Stage_Draw(const Stage *stage)
{
    DrawRectangle(0, (int)stage->groundY, (int)stage->width, 200, DARKGREEN);

    // スクロールしていることが分かるよう、地面に一定間隔で目印を配置する。
    for (int x = 0; x < (int)stage->width; x += 160) {
        DrawRectangle(x, (int)stage->groundY + 30, 80, 8, GREEN);
    }
}
