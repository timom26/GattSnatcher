#include "device_interrogator.h"
#include <esp_log.h>
#include <ios>
#include <algorithm>
#include <console_print_controller.h>
#include <cstring>
#include <dirent.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <sstream>
#include <iomanip>
#include <driver/uart.h>
#include <esp_bt.h>
#include "hci_event_parser.h"
#include "freertos/semphr.h"

// Mutex to serialize dispatching requests
static SemaphoreHandle_t dispatchMutex = NULL;


static const char *TAG = "QUESTIONER";

// Using UART0 in this example (adjust as needed)
// #define UART_NUM UART_NUM_0
#define UART_BUF_SIZE 128 //assure its more than sizeof(LeAdvertisingSingleReportWithTimestamp)

DeviceInterrogator &DeviceInterrogator::getInstance() {
    static DeviceInterrogator instance = {};
    return instance;
}
DeviceInterrogator::DeviceInterrogator() {
    profileTabs[PROFILE_A_APP_ID].gattc_cb = InterrogatorEventLoop::gattc_profile_a_event_handler;
    profileTabs[PROFILE_A_APP_ID].gattc_if = ESP_GATT_IF_NONE;
    profileTabs[PROFILE_A_APP_ID].conn_id = UNUSED_CONN_ID;

    profileTabs[PROFILE_B_APP_ID].gattc_cb = InterrogatorEventLoop::gattc_profile_b_event_handler;
    profileTabs[PROFILE_B_APP_ID].gattc_if = ESP_GATT_IF_NONE;
    profileTabs[PROFILE_B_APP_ID].conn_id = UNUSED_CONN_ID;

    profileTabs[PROFILE_C_APP_ID].gattc_cb = InterrogatorEventLoop::gattc_profile_c_event_handler;
    profileTabs[PROFILE_C_APP_ID].gattc_if = ESP_GATT_IF_NONE;
    profileTabs[PROFILE_C_APP_ID].conn_id = UNUSED_CONN_ID;

}


// UART configuration structure
static uart_config_t uart_config = {
    .baud_rate = 460800,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
};

// Define the event base for questioner events
const char *QUESTIONER_EVENT_BASE = "questioner_events";

// Initialize the UART driver and configuration
esp_err_t DeviceInterrogator::initOutputHandler(void)
{
    _uart = UartController::getInstance();
    _rom = FilePrintController::getInstance();
    _console = ConsolePrintController::getInstance();
    if (_rom == NULL || _uart == NULL || _console == NULL)
    {
        ESP_LOGE(TAG, "Cannot create output controller");
        return ESP_FAIL;
    }
    ERR_GUARD(_uart->init(true));
    ERR_GUARD(_rom->init(true));
    ERR_GUARD(_console->init(true));
    return ESP_OK;
}

