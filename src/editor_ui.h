// 依存: player.h
#ifndef EDITOR_UI_H
#define EDITOR_UI_H

#include <stdbool.h>

#include "player.h"

bool EditorUI_DrawInspector(const Player *player, const char *appearancePath,
                            bool selected, const char *message);
void EditorUI_DrawHint(bool selected);

#endif
