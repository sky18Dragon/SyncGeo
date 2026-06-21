#include "FurbleControl.h"

#include "FurbleGPS.h"
#include "FurblePlatform.h"

namespace Furble {
Control::Target::Target(Camera *camera) {
  m_Camera = camera;
  m_Queue = xQueueCreate(m_QueueLength, sizeof(cmd_t));
}

Control::Target::~Target() {
  vQueueDelete(m_Queue);
  m_Queue = NULL;
  m_Camera->disconnect();
  m_Camera = NULL;
}

Camera *Control::Target::getCamera(void) const {
  return m_Camera;
}

void Control::Target::sendCommand(cmd_t cmd) {
  BaseType_t ret = xQueueSend(m_Queue, &cmd, 0);
  if (ret != pdTRUE) {
    ESP_LOGE(LOG_TAG, "Failed to send command to target.");
  }
}

Control::cmd_t Control::Target::getCommand(void) {
  cmd_t cmd = CMD_ERROR;
  BaseType_t ret = xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50));
  if (ret != pdTRUE) {
    return CMD_ERROR;
  }
  return cmd;
}

void Control::Target::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  const std::lock_guard<std::mutex> lock(m_GpsMutex);
  m_GPS = gps;
  m_Timesync = timesync;
}

void Control::Target::task(void) {
  const char *name = m_Camera->getName().c_str();

  while (true) {
    cmd_t cmd = this->getCommand();
    switch (cmd) {
      case CMD_SHUTTER_PRESS:
        ESP_LOGI(LOG_TAG, "shutterPress(%s)", name);
        m_Camera->shutterPress();
        break;
      case CMD_SHUTTER_RELEASE:
        ESP_LOGI(LOG_TAG, "shutterRelease(%s)", name);
        m_Camera->shutterRelease();
        break;
      case CMD_FOCUS_PRESS:
        ESP_LOGI(LOG_TAG, "focusPress(%s)", name);
        m_Camera->focusPress();
        break;
      case CMD_FOCUS_RELEASE:
        ESP_LOGI(LOG_TAG, "focusRelease(%s)", name);
        m_Camera->focusRelease();
        break;
      case CMD_GPS_UPDATE:
        ESP_LOGI(LOG_TAG, "updateGeoData(%s)", name);
        {
          // D8: Snapshot GPS payload under mutex to prevent torn reads
          Camera::gps_t gps;
          Camera::timesync_t timesync;
          {
            const std::lock_guard<std::mutex> lock(m_GpsMutex);
            gps = m_GPS;
            timesync = m_Timesync;
          }
          m_Camera->updateGeoData(gps, timesync);
        }
        break;
      case CMD_DISCONNECT:
        m_Camera->setActive(false);
        goto task_exit;
      case CMD_ERROR:
        // ignore continue
        break;
      default:
        ESP_LOGE(LOG_TAG, "Invalid control command %d.", cmd);
    }
  }
task_exit:
  m_Stopped = true;
  vTaskDelete(NULL);
}

Control &Control::getInstance(void) {
  static Control instance;
  if (instance.m_Queue == NULL) {
    instance.m_Queue = xQueueCreate(m_QueueLength, sizeof(cmd_t));
    if (instance.m_Queue == NULL) {
      ESP_LOGE(LOG_TAG, "Failed to create control queue.");
      abort();
    }
  }

  return instance;
}

Control::state_t Control::connectAll(void) {
  uint32_t timeout = m_InfiniteReconnect ? TIMEOUT_INFINITE_MS : TIMEOUT_DEFAULT_MS;

  // Snapshot target pointers under mutex to avoid holding it during connect/scan ops (narrow scope).
  Camera *camera = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    for (const auto &target : m_Targets) {
      if (!target->getCamera()->isConnected()) {
        camera = target->getCamera();
        m_ConnectCamera = camera;
        break;  // serial connection — one at a time
      }
    }
  }

  if (camera != nullptr) {
    if (!camera->connect(m_Power, timeout)) {
      m_BackoffFailCount++;
    } else {
      {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        m_ConnectCamera = nullptr;
      }
      // Successfully connected — when all are connected, apply CONTROL profile
      if (allConnected()) {
        applyProfile(ActiveProfile::CONTROL);
      }
    }
  }

  if (allConnected()) {
    m_BackoffFailCount = 0;
    m_ColdStartStreak = 0;
    return STATE_ACTIVE;
  }

  if (m_State == STATE_DISCONNECTING) {
    return STATE_DISCONNECTING;
  }

  if (m_InfiniteReconnect || (m_BackoffFailCount < 2)) {
    if (m_InfiniteReconnect) {
      // D3: Use tiered backoff instead of hardcoded 5s
      const uint32_t backoffMs = getBackoffMs(m_BackoffFailCount);
      m_BackoffUntilMs = Platform::getInstance().tick() + backoffMs;
      ESP_LOGI(LOG_TAG, "Backoff: %lums (failcount=%lu)", static_cast<unsigned long>(backoffMs),
               static_cast<unsigned long>(m_BackoffFailCount));
      vTaskDelay(pdMS_TO_TICKS(backoffMs));
    }
    return STATE_CONNECT;
  }

  return STATE_CONNECT_FAILED;
}

