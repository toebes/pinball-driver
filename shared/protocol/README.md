# PY32 ring wire format

The ring uses UART at 115200 baud, 8 data bits, no parity, and one stop bit.

| Field | Size | Encoding |
| --- | ---: | --- |
| Length | 1 byte | Number of bytes after this byte, including the CRC |
| Addresses | 1 byte | Source in the high nibble, destination in the low nibble |
| Control | 1 byte | TTL in the high nibble, command or internal destination in the low nibble |
| Payload | 0 to 130 bytes | Defined by the command |
| CRC | 2 bytes | CRC-16/IBM-3740, most significant byte first |

Use CRC-16/IBM-3740 (also called CRC-16/CCITT-FALSE) on every frame. Parameters are polynomial 0x1021, initial value 0xFFFF, no reflection, and final XOR 0x0000. Cover the length byte, both header bytes, and the payload. The check value for ASCII '123456789' is 0x29B1.

Valid length values are 4 through 134. The largest frame is 135 bytes on the wire. Zero is a regular byte within a frame. An invalid length, UART receive error, CRC failure, or timeout requires the receiver to wait for an idle gap before accepting a new frame.

A node validates the frame before acting on it. For unicast, a node consumes frames addressed to it. Otherwise, it decrements TTL and forwards only if the result is positive. A zero-TTL frame is never forwarded. A forwarder preserves the addresses and recomputes the CRC after updating TTL. An initial TTL of 5 covers the five-device ring.

## Command registry

| Value (decimal) | Command | Status |
| ---: | --- | --- |
| 0 | `CMD_IDLE` | Implemented |
| 1 | `CMD_SET_ADDRESS` | Implemented |
| 2 | `CMD_NODE_CONFIG` | Implemented. Payload definition pending. |
| 3 | `CMD_SET_PORT_TYPE` | Implemented |
| 4 | `CMD_SET_LED_COLOR` | Implemented |
| 5 | `CMD_MONITOR_SENSOR` | Implemented |
| 6 | `CMD_SET_SERVO_POSITION` | Implemented. Position feedback pending. |
| 7 | Unused | Unused |
| 8 | Unused | Unused |
| 9 | Unused | Unused |
| 10 | `CMD_FIRMWARE_START` | Implemented |
| 11 | `CMD_FIRMWARE_DATA` | Implemented |
| 12 | `CMD_FIRMWARE_APPLY` | Implemented |
| 13 | `CMD_DEBUG` | Implemented |
| 14 | Unused | Unused |
| 15 | Unused | Unused |

## Address assignment (`CMD_SET_ADDRESS`)

The coordinator is address 0. PY32s start at address 15 after reset. Addresses
1 through 13 are assignable, and 14 is reserved for multicast. Receivers
discard frames with their own source address. The receive recovery gap is 20 ms.

The coordinator starts discovery with command 1 (`CMD_SET_ADDRESS`), source 0,
destination 15, TTL 15, and payload 0x01. The low nibble of the one-byte
payload is the new address. The upper nibble is reserved and is currently zero.

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 05 | Packet Length |
| 1 | 0F | Source Address=0 Destination=15 |
| 2 | F1 | TTL=15 Command=1 `CMD_SET_ADDRESS` |
| 3 | 01 | Top Nibble Reserved New Address=1 |
| 4 | XX | CRC-HI Byte |
| 5 | XX | CRC-LO Byte |

An unassigned PY32 (which technically has the address 15 by default) consumes `CMD_SET_ADDRESS`, adopts the requested address, and
sends these frames in order:

1. `CMD_SET_ADDRESS` to 15 with TTL 15. Preserve the original source
and upper payload nibble, and increment the address in the low nibble.
Address 13 ends assignment to avoid assigning reserved address 14.

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 05 | Packet Length |
| 1 | 0F | Source Address=0 Destination=15 |
| 2 | F1 | TTL=15 Command=1 `CMD_SET_ADDRESS` |
| 3 | 02 | Top Nibble Reserved New Address=2 |
| 4 | XX | CRC-HI Byte |
| 5 | XX | CRC-LO Byte |

