# AFMS HMI Modbus RTU Register Map

## Communication

- Protocol: Modbus RTU slave
- Slave ID: 1
- Baud: 9600
- Format: 8 data bits, no parity, 1 stop bit
- MAX485 RO -> RX0 / GPIO3
- MAX485 DI -> TX0 / GPIO1
- MAX485 DE and /RE -> D6 / GPIO12
- UART0 is reserved for Modbus RTU.
- Diagnostic logging uses UART1 TX-only on D4 / GPIO2.

Holding-register addresses are zero-based internally. DOPSoft normally displays offset 0 as 40001. Coil addresses are also zero-based internally; DOPSoft normally displays coil offset 0 as 00001.

## Preferred loss-command coils

Use these coils for the 16 DOPSoft loss-selection buttons. Configure each button as **Set ON / Bit Set**. AFMS consumes and resets the coil automatically, so a Momentary Write object is not required.

| Loss code | Coil offset | DOPSoft display | Purpose |
|---:|---:|---:|---|
| 1 | 0 | 00001 | Planned shutdown |
| 2 | 1 | 00002 | Loss 2 |
| 3 | 2 | 00003 | Loss 3 |
| 4 | 3 | 00004 | Loss 4 |
| 5 | 4 | 00005 | Loss 5 |
| 6 | 5 | 00006 | Loss 6 |
| 7 | 6 | 00007 | Loss 7 |
| 8 | 7 | 00008 | Loss 8 |
| 9 | 8 | 00009 | Loss 9 |
| 10 | 9 | 00010 | Loss 10 |
| 11 | 10 | 00011 | Loss 11 |
| 12 | 11 | 00012 | Loss 12 |
| 13 | 12 | 00013 | Loss 13 |
| 14 | 13 | 00014 | Loss 14 |
| 15 | 14 | 00015 | Loss 15 |
| 16 | 15 | 00016 | Loss 16 |

AFMS supports Modbus function 01 (Read Coils), function 05 (Write Single Coil), and function 15 (Write Multiple Coils). If more than one loss coil is ON in the same processing cycle, AFMS accepts only the lowest-numbered asserted loss and clears all asserted loss coils.

A loss command is accepted only while the machine is in Loss Required state and the alarm is active. One loss is captured per idle event. After acceptance, the alarm remains cleared until a new production pulse starts another cycle and a later idle event occurs.

## HMI writes to AFMS holding registers

| Offset | 4xxxx display | Name | Format |
|---:|---:|---|---|
| 0 | 40001 | Legacy loss code command | UINT16, 1-16; compatibility only |
| 1 | 40002 | Cycle time | UINT16 seconds |
| 2 | 40003 | Target quantity | UINT16 |
| 3 | 40004 | Operator ID | UINT16 |
| 4 | 40005 | Shift command | UINT16, 1-3 |
| 5 | 40006 | HMI heartbeat | UINT16; HMI should increment periodically |
| 6 | 40007 | Part number low word | UINT16 |
| 7 | 40008 | Part number high word | UINT16 |
| 8-15 | 40009-40016 | Part name | 16 ASCII characters, two per register |
| 94 | 40095 | Loss code alias | UINT16, 1-16; compatibility only |

Offsets 0-15 and offset 94 are writable. All other holding registers are read-only. For DOPSoft loss buttons, use coils 00001-00016 rather than 40001 or 40095.

## HMI reads from AFMS

