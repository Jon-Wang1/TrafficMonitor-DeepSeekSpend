#include "api.h"

#include "http_client.h"
#include "json.h"
#include "util.h"

#include <cstdio>
#include <ctime>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace {

const wchar_t* kBrowserUserAgent =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

std::wstring HttpErrorMessage(int status)
{
    switch (status) {
    case 401:
        return L"HTTP 401（认证失败）";
    case 402:
        return L"HTTP 402（余额不足）";
    case 403:
        return L"HTTP 403（拒绝访问）";
    case 429:
        return L"HTTP 429（请求过于频繁）";
    default:
        if (status >= 500) {
            return L"服务端错误（HTTP " + std::to_wstring(status) + L"）";
        }
        return L"HTTP " + std::to_wstring(status);
    }
}

bool ParseBalanceEntry(const JsonValue& value, BalanceEntry& entry)
{
    if (!value.IsObject()) {
        return false;
    }
    const JsonValue* currency = value.Find("currency");
    if (currency == nullptr || !currency->IsString() || currency->string_value.empty()) {
        return false;
    }
    entry.currency = NormalizeCurrency(Utf8ToWide(currency->string_value));

    const JsonValue* total = value.Find("total_balance");
    const JsonValue* granted = value.Find("granted_balance");
    const JsonValue* topped_up = value.Find("topped_up_balance");

    if (total != nullptr) {
        entry.total_cents = AmountToCents(total->AsString());
    }
    if (granted != nullptr) {
        entry.granted_cents = AmountToCents(granted->AsString());
    }
    if (topped_up != nullptr) {
        entry.topped_up_cents = AmountToCents(topped_up->AsString());
    }
    return true;
}

int64_t CostValueToMicros(const JsonValue& value)
{
    if (value.IsObject()) {
        const JsonValue* inner = value.Find("value");
        if (inner != nullptr) {
            return CostValueToMicros(*inner);
        }
        return 0;
    }
    return AmountToMicros(value.AsString());
}

const JsonValue* FindBizDataArray(const JsonValue& root)
{
    const JsonValue* data = root.Find("data");
    if (data == nullptr || !data->IsObject()) {
        return nullptr;
    }
    const JsonValue* biz_data = data->Find("biz_data");
    if (biz_data == nullptr) {
        return nullptr;
    }
    if (biz_data->IsArray()) {
        return biz_data;
    }
    if (biz_data->IsObject()) {
        const JsonValue* inner = biz_data->Find("data");
        if (inner != nullptr && inner->IsArray()) {
            return inner;
        }
    }
    return nullptr;
}

} // namespace

BalanceResult FetchBalance(const std::wstring& api_key, const std::wstring& base_url)
{
    BalanceResult result;
    if (api_key.empty()) {
        result.error = L"未配置 API Key";
        return result;
    }

    const std::wstring url = base_url + L"/user/balance";
    std::vector<HttpHeader> headers{
        {L"Authorization", L"Bearer " + api_key},
        {L"Accept", L"application/json"},
        {L"User-Agent", L"DeepSeekUsageTrafficMonitor/1.0"},
    };

    const HttpResult http = HttpGet(url, headers, 10000);
    result.http_status = http.status_code;
    if (!http.ok) {
        result.error = http.error;
        return result;
    }
    if (http.status_code == 401 || http.status_code == 403) {
        result.auth_error = true;
        result.error = HttpErrorMessage(http.status_code);
        return result;
    }
    if (http.status_code != 200) {
        result.error = HttpErrorMessage(http.status_code);
        return result;
    }

    JsonValue root;
    std::string parse_error;
    if (!ParseJson(http.body, root, parse_error) || !root.IsObject()) {
        result.error = L"余额响应解析失败";
        return result;
    }

    const JsonValue* is_available = root.Find("is_available");
    result.is_available = is_available != nullptr ? is_available->AsBool(true) : true;

    const JsonValue* infos = root.Find("balance_infos");
    if (infos == nullptr || !infos->IsArray()) {
        result.error = L"余额响应缺少 balance_infos";
        return result;
    }

    for (const JsonValue& item : infos->array) {
        BalanceEntry entry;
        if (ParseBalanceEntry(item, entry)) {
            result.entries.push_back(std::move(entry));
        }
    }

    result.ok = true;
    return result;
}

