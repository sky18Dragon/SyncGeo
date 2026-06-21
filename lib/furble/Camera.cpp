#include <NimBLEAdvertisedDevice.h>

#include "Camera.h"
#include "FurblePlatform.h"

namespace Furble {

Camera::Camera(Type type, PairType pairType) : m_PairType(pairType), m_Type(type) {}

Camera::~Camera() {
  m_Connected = false;
  m_Client = nullptr;
}

void Camera::onConnect(NimBLEClient *pClient) {
  ESP_LOGI(LOG_TAG, "Connected, adjusting transmit power to %d", m_Power);
  NimBLEDevice::setPower(m_Power);
  m_Connected = true;
  Platform::getInstance().acquire(Platform::PowerLock::BLE_CONNECTED);
}

void Camera::onDisconnect(NimBLEClient *pClient, int reason) {
  ESP_LOGI(LOG_TAG, "Disconnected");
  if (m_Connected) {
    Platform::getInstance().release(Platform::PowerLock::BLE_CONNECTED);
  }
  m_Connected = false;
  m_Progress = 0;
}

bool Camera::connect(esp_power_level_t power, uint32_t timeout) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  m_Power = power;

  m_Client = NimBLEDevice::createClient();
  if (m_Client == nullptr) {
    ESP_LOGI(LOG_TAG, "Failed to create client");
    return false;
  }

  m_Client->setClientCallbacks(this, false);
  m_Client->setSelfDelete(true, true);  // self-delete on any connection failure

  // adjust connection timeout and parameters
  m_Client->setConnectTimeout(timeout);
  // try extending range by adjusting connection parameters
  m_Client->setConnectionParams(m_MinInterval, m_MaxInterval, m_Latency, m_Timeout);

  bool connected = this->_connect();
  if (connected) {
    m_Paired = true;
  } else {
    this->_disconnect();
  }

  return m_Connected;
}

void Camera::disconnect(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_Active = false;
  m_Progress = 0;
  this->_disconnect();
}

bool Camera::isActive(void) const {
  return m_Active;
}

void Camera::setActive(bool active) {
  m_Active = active;
}

const Camera::Type &Camera::getType(void) const {
  return m_Type;
}

const std::string &Camera::getName(void) const {
  return m_Name;
}

const NimBLEAddress &Camera::getAddress(void) const {
  return m_Address;
}

uint8_t Camera::getConnectProgress(void) const {
  return m_Progress.load();
}

bool Camera::isConnected(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Connected && m_Client && m_Client->isConnected();
}

bool Camera::requestConnectionUpdate(const ConnectionProfile &profile) {
  if (m_Client == nullptr || !m_Client->isConnected()) return false;

  const esp_err_t err = m_Client->updateConnParams(
      profile.minInterval, profile.maxInterval,
      profile.latency, profile.timeout);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "Connection param update failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(LOG_TAG, "Connection params updated: min=%u max=%u lat=%u to=%u",
           profile.minInterval, profile.maxInterval,
           profile.latency, profile.timeout);
  return true;
}

}  // namespace Furble
