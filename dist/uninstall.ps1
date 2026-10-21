$ErrorActionPreference = 'Stop'

$targetDll = 'C:\Program Files\TrafficMonitor\plugins\DeepSeekUsage.dll'
$exe = 'C:\Program Files\TrafficMonitor\TrafficMonitor.exe'

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

if (Test-Path -LiteralPath $targetDll) {
    Remove-Item -LiteralPath $targetDll -Force
    Write-Host "已删除：$targetDll"
} else {
    Write-Host '插件 DLL 不存在，可能已经卸载。'
}

if (Test-Path -LiteralPath $exe) {
    Start-Process -FilePath $exe
    Write-Host 'TrafficMonitor 已重新启动。'
}

$answer = Read-Host '是否同时删除配置、日志和加密保存的凭据？输入 yes 删除'
if ($answer -eq 'yes') {
    $paths = @(
        (Join-Path $env:APPDATA 'TrafficMonitor\plugins\DeepSeekUsage.ini'),
        (Join-Path $env:APPDATA 'TrafficMonitor\plugins\DeepSeekUsage.log'),
        'C:\Program Files\TrafficMonitor\plugins\DeepSeekUsage.ini',
        'C:\Program Files\TrafficMonitor\plugins\DeepSeekUsage.log'
    )
    foreach ($path in $paths) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
            Write-Host "已删除：$path"
        }
    }
    Write-Host '配置和凭据已删除。'
} else {
    Write-Host '已保留配置；重新安装后可继续使用。'
}

Write-Host ''
Read-Host '按回车退出'
