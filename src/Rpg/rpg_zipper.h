// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_inspect.h
#ifndef RPG_ZIPPER_H
#define RPG_ZIPPER_H

#include "rpg_character.h"
#include "rpg_inspect.h"

typedef struct RpgZipper {
    RpgCharacter character;
    RpgInspect inspect;
    float launchSpeed;
    float returnSpeed;
    float followSpeed;
    bool launchPreviewEnabled;
} RpgZipper;

RpgZipper RpgZipper_Default(void);
bool RpgZipper_Load(const char *filePath, RpgZipper *zipper);
bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper);
Rectangle RpgZipper_GetSpriteBounds(const RpgCharacter *character, float groundY);
void RpgZipper_DrawPointerFeedback(Rectangle bounds, bool isHovered, bool isSelected);

#endif
// 役割: Zipper の設定、境界、入力フィードバック API を宣言する。
