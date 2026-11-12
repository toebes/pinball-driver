# PY32 first build

The node firmware sends SK6812 LED frames, supports Hall sampling, and handles UART ring address assignment and LED commands. The debugger can inspect `heartbeat_count`.

## Build in PowerShell

From the repository folder:

```powershell
cmake -S . -B build/py32 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake" "-DARM_GNU_BIN=C:/Tools/ArmGNU/bin" "-DPUYA_SDK_ROOT=C:/Tools/Puya/PY32F002B_Firmware"
cmake --build build/py32
```

CMake and Ninja must be on PATH. If Ninja is not found, add `"-DCMAKE_MAKE_PROGRAM=C:/Program Files/bin/ninja.exe"` to the configure command.

Outputs: `build/py32/py32_smoke.elf`, `.hex`, `.bin`, and `.map`.

The build references the external official SDK, including its GCC startup assembly, system initialization source, CMSIS headers, and `py32f002bx5.ld`. It does not copy SDK files into Git. The SDK template selects `PY32F002Bx5`, Cortex-M0+, 24 KB flash, and 3 KB RAM. Confirm these against the fitted device before programming.

Local validation used Arm GNU 15.3.1 and CMake 3.30.1. The link can report vendor-script RWX segment and unused newlib file I/O warnings. Review the linker layout before treating this as a production build.

For the verified ring bench build, use `-B build/py32-ring-bench` and add `"-DPINBALL_ENABLE_HALL_ADC=OFF"` when configuring. Build that directory and use the bootloader and application ELFs for initial SWD installation on each node.

## Verified ST-LINK bring-up

The ST-LINK/V2 connected without NRST at 100 kHz using the SDK-bundled Puya OpenOCD. Flash programming and readback verification succeeded. The `heartbeat_count` value at `0x20000020` advanced from `0x03153d8c` to `0x031d5ddb` across a 200 ms run interval. This address belongs to this build only; use the symbol in the debugger.

`debug/py32-stlink.cfg` keeps both vendor reset hooks at 100 kHz. The default speed increase caused a programming failure in this setup.

The original 24 KB flash contents were saved locally in `.local/backups/before-smoke.bin`. Vendor tools and backup files are ignored by Git.

To reproduce the local tool setup, extract `Packs/GCC/openocd-0.12.0.zip` from the official SDK into `.local/puya/`.

For nodes with the resident bootloader installed, attach the debugger and load symbols from py32_app.elf. Inspect heartbeat_count to confirm that the application is running.

## Resident bootloader bench build

The PY32 targets use size optimization and link without semihosting. The ring
bench build produces `py32_bootloader.elf` and `py32_app.elf`.

| Region | Start | Size |
|---|---|---:|
| Resident bootloader | 0x08000000 | 4 KiB |
| Application | 0x08001000 | 8 KiB |
| Staging | 0x08003000 | 8 KiB |
| Update metadata | 0x08005000 | 4 KiB |

With the ST-LINK attached, program and verify both ELFs at their linked
addresses, then reset and run. The bootloader checks the application's initial
stack and reset vector before handing off. SWD confirmed that the relocated
application runs with VTOR at `0x08001000` and an advancing heartbeat.

Use the standalone `py32_smoke.elf` only on a board where you intend to
replace the resident bootloader. It is linked at the flash base. For an
installed bootloader, load application symbols from `py32_app.elf`.

Firmware commands 10 through 12 stage and verify an application image, then request activation by the resident bootloader. The default application version is 0.6. Pass `-DPINBALL_VERSION_MINOR=6` when reconfiguring a build directory with an older cached version. Set `-DPINBALL_VERSION_MINOR=0` in a separate bench build to create an older test target.

Application diagnostics can travel as DEBUG packets to the ESP32 USB serial
log. Only one OpenOCD session can use the ST-LINK at a time.

The bench build leaves automatic Hall sampling disabled. Monitor_Sensor starts
ADC sampling on demand for the selected port. Confirm the sensor output stays
within the PY32 ADC input range before monitoring a connected sensor.
