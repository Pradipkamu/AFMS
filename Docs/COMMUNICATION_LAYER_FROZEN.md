# AFMS Communication Layer — Frozen Baseline

Status: **PROVEN AND FROZEN**

This document defines the approved AFMS HMI communication baseline. Future changes must preserve this interface unless a new major hardware revision is explicitly approved and tested.

## Physical interface

- Protocol: Modbus RTU over RS-485
- ESP8266 role: Modbus RTU slave
- HMI role: Modbus RTU master
- Transceiver: MAX485-compatible RS-485 transceiver

## Fixed wiring

| MAX485 signal | ESP8266 connection |
|---|---|
| RO | RX0 / GPIO3 |
| DI | TX0 / GPIO1 |
| DE and /RE tied together | D6 / GPIO12 |
| A | HMI RS-485 A |
| B | HMI RS-485 B |
| GND | Common ground |

## Fixed UART allocation

- UART0 TX/RX is reserved exclusively for Modbus RTU.
- UART0 must not carry debug text during normal operation.
- UART1 TX-only on GPIO2 / D4 is reserved for optional diagnostics.
- SoftwareSerial must not be reintroduced for AFMS HMI communication.

## Fixed Modbus settings

- Slave ID: 1
- Baud rate: 9600
- Data bits: 8
- Parity: None
- Stop bits: 1
- Frame format: 8-N-1

## Compatibility rules

1. Existing holding-register offsets must not be renumbered.
2. Existing data widths and word order must remain unchanged.
3. Existing HMI commands must keep the same meaning.
4. New registers must use unused offsets only.
5. UART0 debug printing is prohibited because it corrupts Modbus frames.
6. RS-485 direction control must remain on GPIO12 unless a new hardware revision is approved.
7. Any communication-layer change requires a standalone wiring test and full AFMS regression test.

## Proven tests

The following have been confirmed working:

- Hardware UART TX/RX communication
- MAX485 wiring and DE/RE direction control
- Modbus RTU request and response handling
- Coil read/write test
- Holding-register read test
- AFMS register read/write integration
- ESP heartbeat register
- HMI-to-AFMS command writes
- AFMS-to-HMI production and status reads

## Authoritative register map

See:

`Docs/HMI_Modbus_Register_Map.md`

This frozen baseline applies from Commit 0021 onward.
