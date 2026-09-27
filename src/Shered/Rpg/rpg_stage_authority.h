#ifndef RPG_STAGE_AUTHORITY_H
#define RPG_STAGE_AUTHORITY_H

/*
 * Stage data has exactly one authoring owner: the editor's static Settings
 * tree.  Play may only modify its own build/Stage runtime tree.  This small
 * process-local gate makes crossing that boundary an intentional operation
 * instead of an accidental call to a storage helper.
 */
typedef enum RpgStageAuthority {
    RPG_STAGE_AUTHORITY_NONE = 0,
    RPG_STAGE_AUTHORITY_EDITOR_STATIC,
    RPG_STAGE_AUTHORITY_EDITOR_RUNTIME,
    RPG_STAGE_AUTHORITY_GAME_RUNTIME
} RpgStageAuthority;

void RpgStageAuthority_Enter(RpgStageAuthority authority);
RpgStageAuthority RpgStageAuthority_Get(void);
bool RpgStageAuthority_CanWriteStatic(void);
bool RpgStageAuthority_CanWriteEditorRuntime(void);
bool RpgStageAuthority_CanWriteGameRuntime(void);

#endif
