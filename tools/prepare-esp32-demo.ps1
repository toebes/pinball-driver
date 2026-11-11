param(
    [Parameter(Mandatory = $true)]
    [string] $DemoArduinoRoot,
    [string] $Py32Build
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

# Keep touch input active while suppressing the vendor demo's coordinate trace.
$source = $source -replace '(?m)^[ \t]*Serial\.(?:print\(\s*"Data [xy] "\s*\)|println\(\s*data->point\.[xy]\s*\));\r?\n', ''

Copy-Item -LiteralPath $lvConfig -Destination (Join-Path $libraries 'lv_conf.h') -Force
Copy-Item -LiteralPath (Join-Path $project 'firmware/esp32/src/ring_ping.cpp') -Destination $sketch -Force
Copy-Item -LiteralPath (Join-Path $project 'shared/protocol/ring_frame.h') -Destination $sketch -Force
Copy-Item -LiteralPath (Join-Path $project 'shared/protocol/firmware_update.h') -Destination $sketch -Force

$imageHeader = Join-Path $sketch 'py32_update_image.h'
if ($Py32Build) {
    $build = (Resolve-Path -LiteralPath $Py32Build).Path
    $image = [IO.File]::ReadAllBytes((Join-Path $build 'py32_app.bin'))
    if ($image.Length -lt 8 -or $image.Length -gt 8192) {
        throw 'PY32 application image must fit in the 8 KiB staging slot.'
    }
    $cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
    if ($cache -notmatch '(?m)^PINBALL_VERSION_MAJOR:STRING=(\d+)\r?$') {
        throw 'Firmware major version missing from CMake cache.'
    }
    $major = [byte]$Matches[1]
    if ($cache -notmatch '(?m)^PINBALL_VERSION_MINOR:STRING=(\d+)\r?$') {
        throw 'Firmware minor version missing from CMake cache.'
    }
    $minor = [byte]$Matches[1]

    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('#pragma once')
    $lines.Add('#define PINBALL_HAS_UPDATE_IMAGE 1')
    $lines.Add("static constexpr uint8_t image_major = $major;")
    $lines.Add("static constexpr uint8_t image_minor = $minor;")
    $lines.Add('static constexpr uint8_t py32_update_image[] = {')
    for ($start = 0; $start -lt $image.Length; $start += 16) {
        $end = [Math]::Min($start + 15, $image.Length - 1)
        $bytes = $image[$start..$end] | ForEach-Object { '0x{0:X2}' -f $_ }
        $lines.Add('    ' + ($bytes -join ', ') + ',')
    }
    $lines.Add('};')
    [IO.File]::WriteAllLines($imageHeader, $lines)
} else {
    [IO.File]::WriteAllText($imageHeader, '#define PINBALL_HAS_UPDATE_IMAGE 0')
}

[IO.File]::WriteAllText($sketchFile, $source)
Write-Output $sketch
