#ifndef FEBIUS_COMMAND_LINE_H
#define FEBIUS_COMMAND_LINE_H

#include <windows.h>
#include <stddef.h>

BOOL CommandLine_Build(wchar_t *command, size_t cch,
                       const wchar_t *const *arguments, size_t argument_count);
BOOL PowerShell_EscapeSingleQuoted(const wchar_t *input, wchar_t *output, size_t cch);

#endif
