#include <windows.h>

#include "config.h"
#include "plugin.h"
#include "util.h"

#include <algorithm>
#include <cwchar>
#include <string>

#ifdef DEEPSEEK_USAGE_TEST_BASE_URLS
namespace {

void CopyWideMessage(const std::wstring& message, wchar_t* buffer, int buffer_length)
{
    if (buffer == nullptr || buffer_length <= 0) {
        return;
    }
    const int count = std::min<int>(buffer_length - 1, static_cast<int>(message.size()));
    if (count > 0) {
        std::wmemcpy(buffer, message.c_str(), static_cast<size_t>(count));
    }
    buffer[count] = L'\0';
}

} // namespace

extern "C" __declspec(dllexport) void* TMTestGetPlugin()
{
    return &CDeepSeekPlugin::Instance();
}

extern "C" __declspec(dllexport) int TMTestApplyConfig(void* plugin, const AppConfig* config,
                                                        int clear_api_key, int clear_user_token,
                                                        wchar_t* error, int error_length)
{
    if (plugin == nullptr || config == nullptr) {
        return 0;
    }
    std::wstring message;
    const bool ok = static_cast<CDeepSeekPlugin*>(plugin)->ApplyConfig(
        *config, clear_api_key != 0, clear_user_token != 0, message);
    CopyWideMessage(message, error, error_length);
    return ok ? 1 : 0;
}

extern "C" __declspec(dllexport) int TMTestExact(void* plugin, const wchar_t* user_token,
                                                  const wchar_t* currency, int boundary,
                                                  wchar_t* message, int message_length)
{
    if (plugin == nullptr) {
        return 0;
    }
    std::wstring result;
    const bool ok = static_cast<CDeepSeekPlugin*>(plugin)->TestExact(
        user_token != nullptr ? user_token : L"",
        currency != nullptr ? currency : L"auto",
        static_cast<DayBoundary>(boundary), result);
    CopyWideMessage(result, message, message_length);
    return ok ? 1 : 0;
}

extern "C" __declspec(dllexport) int TMTestBalance(void* plugin, const wchar_t* api_key,
                                                    wchar_t* message, int message_length)
{
    if (plugin == nullptr) {
        return 0;
    }
    std::wstring result;
    const bool ok = static_cast<CDeepSeekPlugin*>(plugin)->TestBalance(
        api_key != nullptr ? api_key : L"", result);
    CopyWideMessage(result, message, message_length);
    return ok ? 1 : 0;
}
#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr) {
            CDeepSeekPlugin::ShutdownExisting();
        }
    }
    return TRUE;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    DebugLog(L"TMPluginGetInstance called");
    return &CDeepSeekPlugin::Instance();
}
