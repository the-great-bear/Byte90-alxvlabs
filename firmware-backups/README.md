# Firmware Backups

Full flash dumps from physical Byte90 devices, readable with `esptool.py`.

## Files

| File | Date | Chip | Flash Size | Notes |
|------|------|------|------------|-------|
| `byte90_esp32s3_8mb_2026-06-12.bin` | 2026-06-12 | ESP32-S3 (QFN56 rev 0.2) | 8 MB | Pre-update backup |

## How to Restore

**Requirements:** Python + esptool (`pip install esptool`)

1. Put the Byte90 into bootloader mode (hold BOOT, press RESET, release BOOT)
2. Run:

```bash
esptool.py --port COM3 --baud 460800 --no-stub write-flash 0x0 byte90_esp32s3_8mb_2026-06-12.bin
```

Replace `COM3` with your actual port. The full 8 MB image is written back to address `0x0`, restoring the device exactly as it was when the backup was taken.
