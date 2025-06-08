#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <deque>
// #include <device_interrogator.h>
#include <esp_bt_defs.h>
#include <esp_err.h>
#include <esp_gattc_api.h>
#include <stdint.h>
#include <vector>


#define ERR_GUARD(EXPR) do { if (esp_err_t __err__ = (EXPR); __err__ != ESP_OK) { return __err__; }} while (false)
#define ERR_GUARD_LOGE(EXPR, EXPR2) do { if (esp_err_t __err__ = (EXPR); __err__ != ESP_OK) { ESP_LOGE(TAG, "%s", (EXPR2)); return __err__; } } while (false)
#define HCI_EVENT_MAX_SIZE (3 + 255) // 3 octet header + 255 bytes of data [Vol. 4, Part E, 5.4]
#define HCI_BUFFER_SIZE 10  // Empirically tested that ESP manages to process messages fast enough,
                            // that 3 items are mostly sufficient
#define MAX_NUM_REPORTS 0x19
#define CONNECTION_OPEN_TIMEOUT_SECONDS 75
struct BLEInterrogateProfileParams
{
    esp_ble_addr_type_t addr_type;
    esp_bd_addr_t bdAddr;
};


typedef struct
{
    esp_bd_addr_t address;
    esp_ble_addr_type_t addr_type;
    int64_t timestamp;
    char advertisementFilename[64];
} interrogation_request_t;


struct LeAdvertisingSingleReport {
    uint8_t adv_event_type;
    esp_ble_addr_type_t addr_type;
    char bdaddr_str[18];     // Formatted Bluetooth address string ("xx:xx:xx:xx:xx:xx")
    uint8_t raw_bdaddr[6];
    //esp_bd_addr_t bdAddr;
    uint8_t adv_data_length;
    uint8_t adv_data[31];    // Advertisement data (max 31 bytes for legacy advertising)
    int8_t rssi;
};

struct LeAdvertisingReport {
    int64_t timestamp;       // Timestamp from the HCI data
    uint8_t num_reports;     // Number of reports contained
    struct LeAdvertisingSingleReport reports[MAX_NUM_REPORTS]; // Container for multiple reports

    bool isAdvertisingReportConnectable() const;

};

//im lazy and need to move fast..this is effectively what is sent over uart..
//<timestamp>,<adv_event_type>,<addr_type>,<bdaddr>,<adv_data_length>,<rssi>,<scanForProfiling>
//ADV:1623456789,0,0,AA:BB:CC:DD:EE:FF,0,-65
struct LeAdvertisingSingleReportWithTimestamp
{
    int64_t timestamp;
    uint8_t adv_event_type;
    uint8_t addr_type;
    char bdaddr_str[18];
    uint8_t adv_data_length;
    uint8_t adv_data[31];
    int8_t rssi;
    char advertisementFilename[64];
    int8_t needForProfiling;
};

typedef struct {
    int64_t timestamp;
    uint16_t len;
    uint8_t *data;
} hci_data_t;

enum BLEEventType {
    BLE_DETECTION,
};

struct BLEEvent {
    BLEEventType type;
    LeAdvertisingReport report;
};
struct BLEProfile
{
    //TODO list of services, their values too?
};


struct ServiceRange {
    uint16_t start_handle;
    uint16_t end_handle;
};

struct CharacteristicWrapper {
    esp_gattc_char_elem_t  meta;            // handle/UUID/props
    std::vector<uint8_t>   value;           // will grow to value_len on read
};

// holds one service’s handle range + its characteristics
struct ServiceWrapper {
    esp_gatt_srvc_id_t                 service;  // all the boring stuff
    ServiceRange                       range;    // start_handle, end_handle
    std::vector<CharacteristicWrapper> chars;    // 0…N characteristics
};

struct PendingRequest {
    uint16_t handle;      // which characteristic we asked to read
    TickType_t timestamp; // xTaskGetTickCount() when we sent it
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    std::vector<ServiceWrapper> services;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
    std::deque<uint16_t> read_char_queue;//waiting to be sent
    std::vector<PendingRequest> pending_requests;//sent, waiting for result
    SemaphoreHandle_t characteristicReadSemaphore;
    TickType_t read_timeout_ticks = pdMS_TO_TICKS(10000);
    bool is_busy;
    TickType_t busy_since;
    bool is_char_scheduled;
    bool should_force_unregister;
    interrogation_request_t interrogation_request;
};