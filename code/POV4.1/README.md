# POV Staff BLE Control

This project now supports BLE control for image selection and playback on the
Adafruit QT Py ESP32-S3 POV staff, plus improved behavior when the IMU is
missing during testing.

## Highlights
- BLE control in normal run mode (no BLE in uploader mode).
- Pairing + bonding required for writes (Just Works).
- BLE device name advertises as `POVStaff`.
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

## Logging in each boot mode
```
Reconnecting to /dev/cu.usbmodem101 .    Connected!
Firmware version: 4.2-ota
Voltage: 4.88
IMU Enabled
AP IP address: 192.168.4.1
mDNS responder started
Server started
Filemanager started
```

```
Reconnecting to /dev/cu.usbmodem101 .    Connected!
Firmware version: 4.2-ota
Voltage: 4.89
IMU Enabled
imagelist added
Connecting WiFi for OTA.............
OTA WiFi connected, IP: 192.168.1.133
OTA WiFi RSSI: -72
mDNS responder started: POVStaff.local
mDNS service added: arduino tcp 3232
Starting ArduinoOTA...
OTA ready as: POVStaff
OTA port: 3232
BLE advertising as: POVStaff
```

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
 - `src/POV4_1.ino` uses placeholder defaults, so `local.ini` must define the real credentials.

Upload over OTA:
```
pio run -e adafruit_qtpy_esp32s3_n4r2_ota -t upload
```

Helper script (uses mDNS by default, or pass an IP):
```
scripts/ota-upload.sh
scripts/ota-upload.sh --host 192.168.1.117
scripts/ota-upload.sh --from-serial --serial-port /dev/cu.usbmodem101
scripts/ota-upload.sh --from-serial --no-install
```

The serial auto-detect flow installs `pyserial` on first run if needed. If a
virtualenv is active, it installs into that environment; otherwise it uses a
user install.

The device logs its IP and hostname on boot (e.g., `POVStaff`). Use the
printed IP if mDNS is unavailable.

## Phone uploader (AP mode)
When the staff is held horizontal on boot, it starts an access point and serves
the phone-friendly uploader UI.

Steps:
1. Hold the staff horizontal and power on.
2. Connect your phone to the `POVSTAFFXXXX` WiFi network.
3. Open `http://192.168.4.1/` and use the loader to upload, scale, and order images.

The uploader converts images to BMP in the browser and the device scales them to
fit the staff before writing `/imagelist.txt`.

## Notes
- BLE is only started in normal mode (not in uploader mode).
- Image list is parsed from `imagelist.txt` for index/name selection.
- Third-party library warnings in `.pio/libdeps` are unchanged.