ExactResult FetchExactCost(const std::wstring& user_token,
                           const std::wstring& currency,
                           std::time_t start_time,
                           std::time_t end_time,
                           int timezone_offset_seconds,
                           const std::wstring& base_url,
                           std::string* raw_body_out)
{
    ExactResult result;
    if (user_token.empty()) {
        result.error = L"未配置 userToken";
        return result;
    }

    (void)start_time;
    const std::time_t shifted = end_time + timezone_offset_seconds;
    struct tm date_parts{};
    gmtime_s(&date_parts, &shifted);
    const int year = date_parts.tm_year + 1900;
    const int month = date_parts.tm_mon + 1;
    wchar_t date_key[16]{};
    swprintf(date_key, 16, L"%04d-%02d-%02d", year, month, date_parts.tm_mday);

    const std::wstring url =
        base_url + L"/api/v0/usage/cost?month=" + std::to_wstring(month) +
        L"&year=" + std::to_wstring(year);

    std::vector<HttpHeader> headers{
        {L"Authorization", L"Bearer " + user_token},
        {L"Accept", L"application/json"},
        {L"x-client-platform", L"web"},
        {L"Origin", L"https://platform.deepseek.com"},
        {L"Referer", L"https://platform.deepseek.com/usage"},
        {L"User-Agent", kBrowserUserAgent},
    };

    const HttpResult http = HttpGet(url, headers, 15000);
    result.http_status = http.status_code;
    if (raw_body_out != nullptr) {
        *raw_body_out = http.body;
    }
    if (!http.ok) {
        result.error = http.error;
        return result;
    }
    if (http.status_code == 401 || http.status_code == 403) {
        result.auth_error = true;
        result.error = HttpErrorMessage(http.status_code);
        return result;
    }
    if (http.status_code != 200) {
        result.error = HttpErrorMessage(http.status_code);
        return result;
    }

    JsonValue root;
    std::string parse_error;
    if (!ParseJson(http.body, root, parse_error) || !root.IsObject()) {
        result.error = L"精确用量响应解析失败";
        return result;
    }

    int64_t code = 0;
    if (const JsonValue* code_value = root.Find("code")) {
        code = static_cast<int64_t>(code_value->AsDouble());
    }
    if (code == 40002 || code == 40003) {
        result.auth_error = true;
        result.error = L"userToken 已失效（code " + std::to_wstring(code) + L"）";
        return result;
    }
    if (code != 0) {
        std::wstring message;
        if (const JsonValue* msg = root.Find("msg"); msg != nullptr && msg->IsString()) {
            message = Utf8ToWide(msg->string_value);
        }
        result.error = L"平台接口错误（code " + std::to_wstring(code) + L"）";
        if (!message.empty()) {
            result.error += L"：" + message;
        }
        return result;
    }

    const JsonValue* data = root.Find("data");
    if (data == nullptr || !data->IsObject()) {
        result.error = L"精确用量响应缺少 data";
        return result;
    }
    if (const JsonValue* biz_code = data->Find("biz_code")) {
        const int64_t biz = static_cast<int64_t>(biz_code->AsDouble());
        if (biz != 0) {
            result.error = L"平台接口 biz_code=" + std::to_wstring(biz);
            return result;
        }
    }

    const JsonValue* currencies = FindBizDataArray(root);
    if (currencies == nullptr) {
        result.ok = true;
        return result;
    }

    const std::wstring wanted_currency = NormalizeCurrency(currency);
    int64_t total_micros = 0;
    bool currency_matched = false;

    for (const JsonValue& currency_entry : currencies->array) {
        if (!currency_entry.IsObject()) {
            continue;
        }
        const JsonValue* currency_value = currency_entry.Find("currency");
        const std::wstring entry_currency = currency_value != nullptr
            ? NormalizeCurrency(Utf8ToWide(currency_value->AsString()))
            : std::wstring();
        if (entry_currency != wanted_currency) {
            continue;
        }
        currency_matched = true;

        const JsonValue* days = currency_entry.Find("days");
        if (days == nullptr || !days->IsArray()) {
            continue;
        }

        for (const JsonValue& day : days->array) {
            if (!day.IsObject()) {
                continue;
            }
            const JsonValue* date_value = day.Find("date");
            if (date_value == nullptr || !date_value->IsString() ||
                Utf8ToWide(date_value->string_value) != date_key) {
                continue;
            }

            const JsonValue* models = day.Find("data");
            if (models == nullptr || !models->IsArray()) {
                break;
            }

            for (const JsonValue& model : models->array) {
                if (!model.IsObject()) {
                    continue;
                }
                const JsonValue* usage = model.Find("usage");
                if (usage == nullptr || !usage->IsArray()) {
                    continue;
                }
                for (const JsonValue& item : usage->array) {
                    if (!item.IsObject()) {
                        continue;
                    }
                    const JsonValue* type_value = item.Find("type");
                    const std::wstring type_text = type_value != nullptr
                        ? Utf8ToWide(type_value->AsString())
                        : std::wstring();
                    if (type_text == L"REQUEST") {
                        continue;
                    }
                    const JsonValue* amount = item.Find("amount");
                    if (amount == nullptr) {
                        continue;
                    }
                    const int64_t micros = AmountToMicros(amount->AsString());
                    if (micros < 0) {
                        result.negative_sum_micros += micros;
                    } else {
                        result.positive_sum_micros += micros;
                    }
                    total_micros += micros;
                    ++result.bucket_count;
                }
            }
            break;
        }
        break;
    }

    result.found_currency = currency_matched;
    if (result.positive_sum_micros == 0 && result.negative_sum_micros < 0) {
        total_micros = -result.negative_sum_micros;
    } else if (total_micros < 0) {
        total_micros = -total_micros;
    }

    result.cost_micros = total_micros;
    result.ok = true;
    return result;
}
