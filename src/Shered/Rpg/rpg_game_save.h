// 役割: 旗で記録するゲーム進行（復帰ステージ・旗ID・Zipper接続状態）を保存する。
// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_GAME_SAVE_H
#define RPG_GAME_SAVE_H

#include <stdbool.h>

typedef struct RpgGameSave {
    bool isValid;
    int stageNumber;
    int flagId;
    bool zipperConnected;
} RpgGameSave;

RpgGameSave RpgGameSave_Default(void);
bool RpgGameSave_Load(RpgGameSave *save);
bool RpgGameSave_Save(const RpgGameSave *save);

#endif
