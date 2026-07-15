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

The register addresses below are zero-based Modbus holding-register offsets. Some HMI software displays the same offset as 40001 + offset.

## HMI writes to AFMS

| Offset | 4xxxx display | Name | Format |
|---:|---:|---|---|
| 0 | 40001 | Loss code | UINT16, 1-16 |
| 1 | 40002 | Cycle time | UINT16 seconds |
| 2 | 40003 | Target quantity | UINT16 |
| 3 | 40004 | Operator ID | UINT16 |
| 4 | 40005 | Shift command | UINT16, 1-3 |
| 5 | 40006 | HMI heartbeat | UINT16 |
| 6 | 40007 | Part number low word | UINT16 |
| 7 | 40008 | Part number high word | UINT16 |
| 8-15 | 40009-40016 | Part name | 16 ASCII characters, two per register |

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
| 30-31 | 40031-40032 | Downtime seconds | UINT32, low word first |
| 32 | 40033 | Availability | permille, 1000 = 100.0% |
| 33 | 40034 | Performance | permille, 1000 = 100.0% |
| 34 | 40035 | Quality | permille, 1000 = 100.0% |
| 35 | 40036 | OEE | permille, 1000 = 100.0% |
| 36-37 | 40037-40038 | Target quantity | UINT32, low word first |
| 38 | 40039 | Active shift ID | UINT16 |
| 39-40 | 40040-40041 | Operator ID | UINT32, low word first |
| 41-42 | 40042-40043 | Part number | UINT32, low word first |
| 43-44 | 40044-40045 | Shift production | UINT32, low word first |
| 45-46 | 40046-40047 | Shift reject | UINT32, low word first |
| 47-48 | 40048-40049 | Shift good | UINT32, low word first |
| 49-50 | 40050-40051 | Target remaining | UINT32, low word first |
| 51 | 40052 | Wi-Fi connected | 0/1 |
| 52 | 40053 | Google connection confirmed | 0/1; becomes 1 after first successful upload |
| 53 | 40054 | Telegram bot connected | 0/1 |
| 54 | 40055 | Offline queue count | UINT16 |
| 55-56 | 40056-40057 | Google upload success count | UINT32, low word first |
| 57-58 | 40058-40059 | Google upload failure count | UINT32, low word first |
| 59-60 | 40060-40061 | Telegram success count | UINT32, low word first |
| 61-62 | 40062-40063 | Telegram failure count | UINT32, low word first |
| 63 | 40064 | Modbus CRC/error count | UINT16 low word |

## Machine state values

| Value | State |
|---:|---|
| 0 | Ready |
| 1 | Running |
| 2 | Idle |
| 3 | Loss required / alarm interlock |
