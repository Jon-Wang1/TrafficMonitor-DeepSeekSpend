#include "plugin.h"

#include "ledger.h"
#include "settings_dialog.h"
#include "util.h"

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <utility>

namespace {

CDeepSeekPlugin* g_instance = nullptr;

int BackoffDelay(int base_seconds, int failures)
{
    if (failures <= 1) {
        return base_seconds;
    }
    const int shift = std::min(failures - 1, 5);
    const long long delay = static_cast<long long>(base_seconds) << shift;
    return static_cast<int>(std::min<long long>(delay, 1800));
}

std::wstring WithTrailingSlash(const std::wstring& directory)
{
    if (directory.empty()) {
        return directory;
    }
    const wchar_t last = directory.back();
    if (last == L'\\' || last == L'/') {
        return directory;
    }
    return directory + L'\\';
}

std::wstring ApiBaseUrl()
{
#ifdef DEEPSEEK_USAGE_TEST_BASE_URLS
    const std::wstring override_url = GetEnvString(L"DEEPSEEK_USAGE_API_BASE_URL");
    if (!override_url.empty()) {
        return override_url;
    }
#endif
    return L"https://api.deepseek.com";
}

std::wstring PlatformBaseUrl()
{
#ifdef DEEPSEEK_USAGE_TEST_BASE_URLS
    const std::wstring override_url = GetEnvString(L"DEEPSEEK_USAGE_PLATFORM_BASE_URL");
    if (!override_url.empty()) {
        return override_url;
    }
#endif
    return L"https://platform.deepseek.com";
}

} // namespace

CDeepSeekItemBase::CDeepSeekItemBase(std::wstring name, std::wstring id,
                                     std::wstring label, std::wstring sample)
    : m_name(std::move(name)),
      m_id(std::move(id)),
      m_label(std::move(label)),
      m_sample(std::move(sample)),
      m_value(L"--")
{
}

const wchar_t* CDeepSeekItemBase::GetItemName() const { return m_name.c_str(); }
const wchar_t* CDeepSeekItemBase::GetItemId() const { return m_id.c_str(); }
const wchar_t* CDeepSeekItemBase::GetItemLableText() const { return m_label.c_str(); }
const wchar_t* CDeepSeekItemBase::GetItemValueSampleText() const { return m_sample.c_str(); }

const wchar_t* CDeepSeekItemBase::GetItemValueText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_return_buffer = m_value;
    return m_return_buffer.c_str();
}

void CDeepSeekItemBase::SetValue(std::wstring value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value = std::move(value);
}

CTodayItem::CTodayItem()
    : CDeepSeekItemBase(L"DeepSeek今日消费", L"DeepSeekToday", L"", L"≈¥8888.88")
{
}

CBalanceItem::CBalanceItem()
    : CDeepSeekItemBase(L"DeepSeek余额", L"DeepSeekBalance", L"余额: ", L"余额: ¥8888.88")
{
}

CDeepSeekPlugin& CDeepSeekPlugin::Instance()
{
    static CDeepSeekPlugin instance;
    g_instance = &instance;
    return instance;
}

void CDeepSeekPlugin::ShutdownExisting()
{
    if (g_instance != nullptr) {
        g_instance->Shutdown();
    }
}

CDeepSeekPlugin::CDeepSeekPlugin() = default;

IPluginItem* CDeepSeekPlugin::GetItem(int index)
{
    switch (index) {
    case 0:
        return &m_today;
    case 1:
        return &m_balance;
    default:
        return nullptr;
    }
}

void CDeepSeekPlugin::DataRequired()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return;
    }
    RebuildDisplayLocked(std::time(nullptr));
}

