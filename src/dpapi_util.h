#pragma once

#include <string>

bool DpapiProtect(const std::wstring& plain, std::wstring& hex);
bool DpapiUnprotect(const std::wstring& hex, std::wstring& plain);
