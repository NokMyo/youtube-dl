#ifndef FEBIUS_HISTORY_H
#define FEBIUS_HISTORY_H

#include <windows.h>
#include <stddef.h>

BOOL History_Init(void);
void History_Shutdown(void);
void History_EnsureLoaded(const wchar_t *folder);
BOOL History_ShouldSkip(const wchar_t *folder,
                        const wchar_t *video_id,
                        const wchar_t *expected_filename,
                        int bitrate);
BOOL History_Record(const wchar_t *folder,
                    const wchar_t *video_id,
                    const wchar_t *actual_filename,
                    int bitrate);
void History_Invalidate(void);
BOOL History_GetPath(const wchar_t *folder, wchar_t *out, size_t cch);

#endif
