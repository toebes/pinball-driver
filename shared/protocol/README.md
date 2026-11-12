# PY32 ring wire format

The ring uses UART at 115200 baud, 8 data bits, no parity, and one stop bit.

| Field | Size | Encoding |
|---|---:|---|
| Length | 1 byte | Number of bytes after this byte, including the CRC |
| Addresses | 1 byte | Source in the high nibble, destination in the low nibble |
| Control | 1 byte | TTL in the high nibble, command or internal destination in the low nibble |
| Payload | 0 to 130 bytes | Defined by the command |
| CRC | 2 bytes | CRC-16/IBM-3740, most significant byte first |

Use CRC-16/IBM-3740 (also called CRC-16/CCITT-FALSE) on every frame. Parameters are polynomial 0x1021, initial value 0xFFFF, no reflection, and final XOR 0x0000. Cover the length byte, both header bytes, and the payload. The check value for ASCII '123456789' is 0x29B1.

Valid length values are 4 through 134. The largest frame is 135 bytes on the wire. Zero is a regular byte within a frame. An invalid length, UART receive error, CRC failure, or timeout requires the receiver to wait for an idle gap before accepting a new frame.

A node validates the frame before acting on it. For unicast, a node consumes frames addressed to it. Otherwise, it decrements TTL and forwards only if the result is positive. A zero-TTL frame is never forwarded. A forwarder preserves the addresses and recomputes the CRC after updating TTL. An initial TTL of 5 covers the five-device ring.

## Address assignment

The coordinator is address 0. PY32s start at address 15 after reset. Addresses
1 through 13 are assignable, and 14 is reserved for multicast. Receivers
discard frames with their own source address. The receive recovery gap is 20 ms.

The coordinator starts discovery with command 1 (Set Address), source 0,
destination 15, TTL 15, and payload 0x01. The low nibble of the one-byte
payload is the new address. The upper nibble is reserved and is currently zero.

An unassigned PY32 consumes Set Address, adopts the requested address, and
sends these frames in order:

1. Set Address to 15 with TTL 15. Preserve the original source and upper
   payload nibble, and increment the address in the low nibble. Address 13
   ends assignment to avoid assigning reserved address 14.
2. Command 2 (Node_Config) from the newly assigned address to the original
   source, with TTL 15 and a one-byte payload. The payload is currently zero
   pending definition.

An assigned PY32 forwards Set Address like any other frame for a different
destination, decrementing TTL and updating CRC. The coordinator consumes
Node_Config replies and drops its own returning Set Address frame. Reset the
PY32s before a fresh enumeration so they all start at 15.

## Set Port Type

Command 3 carries two payload bytes from the coordinator to an assigned
node. The first is the port index (0 for Port 1 through 5 for Port 6). The
second is the port type. The length byte is 6, the complete frame is 7 bytes,
and the coordinator sends with TTL 5. There is no reply.

| Value | Port type |
|---:|---|
| 0 | Undefined |
| 1 | LED output only |
| 2 | LED with Hall Effect Sensor |
| 3 | Standard Servo |
| 4 | Servo with Position feedback |

Port types start as undefined and are held in RAM until reset. The two
servo types enable servo position commands. Closed-loop position feedback
is pending. In builds with automatic Hall sampling enabled,
type 2 enables the Hall-based LED indication until a Set LED color
command overrides it. On-demand Monitor_Sensor works independently.

## IDLE and ring inactivity

Command 0 (IDLE) has no payload and no reply. Its length byte is 4 and
the complete frame is 5 bytes. Only the ESP32 originates IDLE, using source 0,
destination 14, and an initial TTL equal to the number of PY32s in the ring
(4 on the current board). Each node counts the frame as activity and forwards
it with the usual TTL and CRC rules. The last node receives it with TTL 1.

The coordinator waits 1 second after startup or its last non-IDLE
transmission, then sends IDLE every 500 ms. Any outgoing non-IDLE frame
restarts the 1-second wait.

After 1 second without receiving a valid frame from another address,
including after startup, each PY32 shows red, green, then blue on all
six ports at brightness 16 out of 256, changing every 500 ms. Frames
addressed elsewhere also count as activity. The next valid frame restores
normal output using the colors and port types retained during the timeout,
with any changes made by that frame.
Servo pulses pause during the idle LED cycle and resume at the last
commanded position when traffic returns.

## Set LED color

Command 4 carries four bytes in order: LED index (0 through 5), red, green,
and blue. Index 0 is LED1. The length byte is 8 and the total frame is
9 bytes. The coordinator sends to the selected node address with TTL 5. The target applies
the RGB values after validating the CRC and leaves that LED under remote
control until reset. Servo ports ignore Set LED color. The target sends a
DEBUG message with the RGB values after accepting the command.


