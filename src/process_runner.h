#ifndef FEBIUS_PROCESS_RUNNER_H
#define FEBIUS_PROCESS_RUNNER_H

#include <windows.h>

typedef void (*ProcessLineCallback)(const char *line, void *context);

typedef struct ProcessResult {
    BOOL started;
    BOOL cancelled;
    BOOL timed_out;
    DWORD exit_code;
} ProcessResult;

BOOL Process_RunLines(const wchar_t *command,
                      ProcessLineCallback callback,
                      void *context,
                      HANDLE cancel_event,
                      DWORD timeout_ms,
                      ProcessResult *result);

BOOL Process_RunHidden(const wchar_t *command,
                       HANDLE cancel_event,
                       DWORD timeout_ms,
                       ProcessResult *result);

#endif
