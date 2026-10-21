#include "ledger.h"

#include "util.h"

#include <algorithm>
#include <map>

namespace {

bool IsTrackedCurrency(const std::wstring& currency)
{
    return currency == L"CNY" || currency == L"USD";
}

} // namespace

bool SelectCurrency(const std::vector<BalanceEntry>& entries,
                    const std::wstring& preference,
                    std::wstring& selected_currency,
                    const BalanceEntry*& selected_entry,
                    std::wstring& other_currency,
                    const BalanceEntry*& other_entry)
{
    selected_currency.clear();
    selected_entry = nullptr;
    other_currency.clear();
    other_entry = nullptr;

    std::map<std::wstring, const BalanceEntry*> by_currency;
    for (const BalanceEntry& entry : entries) {
        by_currency[NormalizeCurrency(entry.currency)] = &entry;
    }

    const std::wstring normalized_preference = NormalizeCurrency(preference);
    if (normalized_preference == L"CNY" || normalized_preference == L"USD") {
        const auto selected_it = by_currency.find(normalized_preference);
        if (selected_it != by_currency.end()) {
            selected_currency = selected_it->first;
            selected_entry = selected_it->second;
        }
    } else {
        auto cny = by_currency.find(L"CNY");
        auto usd = by_currency.find(L"USD");
        if (cny != by_currency.end()) {
            selected_currency = L"CNY";
            selected_entry = cny->second;
        } else if (usd != by_currency.end()) {
            selected_currency = L"USD";
            selected_entry = usd->second;
        }
    }

    if (!selected_currency.empty()) {
        const std::wstring other = selected_currency == L"CNY" ? L"USD" : L"CNY";
        const auto other_it = by_currency.find(other);
        if (other_it != by_currency.end()) {
            other_currency = other;
            other_entry = other_it->second;
        }
    }

    return selected_entry != nullptr;
}

void ApplyBalanceSamples(LedgerState& ledger,
                         const std::vector<BalanceEntry>& entries,
                         DayBoundary boundary,
                         std::time_t now,
                         bool& changed)
{
    changed = false;
    const std::wstring today = DayKey(now, boundary);
    const std::time_t day_start = DayStartEpoch(now, boundary);
    const bool started_late = (now - day_start) > 120;

    if (ledger.day_key != today) {
        ledger.day_key = today;
        changed = true;
    }

    for (const BalanceEntry& entry : entries) {
        const std::wstring currency = NormalizeCurrency(entry.currency);
        if (!IsTrackedCurrency(currency)) {
            continue;
        }

        LedgerCurrencyState& state = ledger.currencies[currency];
        if (state.date != today) {
            state.date = today;
            state.spend_cents = 0;
            state.last_balance_cents = entry.total_cents;
            state.has_last_balance = true;
            state.last_sample_time = now;
            state.coverage_start_time = now;
            state.incomplete = started_late;
            state.had_gap = false;
            changed = true;
            continue;
        }

        if (!state.has_last_balance) {
            state.last_balance_cents = entry.total_cents;
            state.has_last_balance = true;
            state.last_sample_time = now;
            state.coverage_start_time = now;
            state.incomplete = true;
            changed = true;
            continue;
        }

        const int64_t difference = state.last_balance_cents - entry.total_cents;
        if (difference > 0) {
            state.spend_cents += difference;
            changed = true;
        }
        if (now - state.last_sample_time > 600) {
            state.had_gap = true;
            changed = true;
        }
        state.last_balance_cents = entry.total_cents;
        state.last_sample_time = now;
        changed = true;
    }
}
