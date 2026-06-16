#include "FurbleGPS.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "FurbleControl.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "esp_log.h"

namespace {
constexpr char TAG[] = "gps";
}

extern "C" void gps_task(void *param) {
  auto *gps = static_cast<Furble::GPS *>(param);
  if (gps != nullptr) gps->task();
}

namespace Furble {

GPS &GPS::getInstance() {
  static GPS instance;
  instance.ensureStarted();
  return instance;
}

void GPS::init() { getInstance().reloadSetting(); }

void GPS::ensureStarted() {
  if (!m_uartInstalled) {
    const uart_config_t uartConfig = {
        .baud_rate = static_cast<int>(Settings::load<uint32_t>(Settings::GPS_BAUD)),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUFFER_SIZE, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uartConfig));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    uart_flush(UART_PORT);
    m_uartInstalled = true;
    ESP_LOGI(TAG, "ATGM336H UART ready: uart=%d tx=GPIO%d rx=GPIO%d baud=%lu", UART_PORT,
             UART_TX_PIN, UART_RX_PIN,
             static_cast<unsigned long>(Settings::load<uint32_t>(Settings::GPS_BAUD)));
  }

  if (m_task == nullptr) {
    const BaseType_t ret = xTaskCreate(gps_task, "gps", 4096, this, 3, &m_task);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create GPS task");
      abort();
    }
  }
}

bool GPS::isEnabled() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_enabled;
}

void GPS::setEnabled(bool enabled) {
  Settings::save<bool>(Settings::GPS, enabled);
  applyEnabled(enabled);
}

void GPS::reloadSetting() { applyEnabled(Settings::load<bool>(Settings::GPS)); }

void GPS::applyEnabled(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
    m_data.enabled = enabled;
    if (!enabled) m_data.hasFix = false;
  }

  if (enabled) {
    const uint32_t baud = Settings::load<uint32_t>(Settings::GPS_BAUD);
    uart_set_baudrate(UART_PORT, baud);
    uart_flush(UART_PORT);
    resetParser();
    if (GPS_PWR_PIN >= 0) gpio_set_level(GPS_PWR_PIN, 1);
    Platform::getInstance().setSleep(false);
    ESP_LOGI(TAG, "GPS enabled at %lu baud", static_cast<unsigned long>(baud));
  } else {
    uart_flush(UART_PORT);
    resetParser();
    if (GPS_PWR_PIN >= 0) gpio_set_level(GPS_PWR_PIN, 0);
    Platform::getInstance().setSleep(true);
    ESP_LOGI(TAG, "GPS disabled");
  }
}

GPS::Snapshot GPS::snapshot() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_data;
}

void GPS::resetParser() {
  m_sentenceLen = 0;
  std::memset(m_sentence, 0, sizeof(m_sentence));
}

void GPS::task() {
  ESP_LOGI(TAG, "Starting ATGM336H GPS task");
  while (true) {
    bool enabled = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      enabled = m_enabled;
    }

    if (!enabled) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    serviceSerial();
  }
}

void GPS::serviceSerial() {
  std::array<uint8_t, READ_BUFFER_SIZE> buffer {};
  const int bytes = uart_read_bytes(UART_PORT, buffer.data(), buffer.size(), pdMS_TO_TICKS(100));
  if (bytes <= 0) return;

  for (int i = 0; i < bytes; ++i) consumeByte(static_cast<char>(buffer[static_cast<size_t>(i)]));
}

void GPS::consumeByte(char ch) {
  if (ch == '$') {
    resetParser();
    m_sentence[m_sentenceLen++] = ch;
    return;
  }

  if (m_sentenceLen == 0) return;

  if (m_sentenceLen < MAX_SENTENCE_LEN - 1) {
    m_sentence[m_sentenceLen++] = ch;
    m_sentence[m_sentenceLen] = '\0';
  } else {
    resetParser();
    return;
  }

  if (ch == '\n') {
    parseSentence(m_sentence);
    resetParser();
  }
}