const wchar_t* CDeepSeekPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index) {
    case TMI_NAME:
        return L"DeepSeek消费";
    case TMI_DESCRIPTION:
        return L"在任务栏显示 DeepSeek API 今日消费与余额";
    case TMI_AUTHOR:
        return L"EchoVM1";
    case TMI_COPYRIGHT:
        return L"© 2026";
    case TMI_VERSION:
        return L"1.0.0";
    case TMI_URL:
        return L"";
    default:
        return L"";
    }
}

const wchar_t* CDeepSeekPlugin::GetTooltipInfo()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tooltip_return = m_tooltip;
    return m_tooltip_return.c_str();
}

ITMPlugin::OptionReturn CDeepSeekPlugin::ShowOptionsDialog(void* hParent)
{
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        initialized = m_initialized;
    }
    if (!initialized) {
        MessageBoxW(static_cast<HWND>(hParent), L"插件尚未初始化，请稍后重试。",
                    L"DeepSeek 消费插件", MB_OK | MB_ICONWARNING);
        return OR_OPTION_UNCHANGED;
    }

    const INT_PTR result = ShowSettingsDialog(*this, static_cast<HWND>(hParent));
    return result == IDOK ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
}

void CDeepSeekPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_app = pApp;
    }

    std::wstring message = L"OnInitialize";
    if (pApp != nullptr) {
        message += L": host API=" + std::to_wstring(pApp->GetAPIVersion());
        if (pApp->GetAPIVersion() >= 1) {
            const wchar_t* version = pApp->GetVersion();
            if (version != nullptr) {
                message += L", host version=" + std::wstring(version);
            }
        }
    } else {
        message += L": null host interface";
    }
    Log(message);
    InitializeOnce();
}

void CDeepSeekPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data != nullptr) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_config_dir = data;
        }
        Log(L"EI_CONFIG_DIR: " + std::wstring(data));
        InitializeOnce();
    }
}

void CDeepSeekPlugin::InitializeOnce()
{
    std::lock_guard<std::mutex> init_lock(m_init_mutex);

    std::wstring directory;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) {
            return;
        }
        directory = m_config_dir;
    }
    if (directory.empty()) {
        return;
    }

    m_config_path = ConfigFilePath(directory);
    m_log_path = WithTrailingSlash(directory) + L"DeepSeekUsage.log";

    PersistedData data;
    std::wstring warning;
    const bool loaded = LoadConfigFile(m_config_path, data, warning);
    if (!warning.empty()) {
        Log(warning);
    }
    if (!loaded) {
        Log(L"读取配置失败，使用默认配置");
        data = PersistedData{};
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_initialized = true;
        m_config = data.config;
        m_ledger = data.ledger;

        HMODULE module = nullptr;
        static int anchor = 0;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&anchor), &module);

        StartWorkerLocked();
        m_force_refresh = true;
        RebuildDisplayLocked(std::time(nullptr));
    }

    Log(L"plugin initialized, config=" + m_config_path);
}

void CDeepSeekPlugin::StartWorkerLocked()
{
    if (m_worker_started) {
        return;
    }
    m_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stop_event == nullptr) {
        Log(L"创建停止事件失败");
        return;
    }
    m_thread = CreateThread(nullptr, 0, WorkerThreadProc, this, 0, &m_thread_id);
    if (m_thread == nullptr) {
        Log(L"创建后台线程失败");
        CloseHandle(m_stop_event);
        m_stop_event = nullptr;
        return;
    }
    m_worker_started = true;
}

void CDeepSeekPlugin::Shutdown()
{
    if (m_stop_event != nullptr) {
        SetEvent(m_stop_event);
    }

    if (m_thread != nullptr) {
        const DWORD wait_result = WaitForSingleObject(m_thread, 3000);
        if (wait_result == WAIT_OBJECT_0) {
            CloseHandle(m_thread);
            m_thread = nullptr;
            if (m_stop_event != nullptr) {
                CloseHandle(m_stop_event);
                m_stop_event = nullptr;
            }
        } else {
            // The DLL is pinned, so the worker may finish safely after the host
            // releases the module. Leaking the handles here is intentional.
            Log(L"后台线程未在超时内退出");
        }
    } else if (m_stop_event != nullptr) {
        CloseHandle(m_stop_event);
        m_stop_event = nullptr;
    }
}

