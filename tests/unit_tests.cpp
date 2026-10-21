#include "api.h"
#include "http_client.h"
#include "json.h"
#include "ledger.h"
#include "timeutil.h"
#include "util.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) {
        ++g_failures;
    }
}

std::vector<BalanceEntry> OneBalance(const wchar_t* currency, int64_t cents)
{
    BalanceEntry entry;
    entry.currency = currency;
    entry.total_cents = cents;
    return {entry};
}

} // namespace

int main()
{
    // ---------- JSON ----------
    JsonValue value;
    std::string error;
    Check(ParseJson(R"({"a":1,"b":"x","c":[true,null,{"d":2.5}]})", value, error),
          "json: basic object");
    Check(value.Find("a") != nullptr && value.Find("a")->AsDouble() == 1.0,
          "json: number");
    Check(value.Find("b") != nullptr && value.Find("b")->string_value == "x",
          "json: string");
    Check(value.Find("c") != nullptr && value.Find("c")->At(2) != nullptr &&
              value.Find("c")->At(2)->Find("d")->AsDouble() == 2.5,
          "json: nested array");
    Check(ParseJson("{\"x\":\"\\u4e2d\"}", value, error) &&
              value.Find("x")->string_value == std::string("\xE4\xB8\xAD"),
          "json: unicode escape");
    Check(ParseJson("{\"x\":\"\\uD83D\\uDE00\"}", value, error) &&
              value.Find("x")->string_value == std::string("\xF0\x9F\x98\x80"),
          "json: surrogate pair");
    Check(!ParseJson("{\"a\":}", value, error), "json: malformed rejected");

    // ---------- amounts ----------
    Check(AmountToCents("110.00") == 11000, "amount: cents");
    Check(AmountToCents("0.01") == 1, "amount: one cent");
    Check(AmountToMicros("0.123456") == 123456, "amount: micros");
    Check(FormatCents(42) == L"0.42", "amount: format cents");
    Check(FormatMicros(12345) == L"0.01", "amount: format micros round down");
    Check(FormatMicros(12600) == L"0.01", "amount: format micros 0.0126");
    Check(FormatMicros(12900) == L"0.01", "amount: format micros 0.0129");
    Check(FormatMicros(12500) == L"0.01", "amount: format micros half");
    Check(FormatMicros(350000) == L"0.35", "amount: format 0.35");
    Check(NormalizeCurrency(L"cny") == L"CNY", "amount: normalize currency");

    // ---------- day boundary ----------
    const std::time_t chinaDayStart = 1767196800; // 2026-01-01 00:00 Beijing
    const std::time_t tenAm = chinaDayStart + 10 * 3600;
    Check(DayKey(tenAm, DayBoundary::China) == L"2026-01-01", "time: day key Beijing");
    Check(DayStartEpoch(tenAm, DayBoundary::China) == chinaDayStart,
          "time: day start Beijing");
    Check(DayKey(1767225600, DayBoundary::Utc) == L"2026-01-01", "time: day key UTC");
    Check(DayStartEpoch(1767225600, DayBoundary::Utc) == 1767225600,
          "time: day start UTC");

    // ---------- ledger ----------
    LedgerState ledger;
    bool changed = false;

    ApplyBalanceSamples(ledger, OneBalance(L"CNY", 10000), DayBoundary::China, tenAm, changed);
    Check(changed && ledger.currencies[L"CNY"].spend_cents == 0,
          "ledger: first sample baseline");
    Check(ledger.currencies[L"CNY"].incomplete, "ledger: late start marked incomplete");
    Check(ledger.currencies[L"CNY"].coverage_start_time == tenAm,
          "ledger: coverage start time");

    ApplyBalanceSamples(ledger, OneBalance(L"CNY", 9550), DayBoundary::China,
                        tenAm + 60, changed);
    Check(ledger.currencies[L"CNY"].spend_cents == 450, "ledger: decrease accumulated");

    ApplyBalanceSamples(ledger, OneBalance(L"CNY", 10000), DayBoundary::China,
                        tenAm + 120, changed);
    Check(ledger.currencies[L"CNY"].spend_cents == 450, "ledger: top-up ignored");

    ApplyBalanceSamples(ledger, OneBalance(L"CNY", 9800), DayBoundary::China,
                        tenAm + 180, changed);
    Check(ledger.currencies[L"CNY"].spend_cents == 650, "ledger: spend after top-up");

    ApplyBalanceSamples(ledger, OneBalance(L"CNY", 9700), DayBoundary::China,
                        tenAm + 180 + 700, changed);
    Check(ledger.currencies[L"CNY"].spend_cents == 750, "ledger: gap delta counted");
    Check(ledger.currencies[L"CNY"].had_gap, "ledger: gap flagged");

    const std::time_t nextDay = chinaDayStart + 86400 + 5;
    ApplyBalanceSamples(ledger, OneBalance(L"CNY", 9700), DayBoundary::China,
                        nextDay, changed);
    Check(ledger.currencies[L"CNY"].spend_cents == 0, "ledger: day reset");
    Check(ledger.currencies[L"CNY"].date == L"2026-01-02", "ledger: new date key");
    Check(!ledger.currencies[L"CNY"].incomplete, "ledger: midnight sample not incomplete");
    Check(ledger.currencies[L"CNY"].coverage_start_time == nextDay,
          "ledger: new coverage start");

    std::vector<BalanceEntry> entries;
    BalanceEntry cny;
    cny.currency = L"CNY";
    cny.total_cents = 12345;
    BalanceEntry usd;
    usd.currency = L"USD";
    usd.total_cents = 500;
    entries = {cny, usd};

    std::wstring selected_currency;
    const BalanceEntry* selected_entry = nullptr;
    std::wstring other_currency;
    const BalanceEntry* other_entry = nullptr;
    Check(SelectCurrency(entries, L"auto", selected_currency, selected_entry,
                         other_currency, other_entry) &&
              selected_currency == L"CNY" && selected_entry->total_cents == 12345 &&
              other_currency == L"USD",
          "ledger: auto currency selection");
    Check(SelectCurrency(entries, L"USD", selected_currency, selected_entry,
                         other_currency, other_entry) &&
              selected_currency == L"USD" && selected_entry->total_cents == 500,
          "ledger: explicit currency selection");

    // ---------- HTTP API against mock server ----------
    const std::wstring mock = L"http://127.0.0.1:8765";
    const HttpResult reset = HttpGet(mock + L"/reset", {}, 5000);
    Check(reset.ok && reset.status_code == 200, "api: mock reset");

    const BalanceResult balance = FetchBalance(L"sk-test", mock);
    Check(balance.ok && balance.entries.size() == 2, "api: balance ok");
    bool found_cny = false;
    bool found_usd = false;
    for (const BalanceEntry& entry : balance.entries) {
        if (entry.currency == L"CNY") {
            found_cny = entry.total_cents == 10000 && entry.granted_cents == 0;
        }
        if (entry.currency == L"USD") {
            found_usd = entry.total_cents == 500;
        }
    }
    Check(found_cny, "api: balance CNY parsed");
    Check(found_usd, "api: balance USD parsed");

    const BalanceResult auth = FetchBalance(L"sk-bad", mock + L"/bal401");
    Check(!auth.ok && auth.auth_error, "api: balance 401 flagged");

    const std::time_t now = std::time(nullptr);
    const ExactResult exact = FetchExactCost(L"token", L"CNY", now - 3600, now + 60,
                                             28800, mock);
    Check(exact.ok && exact.cost_micros == 350000 && exact.bucket_count == 2,
          "api: exact CNY cost sums, USD ignored");
    Check(exact.negative_sum_micros == 0 && exact.positive_sum_micros == 350000,
          "api: exact positive sums");

    const ExactResult negative = FetchExactCost(L"token", L"CNY", now - 3600, now + 60,
                                                28800, mock + L"/negative");
    Check(negative.ok && negative.cost_micros == 350000,
          "api: negative charge sign normalized");
    Check(negative.negative_sum_micros == -350000, "api: negative sums tracked");

    const ExactResult auth_fail = FetchExactCost(L"token", L"CNY", now - 3600, now + 60,
                                                 28800, mock + L"/authfail");
    Check(!auth_fail.ok && auth_fail.auth_error, "api: exact auth error flagged");

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
