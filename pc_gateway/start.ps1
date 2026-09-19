# 优先使用本机ESP-IDF自带Python，找不到时使用系统Python。
$gatewayPython = 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $gatewayPython)) {
    $gatewayPython = 'python'
}
& $gatewayPython (Join-Path $PSScriptRoot 'h7_wifi_tool.py')
