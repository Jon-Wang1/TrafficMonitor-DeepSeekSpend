#include "util.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

namespace {

const wchar_t* kLogFileName = L"DeepSeekUsage_load.log";
constexpr unsigned long long kMaxLogSize = 1024ull * 1024ull;

void AppendLogLine(const std::wstring& path, const std::wstring& msg)
{
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    wchar_t pid_buffer[32]{};
    swprintf(pid_buffer, 32, L"[pid %lu] ", GetCurrentProcessId());

    const std::wstring line = FormatLocalTime() + L" " + pid_buffer + msg + L"\r\n";

    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                                             static_cast<int>(line.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (utf8_len > 0) {
        std::vector<char> utf8(static_cast<size_t>(utf8_len));
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                            utf8.data(), utf8_len, nullptr, nullptr);

        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) && size.QuadPart == 0) {
            const char bom[] = "\xEF\xBB\xBF";
            DWORD written = 0;
            WriteFile(file, bom, 3, &written, nullptr);
        }

        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    CloseHandle(file);
}

} // namespace

HMODULE GetThisModuleHandle()
{
    HMODULE module = nullptr;
    static int anchor = 0;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&anchor), &module);
    return module;
}

std::wstring GetEnvString(const wchar_t* name)
{
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return L"";
    }
    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), length);
    if (copied == 0 || copied >= length) {
        return L"";
    }
    value.resize(copied);
    return value;
}

std::wstring GetModuleDir()
{
    HMODULE module = nullptr;
    static int anchor = 0;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&anchor), &module) ||
        module == nullptr) {
        return L"";
    }

    wchar_t buffer[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(buffer, length);
    const std::wstring::size_type pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L"";
    }
    return path.substr(0, pos + 1);
}

std::wstring GetTempDir()
{
    wchar_t buffer[MAX_PATH + 1]{};
    DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0) {
        return L"";
    }
    return std::wstring(buffer, length);
}

std::wstring FormatLocalTime()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[64]{};
    swprintf(buffer, 64, L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

void DebugLog(const std::wstring& msg)
{
    const std::wstring dir = GetTempDir();
    if (!dir.empty()) {
        AppendLogLine(dir + kLogFileName, msg);
    }
}

void LogToFile(const std::wstring& path, const std::wstring& msg)
{
    if (path.empty()) {
        return;
    }

    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        ULARGE_INTEGER size{};
        size.LowPart = attributes.nFileSizeLow;
        size.HighPart = attributes.nFileSizeHigh;
        if (size.QuadPart > kMaxLogSize) {
            const std::wstring old_path = path + L".old";
            DeleteFileW(old_path.c_str());
            MoveFileExW(path.c_str(), old_path.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }

    AppendLogLine(path, msg);
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return "";
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return "";
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) {
        return L"";
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0) {
        return L"";
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), length);
    return result;
}

bool ParseDecimal(const std::string& text, long double& value)
{
    if (text.empty()) {
        return false;
    }
    const char* begin = text.c_str();
    char* end = nullptr;
    value = std::strtold(begin, &end);
    if (end == begin) {
        return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    return std::isfinite(static_cast<double>(value));
}

int64_t AmountToCents(const std::string& text)
{
    long double value = 0;
    if (!ParseDecimal(text, value)) {
        return 0;
    }
    return static_cast<int64_t>(std::llround(value * 100.0L));
}

int64_t AmountToMicros(const std::string& text)
{
    long double value = 0;
    if (!ParseDecimal(text, value)) {
        return 0;
    }
    return static_cast<int64_t>(std::llround(value * 1000000.0L));
}

int64_t MicrosToCents(int64_t micros)
{
    if (micros >= 0) {
        return (micros + 5000) / 10000;
    }
    return -((-micros + 5000) / 10000);
}

std::wstring FormatCents(int64_t cents)
{
    const bool negative = cents < 0;
    const uint64_t absolute = negative
        ? static_cast<uint64_t>(-(cents + 1)) + 1
        : static_cast<uint64_t>(cents);
    const uint64_t whole = absolute / 100;
    const uint64_t fraction = absolute % 100;

    wchar_t buffer[64]{};
    swprintf(buffer, 64, L"%ls%llu.%02llu",
             negative ? L"-" : L"",
             static_cast<unsigned long long>(whole),
             static_cast<unsigned long long>(fraction));
    return buffer;
}

std::wstring FormatMicros(int64_t micros)
{
    return FormatCents(MicrosToCents(micros));
}

namespace {

std::wstring FormatTenthsValue(int64_t tenths)
{
    const bool negative = tenths < 0;
    const uint64_t absolute = negative
        ? static_cast<uint64_t>(-(tenths + 1)) + 1
        : static_cast<uint64_t>(tenths);
    const uint64_t whole = absolute / 10;
    const uint64_t fraction = absolute % 10;

    wchar_t buffer[64]{};
    swprintf(buffer, 64, L"%ls%llu.%llu",
             negative ? L"-" : L"",
             static_cast<unsigned long long>(whole),
             static_cast<unsigned long long>(fraction));
    return buffer;
}

} // namespace

std::wstring FormatCents1(int64_t cents)
{
    const int64_t tenths = cents >= 0
        ? (cents + 5) / 10
        : -((-cents + 5) / 10);
    return FormatTenthsValue(tenths);
}

std::wstring FormatMicros1(int64_t micros)
{
    const int64_t tenths = micros >= 0
        ? (micros + 50000) / 100000
        : -((-micros + 50000) / 100000);
    return FormatTenthsValue(tenths);
}

std::wstring CurrencySymbol(const std::wstring& currency)
{
    const std::wstring normalized = NormalizeCurrency(currency);
    if (normalized == L"CNY") {
        return L"¥";
    }
    if (normalized == L"USD") {
        return L"$";
    }
    if (normalized.empty()) {
        return L"¥";
    }
    return normalized + L" ";
}

std::wstring FormatAmountCents(int64_t cents, const std::wstring& currency)
{
    return CurrencySymbol(currency) + FormatCents(cents);
}

std::wstring FormatAmountMicros(int64_t micros, const std::wstring& currency)
{
    return CurrencySymbol(currency) + FormatMicros(micros);
}

std::wstring FormatAmountCents1(int64_t cents, const std::wstring& currency)
{
    return CurrencySymbol(currency) + FormatCents1(cents);
}

std::wstring FormatAmountMicros1(int64_t micros, const std::wstring& currency)
{
    return CurrencySymbol(currency) + FormatMicros1(micros);
}

std::wstring ToLowerAscii(const std::wstring& text)
{
    std::wstring result = text;
    for (wchar_t& ch : result) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return result;
}

std::wstring NormalizeCurrency(const std::wstring& currency)
{
    const std::wstring upper = [&currency]() {
        std::wstring result = currency;
        for (wchar_t& ch : result) {
            if (ch >= L'a' && ch <= L'z') {
                ch = static_cast<wchar_t>(ch - L'a' + L'A');
            }
        }
        return result;
    }();
    return upper;
}
