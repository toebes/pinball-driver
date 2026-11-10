# PinballDriver

Firmware project for four Puya PY32F002B nodes and an ESP32 coordinator in a pinball driver system. A shared ring protocol will define communication between the devices.

## Project status

This repository starts with a project layout and development notes. Firmware, protocol definitions, board pin assignments, and build targets have not been implemented yet.

No Puya or Espressif SDK files are included. Obtain and verify the official device support files and their redistribution terms before adding them. Do not substitute startup code, memory maps, or peripheral headers from a different chip.

## Layout

```text
firmware/
  py32/       PY32F002B node application source and headers
  esp32/      ESP32 coordinator application source and headers
shared/
  protocol/   Ring protocol specification and shared definitions
cmake/        Future CMake toolchain and build support
debug/        Future verified OpenOCD and debugger configuration
docs/         Architecture, hardware, and bring-up notes
hardware/     Schematics, pin assignments, and board documentation
tools/        Project-specific development utilities
.vscode/      Shared VS Code extension recommendations
```

## Development environment

The planned Windows 11 / VS Code environment uses:

- Arm GNU Toolchain 15.3.1 (`arm-none-eabi`)
- CMake 3.30.1
- Ninja 1.12.1
- OpenOCD
- VS Code C/C++, CMake Tools, and Cortex-Debug extensions

These are the versions reported during setup, rather than minimum version requirements. The Arm toolchain is for the PY32 firmware; select the ESP32 SDK and toolchain after confirming the exact ESP32 variant.

Open this folder in VS Code. There are no build or flash commands yet: a working build requires the official PY32F002B startup code, linker configuration, CMSIS/device headers, and selected peripheral drivers. Debug and flash configuration must match the actual target and programming setup.

## Next steps

1. Obtain the official Puya SDK for the PY32F002BF15U6TR and record its version, source, and license.
2. Confirm board pin assignments, clock configuration, and memory layout from the device documentation.
3. Add a minimal PY32 application and CMake/Ninja build, then validate it on hardware.
4. Verify the flash programming method before adding shared debugger settings.
5. Define the ring protocol and node roles, then select the exact ESP32 target and coordinator framework.

See [architecture notes](docs/architecture.md) for the initial scope and unresolved decisions.
