#include "timeutil.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int64_t kSecondsPerDay = 86400;

int64_t DaysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::time_t UtcEpochFromTm(const struct tm& tm)
{
    const int64_t days = DaysFromCivil(tm.tm_year + 1900,
                                       static_cast<unsigned>(tm.tm_mon + 1),
                                       static_cast<unsigned>(tm.tm_mday));
    return static_cast<std::time_t>(days * kSecondsPerDay);
}

} // namespace

DayBoundary ParseDayBoundary(const std::wstring& value)
{
    if (value == L"local") {
        return DayBoundary::Local;
    }
    if (value == L"utc") {
        return DayBoundary::Utc;
    }
    return DayBoundary::China;
}

std::wstring DayBoundaryToString(DayBoundary boundary)
{
    switch (boundary) {
    case DayBoundary::Local:
        return L"local";
    case DayBoundary::Utc:
        return L"utc";
    case DayBoundary::China:
    default:
        return L"cn";
    }
}

std::wstring DayBoundaryDisplayName(DayBoundary boundary)
{
    switch (boundary) {
    case DayBoundary::Local:
        return L"本机时区";
    case DayBoundary::Utc:
        return L"UTC";
    case DayBoundary::China:
    default:
        return L"北京时间（UTC+8）";
    }
}

int GetDayOffsetSeconds(DayBoundary boundary)
{
    switch (boundary) {
    case DayBoundary::China:
        return 8 * 3600;
    case DayBoundary::Utc:
        return 0;
    case DayBoundary::Local:
    default: {
        TIME_ZONE_INFORMATION tzi{};
        const DWORD result = GetTimeZoneInformation(&tzi);
        LONG bias = tzi.Bias;
        if (result == TIME_ZONE_ID_DAYLIGHT) {
            bias += tzi.DaylightBias;
        } else if (result == TIME_ZONE_ID_STANDARD) {
            bias += tzi.StandardBias;
        }
        // Bias is the difference in minutes between UTC and local time
        // (UTC = local + bias). The API expects the offset east of UTC.
        return static_cast<int>(-bias * 60);
    }
    }
}

std::wstring DayKey(std::time_t now, DayBoundary boundary)
{
    const int offset = GetDayOffsetSeconds(boundary);
    const std::time_t shifted = now + offset;
    struct tm tm_value{};
    gmtime_s(&tm_value, &shifted);
    wchar_t buffer[32]{};
    swprintf(buffer, 32, L"%04d-%02d-%02d",
             tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday);
    return buffer;
}

std::time_t DayStartEpoch(std::time_t now, DayBoundary boundary)
{
    const int offset = GetDayOffsetSeconds(boundary);
    const std::time_t shifted = now + offset;
    struct tm tm_value{};
    gmtime_s(&tm_value, &shifted);
    tm_value.tm_hour = 0;
    tm_value.tm_min = 0;
    tm_value.tm_sec = 0;
    return UtcEpochFromTm(tm_value) - offset;
}

std::wstring FormatTimeHm(std::time_t value)
{
    struct tm tm_value{};
    localtime_s(&tm_value, &value);
    wchar_t buffer[16]{};
    swprintf(buffer, 16, L"%02d:%02d", tm_value.tm_hour, tm_value.tm_min);
    return buffer;
}

std::wstring FormatTimeHms(std::time_t value)
{
    struct tm tm_value{};
    localtime_s(&tm_value, &value);
    wchar_t buffer[16]{};
    swprintf(buffer, 16, L"%02d:%02d:%02d",
             tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec);
    return buffer;
}

std::wstring FormatDateTime(std::time_t value)
{
    struct tm tm_value{};
    localtime_s(&tm_value, &value);
    wchar_t buffer[32]{};
    swprintf(buffer, 32, L"%04d-%02d-%02d %02d:%02d:%02d",
             tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday,
             tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec);
    return buffer;
}
