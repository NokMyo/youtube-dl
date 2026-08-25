#include "command_line.h"

#include <wchar.h>

static BOOL AppendChar(wchar_t *command, size_t cch, size_t *length, wchar_t value) {
    if (*length + 1 >= cch) return FALSE;
    command[(*length)++] = value;
    command[*length] = 0;
    return TRUE;
}

static BOOL AppendArgument(wchar_t *command, size_t cch, size_t *length, const wchar_t *argument) {
    if (!command || !argument || !length || !cch || *length >= cch) return FALSE;
    if (*length && !AppendChar(command, cch, length, L' ')) return FALSE;
    if (!AppendChar(command, cch, length, L'"')) return FALSE;

    size_t backslashes = 0;
    for (const wchar_t *cursor = argument;; ++cursor) {
        if (*cursor == L'\\') {
            backslashes++;
            continue;
        }
        size_t copies = *cursor == L'"' ? backslashes * 2 + 1 : backslashes;
        if (!*cursor) copies = backslashes * 2;
        for (size_t i = 0; i < copies; ++i) {
            if (!AppendChar(command, cch, length, L'\\')) return FALSE;
        }
        backslashes = 0;
        if (!*cursor) break;
        if (!AppendChar(command, cch, length, *cursor)) return FALSE;
    }
    return AppendChar(command, cch, length, L'"');
}

BOOL CommandLine_Build(wchar_t *command, size_t cch,
                       const wchar_t *const *arguments, size_t argument_count) {
    if (!command || !cch || !arguments || !argument_count) return FALSE;
    command[0] = 0;
    size_t length = 0;
    for (size_t i = 0; i < argument_count; ++i) {
        if (!AppendArgument(command, cch, &length, arguments[i])) {
            command[0] = 0;
            return FALSE;
        }
    }
    return TRUE;
}

BOOL PowerShell_EscapeSingleQuoted(const wchar_t *input, wchar_t *output, size_t cch) {
    if (!input || !output || !cch) return FALSE;
    size_t written = 0;
    for (size_t i = 0; input[i]; ++i) {
        if (written + (input[i] == L'\'' ? 2 : 1) >= cch) return FALSE;
        output[written++] = input[i];
        if (input[i] == L'\'') output[written++] = L'\'';
    }
    output[written] = 0;
    return TRUE;
}
