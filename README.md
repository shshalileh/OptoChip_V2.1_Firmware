# OptoChip V2.1 Firmware

OptoChip V2.1 is a wireless, implantable optoelectronic neurostimulator for NFC-configured optical stimulation. This repository contains the STM32 firmware that controls the implantable OptoChip platform, including dynamic NFC command exchange, scheduled stimulation, charger configuration, LED-driver control, and low-power operation.

The current checked-in board configuration targets the STM32U031K8Ux.

The custom application code is written to be portable across the STM32U family. Board-specific files such as the startup file, linker script, Cube/HAL peripheral initialization, pin mapping, and clock configuration must be regenerated or adapted when moving to a different STM32U device or board.

## Features

- ST25DVxxKC dynamic NFC tag command interface using NDEF text records.
- Hex-text command protocol for charger, LED, RTC, and scheduler control.
- RTC-backed stimulation scheduler with 8 stored session slots.
- Two-channel LP5817 LED output support for the current OptoChip V2.1 board.
- BQ25155 charger configuration and battery-voltage readback.
- Stop2 low-power flow with NFC wake/processing support.
- Runtime recovery path for malformed or interrupted NFC NDEF writes.

## Hardware Configuration

- MCU: STM32U031K8Ux, STM32U0 family.
- NFC: ST25DVxxKC dynamic NFC tag through ST X-CUBE-NFC7 support.
- NFC bus: I2C3.
- NFC power control: `NFC_VCC` on PB6.
- NFC GPO interrupt: PB7 / EXTI7.
- LED driver: LP5817, configured for two LED channels.
- Charger / PMIC: BQ25155.
- Timing: RTC with LSE, plus LPTIM1 for LED stimulation timing.

## Repository Layout

- `Core/`: STM32CubeIDE-generated application entry points, interrupts, startup integration, and board initialization.
- `Custom_lib/`: custom OptoChip application logic, including NFC command handling, RTC helpers, scheduler, charger driver, LED driver, and ST25DV wrapper logic.
- `Drivers/`: ST HAL, CMSIS, and BSP components.
- `Middlewares/ST/`: ST NFC middleware.
- `NFC7/`: generated/customized X-CUBE-NFC7 support files.
- `COMMAND_PROTOCOL.md`: NFC command and response reference.

The CubeMX `.ioc` file is intentionally not tracked in Git. It remains a local development file so board-specific Cube configuration can be kept on the developer machine without publishing it on GitHub.

## Building

1. Open the project in STM32CubeIDE.
2. Select the Debug configuration for `Firmware_NFC_Device`.
3. Build the project.
4. Flash using an ST-LINK-compatible debug probe.

The project was developed as a CubeIDE firmware tree. If retargeting to another STM32U MCU, regenerate the MCU support files for the new target, then reconnect the custom modules under `Custom_lib/` to the new board initialization, I2C instance, RTC, LPTIM, GPIO, and linker layout.

## NFC Command Protocol

Commands are sent as hex text in an NDEF text record and responses are written back as hex text. See [COMMAND_PROTOCOL.md](COMMAND_PROTOCOL.md) for command IDs, payloads, limits, examples, and response formats.

## Extra notes

- The current hardware configuration supports LED IDs `00` and `01`.
- Scheduler slots are `00` through `07`.
- Timing fields in NFC commands use the same BCD month/day/hour/minute/second style used by the firmware RTC helper.
- This firmware is intended for research and embedded development use. Validate electrical limits, optical output, and power behavior on the target hardware before use.
