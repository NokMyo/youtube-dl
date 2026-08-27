#ifndef FEBIUS_LOGGER_H
#define FEBIUS_LOGGER_H

#include <windows.h>

BOOL Logger_Init(const wchar_t *local_app_dir);
void Logger_Write(const wchar_t *category, const wchar_t *message);
void Logger_Shutdown(void);
const wchar_t *Logger_GetPath(void);

#endif
