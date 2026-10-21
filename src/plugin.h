#pragma once

#include "PluginInterface.h"

#include "api.h"
#include "config.h"
#include "timeutil.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class CDeepSeekItemBase : public IPluginItem
{
public:
    CDeepSeekItemBase(std::wstring name, std::wstring id,
                      std::wstring label, std::wstring sample);

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;

    void SetValue(std::wstring value);

private:
    std::wstring m_name;
    std::wstring m_id;
    std::wstring m_label;
    std::wstring m_sample;
    mutable std::mutex m_mutex;
    std::wstring m_value;
    mutable std::wstring m_return_buffer;
};

class CTodayItem : public CDeepSeekItemBase
{
public:
    CTodayItem();
};

class CBalanceItem : public CDeepSeekItemBase
{
public:
    CBalanceItem();
};

struct ConfigView
{
    bool has_api_key{false};
    bool has_user_token{false};
    int balance_interval_sec{60};
    int exact_interval_sec{300};
    bool exact_enabled{false};
    std::wstring currency{L"auto"};
    DayBoundary day_boundary{DayBoundary::China};
    bool exact_auth_error{false};
    bool exact_enabled_runtime{false};
};

class CDeepSeekPlugin : public ITMPlugin
{
public:
    static CDeepSeekPlugin& Instance();
    static void ShutdownExisting();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    void OnInitialize(ITrafficMonitor* pApp) override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    ConfigView GetConfigView() const;
    bool ApplyConfig(const AppConfig& config, bool clear_api_key, bool clear_user_token,
                     std::wstring& error);
    bool TestBalance(const std::wstring& api_key_override, std::wstring& message);
    bool TestExact(const std::wstring& user_token_override, const std::wstring& currency,
                   DayBoundary boundary, std::wstring& message);

    void Shutdown();

private:
    CDeepSeekPlugin();
    ~CDeepSeekPlugin() = default;
    CDeepSeekPlugin(const CDeepSeekPlugin&) = delete;
    CDeepSeekPlugin& operator=(const CDeepSeekPlugin&) = delete;

    void InitializeOnce();
    void StartWorkerLocked();
    static DWORD WINAPI WorkerThreadProc(LPVOID parameter);
    void WorkerThread();
    bool PersistLocked(std::wstring& error);
    void RebuildDisplayLocked(std::time_t now);
    void BuildTooltipLocked(std::time_t now);
    bool ComputeExactDisplayLocked(std::time_t now, bool& stale) const;
    std::wstring SelectedCurrencyForExactLocked() const;
    void HandleBalanceResultLocked(const BalanceResult& result, std::time_t now);
    void HandleExactResultLocked(const ExactResult& result, const std::wstring& currency,
                                 std::time_t now);
    void Log(const std::wstring& message) const;

    mutable std::mutex m_mutex;
    std::mutex m_init_mutex;

    AppConfig m_config;
    LedgerState m_ledger;

    std::vector<BalanceEntry> m_balances;
    bool m_has_balance_snapshot{false};
    bool m_balance_is_available{true};
    bool m_balance_auth_error{false};
    bool m_has_balance_error{false};
    std::wstring m_balance_error;
    std::time_t m_balance_last_success{0};

    bool m_exact_has_result{false};
    int64_t m_exact_cost_micros{0};
    std::time_t m_exact_last_success{0};
    bool m_exact_auth_error{false};
    bool m_exact_suspended{false};
    bool m_has_exact_error{false};
    std::wstring m_exact_error;
    std::wstring m_exact_currency;

    std::wstring m_today_text{L"--"};
    std::wstring m_balance_text{L"--"};
    std::wstring m_tooltip{L"DeepSeek 消费插件尚未初始化"};
    mutable std::wstring m_tooltip_return;

    std::wstring m_config_dir;
    std::wstring m_config_path;
    std::wstring m_log_path;
    ITrafficMonitor* m_app{};
    bool m_initialized{false};
    bool m_worker_started{false};
    std::atomic<bool> m_force_refresh{false};
    HANDLE m_stop_event{nullptr};
    HANDLE m_thread{nullptr};
    DWORD m_thread_id{0};

    CTodayItem m_today;
    CBalanceItem m_balance;
};
