# furble ESP32-S3-ePaper-1.54 phase5

Phase5 keeps the verified phase3/phase4 camera and power behavior, then polishes the 200x200 e-paper interface using the xiaozhang-note visual language.

## UI changes

- Black top title bar with white text.
- Rounded menu/list cards.
- Active selection uses inverted black pill/card with white text.
- Cleaner centered status screens for scanning, connecting, messages, and remote mode.
- Bottom hint bar keeps the REC/PWR action model.
- Sleep/shutdown screen now draws a generated 1bpp bitmap logo reading `SyncGeo`, matching xiaozhang-note's centered sleep logo approach.

## Assets

- AI source image: `assets/syncgeo_sleep_logo_ai.png`.
- 200x200 1bpp preview: `assets/syncgeo_sleep_logo_200_1bpp.png`.
- Firmware header: `include/SyncGeoSleepLogo.h`.

## Build

```powershell
platformio run -e esp32-s3-epaper154
```

## Upload

```powershell
platformio run -e esp32-s3-epaper154 -t upload
```
