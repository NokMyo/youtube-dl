#include "updater.h"

#include "command_line.h"

#include <winhttp.h>
#include <wincrypt.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define UPDATE_DOWNLOAD_LIMIT (512ULL * 1024ULL * 1024ULL)
#define UPDATE_CHECKSUM_LIMIT (64ULL * 1024ULL)

static void SetErrorText(wchar_t *error, size_t cch, const wchar_t *text) {
    if (error && cch) StringCchCopyW(error, cch, text ? text : L"");
}

static BOOL IsSafeVersion(const wchar_t *version) {
    if (!version || !*version) return FALSE;
    int dots = 0;
    for (const wchar_t *cursor = version; *cursor; ++cursor) {
        if (*cursor == L'.') {
            if (cursor == version || cursor[-1] == L'.') return FALSE;
            dots++;
        } else if (*cursor < L'0' || *cursor > L'9') {
            return FALSE;
        }
    }
    size_t length = wcslen(version);
    return dots == 2 && version[length - 1] != L'.';
}

static BOOL BuildPendingPath(const wchar_t *suffix, wchar_t *path, size_t cch) {
    wchar_t executable[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    return length > 0 && length < MAX_PATH &&
           SUCCEEDED(StringCchPrintfW(path, cch, L"%s%s", executable, suffix));
}

static BOOL QueryResponseStatus(HINTERNET request, DWORD *status) {
    DWORD size = sizeof(*status);
    return WinHttpQueryHeaders(request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, status, &size, WINHTTP_NO_HEADER_INDEX);
}

static BOOL DownloadGitHubPath(const wchar_t *path, const wchar_t *destination,
                               ULONGLONG maximum_bytes,
                               UpdaterProgressCallback progress_callback,
                               void *progress_context,
                               wchar_t *error, size_t error_cch) {
    BOOL success = FALSE;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    unsigned char *buffer = NULL;
    ULONGLONG total = 0;
    int last_percent = -1;

    session = WinHttpOpen(L"FebiusDownrush-Updater/1.0",
                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto cleanup;
    WinHttpSetTimeouts(session, 5000, 10000, 10000, 30000);
    connection = WinHttpConnect(session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) goto cleanup;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto cleanup;

    DWORD status = 0;
    if (!QueryResponseStatus(request, &status) || status != 200) goto cleanup;

    DWORD content_length = 0;
    DWORD content_length_size = sizeof(content_length);
    BOOL length_known = WinHttpQueryHeaders(request,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &content_length_size,
        WINHTTP_NO_HEADER_INDEX);
    if (length_known && (content_length == 0 || content_length > maximum_bytes)) {
        SetErrorText(error, error_cch, L"업데이트 파일의 크기가 올바르지 않습니다.");
        goto cleanup;
    }

    file = CreateFileW(destination, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        SetErrorText(error, error_cch,
                     L"현재 프로그램 폴더에 업데이트 파일을 저장할 수 없습니다.");
        goto cleanup;
    }
    buffer = (unsigned char *)malloc(64 * 1024);
    if (!buffer) {
        SetErrorText(error, error_cch, L"업데이트에 필요한 메모리를 확보하지 못했습니다.");
        goto cleanup;
    }

    if (progress_callback) progress_callback(0, progress_context);
    for (;;) {
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer, 64 * 1024, &received)) goto cleanup;
        if (!received) break;
        total += received;
        if (total > maximum_bytes) {
            SetErrorText(error, error_cch, L"업데이트 파일이 허용된 크기를 초과했습니다.");
            goto cleanup;
        }
        DWORD written = 0;
        if (!WriteFile(file, buffer, received, &written, NULL) || written != received) {
            SetErrorText(error, error_cch, L"업데이트 파일을 저장하지 못했습니다.");
            goto cleanup;
        }
        if (progress_callback && length_known && content_length) {
            int percent = (int)(total * 100ULL / content_length);
            if (percent > 100) percent = 100;
            if (percent != last_percent) {
                last_percent = percent;
                progress_callback(percent, progress_context);
            }
        }
    }
    if (!total || (length_known && total != content_length)) {
        SetErrorText(error, error_cch, L"업데이트 파일을 완전히 받지 못했습니다.");
        goto cleanup;
    }
    if (!FlushFileBuffers(file)) {
        SetErrorText(error, error_cch, L"업데이트 파일을 디스크에 저장하지 못했습니다.");
        goto cleanup;
    }
    if (progress_callback) progress_callback(100, progress_context);
    success = TRUE;

cleanup:
    if (!success && error && error_cch && !error[0]) {
        SetErrorText(error, error_cch,
                     L"업데이트 파일을 내려받지 못했습니다. 인터넷 연결을 확인해 주세요.");
    }
    free(buffer);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (!success) DeleteFileW(destination);
    return success;
}

static int HexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

