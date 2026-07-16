#include "shortcuts.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>

static void ExeConfigPath(wchar_t* buf, int bufSize)
{
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { if (bufSize > 0) buf[0] = 0; return; }
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *(slash + 1) = 0;
    _snwprintf_s(buf, bufSize, _TRUNCATE, L"%sshortcuts.cfg", dir);
}

static bool ExeDirWritable(const wchar_t* cfgPath)
{
    wchar_t probe[MAX_PATH + 8];
    _snwprintf_s(probe, _TRUNCATE, L"%s.probe", cfgPath);
    HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

void Shortcuts_GetConfigPath(wchar_t* buf, int bufSize)
{
    ExeConfigPath(buf, bufSize);
    if (buf[0])
    {
        if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return;
        if (ExeDirWritable(buf)) return;
    }

    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appData)))
        return;

    wchar_t dir[MAX_PATH];
    _snwprintf_s(dir, _TRUNCATE, L"%s\\SimpleShortcut", appData);
    CreateDirectoryW(dir, NULL);
    _snwprintf_s(buf, bufSize, _TRUNCATE, L"%s\\shortcuts.cfg", dir);
}

static const wchar_t* EscapeNewlines(const wchar_t* src)
{
    static wchar_t buf[8192];
    int j = 0;
    for (int i = 0; src[i] && j < 8190; i++)
    {
        if (src[i] == L'\\')
        {
            buf[j++] = L'\\';
            buf[j++] = L'\\';
        }
        else if (src[i] == L'\n')
        {
            buf[j++] = L'\\';
            buf[j++] = L'n';
        }
        else if (src[i] == L'\r')
        {
        }
        else
        {
            buf[j++] = src[i];
        }
    }
    buf[j] = 0;
    return buf;
}

static void UnescapeNewlines(wchar_t* dst, int dstSize, const wchar_t* src)
{
    int j = 0;
    for (int i = 0; src[i] && j < dstSize - 2; i++)
    {
        if (src[i] == L'\\' && src[i + 1] == L'n')
        {
            dst[j++] = L'\r';
            dst[j++] = L'\n';
            i++;
        }
        else if (src[i] == L'\\' && src[i + 1] == L'\\')
        {
            dst[j++] = L'\\';
            i++;
        }
        else
        {
            dst[j++] = src[i];
        }
    }
    dst[j] = 0;
}

bool Shortcuts_Load(std::vector<ShortcutEntry>& entries)
{
    entries.clear();

    wchar_t path[MAX_PATH];
    Shortcuts_GetConfigPath(path, MAX_PATH);
    if (!path[0]) return false;

    FILE* f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f) return false;

    ShortcutEntry cur;
    memset(&cur, 0, sizeof(cur));
    bool inEntry = false;

    wchar_t line[8192];
    while (fgetws(line, 8192, f))
    {
        size_t len = wcslen(line);
        while (len > 0 && (line[len - 1] == L'\n' || line[len - 1] == L'\r'))
            line[--len] = 0;

        if (line[0] == L'[')
        {
            if (inEntry)
                entries.push_back(cur);
            memset(&cur, 0, sizeof(cur));
            inEntry = true;
        }
        else
        {
            wchar_t* eq = wcschr(line, L'=');
            if (!eq) continue;

            *eq = 0;
            wchar_t* key = line;
            wchar_t* val = eq + 1;

            while (*key == L' ' || *key == L'\t') key++;
            wchar_t* kend = key + wcslen(key) - 1;
            while (kend >= key && (*kend == L' ' || *kend == L'\t')) { *kend = 0; kend--; }

            while (*val == L' ' || *val == L'\t') val++;

            if (wcscmp(key, L"text") == 0)
                UnescapeNewlines(cur.text, 4096, val);
            else if (wcscmp(key, L"shortcut") == 0)
                wcsncpy_s(cur.shortcut, val, _TRUNCATE);
            else if (wcscmp(key, L"bind") == 0)
                wcsncpy_s(cur.bind, val, _TRUNCATE);
        }
    }

    if (inEntry)
        entries.push_back(cur);

    fclose(f);
    return true;
}

bool Shortcuts_Save(const std::vector<ShortcutEntry>& entries)
{
    wchar_t path[MAX_PATH];
    Shortcuts_GetConfigPath(path, MAX_PATH);
    if (!path[0]) return false;

    wchar_t tmp[MAX_PATH + 4];
    _snwprintf_s(tmp, _TRUNCATE, L"%s.tmp", path);

    FILE* f = _wfopen(tmp, L"w, ccs=UTF-8");
    if (!f) return false;

    bool ok = true;
    for (size_t i = 0; i < entries.size() && ok; i++)
    {
        if (fwprintf(f, L"[%zu]\n", i) < 0) ok = false;
        if (fwprintf(f, L"text=%s\n",
                 entries[i].text[0] ? EscapeNewlines(entries[i].text) : L"") < 0) ok = false;
        if (fwprintf(f, L"shortcut=%s\n",
                 entries[i].shortcut[0] ? entries[i].shortcut : L"") < 0) ok = false;
        if (fwprintf(f, L"bind=%s\n",
                 entries[i].bind[0] ? entries[i].bind : L"") < 0) ok = false;
        if (fwprintf(f, L"\n") < 0) ok = false;
    }

    if (fclose(f) != 0) ok = false;

    if (!ok)
    {
        DeleteFileW(tmp);
        return false;
    }

    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}
