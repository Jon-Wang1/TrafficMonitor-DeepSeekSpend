#include "ini.h"

#include "util.h"

#include <windows.h>

#include <cstdlib>
#include <vector>

namespace {

constexpr size_t kMaxIniSize = 4 * 1024 * 1024;

std::wstring Trim(const std::wstring& text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == L' ' || text[begin] == L'\t' ||
            text[begin] == L'\r' || text[begin] == L'\n')) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == L' ' || text[end - 1] == L'\t' ||
            text[end - 1] == L'\r' || text[end - 1] == L'\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool ReadFileBytes(const std::wstring& path, std::string& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > kMaxIniSize) {
        CloseHandle(file);
        return false;
    }

    data.resize(static_cast<size_t>(size.QuadPart));
    size_t total = 0;
    while (total < data.size()) {
        DWORD read = 0;
        const DWORD request = static_cast<DWORD>(data.size() - total);
        if (!ReadFile(file, data.data() + total, request, &read, nullptr) || read == 0) {
            break;
        }
        total += read;
    }
    data.resize(total);
    CloseHandle(file);
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    size_t total = 0;
    bool ok = true;
    while (total < data.size()) {
        DWORD written = 0;
        const DWORD request = static_cast<DWORD>(data.size() - total);
        if (!WriteFile(file, data.data() + total, request, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        total += written;
    }
    if (ok) {
        FlushFileBuffers(file);
    }
    CloseHandle(file);
    return ok;
}

void EnsureParentDirectory(const std::wstring& path)
{
    const std::wstring::size_type pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos || pos == 0) {
        return;
    }
    const std::wstring directory = path.substr(0, pos);
    if (!directory.empty()) {
        CreateDirectoryW(directory.c_str(), nullptr);
    }
}

} // namespace

bool IniFile::Load(const std::wstring& path)
{
    m_data.clear();

    std::string bytes;
    if (!ReadFileBytes(path, bytes)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        return false;
    }

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    const std::wstring text = Utf8ToWide(bytes);
    std::wstring current_section;
    size_t position = 0;
    while (position <= text.size()) {
        const size_t newline = text.find(L'\n', position);
        const size_t end = newline == std::wstring::npos ? text.size() : newline;
        std::wstring line = Trim(text.substr(position, end - position));
        position = newline == std::wstring::npos ? text.size() + 1 : newline + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#') {
            continue;
        }
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']') {
            current_section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        const std::wstring key = Trim(line.substr(0, equals));
        const std::wstring value = Trim(line.substr(equals + 1));
        if (!key.empty()) {
            m_data[current_section][key] = value;
        }
    }
    return true;
}

std::wstring IniFile::GetString(const std::wstring& section, const std::wstring& key,
                                const std::wstring& default_value) const
{
    const auto section_it = m_data.find(section);
    if (section_it == m_data.end()) {
        return default_value;
    }
    const auto value_it = section_it->second.find(key);
    return value_it == section_it->second.end() ? default_value : value_it->second;
}

int IniFile::GetInt(const std::wstring& section, const std::wstring& key, int default_value) const
{
    const std::wstring value = GetString(section, key, L"");
    if (value.empty()) {
        return default_value;
    }
    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return default_value;
    }
    return static_cast<int>(parsed);
}

int64_t IniFile::GetInt64(const std::wstring& section, const std::wstring& key,
                          int64_t default_value) const
{
    const std::wstring value = GetString(section, key, L"");
    if (value.empty()) {
        return default_value;
    }
    wchar_t* end = nullptr;
    const long long parsed = wcstoll(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return default_value;
    }
    return static_cast<int64_t>(parsed);
}

bool IniFile::GetBool(const std::wstring& section, const std::wstring& key,
                      bool default_value) const
{
    const std::wstring value = ToLowerAscii(GetString(section, key, L""));
    if (value == L"true" || value == L"1" || value == L"yes") {
        return true;
    }
    if (value == L"false" || value == L"0" || value == L"no") {
        return false;
    }
    return default_value;
}

void IniFile::SetString(const std::wstring& section, const std::wstring& key,
                        const std::wstring& value)
{
    m_data[section][key] = value;
}

void IniFile::SetInt(const std::wstring& section, const std::wstring& key, int value)
{
    m_data[section][key] = std::to_wstring(value);
}

void IniFile::SetInt64(const std::wstring& section, const std::wstring& key, int64_t value)
{
    m_data[section][key] = std::to_wstring(value);
}

void IniFile::SetBool(const std::wstring& section, const std::wstring& key, bool value)
{
    m_data[section][key] = value ? L"true" : L"false";
}

bool IniFile::Save(const std::wstring& path) const
{
    std::string output;
    output.append("\xEF\xBB\xBF", 3);
    for (const auto& section : m_data) {
        output += "[" + WideToUtf8(section.first) + "]\r\n";
        for (const auto& entry : section.second) {
            output += WideToUtf8(entry.first) + "=" + WideToUtf8(entry.second) + "\r\n";
        }
        output += "\r\n";
    }

    EnsureParentDirectory(path);
    const std::wstring temp_path = path + L".tmp";
    if (!WriteFileBytes(temp_path, output)) {
        return false;
    }
    if (!MoveFileExW(temp_path.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_path.c_str());
        return false;
    }
    return true;
}
