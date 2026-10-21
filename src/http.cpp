#include "http_client.h"

#include <winhttp.h>

#include <algorithm>

namespace {

std::wstring WinHttpErrorMessage(DWORD error)
{
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT:
        return L"请求超时";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return L"无法连接到服务器";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"域名解析失败";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"TLS/证书校验失败";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return L"连接被重置";
    case ERROR_WINHTTP_INVALID_URL:
        return L"URL 无效";
    case ERROR_WINHTTP_OPERATION_CANCELLED:
        return L"请求被取消";
    case ERROR_WINHTTP_INTERNAL_ERROR:
        return L"WinHTTP 内部错误";
    default:
        return L"网络错误（WinHTTP 代码 " + std::to_wstring(error) + L"）";
    }
}

void CloseIfValid(HINTERNET handle)
{
    if (handle != nullptr) {
        WinHttpCloseHandle(handle);
    }
}

} // namespace

HttpResult HttpGet(const std::wstring& url,
                   const std::vector<HttpHeader>& headers,
                   int timeout_ms,
                   size_t max_body_bytes)
{
    HttpResult result;

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.lpszHostName == nullptr) {
        result.error = L"URL 解析失败";
        return result;
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath != nullptr ? components.lpszUrlPath : L"",
                      components.dwUrlPathLength);
    if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }
    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(L"DeepSeekUsageTrafficMonitor/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        result.win32_error = GetLastError();
        result.error = WinHttpErrorMessage(result.win32_error);
        return result;
    }

    const int timeout = std::max(1000, timeout_ms);
    WinHttpSetTimeouts(session, 5000, 5000, timeout, timeout);

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET connection = WinHttpConnect(session, host.c_str(),
                                          static_cast<INTERNET_PORT>(components.nPort), 0);
    if (connection == nullptr) {
        result.win32_error = GetLastError();
        result.error = WinHttpErrorMessage(result.win32_error);
        CloseIfValid(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (request == nullptr) {
        result.win32_error = GetLastError();
        result.error = WinHttpErrorMessage(result.win32_error);
        CloseIfValid(connection);
        CloseIfValid(session);
        return result;
    }

    std::wstring header_block = L"Accept-Encoding: identity\r\n";
    for (const HttpHeader& header : headers) {
        header_block += header.name + L": " + header.value + L"\r\n";
    }
    if (!WinHttpAddRequestHeaders(request, header_block.c_str(),
                                  static_cast<DWORD>(header_block.size()),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        result.win32_error = GetLastError();
        result.error = L"设置请求头失败（代码 " + std::to_wstring(result.win32_error) + L"）";
        CloseIfValid(request);
        CloseIfValid(connection);
        CloseIfValid(session);
        return result;
    }

    bool failed = false;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        result.win32_error = GetLastError();
        result.error = WinHttpErrorMessage(result.win32_error);
        failed = true;
    }

    if (!failed) {
        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status_code,
                                 &status_size, WINHTTP_NO_HEADER_INDEX)) {
            result.win32_error = GetLastError();
            result.error = L"读取 HTTP 状态码失败（代码 " +
                           std::to_wstring(result.win32_error) + L"）";
            failed = true;
        } else {
            result.status_code = static_cast<int>(status_code);
        }
    }

    if (!failed) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            if (result.body.size() + static_cast<size_t>(available) > max_body_bytes) {
                result.error = L"响应体过大";
                failed = true;
                break;
            }
            std::string chunk(static_cast<size_t>(available), '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                result.win32_error = GetLastError();
                result.error = WinHttpErrorMessage(result.win32_error);
                failed = true;
                break;
            }
            chunk.resize(read);
            result.body += chunk;
        }
    }

    CloseIfValid(request);
    CloseIfValid(connection);
    CloseIfValid(session);

    result.ok = !failed;
    if (result.ok) {
        result.error.clear();
    }
    return result;
}