uint32_t Control::getBackoffMs(uint32_t failcount) {
  // D3: Tiered backoff — 10s / 30s / 60s
  if (failcount <= 1) return 10000;
  if (failcount <= 9) return 30000;
  return 60000;
}

void Control::applyProfile(ActiveProfile profile) {
  if (m_Targets.empty()) return;
  if (profile == m_ActiveProfile) return;  // skip redundant switches

  const Camera::ConnectionProfile *camProfile = nullptr;
  const char *label = nullptr;
  if (profile == ActiveProfile::GPS) {
    camProfile = &Camera::GPS_PROFILE;
    label = "GPS";
  } else if (profile == ActiveProfile::CONTROL) {
    camProfile = &Camera::CONTROL_PROFILE;
    label = "CONTROL";
  }

  if (camProfile == nullptr) return;

  bool anyFailed = false;
  for (const auto &target : m_Targets) {
    Camera *cam = target->getCamera();
    if (cam == nullptr) {
      ESP_LOGW(LOG_TAG, "Profile switch %s: null camera in target", label);
      continue;
    }
    if (!cam->isConnected()) continue;

    if (!cam->requestConnectionUpdate(*camProfile)) {
      anyFailed = true;
    }
  }

  if (!anyFailed) {
    m_ActiveProfile = profile;
    ESP_LOGI(LOG_TAG, "BLE profile: %s (%u/%u/%u/%u)", label,
             camProfile->minInterval, camProfile->maxInterval,
             camProfile->latency, camProfile->timeout);
  } else {
    ESP_LOGW(LOG_TAG, "BLE profile switch to %s had failures", label);
  }
}

uint32_t Control::getLastActivityMs(void) const {
  return m_LastActivityMs;
}

void Control::notifyActivity(void) {
  m_LastActivityMs = Platform::getInstance().tick();
}

