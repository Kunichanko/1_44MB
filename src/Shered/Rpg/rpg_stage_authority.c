#include "rpg_stage_authority.h"

static RpgStageAuthority activeAuthority = RPG_STAGE_AUTHORITY_NONE;

void RpgStageAuthority_Enter(RpgStageAuthority authority)
{
    activeAuthority = authority;
}

RpgStageAuthority RpgStageAuthority_Get(void)
{
    return activeAuthority;
}

bool RpgStageAuthority_CanWriteStatic(void)
{
    return activeAuthority == RPG_STAGE_AUTHORITY_EDITOR_STATIC;
}

bool RpgStageAuthority_CanWriteEditorRuntime(void)
{
    return activeAuthority == RPG_STAGE_AUTHORITY_EDITOR_RUNTIME;
}

bool RpgStageAuthority_CanWriteGameRuntime(void)
{
    return activeAuthority == RPG_STAGE_AUTHORITY_GAME_RUNTIME;
}