BOOL Updater_ParseSha256Text(const char *text, size_t length,
                             unsigned char digest[UPDATER_SHA256_BYTES]) {
    if (!text || !digest) return FALSE;
    size_t position = 0;
    if (length >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        position = 3;
    }
    while (position < length && (text[position] == ' ' || text[position] == '\t' ||
           text[position] == '\r' || text[position] == '\n')) position++;
    if (length - position < UPDATER_SHA256_BYTES * 2) return FALSE;
    for (size_t i = 0; i < UPDATER_SHA256_BYTES; ++i) {
        int high = HexValue(text[position + i * 2]);
        int low = HexValue(text[position + i * 2 + 1]);
        if (high < 0 || low < 0) return FALSE;
        digest[i] = (unsigned char)((high << 4) | low);
    }
    position += UPDATER_SHA256_BYTES * 2;
    return position == length || text[position] == ' ' || text[position] == '\t' ||
           text[position] == '\r' || text[position] == '\n' || text[position] == '*';
}

static BOOL ReadExpectedHash(const wchar_t *path,
                             unsigned char digest[UPDATER_SHA256_BYTES]) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    char text[4096];
    DWORD received = 0;
    BOOL read = ReadFile(file, text, sizeof(text), &received, NULL);
    CloseHandle(file);
    return read && received > 0 && Updater_ParseSha256Text(text, received, digest);
}

static BOOL HashFileSha256(const wchar_t *path,
                           unsigned char digest[UPDATER_SHA256_BYTES]) {
    BOOL success = FALSE;
    HANDLE file = INVALID_HANDLE_VALUE;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    unsigned char *buffer = NULL;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) goto cleanup;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT) ||
        !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto cleanup;
    buffer = (unsigned char *)malloc(1024 * 1024);
    if (!buffer) goto cleanup;
    for (;;) {
        DWORD received = 0;
        if (!ReadFile(file, buffer, 1024 * 1024, &received, NULL)) goto cleanup;
        if (!received) break;
        if (!CryptHashData(hash, buffer, received, 0)) goto cleanup;
    }
    DWORD digest_size = UPDATER_SHA256_BYTES;
    success = CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) &&
              digest_size == UPDATER_SHA256_BYTES;

cleanup:
    free(buffer);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return success;
}

static BOOL LooksLikeExecutable(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER size;
    unsigned char signature[2];
    DWORD received = 0;
    BOOL valid = GetFileSizeEx(file, &size) && size.QuadPart >= 64 * 1024 &&
                 ReadFile(file, signature, sizeof(signature), &received, NULL) &&
                 received == sizeof(signature) && signature[0] == 'M' && signature[1] == 'Z';
    CloseHandle(file);
    return valid;
}

BOOL Updater_DownloadRelease(const wchar_t *version,
                             wchar_t *downloaded_path, size_t downloaded_path_cch,
                             UpdaterProgressCallback progress_callback,
                             void *progress_context,
                             wchar_t *error, size_t error_cch) {
    if (error && error_cch) error[0] = 0;
    if (!downloaded_path || !downloaded_path_cch || !IsSafeVersion(version)) {
        SetErrorText(error, error_cch, L"업데이트 버전 정보가 올바르지 않습니다.");
        return FALSE;
    }
    downloaded_path[0] = 0;
    wchar_t checksum_path[MAX_PATH];
    if (!BuildPendingPath(L".update", downloaded_path, downloaded_path_cch) ||
        !BuildPendingPath(L".sha256.download", checksum_path, MAX_PATH)) {
        SetErrorText(error, error_cch, L"프로그램 경로가 너무 길어 업데이트할 수 없습니다.");
        return FALSE;
    }
    DeleteFileW(downloaded_path);
    DeleteFileW(checksum_path);

    wchar_t checksum_url[512], executable_url[512];
    if (FAILED(StringCchPrintfW(checksum_url, 512,
            L"/NokMyo/youtube-dl/releases/download/v%s/FebiusDownrush.exe.sha256", version)) ||
        FAILED(StringCchPrintfW(executable_url, 512,
            L"/NokMyo/youtube-dl/releases/download/v%s/FebiusDownrush.exe", version))) {
        SetErrorText(error, error_cch, L"업데이트 주소를 만들지 못했습니다.");
        return FALSE;
    }

    BOOL success = DownloadGitHubPath(checksum_url, checksum_path,
                                      UPDATE_CHECKSUM_LIMIT, NULL, NULL,
                                      error, error_cch);
    unsigned char expected[UPDATER_SHA256_BYTES];
    unsigned char actual[UPDATER_SHA256_BYTES];
    if (success && !ReadExpectedHash(checksum_path, expected)) {
        SetErrorText(error, error_cch, L"업데이트 확인 파일을 읽지 못했습니다.");
        success = FALSE;
    }
    DeleteFileW(checksum_path);
    if (success) {
        success = DownloadGitHubPath(executable_url, downloaded_path,
                                     UPDATE_DOWNLOAD_LIMIT, progress_callback,
                                     progress_context, error, error_cch);
    }
    if (success && (!HashFileSha256(downloaded_path, actual) ||
                    memcmp(expected, actual, sizeof(expected)) != 0)) {
        SetErrorText(error, error_cch,
                     L"업데이트 파일의 무결성 확인에 실패했습니다. 기존 프로그램은 그대로 유지됩니다.");
        success = FALSE;
    }
    if (success && !LooksLikeExecutable(downloaded_path)) {
        SetErrorText(error, error_cch, L"받은 업데이트 파일이 올바른 실행 파일이 아닙니다.");
        success = FALSE;
    }
    if (!success) DeleteFileW(downloaded_path);
    return success;
}

