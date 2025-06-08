#pragma once
#include <cstring>
#include <esp_err.h>
#include <interrogator_event_loop.h>
#include <string>
#include <map>
#include <mutex>

struct BDAddrWrapper {
    esp_bd_addr_t addr; // C array of 6 uint8_t
    BDAddrWrapper() {
        memset(addr, 0, sizeof(addr));
    }
    BDAddrWrapper(const esp_bd_addr_t input) {
        memcpy(addr, input, sizeof(addr));
    }
    bool operator<(const BDAddrWrapper &other) const {
        return memcmp(addr, other.addr, sizeof(addr)) < 0;
    }
};
inline void convertEspBdAddrToBDAddrWrapper(esp_bd_addr_t &espAddr, BDAddrWrapper &wrapper)
{
    memcpy(wrapper.addr, espAddr, sizeof(wrapper.addr));
}

// Convert from a BDAddrWrapper (input by reference) to an esp_bd_addr_t (passed by reference)
inline void convertBDAddrWrapperToEspBdAddr(BDAddrWrapper &wrapper, esp_bd_addr_t &espAddr)
{
    memcpy(espAddr, wrapper.addr, sizeof(wrapper.addr));
}


class DeviceDatabase {
    DeviceDatabase();
    //mac address, and connection ID
    std::map<BDAddrWrapper, uint16_t> connections;
    std::mutex connection_mutex;
public:
    DeviceDatabase(const DeviceDatabase&) = delete;             // Copy ctor
    DeviceDatabase(DeviceDatabase&&) = delete;                  // Move ctor
    DeviceDatabase& operator=(const DeviceDatabase&) = delete;  // Copy assignment
    DeviceDatabase& operator=(DeviceDatabase&&) = delete;       // Move assignment
    static DeviceDatabase& getInstance();

    esp_err_t init();
    esp_err_t addProfile(const BLEProfile bleProfile);
    esp_err_t addScan(const LeAdvertisingSingleReport &singleReport,int64_t timestamp);
    bool isProfileAlreadyInterrogated(esp_bd_addr_t macAddress);


    esp_err_t addConnection(esp_bd_addr_t & macAddress, uint16_t conn_id);
    esp_err_t removeConnection(esp_bd_addr_t & macAddress);
    /**
     * Wait for the connection associated with macAddress to become available.
     *
     * @param macAddress The MAC address to query.
     * @param conn_id Output parameter that will receive the connection ID.
     * @param timeout_ms Maximum time to wait (in milliseconds).
     *
     * @return ESP_OK if connection was found within timeout, otherwise ESP_ERR_TIMEOUT.
     */
    esp_err_t getConnectionId(esp_bd_addr_t & macAddress, uint16_t &conn_id, uint32_t timeout_ms);
};