DWORD WINAPI CDeepSeekPlugin::WorkerThreadProc(LPVOID parameter)
{
    static_cast<CDeepSeekPlugin*>(parameter)->WorkerThread();
    return 0;
}

void CDeepSeekPlugin::WorkerThread()
{
    Log(L"worker started");

    std::time_t next_balance = 0;
    std::time_t next_exact = 0;
    int balance_failures = 0;
    int exact_failures = 0;

    while (WaitForSingleObject(m_stop_event, 1000) != WAIT_OBJECT_0) {
        AppConfig config;
        bool exact_suspended = false;
        bool force_refresh = m_force_refresh.exchange(false);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            config = m_config;
            exact_suspended = m_exact_suspended;
            if (force_refresh) {
                next_balance = 0;
                next_exact = 0;
                balance_failures = 0;
                exact_failures = 0;
            }
            RebuildDisplayLocked(std::time(nullptr));
        }

        const std::time_t now = std::time(nullptr);

        if (config.api_key.empty()) {
            next_balance = 0;
            next_exact = 0;
            continue;
        }

        if (next_balance == 0) {
            next_balance = now;
        }
        if (now >= next_balance) {
            const BalanceResult result = FetchBalance(config.api_key, ApiBaseUrl());
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                HandleBalanceResultLocked(result, now);
                RebuildDisplayLocked(now);
                std::wstring error;
                if (!PersistLocked(error)) {
                    Log(L"保存状态失败：" + error);
                }
            }
            if (result.ok) {
                balance_failures = 0;
                next_balance = now + config.balance_interval_sec;
            } else {
                ++balance_failures;
                next_balance = now + BackoffDelay(config.balance_interval_sec, balance_failures);
            }
        }

        const bool exact_wanted =
            config.exact_enabled && !config.user_token.empty() && !exact_suspended;
        if (!exact_wanted) {
            next_exact = 0;
            exact_failures = 0;
            continue;
        }

        if (next_exact == 0) {
            next_exact = now;
        }
        if (now >= next_exact) {
            std::wstring currency;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                currency = SelectedCurrencyForExactLocked();
            }

            const std::time_t start = DayStartEpoch(now, config.day_boundary);
            const int offset = GetDayOffsetSeconds(config.day_boundary);

            std::string raw_body;
            const bool debug_raw =
                GetEnvironmentVariableW(L"DEEPSEEK_USAGE_DEBUG_RAW", nullptr, 0) > 0;
            std::string* raw_pointer = debug_raw ? &raw_body : nullptr;

            const ExactResult result = FetchExactCost(
                config.user_token, currency, start, now, offset,
                PlatformBaseUrl(), raw_pointer);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                HandleExactResultLocked(result, currency, now);
                RebuildDisplayLocked(now);
            }

            if (debug_raw && !raw_body.empty()) {
                const std::wstring debug_path =
                    WithTrailingSlash(m_config_dir) + L"exact_debug.json";
                HANDLE file = CreateFileW(debug_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(file, raw_body.data(), static_cast<DWORD>(raw_body.size()),
                              &written, nullptr);
                    CloseHandle(file);
                }
            }

            if (result.ok) {
                exact_failures = 0;
                next_exact = now + config.exact_interval_sec;
            } else {
                ++exact_failures;
                next_exact = now + BackoffDelay(config.exact_interval_sec, exact_failures);
            }
        }
    }

    Log(L"worker stopped");
}

