#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <cstdint>

#include "WaveshareEPaper154.h"

namespace Furble {

class Platform {
 public:
  // Power optimization: named locks for light-sleep gating (D7)
  enum class PowerLock : uint32_t {
    BLE_SCAN = 0,
    BLE_CONNECTED,
    GATT_WRITE,
    GPS_UART_ACTIVE,
    EPAPER_REFRESH,
    _COUNT  // sentinel — keep last
  };

  static Platform &getInstance();
  static void init();

  Platform(const Platform &) = delete;
  Platform &operator=(const Platform &) = delete;

  uint32_t tick() const;
  void update();
  void powerOff();
  void setSleep(bool enable);

  // Power optimization: reference-counted power locks (D7)
  void acquire(PowerLock lock);
  void release(PowerLock lock);
  uint32_t getLockCount() const;

  int batteryPercent();
  float batteryVoltage();
  int batteryPercentFromVoltage(float voltage) const;

  bool buttonDown(WaveshareEPaper154::Button button) const;
  WaveshareEPaper154::ButtonEvent popButtonEvent(WaveshareEPaper154::Button button);
  bool touchAvailable() const;
  WaveshareEPaper154::TouchEvent popTouchEvent();

  void displayClear(bool white = true);
  void displayDrawPixel(uint16_t x, uint16_t y, bool black);
  void displayRefreshPartial();
  void displayRefreshFull();

 private:
  Platform() = default;
  bool m_initialized = false;

  // Power lock counters (one per PowerLock enum value)
  uint32_t m_lockCounts[static_cast<uint32_t>(PowerLock::_COUNT)] = {};
  mutable portMUX_TYPE m_lockSpinlock = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace Furble

#endif
