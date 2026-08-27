#include "process_runner.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct LineBuffer {
    char text[16384];
    size_t length;
    ProcessLineCallback callback;
    void *context;
} LineBuffer;

static void FeedOutput(LineBuffer *buffer, const char *bytes, DWORD count) {
    if (!buffer || !bytes) return;
    for (DWORD i = 0; i < count; ++i) {
        char value = bytes[i];
        if (value == '\n') {
            buffer->text[buffer->length] = 0;
            if (buffer->callback) buffer->callback(buffer->text, buffer->context);
            buffer->length = 0;
        } else if (value != '\r' && buffer->length + 1 < sizeof(buffer->text)) {
            buffer->text[buffer->length++] = value;
        }
    }
}

static void FlushOutput(LineBuffer *buffer) {
    if (!buffer || !buffer->length) return;
    buffer->text[buffer->length] = 0;
    if (buffer->callback) buffer->callback(buffer->text, buffer->context);
    buffer->length = 0;
}

static void InitializeResult(ProcessResult *result) {
    if (!result) return;
    ZeroMemory(result, sizeof(*result));
    result->exit_code = ERROR_GEN_FAILURE;
}

BOOL Process_RunLines(const wchar_t *command,
                      ProcessLineCallback callback,
                      void *context,
                      HANDLE cancel_event,
                      DWORD timeout_ms,
                      ProcessResult *result) {
    InitializeResult(result);
    if (!command || !*command) return FALSE;

    SECURITY_ATTRIBUTES security = { sizeof(security), NULL, TRUE };
    HANDLE read_pipe = NULL, write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return FALSE;
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return FALSE;
    }

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    wchar_t *mutable_command = _wcsdup(command);
    if (!mutable_command) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return FALSE;
    }

    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    BOOL created = CreateProcessW(NULL, mutable_command, NULL, NULL, TRUE,
                                  flags, NULL, NULL, &startup, &process);
    free(mutable_command);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        return FALSE;
    }
    if (result) result->started = TRUE;

    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    ZeroMemory(&limits, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    BOOL job_ready = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                                    &limits, sizeof(limits)) &&
                     AssignProcessToJobObject(job, process.hProcess);
    if (!job_ready) {
        TerminateProcess(process.hProcess, ERROR_ACCESS_DENIED);
        if (job) CloseHandle(job);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(read_pipe);
        return FALSE;
    }
    ResumeThread(process.hThread);

    LineBuffer line_buffer;
    ZeroMemory(&line_buffer, sizeof(line_buffer));
    line_buffer.callback = callback;
    line_buffer.context = context;
    ULONGLONG started_at = GetTickCount64();
    BOOL cancelled = FALSE, timed_out = FALSE;

    for (;;) {
        if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) {
            cancelled = TRUE;
            TerminateJobObject(job, ERROR_CANCELLED);
        } else if (timeout_ms != INFINITE && GetTickCount64() - started_at >= timeout_ms) {
            timed_out = TRUE;
            TerminateJobObject(job, WAIT_TIMEOUT);
        }

        DWORD available = 0;
        if (PeekNamedPipe(read_pipe, NULL, 0, NULL, &available, NULL) && available) {
            char chunk[4096];
            DWORD wanted = available < sizeof(chunk) ? available : (DWORD)sizeof(chunk);
            DWORD received = 0;
            if (ReadFile(read_pipe, chunk, wanted, &received, NULL) && received) {
                FeedOutput(&line_buffer, chunk, received);
                continue;
            }
        }

        DWORD wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) {
            TerminateJobObject(job, ERROR_GEN_FAILURE);
            break;
        }
        if (cancelled || timed_out) {
            WaitForSingleObject(process.hProcess, 5000);
            break;
        }
    }

    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(read_pipe, NULL, 0, NULL, &available, NULL) || !available) break;
        char chunk[4096];
        DWORD wanted = available < sizeof(chunk) ? available : (DWORD)sizeof(chunk);
        DWORD received = 0;
        if (!ReadFile(read_pipe, chunk, wanted, &received, NULL) || !received) break;
        FeedOutput(&line_buffer, chunk, received);
    }
    FlushOutput(&line_buffer);

    DWORD exit_code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(process.hProcess, &exit_code);
    if (result) {
        result->cancelled = cancelled;
        result->timed_out = timed_out;
        result->exit_code = exit_code;
    }

    CloseHandle(read_pipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return TRUE;
}

BOOL Process_RunHidden(const wchar_t *command,
                       HANDLE cancel_event,
                       DWORD timeout_ms,
                       ProcessResult *result) {
    return Process_RunLines(command, NULL, NULL, cancel_event, timeout_ms, result);
}
