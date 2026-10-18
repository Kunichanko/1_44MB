// Responsibility: shared, opt-in notification for one runtime object folder.
#ifndef RPG_OBJECT_FOLDER_MONITOR_H
#define RPG_OBJECT_FOLDER_MONITOR_H

#include <stdbool.h>

enum {
    RPG_OBJECT_FOLDER_MONITOR_INVALID_HANDLE = -1,
    RPG_OBJECT_FOLDER_MONITOR_MAX_COUNT = 160
};

/* The callback belongs to the object module.  The monitor only reports that
 * a registered directory changed; it never interprets object metadata. */
typedef void (*RpgObjectFolderMonitorCallback)(void *context, const char *directory);

/* Connect an object to a directory.  Reusing a handle updates its target and
 * callback without creating a second watcher. */
int RpgObjectFolderMonitor_Connect(int handle, const char *directory, bool recursive,
                                   RpgObjectFolderMonitorCallback callback, void *context);
void RpgObjectFolderMonitor_Disconnect(int *handle);
/* Polling is non-blocking.  A change invokes the registered object callback. */
bool RpgObjectFolderMonitor_Poll(int handle);
/* Shared runtime tick: dispatches changes for every connected object once. */
void RpgObjectFolderMonitor_PollAll(void);
/* Notify registered listeners immediately after this process has changed a
 * directory.  This complements (but does not replace) the asynchronous
 * Win32 notification, so object state never has to wait for a later frame. */
void RpgObjectFolderMonitor_NotifyLocalChange(const char *directory);
void RpgObjectFolderMonitor_DisconnectAll(void);

#endif