void GPS::parseSentence(const char *sentence) {
  const char *payload = nullptr;
  size_t payloadLen = 0;
  const uint32_t now = Platform::getInstance().tick();
  if (!validateChecksum(sentence, &payload, payloadLen)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_data.checksumErrors;
    return;
  }

  if (payloadLen == 0 || payloadLen >= MAX_SENTENCE_LEN) return;

  char copy[MAX_SENTENCE_LEN] = {};
  std::memcpy(copy, payload, payloadLen);
  copy[payloadLen] = '\0';

  std::array<char *, 24> fields {};
  size_t count = 0;
  if (!splitFields(copy, fields.data(), fields.size(), count) || count == 0) return;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_data.sentences;
    m_data.lastSentenceMs = now;
  }

  const char *type = fields[0];
  const size_t typeLen = std::strlen(type);
  const char *suffix = typeLen >= 3 ? type + typeLen - 3 : type;

  if (std::strcmp(suffix, "GGA") == 0) {
    parseGGA(fields.data(), count);
  } else if (std::strcmp(suffix, "RMC") == 0) {
    parseRMC(fields.data(), count);
  } else if (std::strcmp(suffix, "ZDA") == 0) {
    parseZDA(fields.data(), count);
  } else if (std::strcmp(suffix, "TXT") == 0) {
    parseTXT(fields.data(), count);
  }

  publishIfReady(now);
}

void GPS::parseGGA(char *fields[], size_t count) {
  if (count < 10) return;

  Snapshot data;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    data = m_data;
  }

  parseTime(fields[1], data);

  double latitude = 0.0;
  double longitude = 0.0;
  const bool haveLat = parseCoordinate(fields[2], fields[3], latitude);
  const bool haveLon = parseCoordinate(fields[4], fields[5], longitude);
  if (haveLat && haveLon) {
    data.latitude = latitude;
    data.longitude = longitude;
    data.validLocation = true;
  }

  data.fixQuality = static_cast<uint8_t>(std::min<unsigned int>(parseUnsigned(fields[6]), 255U));
  data.satellites = parseUnsigned(fields[7]);
  data.altitude = parseDouble(fields[9], data.altitude);
  data.hasFix = data.enabled && data.fixQuality > 0 && data.validLocation;
  if (data.hasFix) data.lastFixMs = Platform::getInstance().tick();

  std::lock_guard<std::mutex> lock(m_mutex);
  m_data = data;
}

void GPS::parseRMC(char *fields[], size_t count) {
  if (count < 10) return;

  Snapshot data;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    data = m_data;
  }

  parseTime(fields[1], data);
  const bool active = fields[2] != nullptr && fields[2][0] == 'A';

  double latitude = 0.0;
  double longitude = 0.0;
  const bool haveLat = parseCoordinate(fields[3], fields[4], latitude);
  const bool haveLon = parseCoordinate(fields[5], fields[6], longitude);
  if (haveLat && haveLon) {
    data.latitude = latitude;
    data.longitude = longitude;
    data.validLocation = true;
  }

  parseDateDMY(fields[9], data);
  data.hasFix = data.enabled && active && data.validLocation;
  if (data.fixQuality > 0) data.hasFix = data.hasFix && data.fixQuality > 0;
  if (data.hasFix) data.lastFixMs = Platform::getInstance().tick();

  std::lock_guard<std::mutex> lock(m_mutex);
  m_data = data;
}

void GPS::parseZDA(char *fields[], size_t count) {
  if (count < 5) return;

  Snapshot data;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    data = m_data;
  }

  parseTime(fields[1], data);
  parseDateYMD(fields[2], fields[3], fields[4], data);

  std::lock_guard<std::mutex> lock(m_mutex);
  m_data = data;
}

void GPS::parseTXT(char *fields[], size_t count) {
  if (count < 5 || fields[4] == nullptr) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  std::snprintf(m_data.antenna, sizeof(m_data.antenna), "%.17s", fields[4]);
}

void GPS::publishIfReady(uint32_t now) {
  Snapshot data;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    data = m_data;
  }

  if (!data.enabled || !data.hasFix || !data.validLocation || !data.validDate || !data.validTime) return;
  if (m_lastPublishMs != 0 && now - m_lastPublishMs < PUBLISH_MS) return;
  m_lastPublishMs = now;

  const Camera::gps_t gps = {
      data.latitude,
      data.longitude,
      data.altitude,
      data.satellites,
  };
  const Camera::timesync_t timesync = {
      data.year, data.month, data.day, data.hour, data.minute, data.second, data.centisecond,
  };
  Control::getInstance().updateGPS(gps, timesync);
}

bool GPS::splitFields(char *payload, char *fields[], size_t maxFields, size_t &count) {
  count = 0;
  if (payload == nullptr || maxFields == 0) return false;

  fields[count++] = payload;
  for (char *p = payload; *p != '\0'; ++p) {
    if (*p == ',') {
      *p = '\0';
      if (count < maxFields) {
        fields[count++] = p + 1;
      } else {
        return false;
      }
    }
  }
  return true;
}

