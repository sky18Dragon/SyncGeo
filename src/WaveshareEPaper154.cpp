#include "WaveshareEPaper154.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char TAG[] = "epaper154";
constexpr spi_host_device_t EPD_SPI_HOST = SPI2_HOST;
constexpr int EPD_BUFFER_LEN = ((Furble::WaveshareEPaper154::WIDTH + 7) / 8) * Furble::WaveshareEPaper154::HEIGHT;
constexpr uint32_t BTN_LONG_MS = 600;
constexpr uint32_t BTN_DOUBLE_MS = 350;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 90;

adc_oneshot_unit_handle_t g_adc1 = nullptr;

void configurePinsInputNoPull(const gpio_num_t *pins, size_t count, const char *label) {
  uint64_t mask = 0;
  for (size_t i = 0; i < count; ++i) {
    if (pins[i] >= GPIO_NUM_0 && pins[i] < GPIO_NUM_MAX) mask |= (1ULL << pins[i]);
  }
  if (mask == 0) return;

  gpio_config_t input = {};
  input.intr_type = GPIO_INTR_DISABLE;
  input.mode = GPIO_MODE_INPUT;
  input.pin_bit_mask = mask;
  input.pull_down_en = GPIO_PULLDOWN_DISABLE;
  input.pull_up_en = GPIO_PULLUP_DISABLE;

  const esp_err_t ret = gpio_config(&input);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to release unused %s pins: %s", label, esp_err_to_name(ret));
  }
}

const uint8_t WF_FULL_1IN54[159] = {
    0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x40, 0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x40, 0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0xA,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x8,  0x1,  0x0,  0x8,  0x1,  0x0,  0x2,
    0xA,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0,  0x0,  0x0,
    0x22, 0x17, 0x41, 0x0,  0x32, 0x20};

const uint8_t WF_PARTIAL_1IN54[159] = {
    0x0,  0x40, 0x0, 0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x80, 0x0, 0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x40, 0x0, 0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0,  0x80, 0x0, 0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xF,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x1,  0x1,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
    0x02, 0x17, 0x41, 0xB0, 0x32, 0x28};

class EpaperDriver {
 public:
  EpaperDriver() { initSpi(); initGpio(); allocateBuffer(); }

  void initFull() {
    setRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    setRst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    setRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    readBusy();
    sendCommand(0x12);
    readBusy();

    sendCommand(0x01);
    sendData(0xC7);
    sendData(0x00);
    sendData(0x01);

    sendCommand(0x11);
    sendData(0x01);

    setWindows(0, Furble::WaveshareEPaper154::WIDTH - 1, Furble::WaveshareEPaper154::HEIGHT - 1, 0);

    sendCommand(0x3C);
    sendData(0x01);

    sendCommand(0x18);
    sendData(0x80);

    sendCommand(0x22);
    sendData(0xB1);
    sendCommand(0x20);

    setCursor(0, Furble::WaveshareEPaper154::HEIGHT - 1);
    readBusy();

    setLut(WF_FULL_1IN54);
  }

