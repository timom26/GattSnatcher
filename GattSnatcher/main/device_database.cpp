#include "device_database.h"

#include <esp_log.h>
#include <mutex>

static const char *TAG = "DEV_DB";

//TODO implement DB
DeviceDatabase &DeviceDatabase::getInstance() {
    static DeviceDatabase instance = {};
    return instance;
}
DeviceDatabase::DeviceDatabase() = default;


esp_err_t DeviceDatabase::init()
{
    return ESP_OK;
}


bool DeviceDatabase::isProfileAlreadyInterrogated(esp_bd_addr_t bdAddr)
{
    return false;
}
esp_err_t DeviceDatabase::addScan(const LeAdvertisingSingleReport &singleReport,int64_t timestamp)
{
    return ESP_OK;
}
esp_err_t DeviceDatabase::addProfile(const BLEProfile bleProfile)
{
    return ESP_OK;
}

esp_err_t DeviceDatabase::addConnection(esp_bd_addr_t & macAddress, uint16_t conn_id) {
    std::lock_guard<std::mutex> lock(connection_mutex);
    BDAddrWrapper bdWrapper;
    convertEspBdAddrToBDAddrWrapper(macAddress, bdWrapper);
    connections[bdWrapper] = conn_id;
    ESP_LOGI(TAG, "Added connection: %s -> %d", macAddress, conn_id);
    return ESP_OK;
}

// Remove a connection entry by MAC address.
esp_err_t DeviceDatabase::removeConnection(esp_bd_addr_t & macAddress) {
    std::lock_guard<std::mutex> lock(connection_mutex);
    size_t erased = connections.erase(macAddress);
    ESP_LOGI(TAG, "Removed connection for %s, erased: %d", macAddress, erased);
    return ESP_OK;
}

// Wait for a connection to be available for a given MAC address up to a maximum wait time.
esp_err_t DeviceDatabase::getConnectionId(esp_bd_addr_t & macAddress, uint16_t &conn_id, uint32_t timeout_ms) {
    const uint32_t delay_time = 100; // delay per poll in ms
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        {
            BDAddrWrapper bdWrapper;
            convertEspBdAddrToBDAddrWrapper(macAddress, bdWrapper);
            std::lock_guard<std::mutex> lock(connection_mutex);
            auto it = connections.find(bdWrapper);
            if (it != connections.end()) {
                conn_id = it->second;
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(delay_time));
        waited += delay_time;
    }
    ESP_LOGE(TAG, "Timeout waiting for connection for MAC: %s", macAddress);
    return ESP_ERR_TIMEOUT;
}