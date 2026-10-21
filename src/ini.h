#pragma once

#include <cstdint>
#include <map>
#include <string>

class IniFile
{
public:
    bool Load(const std::wstring& path);

    std::wstring GetString(const std::wstring& section, const std::wstring& key,
                           const std::wstring& default_value) const;
    int GetInt(const std::wstring& section, const std::wstring& key, int default_value) const;
    int64_t GetInt64(const std::wstring& section, const std::wstring& key,
                     int64_t default_value) const;
    bool GetBool(const std::wstring& section, const std::wstring& key,
                 bool default_value) const;

    void SetString(const std::wstring& section, const std::wstring& key,
                   const std::wstring& value);
    void SetInt(const std::wstring& section, const std::wstring& key, int value);
    void SetInt64(const std::wstring& section, const std::wstring& key, int64_t value);
    void SetBool(const std::wstring& section, const std::wstring& key, bool value);

    bool Save(const std::wstring& path) const;

private:
    std::map<std::wstring, std::map<std::wstring, std::wstring>> m_data;
};
