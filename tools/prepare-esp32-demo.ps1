param(
    [Parameter(Mandatory = $true)]
    [string] $DemoArduinoRoot
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$demoRoot = (Resolve-Path -LiteralPath $DemoArduinoRoot).Path
$widgets = Join-Path $demoRoot '3_3-4_TFT-LVGL-Widgets'
$sketch = Join-Path $widgets 'LvglWidgets'
$sketchFile = Join-Path $sketch 'LvglWidgets.ino'
$libraries = Join-Path $demoRoot 'Libraries'
$lvConfig = Join-Path $widgets 'LVGL configuration replacement file/lv_conf.h'

foreach ($file in @($sketchFile, $lvConfig, (Join-Path $libraries 'Lvgl/src/lvgl.h'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Expected Guition demo file missing: $file"
    }
}

$source = [IO.File]::ReadAllText($sketchFile)
if (!$source.Contains('void led_control_screen_begin();')) {
    if (!$source.Contains('#include <demos/lv_demos.h>') -or
        !$source.Contains('    lv_demo_widgets();')) {
        throw 'Expected LVGL Widgets entry points missing from the vendor sketch.'
    }
    $source = $source.Replace('#include <demos/lv_demos.h>',
        "void led_control_screen_begin();`nvoid led_control_screen_poll();")
    $source = $source.Replace('    lv_demo_widgets();', '    led_control_screen_begin();')
}
if (!$source.Contains('  led_control_screen_poll();')) {
    if (!$source.Contains('  lv_timer_handler();')) {
        throw 'Expected LVGL loop entry point missing from the vendor sketch.'
    }
    $source = $source.Replace('  lv_timer_handler();',
        "  led_control_screen_poll();`n  lv_timer_handler();")
}

Copy-Item -LiteralPath $lvConfig -Destination (Join-Path $libraries 'lv_conf.h') -Force
Copy-Item -LiteralPath (Join-Path $project 'firmware/esp32/src/ring_ping.cpp') -Destination $sketch -Force
Copy-Item -LiteralPath (Join-Path $project 'shared/protocol/ring_frame.h') -Destination $sketch -Force
[IO.File]::WriteAllText($sketchFile, $source)
Write-Output $sketch
