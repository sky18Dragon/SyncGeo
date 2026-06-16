#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <cstdint>

#include "WaveshareEPaper154.h"

namespace Furble {

class Platform {
 public:
  static Platform &getInstance();
  static void init();

  Platform(const Platform &) = delete;
  Platform &operator=(const Platform &) = delete;

  uint32_t tick() const;
  void update();
  void powerOff();
  void setSleep(bool enable);

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
};

}  // namespace Furble

#endif
