# furble ESP32-S3-ePaper-1.54 phase6

Phase6 keeps the verified phase3/phase4 camera and power behavior plus phase5's rounded e-paper UI and SyncGeo sleep logo, then tunes the home menu to match the xiaozhang-note reference screen more closely.

## UI changes

- Home menu header now uses a white background with a left-aligned `menu` label.
- Home menu shows battery capacity at the top-right as a percent value.
- Home menu removes the bottom REC/PWR hint and voltage/percent footer.
- Home menu uses clearer task labels: `Scan Cam` and `Saved Cams`.
- Home menu hides `Remote` and `Disconnect` until a camera is actively connected.
- Home menu adds `GPS`; when a camera is connected the six visible items are `Scan Cam`, `Saved Cams`, `GPS`, `Remote`, `Disconnect`, and `Sleep`.
- All non-sleep screens now use the same white header style with left-aligned title, divider line, and top-right battery percent.
- Menu rows are spaced taller and lower on the 200x200 display to use the freed footer area.
- Non-home screens keep their bottom action hints while sharing the unified top header.
- Remote mode no longer repeats battery info in the content area; the top-right battery percent is the single battery indicator.
- Remote mode shows camera-aware PWR action text, including `PWR  2s Shot` for Ricoh cameras.
- Disconnect and Sleep actions now open a compact `confirm` screen before executing.
- Footer hints were shortened for the 200x200 display.
- GPS screen supports a REC-toggle software switch and shows the furble GPS fields: fix age, satellites, latitude, longitude, altitude, and UTC date/time.
- When GPS has a valid fix and UTC date/time, the firmware forwards geotag data through the existing `Control::updateGPS()` path for connected cameras.

## GPS / ATGM336H-5N wiring

Default firmware wiring is RX-only NMEA:

- ATGM336H `VCC` -> board `3V3` (bare ATGM336H module requires 2.7-3.6V; breakout boards may accept 3.3-5V)
- ATGM336H `GND` -> board `GND`
- ATGM336H `TXD` -> Waveshare header `RXD / GPIO44`
- ATGM336H `RXD` -> Waveshare header `TXD / GPIO43`

The ATGM336H-5N manuals specify UART NMEA0183 output, default `9600` baud, `8N1`, and common sentences such as `GNGGA`, `GNRMC`, `GNZDA`, and antenna status `GPTXT`.

## Build

```powershell
platformio run -e esp32-s3-epaper154
```

## Upload

```powershell
platformio run -e esp32-s3-epaper154 -t upload
```
