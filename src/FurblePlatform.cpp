#include "FurblePlatform.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"

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
  // Power optimization: cap CPU at 80MHz, enable light-sleep only when safe (D6/D7)
  esp_pm_config_t pmConfig = {
      .max_freq_mhz = 80,
      .min_freq_mhz = 40,
      .light_sleep_enable = enable,
  };
  const esp_err_t ret = esp_pm_configure(&pmConfig);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "esp_pm_configure(%d) failed: %s", enable, esp_err_to_name(ret));
  }
}

void Platform::acquire(PowerLock lock) {
  const uint32_t idx = static_cast<uint32_t>(lock);
  if (idx >= static_cast<uint32_t>(PowerLock::_COUNT)) return;

  portENTER_CRITICAL(&m_lockSpinlock);
  m_lockCounts[idx]++;
  const bool wasZero = (m_lockCounts[idx] == 1);
  portEXIT_CRITICAL(&m_lockSpinlock);

  // Any first active lock disables light-sleep (avoid redundant esp_pm_configure calls)
  if (wasZero) {
    setSleep(false);
  }
}

void Platform::release(PowerLock lock) {
  const uint32_t idx = static_cast<uint32_t>(lock);
  if (idx >= static_cast<uint32_t>(PowerLock::_COUNT)) return;

  portENTER_CRITICAL(&m_lockSpinlock);
  if (m_lockCounts[idx] > 0) {
    m_lockCounts[idx]--;
  }
  uint32_t total = 0;
  for (uint32_t i = 0; i < static_cast<uint32_t>(PowerLock::_COUNT); ++i) {
    total += m_lockCounts[i];
  }
  const bool allZero = (total == 0);
  portEXIT_CRITICAL(&m_lockSpinlock);

  // Re-enable light-sleep only when all locks are released
  if (allZero) {
    setSleep(true);
  }
}

uint32_t Platform::getLockCount() const {
  uint32_t total = 0;
  portENTER_CRITICAL(&m_lockSpinlock);
  for (uint32_t i = 0; i < static_cast<uint32_t>(PowerLock::_COUNT); ++i) {
    total += m_lockCounts[i];
  }
  portEXIT_CRITICAL(&m_lockSpinlock);
  return total;
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
