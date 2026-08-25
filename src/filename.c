#include "filename.h"

#include <strsafe.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static void Trim(wchar_t *s) {
    if (!s || !*s) return;
    wchar_t *start = s;
    while (*start && iswspace(*start)) start++;
    if (start != s) memmove(s, start, (wcslen(start) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n && iswspace(s[n - 1])) s[--n] = 0;
}

static void CollapseSpaces(wchar_t *s) {
    size_t read = 0, write = 0;
    BOOL previous_space = FALSE;
    while (s[read]) {
        wchar_t c = s[read++];
        BOOL space = iswspace(c) ? TRUE : FALSE;
        if (space) {
            if (!previous_space) s[write++] = L' ';
        } else {
            s[write++] = c;
        }
        previous_space = space;
    }
    s[write] = 0;
    Trim(s);
}

static wchar_t *FindCI(wchar_t *haystack, const wchar_t *needle) {
    if (!needle || !*needle) return haystack;
    size_t length = wcslen(needle);
    for (wchar_t *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < length && p[i] && towlower(p[i]) == towlower(needle[i])) i++;
        if (i == length) return p;
    }
    return NULL;
}

static BOOL ContainsCI(const wchar_t *text, const wchar_t *needle) {
    return FindCI((wchar_t *)text, needle) != NULL;
}

static void RemovePhraseCI(wchar_t *text, const wchar_t *phrase) {
    size_t length = wcslen(phrase);
    if (!length) return;
    for (;;) {
        wchar_t *found = FindCI(text, phrase);
        if (!found) break;
        memmove(found, found + length, (wcslen(found + length) + 1) * sizeof(wchar_t));
    }
}

static BOOL IsNoiseText(const wchar_t *text) {
    static const wchar_t *keywords[] = {
        L"official", L"music video", L"official audio", L"official video",
        L"lyric", L"lyrics", L"visualizer", L"m/v", L"mv", L"audio",
        L"공식", L"뮤비", L"뮤직비디오", L"가사", L"오피셜",
        L"공식 영상", L"공식 음원", L"가사 영상"
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (ContainsCI(text, keywords[i])) return TRUE;
    }
    return FALSE;
}

static void RemoveNoiseBrackets(wchar_t *text, wchar_t open, wchar_t close) {
    wchar_t *cursor = text;
    while ((cursor = wcschr(cursor, open)) != NULL) {
        wchar_t *end = wcschr(cursor + 1, close);
        if (!end) break;
        size_t length = (size_t)(end - cursor - 1);
        wchar_t inside[512];
        if (length >= sizeof(inside) / sizeof(inside[0])) length = sizeof(inside) / sizeof(inside[0]) - 1;
        wcsncpy(inside, cursor + 1, length);
        inside[length] = 0;
        Trim(inside);
        if (!inside[0] || IsNoiseText(inside)) {
            memmove(cursor, end + 1, (wcslen(end + 1) + 1) * sizeof(wchar_t));
        } else {
            cursor = end + 1;
        }
    }
}

static void RemoveNoiseAndEmptyBrackets(wchar_t *text) {
    static const wchar_t pairs[][2] = {
        { L'(', L')' }, { L'[', L']' }, { L'【', L'】' }, { L'（', L'）' }
    };
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
            RemoveNoiseBrackets(text, pairs[i][0], pairs[i][1]);
        }
    }
}

static BOOL IsReservedDeviceName(const wchar_t *name) {
    wchar_t base[32];
    size_t i = 0;
    while (name[i] && name[i] != L'.' && i < sizeof(base) / sizeof(base[0]) - 1) {
        base[i] = towupper(name[i]);
        i++;
    }
    while (i && iswspace(base[i - 1])) i--;
    base[i] = 0;
    if (!_wcsicmp(base, L"CON") || !_wcsicmp(base, L"PRN") ||
        !_wcsicmp(base, L"AUX") || !_wcsicmp(base, L"NUL")) return TRUE;
    if ((wcsncmp(base, L"COM", 3) == 0 || wcsncmp(base, L"LPT", 3) == 0) &&
        wcslen(base) == 4 && base[3] >= L'1' && base[3] <= L'9') return TRUE;
    return FALSE;
}

