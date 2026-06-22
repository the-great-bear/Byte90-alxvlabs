# Firmware Backups

Full flash dumps from physical Byte90 devices, readable with `esptool`.

## Real Baseline

**`byte90_esp32s3_8mb_2026-06-12.bin`** — this is the canonical Real Baseline.

- Full backup of the original firmware from the maker
- All new firmware must be built on top of this
- When in doubt, restore from this file

## Files

| File | Date | Notes |
|------|------|-------|
| `byte90_esp32s3_8mb_2026-06-12.bin` | 2026-06-12 | **REAL BASELINE** — original firmware from the maker |
| `byte90_esp32s3_8mb_2026-06-22.bin` | 2026-06-22 | Backup taken after restore to Real Baseline |

## How to Restore to Real Baseline

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0x0 byte90_esp32s3_8mb_2026-06-12.bin
```

Replace `/dev/ttyACM0` with your port (`COM3` on Windows).
