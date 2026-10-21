$ErrorActionPreference = 'Stop'

$dll = Join-Path $PSScriptRoot 'DeepSeekUsage.dll'
$targetDir = 'C:\Program Files\TrafficMonitor\plugins'
$targetDll = Join-Path $targetDir 'DeepSeekUsage.dll'
$exe = 'C:\Program Files\TrafficMonitor\TrafficMonitor.exe'

if (-not (Test-Path -LiteralPath $dll)) {
    throw "找不到 $dll"
}
if (-not (Test-Path -LiteralPath $targetDir)) {
    throw "找不到 TrafficMonitor 插件目录：$targetDir"
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 TrafficMonitor：$exe"
}

Write-Host '正在关闭 TrafficMonitor...'
foreach ($process in @(Get-Process -Name TrafficMonitor -ErrorAction SilentlyContinue)) {
    & taskkill.exe /PID $process.Id 2>$null | Out-Null
}
for ($i = 0; $i -lt 60; $i++) {
    if (-not (Get-Process -Name TrafficMonitor -ErrorAction SilentlyContinue)) {
        break
    }
    Start-Sleep -Milliseconds 250
}
foreach ($process in @(Get-Process -Name TrafficMonitor -ErrorAction SilentlyContinue)) {
    & taskkill.exe /F /PID $process.Id 2>$null | Out-Null
}
Start-Sleep -Milliseconds 500

Copy-Item -LiteralPath $dll -Destination $targetDll -Force
Write-Host "已安装：$targetDll"

Start-Process -FilePath $exe
Write-Host 'TrafficMonitor 已重新启动。'
Write-Host ''
Write-Host '接下来请手动完成两步：'
Write-Host '1. 右键任务栏窗口或托盘图标 -> 显示设置 -> 勾选 “DeepSeek今日消费”'
Write-Host '   （可选再勾选 “DeepSeek余额”）-> 确定。'
Write-Host '2. TrafficMonitor 选项 -> 常规设置 -> 插件管理 -> DeepSeek消费 -> 设置'
Write-Host '   -> 填入 API Key -> 测试 API Key -> 确定。'
Write-Host ''
Write-Host 'API Key 只保存在本机（DPAPI 加密），只会发往 api.deepseek.com。'
Write-Host '精确模式需要额外粘贴 platform.deepseek.com 的 userToken；'
Write-Host '不启用精确模式也可以正常使用估算模式。'
Write-Host ''
Read-Host '按回车退出'
