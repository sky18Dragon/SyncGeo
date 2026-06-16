#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Device.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"
#include "esp_log.h"

extern "C" void app_main() {
  ESP_LOGI(FURBLE_STR, "furble ESP32-S3-ePaper-1.54 phase6 firmware: %s", FURBLE_VERSION);

  Furble::Platform::init();
  Furble::Settings::init();
  Furble::Device::init(Furble::Settings::load<esp_power_level_t>(Furble::Settings::TX_POWER));

  auto &control = Furble::Control::getInstance();
  TaskHandle_t controlHandle = nullptr;
  BaseType_t ret = xTaskCreate(control_task, "control", 8192, &control, 4, &controlHandle);
  if (ret != pdPASS) {
    ESP_LOGE(FURBLE_STR, "Failed to create control task");
    abort();
  }
  Furble::GPS::init();

  Furble::UI ui;
  ui.task();
}
