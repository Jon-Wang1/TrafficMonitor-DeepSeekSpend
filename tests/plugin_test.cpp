#include <windows.h>

#include "PluginInterface.h"

#include "config.h"
#include "http_client.h"
#include "resource.h"
#include "util.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::wstring& name)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", WideToUtf8(name).c_str());
    if (!condition) {
        ++g_failures;
    }
}

void PrintValue(const char* label, const std::wstring& value)
{
    std::printf("%s: %s\n", label, WideToUtf8(value).c_str());
}

} // namespace

int main(int argc, char** argv)
{
    const bool show_settings = argc > 1 && std::strcmp(argv[1], "--show-settings") == 0;
    const std::wstring config_dir = L"build\\plugin_test_config";
    CreateDirectoryW(config_dir.c_str(), nullptr);

    const std::wstring config_path = ConfigFilePath(config_dir);
    DeleteFileW(config_path.c_str());
    DeleteFileW((config_dir + L"\\DeepSeekUsage.log").c_str());

    PersistedData data;
    data.config.api_key = L"test-key";
    data.config.balance_interval_sec = 1;
    data.config.exact_interval_sec = 1;
    data.config.exact_enabled = false;
    data.config.currency = L"auto";
    data.config.day_boundary = DayBoundary::China;
    std::wstring error;
    Check(SaveConfigFile(config_path, data, error), L"save test config");

    SetEnvironmentVariableW(L"DEEPSEEK_USAGE_API_BASE_URL", L"http://127.0.0.1:8765");
    SetEnvironmentVariableW(L"DEEPSEEK_USAGE_PLATFORM_BASE_URL", L"http://127.0.0.1:8765");

    const HttpResult reset = HttpGet(L"http://127.0.0.1:8765/reset", {}, 5000);
    Check(reset.ok && reset.status_code == 200, L"reset mock server");

    HMODULE module = LoadLibraryW(L"build\\DeepSeekUsage_test.dll");
    Check(module != nullptr, L"load test DLL");
    if (module == nullptr) {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    Check(FindResourceW(module, MAKEINTRESOURCEW(IDD_SETTINGS), RT_DIALOG) != nullptr,
          L"settings dialog resource present");

    using GetInstanceFn = ITMPlugin* (*)();
    auto get_instance = reinterpret_cast<GetInstanceFn>(
        GetProcAddress(module, "TMPluginGetInstance"));
    Check(get_instance != nullptr, L"find export");
    if (get_instance == nullptr) {
        return 1;
    }

    ITMPlugin* plugin = get_instance();
    Check(plugin != nullptr, L"plugin instance");

    using GetPluginFn = void* (*)();
    using ApplyConfigFn = int (*)(void*, const AppConfig*, int, int, wchar_t*, int);
    using TestExactFn = int (*)(void*, const wchar_t*, const wchar_t*, int, wchar_t*, int);
    using TestBalanceFn = int (*)(void*, const wchar_t*, wchar_t*, int);

    auto get_plugin = reinterpret_cast<GetPluginFn>(
        GetProcAddress(module, "TMTestGetPlugin"));
    auto apply_config = reinterpret_cast<ApplyConfigFn>(
        GetProcAddress(module, "TMTestApplyConfig"));
    auto test_exact = reinterpret_cast<TestExactFn>(
        GetProcAddress(module, "TMTestExact"));
    auto test_balance = reinterpret_cast<TestBalanceFn>(
        GetProcAddress(module, "TMTestBalance"));
    Check(get_plugin != nullptr && apply_config != nullptr &&
              test_exact != nullptr && test_balance != nullptr,
          L"test hooks present");

    void* plugin_object = get_plugin != nullptr ? get_plugin() : nullptr;
    Check(plugin_object != nullptr, L"test plugin object");

    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, config_dir.c_str());
    plugin->OnInitialize(nullptr);

    if (show_settings) {
        plugin->ShowOptionsDialog(nullptr);
        return 0;
    }

    for (int i = 0; i < 12; ++i) {
        Sleep(500);
        plugin->DataRequired();
    }

    std::wstring today = plugin->GetItem(0)->GetItemValueText();
    std::wstring balance = plugin->GetItem(1)->GetItemValueText();
    std::wstring tooltip = plugin->GetTooltipInfo();
    PrintValue("today (estimate)", today);
    PrintValue("balance", balance);
    PrintValue("tooltip (estimate)", tooltip);

    Check(today == L"≈¥0.4", L"estimate value");
    Check(balance == L"¥99.6", L"balance value");
    Check(tooltip.find(L"估算") != std::wstring::npos, L"tooltip says estimate");

    AppConfig exact_config;
    exact_config.api_key.clear();
    exact_config.user_token = L"test-token";
    exact_config.balance_interval_sec = 1;
    exact_config.exact_interval_sec = 1;
    exact_config.exact_enabled = true;
    exact_config.currency = L"auto";
    exact_config.day_boundary = DayBoundary::China;
    wchar_t error_buffer[512]{};
    Check(apply_config(plugin_object, &exact_config, 0, 0, error_buffer, 512),
          L"apply exact config");

    for (int i = 0; i < 8; ++i) {
        Sleep(500);
        plugin->DataRequired();
    }

    today = plugin->GetItem(0)->GetItemValueText();
    tooltip = plugin->GetTooltipInfo();
    PrintValue("today (exact)", today);
    PrintValue("tooltip (exact)", tooltip);

    Check(today == L"¥0.4", L"exact value");
    Check(tooltip.find(L"精确") != std::wstring::npos, L"tooltip says exact");

    wchar_t test_message_buffer[1024]{};
    Check(test_exact(plugin_object, L"", L"CNY", 0, test_message_buffer, 1024),
          L"TestExact success");
    const std::wstring test_message = test_message_buffer;
    PrintValue("TestExact message", test_message);
    Check(test_message.find(L"0.35") != std::wstring::npos, L"TestExact cost in message");

    wchar_t balance_message_buffer[1024]{};
    Check(test_balance(plugin_object, L"", balance_message_buffer, 1024),
          L"TestBalance success");
    PrintValue("TestBalance message", balance_message_buffer);

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
