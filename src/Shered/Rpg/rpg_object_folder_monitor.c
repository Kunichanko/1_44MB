// Responsibility: Win32 change-notification handles shared by runtime objects.
#include "rpg_object_folder_monitor.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct RpgObjectFolderMonitorEntry {
    bool active;
    bool recursive;
    char directory[1200];
    RpgObjectFolderMonitorCallback callback;
    void *context;
#ifdef _WIN32
    HANDLE notification;
#endif
} RpgObjectFolderMonitorEntry;

static RpgObjectFolderMonitorEntry entries[RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT];

#ifdef _WIN32
static bool ToWide(const char *path, wchar_t *wide, int count)
{
    return path != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) > 0;
}

static void CloseEntry(RpgObjectFolderMonitorEntry *entry)
{
    if (entry == NULL) return;
    if (entry->notification != NULL && entry->notification != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(entry->notification);
    entry->notification = NULL;
}

static bool OpenEntry(RpgObjectFolderMonitorEntry *entry)
{
    wchar_t directory[1200];
    if (entry == NULL || !ToWide(entry->directory, directory, 1200)) return false;
    entry->notification = FindFirstChangeNotificationW(
        directory, entry->recursive,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (entry->notification == INVALID_HANDLE_VALUE) {
        entry->notification = NULL;
        return false;
    }
    return true;
}
#endif

static bool IsValidHandle(int handle)
{
    return handle >= 0 && handle < RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT && entries[handle].active;
}

int RpgObjectFolderMonitor_Connect(int handle, const char *directory, bool recursive,
                                   RpgObjectFolderMonitorCallback callback, void *context)
{
    RpgObjectFolderMonitorEntry *entry = NULL;
    bool pathChanged;
    bool created = false;
    if (directory == NULL || directory[0] == '\0' || callback == NULL) return RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
    if (IsValidHandle(handle)) entry = &entries[handle];
    if (entry == NULL) {
        for (int index = 0; index < RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT; index++) {
            if (!entries[index].active) { handle = index; entry = &entries[index]; break; }
        }
        if (entry == NULL) return RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
        memset(entry, 0, sizeof(*entry));
        entry->active = true;
        pathChanged = true;
        created = true;
    } else {
        pathChanged = entry->recursive != recursive || strcmp(entry->directory, directory) != 0;
    }
    if (pathChanged) {
#ifdef _WIN32
        CloseEntry(entry);
#endif
        if (snprintf(entry->directory, sizeof(entry->directory), "%s", directory) <= 0) {
            memset(entry, 0, sizeof(*entry));
            return RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
        }
        entry->recursive = recursive;
#ifdef _WIN32
        (void)OpenEntry(entry);
#endif
    }
    entry->callback = callback;
    entry->context = context;
    /* A first connection and a directory switch are state changes too.  This
     * gives an object one uniform callback path for initial synchronization,
     * recovery after a folder return, and later filesystem notifications. */
    if ((created || pathChanged) && entry->callback != NULL)
        entry->callback(entry->context, entry->directory);
    return handle;
}

void RpgObjectFolderMonitor_Disconnect(int *handle)
{
    if (handle == NULL || !IsValidHandle(*handle)) {
        if (handle != NULL) *handle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
        return;
    }
#ifdef _WIN32
    CloseEntry(&entries[*handle]);
#endif
    memset(&entries[*handle], 0, sizeof(entries[*handle]));
    *handle = RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE;
}

bool RpgObjectFolderMonitor_Poll(int handle)
{
    RpgObjectFolderMonitorEntry *entry;
    bool changed = false;
    if (!IsValidHandle(handle)) return false;
    entry = &entries[handle];
#ifdef _WIN32
    if (entry->notification == NULL) {
        changed = true;
        (void)OpenEntry(entry);
    } else {
        DWORD state = WaitForSingleObject(entry->notification, 0);
        if (state == WAIT_OBJECT_0) {
            changed = true;
            if (FindNextChangeNotification(entry->notification) == 0) {
                CloseEntry(entry);
                (void)OpenEntry(entry);
            }
        } else if (state != WAIT_TIMEOUT) {
            changed = true;
            CloseEntry(entry);
            (void)OpenEntry(entry);
        }
    }
#endif
    if (changed && entry->callback != NULL) entry->callback(entry->context, entry->directory);
    return changed;
}

void RpgObjectFolderMonitor_PollAll(void)
{
    for (int index = 0; index < RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT; index++)
        if (entries[index].active) (void)RpgObjectFolderMonitor_Poll(index);
}

static bool IsSameOrDescendantPath(const char *directory, const char *path)
{
    size_t length;
    if (directory == NULL || path == NULL || directory[0] == '\0' || path[0] == '\0') return false;
#ifdef _WIN32
    if (_stricmp(directory, path) == 0) return true;
#else
    if (strcmp(directory, path) == 0) return true;
#endif
    length = strlen(directory);
    if (length == 0) return false;
#ifdef _WIN32
    if (_strnicmp(directory, path, length) != 0) return false;
#else
    if (strncmp(directory, path, length) != 0) return false;
#endif
    return directory[length] == '\\' || directory[length] == '/' ||
           (path[length] == '\\' || path[length] == '/');
}

void RpgObjectFolderMonitor_NotifyLocalChange(const char *directory)
{
    if (directory == NULL || directory[0] == '\0') return;
    for (int index = 0; index < RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT; index++) {
        RpgObjectFolderMonitorEntry *entry = &entries[index];
        bool matches;
        if (!entry->active || entry->callback == NULL) continue;
        matches = IsSameOrDescendantPath(entry->directory, directory) ||
                  (entry->recursive && IsSameOrDescendantPath(directory, entry->directory));
        if (matches) entry->callback(entry->context, entry->directory);
    }
}

void RpgObjectFolderMonitor_DisconnectAll(void)
{
    for (int index = 0; index < RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT; index++) {
        int handle = index;
        if (entries[index].active) RpgObjectFolderMonitor_Disconnect(&handle);
    }
}
