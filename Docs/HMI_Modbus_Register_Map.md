# AFMS HMI Modbus RTU Register Map

## Communication

- Protocol: Modbus RTU slave
- Slave ID: 1
- Baud: 9600
- Format: 8 data bits, no parity, 1 stop bit
- MAX485 RO -> D7 / GPIO13
- MAX485 DI -> D8 / GPIO15
- MAX485 DE and /RE -> D6 / GPIO12

The register addresses below are zero-based Modbus holding-register offsets. Some HMI software displays the same offset as 40001 + offset.

## HMI writes to AFMS

| Offset | 4xxxx display | Name | Format |
|---:|---:|---|---|
| 0 | 40001 | Loss code | UINT16, 1-16 |
| 1 | 40002 | Cycle time | UINT16 seconds |
| 2 | 40003 | Target quantity | UINT16 |
| 3 | 40004 | Operator ID | UINT16 |
| 4 | 40005 | Shift command | UINT16 |
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