void Control::task(void) {
  while (true) {
    cmd_t cmd;
    BaseType_t ret = xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50));

    switch (m_State) {
      case STATE_IDLE:
        if (ret == pdTRUE) {
          if (cmd == CMD_CONNECT) {
            ESP_LOGI(LOG_TAG, "State: IDLE -> CONNECT");
            m_State = STATE_CONNECT;
            m_StateEnteredMs = Platform::getInstance().tick();
            continue;
          }
        }
        break;

      case STATE_CONNECT:
        m_State = STATE_CONNECTING;
        m_StateEnteredMs = Platform::getInstance().tick();
        ESP_LOGI(LOG_TAG, "State: CONNECT -> CONNECTING");
        m_State = connectAll();
        if (m_State == STATE_ACTIVE) {
          ESP_LOGI(LOG_TAG, "State: CONNECTING -> ACTIVE");
          m_StateEnteredMs = Platform::getInstance().tick();
        }
        break;

      case STATE_CONNECTING:
      case STATE_CONNECT_FAILED:
        break;

      case STATE_ACTIVE:
        if (!allConnected()) {
          ESP_LOGI(LOG_TAG, "State: ACTIVE -> CONNECT (disconnected)");
          m_State = STATE_CONNECT;
          m_StateEnteredMs = Platform::getInstance().tick();
          continue;
        }

        if (ret == pdTRUE) {
          // User command received — transition to ACTIVE from IDLE if needed
          if (m_LastActivityMs == 0) {
            m_LastActivityMs = Platform::getInstance().tick();
          }
          for (const auto &target : m_Targets) {
            switch (cmd) {
              case CMD_SHUTTER_PRESS:
              case CMD_SHUTTER_RELEASE:
              case CMD_FOCUS_PRESS:
              case CMD_FOCUS_RELEASE:
              case CMD_GPS_UPDATE:
                target->sendCommand(cmd);
                break;
              default:
                ESP_LOGE(LOG_TAG, "Invalid control command %d.", cmd);
                break;
            }
          }
        } else {
          // D3: Check for idle timeout — transition to CONNECTED_IDLE after 60s no activity
          const uint32_t now = Platform::getInstance().tick();
          if (m_LastActivityMs != 0 && now - m_LastActivityMs >= 60000) {
            ESP_LOGI(LOG_TAG, "State: ACTIVE -> CONNECTED_IDLE (60s idle)");
            m_State = STATE_CONNECTED_IDLE;
            m_StateEnteredMs = now;
            applyProfile(ActiveProfile::GPS);
          }
        }
        break;

      case STATE_CONNECTED_IDLE:
        if (!allConnected()) {
          ESP_LOGI(LOG_TAG, "State: CONNECTED_IDLE -> CONNECT (disconnected)");
          m_State = STATE_CONNECT;
          m_StateEnteredMs = Platform::getInstance().tick();
          continue;
        }

        if (ret == pdTRUE) {
          switch (cmd) {
            case CMD_GPS_UPDATE:
              // GPS update received during idle — handle it then go back to idle
              for (const auto &target : m_Targets) {
                target->sendCommand(cmd);
              }
              break;
            case CMD_SHUTTER_PRESS:
            case CMD_SHUTTER_RELEASE:
            case CMD_FOCUS_PRESS:
            case CMD_FOCUS_RELEASE:
              // User command — wake to ACTIVE (control profile) and forward.
              // NOTE: applyProfile requests BLE param update but does NOT wait for completion.
              // The first shutter command may still use GPS_PROFILE latency (~200ms vs ~30ms).
              // This is an accepted tradeoff: waiting would add 50-200ms to every wake-up.
              ESP_LOGI(LOG_TAG, "State: CONNECTED_IDLE -> ACTIVE (user command)");
              notifyActivity();
              m_State = STATE_ACTIVE;
              m_StateEnteredMs = Platform::getInstance().tick();
              applyProfile(ActiveProfile::CONTROL);
              for (const auto &target : m_Targets) {
                target->sendCommand(cmd);
              }
              break;
            default:
              break;
          }
        }

        // Check GPS sampling period — only if GPS is enabled
        if (GPS::getInstance().isEnabled()) {
          const uint32_t now = Platform::getInstance().tick();
          if (m_GpsLastSampleMs == 0 || now - m_GpsLastSampleMs >= 60000) {
            ESP_LOGI(LOG_TAG, "State: CONNECTED_IDLE -> GPS_SAMPLE");
            m_State = STATE_GPS_SAMPLE;
            m_StateEnteredMs = now;
            m_GpsLastSampleMs = now;
            continue;
          }
        }
        break;

      case STATE_GPS_SAMPLE:
        // Proactively sample GPS snapshot and publish if fix is from CURRENT window.
        {
          const auto gpsData = GPS::getInstance().snapshot();
          const bool fresh = (gpsData.lastFixMs >= m_StateEnteredMs)
                          && (gpsData.lastLocationMs >= m_StateEnteredMs)
                          && (gpsData.lastTimeMs >= m_StateEnteredMs);
          if (fresh && gpsData.enabled && gpsData.hasFix && gpsData.validLocation
              && gpsData.validDate && gpsData.validTime) {
            ESP_LOGI(LOG_TAG, "State: GPS_SAMPLE -> CAMERA_GPS_WRITE (fresh fix acquired)");
            const Camera::gps_t gps = {
                gpsData.latitude, gpsData.longitude,
                gpsData.altitude, gpsData.satellites,
            };
            const Camera::timesync_t timesync = {
                gpsData.year, gpsData.month, gpsData.day,
                gpsData.hour, gpsData.minute, gpsData.second,
                gpsData.centisecond,
            };
            for (const auto &target : m_Targets) {
              target->updateGPS(gps, timesync);
              target->sendCommand(CMD_GPS_UPDATE);
            }
            m_State.store(STATE_CAMERA_GPS_WRITE, std::memory_order_release);
            m_StateEnteredMs = Platform::getInstance().tick();
            applyProfile(ActiveProfile::GPS);
            continue;
          }
        }
        // Also handle async GPS_UPDATE if it arrives
        if (ret == pdTRUE && cmd == CMD_GPS_UPDATE) {
          ESP_LOGI(LOG_TAG, "State: GPS_SAMPLE -> CAMERA_GPS_WRITE (async publish)");
          m_State = STATE_CAMERA_GPS_WRITE;
          m_StateEnteredMs = Platform::getInstance().tick();
          applyProfile(ActiveProfile::GPS);
          for (const auto &target : m_Targets) {
            target->sendCommand(cmd);
          }
          continue;
        }
        // Timeout: no fix acquired within window
        {
          const uint32_t now = Platform::getInstance().tick();
          if (now - m_StateEnteredMs >= GPS::GPS_FIX_WINDOW_MS) {
            ESP_LOGI(LOG_TAG, "State: GPS_SAMPLE -> CONNECTED_IDLE (timeout, %lu consecutive)",
                     static_cast<unsigned long>(m_ColdStartStreak + 1));
            m_State = STATE_CONNECTED_IDLE;
            m_StateEnteredMs = now;
            applyProfile(ActiveProfile::GPS);
            m_ColdStartStreak++;
            continue;
          }
        }
        break;

      case STATE_CAMERA_GPS_WRITE:
        // GPS write complete, return to idle
        ESP_LOGI(LOG_TAG, "State: CAMERA_GPS_WRITE -> CONNECTED_IDLE");
        m_State = STATE_CONNECTED_IDLE;
        m_StateEnteredMs = Platform::getInstance().tick();
        applyProfile(ActiveProfile::GPS);
        m_ColdStartStreak = 0;
        break;

      case STATE_DISCONNECTING:
        break;
    }
  }
}