2. Command 2 (`CMD_NODE_CONFIG`) from the newly assigned address to the original
   source, with TTL 15 and a one-byte payload. The payload is currently zero
   pending definition.

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 05 | Packet Length |
| 1 | 10 | Source Address=1 Destination=0 |
| 2 | F2 | TTL=15 Command=2 `CMD_NODE_CONFIG` |
| 3 | YY | Configuration data (meaning unspecified) |
| 4 | XX | CRC-HI Byte |
| 5 | XX | CRC-LO Byte |

An assigned PY32 forwards `CMD_SET_ADDRESS` like any other frame for a different
destination, decrementing TTL and updating CRC. The coordinator consumes
`CMD_NODE_CONFIG` replies and drops its own returning `CMD_SET_ADDRESS` frame. Reset the
PY32s before a fresh enumeration so they all start at 15.

## Set Port Type (`CMD_SET_PORT_TYPE`)

Command 3 carries two payload bytes from the coordinator to an assigned
node. The first is the port index (0 for Port 1 through 5 for Port 6). The
second is the requested port type. Send 0 to query the current setting.
Values 1 through 4 set the port type. The node replies to either request
with the port index and its current type. Both frames have a length byte
of 6, 7 bytes total, and an initial TTL of 5.

Port Type Request

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 06 | Packet Length |
| 1 | 0N | Source Address=0 Destination=N (target node) |
| 2 | 53 | TTL=5 Command=3 `CMD_SET_PORT_TYPE` |
| 3 | 0P | Port Number (0-5) |
| 4 | TT | 00=Query, 01-04=Set Port Type |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

Port Type Response

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 06 | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | 53 | TTL=5 Command=3 `CMD_SET_PORT_TYPE` |
| 3 | 0P | Port Number (0-5) |
| 4 | TT | Current Port Type (00-04) |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

| Value | Port type |
| ---: | --- |
| 00 | Undefined |
| 01 | LED output only |
| 02 | LED with Hall Effect Sensor |
| 03 | Standard Servo |
| 04 | Servo with Position feedback |

The ESP32 queries the selected node and port on startup, after address
assignment, and when the selection changes. The dropdown defaults to
Undefined until a matching response arrives. Selecting Undefined and
pressing Set Port Type sends a query.

The query requires PY32 firmware 0.8 or newer. Older firmware interprets
00 as a request to set the port to Undefined.

A node with blank or invalid settings starts with all ports undefined.
The relocated PY32 application saves changed port types immediately and
restores them after resets and ring firmware updates. A full chip erase
clears the saved types. Repeating the current type does not write flash.
If saving fails, the node replies with the previous type and sends a
`CMD_DEBUG` message. The two servo types enable servo position commands. Closed-loop position feedback
is pending. In builds with automatic Hall sampling enabled,
type 2 enables the Hall-based LED indication until a `CMD_SET_LED_COLOR`
command overrides it. On-demand `CMD_MONITOR_SENSOR` works independently.

## Ring Heartbeat Activity (`CMD_IDLE`)

Command 0 (`CMD_IDLE`) has no payload and no reply. Its length byte is 4 and
the complete frame is 5 bytes. Only the ESP32 originates `CMD_IDLE`, using source 0,
destination 14, and an initial TTL equal to the number of PY32s in the ring
(4 on the current board). Each node counts the frame as activity and forwards
it with the usual TTL and CRC rules. The last node receives it with TTL 1.

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 04 | Packet Length |
| 1 | 0E | Source Address=0 Destination=14 (Multicast) |
| 2 | F0 | TTL=15 Command=0 `CMD_IDLE` |
| 3 | XX | CRC-HI Byte |
| 4 | XX | CRC-LO Byte |

The coordinator waits 1 second after startup or its last non-`CMD_IDLE`
transmission, then sends `CMD_IDLE` every 500 ms. Any outgoing non-`CMD_IDLE` frame
restarts the 1-second wait.

After 1 second without receiving a valid frame from another address,
including after startup, each PY32 shows red, green, then blue on all
six ports at brightness 16 out of 256, changing every 500 ms. Frames
addressed elsewhere also count as activity. The next valid frame restores
normal output using the colors and port types retained during the timeout,
with any changes made by that frame.
Servo pulses pause during the idle LED cycle and resume at the last
commanded position when traffic returns.

## Set LED Color (`CMD_SET_LED_COLOR`)

