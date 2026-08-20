// 依存: ../Shered/player.h、../Shered/enemy_group.h
#ifndef EDITOR_UI_H
#define EDITOR_UI_H

#include <stdbool.h>

#include "player.h"
#include "enemy_group.h"

bool EditorUI_DrawInspector(Player *player, const char *appearancePath,
                            bool selected, bool acceptsInput, const char *message,
                            bool *requestedSave);
bool EditorUI_DrawEnemyInspector(EnemyGroup *group, bool selected, bool acceptsInput,
                                 const char *message);
bool EditorUI_DrawGlobalInspector(float *gridOverlayOpacity, int *selectedTileType,
                                  bool *isGridEditing, bool selected, bool acceptsInput,
                                  const char *message);
bool EditorUI_DrawPlayButton(bool isPlaying);
void EditorUI_DrawGlobalButton(bool selected);
void EditorUI_DrawHint(bool playerSelected, bool enemySelected, bool isPlaying);
void EditorUI_ResetInput(void);

#endif
// 役割: 横スクロール用エディターの共通 UI API を宣言する。
