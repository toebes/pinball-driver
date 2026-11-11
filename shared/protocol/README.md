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

A firmware page payload consists of a two-byte page index relative to the application region and 128 bytes of page data. The coordinator sends one page at a time and waits for the target to acknowledge that page after writing and verifying it. Image size and whole-image integrity are handled at the update level. Command values, page-index byte order, and update session details remain to be specified.

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

## Set LED color

Command 4 carries four bytes in order: LED index (0 through 5), red, green,
and blue. Index 0 is LED1. The length byte is 8 and the total frame is
9 bytes. The coordinator sends to the assigned node address with TTL 5 for the bench test. Node 1 applies
the RGB values after validating the CRC and leaves that LED under remote
control until reset. This command has no reply in the bench implementation.
