#pragma once

#include "api.h"
#include "config.h"
#include "timeutil.h"

#include <ctime>
#include <string>
#include <vector>

bool SelectCurrency(const std::vector<BalanceEntry>& entries,
                    const std::wstring& preference,
                    std::wstring& selected_currency,
                    const BalanceEntry*& selected_entry,
                    std::wstring& other_currency,
                    const BalanceEntry*& other_entry);

void ApplyBalanceSamples(LedgerState& ledger,
                         const std::vector<BalanceEntry>& entries,
                         DayBoundary boundary,
                         std::time_t now,
                         bool& changed);