## Monitor_Sensor

Command 5 carries two payload bytes. The length byte is 6 and the complete
frame is 7 bytes. Port index 0 corresponds to Port 1 and index 5 to Port 6.

| Direction | Byte 0 | Byte 1 |
|---|---|---|
| Coordinator to selected PY32 | Port index | 1 starts monitoring, 0 stops |
| PY32 to coordinator | Port index | Scaled ADC reading, 0 through 255 |

An assigned node reports one selected port every 250 ms while monitoring.
A start request selects or replaces the monitored port. A stop request ends
monitoring for the specified port. The 12-bit ADC range maps to 0 through 255
with rounding. The ESP32 shows the last five readings with the newest first
and prints received samples to USB serial. The request has no separate ACK.
An accepted firmware update stops monitoring on its target.

## Set_Servo_Position

Command 6 carries the port index (0 through 5), followed by a signed
two's-complement position byte (-128 through 127). The length byte is 6
and the complete frame is 7 bytes. The ESP32 sends it to an assigned
node with TTL 5. The command has no reply and only port types 3 and 4
accept it.

Positions -128, 0, and 127 map to 1.0 ms, 1.5 ms, and 2.0 ms high pulses
respectively, repeated every 20 ms. The node starts sending pulses after
the first position command. Changing away from a servo port type clears
the saved position.

## Command registry

| Value (decimal) | Command | Status |
|---:|---|---|
| 0 | IDLE | Implemented |
| 1 | Set Address | Implemented |
| 2 | Node_Config | Implemented. Payload definition pending. |
| 3 | Set Port Type | Implemented |
| 4 | Set LED color | Implemented |
| 5 | Monitor_Sensor | Implemented |
| 6 | Set_Servo_Position | Implemented. Position feedback pending. |
| 10 | Firmware Start | Implemented |
| 11 | Firmware Data | Implemented |
| 12 | Firmware Apply | Implemented |
| 13 | DEBUG | Implemented |

## Firmware update

The ESP32 stores the update image and updates one assigned PY32 at a time.
Each command is unicast. Firmware Start accepts a version strictly greater than the running major/minor pair. Replies swap source and destination and use the same
command number. Every frame still carries the ring CRC-16.

| Command | ESP32 request payload | PY32 response payload |
|---|---|---|
| 10 Firmware Start | Major, minor, 0 | Current major, current minor, status (0 = no update, 1 = update needed) |
| 11 Firmware Data | Two-byte byte offset, then 1 to 128 image bytes | Same two-byte offset |
| 12 Firmware Apply | Four-byte image CRC-32, then 0 | Calculated image CRC-32, then status (0 = rejected, 1 = accepted) |

Use byte offsets relative to the start of the application image. Wire
order for offsets and CRC-32 is most significant byte first. Start at offset
0 and send contiguous chunks, using 128 bytes except for a possible short
final chunk. The ESP32 waits for the matching offset ACK before sending the
next chunk. The receiver validates duplicate retries and re-acknowledges them
without advancing the accepted image length. The node acknowledges data only after the staging page passes readback. Out-of-order and failed writes receive no ACK. The maximum data frame is 135
bytes on the wire.

The image check is CRC-32/ISO-HDLC over the exact image bytes, excluding
any flash-page padding. The check value for ASCII '123456789' is 0xCBF43926.
Use the end of the last accepted contiguous chunk as the image length.
A successful Apply reply means the image passed its CRC and is accepted for
activation. Wait for UART transmission complete before activating or resetting.

After reboot, the updated PY32 is at address 15. The ESP32 sends Set Address
(command 1) to 15 with the updated node's previous address and waits for that
node's Node_Config. This recovery assumes the other nodes keep their assigned
addresses. If multiple nodes reset, run a fresh enumeration with all PY32s reset.

The bootloader reserves separate 8 KiB application and staging regions.
Firmware Apply checks the staged vectors and CRC, writes a pending record,
and acknowledges before resetting. At reset the bootloader rechecks staging,
copies the image into the application region, verifies the application CRC,
then erases the pending record. A reset during copy leaves the pending record
available for the next boot to retry. A persistently invalid staged image or
failed copy requires SWD service. The firmware CRC detects corruption.

## DEBUG

Command 13 carries up to 130 bytes of debug text with destination 0. Send
plain ASCII or UTF-8 bytes without a terminating NUL. The frame length gives
the text length. Forwarders apply the usual TTL and CRC rules. The ESP32
writes the text to USB serial with a `Node <address> debug:` prefix. DEBUG
has no reply. The PY32 application emits an acknowledgement text such as
`LED 1 RGB: 25, 0, 0` after applying a Set LED command.
