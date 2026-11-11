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

Local validation used Arm GNU 15.3.1 and CMake 3.30.1. The initial link succeeds with vendor-script RWX segment and newlib unimplemented file-I/O warnings. The smoke program does not use file I/O. Review the linker layout before treating this as a production build.

For the verified ring bench build, use `-B build/py32-ring-bench` and add `"-DPINBALL_ENABLE_HALL_ADC=OFF"` when configuring. Build that directory and use its ELF for all four nodes.

## Verified ST-LINK bring-up

The ST-LINK/V2 connected without NRST at 100 kHz using the SDK-bundled Puya OpenOCD. Flash programming and readback verification succeeded. The `heartbeat_count` value at `0x20000020` advanced from `0x03153d8c` to `0x031d5ddb` across a 200 ms run interval. This address belongs to this build only; use the symbol in the debugger.

`debug/py32-stlink.cfg` keeps both vendor reset hooks at 100 kHz. The default speed increase caused a programming failure in this setup.

The original 24 KB flash contents were saved locally in `.local/backups/before-smoke.bin`. Vendor tools and backup files are ignored by Git.

To reproduce the local tool setup, extract `Packs/GCC/openocd-0.12.0.zip` from the official SDK into `.local/puya/`.

In VS Code, open the PinballDriver folder, select **PY32 smoke - ST-LINK** in Run and Debug, and press F5. This local launch configuration programs the smoke ELF and stops at main. Add `heartbeat_count` to Watch and continue execution; pause to inspect its value. The VS Code debug session was verified during bring-up.

## Semihosting printf

The smoke firmware now links with `rdimon.specs`, initializes monitor handles, and uses a small line buffer for stdout. OpenOCD enables semihosting when the target halts. The board successfully printed:

```text
PinballDriver: PY32 semihosting ready. Core clock: 24000000 Hz
```

In VS Code, start debugging with F5, then Continue past the breakpoint at main. Output appears in the **Terminal** tab named **gdb-server**. Use a newline in printf messages, or call `fflush(stdout)` to flush a partial line.

The firmware checks whether halting debug is enabled before using semihosting. Semihosting output requires OpenOCD to be attached and configured. Printing also interrupts normal processor execution, so avoid it in timing-critical paths.

Only one OpenOCD session can use the ST-LINK at a time. Stop the existing debug session before starting another programming or connection check.