void CDeepSeekPlugin::HandleBalanceResultLocked(const BalanceResult& result, std::time_t now)
{
    if (result.ok) {
        m_balances = result.entries;
        m_has_balance_snapshot = true;
        m_balance_is_available = result.is_available;
        m_balance_last_success = now;
        m_balance_auth_error = false;
        m_has_balance_error = false;
        m_balance_error.clear();

        bool changed = false;
        ApplyBalanceSamples(m_ledger, result.entries, m_config.day_boundary, now, changed);

        std::wstring selected_currency;
        const BalanceEntry* selected_entry = nullptr;
        std::wstring other_currency;
        const BalanceEntry* other_entry = nullptr;
        const bool selected = SelectCurrency(m_balances, m_config.currency, selected_currency,
                                             selected_entry, other_currency, other_entry);
        Log(L"balance ok: entries=" + std::to_wstring(result.entries.size()) +
            L", selected=" + (selected ? selected_currency : std::wstring(L"none")) +
            L", changed=" + (changed ? std::wstring(L"true") : std::wstring(L"false")));
    } else {
        m_has_balance_error = true;
        m_balance_error = result.error;
        m_balance_auth_error = result.auth_error;
        Log(L"balance error: " + result.error);
    }
}

void CDeepSeekPlugin::HandleExactResultLocked(const ExactResult& result,
                                              const std::wstring& currency,
                                              std::time_t now)
{
    if (result.ok) {
        m_exact_has_result = true;
        m_exact_cost_micros = result.cost_micros;
        m_exact_last_success = now;
        m_exact_currency = currency;
        m_exact_auth_error = false;
        m_exact_suspended = false;
        m_has_exact_error = false;
        m_exact_error.clear();

        Log(L"exact ok: buckets=" + std::to_wstring(result.bucket_count) +
            L", cost_micros=" + std::to_wstring(result.cost_micros) +
            L", negative=" + std::to_wstring(result.negative_sum_micros) +
            L", positive=" + std::to_wstring(result.positive_sum_micros) +
            L", currency=" + currency);
    } else if (result.auth_error) {
        m_exact_auth_error = true;
        m_exact_suspended = true;
        m_has_exact_error = true;
        m_exact_error = result.error;
        Log(L"exact auth error: " + result.error);
    } else {
        m_exact_auth_error = false;
        m_has_exact_error = true;
        m_exact_error = result.error;
        Log(L"exact error: " + result.error);
    }
}

bool CDeepSeekPlugin::PersistLocked(std::wstring& error)
{
    if (m_config_path.empty()) {
        error = L"配置路径为空";
        return false;
    }
    PersistedData data;
    data.config = m_config;
    data.ledger = m_ledger;
    return SaveConfigFile(m_config_path, data, error);
}

bool CDeepSeekPlugin::ComputeExactDisplayLocked(std::time_t now, bool& stale) const
{
    stale = false;
    if (!m_config.exact_enabled || m_config.user_token.empty() || m_exact_auth_error ||
        m_exact_suspended || !m_exact_has_result || m_exact_last_success <= 0) {
        return false;
    }

    const std::time_t age = now - m_exact_last_success;
    const std::time_t fresh_limit =
        std::max<std::time_t>(600, static_cast<std::time_t>(m_config.exact_interval_sec) * 2);

    if (age <= fresh_limit && !m_has_exact_error) {
        return true;
    }
    if (age <= 900) {
        stale = true;
        return true;
    }
    return false;
}

std::wstring CDeepSeekPlugin::SelectedCurrencyForExactLocked() const
{
    std::wstring selected_currency;
    const BalanceEntry* selected_entry = nullptr;
    std::wstring other_currency;
    const BalanceEntry* other_entry = nullptr;
    if (SelectCurrency(m_balances, m_config.currency, selected_currency,
                       selected_entry, other_currency, other_entry) &&
        !selected_currency.empty()) {
        return selected_currency;
    }
    const std::wstring preference = NormalizeCurrency(m_config.currency);
    if (preference == L"CNY" || preference == L"USD") {
        return preference;
    }
    return L"CNY";
}

