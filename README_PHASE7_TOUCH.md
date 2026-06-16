# Phase 7: Waveshare ESP32-S3 Touch ePaper 1.54

This build adds a separate Touch firmware target for `ESP32-S3-Touch-ePaper-1.54` while keeping the existing non-touch target unchanged.

## Build targets

- Non-touch: `platformio run -e esp32-s3-epaper154`
- Touch: `platformio run -e esp32-s3-touch-epaper154`
- Upload Touch example: `platformio run -e esp32-s3-touch-epaper154 -t upload --upload-port COM12`

Generated binaries are copied to:

- `outputs/furble-esp32-s3-epaper154-phase7/`
- `outputs/furble-esp32-s3-touch-epaper154-phase7/`

## Touch hardware mapping

The Touch target follows Waveshare's FT6336 example for this board:

| Function | GPIO |
| --- | --- |
| EPD DC | GPIO10 |
| EPD CS | GPIO11 |
| EPD SCK | GPIO12 |
| EPD MOSI | GPIO13 |
| EPD RST | GPIO9 |
| EPD BUSY | GPIO8 |
| EPD PWR | GPIO6 |
| FT6336 INT | GPIO21 |
| FT6336 RST | GPIO7 |
| FT6336 SDA | GPIO47 |
| FT6336 SCL | GPIO48 |
| FT6336 I2C address | `0x38` |

## Touch interaction design

Physical buttons still work as a fallback. Touch is now the primary path on Touch hardware:

- Home/menu
  - Tap a menu row to open it directly.
  - `Remote` and `Disconnect` remain conditional on an active camera connection.
- Scan
  - Tap the title/header area to cancel scanning and return to menu.
- Cameras
  - Tap a camera row to connect.
  - Tap title/header to return to menu.
- Remote
  - Tap `Shutter` for shutter.
  - Tap `Focus` / `2s Shot` for the secondary camera action.
  - Tap `Menu` to return to menu.
  - Tap `Disconnect` to show confirmation.
- GPS
  - Tap `GPS ON/OFF` status row to toggle.
  - When GPS is off, `Tap to turn on` also enables GPS.
  - Tap title/header to return to menu.
- Confirm dialogs
  - Tap `Yes` or `No`.
  - Tap title/header as cancel/no.

## Notes

- Touch coordinates are read from FT6336 registers `0x02` and `0x03..0x06`, clamped to the 200x200 display.
- Events are emitted on first touch-down as debounced taps, which makes the e-paper UI feel responsive and reduces accidental repeats.

## Touch troubleshooting

If the screen refreshes but touch does nothing:

1. Make sure the flashed environment is the Touch build, not the non-touch build:
   `platformio run -e esp32-s3-touch-epaper154 -t upload --upload-port COM12`
2. Open serial monitor after boot and check the version line contains `epaper154-touch-phase7`.
3. On boot, the firmware should log either:
   - `FT6336 touch enabled ...`, or
   - `FT6336 probe failed ...; touch will keep polling`.
4. Each detected tap logs `touch tap x=<x> y=<y>`. If this log appears but UI does not react, the touch coordinate orientation needs calibration.
5. If no FT6336/touch log appears, the non-touch build is probably flashed.


### 2026-06-15 I2C transaction failed fix

If serial output repeats `i2c.master: I2C transaction failed` while idle, use this or a newer build. Touch reads are now gated by FT6336 INT falling edge instead of 50Hz polling, matching the Waveshare `12_FT6336_Test` flow and avoiding idle I2C transactions.

### 2026-06-15 power-order fix

Espressif I2C docs state that `i2c_master_transmit_receive` fails on NACK/timeout and recommend `i2c_master_probe`/checking pull-ups and bus state for timeout. On this board the FT6336 is on the e-paper/touch power rail, so the firmware must call `epaperPowerOn()` before initializing or reading FT6336. The Touch build now powers the e-paper/touch rail before `initTouch()`.