BaseType_t Control::sendCommand(cmd_t cmd) {
  return xQueueSend(m_Queue, &cmd, 0);
}

BaseType_t Control::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  for (const auto &target : m_Targets) {
    target->updateGPS(gps, timesync);
  }

  cmd_t cmd = CMD_GPS_UPDATE;
  return xQueueSend(m_Queue, &cmd, 0);
}

bool Control::allConnected(void) {
  if (m_Targets.empty()) return false;

  for (const auto &target : m_Targets) {
    if (!target->getCamera()->isConnected()) {
      return false;
    }
  }

  return true;
}

const std::vector<std::unique_ptr<Control::Target>> &Control::getTargets(void) {
  return m_Targets;
}

void Control::connectAll(bool infiniteReconnect) {
  m_InfiniteReconnect = infiniteReconnect;

  this->sendCommand(CMD_CONNECT);
}

void Control::disconnect(void) {
  m_State = STATE_DISCONNECTING;

  // Force cancel any active connection attempts
  ble_gap_conn_cancel();

  const std::lock_guard<std::mutex> lock(m_Mutex);

  // send disconnect
  for (const auto &target : m_Targets) {
    target->sendCommand(CMD_DISCONNECT);
  }

  // wait for tasks to finish
  for (const auto &target : m_Targets) {
    do {
      vTaskDelay(pdMS_TO_TICKS(1));
    } while (!target.get()->m_Stopped);
  }

  m_Targets.clear();

  // Reset backoff state
  m_BackoffFailCount = 0;
  m_LastActivityMs = 0;
  m_ColdStartStreak = 0;
  m_BackoffUntilMs = 0;
  m_GpsLastSampleMs = 0;
  m_ActiveProfile = ActiveProfile::NONE;

  m_State = STATE_IDLE;
}

void Control::addActive(Camera *camera) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  auto target = std::make_unique<Control::Target>(camera);

  // Create per-target task that will self-delete on disconnect
  BaseType_t ret = xTaskCreate(
      [](void *param) {
        auto *target = static_cast<Furble::Control::Target *>(param);
        target->task();
      },
      camera->getName().c_str(), 4096, target.get(), 3, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create task for '%s'.", camera->getName().c_str());
  } else {
    m_Targets.push_back(std::move(target));
  }
}

Camera *Control::getConnectingCamera(void) const {
  return m_ConnectCamera;
}

Control::state_t Control::getState(void) const {
  return m_State;
}

void Control::setPower(esp_power_level_t power) {
  m_Power = power;
}

};  // namespace Furble

void control_task(void *param) {
  Furble::Control *control = static_cast<Furble::Control *>(param);

  control->task();
}