void CDeepSeekPlugin::RebuildDisplayLocked(std::time_t now)
{
    std::wstring selected_currency;
    const BalanceEntry* selected_entry = nullptr;
    std::wstring other_currency;
    const BalanceEntry* other_entry = nullptr;
    const bool has_selected = SelectCurrency(m_balances, m_config.currency, selected_currency,
                                             selected_entry, other_currency, other_entry);

    std::wstring display_currency = selected_currency;
    if (display_currency.empty()) {
        display_currency = NormalizeCurrency(m_config.currency) == L"USD" ? L"USD" : L"CNY";
    }

    const bool has_key = !m_config.api_key.empty();
    bool exact_stale = false;
    const bool use_exact = has_key && ComputeExactDisplayLocked(now, exact_stale);

    if (!has_key || m_balance_auth_error) {
        m_today_text = L"--";
    } else if (use_exact) {
        const std::wstring exact_currency =
            m_exact_currency.empty() ? display_currency : m_exact_currency;
        m_today_text = (exact_stale ? L"≈" : L"") +
                       FormatAmountMicros(m_exact_cost_micros, exact_currency);
    } else {
        const auto state_it = m_ledger.currencies.find(display_currency);
        const std::wstring today = DayKey(now, m_config.day_boundary);
        if (state_it != m_ledger.currencies.end() && state_it->second.date == today) {
            m_today_text = L"≈" + FormatAmountCents(state_it->second.spend_cents, display_currency);
        } else {
            m_today_text = L"--";
        }
    }

    if (!has_key || m_balance_auth_error || !m_has_balance_snapshot ||
        !has_selected || selected_entry == nullptr) {
        m_balance_text = L"--";
    } else {
        m_balance_text = FormatAmountCents(selected_entry->total_cents, display_currency);
    }

    m_today.SetValue(m_today_text);
    m_balance.SetValue(m_balance_text);
    BuildTooltipLocked(now);
}