Command 4 carries four bytes in order: LED index (0 through 5), red, green,
and blue. Index 0 is LED1. The length byte is 8 and the total frame is
9 bytes. The coordinator sends to the selected node address with TTL 5. The target applies
the RGB values after validating the CRC and leaves that LED under remote
control until reset. Servo ports ignore `CMD_SET_LED_COLOR`. The target can send a
`CMD_DEBUG` message with the RGB values after accepting the command.

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 09 | Packet Length |
| 1 | 0N | Source Address=0 Destination=N (target node) |
| 2 | F0 | TTL=15 Command=4 `CMD_SET_LED_COLOR` |
| 3 | 0P | 0=Unused P=Port Number (0-5) |
| 4 | RR | Red value for LED |
| 5 | GG | Green value for LED |
| 6 | BB | Blue Value for LED |
| 7 | XX | CRC-HI Byte |
| 8 | XX | CRC-LO Byte |


## Monitor Sensor (`CMD_MONITOR_SENSOR`)

Command 5 carries two payload bytes. The length byte is 6 and the complete
frame is 7 bytes. Port index 0 corresponds to Port 1 and index 5 to Port 6.

| Direction | Byte 0 | Byte 1 |
| --- | --- | --- |
| Coordinator to selected PY32 | Port index | 1 starts monitoring, 0 stops |
| PY32 to coordinator | Port index | Scaled ADC reading, 0 through 255 |

An assigned node reports one selected port every 250 ms while monitoring.
A start request selects or replaces the monitored port. A stop request ends
monitoring for the specified port. The 12-bit ADC range maps to 0 through 255
with rounding. The ESP32 shows the last five readings with the newest first
and prints received samples to USB serial. The request has no separate ACK.
An accepted firmware update stops monitoring on its target.

Monitor Request

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 07 | Packet Length |
| 1 | 0N | Source Address=0 Destination=N (target node) |
| 2 | F0 | TTL=15 Command=5 `CMD_MONITOR_SENSOR` |
| 3 | 0P | 0=Unused P=Port Number (0-5) |
| 4 | SS | Start/Stop for request 1=Start Monitoring, 0=Stop |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

Monitor Response

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 07 | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | F5 | TTL=15 Command=5 `CMD_MONITOR_SENSOR` |
| 3 | 0P | 0=Unused P=Port Number (0-5) |
| 4 | VV | Scaled value (0-255) read from port |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

## Set Servo Position (`CMD_SET_SERVO_POSITION`)

Command 6 carries the port index (0 through 5), followed by a signed
two's-complement position byte (-128 through 127). The length byte is 6
and the complete frame is 7 bytes. The ESP32 sends it to an assigned
node with TTL 5. The command has no reply and only port types 3 and 4
accept it.

Positions -128, 0, and 127 map to 1.0 ms, 1.5 ms, and 2.0 ms high pulses
respectively, repeated every 20 ms. The node starts sending pulses after
the first position command. Changing away from a servo port type clears
the saved position.

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 07 | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | F6 | TTL=15 Command=6 `CMD_SET_SERVO_POSITION` |
| 3 | 0P | 0=Unused P=Port Number (0-5) |
| 4 | VV | Position to set servo to (signed byte -128-127) |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

## Firmware update

The ESP32 stores the update image and updates one assigned PY32 at a time.
Each command is unicast. `CMD_FIRMWARE_START` accepts a version strictly greater than the running major/minor pair. Replies swap source and destination and use the same
command number. Every frame still carries the ring CRC-16.

Firmware Start Request

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 07 | Packet Length |
| 1 | 0N | Source Address=0 Destination=N |
| 2 | FA | TTL=15 Command=10 `CMD_FIRMWARE_START` |
| 3 | HH | Major version number |
| 3 | LL | Minor Version Number |
| 4 | 00 | 0 |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

Firmware Start Response

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 07 | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | FA | TTL=15 Command=10 `CMD_FIRMWARE_START` |
| 3 | HH | Major version number |
| 3 | LL | Minor Version Number |
| 4 | SS | Status: 00=No update, 01=Update needed |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |

Firmware Data Request

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | NN | Packet Length |
| 1 | 0N | Source Address=0 Destination=N |
| 2 | FB | TTL=15 Command=11 `CMD_FIRMWARE_DATA` |
| 3 | HH | High Byte of Offset to data |
| 4 | LL | Low Byte of Offset to data |
| 5 | DD | First byte of data |
| ... | DD | Remaining bytes of data |
| N+6 | XX | CRC-HI Byte |
| N+6 | XX | CRC-LO Byte |