  void initPartial() {
    setRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    setRst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    setRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    readBusy();
    setLut(WF_PARTIAL_1IN54);

    sendCommand(0x37);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x40);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);

    sendCommand(0x3C);
    sendData(0x80);

    sendCommand(0x22);
    sendData(0xC0);
    sendCommand(0x20);
    readBusy();
  }

  void clear(bool white) { std::memset(m_buffer, white ? 0xFF : 0x00, EPD_BUFFER_LEN); }

  void displayFull() {
    sendCommand(0x24);
    writeBytes(m_buffer, EPD_BUFFER_LEN);
    turnOnDisplay();
  }

  void displayPartBaseImage() {
    sendCommand(0x24);
    writeBytes(m_buffer, EPD_BUFFER_LEN);
    sendCommand(0x26);
    writeBytes(m_buffer, EPD_BUFFER_LEN);
    turnOnDisplay();
  }

  void displayPartial() {
    sendCommand(0x24);
    writeBytes(m_buffer, EPD_BUFFER_LEN);
    turnOnDisplayPartial();
  }

  void drawPixel(uint16_t x, uint16_t y, bool black) {
    if (x >= Furble::WaveshareEPaper154::WIDTH || y >= Furble::WaveshareEPaper154::HEIGHT) return;

    constexpr uint16_t bytesPerRow = (Furble::WaveshareEPaper154::WIDTH + 7) / 8;
    const uint32_t index = y * bytesPerRow + (x >> 3);
    const uint8_t bit = 7 - (x & 0x07);

    if (black) {
      m_buffer[index] &= ~(0x01 << bit);
    } else {
      m_buffer[index] |= (0x01 << bit);
    }
  }

 private:
  void allocateBuffer() {
    m_buffer = static_cast<uint8_t *>(heap_caps_malloc(EPD_BUFFER_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (m_buffer == nullptr) {
      ESP_LOGW(TAG, "PSRAM allocation failed; using internal RAM for e-paper buffer");
      m_buffer = static_cast<uint8_t *>(heap_caps_malloc(EPD_BUFFER_LEN, MALLOC_CAP_8BIT));
    }
    assert(m_buffer != nullptr);
    clear(true);
  }

  void initGpio() {
    gpio_config_t output = {};
    output.intr_type = GPIO_INTR_DISABLE;
    output.mode = GPIO_MODE_OUTPUT;
    output.pin_bit_mask = (1ULL << Furble::WaveshareEPaper154::EPD_RST_PIN) |
                          (1ULL << Furble::WaveshareEPaper154::EPD_DC_PIN) |
                          (1ULL << Furble::WaveshareEPaper154::EPD_CS_PIN);
    output.pull_down_en = GPIO_PULLDOWN_DISABLE;
    output.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&output));

    gpio_config_t input = {};
    input.intr_type = GPIO_INTR_DISABLE;
    input.mode = GPIO_MODE_INPUT;
    input.pin_bit_mask = (1ULL << Furble::WaveshareEPaper154::EPD_BUSY_PIN);
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&input));

    setCs(1);
    setRst(1);
  }

  void initSpi() {
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = Furble::WaveshareEPaper154::EPD_MOSI_PIN;
    buscfg.sclk_io_num = Furble::WaveshareEPaper154::EPD_SCK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = EPD_BUFFER_LEN;

    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = 40 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &devcfg, &m_spi));
  }

  void readBusy() {
    while (gpio_get_level(Furble::WaveshareEPaper154::EPD_BUSY_PIN) == 1) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  void spiSendByte(uint8_t data) {
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(m_spi, &t));
  }

  void sendData(uint8_t data) {
    setDc(1);
    setCs(0);
    spiSendByte(data);
    setCs(1);
  }

  void sendCommand(uint8_t command) {
    setDc(0);
    setCs(0);
    spiSendByte(command);
    setCs(1);
  }

  void writeBytes(const uint8_t *buffer, int len) {
    setDc(1);
    setCs(0);

    spi_transaction_t t = {};
    t.length = 8 * len;
    t.tx_buffer = buffer;
    ESP_ERROR_CHECK(spi_device_polling_transmit(m_spi, &t));

    setCs(1);
  }

  void setWindows(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
    sendCommand(0x44);
    sendData((xStart >> 3) & 0xFF);
    sendData((xEnd >> 3) & 0xFF);

    sendCommand(0x45);
    sendData(yStart & 0xFF);
    sendData((yStart >> 8) & 0xFF);
    sendData(yEnd & 0xFF);
    sendData((yEnd >> 8) & 0xFF);
  }

  void setCursor(uint16_t xStart, uint16_t yStart) {
    sendCommand(0x4E);
    sendData(xStart & 0xFF);

    sendCommand(0x4F);
    sendData(yStart & 0xFF);
    sendData((yStart >> 8) & 0xFF);
  }

  void setLut(const uint8_t *lut) {
    sendCommand(0x32);
    writeBytes(lut, 153);
    readBusy();

    sendCommand(0x3F);
    sendData(lut[153]);

    sendCommand(0x03);
    sendData(lut[154]);

    sendCommand(0x04);
    sendData(lut[155]);
    sendData(lut[156]);
    sendData(lut[157]);

    sendCommand(0x2C);
    sendData(lut[158]);
  }

  void turnOnDisplay() {
    sendCommand(0x22);
    sendData(0xC7);
    sendCommand(0x20);
    readBusy();
  }

  void turnOnDisplayPartial() {
    sendCommand(0x22);
    sendData(0xCF);
    sendCommand(0x20);
    readBusy();
  }

  void setCs(int level) { gpio_set_level(Furble::WaveshareEPaper154::EPD_CS_PIN, level); }
  void setDc(int level) { gpio_set_level(Furble::WaveshareEPaper154::EPD_DC_PIN, level); }
  void setRst(int level) { gpio_set_level(Furble::WaveshareEPaper154::EPD_RST_PIN, level); }

  spi_device_handle_t m_spi = nullptr;
  uint8_t *m_buffer = nullptr;
};

