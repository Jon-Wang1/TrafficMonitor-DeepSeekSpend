#include "settings_dialog.h"

#include "plugin.h"
#include "resource.h"
#include "util.h"

#include "json.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct DialogState
{
    CDeepSeekPlugin* plugin{nullptr};
    ConfigView view;
    bool clear_api_key{false};
    bool clear_user_token{false};
};

std::wstring Trim(const std::wstring& text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == L' ' || text[begin] == L'\t' ||
            text[begin] == L'\r' || text[begin] == L'\n')) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == L' ' || text[end - 1] == L'\t' ||
            text[end - 1] == L'\r' || text[end - 1] == L'\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring GetDlgText(HWND dialog, int control_id)
{
    wchar_t buffer[4096]{};
    const int length = GetDlgItemTextW(dialog, control_id, buffer,
                                       static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, static_cast<size_t>(length)) : std::wstring();
}

void SetDlgText(HWND dialog, int control_id, const std::wstring& text)
{
    SetDlgItemTextW(dialog, control_id, text.c_str());
}

std::wstring NormalizeApiKey(const std::wstring& input)
{
    std::wstring key = Trim(input);
    const std::wstring lower = ToLowerAscii(key);
    if (lower.rfind(L"bearer ", 0) == 0) {
        key = Trim(key.substr(7));
    }
    return key;
}

std::wstring NormalizeUserToken(const std::wstring& input)
{
    std::wstring token = Trim(input);
    if (token.empty()) {
        return token;
    }

    if (token.front() == L'{') {
        JsonValue value;
        std::string error;
        if (ParseJson(WideToUtf8(token), value, error)) {
            if (const JsonValue* inner = value.Find("value");
                inner != nullptr && inner->IsString() && !inner->string_value.empty()) {
                return Utf8ToWide(inner->string_value);
            }
        }
    }

    if (token.size() >= 2 && token.front() == L'"' && token.back() == L'"') {
        token = token.substr(1, token.size() - 2);
    }
    return token;
}

int CurrencyIndex(const std::wstring& currency)
{
    if (currency == L"CNY") {
        return 1;
    }
    if (currency == L"USD") {
        return 2;
    }
    return 0;
}

std::wstring CurrencyFromIndex(int index)
{
    if (index == 1) {
        return L"CNY";
    }
    if (index == 2) {
        return L"USD";
    }
    return L"auto";
}

int BoundaryIndex(DayBoundary boundary)
{
    switch (boundary) {
    case DayBoundary::Local:
        return 1;
    case DayBoundary::Utc:
        return 2;
    case DayBoundary::China:
    default:
        return 0;
    }
}

DayBoundary BoundaryFromIndex(int index)
{
    if (index == 1) {
        return DayBoundary::Local;
    }
    if (index == 2) {
        return DayBoundary::Utc;
    }
    return DayBoundary::China;
}

