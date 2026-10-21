#pragma once

#include "timeutil.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <string>

#ifdef DEEPSEEK_USAGE_TEST_BASE_URLS
inline constexpr int kMinBalanceIntervalSec = 1;
inline constexpr int kMinExactIntervalSec = 1;
#else
inline constexpr int kMinBalanceIntervalSec = 30;
inline constexpr int kMinExactIntervalSec = 60;
#endif
inline constexpr int kMaxIntervalSec = 3600;

struct AppConfig
{
    std::wstring api_key;
    std::wstring user_token;
    int balance_interval_sec{60};
    int exact_interval_sec{300};
    bool exact_enabled{false};
    std::wstring currency{L"auto"};
    DayBoundary day_boundary{DayBoundary::China};
};

struct LedgerCurrencyState
{
    std::wstring date;
    int64_t spend_cents{0};
    int64_t last_balance_cents{0};
    bool has_last_balance{false};
    std::time_t last_sample_time{0};
    std::time_t coverage_start_time{0};
    bool incomplete{false};
    bool had_gap{false};
};

struct LedgerState
{
    std::wstring day_key;
    std::map<std::wstring, LedgerCurrencyState> currencies;
};

struct PersistedData
{
    AppConfig config;
    LedgerState ledger;
};

std::wstring ConfigFilePath(const std::wstring& config_dir);

bool LoadConfigFile(const std::wstring& path, PersistedData& data, std::wstring& warning);
bool SaveConfigFile(const std::wstring& path, const PersistedData& data, std::wstring& error);
