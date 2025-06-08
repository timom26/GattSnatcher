#pragma once
#include "struct_and_definitions.h"
#include "esp_err.h"
#include <mutex>
#include "esp_bt.h"
#include <console_print_controller.h>
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include <interrogator_event_loop.h>
#include <uart_controller.h>
#include "rom_print_controller.h"

#define UNUSED_CONN_ID UINT16_MAX
#define REMOTE_SERVICE_UUID        0x00FF
#define REMOTE_NOTIFY_CHAR_UUID    0xFF01

struct ProfileTaskParams {
    char macAddress[18];
    LeAdvertisingReport report;
};

typedef struct {
    uint8_t app_id;
    esp_gatt_if_t gattc_if;   //(filled on ESP_GATTC_REG_EVT)
    uint16_t conn_id;         // Connection ID (from ESP_GATTC_OPEN_EVT)
    QueueHandle_t event_queue;// Queue for dispatching events to the profile's task
} profile_context_t;


static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

static esp_bt_uuid_t remote_filter_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_NOTIFY_CHAR_UUID,},
};

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};

static void pendingMonitorTask(void *pvParameters);
void interrogationDispatcherTask(void *pvParameters);
static void dumpStateTask(void *pvParameters);
static void timeoutEnforcerTask(void *pvParameters);
static const char* gapEvtToString(esp_gap_ble_cb_event_t event);

class DeviceInterrogator {
    DeviceInterrogator();
    std::mutex _gatt_mutex;
    bool _gatt_discovery_complete;
    UartController * _uart;
    FilePrintController * _rom;
    ConsolePrintController * _console;
  public:
    DeviceInterrogator(const DeviceInterrogator&) = delete;             // Copy ctor
    DeviceInterrogator(DeviceInterrogator&&) = delete;                  // Move ctor
    DeviceInterrogator& operator=(const DeviceInterrogator&) = delete;  // Copy assignment
    DeviceInterrogator& operator=(DeviceInterrogator&&) = delete;       // Move assignment
    static DeviceInterrogator& getInstance();
    esp_err_t mainFunction();
    esp_err_t initNvs();

    esp_err_t init_ble();
    esp_err_t deinit_ble();
    void resetState();
    esp_err_t initOutputHandler();
    esp_err_t initQueues();
    esp_err_t awaitAssertInterfacesInitialized();
    esp_err_t startUartTask();//TODO this will always have to use UART..


    // esp_err_t scan_gatt_profile(esp_bd_addr_t remote_bda);

    static void questioner_uart_task(void *pvParameters);
    static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
    static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
    esp_err_t sendInterrogationRequestToQueue(interrogation_request_t req);

    esp_err_t startDispatcherTask();

    static bool parse_bdaddr_str(const char *bdaddr_str, esp_bd_addr_t & bd_addr);
    void startPendingMonitor();
    esp_err_t isCharReadFinished(bool & returnVal);
    esp_err_t finalProcedure(int APP_ID, bool print);

    /**
     * Dump the internal state of all GATT profiles and the interrogator.
     */
    void dumpState();
    esp_err_t launchProfileStatusPrinterTask();
    esp_err_t launchTimeoutEnforcerTask();

    bool conn_device[PROFILE_NUM] = {false,false,false};
    bool get_service[PROFILE_NUM] = {false,false,false};
    bool continueMonitorTask = true;
    bool Isconnecting    = false;
    bool stop_scan_done  = false;

    QueueHandle_t interrogationRequestQueue = nullptr;
    QueueHandle_t getInterrogationRequestQueue();
    void setInterrogationRequestQueue(QueueHandle_t par);

    struct gattc_profile_inst profileTabs[PROFILE_NUM] = {};//rest in the constructor
};