EpaperDriver *asDisplay(void *display) { return static_cast<EpaperDriver *>(display); }

int batteryPercentFromVoltage(float v) {
  struct Point {
    float voltage;
    int percent;
  };
  constexpr Point points[] = {{3.20f, 0}, {3.40f, 25}, {3.70f, 50}, {3.90f, 75}, {4.20f, 100}};

  if (v <= points[0].voltage) return points[0].percent;
  if (v >= points[4].voltage) return points[4].percent;

  for (size_t i = 1; i < (sizeof(points) / sizeof(points[0])); ++i) {
    if (v <= points[i].voltage) {
      const float span = points[i].voltage - points[i - 1].voltage;
      const float pos = (v - points[i - 1].voltage) / span;
      return points[i - 1].percent + static_cast<int>((points[i].percent - points[i - 1].percent) * pos + 0.5f);
    }
  }
  return 0;
}

}  // namespace

namespace Furble {

WaveshareEPaper154 &WaveshareEPaper154::instance() {
  static WaveshareEPaper154 board;
  return board;
}

void WaveshareEPaper154::init() {
  if (m_initialized) return;

  initPowerPins();
  initButtons();
  // If the board woke while PWR is still held, do not treat that hold as an app event.
  m_pwr.suppressUntilRelease = (gpio_get_level(BTN_PWR_PIN) == 0);
  m_pwr.down = m_pwr.suppressUntilRelease;
  m_pwr.longReported = m_pwr.suppressUntilRelease;
  initBatteryAdc();
  epaperPowerOn();
  vTaskDelay(pdMS_TO_TICKS(200));
  initTouch();
  initDisplay();

  m_initialized = true;
}

void WaveshareEPaper154::initPowerPins() {
  gpio_config_t output = {};
  output.intr_type = GPIO_INTR_DISABLE;
  output.mode = GPIO_MODE_OUTPUT;
  output.pin_bit_mask = (1ULL << EPD_PWR_PIN) | (1ULL << AUDIO_PWR_PIN) | (1ULL << VBAT_PWR_PIN);
  output.pull_down_en = GPIO_PULLDOWN_DISABLE;
  output.pull_up_en = GPIO_PULLUP_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&output));

  // Keep board power latched and keep the unused audio rail off.
  gpio_set_level(VBAT_PWR_PIN, 1);
  gpio_set_level(AUDIO_PWR_PIN, 1);
  gpio_set_level(EPD_PWR_PIN, 1);

  disableUnusedPeripherals(!HAS_TOUCH);
}

void WaveshareEPaper154::disableUnusedPeripherals(bool includeSharedI2c) {
  // GPIO42 is the board audio/PA enable rail. Existing board logic treats high as off.
  gpio_set_level(AUDIO_PWR_PIN, 1);

  // The current firmware does not use Micro SD, ES8311/I2S audio, RTC alarm, or SHTC3.
  // Release their signal pins to high impedance to avoid actively driving unused hardware.
  static constexpr gpio_num_t sdPins[] = {SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN};
  static constexpr gpio_num_t audioPins[] = {I2S_MCLK_PIN, I2S_SCLK_PIN, I2S_ASDOUT_PIN,
                                             I2S_LRCK_PIN, I2S_DSDIN_PIN, AUDIO_PA_CTRL_PIN};
  static constexpr gpio_num_t rtcIntPins[] = {RTC_INT_PIN};
  static constexpr gpio_num_t sharedI2cPins[] = {RTC_SENSOR_I2C_SDA_PIN, RTC_SENSOR_I2C_SCL_PIN};

  configurePinsInputNoPull(sdPins, sizeof(sdPins) / sizeof(sdPins[0]), "Micro SD");
  configurePinsInputNoPull(audioPins, sizeof(audioPins) / sizeof(audioPins[0]), "I2S/audio");
  configurePinsInputNoPull(rtcIntPins, sizeof(rtcIntPins) / sizeof(rtcIntPins[0]), "RTC INT");

  if (includeSharedI2c) {
    configurePinsInputNoPull(sharedI2cPins, sizeof(sharedI2cPins) / sizeof(sharedI2cPins[0]), "RTC/SHTC3 I2C");
  }
}