void CDeepSeekPlugin::BuildTooltipLocked(std::time_t now)
{
    std::wstring tip;
    auto add_line = [&tip](const std::wstring& line) {
        if (!tip.empty()) {
            tip += L"\r\n";
        }
        tip += line;
    };

    if (m_config.api_key.empty()) {
        add_line(L"DeepSeek 消费插件");
        add_line(L"API Key 未配置");
        add_line(L"请在 TrafficMonitor 的“插件管理”中打开本插件设置。");
        m_tooltip = tip;
        return;
    }

    std::wstring selected_currency;
    const BalanceEntry* selected_entry = nullptr;
    std::wstring other_currency;
    const BalanceEntry* other_entry = nullptr;
    const bool has_selected = SelectCurrency(m_balances, m_config.currency, selected_currency,
                                             selected_entry, other_currency, other_entry);

    std::wstring display_currency = selected_currency;
    if (display_currency.empty()) {
        display_currency = NormalizeCurrency(m_config.currency) == L"USD" ? L"USD" : L"CNY";
    }

    bool exact_stale = false;
    const bool use_exact = ComputeExactDisplayLocked(now, exact_stale);

    add_line(std::wstring(L"DeepSeek 今日消费（") +
             (use_exact ? (exact_stale ? L"精确，可能过期" : L"精确") : L"估算") + L"）");
    add_line(L"今日：" + m_today_text + L" " + display_currency);
    add_line(L"日界：" + DayBoundaryDisplayName(m_config.day_boundary));

    const auto state_it = m_ledger.currencies.find(display_currency);
    const bool state_is_today =
        state_it != m_ledger.currencies.end() &&
        state_it->second.date == DayKey(now, m_config.day_boundary);

    if (!use_exact && state_is_today) {
        if (state_it->second.incomplete) {
            add_line(L"统计自 " + FormatTimeHm(state_it->second.coverage_start_time) +
                     L"（此前消费可能未计入）");
        }
        if (state_it->second.had_gap) {
            add_line(L"采样有中断，估算可能不完整");
        }
        if (state_it->second.last_sample_time > 0) {
            add_line(L"估算更新于 " + FormatDateTime(state_it->second.last_sample_time));
        }
        add_line(L"估算说明：充值会被忽略；退款、赠送额度过期可能被计成消费。");
    }

    if (m_balance_auth_error) {
        add_line(L"余额：--（API Key 无效或已过期）");
    } else if (!has_selected || selected_entry == nullptr) {
        add_line(L"余额：--" + (m_has_balance_error ? L"（" + m_balance_error + L"）" : L""));
    } else {
        add_line(L"余额：" + FormatAmountCents(selected_entry->total_cents, display_currency) +
                 L" " + display_currency);
        add_line(L"赠送 " + FormatAmountCents(selected_entry->granted_cents, display_currency) +
                 L" / 充值 " + FormatAmountCents(selected_entry->topped_up_cents, display_currency));
        if (other_entry != nullptr) {
            add_line(other_currency + L" 余额：" +
                     FormatAmountCents(other_entry->total_cents, other_currency));
        }
        if (m_has_balance_error) {
            add_line(L"余额接口错误：" + m_balance_error + L"（显示上次数据）");
        }
        if (!m_balance_is_available) {
            add_line(L"注意：余额不足，API 调用可能失败");
        }
    }

    if (m_balance_last_success > 0) {
        add_line(L"余额更新于 " + FormatDateTime(m_balance_last_success));
    }

    if (!m_config.exact_enabled) {
        add_line(L"精确模式：未启用");
    } else if (m_config.user_token.empty()) {
        add_line(L"精确模式：未配置 userToken");
    } else if (m_exact_auth_error || m_exact_suspended) {
        add_line(L"精确模式：userToken 已失效，请在设置中重新粘贴");
    } else if (m_has_exact_error) {
        add_line(L"精确模式：查询失败（" + m_exact_error + L"）" +
                 (m_exact_last_success > 0 ? L"，显示上次数据" : L""));
    } else if (m_exact_last_success > 0) {
        add_line(L"精确模式：已启用，精确数据更新于 " + FormatDateTime(m_exact_last_success));
    } else {
        add_line(L"精确模式：等待首次查询");
    }

    m_tooltip = tip;
}

ConfigView CDeepSeekPlugin::GetConfigView() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ConfigView view;
    view.has_api_key = !m_config.api_key.empty();
    view.has_user_token = !m_config.user_token.empty();
    view.balance_interval_sec = m_config.balance_interval_sec;
    view.exact_interval_sec = m_config.exact_interval_sec;
    view.exact_enabled = m_config.exact_enabled;
    view.currency = m_config.currency;
    view.day_boundary = m_config.day_boundary;
    view.exact_auth_error = m_exact_auth_error || m_exact_suspended;
    view.exact_enabled_runtime = m_config.exact_enabled && !m_exact_suspended;
    return view;
}

