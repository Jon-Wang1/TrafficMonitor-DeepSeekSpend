#include "dpapi_util.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstdint>
#include <vector>

namespace {

const char* kHexDigits = "0123456789ABCDEF";

std::wstring ToHex(const BYTE* data, DWORD size)
{
    std::wstring result;
    result.reserve(static_cast<size_t>(size) * 2);
    for (DWORD i = 0; i < size; ++i) {
        result.push_back(static_cast<wchar_t>(kHexDigits[data[i] >> 4]));
        result.push_back(static_cast<wchar_t>(kHexDigits[data[i] & 0x0F]));
    }
    return result;
}

int HexValue(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9') {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f') {
        return ch - L'a' + 10;
    }
    if (ch >= L'A' && ch <= L'F') {
        return ch - L'A' + 10;
    }
    return -1;
}

bool FromHex(const std::wstring& text, std::vector<BYTE>& data)
{
    if (text.size() % 2 != 0) {
        return false;
    }
    data.resize(text.size() / 2);
    for (size_t i = 0; i < data.size(); ++i) {
        const int high = HexValue(text[i * 2]);
        const int low = HexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        data[i] = static_cast<BYTE>((high << 4) | low);
    }
    return true;
}

} // namespace

bool DpapiProtect(const std::wstring& plain, std::wstring& hex)
{
    hex.clear();
    if (plain.empty()) {
        return true;
    }
    if (plain.size() > 0x7FFFFFFF / sizeof(wchar_t)) {
        return false;
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(plain.size() * sizeof(wchar_t));
    input.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(plain.data()));

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"DeepSeekUsage", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }

    hex = ToHex(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool DpapiUnprotect(const std::wstring& hex, std::wstring& plain)
{
    plain.clear();
    if (hex.empty()) {
        return true;
    }

    std::vector<BYTE> data;
    if (!FromHex(hex, data)) {
        return false;
    }
    if (data.size() > 0xFFFFFFFFu) {
        return false;
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(data.size());
    input.pbData = data.data();

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }

    const bool valid_size = output.cbData % sizeof(wchar_t) == 0;
    if (valid_size && output.cbData > 0) {
        plain.assign(reinterpret_cast<const wchar_t*>(output.pbData),
                     output.cbData / sizeof(wchar_t));
    }
    LocalFree(output.pbData);
    return valid_size;
}