bool GPS::validateChecksum(const char *sentence, const char **payloadBegin, size_t &payloadLen) {
  if (sentence == nullptr || sentence[0] != '$') return false;

  const char *star = std::strchr(sentence, '*');
  const char *end = sentence + std::strcspn(sentence, "\r\n");
  if (star == nullptr || star + 2 >= end) return false;

  uint8_t checksum = 0;
  for (const char *p = sentence + 1; p < star; ++p) checksum ^= static_cast<uint8_t>(*p);

  char expectedText[3] = {star[1], star[2], '\0'};
  char *parseEnd = nullptr;
  errno = 0;
  const unsigned long expected = std::strtoul(expectedText, &parseEnd, 16);
  if (errno != 0 || parseEnd == expectedText || *parseEnd != '\0') return false;
  if (checksum != static_cast<uint8_t>(expected)) return false;

  *payloadBegin = sentence + 1;
  payloadLen = static_cast<size_t>(star - (sentence + 1));
  return true;
}

bool GPS::parseTime(const char *text, Snapshot &data) {
  if (text == nullptr || std::strlen(text) < 6) return false;

  const unsigned long raw = std::strtoul(text, nullptr, 10);
  const unsigned int hour = static_cast<unsigned int>(raw / 10000UL);
  const unsigned int minute = static_cast<unsigned int>((raw / 100UL) % 100UL);
  const unsigned int second = static_cast<unsigned int>(raw % 100UL);
  if (hour > 23 || minute > 59 || second > 60) return false;

  unsigned int centisecond = 0;
  const char *dot = std::strchr(text, '.');
  if (dot != nullptr && dot[1] >= '0' && dot[1] <= '9') {
    centisecond = static_cast<unsigned int>(dot[1] - '0') * 10U;
    if (dot[2] >= '0' && dot[2] <= '9') centisecond += static_cast<unsigned int>(dot[2] - '0');
  }

  data.hour = hour;
  data.minute = minute;
  data.second = second;
  data.centisecond = centisecond;
  data.validTime = true;
  return true;
}

bool GPS::parseDateDMY(const char *text, Snapshot &data) {
  if (text == nullptr || std::strlen(text) != 6) return false;

  char dd[3] = {text[0], text[1], '\0'};
  char mm[3] = {text[2], text[3], '\0'};
  char yy[3] = {text[4], text[5], '\0'};
  const unsigned int day = parseUnsigned(dd);
  const unsigned int month = parseUnsigned(mm);
  unsigned int year = parseUnsigned(yy);
  year += year >= 80 ? 1900 : 2000;

  if (month < 1 || month > 12 || day < 1 || day > 31) return false;
  data.year = year;
  data.month = month;
  data.day = day;
  data.validDate = true;
  return true;
}

bool GPS::parseDateYMD(const char *dayText, const char *monthText, const char *yearText, Snapshot &data) {
  const unsigned int day = parseUnsigned(dayText);
  const unsigned int month = parseUnsigned(monthText);
  const unsigned int year = parseUnsigned(yearText);
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) return false;

  data.year = year;
  data.month = month;
  data.day = day;
  data.validDate = true;
  return true;
}

bool GPS::parseCoordinate(const char *value, const char *hemisphere, double &degrees) {
  if (value == nullptr || value[0] == '\0' || hemisphere == nullptr || hemisphere[0] == '\0') return false;

  char *end = nullptr;
  errno = 0;
  const double raw = std::strtod(value, &end);
  if (errno != 0 || end == value || !std::isfinite(raw)) return false;

  const double wholeDegrees = std::floor(raw / 100.0);
  const double minutes = raw - wholeDegrees * 100.0;
  if (minutes < 0.0 || minutes >= 60.0) return false;

  degrees = wholeDegrees + minutes / 60.0;
  if (hemisphere[0] == 'S' || hemisphere[0] == 'W') degrees = -degrees;
  return true;
}

unsigned int GPS::parseUnsigned(const char *text) {
  if (text == nullptr || text[0] == '\0') return 0;
  char *end = nullptr;
  errno = 0;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (errno != 0 || end == text) return 0;
  return static_cast<unsigned int>(value);
}

double GPS::parseDouble(const char *text, double fallback) {
  if (text == nullptr || text[0] == '\0') return fallback;
  char *end = nullptr;
  errno = 0;
  const double value = std::strtod(text, &end);
  if (errno != 0 || end == text || !std::isfinite(value)) return fallback;
  return value;
}

}  // namespace Furble
