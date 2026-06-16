#ifndef FURBLE_GPS_H
#define FURBLE_GPS_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "Camera.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Furble {

class GPS {
 public:
  struct Snapshot {
    bool enabled = false;
    bool hasFix = false;
    bool validLocation = false;
    bool validDate = false;
    bool validTime = false;
    uint8_t fixQuality = 0;
    unsigned int satellites = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    unsigned int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
    unsigned int centisecond = 0;
    uint32_t lastSentenceMs = 0;
    uint32_t lastFixMs = 0;
    uint32_t sentences = 0;
    uint32_t checksumErrors = 0;
    char antenna[18] = "";
  };

  static GPS &getInstance();
  static void init();

  GPS(const GPS &) = delete;
  GPS(GPS &&) = delete;
  GPS &operator=(const GPS &) = delete;
  GPS &operator=(GPS &&) = delete;

  bool isEnabled() const;
  void setEnabled(bool enabled);
  void reloadSetting();
  Snapshot snapshot() const;
  void task();

 private:
  GPS() = default;

  static constexpr uart_port_t UART_PORT = UART_NUM_1;
  static constexpr int UART_RX_PIN = GPIO_NUM_44;  // Waveshare header RXD; connect ATGM TXD here.
  static constexpr int UART_TX_PIN = GPIO_NUM_43;  // Waveshare header TXD; connect ATGM RXD here.
  static constexpr gpio_num_t GPS_PWR_PIN = static_cast<gpio_num_t>(-1);  // Optional external power gate.
  static constexpr size_t UART_BUFFER_SIZE = 1024;
  static constexpr size_t READ_BUFFER_SIZE = 128;
  static constexpr size_t MAX_SENTENCE_LEN = 128;
  static constexpr uint32_t PUBLISH_MS = 1000;

  void ensureStarted();
  void applyEnabled(bool enabled);
  void resetParser();
  void serviceSerial();
  void consumeByte(char ch);
  void parseSentence(const char *sentence);
  void parseGGA(char *fields[], size_t count);
  void parseRMC(char *fields[], size_t count);
  void parseZDA(char *fields[], size_t count);
  void parseTXT(char *fields[], size_t count);
  void publishIfReady(uint32_t now);

  static bool splitFields(char *payload, char *fields[], size_t maxFields, size_t &count);
  static bool validateChecksum(const char *sentence, const char **payloadBegin, size_t &payloadLen);
  static bool parseTime(const char *text, Snapshot &data);
  static bool parseDateDMY(const char *text, Snapshot &data);
  static bool parseDateYMD(const char *day, const char *month, const char *year, Snapshot &data);
  static bool parseCoordinate(const char *value, const char *hemisphere, double &degrees);
  static unsigned int parseUnsigned(const char *text);
  static double parseDouble(const char *text, double fallback);

  mutable std::mutex m_mutex;
  Snapshot m_data;
  TaskHandle_t m_task = nullptr;
  bool m_uartInstalled = false;
  bool m_enabled = false;
  uint32_t m_lastPublishMs = 0;
  char m_sentence[MAX_SENTENCE_LEN] = {};
  size_t m_sentenceLen = 0;
};

}  // namespace Furble

extern "C" void gps_task(void *param);

#endif
