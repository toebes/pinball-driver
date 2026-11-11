# PinballDriver

Firmware project for four Puya PY32F002B nodes and an ESP32 coordinator in a pinball driver system. A shared ring protocol defines communication between the devices.

## Project status

The ESP32 touchscreen assigns addresses, sends RGB commands to a selected node and port, and displays Hall sensor readings. The full ring and one-node-at-a-time firmware updates have been verified on hardware. Version 0.4 was tested on the bench, where the Hall reading changed when a ball was present. Automatic ball detection sampling is disabled in the ring bench build. On-demand monitoring remains available for inputs within the ADC voltage range.

See the [PY32 build](docs/py32-build.md), [ESP32 build](docs/esp32-build.md), and [ring protocol](shared/protocol/README.md).

No Puya or Espressif SDK files are included. Obtain and verify the official device support files and their redistribution terms before adding them. Do not substitute startup code, memory maps, or peripheral headers from a different chip.

## Layout

```text
firmware/
  py32/       PY32F002B node application source and headers
  esp32/      ESP32 coordinator application source and headers
shared/
  protocol/   Ring protocol specification and shared definitions
cmake/        CMake toolchain and build support
debug/        Verified OpenOCD and debugger configuration
docs/         Architecture, hardware, and bring-up notes
hardware/     Schematics, pin assignments, and board documentation
tools/        Project-specific development utilities
.vscode/      Shared VS Code extension recommendations
```

## Development environment

The Windows 11 / VS Code environment uses:

- Arm GNU Toolchain 15.3.1 (`arm-none-eabi`)
- CMake 3.30.1
- Ninja 1.12.1
- OpenOCD
- VS Code C/C++, CMake Tools, and Cortex-Debug extensions

See the [architecture notes](docs/architecture.md) for UART wiring and open decisions.