void WaveshareEPaper154::initButtons() {
  gpio_config_t input = {};
  input.intr_type = GPIO_INTR_DISABLE;
  input.mode = GPIO_MODE_INPUT;
  input.pin_bit_mask = (1ULL << BTN_REC_PIN) | (1ULL << BTN_PWR_PIN);
  input.pull_down_en = GPIO_PULLDOWN_DISABLE;
  input.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&input));
}

void WaveshareEPaper154::initBatteryAdc() {
  adc_oneshot_unit_init_cfg_t initConfig = {
      .unit_id = ADC_UNIT_1,
      .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  if (adc_oneshot_new_unit(&initConfig, &g_adc1) != ESP_OK) {
    ESP_LOGW(TAG, "ADC unit init failed; battery percentage unavailable");
    return;
  }

  adc_oneshot_chan_cfg_t channelConfig = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  if (adc_oneshot_config_channel(g_adc1, static_cast<adc_channel_t>(BAT_ADC_CHANNEL), &channelConfig) != ESP_OK) {
    ESP_LOGW(TAG, "ADC channel init failed; battery percentage unavailable");
    return;
  }

  m_adcInitialized = true;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t caliConfig = {
      .unit_id = ADC_UNIT_1,
      .chan = static_cast<adc_channel_t>(BAT_ADC_CHANNEL),
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  const esp_err_t caliRet = adc_cali_create_scheme_curve_fitting(&caliConfig, &m_adcCaliHandle);
  if (caliRet == ESP_OK) {
    m_adcCalibrated = true;
    ESP_LOGI(TAG, "ADC curve fitting calibration enabled for battery monitor");
  } else {
    ESP_LOGW(TAG, "ADC calibration unavailable (%s); using raw fallback", esp_err_to_name(caliRet));
  }
#else
  ESP_LOGW(TAG, "ADC curve fitting calibration not supported; using raw fallback");
#endif
}

void WaveshareEPaper154::initTouch() {
#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  if (m_touchInitialized) return;

  gpio_config_t touchIo = {};
  touchIo.intr_type = GPIO_INTR_DISABLE;
  touchIo.mode = GPIO_MODE_OUTPUT;
  touchIo.pin_bit_mask = (1ULL << EPD_TP_RST_PIN);
  touchIo.pull_down_en = GPIO_PULLDOWN_DISABLE;
  touchIo.pull_up_en = GPIO_PULLUP_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&touchIo));

  gpio_set_level(EPD_TP_RST_PIN, 1);

  i2c_master_bus_config_t busConfig = {};
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.i2c_port = I2C_NUM_0;
  busConfig.scl_io_num = ESP32_I2C_SCL_PIN;
  busConfig.sda_io_num = ESP32_I2C_SDA_PIN;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;

  esp_err_t ret = i2c_new_master_bus(&busConfig, &m_i2cBus);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Touch I2C bus init failed: %s", esp_err_to_name(ret));
    return;
  }

  i2c_device_config_t devConfig = {};
  devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devConfig.device_address = FT6336_I2C_ADDR;
  devConfig.scl_speed_hz = 400000;

  ret = i2c_master_bus_add_device(m_i2cBus, &devConfig, &m_touchDev);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "FT6336 add device failed: %s", esp_err_to_name(ret));
    return;
  }

  gpio_set_level(EPD_TP_RST_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level(EPD_TP_RST_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level(EPD_TP_RST_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(100));

  if (!probeTouchController()) {
    ESP_LOGW(TAG, "FT6336 not detected; touch disabled. Check board variant/cable/power and SDA%d/SCL%d/RST%d/INT%d",
             ESP32_I2C_SDA_PIN, ESP32_I2C_SCL_PIN, EPD_TP_RST_PIN, EPD_TP_INT_PIN);
    return;
  }

  m_touchInitialized = true;
  m_touchOnline = true;

  gpio_config_t intIo = {};
  intIo.intr_type = GPIO_INTR_NEGEDGE;
  intIo.mode = GPIO_MODE_INPUT;
  intIo.pin_bit_mask = (1ULL << EPD_TP_INT_PIN);
  intIo.pull_down_en = GPIO_PULLDOWN_DISABLE;
  intIo.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&intIo));
  m_touchIntHigh = gpio_get_level(EPD_TP_INT_PIN) != 0;
  if (!m_touchIntHigh) {
    m_touchIrq = true;
    ESP_LOGI(TAG, "FT6336 INT is already low after init; priming first touch read");
  }

  ret = gpio_install_isr_service(0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Touch GPIO ISR service unavailable: %s; using INT polling", esp_err_to_name(ret));
  } else {
    ret = gpio_isr_handler_add(EPD_TP_INT_PIN, touchIsr, this);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
      m_touchUseInterrupt = true;
    } else {
      ESP_LOGW(TAG, "Touch GPIO ISR add failed: %s; using INT polling", esp_err_to_name(ret));
    }
  }

  ESP_LOGI(TAG, "FT6336 touch armed on SDA%d/SCL%d INT%d RST%d mode=%s", ESP32_I2C_SDA_PIN, ESP32_I2C_SCL_PIN,
           EPD_TP_INT_PIN, EPD_TP_RST_PIN, m_touchUseInterrupt ? "irq" : "poll-int");
