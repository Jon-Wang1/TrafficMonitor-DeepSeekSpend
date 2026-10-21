#pragma once

#include <ctime>
#include <string>

enum class DayBoundary
{
    China,
    Local,
    Utc
};

DayBoundary ParseDayBoundary(const std::wstring& value);
std::wstring DayBoundaryToString(DayBoundary boundary);
std::wstring DayBoundaryDisplayName(DayBoundary boundary);

int GetDayOffsetSeconds(DayBoundary boundary);
std::wstring DayKey(std::time_t now, DayBoundary boundary);
std::time_t DayStartEpoch(std::time_t now, DayBoundary boundary);

std::wstring FormatTimeHm(std::time_t value);
std::wstring FormatTimeHms(std::time_t value);
std::wstring FormatDateTime(std::time_t value);
