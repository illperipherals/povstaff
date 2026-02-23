# POV Staff BLE Control

This project now supports BLE control for image selection and playback on the
Adafruit QT Py ESP32-S3 POV staff, plus improved behavior when the IMU is
missing during testing.

## Highlights
- BLE control in normal run mode (no BLE in uploader mode).
- Pairing + bonding required for writes (Just Works).
- BLE device name advertises as `POVStaff-<FW_VERSION>`.
- Image lock overrides IMU-driven changes until unlocked.
- IMU-less test mode runs with a fixed speed and no I2C spam.
- BLE usability helpers: `help`, `list`, aliases, and test `speed`.
- PlatformIO layout: main sketch in `src/POV4_1.ino`, config in `platformio.ini`, OTA partitions in `partitions.csv`.

## BLE service
Nordic UART Service (NUS):
- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (write): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (notify/read): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

Named control characteristics (same service):
- Current Image (read/notify): `6E400004-B5A3-F393-E0A9-E50E24DCCA9E`
- Next Image (write): `6E400005-B5A3-F393-E0A9-E50E24DCCA9E`
- Previous Image (write): `6E400006-B5A3-F393-E0A9-E50E24DCCA9E`
- Image Lock (read/write/notify): `6E400007-B5A3-F393-E0A9-E50E24DCCA9E`

Pair/bond once before writing. TX notifications must be enabled to see
responses; TX reads return the last response.

## BLE commands (write UTF-8 text to RX)
- `help` - show command summary
- `list` - list images with indices
- `status` - report paused/lock/index/name
- `next`, `prev`
- `pause`, `resume` (aliases: `stop`, `run`)
- `lock`, `unlock`
- `index:<n>` - 0-based index
- `name:<filename>` - exact match to imagelist entry
- `speed:<deg_per_sec>` - IMU-less testing only
- Aliases: `n`, `p`, `s`, `l`, `u`

## Behavioral changes
- **Image lock**: when locked, IMU-driven auto changes are ignored. BLE commands
  can still change images.
- **IMU missing**: device still enters run mode and uses a fixed rotation speed
  (default 360 deg/s). IMU reads are skipped to avoid I2C errors.
- **BLE logging**: serial output shows advertising, connect/disconnect, command
  strings, and notification state.

## PlatformIO
Build:
```
pio run -e adafruit_qtpy_esp32s3_n4r2
```

Upload:
```
pio run -e adafruit_qtpy_esp32s3_n4r2 -t upload
```

Serial monitor:
```
pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```

## OTA (push)
OTA uses WiFi STA in normal mode and ArduinoOTA. Configure these before use:
- Set `OTA_WIFI_SSID` / `OTA_WIFI_PASSWORD` in `local.ini` (see `platformio.ini`).
- Update the OTA password in `platformio.ini` (`OTA_PASSWORD` and `--auth`), or override in `local.ini`.

Upload over OTA:
```
pio run -e adafruit_qtpy_esp32s3_n4r2_ota -t upload
```

The device logs its IP and hostname on boot (e.g., `POVStaff-4.1`). Use the
printed IP if mDNS is unavailable.

## Notes
- BLE is only started in normal mode (not in uploader mode).
- Image list is parsed from `imagelist.txt` for index/name selection.
- Third-party library warnings in `.pio/libdeps` are unchanged.