| Offset | 4xxxx display | Name | Format |
|---:|---:|---|---|
| 16 | 40017 | Machine state | UINT16 |
| 17-18 | 40018-40019 | Production count | UINT32, low word first |
| 19-20 | 40020-40021 | Reject count | UINT32, low word first |
| 21-22 | 40022-40023 | Good count | UINT32, low word first |
| 23-24 | 40024-40025 | Idle seconds | UINT32, low word first |
| 25 | 40026 | Alarm active | 0/1 |
| 26 | 40027 | Modbus communication active | 0/1 |
| 27 | 40028 | ESP heartbeat | UINT16 incrementing |
| 28-29 | 40029-40030 | Run seconds | UINT32, low word first |
| 30-31 | 40031-40032 | Unplanned downtime seconds | UINT32, low word first |
| 32 | 40033 | Availability | permille, 1000 = 100.0% |
| 33 | 40034 | Performance | permille, 1000 = 100.0% |
| 34 | 40035 | Quality | permille, 1000 = 100.0% |
| 35 | 40036 | OEE | permille, 1000 = 100.0% |
| 36-37 | 40037-40038 | Original target quantity | UINT32, low word first |
| 38 | 40039 | Active shift ID | UINT16 |
| 39-40 | 40040-40041 | Operator ID | UINT32, low word first |
| 41-42 | 40042-40043 | Part number | UINT32, low word first |
| 43-44 | 40044-40045 | Shift production | UINT32, low word first |
| 45-46 | 40046-40047 | Shift reject | UINT32, low word first |
| 47-48 | 40048-40049 | Shift good | UINT32, low word first |
| 49-50 | 40050-40051 | Original target remaining | UINT32, low word first |
| 51 | 40052 | Wi-Fi connected | 0/1 |
| 52 | 40053 | Google connection confirmed | 0/1; becomes 1 after first successful upload |
| 53 | 40054 | Telegram bot connected | 0/1 |
| 54 | 40055 | Offline queue count | UINT16 |
| 55-56 | 40056-40057 | Google upload success count | UINT32, low word first |
| 57-58 | 40058-40059 | Google upload failure count | UINT32, low word first |
| 59-60 | 40060-40061 | Telegram success count | UINT32, low word first |
| 61-62 | 40062-40063 | Telegram failure count | UINT32, low word first |
| 63 | 40064 | Modbus CRC/error count | UINT16 low word |
| 64-65 | 40065-40066 | Modbus request count | UINT32, low word first |
| 66 | 40067 | Wi-Fi RSSI | Signed INT16 in dBm |
| 67 | 40068 | Time synchronized | 0/1 |
| 68 | 40069 | Year | UINT16, e.g. 2026 |
| 69 | 40070 | Month | UINT16, 1-12 |
| 70 | 40071 | Day | UINT16, 1-31 |
| 71 | 40072 | Hour | UINT16, 0-23 |
| 72 | 40073 | Minute | UINT16, 0-59 |
| 73 | 40074 | Second | UINT16, 0-59 |
| 74 | 40075 | Current cycle time | UINT16 seconds |
| 75 | 40076 | HMI heartbeat echo | Last value received at 40006 |
| 76 | 40077 | HMI heartbeat age | UINT16 seconds since 40006 changed |
| 77 | 40078 | Last Modbus request age | UINT16 milliseconds; 65535 means none received |
| 80-81 | 40081-40082 | Scheduled shift elapsed time | UINT32 seconds, low word first |
| 82-83 | 40083-40084 | Planned shutdown time | UINT32 seconds, low word first; Loss 1 only |
| 84-85 | 40085-40086 | Planned production time | UINT32 seconds, low word first; scheduled elapsed minus Loss 1 |
| 86 | 40087 | Last accepted loss code | UINT16, 1-16 |
| 87-88 | 40088-40089 | Last loss duration | UINT32 seconds, low word first |
| 89 | 40090 | Loss command result | 0 none, 1 accepted, 2 rejected |
| 90-91 | 40091-40092 | Adjusted target excluding planned shutdown | UINT32, low word first |
| 92-93 | 40093-40094 | Adjusted target remaining | UINT32, low word first |

Registers 40079-40080 are reserved for future use.

## Loss-capture verification sequence

1. Generate at least one valid production pulse.
2. Wait for cycle time to expire. Register 40017 becomes 2 (Idle).
3. Wait for `loss_alarm_delay_seconds`. Register 40017 becomes 3 and 40026 becomes 1.
4. Press one loss button using coil 00001-00016.
5. Register 40090 becomes 1 when accepted. Register 40087 shows the selected code.
6. Registers 40088-40089 show the captured duration in seconds.
7. Register 40026 returns to 0 and 40017 returns to 2 while the machine remains stopped.
8. Additional loss-button presses during the same idle event are rejected and do not create duplicate loss records.
9. The next valid production pulse starts a new cycle and production counting resumes.

During Loss Required, production pulses are blocked and do not increase registers 40018-40019.

## Adjusted target rule

The original target remains unchanged at 40037-40038. AFMS calculates the adjusted target using the full configured duration of the active shift:

`Adjusted target = Original target x (Configured shift duration - Loss 1 duration) / Configured shift duration`

The result is rounded down to a whole component. Losses 2-16 do not reduce the target.

`Adjusted target remaining = max(Adjusted target - Shift production, 0)`

Example: for an 8-hour shift, original target 480, and one hour recorded as Loss 1, the adjusted target is 420.

## OEE time relationship

- Scheduled shift elapsed = current time minus configured active-shift start time.
- Planned shutdown = accumulated duration recorded with Loss code 1.
- Planned production time = scheduled shift elapsed minus planned shutdown.
- Unplanned downtime = Losses 2-16 and other OEE downtime.
- Run time = planned production time minus unplanned downtime.
- Availability = run time divided by planned production time.

## Machine state values

| Value | State |
|---:|---|
| 0 | Ready |
| 1 | Running |
| 2 | Idle |
| 3 | Loss required / alarm interlock |

## Recommended HMI heartbeat

Increment holding register 40006 once every second. Confirm that 40076 follows it and that 40077 remains below 3 seconds. This provides independent supervision of both communication directions.