#else
  m_touchInitialized = false;
#endif
}

void IRAM_ATTR WaveshareEPaper154::touchIsr(void *arg) {
#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  auto *self = static_cast<WaveshareEPaper154 *>(arg);
  if (self != nullptr) self->m_touchIrq = true;
#else
  (void)arg;
#endif
}

bool WaveshareEPaper154::probeTouchController() {
#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  if (m_i2cBus == nullptr) return false;

  esp_err_t lastRet = ESP_FAIL;
  for (int attempt = 1; attempt <= 5; ++attempt) {
    lastRet = i2c_master_probe(m_i2cBus, FT6336_I2C_ADDR, pdMS_TO_TICKS(100));
    if (lastRet == ESP_OK) {
      ESP_LOGI(TAG, "FT6336 probe OK at 0x%02x after reset (attempt %d, SDA=%d SCL=%d INT=%d RST=%d)",
               FT6336_I2C_ADDR, attempt, gpio_get_level(ESP32_I2C_SDA_PIN), gpio_get_level(ESP32_I2C_SCL_PIN),
               gpio_get_level(EPD_TP_INT_PIN), gpio_get_level(EPD_TP_RST_PIN));
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  ESP_LOGW(TAG, "FT6336 probe failed at 0x%02x: %s (SDA=%d SCL=%d INT=%d RST=%d)", FT6336_I2C_ADDR,
           esp_err_to_name(lastRet), gpio_get_level(ESP32_I2C_SDA_PIN), gpio_get_level(ESP32_I2C_SCL_PIN),
           gpio_get_level(EPD_TP_INT_PIN), gpio_get_level(EPD_TP_RST_PIN));
  return false;
#else
  return false;
#endif
}

bool WaveshareEPaper154::readTouchPoint(uint16_t &x, uint16_t &y) {
#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  if (!m_touchInitialized || !m_touchOnline || m_touchDev == nullptr) return false;

  uint8_t reg = 0x02;
  uint8_t points = 0;
  esp_err_t ret = i2c_master_bus_wait_all_done(m_i2cBus, pdMS_TO_TICKS(1000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "FT6336 wait before points read failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = i2c_master_transmit_receive(m_touchDev, &reg, 1, &points, 1, pdMS_TO_TICKS(1000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "FT6336 read points failed: %s", esp_err_to_name(ret));
    return false;
  }
  if ((points & 0x0F) == 0) return false;

  reg = 0x03;
  uint8_t buffer[4] = {};
  ret = i2c_master_bus_wait_all_done(m_i2cBus, pdMS_TO_TICKS(1000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "FT6336 wait before xy read failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = i2c_master_transmit_receive(m_touchDev, &reg, 1, buffer, sizeof(buffer), pdMS_TO_TICKS(1000));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "FT6336 read xy failed: %s", esp_err_to_name(ret));
    return false;
  }

  x = static_cast<uint16_t>(((buffer[0] & 0x0F) << 8) | buffer[1]);
  y = static_cast<uint16_t>(((buffer[2] & 0x0F) << 8) | buffer[3]);
  x = std::min<uint16_t>(x, WIDTH - 1);
  y = std::min<uint16_t>(y, HEIGHT - 1);
  return true;
#else
  (void)x;
  (void)y;
  return false;
#endif
}

void WaveshareEPaper154::updateTouch() {
#if defined(FURBLE_WAVESHARE_TOUCH_EPAPER154)
  if (!HAS_TOUCH || !m_touchInitialized || !m_touchOnline) return;

  const uint32_t now = tickMs();
  const bool intHigh = gpio_get_level(EPD_TP_INT_PIN) != 0;
  const bool irqSeen = m_touchIrq;
  static uint32_t lastIdleLogAt = 0;
  static constexpr uint32_t TOUCH_POLL_MS = 80;
  static constexpr uint32_t TOUCH_IDLE_LOG_MS = 2000;

  m_touchIrq = false;
  m_touchIntHigh = intHigh;

  if (!irqSeen && now - m_lastTouchPollAt < TOUCH_POLL_MS) return;
  m_lastTouchPollAt = now;

  uint16_t x = 0;
  uint16_t y = 0;
  if (!readTouchPoint(x, y)) {
    if (!intHigh && irqSeen) {
      ESP_LOGW(TAG, "touch IRQ/INT low but FT6336 reports no point");
    } else if (now - lastIdleLogAt >= TOUCH_IDLE_LOG_MS) {
      lastIdleLogAt = now;
      ESP_LOGI(TAG, "touch poll idle int=%d irq=%d", intHigh ? 1 : 0, irqSeen ? 1 : 0);
    }
    m_touchDown = false;
    return;
  }

  const bool wasDown = m_touchDown;
  m_touchDown = true;
  m_touchX = x;
  m_touchY = y;
  m_touchDownAt = now;
  ESP_LOGI(TAG, "touch point x=%u y=%u", static_cast<unsigned>(m_touchX), static_cast<unsigned>(m_touchY));
  if (wasDown) return;
  if (m_pendingTouch.type != TouchEventType::NONE) return;
  if (m_lastTouchEventAt != 0 && now - m_lastTouchEventAt < TOUCH_DEBOUNCE_MS) return;

  m_lastTouchEventAt = now;
  m_pendingTouch.type = TouchEventType::TAP;
  m_pendingTouch.x = m_touchX;
  m_pendingTouch.y = m_touchY;
  ESP_LOGI(TAG, "touch tap x=%u y=%u", static_cast<unsigned>(m_touchX), static_cast<unsigned>(m_touchY));
#endif
}

void WaveshareEPaper154::initDisplay() {
  if (m_displayInitialized) return;

  auto *display = new EpaperDriver();
  display->initFull();
  display->clear(true);
  display->displayPartBaseImage();
  display->initPartial();

  m_display = display;
  m_displayInitialized = true;
}

uint32_t WaveshareEPaper154::tickMs() const { return esp_timer_get_time() / 1000ULL; }

void WaveshareEPaper154::update() {
  updateButton(Button::REC, m_rec, BTN_REC_PIN);
  updateButton(Button::PWR, m_pwr, BTN_PWR_PIN);
  updateTouch();
}

bool WaveshareEPaper154::isButtonDown(Button button) const {
  const gpio_num_t pin = button == Button::REC ? BTN_REC_PIN : BTN_PWR_PIN;
  return gpio_get_level(pin) == 0;
}

void WaveshareEPaper154::queueEvent(ButtonState &state, ButtonEvent event) {
  if (state.pending == ButtonEvent::NONE) state.pending = event;
}

void WaveshareEPaper154::updateButton(Button button, ButtonState &state, gpio_num_t pin) {
  (void)button;
  const bool down = gpio_get_level(pin) == 0;
  const uint32_t now = tickMs();

  if (!down && !state.down && state.lastReleaseAt != 0 && (now - state.lastReleaseAt) > BTN_DOUBLE_MS) {
    state.lastReleaseAt = 0;
    queueEvent(state, ButtonEvent::SINGLE);
    return;
  }

  if (down && !state.down) {
    state.down = true;
    state.longReported = false;
    state.downAt = now;
    return;
  }

  if (down && state.down && !state.longReported && (now - state.downAt >= BTN_LONG_MS)) {
    state.longReported = true;
    if (!state.suppressUntilRelease) queueEvent(state, ButtonEvent::LONG);
    return;
  }

  if (!down && state.down) {
    state.down = false;
    if (state.suppressUntilRelease) {
      state.suppressUntilRelease = false;
      state.lastReleaseAt = 0;
      return;
    }
    if (!state.longReported) {
      if (state.lastReleaseAt != 0 && (now - state.lastReleaseAt) <= BTN_DOUBLE_MS) {
        state.lastReleaseAt = 0;
        queueEvent(state, ButtonEvent::DOUBLE);
      } else {
        state.lastReleaseAt = now;
      }
    }
  }
}

WaveshareEPaper154::ButtonEvent WaveshareEPaper154::popButtonEvent(Button button) {
  ButtonState &state = button == Button::REC ? m_rec : m_pwr;
  const ButtonEvent event = state.pending;
  state.pending = ButtonEvent::NONE;
  return event;
}

bool WaveshareEPaper154::touchAvailable() const {
  return m_pendingTouch.type != TouchEventType::NONE;
}

WaveshareEPaper154::TouchEvent WaveshareEPaper154::popTouchEvent() {
  const TouchEvent event = m_pendingTouch;
  m_pendingTouch = {};
  return event;
}

float WaveshareEPaper154::readBatteryVoltage() {
  if (!m_adcInitialized) return -1.0f;

  constexpr int samples = 16;
  int rawSum = 0;
  int rawSamples = 0;
  int calibratedMvSum = 0;
  int calibratedSamples = 0;

  for (int i = 0; i < samples; ++i) {
    int raw = 0;
    if (adc_oneshot_read(g_adc1, static_cast<adc_channel_t>(BAT_ADC_CHANNEL), &raw) == ESP_OK) {
      rawSum += raw;
      rawSamples++;

      if (m_adcCalibrated && m_adcCaliHandle != nullptr) {
        int millivolts = 0;
        if (adc_cali_raw_to_voltage(m_adcCaliHandle, raw, &millivolts) == ESP_OK) {
          calibratedMvSum += millivolts;
          calibratedSamples++;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }

  if (m_adcCalibrated && calibratedSamples > 0) {
    const float pinMillivolts = static_cast<float>(calibratedMvSum) / calibratedSamples;
    return pinMillivolts * 2.0f / 1000.0f;
  }

  if (rawSamples == 0) return -1.0f;
  const float rawMean = static_cast<float>(rawSum) / rawSamples;
  const float pinVoltage = rawMean * 3.3f / 4095.0f;
  return pinVoltage * 2.0f;
}

int WaveshareEPaper154::readBatteryPercent() {
  const float voltage = readBatteryVoltage();
  if (voltage < 0.0f) return -1;
  return batteryPercentFromVoltage(voltage);
}

int WaveshareEPaper154::batteryPercentFromVoltage(float voltage) const { return ::batteryPercentFromVoltage(voltage); }

void WaveshareEPaper154::clearDisplay(bool white) {
  if (!m_displayInitialized) return;
  asDisplay(m_display)->clear(white);
}

void WaveshareEPaper154::drawPixel(uint16_t x, uint16_t y, bool black) {
  if (!m_displayInitialized) return;
  asDisplay(m_display)->drawPixel(x, y, black);
}

void WaveshareEPaper154::refreshDisplayPartial() {
  if (!m_displayInitialized) return;
  asDisplay(m_display)->displayPartial();
}

void WaveshareEPaper154::refreshDisplayFull() {
  if (!m_displayInitialized) return;
  asDisplay(m_display)->displayFull();
}

void WaveshareEPaper154::epaperPowerOn() { gpio_set_level(EPD_PWR_PIN, 0); }

void WaveshareEPaper154::epaperPowerOff() { gpio_set_level(EPD_PWR_PIN, 1); }

void WaveshareEPaper154::enterDeepSleep() {
  // Leave the last rendered e-paper frame visible so the UI can show why the
  // device went to sleep and how to wake it again.
  epaperPowerOff();
  gpio_set_level(AUDIO_PWR_PIN, 1);
  gpio_set_level(VBAT_PWR_PIN, 1);
  disableUnusedPeripherals(true);

  const uint64_t wakeMask = (1ULL << BTN_REC_PIN) | (1ULL << BTN_PWR_PIN);
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW));
  ESP_LOGI(TAG, "Entering deep sleep; wake with REC(GPIO0) or PWR(GPIO18)");
  esp_deep_sleep_start();
}

}  // namespace Furble
