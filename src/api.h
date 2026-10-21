#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct BalanceEntry
{
    std::wstring currency;
    int64_t total_cents{0};
    int64_t granted_cents{0};
    int64_t topped_up_cents{0};
};

struct BalanceResult
{
    bool ok{false};
    bool is_available{true};
    bool auth_error{false};
    std::vector<BalanceEntry> entries;
    std::wstring error;
    int http_status{0};
};

struct ExactResult
{
    bool ok{false};
    bool auth_error{false};
    int64_t cost_micros{0};
    bool found_currency{false};
    std::wstring error;
    int http_status{0};
    int64_t negative_sum_micros{0};
    int64_t positive_sum_micros{0};
    size_t bucket_count{0};
};

BalanceResult FetchBalance(const std::wstring& api_key,
                           const std::wstring& base_url = L"https://api.deepseek.com");

ExactResult FetchExactCost(const std::wstring& user_token,
                           const std::wstring& currency,
                           std::time_t start_time,
                           std::time_t end_time,
                           int timezone_offset_seconds,
                           const std::wstring& base_url = L"https://platform.deepseek.com",
                           std::string* raw_body_out = nullptr);
