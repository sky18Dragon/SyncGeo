#ifndef FURBLE_WAVESHARE_EPAPER154_H
#define FURBLE_WAVESHARE_EPAPER154_H

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"

namespace Furble {

class WaveshareEPaper154 {
 public:
  enum class Button { REC, PWR };
  enum class ButtonEvent { NONE, SINGLE, DOUBLE, LONG };
  enum class TouchEventType { NONE, TAP };

  struct TouchEvent {
    TouchEventType type = TouchEventType::NONE;
    uint16_t x = 0;
    uint16_t y = 0;
  };

  static constexpr int WIDTH = 200;
  static constexpr int HEIGHT = 200;

  static constexpr gpio_num_t EPD_DC_PIN = GPIO_NUM_10;
  static constexpr gpio_num_t EPD_CS_PIN = GPIO_NUM_11;
  static constexpr gpio_num_t EPD_SCK_PIN = GPIO_NUM_12;
  static constexpr gpio_num_t EPD_MOSI_PIN = GPIO_NUM_13;
  static constexpr gpio_num_t EPD_RST_PIN = GPIO_NUM_9;
  static constexpr gpio_num_t EPD_BUSY_PIN = GPIO_NUM_8;
  static constexpr gpio_num_t EPD_PWR_PIN = GPIO_NUM_6;
  static constexpr gpio_num_t AUDIO_PWR_PIN = GPIO_NUM_42;
  static constexpr gpio_num_t VBAT_PWR_PIN = GPIO_NUM_17;
  static constexpr gpio_num_t RTC_INT_PIN = GPIO_NUM_5;
  static constexpr gpio_num_t SD_CLK_PIN = GPIO_NUM_39;
  static constexpr gpio_num_t SD_MISO_PIN = GPIO_NUM_40;
  static constexpr gpio_num_t SD_MOSI_PIN = GPIO_NUM_41;
  static constexpr gpio_num_t I2S_MCLK_PIN = GPIO_NUM_14;
  static constexpr gpio_num_t I2S_SCLK_PIN = GPIO_NUM_15;
  static constexpr gpio_num_t I2S_ASDOUT_PIN = GPIO_NUM_16;
  static constexpr gpio_num_t I2S_LRCK_PIN = GPIO_NUM_38;
  static constexpr gpio_num_t I2S_DSDIN_PIN = GPIO_NUM_45;
  static constexpr gpio_num_t AUDIO_PA_CTRL_PIN = GPIO_NUM_46;
  static constexpr gpio_num_t RTC_SENSOR_I2C_SDA_PIN = GPIO_NUM_47;
  static constexpr gpio_num_t RTC_SENSOR_I2C_SCL_PIN = GPIO_NUM_48;
  static constexpr gpio_num_t BTN_REC_PIN = GPIO_NUM_0;
  static constexpr gpio_num_t BTN_PWR_PIN = GPIO_NUM_18;
  static constexpr int BAT_ADC_CHANNEL = 3;  // GPIO4, ADC1_CHANNEL_3 on ESP32-S3.

#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  static constexpr bool HAS_TOUCH = true;
  static constexpr gpio_num_t EPD_TP_INT_PIN = GPIO_NUM_21;
  static constexpr gpio_num_t EPD_TP_RST_PIN = GPIO_NUM_7;
  static constexpr gpio_num_t ESP32_I2C_SDA_PIN = RTC_SENSOR_I2C_SDA_PIN;
  static constexpr gpio_num_t ESP32_I2C_SCL_PIN = RTC_SENSOR_I2C_SCL_PIN;
  static constexpr uint8_t FT6336_I2C_ADDR = 0x38;
#else
  static constexpr bool HAS_TOUCH = false;
#endif

  static WaveshareEPaper154 &instance();

  WaveshareEPaper154(const WaveshareEPaper154 &) = delete;
  WaveshareEPaper154 &operator=(const WaveshareEPaper154 &) = delete;

  void init();
  void update();

  uint32_t tickMs() const;
  bool isButtonDown(Button button) const;
  ButtonEvent popButtonEvent(Button button);
  bool touchAvailable() const;
  TouchEvent popTouchEvent();

  int readBatteryPercent();
  float readBatteryVoltage();
  int batteryPercentFromVoltage(float voltage) const;

  void clearDisplay(bool white = true);
  void drawPixel(uint16_t x, uint16_t y, bool black);
  void refreshDisplayPartial();
  void refreshDisplayFull();

  void epaperPowerOn();
  void epaperPowerOff();
  void enterDeepSleep();

 private:
  WaveshareEPaper154() = default;

  struct ButtonState {
    bool down = false;
    bool longReported = false;
    uint32_t downAt = 0;
    uint32_t lastReleaseAt = 0;
    bool suppressUntilRelease = false;
    ButtonEvent pending = ButtonEvent::NONE;
  };

  void initPowerPins();
  void disableUnusedPeripherals(bool includeSharedI2c);
  void initButtons();
  void initBatteryAdc();
  void initDisplay();
  void initTouch();
  static void touchIsr(void *arg);
  void updateButton(Button button, ButtonState &state, gpio_num_t pin);
  void updateTouch();
  void queueEvent(ButtonState &state, ButtonEvent event);
  bool readTouchPoint(uint16_t &x, uint16_t &y);
  bool probeTouchController();

  bool m_initialized = false;
  bool m_adcInitialized = false;
  bool m_adcCalibrated = false;
  adc_cali_handle_t m_adcCaliHandle = nullptr;
  bool m_displayInitialized = false;
  void *m_display = nullptr;

  ButtonState m_rec;
  ButtonState m_pwr;

  bool m_touchInitialized = false;
  bool m_touchOnline = false;
  volatile bool m_touchIrq = false;
  bool m_touchUseInterrupt = false;
  bool m_touchIntHigh = true;
  bool m_touchDown = false;
  uint32_t m_lastTouchPollAt = 0;
  uint32_t m_touchDownAt = 0;
  uint32_t m_lastTouchEventAt = 0;
  uint16_t m_touchX = 0;
  uint16_t m_touchY = 0;
  TouchEvent m_pendingTouch;

#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  i2c_master_bus_handle_t m_i2cBus = nullptr;
  i2c_master_dev_handle_t m_touchDev = nullptr;
#endif
};

}  // namespace Furble

#endif
