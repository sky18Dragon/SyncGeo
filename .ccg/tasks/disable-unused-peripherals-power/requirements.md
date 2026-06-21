# Requirements

Disable board peripherals unused by this firmware at boot and before deep sleep, using the Waveshare ESP32-S3 Touch ePaper 1.54 pin table provided by the user.

## Confirmed unused pins

- Micro SD: GPIO39 SD_CLK, GPIO40 SD_MISO, GPIO41 SD_MOSI.
- ES8311/I2S/audio: GPIO14 I2S_MCLK, GPIO15 I2S_SCLK, GPIO16 I2S_ASDOUT, GPIO38 I2S_LRCK, GPIO45 I2S_DSDIN, GPIO46 PA_CTRL, GPIO42 PA_EN.
- RTC/SHTC3: GPIO47 RTC_SDA, GPIO48 RTC_SCL, GPIO5 RTC_INT. On the touch variant, GPIO47/48 are also used by FT6336 touch at runtime, so they must not be released while the app is running.

## Constraints

- Do not touch e-Paper display pins.
- Do not change GPIO0 REC/BOOT or GPIO18 PWR button behavior.
- Use conservative high-Z input/no-pull handling where there is no separate power gate.