esp_err_t DeviceInterrogator::initQueues(){
    if (interrogationRequestQueue == nullptr)
    {
        interrogationRequestQueue = xQueueCreate(50, sizeof(interrogation_request_t));
        if (interrogationRequestQueue == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create interrogation request queue");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}


// UART task that waits for incoming data, looks for the magic prefix,
// parses the MAC address, and posts an event.
void DeviceInterrogator::questioner_uart_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Questioner Uart Task");
    uint8_t data[UART_BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(CONFIG_UART_PORT_NUM, data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(20));
        if (len > 0) {
            data[len] = '\0'; // Ensure null termination
            char *saveptr;
            char *line = strtok_r(reinterpret_cast<char *>(data), "\n", &saveptr);
            while (line != nullptr) {
                LeAdvertisingSingleReportWithTimestamp report = {};
                if (HciEventParser::parseAdvReportFromString(line, report)) {
                    // ESP_LOGI(TAG, "Parsed Adv Report: timestamp %lld, adv_event_type %u, addr_type %u, bdaddr %s, adv_data_length %u, rssi %d",
                    //          report.timestamp, report.adv_event_type, report.addr_type,
                    //          report.bdaddr_str, report.adv_data_length, report.rssi);

                    interrogation_request_t request{};
                    request.addr_type = static_cast<esp_ble_addr_type_t>(report.addr_type);
                    request.timestamp = report.timestamp;
                    if (!DeviceInterrogator::parse_bdaddr_str(report.bdaddr_str, request.address)) {
                        ESP_LOGE(TAG, "Failed to parse BDADDR string");
                        return;
                    }
                    strncpy(request.advertisementFilename, report.advertisementFilename, sizeof(request.advertisementFilename));
                    request.advertisementFilename[sizeof(request.advertisementFilename)-1] = '\0';
                    DeviceInterrogator::getInstance().sendInterrogationRequestToQueue(request);
                } else {
                    ESP_LOGE(TAG, "Failed to parse advertising report from: %s", line);
                }

                line = strtok_r(nullptr, "\n", &saveptr);
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

esp_err_t DeviceInterrogator::sendInterrogationRequestToQueue(interrogation_request_t req)
{
    if (xQueueSendToBack(interrogationRequestQueue, &req, 0) != pdPASS) {
        ESP_LOGE("UART_TASK", "Failed to enqueue interrogation request");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void DeviceInterrogator::esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    DeviceInterrogator &interrogator = DeviceInterrogator::getInstance();

    // ESP_LOGI(TAG, "EVT %d, gattc if %d, app_id %d", event, gattc_if, param->reg.app_id);

    // Handle GATT client unregister event

    if (event == ESP_GATTC_UNREG_EVT) {
            ESP_LOGE(TAG, "UNREG_EVT received with null param");
            for (int app_id = 0; app_id < PROFILE_NUM; ++app_id)
            {
                if (gattc_if == interrogator.profileTabs[app_id].gattc_if)
                {
                    interrogator.finalProcedure(app_id, true);
                    interrogator.profileTabs[app_id].gattc_if = ESP_GATT_IF_NONE;
                }
                esp_err_t ret = esp_ble_gattc_app_register(app_id);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "timeoutEnforcer: failed to re-register gattc_if for profile %d", app_id);
                } else {
                    ESP_LOGI(TAG, "timeoutEnforcer: successfully re-registered gattc_if for profile %d", app_id);
                    interrogator.finalProcedure(app_id, true);
                }
            }
            return;

        // int app_id = param->reg.app_id;
        // if (app_id < 0 || app_id >= PROFILE_NUM) {
        //     ESP_LOGE(TAG, "UNREG_EVT: invalid app_id %d", app_id);
        //     return;
        // }
        // // clear out the stale interface and re-register
        // interrogator.profileTabs[app_id].gattc_if = ESP_GATT_IF_NONE;
        // esp_err_t ret = interrogator.finalProcedure(app_id,false);
        // if (ret != ESP_OK){
        //     ESP_LOGE(TAG, "Final Procedure between unregister and new register Failed");
        // }
        // ret = esp_ble_gattc_app_register(app_id);
        // if (ret != ESP_OK) {
        //     ESP_LOGE(TAG, "timeoutEnforcer: failed to re-register gattc_if for profile %d", app_id);
        // } else {
        //     ESP_LOGI(TAG, "timeoutEnforcer: successfully re-registered gattc_if for profile %d", app_id);
        // }
        // return;
    }

    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            interrogator.profileTabs[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(TAG, "Reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        bool found = false;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gattc_if == ESP_GATT_IF_NONE || gattc_if == interrogator.profileTabs[idx].gattc_if) {
                if (interrogator.profileTabs[idx].gattc_cb) {
                    interrogator.profileTabs[idx].gattc_cb(event, gattc_if, param);
                    found = true;
                }
            }
        }
        if (!found){
            ESP_LOGE(TAG,"The callback was not matched to any of the interfaces");
        }
    } while (0);
}
// Create and start the UART controller task
esp_err_t DeviceInterrogator::startUartTask(void)
{
    BaseType_t ret = xTaskCreate(DeviceInterrogator::questioner_uart_task, "questioner_uart_task", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create questioner_uart_task");
        return ESP_FAIL;
    }
    return ESP_OK;
}


static void timeoutEnforcerTask(void *pvParameters) {
    DeviceInterrogator &interrogator = DeviceInterrogator::getInstance();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "timeoutEnforcer: checking profiles");
        int timedOutCount = 0;
        for (int app_id = 0; app_id < PROFILE_NUM; ++app_id) {
            auto &profile = interrogator.profileTabs[app_id];
            // if still marked busy, but no real connection and no queued requests
            if (profile.is_busy
                && profile.conn_id == UNUSED_CONN_ID
                && profile.services.empty()
                && profile.pending_requests.empty()
                && profile.read_char_queue.empty()
                && !interrogator.conn_device[app_id]
                && !interrogator.get_service[app_id]
                // and we've been waiting longer than our threshold
                && profile.busy_since != static_cast<TickType_t>(0)
                && (xTaskGetTickCount() - profile.busy_since) > pdMS_TO_TICKS(CONNECTION_OPEN_TIMEOUT_SECONDS * 1000)) {
                ESP_LOGW(TAG,
                    "timeoutEnforcer: profile %d is timed out, remote_bda=%02x:%02x:%02x:%02x:%02x:%02x",
                    app_id,
                    profile.remote_bda[0], profile.remote_bda[1], profile.remote_bda[2],
                    profile.remote_bda[3], profile.remote_bda[4], profile.remote_bda[5]
                );
                // esp_err_t result = esp_ble_gap_disconnect(profile.remote_bda);
                // if (result == ESP_OK){
                //     ESP_LOGW(TAG, "Profile %d disconnected", app_id);
                // }else{
                //     ESP_LOGE(TAG, "Profile %d failed to disconnect with error %d", app_id,result);
                // }
                // profile.should_force_unregister = true;
                timedOutCount++;
            }
        }
        if (timedOutCount == PROFILE_NUM) {
            ESP_LOGW(TAG, "timeoutEnforcer: all profiles timed out – resetting Bluetooth stack");
            esp_restart();
            DeviceInterrogator &intr = DeviceInterrogator::getInstance();
            intr.deinit_ble();
            intr.resetState();
            intr.init_ble();
        }
    }
}
esp_err_t DeviceInterrogator::deinit_ble() {
    // Unregister all GATT client apps
    esp_ble_gattc_app_unregister(PROFILE_A_APP_ID);
    esp_ble_gattc_app_unregister(PROFILE_B_APP_ID);
    esp_ble_gattc_app_unregister(PROFILE_C_APP_ID);
    // Disable and deinitialize Bluedroid
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    // Disable and deinitialize the BT controller
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    ESP_LOGI(TAG, "Bluetooth stack deinitialized");
    return ESP_OK;
}

void DeviceInterrogator::resetState() {
    // Clear per-profile state
    for (int i = 0; i < PROFILE_NUM; ++i) {
        auto &profile = profileTabs[i];
        profile.gattc_if = ESP_GATT_IF_NONE;
        profile.conn_id = UNUSED_CONN_ID;
        profile.services.clear();
        profile.read_char_queue.clear();
        profile.pending_requests.clear();
        profile.is_char_scheduled = false;
        profile.should_force_unregister = false;
        profile.is_busy = false;
        profile.busy_since = 0;
        memset(profile.remote_bda, 0, sizeof(profile.remote_bda));
        conn_device[i] = false;
        get_service[i] = false;
    }
    // Reset flags
    Isconnecting = false;
    stop_scan_done = false;
    continueMonitorTask = true;
    ESP_LOGW(TAG, "DeviceInterrogator internal state reset");
}


esp_err_t DeviceInterrogator::init_ble()
{
    // Only release Classic BT memory if Classic mode is currently enabled
    if (esp_bt_controller_get_status() & ESP_BT_MODE_CLASSIC_BT) {
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    }
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret;
    ERR_GUARD_LOGE(esp_bt_controller_init(&bt_cfg),"initialize controller failed:" );
    ERR_GUARD_LOGE(esp_bt_controller_enable(ESP_BT_MODE_BLE), "enable controller failed:" );
    ERR_GUARD_LOGE(esp_bluedroid_init(), "init bluetooth failed");
    ERR_GUARD_LOGE(esp_bluedroid_enable(), "enable bluetooth failed");

    ERR_GUARD_LOGE(esp_ble_gap_register_callback(DeviceInterrogator::esp_gap_cb), "gap register error");
    ERR_GUARD_LOGE(esp_ble_gattc_register_callback(DeviceInterrogator::esp_gattc_cb), "gattc register error");

    ERR_GUARD_LOGE(esp_ble_gattc_app_register(PROFILE_A_APP_ID), "gattc app A register error");
    ERR_GUARD_LOGE(esp_ble_gattc_app_register(PROFILE_B_APP_ID), "gattc app B register error");
    ERR_GUARD_LOGE(esp_ble_gattc_app_register(PROFILE_C_APP_ID), "gattc app C register error");
    ERR_GUARD_LOGE(esp_ble_gatt_set_local_mtu(200), "set local MTU failed");

    startPendingMonitor();
    return ESP_OK;
}



void interrogationDispatcherTask(void *pvParameters) {
    interrogation_request_t request;
    QueueHandle_t interrogationRequestQueue = DeviceInterrogator::getInstance().getInterrogationRequestQueue();
    // Lazily create dispatch mutex once
    if (dispatchMutex == NULL) {
        dispatchMutex = xSemaphoreCreateMutex();
        if (dispatchMutex == NULL) {
            ESP_LOGE("DISPATCH", "Failed to create dispatch mutex");
        }
    }
    while (1) {

        if (xQueueReceive(interrogationRequestQueue, &request, 0) == pdTRUE) {
            bool assigned = false;
            // lock dispatch to avoid race between dispatcher tasks
            if (dispatchMutex) {
                xSemaphoreTake(dispatchMutex, portMAX_DELAY);
            }
            for (int profile_num = 0; profile_num < PROFILE_NUM; profile_num++) {
                auto &profile = DeviceInterrogator::getInstance().profileTabs[profile_num];
                // skip any profile whose interface isn't initialized (for example when reinitializing)
                if (profile.gattc_if == ESP_GATT_IF_NONE) {
                    continue;
                }
                if (!profile.is_busy) {
                    // Print MAC address being dispatched
                    char mac_str[18];
                    sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                            request.address[0], request.address[1], request.address[2],
                            request.address[3], request.address[4], request.address[5]);
                    ESP_LOGW("DISPATCH", "Dispatching request for %s to profile %d",
                            mac_str, profile_num);
                    esp_err_t err = esp_ble_gattc_open(
                        profile.gattc_if,
                        request.address,
                        request.addr_type,
                        true
                    );
                    if (err == ESP_OK) {
                        // only mark busy when the open request was accepted
                        memcpy(profile.remote_bda, request.address, 6);
                        profile.is_busy = true;
                        profile.busy_since = xTaskGetTickCount();
                        profile.interrogation_request = request;
                        assigned = true;
                        break;
                    } else {
                        ESP_LOGE("DISPATCH", "esp_ble_gattc_open failed: %x", err);
                    }
                }
            }
            if (dispatchMutex) {
                xSemaphoreGive(dispatchMutex);
            }
            if (!assigned) { // No free profile was found.Requeue at front so it’s retried immediately once a profile frees up
                xQueueSendToFront(interrogationRequestQueue, &request, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


QueueHandle_t DeviceInterrogator::getInterrogationRequestQueue()
{
    return interrogationRequestQueue;
}

void DeviceInterrogator::setInterrogationRequestQueue(QueueHandle_t par)
{
    interrogationRequestQueue = par;
}



void DeviceInterrogator::esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    ESP_LOGI("GAP_CB", "Received GAP_CB event %s",gapEvtToString(event));
    DeviceInterrogator &interrogator = DeviceInterrogator::getInstance();
    // auto &profile = interrogator.profileTabs[app_id];

    // if (profile.should_force_unregister)
    // {
    //     if (ESP_OK != esp_ble_gattc_app_unregister(profile.gattc_if))
    //     {
    //         ESP_LOGE(TAG, "GATTC app FORCED unregister failed");
    //     }else
    //     {
    //         ESP_LOGI(TAG, "GATTC app FORCED unregister");
    //     }
    // }
    // Handle forced gap-level disconnect cleanup
    for (int idx = 0; idx < PROFILE_NUM; ++idx) {
        auto &profile = interrogator.profileTabs[idx];
        if (profile.should_force_unregister) {
            ESP_LOGW(TAG, "gap_cb: forced gap-level disconnect for profile %d", idx);
            interrogator.finalProcedure(idx, true);
            profile.should_force_unregister = false;
        }
    }
    //NOTE: NO NEED FOR SCANNING, WE WILL ALREADY HAVE THE DATA AVAILABLE FROM THE TOP DEVICE
}
esp_err_t DeviceInterrogator::launchTimeoutEnforcerTask(void)
{
    xTaskCreate(
        timeoutEnforcerTask,
        "timeoutEnforcer",
        4096,
        nullptr,
        tskIDLE_PRIORITY + 1,
        nullptr
    );
    return ESP_OK;
}

esp_err_t DeviceInterrogator::launchProfileStatusPrinterTask()
{
    ERR_GUARD(startDispatcherTask());

    // Start periodic state-dump task (every 2 seconds)
    xTaskCreate(
        dumpStateTask,            // function
        "DUMPSTATE",              // name
        4096,                     // stack depth
        nullptr,                  // params
        tskIDLE_PRIORITY + 1,     // priority (low)
        nullptr                   // handle
    );

    return ESP_OK;
}
esp_err_t DeviceInterrogator::mainFunction()
{
    ESP_LOGI(TAG,"Interrogator main task");
    ERR_GUARD(DeviceInterrogator::getInstance().initNvs());
    ERR_GUARD(DeviceInterrogator::getInstance().init_ble());
    ERR_GUARD(DeviceInterrogator::getInstance().initOutputHandler());
    ERR_GUARD(DeviceInterrogator::getInstance().initQueues());
    ESP_LOGI(TAG,"After stack init");
    vTaskDelay(pdMS_TO_TICKS(10));
    ERR_GUARD(startUartTask());
    vTaskDelay(pdMS_TO_TICKS(10));

    // parse_bdaddr_str("00:1B:66:B6:1C:AE",bda);
    // ESP_LOGI(TAG,"before open connection");
    // esp_ble_gattc_open(3, bda, BLE_ADDR_TYPE_PUBLIC,true);
    // ESP_LOGI(TAG,"after open connection");
    ERR_GUARD(awaitAssertInterfacesInitialized());
    ERR_GUARD(startDispatcherTask());
    ERR_GUARD(launchProfileStatusPrinterTask());
    ERR_GUARD(launchTimeoutEnforcerTask());

    return ESP_OK;
}
esp_err_t DeviceInterrogator::awaitAssertInterfacesInitialized()
{
    const TickType_t timeout_ticks = pdMS_TO_TICKS(10000); // 10 seconds max wait
    TickType_t start = xTaskGetTickCount();

    while (true) {
        bool all_ready = true;
        for (int i = 0; i < PROFILE_NUM; ++i) {
            if (profileTabs[i].gattc_if == ESP_GATT_IF_NONE) {
                all_ready = false;
                break;
            }
        }

        if (all_ready) {
            ESP_LOGI(TAG, "All GATT interfaces initialized.");
            return ESP_OK;
        }

        if ((xTaskGetTickCount() - start) > timeout_ticks) {
            ESP_LOGE(TAG, "Timed out waiting for GATT interfaces to be initialized");
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
esp_err_t DeviceInterrogator::startDispatcherTask()
{
    BaseType_t ret = xTaskCreate(interrogationDispatcherTask, "DISPATCH", 2048, NULL, 4, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DISPATCH_TASK");
        return ESP_FAIL;
    }
    return ESP_OK;
}
bool DeviceInterrogator::parse_bdaddr_str(const char *bdaddr_str, esp_bd_addr_t & bd_addr)
{
    // Temporary array to store integer values
    int values[6] = {0};
    // sscanf will extract 6 hexadecimal values separated by colons.
    if (sscanf(bdaddr_str, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return false;
               }
    // Convert the integers to uint8_t and store in bd_addr
    for (int i = 0; i < 6; i++) {
        bd_addr[i] = (uint8_t) values[i];
    }
    return true;
}
esp_err_t DeviceInterrogator::initNvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static void pendingMonitorTask(void *pvParameters) {
    ESP_LOGI(TAG,"pending monitor task started");
    DeviceInterrogator &intr = DeviceInterrogator::getInstance();

    while (intr.continueMonitorTask){
        vTaskDelay(pdMS_TO_TICKS(2000));

        for (int app_id = 0; app_id < PROFILE_NUM; ++app_id) {
            auto &profile = intr.profileTabs[app_id];

            // 1) Purge any pending_requests older than 120 s:
            TickType_t now = xTaskGetTickCount();
            profile.pending_requests.erase(
                std::remove_if(
                    profile.pending_requests.begin(),
                    profile.pending_requests.end(),
                    [&](auto &pr){
                        return (now - pr.timestamp) > pdMS_TO_TICKS(120000);
                    }
                ),
                profile.pending_requests.end()
            );

            // 2) Check for “completely done”
            if (profile.is_char_scheduled
                && profile.read_char_queue.empty()
                && profile.pending_requests.empty())
            {
                ESP_LOGI(TAG,
                    "Finished all characteristic reads for profile %d → finalProcedure",
                    app_id);
                intr.finalProcedure(app_id,true);
            }
        }
    }
}

void DeviceInterrogator::startPendingMonitor() {
    static bool created = false;
    if (!created) {
        xTaskCreate(
            pendingMonitorTask,
            "pendingMonitor",
            4096,
            nullptr,
            tskIDLE_PRIORITY + 1,
            nullptr
        );
        created = true;
    }
}

esp_err_t DeviceInterrogator::finalProcedure(int APP_ID,bool print) {
    auto &profile = profileTabs[APP_ID];
    ESP_LOGI(TAG,"FINAL_PROCEDURE triggered for the profile %d",APP_ID);

    // Attempt to close the GATT connection, but always proceed to clear state
    // If we ever opened, formally close; otherwise skip
    // if (profile.conn_id != 0)
    // {
        esp_err_t close_err = esp_ble_gattc_close(profile.gattc_if, profile.conn_id);
        // esp_err_t close_err = esp_ble_gap_disconnect(profile.remote_bda);
        if (close_err != ESP_OK) {
            ESP_LOGE(TAG, "gattc_close failed for profile %d: %x", APP_ID, close_err);
        }
    // }else
    // {
    //     ESP_LOGE(TAG, "Skipped close for profile %d", APP_ID);
    // }

    // 2) Dump the profile JSON to LittleFS
    // write_profile_lfs(APP_ID, false);//TODO change to true
    if (print){
        _console->printGattProfileJson(APP_ID,profileTabs);
    }
    _rom->printGattProfileJson(APP_ID,profileTabs);
    // 3) Reset profile state for reuse
    profile.services.clear();
    profile.read_char_queue.clear();
    profile.pending_requests.clear();
    profile.is_char_scheduled = false;
    profile.conn_id = UNUSED_CONN_ID;
    conn_device[APP_ID] = false;
    get_service[APP_ID] = false;
    memset(profile.remote_bda, 0, sizeof(profile.remote_bda));
    profileTabs[APP_ID].busy_since = (TickType_t)0;
    ESP_LOGI(TAG, "Profile %d cleaned up and ready for next use", APP_ID);
    profileTabs[APP_ID].should_force_unregister = false;
    profileTabs[APP_ID].is_busy = false;
    return ESP_OK;
}


void DeviceInterrogator::dumpState() {
    UBaseType_t qlen = uxQueueMessagesWaiting(interrogationRequestQueue);
    TickType_t now = xTaskGetTickCount();
    unsigned busy_sec0 = profileTabs[0].is_busy ? (unsigned)((now - profileTabs[0].busy_since) * portTICK_PERIOD_MS / 1000) : 0;
    unsigned busy_sec1 = profileTabs[1].is_busy ? (unsigned)((now - profileTabs[1].busy_since) * portTICK_PERIOD_MS / 1000) : 0;
    unsigned busy_sec2 = profileTabs[2].is_busy ? (unsigned)((now - profileTabs[2].busy_since) * portTICK_PERIOD_MS / 1000) : 0;
    ESP_LOGI(TAG,
        "\n======================================================\n"
        "StateDump: queue=%p, qlen=%u; \n"
        "P0:[app=%u,if=%u,cid=%u,addr=%02x:%02x:%02x:%02x:%02x:%02x,b=%d,s=%d,pr=%u,rq=%u,sv=%u,bs=%u]; \n"
        "P1:[app=%u,if=%u,cid=%u,addr=%02x:%02x:%02x:%02x:%02x:%02x,b=%d,s=%d,pr=%u,rq=%u,sv=%u,bs=%u]; \n"
        "P2:[app=%u,if=%u,cid=%u,addr=%02x:%02x:%02x:%02x:%02x:%02x,b=%d,s=%d,pr=%u,rq=%u,sv=%u,bs=%u]; \n"
        "conn_dev=[%d,%d,%d], get_svc=[%d,%d,%d]; \n"
        "flags:[contTask=%d,connecting=%d,stopScanDone=%d]\n"
        "======================================================\n\n",
        interrogationRequestQueue, (unsigned)qlen,
        // P0
        profileTabs[0].app_id, profileTabs[0].gattc_if, profileTabs[0].conn_id,
          profileTabs[0].remote_bda[0], profileTabs[0].remote_bda[1], profileTabs[0].remote_bda[2],
          profileTabs[0].remote_bda[3], profileTabs[0].remote_bda[4], profileTabs[0].remote_bda[5],
          profileTabs[0].is_busy, profileTabs[0].is_char_scheduled,
          (unsigned)profileTabs[0].pending_requests.size(),
          (unsigned)profileTabs[0].read_char_queue.size(),
          (unsigned)profileTabs[0].services.size(), busy_sec0,
        // P1
        profileTabs[1].app_id, profileTabs[1].gattc_if, profileTabs[1].conn_id,
          profileTabs[1].remote_bda[0], profileTabs[1].remote_bda[1], profileTabs[1].remote_bda[2],
          profileTabs[1].remote_bda[3], profileTabs[1].remote_bda[4], profileTabs[1].remote_bda[5],
          profileTabs[1].is_busy, profileTabs[1].is_char_scheduled,
          (unsigned)profileTabs[1].pending_requests.size(),
          (unsigned)profileTabs[1].read_char_queue.size(),
          (unsigned)profileTabs[1].services.size(), busy_sec1,
        // P2
        profileTabs[2].app_id, profileTabs[2].gattc_if, profileTabs[2].conn_id,
          profileTabs[2].remote_bda[0], profileTabs[2].remote_bda[1], profileTabs[2].remote_bda[2],
          profileTabs[2].remote_bda[3], profileTabs[2].remote_bda[4], profileTabs[2].remote_bda[5],
          profileTabs[2].is_busy, profileTabs[2].is_char_scheduled,
          (unsigned)profileTabs[2].pending_requests.size(),
          (unsigned)profileTabs[2].read_char_queue.size(),
          (unsigned)profileTabs[2].services.size(), busy_sec2,
        // connections & flags
        conn_device[0], conn_device[1], conn_device[2],
        get_service[0], get_service[1], get_service[2],
        continueMonitorTask, Isconnecting, stop_scan_done
    );
}

static void dumpStateTask(void *pvParameters) {
    DeviceInterrogator &intr = DeviceInterrogator::getInstance();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        intr.dumpState();
    }
}

static const char* gapEvtToString(esp_gap_ble_cb_event_t event) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT: return "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT: return "ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: return "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_SCAN_RESULT_EVT: return "ESP_GAP_BLE_SCAN_RESULT_EVT";
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: return "ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT: return "ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: return "ESP_GAP_BLE_ADV_START_COMPLETE_EVT";
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT: return "ESP_GAP_BLE_SCAN_START_COMPLETE_EVT";
        case ESP_GAP_BLE_AUTH_CMPL_EVT: return "ESP_GAP_BLE_AUTH_CMPL_EVT";
        case ESP_GAP_BLE_KEY_EVT: return "ESP_GAP_BLE_KEY_EVT";
        case ESP_GAP_BLE_SEC_REQ_EVT: return "ESP_GAP_BLE_SEC_REQ_EVT";
        case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: return "ESP_GAP_BLE_PASSKEY_NOTIF_EVT";
        case ESP_GAP_BLE_PASSKEY_REQ_EVT: return "ESP_GAP_BLE_PASSKEY_REQ_EVT";
        case ESP_GAP_BLE_OOB_REQ_EVT: return "ESP_GAP_BLE_OOB_REQ_EVT";
        case ESP_GAP_BLE_LOCAL_IR_EVT: return "ESP_GAP_BLE_LOCAL_IR_EVT";
        case ESP_GAP_BLE_LOCAL_ER_EVT: return "ESP_GAP_BLE_LOCAL_ER_EVT";
        case ESP_GAP_BLE_NC_REQ_EVT: return "ESP_GAP_BLE_NC_REQ_EVT";
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT: return "ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT";
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: return "ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT";
        case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT: return "ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT";
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT: return "ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT";
        case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT: return "ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT";
        case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT: return "ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT";
        case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT: return "ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT";
        case ESP_GAP_BLE_CLEAR_BOND_DEV_COMPLETE_EVT: return "ESP_GAP_BLE_CLEAR_BOND_DEV_COMPLETE_EVT";
        case ESP_GAP_BLE_GET_BOND_DEV_COMPLETE_EVT: return "ESP_GAP_BLE_GET_BOND_DEV_COMPLETE_EVT";
        case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT: return "ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT";
        case ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT: return "ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT";
        case ESP_GAP_BLE_UPDATE_DUPLICATE_EXCEPTIONAL_LIST_COMPLETE_EVT: return "ESP_GAP_BLE_UPDATE_DUPLICATE_EXCEPTIONAL_LIST_COMPLETE_EVT";
        case ESP_GAP_BLE_SET_CHANNELS_EVT: return "ESP_GAP_BLE_SET_CHANNELS_EVT";
        case ESP_GAP_BLE_READ_PHY_COMPLETE_EVT: return "ESP_GAP_BLE_READ_PHY_COMPLETE_EVT";
        case ESP_GAP_BLE_SET_PREFERRED_DEFAULT_PHY_COMPLETE_EVT: return "ESP_GAP_BLE_SET_PREFERRED_DEFAULT_PHY_COMPLETE_EVT";
        case ESP_GAP_BLE_SET_PREFERRED_PHY_COMPLETE_EVT: return "ESP_GAP_BLE_SET_PREFERRED_PHY_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_SET_CLEAR_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_ADV_SET_CLEAR_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_CREATE_SYNC_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_CREATE_SYNC_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_CANCEL_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_SYNC_CANCEL_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_TERMINATE_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_SYNC_TERMINATE_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_ADD_DEV_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_ADD_DEV_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_REMOVE_DEV_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_REMOVE_DEV_COMPLETE_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_CLEAR_DEV_COMPLETE_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_CLEAR_DEV_COMPLETE_EVT";
        case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT: return "ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT: return "ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT";
        case ESP_GAP_BLE_PREFER_EXT_CONN_PARAMS_SET_COMPLETE_EVT: return "ESP_GAP_BLE_PREFER_EXT_CONN_PARAMS_SET_COMPLETE_EVT";
        case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT: return "ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT";
        case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: return "ESP_GAP_BLE_EXT_ADV_REPORT_EVT";
        case ESP_GAP_BLE_SCAN_TIMEOUT_EVT: return "ESP_GAP_BLE_SCAN_TIMEOUT_EVT";
        case ESP_GAP_BLE_ADV_TERMINATED_EVT: return "ESP_GAP_BLE_ADV_TERMINATED_EVT";
        case ESP_GAP_BLE_SCAN_REQ_RECEIVED_EVT: return "ESP_GAP_BLE_SCAN_REQ_RECEIVED_EVT";
        case ESP_GAP_BLE_CHANNEL_SELECT_ALGORITHM_EVT: return "ESP_GAP_BLE_CHANNEL_SELECT_ALGORITHM_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_REPORT_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_REPORT_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_LOST_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_SYNC_LOST_EVT";
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_ESTAB_EVT: return "ESP_GAP_BLE_PERIODIC_ADV_SYNC_ESTAB_EVT";
        case ESP_GAP_BLE_SC_OOB_REQ_EVT: return "ESP_GAP_BLE_SC_OOB_REQ_EVT";
        case ESP_GAP_BLE_SC_CR_LOC_OOB_EVT: return "ESP_GAP_BLE_SC_CR_LOC_OOB_EVT";
        case ESP_GAP_BLE_GET_DEV_NAME_COMPLETE_EVT: return "ESP_GAP_BLE_GET_DEV_NAME_COMPLETE_EVT";
        case ESP_GAP_BLE_EVT_MAX: return "ESP_GAP_BLE_EVT_MAX";
        default: return "UNKNOWN_GAP_EVT";
    }
}