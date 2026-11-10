#ifndef RPG_KEY_DOOR_TRACE_H
#define RPG_KEY_DOOR_TRACE_H

/* Temporary, behaviour-free diagnostic trace for the key-door transfer path.
   Each write is flushed so a failed transfer or closed window still leaves a
   complete record beside the executable. */
void RpgKeyDoorTrace_Reset(void);
void RpgKeyDoorTrace_Log(const char *format, ...);

#endif
