# TrafficMonitor DeepSeek Spend Plugin
<img width="975" height="75" alt="image" src="https://github.com/user-attachments/assets/fde3a9e8-a379-4435-90d7-39b2073f1214" />


**TrafficMonitor-DeepSeekSpend** — 在 TrafficMonitor 的任务栏上显示 **今日 DeepSeek API 消费金额**，可选显示账户余额。

- **估算模式（默认）**：只需 DeepSeek API Key，读取官方余额接口，按当日余额下降量估算消费，显示 `≈¥0.4`。
- **精确模式（可选）**：额外提供 platform.deepseek.com 的 `userToken`，调用非官方用量接口，显示 `¥0.4`。
- 支持 CNY/USD 多币种、北京时间/本机时区/UTC 日界、DPAPI 加密存储凭据、Win32 设置窗口。

## 环境要求

- Windows 10/11 x64
- TrafficMonitor 1.82 或更高（已在 1.8.6 x64 上测试）
- 插件是 x64 DLL，必须与 TrafficMonitor 的位数一致

## 下载

- 从 GitHub Releases 下载 `DeepSeekUsage.dll`。
- 也可以从源码构建（见下文）。

## 安装

1. 关闭 TrafficMonitor。
2. 把 `DeepSeekUsage.dll` 复制到 `C:\Program Files\TrafficMonitor\plugins\`（需要管理员权限）。
3. 启动 TrafficMonitor，右键任务栏窗口或托盘图标 → **显示设置** → 勾选 **DeepSeek今日消费**（可选再勾选 **DeepSeek余额**）→ 确定。
4. 打开 TrafficMonitor **选项 → 常规设置 → 插件管理 → DeepSeek消费 → 设置**：
   - 填入 API Key，点 **测试 API Key**，确认能读到余额，然后保存。
   - 可选：勾选 **启用精确模式**，粘贴 platform.deepseek.com 的 `userToken`，点 **测试连接**。

## 任务栏显示

| 显示 | 含义 |
|---|---|
| `≈¥0.4` | 估算值（官方余额差值） |
| `¥0.4` | 精确值（平台接口，数据较新） |
| `≈¥0.4`（精确模式临时失败时） | 最近一次精确值，可能已过期 |
| `--` | 未配置 / 认证失败 / 暂无数据 |

任务栏和悬停提示的金额保留 **1 位小数**；插件设置里的“测试”结果保留 **2 位小数**用于核对。为控制任务栏宽度，按最大 `999.9` 预留显示宽度，超过该值时可能显示不全。

悬停提示包含：来源（精确/估算）、日界、统计起始时间、余额明细（总额/赠送/充值）、更新时间、错误或 Token 状态。

币种自动选择：有 CNY 时显示 CNY，否则 USD；两种都有时任务栏显示 CNY，悬停提示同时列出另一种。绝不相加不同币种。

## 数据来源与精度

### 估算模式（默认，只需 API Key）

- 官方接口：`GET https://api.deepseek.com/user/balance`，默认 60 秒轮询。
- 当日消费 = 当日观察到的余额下降量之和；余额上升（充值）忽略。
- 已知误差：
  - 当天充值会使余额差值估算明显偏低（充值把消费“抵消”了）；此时请以精确模式或平台用量页为准。
  - 同一采样间隔内“先消费后充值”可能漏计。
  - 退款、赠送额度过期会被计成消费。
  - 平台结算约有 1–5 分钟滞后。
  - 首次运行或跨天启动时，统计起始前的消费可能未计入；tooltip 会标注“统计自 HH:MM”。
- **估算值仅供参考，不能用于对账或预算强制。**

### 精确模式（可选，需要 userToken）

- 非官方接口：`GET https://platform.deepseek.com/api/v0/usage/cost?month=<M>&year=<Y>`，按当天日期汇总当日成本，默认 5 分钟。
- 该接口返回全账号（所有 API Key）的当日成本；按平台月度数据中的日期取值，默认日界为北京时间。
- 临时失败保留上次值并标记 `≈`，连续失败超过 15 分钟回退估算。
- 风险：接口非官方、可能随时变更；`userToken` 是完整平台登录会话，比 API Key 更敏感；Token 会过期，需要重新粘贴。
- 只发往 `platform.deepseek.com`，不上传任何第三方。

## 日界

- 默认北京时间（UTC+8）；设置中可切换本机时区/UTC。
- 切换日界会重置当日估算，下一次余额采样重新建立基线。

## 安全

- API Key 与 userToken 使用 Windows DPAPI（当前用户）加密，保存在 `%APPDATA%\TrafficMonitor\plugins\DeepSeekUsage.ini`（便携模式下在 `<TrafficMonitor>\plugins\`）。
- 凭据只发往对应域名；日志和 tooltip 不回显凭据。
- 请使用同一 DeepSeek 账号的 API Key 与 userToken。

## 常见问题

- **任务栏没有显示**：确认已在“显示设置”中勾选插件项；DLL 必须直接放在 `plugins` 目录下，不能放子目录。
- **显示 `--`**：未填 API Key、Key 失效、网络错误或尚无余额数据；在插件设置里用“测试 API Key”定位。
- **估算值和平台对不上**：估算受充值、退款、赠送过期和结算延迟影响；可启用精确模式对比。
- **精确模式提示 Token 失效**：重新从浏览器 Local Storage 复制 `userToken` 并保存。
- **日志**：`%APPDATA%\TrafficMonitor\plugins\DeepSeekUsage.log`。

## 从源码构建

需要 MinGW-w64 x86_64（UCRT）工具链，已用 GCC 16.2.0 验证。

```bash
# 将 MinGW 的 bin 目录加入 PATH 后
mingw32-make                     # 生成 build\DeepSeekUsage.dll
mingw32-make test-dll            # 生成带测试钩子的 build\DeepSeekUsage_test.dll
```

构建后把 `build\DeepSeekUsage.dll` 复制到 `C:\Program Files\TrafficMonitor\plugins\`，重启 TrafficMonitor。

测试：

```bash
python -I tests/mock_server.py 8765    # 另开终端
build\unit_tests.exe                    # JSON/金额/日界/账本/API 单元测试
build\plugin_test.exe                   # 加载测试 DLL，验证估算/精确显示与设置资源
```

设置窗口截图：`tests\screenshot_settings.ps1`（需 mock 服务器运行）。

## 目录结构

```
include/PluginInterface.h   TrafficMonitor 插件 SDK 头文件
src/                        插件源码
tests/                      单元测试、mock 服务器、插件测试、截图脚本
LICENSES/                   第三方许可证全文
LICENSE                     本项目原始代码的 MIT 许可证
THIRD_PARTY_NOTICES.md      第三方代码归属
build/                      构建产物
```

## 卸载

关闭 TrafficMonitor，删除 `C:\Program Files\TrafficMonitor\plugins\DeepSeekUsage.dll`。如需同时删除配置和加密凭据，删除 `%APPDATA%\TrafficMonitor\plugins\DeepSeekUsage.ini` 和 `%APPDATA%\TrafficMonitor\plugins\DeepSeekUsage.log`。

## 许可与免责

- 本项目的原始代码以 **MIT License** 发布，见 `LICENSE`。
- `include/PluginInterface.h` 来自 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)，Copyright (c) 2019 zhongyang219，按 **"Anti 996" License Version 1.0 (Draft)** 使用；完整文本见 `LICENSES/Anti-996-License.txt`，归属说明见 `THIRD_PARTY_NOTICES.md`。
- 精确模式调用非官方接口，存在接口变更和 ToS 风险；仅供个人评估使用。
- 估算值不构成账单依据。
