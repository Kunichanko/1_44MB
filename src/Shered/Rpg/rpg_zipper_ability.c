// Responsibility: interpret known .ability filenames in the active Zipper folder.
#include "rpg_zipper_ability.h"

#include "rpg_object_folder.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
/* rpg_object_folder.h reaches raylib first; omit the conflicting Win32 UI
   declarations because this module only needs filesystem APIs. */
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

typedef struct RpgZipperAbilityDefinition {
    const char *fileName;
    RpgZipperAbilityId ability;
} RpgZipperAbilityDefinition;

static const RpgZipperAbilityDefinition abilityDefinitions[] = {
    { "double_jump.ability", RPG_ZIPPER_ABILITY_DOUBLE_JUMP }
};

static RpgZipperAbilityState abilityState;
static unsigned int observedStorageSerial = UINT_MAX;

static void SetAbilityEnabled(RpgZipperAbilityId ability)
{
    if (ability == RPG_ZIPPER_ABILITY_DOUBLE_JUMP) abilityState.doubleJumpEnabled = true;
}

#ifdef _WIN32
static bool ToWide(const char *text, wchar_t *wide, int count)
{
    return text != NULL && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count) > 0;
}

static bool IsKnownAbilityFile(const wchar_t *name, RpgZipperAbilityId *ability)
{
    char utf8[260];
    if (name == NULL || ability == NULL ||
        WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, (int)sizeof(utf8), NULL, NULL) <= 0)
        return false;
    for (int index = 0; index < (int)(sizeof(abilityDefinitions) / sizeof(abilityDefinitions[0])); index++) {
        if (_stricmp(utf8, abilityDefinitions[index].fileName) == 0) {
            *ability = abilityDefinitions[index].ability;
            return true;
        }
    }
    return false;
}

/* Eaten file objects reside inside their object folder, so this is recursive.
   It is reached only from the watcher-change path, never every frame. */
static void ScanDirectoryForAbilities(const wchar_t *directory, int depth)
{
    WIN32_FIND_DATAW found;
    wchar_t search[1200];
    HANDLE handle;
    if (directory == NULL || depth > 32 || swprintf(search, 1200, L"%ls\\*", directory) < 0)
        return;
    handle = FindFirstFileW(search, &found);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            wchar_t child[1200];
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                swprintf(child, 1200, L"%ls\\%ls", directory, found.cFileName) >= 0)
                ScanDirectoryForAbilities(child, depth + 1);
        } else {
            RpgZipperAbilityId ability;
            if (IsKnownAbilityFile(found.cFileName, &ability)) SetAbilityEnabled(ability);
        }
    } while (FindNextFileW(handle, &found) != 0);
    FindClose(handle);
}
#endif

void RpgZipperAbilities_Update(void)
{
    const unsigned int currentSerial = RpgObjectFolder_GetZipperStorageChangeSerial();
    char root[1200];
    if (observedStorageSerial == currentSerial) return;
    observedStorageSerial = currentSerial;
    memset(&abilityState, 0, sizeof(abilityState));
#ifdef _WIN32
    {
        wchar_t wideRoot[1200];
        if (RpgObjectFolder_GetZipperInboxDirectory(root, sizeof(root)) &&
            ToWide(root, wideRoot, (int)(sizeof(wideRoot) / sizeof(wideRoot[0]))))
            ScanDirectoryForAbilities(wideRoot, 0);
    }
#endif
}

const RpgZipperAbilityState *RpgZipperAbilities_GetState(void)
{
    return &abilityState;
}

bool RpgZipperAbilities_IsEnabled(RpgZipperAbilityId ability)
{
    if (ability == RPG_ZIPPER_ABILITY_DOUBLE_JUMP) return abilityState.doubleJumpEnabled;
    return false;
}
