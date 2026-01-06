# Siemens S7 PLC RGB LED Demo

This repository contains an Arduino-based demo that shows how a modern embedded device can communicate directly with a Siemens S7 PLC using the native S7 (PUT/GET) protocol.

In this demo, an ESP32-P4 development kit connects to a Siemens PLC over Ethernet, reads a PLC data block, and controls an addressable RGB LED strip based on the state of PLC-connected switches and push buttons.



# Demo Overview

## PLC Side

- Siemens S7-1200 PLC (it should S7-1500 compatible)
- x2 maintained switches
- x2 momentary push buttons (one normally closed)

### Data Block Mapping

The PLC exposes a single Data Block (DB1) with the following bit layout:

| Address    | Description                             |
| ---------- | --------------------------------------- |
| DB1.DBX0.0 | Switch 1. LED enable on/off             |
| DB1.DBX0.1 | Switch 2. Chase effect enable           |
| DB1.DBX0.2 | Push button 1. Green color selection    |
| DB1.DBX0.3 | Push button 2 (NC). Red color selection |

The PLC logic continuously updates this Data Block based on physical input state.



## Embedded Side

- ESP32-P4 Development Kit (with native Ethernet)
- Addressable RGB LED strip (WS2812)

- Arduino framework

### S7 Communication Library

This demo uses the [Settimino](https://settimino.sourceforge.net/) open-source library to implement an S7 client on the ESP32.

### Firmware Behavior

The Arduino sketch performs the following steps:

1. Initialize Ethernet interface
2. Connect to the PLC over TCP/IP
3. Establish an S7 connection
4. Read the configured Data Block (DB1) cyclically
5. Decode switch/button bits
6. Update RGB LED strip:
   - Enable/disable LEDs
   - Select color
   - Enable chase animation



# Related Blog Post

Additional information about this demo is available in this blog post: **Bridging Embedded Hardware and Siemens PLCs with an ESP32-P4**
