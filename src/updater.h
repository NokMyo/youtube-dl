#ifndef FEBIUS_UPDATER_H
#define FEBIUS_UPDATER_H

#include <windows.h>
#include <stddef.h>

#define UPDATER_SHA256_BYTES 32

typedef void (*UpdaterProgressCallback)(int percent, void *context);

BOOL Updater_ParseSha256Text(const char *text, size_t length,
                             unsigned char digest[UPDATER_SHA256_BYTES]);
BOOL Updater_DownloadRelease(const wchar_t *version,
                             wchar_t *downloaded_path, size_t downloaded_path_cch,
                             UpdaterProgressCallback progress_callback,
                             void *progress_context,
                             wchar_t *error, size_t error_cch);
BOOL Updater_LaunchReplacement(const wchar_t *downloaded_path,
                               wchar_t *error, size_t error_cch);
void Updater_CleanupPending(void);

#endif
