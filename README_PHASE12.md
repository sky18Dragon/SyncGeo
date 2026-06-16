# Superseded by phase 3

Phase 1/2 was validated manually. Current firmware work is documented in README_PHASE3.md.

# furble ESP32-S3-ePaper-1.54 standalone port

This folder is a hardware-specific furble port for the Waveshare ESP32-S3-ePaper-1.54 device.
It intentionally does **not** include the original M5Stack hardware support.

Implemented scope:

- Phase 1: standalone ESP-IDF/PlatformIO target `esp32-s3-epaper154`.
- Phase 1: Waveshare-only board pins, power latch, buttons, ADC battery read, deep sleep entry.
- Phase 2: 200x200 e-paper driver and LVGL RGB565-to-1bpp flush path.
- Phase 2: monochrome boot/status screen for manual flashing and display validation.
- furble BLE camera protocol/core libraries are copied for later UI/control integration.

Not yet implemented intentionally:

- Full camera scan/connect menu.
- Full remote shutter/intervalometer UI.
- GPS support.
- Legacy M5Stack/M5Stick targets.

Build:

```powershell
platformio run -e esp32-s3-epaper154
```

Manual flash after build:

```powershell
platformio run -e esp32-s3-epaper154 -t upload
```

Expected first screen after boot:

- title `furble`
- `ESP32-S3 ePaper 1.54`
- `phase 1+2 display ready`
- battery line
- REC/PWR button event counters

Button mapping in this phase:

- REC single/double/long: updates screen and emits a shutter command to the control queue.
- PWR single/double: refresh/update status.
- PWR long: enters deep sleep; wake via REC(GPIO0) or PWR(GPIO18).
