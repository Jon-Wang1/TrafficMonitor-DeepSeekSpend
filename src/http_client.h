#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct HttpHeader
{
    std::wstring name;
    std::wstring value;
};

struct HttpResult
{
    bool ok{false};
    int status_code{0};
    std::string body;
    std::wstring error;
    DWORD win32_error{0};
};

HttpResult HttpGet(const std::wstring& url,
                   const std::vector<HttpHeader>& headers,
                   int timeout_ms,
                   size_t max_body_bytes = 8 * 1024 * 1024);
