#pragma once

#include <vector>

struct ShortcutEntry
{
    wchar_t text[4096];
    wchar_t shortcut[128];
    wchar_t bind[128];
};

bool Shortcuts_Load(std::vector<ShortcutEntry>& entries);
bool Shortcuts_Save(const std::vector<ShortcutEntry>& entries);
void Shortcuts_GetConfigPath(wchar_t* buf, int bufSize);