static BOOL WriteUpdaterScript(const wchar_t *path) {
    static const wchar_t script[] =
        L"param([int]$ParentId,[string]$Source,[string]$Destination)\r\n"
        L"$ErrorActionPreference='Stop'\r\n"
        L"try {\r\n"
        L"  Wait-Process -Id $ParentId -ErrorAction SilentlyContinue\r\n"
        L"  $moved=$false\r\n"
        L"  for($i=0;$i -lt 30 -and -not $moved;$i++){\r\n"
        L"    try { Move-Item -LiteralPath $Source -Destination $Destination -Force; $moved=$true }\r\n"
        L"    catch { Start-Sleep -Milliseconds 200 }\r\n"
        L"  }\r\n"
        L"  if(-not $moved){ throw 'replace failed' }\r\n"
        L"  Start-Process -FilePath $Destination\r\n"
        L"} catch {\r\n"
        L"  Add-Type -AssemblyName System.Windows.Forms\r\n"
        L"  [System.Windows.Forms.MessageBox]::Show('업데이트를 적용하지 못했습니다. 기존 프로그램을 다시 실행해 주세요.','Febius Downrush') | Out-Null\r\n"
        L"} finally {\r\n"
        L"  Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\r\n"
        L"}\r\n";
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    const WORD bom = 0xFEFF;
    DWORD written = 0;
    BOOL success = WriteFile(file, &bom, sizeof(bom), &written, NULL) &&
                   written == sizeof(bom);
    DWORD script_bytes = (DWORD)(wcslen(script) * sizeof(wchar_t));
    written = 0;
    success = success && WriteFile(file, script, script_bytes, &written, NULL) &&
              written == script_bytes && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success) DeleteFileW(path);
    return success;
}

BOOL Updater_LaunchReplacement(const wchar_t *downloaded_path,
                               wchar_t *error, size_t error_cch) {
    if (error && error_cch) error[0] = 0;
    wchar_t current_path[MAX_PATH], temp_directory[MAX_PATH], temp_file[MAX_PATH];
    wchar_t script_path[MAX_PATH], system_directory[MAX_PATH], powershell[MAX_PATH];
    DWORD current_length = GetModuleFileNameW(NULL, current_path, MAX_PATH);
    DWORD temp_length = GetTempPathW(MAX_PATH, temp_directory);
    DWORD system_length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (!downloaded_path || !*downloaded_path || current_length == 0 ||
        current_length >= MAX_PATH || temp_length == 0 || temp_length >= MAX_PATH ||
        system_length == 0 || system_length >= MAX_PATH ||
        !GetTempFileNameW(temp_directory, L"FDU", 0, temp_file) ||
        FAILED(StringCchPrintfW(script_path, MAX_PATH, L"%s.ps1", temp_file)) ||
        FAILED(StringCchPrintfW(powershell, MAX_PATH,
            L"%s\\WindowsPowerShell\\v1.0\\powershell.exe", system_directory))) {
        SetErrorText(error, error_cch, L"업데이트 적용 작업을 준비하지 못했습니다.");
        return FALSE;
    }
    DeleteFileW(temp_file);
    if (!WriteUpdaterScript(script_path)) {
        SetErrorText(error, error_cch, L"업데이트 적용 작업을 준비하지 못했습니다.");
        return FALSE;
    }

    wchar_t process_id[24], command[4096];
    StringCchPrintfW(process_id, 24, L"%lu", GetCurrentProcessId());
    const wchar_t *const arguments[] = {
        powershell, L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass",
        L"-File", script_path, process_id, downloaded_path, current_path
    };
    if (!CommandLine_Build(command, 4096, arguments,
                           sizeof(arguments) / sizeof(arguments[0]))) {
        DeleteFileW(script_path);
        SetErrorText(error, error_cch, L"업데이트 적용 명령을 만들지 못했습니다.");
        return FALSE;
    }

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    wchar_t *mutable_command = _wcsdup(command);
    if (!mutable_command) {
        DeleteFileW(script_path);
        SetErrorText(error, error_cch, L"업데이트에 필요한 메모리를 확보하지 못했습니다.");
        return FALSE;
    }
    BOOL launched = CreateProcessW(NULL, mutable_command, NULL, NULL, FALSE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                   NULL, NULL, &startup, &process);
    free(mutable_command);
    if (!launched) {
        DeleteFileW(script_path);
        SetErrorText(error, error_cch, L"업데이트 적용 작업을 시작하지 못했습니다.");
        return FALSE;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

void Updater_CleanupPending(void) {
    wchar_t path[MAX_PATH];
    if (BuildPendingPath(L".update", path, MAX_PATH)) DeleteFileW(path);
    if (BuildPendingPath(L".sha256.download", path, MAX_PATH)) DeleteFileW(path);
}
