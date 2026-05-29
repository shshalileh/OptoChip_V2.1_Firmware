# OptoChip V2.1 NFC Command Protocol

Commands are written to the ST25DVxxKC tag as hex text in an NDEF text record. The firmware parses the text after an RF write event, executes the command, and writes a hex-text response back to the tag.

Separators are allowed for readability. Spaces, hyphens, colons, carriage returns, line feeds, and tabs are ignored by the parser.

Example equivalent command text:

```text
32
```

```text
30 00 00 08 04 11 00 00 14 14 0A 00 0F 01
```

## General Format

```text
CMD [PAYLOAD_BYTES...]
```

- `CMD`: one byte command code.
- Payload bytes are command-specific.
- Multi-byte integer fields are big-endian.
- Date/time fields are BCD: `MM DD HH MIN SS`.

## Hardware And Scheduler Limits

- Scheduler slots: `00` to `07`.
- LED IDs: `00` for LED0, `01` for LED1.
- Maximum LED current: 30 mA.
- Stimulation frequency: 1 Hz to 100 Hz.
- Duty cycle: 1 percent to 99 percent.
- Duration units: `00` seconds, `01` minutes.
- Maximum duration after conversion to seconds: 86400 seconds.

## Commands

| Command | Payload | Description |
| --- | --- | --- |
| `01` | `CURRENT_MA` | Set BQ25155 fast-charge current. |
| `02` | `LED CURRENT_MA` | Set LP5817 current for one LED channel. |
| `03` | `LED FREQ_HZ DUTY_PCT` | Validate pulse settings for one LED channel. |
| `04` | `LED CURRENT_MA FREQ_HZ DUTY_PCT DURATION_SECONDS` | Schedule an immediate LED session using the current RTC time. |
| `10` | none | Read battery voltage. |
| `11` | none | Read LED0/LED1 duty and enable state. |
| `20` | none | Read MCU RTC time. |
| `21` | `MM DD HH MIN SS` | Set MCU RTC time. |
| `30` | `SLOT LED MM DD HH MIN SS CURRENT_MA FREQ_HZ DUTY_PCT DUR_H DUR_L DUR_UNIT` | Set a scheduled LED session. |
| `31` | `SLOT` | Clear one scheduler slot. |
| `32` | none | Read scheduler status. |
| `33` | `LED` | Stop an active LED stimulation. |
| `34` | none | Clear all scheduler slots. |

## Scheduled LED Session

```text
30 SLOT LED MM DD HH MIN SS CURRENT_MA FREQ_HZ DUTY_PCT DUR_H DUR_L DUR_UNIT
```

- `SLOT`: scheduler slot, `00` to `07`.
- `LED`: `00` or `01`.
- `MM DD HH MIN SS`: BCD scheduled start time.
- `CURRENT_MA`: LED current in mA, up to 30 mA.
- `FREQ_HZ`: stimulation frequency, 1 Hz to 100 Hz.
- `DUTY_PCT`: duty cycle, 1 percent to 99 percent.
- `DUR_H DUR_L`: 16-bit duration value, big-endian.
- `DUR_UNIT`: `00` for seconds, `01` for minutes.

Example: slot 0, LED0, Aug 04 11:00:00, 20 mA, 20 Hz, 10 percent duty, 15 minutes:

```text
30 00 00 08 04 11 00 00 14 14 0A 00 0F 01
```

## Time Commands

Read MCU time:

```text
20
```

Set MCU time to Aug 04 10:30:00:

```text
21 08 04 10 30 00
```

Response:

```text
A0 MM DD HH MIN SS
```

## Scheduler Management

Clear slot 0:

```text
31 00
```

Read schedule status:

```text
32
```

Stop LED0:

```text
33 00
```

Clear all sessions:

```text
34
```

## Responses

| Response | Payload | Description |
| --- | --- | --- |
| `90` | `VBAT_H VBAT_L` | Battery voltage in mV, big-endian. |
| `91` | `LED0_DUTY LED0_EN LED1_DUTY LED1_EN` | LED status readback. |
| `A0` | `MM DD HH MIN SS` | RTC time. |
| `B0` | scheduler payload | Scheduler status. |
| `F0` | none | Acknowledgement. |
| `F1` | `ERROR_CODE` | Error response. |

## Scheduler Status Response

Current extended response with scheduled session times:

```text
B0 ACTIVE_MASK NEXT_VALID NEXT_SLOT TABLE_VALID 01 ENTRY_COUNT [SLOT STATE MM DD HH MIN SS]...
```

- `ACTIVE_MASK`: bit mask of currently active LED channels.
- `NEXT_VALID`: `01` when `NEXT_SLOT` is valid.
- `NEXT_SLOT`: next scheduled slot.
- `TABLE_VALID`: `01` when the persisted scheduler table loaded correctly.
- `01`: scheduler status format version with per-slot scheduled times.
- `ENTRY_COUNT`: number of non-empty slot entries, `00` to `08`.
- Each entry is `SLOT STATE MM DD HH MIN SS`.

Session states:

| State | Meaning |
| --- | --- |
| `00` | Empty |
| `01` | Scheduled |
| `02` | Running |
| `03` | Completed |
| `04` | Cancelled |
| `05` | Error |

Legacy status response used by older firmware:

```text
B0 ACTIVE_MASK NEXT_VALID NEXT_SLOT TABLE_VALID STATE0 STATE1 STATE2 STATE3 STATE4 STATE5 STATE6 STATE7
```

## Error Codes

`F1 ERROR_CODE` means the command was parsed but failed before or during execution.

Common error codes:

- `01`: text parse error, charger command failure, or battery command-specific failure.
- `02`: command validation error or LED current failure.
- `03`: unsupported command or invalid LED pulse target.
- `04`: LED full-setup failure.
- `10`: battery voltage read failure.
- `11`: LED status read failure.
- `20`: RTC read failure.
- `21`: RTC set failure.
- `30`: scheduled LED session setup failure.
- `31`: clear-session failure.
- `32`: scheduler status failure.
- `33`: stop-active-LED failure.
- `34`: clear-all-sessions failure.

## NFC Wake Note

The current OptoChip V2.1 configuration maps the ST25DVxxKC GPO signal to PB7 / EXTI7. NFC writes raise the GPO interrupt, the firmware waits for RF write completion, validates the NDEF contents, executes a valid command, and writes the response text back to the tag.
