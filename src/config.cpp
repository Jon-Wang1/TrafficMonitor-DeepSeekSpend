#include "config.h"

#include "dpapi_util.h"
#include "ini.h"
#include "util.h"

#include <algorithm>

namespace {

const wchar_t* kConfigSection = L"config";
const wchar_t* kStateSection = L"state";

int ClampInt(int value, int minimum, int maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

void LoadCurrencyState(const IniFile& ini, const std::wstring& currency,
                       LedgerCurrencyState& state)
{
    const std::wstring suffix = L"_" + currency;
    state.date = ini.GetString(kStateSection, L"date" + suffix, L"");
    state.spend_cents = ini.GetInt64(kStateSection, L"spend" + suffix, 0);
    state.last_balance_cents = ini.GetInt64(kStateSection, L"last" + suffix, 0);
    state.has_last_balance = ini.GetBool(kStateSection, L"has_last" + suffix, false);
    state.last_sample_time = static_cast<std::time_t>(
        ini.GetInt64(kStateSection, L"last_sample" + suffix, 0));
    state.coverage_start_time = static_cast<std::time_t>(
        ini.GetInt64(kStateSection, L"coverage" + suffix, 0));
    state.incomplete = ini.GetBool(kStateSection, L"incomplete" + suffix, false);
    state.had_gap = ini.GetBool(kStateSection, L"had_gap" + suffix, false);
}

void SaveCurrencyState(IniFile& ini, const std::wstring& currency,
                       const LedgerCurrencyState& state)
{
    const std::wstring suffix = L"_" + currency;
    ini.SetString(kStateSection, L"date" + suffix, state.date);
    ini.SetInt64(kStateSection, L"spend" + suffix, state.spend_cents);
    ini.SetInt64(kStateSection, L"last" + suffix, state.last_balance_cents);
    ini.SetBool(kStateSection, L"has_last" + suffix, state.has_last_balance);
    ini.SetInt64(kStateSection, L"last_sample" + suffix,
                 static_cast<int64_t>(state.last_sample_time));
    ini.SetInt64(kStateSection, L"coverage" + suffix,
                 static_cast<int64_t>(state.coverage_start_time));
    ini.SetBool(kStateSection, L"incomplete" + suffix, state.incomplete);
    ini.SetBool(kStateSection, L"had_gap" + suffix, state.had_gap);
}

} // namespace

std::wstring ConfigFilePath(const std::wstring& config_dir)
{
    std::wstring directory = config_dir;
    if (!directory.empty()) {
        const wchar_t last = directory.back();
        if (last != L'\\' && last != L'/') {
            directory.push_back(L'\\');
        }
    }
    return directory + L"DeepSeekUsage.ini";
}

bool LoadConfigFile(const std::wstring& path, PersistedData& data, std::wstring& warning)
{
    warning.clear();
    data = PersistedData{};

    IniFile ini;
    if (!ini.Load(path)) {
        warning = L"无法读取配置文件：" + path;
        return false;
    }

    AppConfig& config = data.config;
    const std::wstring encrypted_key = ini.GetString(kConfigSection, L"api_key_enc", L"");
    if (!encrypted_key.empty() && !DpapiUnprotect(encrypted_key, config.api_key)) {
        warning = L"API Key 解密失败，已忽略已保存的 Key";
    }

    const std::wstring encrypted_token = ini.GetString(kConfigSection, L"user_token_enc", L"");
    if (!encrypted_token.empty() && !DpapiUnprotect(encrypted_token, config.user_token)) {
        if (!warning.empty()) {
            warning += L"；";
        }
        warning += L"userToken 解密失败，已忽略已保存的 Token";
    }

    config.balance_interval_sec = ClampInt(
        ini.GetInt(kConfigSection, L"balance_interval_sec", 60),
        kMinBalanceIntervalSec, kMaxIntervalSec);
    config.exact_interval_sec = ClampInt(
        ini.GetInt(kConfigSection, L"exact_interval_sec", 300),
        kMinExactIntervalSec, kMaxIntervalSec);
    config.exact_enabled = ini.GetBool(kConfigSection, L"exact_enabled", false);

    const std::wstring currency = NormalizeCurrency(
        ini.GetString(kConfigSection, L"currency", L"auto"));
    if (currency == L"CNY" || currency == L"USD") {
        config.currency = currency;
    } else {
        config.currency = L"auto";
    }

    config.day_boundary = ParseDayBoundary(
        ini.GetString(kConfigSection, L"day_boundary", L"cn"));

    data.ledger.day_key = ini.GetString(kStateSection, L"day_key", L"");

    for (const std::wstring& currency_code : {std::wstring(L"CNY"), std::wstring(L"USD")}) {
        LedgerCurrencyState state;
        LoadCurrencyState(ini, currency_code, state);
        if (!state.date.empty() || state.has_last_balance || state.spend_cents != 0) {
            data.ledger.currencies[currency_code] = state;
        }
    }

    return true;
}

bool SaveConfigFile(const std::wstring& path, const PersistedData& data, std::wstring& error)
{
    error.clear();

    std::wstring encrypted_key;
    if (!DpapiProtect(data.config.api_key, encrypted_key)) {
        error = L"API Key 加密失败";
        return false;
    }
    std::wstring encrypted_token;
    if (!DpapiProtect(data.config.user_token, encrypted_token)) {
        error = L"userToken 加密失败";
        return false;
    }

    IniFile ini;
    ini.SetString(kConfigSection, L"api_key_enc", encrypted_key);
    ini.SetString(kConfigSection, L"user_token_enc", encrypted_token);
    ini.SetInt(kConfigSection, L"balance_interval_sec", data.config.balance_interval_sec);
    ini.SetInt(kConfigSection, L"exact_interval_sec", data.config.exact_interval_sec);
    ini.SetBool(kConfigSection, L"exact_enabled", data.config.exact_enabled);
    ini.SetString(kConfigSection, L"currency", data.config.currency);
    ini.SetString(kConfigSection, L"day_boundary",
                  DayBoundaryToString(data.config.day_boundary));

    ini.SetString(kStateSection, L"day_key", data.ledger.day_key);
    SaveCurrencyState(ini, L"CNY",
                      data.ledger.currencies.count(L"CNY")
                          ? data.ledger.currencies.at(L"CNY")
                          : LedgerCurrencyState{});
    SaveCurrencyState(ini, L"USD",
                      data.ledger.currencies.count(L"USD")
                          ? data.ledger.currencies.at(L"USD")
                          : LedgerCurrencyState{});

    if (!ini.Save(path)) {
        error = L"写入配置文件失败：" + path;
        return false;
    }
    return true;
}
