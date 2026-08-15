// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_inspect.h
#ifndef RPG_ZIPPER_H
#define RPG_ZIPPER_H

#include "rpg_character.h"
#include "rpg_inspect.h"

typedef struct RpgZipper {
    RpgCharacter character;
    RpgInspect inspect;
} RpgZipper;

RpgZipper RpgZipper_Default(void);
bool RpgZipper_Load(const char *filePath, RpgZipper *zipper);
bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper);

#endif
