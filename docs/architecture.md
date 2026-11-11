# Architecture

## Confirmed project scope

- Four PY32F002B nodes (reported part: PY32F002BF15U6TR).
- An ESP32-8048S070C display board, PCB V1.4 (ESP32-S3) as the coordinator.
- A shared ring communication protocol.
- Windows 11 and VS Code for development, with CMake/Ninja for the PY32 build.

## Coordinator UART wiring

The V1.4 board photo shows P4 labeled IO18, IO17, 3.3V, and GND. The ring uses UART1 at 115200 baud, 8N1, with GPIO17 as TX and GPIO18 as RX.

| P4 | Supplied wire color | First bench link | Full ring |
|---|---|---|---|
| IO17 (TX) | Blue | Node 1 PB5 (RX) | Node 1 PB5 (RX) |
| IO18 (RX) | Black | Node 1 PB4 (TX) | Node 4 TX |
| GND | Red | Node board GND | Node board GND |
| 3.3V | Yellow | Leave open with separate board supplies | Leave open with separate board supplies |

Check that both ends use compatible 3.3 V UART signal levels. Use the P4 silkscreen to identify the wires. GPIO17 and GPIO18 are allocated to the ring, so keep I2S audio and any GPIO18 touch interrupt disabled.

## Decisions still required

- Node responsibilities and board pin assignments.
- Ring retry policy, Node_Config payload, and firmware update commands.
- Output enable behavior, communication-loss behavior, and any hardware-specific drive limits.
- Select a destination node from the ESP32 LED screen.
- Official PY32 SDK release and licensing.

The ring returned Node_Config replies from all four PY32s. The LED command was verified on Node 1.