BOOL Filename_IsSafe(const wchar_t *name) {
    static const wchar_t *invalid = L"<>:\"/\\|?*";
    if (!name || !*name || !wcscmp(name, L".") || !wcscmp(name, L"..")) return FALSE;
    for (size_t i = 0; name[i]; ++i) {
        if (name[i] < 32 || wcschr(invalid, name[i])) return FALSE;
    }
    size_t length = wcslen(name);
    if (!length || name[length - 1] == L'.' || name[length - 1] == L' ') return FALSE;
    return !IsReservedDeviceName(name);
}

BOOL Filename_IsHttpUrl(const wchar_t *url) {
    return url && (!_wcsnicmp(url, L"http://", 7) || !_wcsnicmp(url, L"https://", 8));
}

void Filename_Sanitize(wchar_t *name, size_t cch) {
    if (!name || !cch) return;
    if (cch == 1) {
        name[0] = 0;
        return;
    }
    static const wchar_t *invalid = L"<>:\"/\\|?*";
    for (size_t i = 0; name[i]; ++i) {
        if (name[i] < 32 || wcschr(invalid, name[i])) name[i] = L' ';
    }
    CollapseSpaces(name);
    size_t length = wcslen(name);
    while (length && (name[length - 1] == L'.' || name[length - 1] == L' ')) name[--length] = 0;
    if (IsReservedDeviceName(name)) {
        size_t copy = length < cch - 1 ? length : cch - 2;
        memmove(name + 1, name, copy * sizeof(wchar_t));
        name[0] = L'_';
        name[copy + 1] = 0;
    }
}

static BOOL IsNA(const wchar_t *value) {
    return !value || !*value || !_wcsicmp(value, L"NA") ||
           !_wcsicmp(value, L"None") || !_wcsicmp(value, L"null");
}

void Filename_BuildClean(const wchar_t *raw_title,
                         const wchar_t *artist,
                         const wchar_t *track,
                         BOOL clean,
                         BOOL sanitize,
                         wchar_t *out,
                         size_t cch) {
    wchar_t base[1024] = L"";
    if (!raw_title) raw_title = L"";

    if (clean) {
        if (!IsNA(artist) && !IsNA(track)) {
            StringCchPrintfW(base, 1024, L"%s - %s", artist, track);
        } else {
            const wchar_t *open = wcschr(raw_title, L'「');
            const wchar_t *close = open ? wcschr(open + 1, L'」') : NULL;
            if (open && close && close > open + 1) {
                wchar_t left[512], middle[512];
                size_t left_length = (size_t)(open - raw_title);
                if (left_length >= 511) left_length = 511;
                wcsncpy(left, raw_title, left_length);
                left[left_length] = 0;
                size_t middle_length = (size_t)(close - open - 1);
                if (middle_length >= 511) middle_length = 511;
                wcsncpy(middle, open + 1, middle_length);
                middle[middle_length] = 0;
                Trim(left);
                Trim(middle);
                while (*left && (left[wcslen(left) - 1] == L'-' ||
                       left[wcslen(left) - 1] == L'–' || left[wcslen(left) - 1] == L'—')) {
                    left[wcslen(left) - 1] = 0;
                    Trim(left);
                }
                if (*left && *middle) StringCchPrintfW(base, 1024, L"%s - %s", left, middle);
            }
            if (!*base) StringCchCopyW(base, 1024, raw_title);
        }

        RemoveNoiseAndEmptyBrackets(base);
        static const wchar_t *phrases[] = {
            L"Official Music Video", L"Official Video", L"Official Audio",
            L"Official M/V", L"Official MV", L"Music Video", L"Lyric Video",
            L"공식 뮤직비디오", L"공식 뮤비", L"공식 영상", L"공식 음원",
            L"가사 영상", L"뮤직비디오", L"오피셜"
        };
        for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i) {
            RemovePhraseCI(base, phrases[i]);
        }
        RemoveNoiseAndEmptyBrackets(base);
    } else {
        StringCchCopyW(base, 1024, raw_title);
    }

    for (size_t i = 0; base[i]; ++i) {
        if (base[i] == L'–' || base[i] == L'—') base[i] = L'-';
    }
    CollapseSpaces(base);
    while (*base && (base[wcslen(base) - 1] == L'-' ||
           base[wcslen(base) - 1] == L'_' || base[wcslen(base) - 1] == L' ')) {
        base[wcslen(base) - 1] = 0;
        Trim(base);
    }
    if (!*base) StringCchCopyW(base, 1024, L"untitled");
    if (sanitize) Filename_Sanitize(base, 1024);
    if (wcslen(base) > 180) base[180] = 0;
    StringCchPrintfW(out, cch, L"%s.mp3", base);
}