void FillCombo(HWND dialog, int control_id, const std::vector<std::wstring>& items, int selection)
{
    HWND combo = GetDlgItem(dialog, control_id);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

int GetComboSelection(HWND dialog, int control_id)
{
    return static_cast<int>(SendDlgItemMessageW(dialog, control_id, CB_GETCURSEL, 0, 0));
}

void CenterOnOwner(HWND dialog)
{
    HWND owner = GetParent(dialog);
    if (owner == nullptr) {
        return;
    }
    RECT dialog_rect{};
    RECT owner_rect{};
    GetWindowRect(dialog, &dialog_rect);
    GetWindowRect(owner, &owner_rect);
    const int width = dialog_rect.right - dialog_rect.left;
    const int height = dialog_rect.bottom - dialog_rect.top;
    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void ShowTokenHelp(HWND dialog)
{
    const wchar_t* text =
        L"获取 userToken 的步骤：\r\n"
        L"1. 用浏览器登录 https://platform.deepseek.com\r\n"
        L"2. 按 F12 打开开发者工具 → Application/应用 → Local Storage\r\n"
        L"3. 选择 https://platform.deepseek.com，找到 userToken\r\n"
        L"4. 复制它的 value；如果 value 形如 {\"value\":\"...\"}，可以整段粘贴\r\n"
        L"5. 回到本窗口粘贴，并点“测试连接”\r\n\r\n"
        L"注意：\r\n"
        L"• userToken 是完整的平台登录会话，比 API Key 更敏感\r\n"
        L"• 只保存在本机，使用 Windows DPAPI 加密\r\n"
        L"• Token 会过期，失效后需要重新粘贴\r\n"
        L"• 精确模式调用的是非官方接口，可能随时变更";
    MessageBoxW(dialog, text, L"userToken 获取说明", MB_OK | MB_ICONINFORMATION);
}

void ShowTestResult(HWND dialog, bool success, const std::wstring& message)
{
    MessageBoxW(dialog, message.c_str(),
                success ? L"测试成功" : L"测试失败",
                MB_OK | (success ? MB_ICONINFORMATION : MB_ICONWARNING));
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<DialogState*>(l_param);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state == nullptr) {
            return FALSE;
        }

        SetDlgText(dialog, IDC_API_KEY_STATUS,
                   state->view.has_api_key ? L"已配置（留空不修改）" : L"未配置");
        std::wstring token_status =
            state->view.has_user_token ? L"已配置（留空不修改）" : L"未配置";
        if (state->view.exact_auth_error) {
            token_status = L"Token 已失效，请重新粘贴";
        }
        SetDlgText(dialog, IDC_TOKEN_STATUS, token_status);

        SetDlgItemInt(dialog, IDC_BALANCE_INTERVAL,
                      static_cast<UINT>(state->view.balance_interval_sec), FALSE);
        SetDlgItemInt(dialog, IDC_EXACT_INTERVAL,
                      static_cast<UINT>(state->view.exact_interval_sec), FALSE);

        FillCombo(dialog, IDC_CURRENCY,
                  {L"自动（CNY 优先）", L"CNY", L"USD"},
                  CurrencyIndex(state->view.currency));
        FillCombo(dialog, IDC_DAY_BOUNDARY,
                  {L"北京时间（UTC+8）", L"本机时区", L"UTC"},
                  BoundaryIndex(state->view.day_boundary));

        CheckDlgButton(dialog, IDC_EXACT_ENABLED,
                       state->view.exact_enabled ? BST_CHECKED : BST_UNCHECKED);

        CenterOnOwner(dialog);
        SetForegroundWindow(dialog);
        SetFocus(GetDlgItem(dialog,
                            state->view.has_api_key ? IDC_BALANCE_INTERVAL : IDC_API_KEY));
        return TRUE;
    }

    case WM_COMMAND:
        if (state == nullptr) {
            return FALSE;
        }
        switch (LOWORD(w_param)) {
        case IDOK: {
            const std::wstring api_key =
                NormalizeApiKey(GetDlgText(dialog, IDC_API_KEY));
            const std::wstring user_token =
                NormalizeUserToken(GetDlgText(dialog, IDC_USER_TOKEN));

            BOOL translated = FALSE;
            int balance_interval = static_cast<int>(
                GetDlgItemInt(dialog, IDC_BALANCE_INTERVAL, &translated, FALSE));
            if (!translated) {
                balance_interval = state->view.balance_interval_sec;
            }
            translated = FALSE;
            int exact_interval = static_cast<int>(
                GetDlgItemInt(dialog, IDC_EXACT_INTERVAL, &translated, FALSE));
            if (!translated) {
                exact_interval = state->view.exact_interval_sec;
            }

            if (balance_interval < kMinBalanceIntervalSec || balance_interval > kMaxIntervalSec) {
                const std::wstring message =
                    L"估算刷新间隔必须在 " + std::to_wstring(kMinBalanceIntervalSec) +
                    L" 到 " + std::to_wstring(kMaxIntervalSec) + L" 秒之间。";
                MessageBoxW(dialog, message.c_str(), L"输入无效", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_BALANCE_INTERVAL));
                return TRUE;
            }
            if (exact_interval < kMinExactIntervalSec || exact_interval > kMaxIntervalSec) {
                const std::wstring message =
                    L"精确刷新间隔必须在 " + std::to_wstring(kMinExactIntervalSec) +
                    L" 到 " + std::to_wstring(kMaxIntervalSec) + L" 秒之间。";
                MessageBoxW(dialog, message.c_str(), L"输入无效", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_EXACT_INTERVAL));
                return TRUE;
            }

            const bool exact_enabled =
                IsDlgButtonChecked(dialog, IDC_EXACT_ENABLED) == BST_CHECKED;
            const bool has_token = !user_token.empty() ||
                (state->view.has_user_token && !state->clear_user_token);
            if (exact_enabled && !has_token) {
                MessageBoxW(dialog,
                            L"启用精确模式需要先填写 userToken。\r\n"
                            L"如果不想启用，请取消勾选“启用精确模式”。",
                            L"缺少 userToken", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, IDC_USER_TOKEN));
                return TRUE;
            }

            if (!api_key.empty()) {
                state->clear_api_key = false;
            }
            if (!user_token.empty()) {
                state->clear_user_token = false;
            }

            AppConfig config;
            config.api_key = api_key;
            config.user_token = user_token;
            config.balance_interval_sec = balance_interval;
            config.exact_interval_sec = exact_interval;
            config.exact_enabled = exact_enabled;
            config.currency = CurrencyFromIndex(GetComboSelection(dialog, IDC_CURRENCY));
            config.day_boundary = BoundaryFromIndex(GetComboSelection(dialog, IDC_DAY_BOUNDARY));

            std::wstring error;
            if (!state->plugin->ApplyConfig(config, state->clear_api_key,
                                            state->clear_user_token, error)) {
                MessageBoxW(dialog, (L"保存失败：" + error).c_str(),
                            L"保存失败", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            EndDialog(dialog, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        case IDC_CLEAR_KEY:
            state->clear_api_key = true;
            SetDlgText(dialog, IDC_API_KEY, L"");
            SetDlgText(dialog, IDC_API_KEY_STATUS, L"保存后清除");
            SetFocus(GetDlgItem(dialog, IDC_API_KEY));
            return TRUE;

        case IDC_CLEAR_TOKEN:
            state->clear_user_token = true;
            SetDlgText(dialog, IDC_USER_TOKEN, L"");
            SetDlgText(dialog, IDC_TOKEN_STATUS, L"保存后清除");
            SetFocus(GetDlgItem(dialog, IDC_USER_TOKEN));
            return TRUE;

        case IDC_TEST_BALANCE: {
            const std::wstring api_key =
                NormalizeApiKey(GetDlgText(dialog, IDC_API_KEY));
            if (api_key.empty() && state->clear_api_key) {
                ShowTestResult(dialog, false,
                               L"API Key 将在保存后被清除，无法测试。请先输入新的 Key。");
                return TRUE;
            }
            if (api_key.empty() && !state->view.has_api_key) {
                ShowTestResult(dialog, false, L"请先输入 API Key。");
                return TRUE;
            }

            EnableWindow(dialog, FALSE);
            HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            std::wstring result;
            const bool success = state->plugin->TestBalance(api_key, result);
            SetCursor(old_cursor);
            EnableWindow(dialog, TRUE);
            ShowTestResult(dialog, success, result);
            return TRUE;
        }

        case IDC_TEST_EXACT: {
            const std::wstring user_token =
                NormalizeUserToken(GetDlgText(dialog, IDC_USER_TOKEN));
            if (user_token.empty() && state->clear_user_token) {
                ShowTestResult(dialog, false,
                               L"userToken 将在保存后被清除，无法测试。请先输入新的 Token。");
                return TRUE;
            }
            if (user_token.empty() && !state->view.has_user_token) {
                ShowTestResult(dialog, false, L"请先输入 userToken。");
                return TRUE;
            }

            const std::wstring currency =
                CurrencyFromIndex(GetComboSelection(dialog, IDC_CURRENCY));
            const DayBoundary boundary =
                BoundaryFromIndex(GetComboSelection(dialog, IDC_DAY_BOUNDARY));

            EnableWindow(dialog, FALSE);
            HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            std::wstring result;
            const bool success =
                state->plugin->TestExact(user_token, currency, boundary, result);
            SetCursor(old_cursor);
            EnableWindow(dialog, TRUE);
            ShowTestResult(dialog, success, result);
            return TRUE;
        }

        case IDC_OPEN_USAGE:
            ShellExecuteW(dialog, L"open", L"https://platform.deepseek.com/usage",
                          nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;

        case IDC_TOKEN_HELP:
            ShowTokenHelp(dialog);
            return TRUE;

        default:
            break;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    default:
        return FALSE;
    }
}

} // namespace

INT_PTR ShowSettingsDialog(CDeepSeekPlugin& plugin, HWND parent)
{
    DialogState state;
    state.plugin = &plugin;
    state.view = plugin.GetConfigView();

    HMODULE module = GetThisModuleHandle();
    if (module == nullptr) {
        MessageBoxW(parent, L"无法获取插件模块句柄。",
                    L"DeepSeek 消费插件", MB_OK | MB_ICONERROR);
        return IDCANCEL;
    }

    const INT_PTR result = DialogBoxParamW(
        module, MAKEINTRESOURCEW(IDD_SETTINGS), parent,
        SettingsDialogProc, reinterpret_cast<LPARAM>(&state));
    if (result == -1) {
        MessageBoxW(parent, L"打开设置窗口失败。",
                    L"DeepSeek 消费插件", MB_OK | MB_ICONERROR);
    }
    return result;
}
