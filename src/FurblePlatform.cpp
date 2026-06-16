#include "FurblePlatform.h"

#include "esp_log.h"
#include "esp_pm.h"

namespace {
constexpr char TAG[] = "platform";
}

namespace Furble {

Platform &Platform::getInstance() {
  static Platform platform;
  if (!platform.m_initialized) {
    WaveshareEPaper154::instance().init();
    platform.setSleep(true);
    platform.m_initialized = true;
  }
  return platform;
}

void Platform::init() { (void)getInstance(); }

uint32_t Platform::tick() const { return WaveshareEPaper154::instance().tickMs(); }

void Platform::update() { WaveshareEPaper154::instance().update(); }

void Platform::powerOff() { WaveshareEPaper154::instance().enterDeepSleep(); }

void Platform::setSleep(bool enable) {
  esp_pm_config_t pmConfig = {
      .max_freq_mhz = 160,
      .min_freq_mhz = 40,
      .light_sleep_enable = enable,
  };
  const esp_err_t ret = esp_pm_configure(&pmConfig);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "esp_pm_configure(%d) failed: %s", enable, esp_err_to_name(ret));
  }
}

int Platform::batteryPercent() { return WaveshareEPaper154::instance().readBatteryPercent(); }

float Platform::batteryVoltage() { return WaveshareEPaper154::instance().readBatteryVoltage(); }

int Platform::batteryPercentFromVoltage(float voltage) const {
  return WaveshareEPaper154::instance().batteryPercentFromVoltage(voltage);
}

bool Platform::buttonDown(WaveshareEPaper154::Button button) const {
  return WaveshareEPaper154::instance().isButtonDown(button);
}

WaveshareEPaper154::ButtonEvent Platform::popButtonEvent(WaveshareEPaper154::Button button) {
  return WaveshareEPaper154::instance().popButtonEvent(button);
}

bool Platform::touchAvailable() const { return WaveshareEPaper154::instance().touchAvailable(); }

WaveshareEPaper154::TouchEvent Platform::popTouchEvent() { return WaveshareEPaper154::instance().popTouchEvent(); }

void Platform::displayClear(bool white) { WaveshareEPaper154::instance().clearDisplay(white); }

void Platform::displayDrawPixel(uint16_t x, uint16_t y, bool black) {
  WaveshareEPaper154::instance().drawPixel(x, y, black);
}

void Platform::displayRefreshPartial() { WaveshareEPaper154::instance().refreshDisplayPartial(); }

void Platform::displayRefreshFull() { WaveshareEPaper154::instance().refreshDisplayFull(); }

}  // namespace Furble