Firmware Data Response (note that there is no data in the response payload)

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 07 | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | FB | TTL=15 Command=11 `CMD_FIRMWARE_DATA` |
| 3 | HH | High Byte of Offset to data |
| 4 | LL | Low Byte of Offset to data |
| 5 | XX | CRC-HI Byte |
| 6 | XX | CRC-LO Byte |


Firmware Apply Request

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 0A | Packet Length |
| 1 | 0N | Source Address=0 Destination=N |
| 2 | FC | TTL=15 Command=12 `CMD_FIRMWARE_APPLY` |
| 3 | IC4 | High byte of image CRC-32 |
| 4 | IC3 | Second High byte of image CRC-32 |
| 5 | IC2 | Next to low byte of image CRC-32 |
| 6 | IC1 | Low byte of image CRC-32 |
| 7 | 00 | 0 |
| 8 | XX | CRC-HI Byte |
| 9 | XX | CRC-LO Byte |

Firmware Apply Response

| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | 0A | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | FC | TTL=15 Command=12 `CMD_FIRMWARE_APPLY` |
| 3 | IC4 | High byte of image CRC-32 |
| 4 | IC3 | Second High byte of image CRC-32 |
| 5 | IC2 | Next to low byte of image CRC-32 |
| 6 | IC1 | Low byte of image CRC-32 |
| 7 | SS | Status 0=rejected, 1=accepted |
| 8 | XX | CRC-HI Byte |
| 9 | XX | CRC-LO Byte |

Byte offsets are relative to the start of the application image. Wire
order for offsets and CRC-32 is most significant byte first. Start at offset
0 and send contiguous chunks, using 128 bytes except for a possible short
final chunk. The ESP32 waits for the matching offset ACK before sending the
next chunk. The receiver validates duplicate retries and re-acknowledges them
without advancing the accepted image length. The node acknowledges data only after the staging page passes readback. Out-of-order and failed writes receive no ACK. The maximum data frame is 135
bytes on the wire.

The image check is CRC-32/ISO-HDLC over the exact image bytes, excluding
any flash-page padding. The check value for ASCII '123456789' is 0xCBF43926.
Use the end of the last accepted contiguous chunk as the image length.
A successful `CMD_FIRMWARE_APPLY` reply means the image passed its CRC and is accepted for
activation. The system waits for UART transmission complete before activating or resetting in order to ensure that the response is sent.

After reboot, the updated PY32 is at address 15. The ESP32 sends `CMD_SET_ADDRESS`
(command 1) to 15 with the updated node's previous address and waits for that
node's `CMD_NODE_CONFIG`. This recovery assumes the other nodes keep their assigned
addresses. If multiple nodes reset, run a fresh enumeration with all PY32s reset.

The bootloader reserves separate 8 KiB application and staging regions.
`CMD_FIRMWARE_APPLY` checks the staged vectors and CRC, writes a pending record,
and acknowledges before resetting. At reset the bootloader rechecks staging,
copies the image into the application region, verifies the application CRC,
then erases the pending record. A reset during copy leaves the pending record
available for the next boot to retry. A persistently invalid staged image or
failed copy requires SWD service. The firmware CRC detects corruption.

## Debug (`CMD_DEBUG`)

Command 13 carries up to 130 bytes of debug text with destination 0. Send
plain ASCII or UTF-8 bytes without a terminating NUL. The frame length gives
the text length. Forwarders apply the usual TTL and CRC rules. The ESP32
writes the text to USB serial with a `Node <address> debug:` prefix. `CMD_DEBUG`
has no reply. For example, The PY32 application emits an acknowledgement text such as
`LED 1 RGB: 25, 0, 0` after applying a `CMD_SET_LED_COLOR` command.


| Byte | Value | Usage |
| :---: | :---: | --- |
| 0 | NN | Packet Length |
| 1 | N0 | Source Address=N Destination=0 |
| 2 | FD | TTL=15 Command=13 `CMD_DEBUG` |
| 3 | ZZ | First byte of data |
| ... | ZZ | Remaining bytes of data |
| N+3 | XX | CRC-HI Byte |
| N+4 | XX | CRC-LO Byte |