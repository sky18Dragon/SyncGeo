# furble ESP32-S3-ePaper-1.54 phase 3

This standalone project targets only the Waveshare ESP32-S3-ePaper-1.54 hardware.
Legacy M5Stack/M5Stick hardware support is intentionally not included.

Implemented in phase 3:

- Two-button e-paper menu, optimized for no touch input.
- BLE camera scanning with furble `Scan`/`CameraList` core.
- Saved camera list loading from NVS.
- Camera selection and connection through furble `Control`.
- Minimal remote screen:
  - REC single: shutter press/release pulse
  - PWR single: focus press/release pulse
  - REC double/long: return to menu
  - PWR double: disconnect
  - PWR long: deep sleep
- Keeps phase 1/2 support: Waveshare power/buttons/battery/deep-sleep and LVGL RGB565 -> 1bpp e-paper flush.

Build:

```powershell
platformio run -e esp32-s3-epaper154
```

Flash:

```powershell
platformio run -e esp32-s3-epaper154 -t upload
```

Expected boot screen:

- `furble phase3`
- menu entries: Scan cameras / Saved cameras / Remote / Disconnect / Sleep
- footer with two-button hints and battery voltage/percentage

Third-stage manual test checklist:

1. Boot shows the phase3 menu.
2. PWR single moves the selection cursor.
3. REC single selects an item.
4. Scan cameras starts BLE scan and updates hit count for 15 seconds.
5. Scan result list supports PWR-next and REC-connect.
6. Remote screen sends shutter/focus pulses after connection.
7. PWR double disconnects; PWR long enters deep sleep.