bool CDeepSeekPlugin::ApplyConfig(const AppConfig& config, bool clear_api_key,
                                  bool clear_user_token, std::wstring& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const bool token_changed = clear_user_token ||
        (!config.user_token.empty() && config.user_token != m_config.user_token);
    const bool boundary_changed = config.day_boundary != m_config.day_boundary;

    if (clear_api_key) {
        m_config.api_key.clear();
    } else if (!config.api_key.empty()) {
        m_config.api_key = config.api_key;
    }

    if (clear_user_token) {
        m_config.user_token.clear();
    } else if (!config.user_token.empty()) {
        m_config.user_token = config.user_token;
    }

    m_config.balance_interval_sec = std::max(kMinBalanceIntervalSec,
        std::min(kMaxIntervalSec, config.balance_interval_sec));
    m_config.exact_interval_sec = std::max(kMinExactIntervalSec,
        std::min(kMaxIntervalSec, config.exact_interval_sec));
    m_config.exact_enabled = config.exact_enabled;

    const std::wstring currency = NormalizeCurrency(config.currency);
    m_config.currency =
        (currency == L"CNY" || currency == L"USD") ? currency : std::wstring(L"auto");
    m_config.day_boundary = config.day_boundary;

    if (boundary_changed) {
        m_ledger = LedgerState{};
    }
    if (token_changed) {
        m_exact_auth_error = false;
        m_exact_suspended = false;
        m_has_exact_error = false;
        m_exact_error.clear();
    }
    if (!m_config.exact_enabled) {
        m_exact_auth_error = false;
        m_exact_suspended = false;
    }

    m_force_refresh = true;
    RebuildDisplayLocked(std::time(nullptr));

    std::wstring save_error;
    const bool saved = PersistLocked(save_error);
    if (!saved) {
        error = save_error;
        Log(L"保存配置失败：" + save_error);
    } else {
        Log(L"settings changed");
    }
    return saved;
}

bool CDeepSeekPlugin::TestBalance(const std::wstring& api_key_override,
                                  std::wstring& message)
{
    std::wstring key = api_key_override;
    if (key.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        key = m_config.api_key;
    }
    if (key.empty()) {
        message = L"请先输入 API Key。";
        return false;
    }

    const BalanceResult result = FetchBalance(key, ApiBaseUrl());
    if (!result.ok) {
        message = L"连接失败：" + result.error;
        return false;
    }
    if (result.entries.empty()) {
        message = L"连接成功，但接口没有返回余额信息。";
        return true;
    }

    const BalanceEntry* chosen = &result.entries.front();
    for (const BalanceEntry& entry : result.entries) {
        if (NormalizeCurrency(entry.currency) == L"CNY") {
            chosen = &entry;
            break;
        }
    }

    message = L"连接成功。\r\n余额：" +
              FormatAmountCents(chosen->total_cents, chosen->currency) +
              L"\r\n赠送：" + FormatAmountCents(chosen->granted_cents, chosen->currency) +
              L"\r\n充值：" + FormatAmountCents(chosen->topped_up_cents, chosen->currency);
    return true;
}

bool CDeepSeekPlugin::TestExact(const std::wstring& user_token_override,
                                const std::wstring& currency,
                                DayBoundary boundary,
                                std::wstring& message)
{
    std::wstring token = user_token_override;
    if (token.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_config.user_token;
    }
    if (token.empty()) {
        message = L"请先输入 userToken。";
        return false;
    }

    std::wstring selected_currency = NormalizeCurrency(currency);
    if (selected_currency != L"CNY" && selected_currency != L"USD") {
        std::lock_guard<std::mutex> lock(m_mutex);
        selected_currency = SelectedCurrencyForExactLocked();
    }

    const std::time_t now = std::time(nullptr);
    const std::time_t start = DayStartEpoch(now, boundary);
    const int offset = GetDayOffsetSeconds(boundary);

    const ExactResult result = FetchExactCost(token, selected_currency, start, now, offset,
                                              PlatformBaseUrl());
    if (!result.ok) {
        message = L"查询失败：" + result.error;
        return false;
    }

    message = L"查询成功。\r\n今日消费：" +
              FormatAmountMicros(result.cost_micros, selected_currency) + L" " +
              selected_currency + L"\r\n数据桶数：" + std::to_wstring(result.bucket_count);
    if (result.negative_sum_micros != 0) {
        message += L"\r\n负值合计：" + FormatMicros(result.negative_sum_micros) +
                   L"（用于核对平台符号约定）";
    }
    if (!result.found_currency) {
        message += L"\r\n注意：响应中没有找到该币种的数据。";
    }
    return true;
}

void CDeepSeekPlugin::Log(const std::wstring& message) const
{
    if (!m_log_path.empty()) {
        LogToFile(m_log_path, message);
    } else {
        DebugLog(message);
    }
}
