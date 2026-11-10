# Initial architecture

## Confirmed project scope

- Four PY32F002B nodes (reported part: PY32F002BF15U6TR).
- An ESP32 coordinator.
- A shared ring communication protocol.
- Windows 11 and VS Code for development, with CMake/Ninja planned for the PY32 build.

## Decisions still required

- Node responsibilities and board pin assignments.
- Ring wiring, electrical interface, baud rate, framing, addressing, error handling, and timeout behavior.
- Output enable behavior, communication-loss behavior, and any hardware-specific drive limits.
- Exact ESP32 device and development framework.
- Official PY32 SDK release and licensing.
- Verified startup code, linker memory map, clock setup, and flash programming configuration.

Do not treat this scaffold as a working firmware implementation or a validated hardware configuration.
