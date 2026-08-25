#ifndef SEOWOL_FILENAME_H
#define SEOWOL_FILENAME_H

#include <windows.h>
#include <stddef.h>

BOOL Filename_IsHttpUrl(const wchar_t *url);
BOOL Filename_IsSafe(const wchar_t *name);
void Filename_Sanitize(wchar_t *name, size_t cch);
void Filename_BuildClean(const wchar_t *raw_title,
                         const wchar_t *artist,
                         const wchar_t *track,
                         BOOL clean,
                         BOOL sanitize,
                         wchar_t *out,
                         size_t cch);

#endif
