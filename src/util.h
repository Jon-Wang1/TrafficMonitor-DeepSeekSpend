#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

HMODULE GetThisModuleHandle();
std::wstring GetEnvString(const wchar_t* name);
std::wstring GetModuleDir();
std::wstring GetTempDir();
std::wstring FormatLocalTime();
void DebugLog(const std::wstring& msg);
void LogToFile(const std::wstring& path, const std::wstring& msg);

std::string WideToUtf8(const std::wstring& text);
std::wstring Utf8ToWide(const std::string& text);

bool ParseDecimal(const std::string& text, long double& value);
int64_t AmountToCents(const std::string& text);
int64_t AmountToMicros(const std::string& text);
int64_t MicrosToCents(int64_t micros);
std::wstring FormatCents(int64_t cents);
std::wstring FormatMicros(int64_t micros);
std::wstring CurrencySymbol(const std::wstring& currency);
std::wstring FormatAmountCents(int64_t cents, const std::wstring& currency);
std::wstring FormatAmountMicros(int64_t micros, const std::wstring& currency);
std::wstring NormalizeCurrency(const std::wstring& currency);
std::wstring ToLowerAscii(const std::wstring& text);
