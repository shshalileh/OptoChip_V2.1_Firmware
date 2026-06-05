# NFC OTA Bootloader

This bootloader is built separately from the application:

```powershell
.\Bootloader\build_bootloader.ps1
```

It links into `0x08000000-0x08002FFF` and reuses the project ST/BSP NFC sources:

- `Core/Src/custom_bus.c`
- `NFC7/custom_nfc07a1_nfctag.c`
- `NFC7/custom_nfc07a1.c`
- `Drivers/BSP/Components/st25dvxxkc/*`

Do not change I2C registration, timing, pins, or bus setup for OTA. Mailbox access is through the ST wrapper APIs:

- `CUSTOM_NFCTAG_WriteMBMode`
- `CUSTOM_NFCTAG_ReadMBMode`
- `CUSTOM_NFCTAG_ReadI2CSecuritySession_Dyn`
- `CUSTOM_NFCTAG_PresentI2CPassword`
- `CUSTOM_NFCTAG_SetMBEN_Dyn`
- `CUSTOM_NFCTAG_ReadMBCtrl_Dyn`
- `CUSTOM_NFCTAG_ReadMBLength_Dyn`
- `CUSTOM_NFCTAG_ReadMailboxData`
- `CUSTOM_NFCTAG_WriteMailboxData`
- `CUSTOM_NFCTAG_ResetMBEN_Dyn`

The application image must be linked at `0x08003000`; this repo's main linker script is configured for that app slot.

## OTA frame

All multi-byte fields are little-endian. The fixed 24-byte frame header is:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | magic, `0x4F544146` (`OTAF`) |
| 4 | 1 | command |
| 5 | 1 | protocol version, `1` |
| 6 | 2 | sequence |
| 8 | 4 | offset; for `BEGIN`, image size |
| 12 | 2 | payload length |
| 14 | 2 | flags |
| 16 | 4 | arg0; for `BEGIN`, unlock token `0x4E46434F` |
| 20 | 4 | CRC32 over bytes `0..19` plus payload |

Payload starts at offset 24. `HELLO` and `STATUS` return bootloader version, flash page size, app max size, max OTA data payload, OTA state, and OTA error as six `uint32_t` values.
