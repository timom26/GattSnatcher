#include "device_scanner.h"

#include <hci_event_parser.h>
#include <stdio.h>
#include <inttypes.h>
#include <output_handler.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "collector_utils.h"
#include "bt_hci_common.h"
#include "constants.h"
#include <struct_and_definitions.h>
#define UART_NUM UART_NUM_0

DeviceScanner &DeviceScanner::getInstance() {
    static DeviceScanner instance = {};
    return instance;
}
DeviceScanner::DeviceScanner() = default;


static const char *TAG = "BLE AD SCANNER";

// Espressif supplied function for customization of BLE scan channel selection
// Location: vendor/libbtdm_app.a
extern "C" void btdm_scan_channel_setting(uint8_t channel);




esp_err_t DeviceScanner::transmitStartupTime() {

    /* Transmit the startup time */
    int64_t start_time = esp_timer_get_time();
    int res = esp_rom_printf("Capture started at: %lu\n", (unsigned long)(start_time / 1000));
    if (res < 0) {
        ESP_LOGE(TAG, "Error while transmitting starting time.");
    }
    return ESP_OK;
}

esp_err_t DeviceScanner::initNvsFlash() {
    /* Initialise NVS - used to store PHY calibration data */
    esp_err_t errCode = nvs_flash_init();
    if (errCode == ESP_ERR_NVS_NO_FREE_PAGES || errCode == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        errCode = nvs_flash_init();
    }
    ESP_ERROR_CHECK(errCode);
    return ESP_OK;
}

esp_err_t DeviceScanner::initOutputHandler()
{
    _uart = UartController::getInstance();
    _rom = FilePrintController::getInstance();
    if (_rom == NULL || _uart == NULL)
    {
        ESP_LOGE(TAG, "Cannot create output controller");
        return ESP_FAIL;
    }
    ERR_GUARD(_rom->init(false));
    currentlyUsedFilename = _rom->getFilename();
    ERR_GUARD(_uart->init(true));
    _uart->setCurrentlyUsedFilename(currentlyUsedFilename);
    return ESP_OK;
}

esp_err_t DeviceScanner::releaseBluetoothClassicHeap() {
    /* Release the heap of Bluetooth Classic as we won't need it */
    esp_err_t errCode = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (errCode != ESP_OK) {
        ESP_LOGI(TAG, "Bluetooth controller release BT CLASSIC memory failed: %s", esp_err_to_name(errCode));
        return errCode;
    }
    return ESP_OK;
}

esp_err_t DeviceScanner::initBluetoothController(esp_bt_controller_config_t & bt_cfg) {
    /* Set up Bluetooth resources */
    esp_err_t errCode = esp_bt_controller_init(&bt_cfg);
    if (errCode != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller initialisation failed: %s", esp_err_to_name(errCode));
        return errCode;
    }
    /* Set up Bluetooth Low Energy mode and enable controller */
    errCode = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (errCode != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Bluetooth Low Energy controller: %s", esp_err_to_name(errCode));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t DeviceScanner::initAdvQueue() {
    /* A queue for storing the received HCI packets */
    _adv_queue = xQueueCreate(40, sizeof(hci_data_t));
    if (_adv_queue == NULL) {
        ESP_LOGE(TAG, "Cannot create HCI IN queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t DeviceScanner::initHciBuffer() {
    /* Create the HCI buffer */
    _hci_buffer = (uint8_t *)malloc(sizeof(uint8_t) * HCI_BUFFER_SIZE * HCI_EVENT_MAX_SIZE);
    if (_hci_buffer == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for HCI buffer.");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t DeviceScanner::registerVhciHostCallback() {
    static esp_vhci_host_callback_t vhci_host_cb = {
        NULL,
        DeviceScanner::controllerOutRdyWrapper
    };
    esp_vhci_host_register_callback(&vhci_host_cb);
    return ESP_OK;
}

esp_err_t DeviceScanner::resetBluetoothController() {
    ESP_LOGI(TAG, "Resetting Bluetooth controller");
    _size = make_cmd_reset(_hci_message);
    esp_vhci_host_send_packet(_hci_message, _size);
    return ESP_OK;
}

esp_err_t DeviceScanner::applyHciEventMask() {
    ESP_LOGI(TAG, "Applying HCI event mask");
    // Enable only LE Meta events => bit 61
    uint8_t evt_mask[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
    _size = make_cmd_set_evt_mask(_hci_message, evt_mask);
    esp_vhci_host_send_packet(_hci_message, _size);
    return ESP_OK;
}

esp_err_t DeviceScanner::startBleScan() {
    ESP_LOGI(TAG, "Starting BLE Scanning");
    uint8_t scan_enable = 0x01;
    uint8_t scan_filter_dups = 0x00;    // Disable duplicates filtering
    _size = make_cmd_ble_set_scan_enable(_hci_message, scan_enable, scan_filter_dups);
    esp_vhci_host_send_packet(_hci_message, _size);
    _ble_scan_initialising = false;
    return ESP_OK;
}

esp_err_t DeviceScanner::setMonitoredChannel(uint8_t monitoredChannel) {
    ESP_LOGI(TAG, "Locking the BLE Scanning to channel %u", MONITORED_CHANNEL);
    btdm_scan_channel_setting(MONITORED_CHANNEL);
    esp_rom_printf("Locked to channel: %u\n", MONITORED_CHANNEL);
    return ESP_OK;
}

esp_err_t DeviceScanner::startControlThread() {
    // Start the control thread
    // FreeRTOS unrestricted task in ESP modification
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html
    // Task stack size - 2048B - taken from ESP HCI example
    // Priority 6 - taken from ESP HCI example
    // Pinned to core 0 - taken from ESP HCI example - Good to do it on the PRO core..
    xTaskCreatePinnedToCore(&hciEvtProcessWrapper, "Process HCI Event", 8192, NULL, 6, NULL, 0);
    return ESP_OK;
}

esp_err_t DeviceScanner::setUpBleScan() {
    ESP_LOGI(TAG, "Setting up BLE Scan parameters");
    // Set up the passive scan
    uint8_t scan_type = 0x00;

    // Interval and Window are set in terms of number of slots (625 microseconds)
    uint16_t scan_interval = 0x50;  // How often to scan
    uint16_t scan_window = 0x50;    // How long to scan

    uint8_t own_addr_type = 0x00;   // Public device address
    uint8_t filter_policy = 0x00;   // Do not further filter any packets

    _size = make_cmd_ble_set_scan_params(_hci_message, scan_type, scan_interval, scan_window, own_addr_type, filter_policy);
    esp_vhci_host_send_packet(_hci_message, _size);
    return ESP_OK;
}



//##################z##################z##################z##################z##################z##################z##################z
//##################z##################z##################z##################z##################z##################z##################z




esp_err_t DeviceScanner::zeroHciDataMemory() {
    memset(&_hci_data, 0, sizeof(hci_data_t));
    return ESP_OK;
}


/**
 *
 * @note keep this function lean, as it can stuck bluetooth stack as a whole
 */
void DeviceScanner::hciEvtProcess(void *pvParameters)
{
    LeAdvertisingReport leAdvertisingReport;
    esp_err_t err = DeviceScanner::zeroHciDataMemory();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HCI data structure");
        return;
    }

    while (1) {
        if (xQueueReceive(_adv_queue, &_hci_data, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Error while receiving a packet from HCI queue.");
            continue;
        }
        esp_err_t err = HciEventParser::fillAdvReport(_hci_data, leAdvertisingReport);
        if (err == ESP_OK){
            if (leAdvertisingReport.isAdvertisingReportConnectable()){
                int64_t now = esp_timer_get_time();    // current time in μs
                for (uint8_t i = 0;i < leAdvertisingReport.num_reports; i++){
                    auto& singleReport = leAdvertisingReport.reports[i];

                    MacKey key;
                    std::memcpy(key.addr,singleReport.bdaddr_str, 6);
                    key.addr_type = singleReport.addr_type;
                    _rom->printAdvertisingSingleReport(singleReport,leAdvertisingReport.timestamp);
                    if ( __builtin_expect(_macCache.shouldPrintAndAddToCache(key, now),false)) {
                        _macCache.evictOld(now);
                        _uart->printAdvertisingSingleReport(singleReport,leAdvertisingReport.timestamp);
                    }
                }
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);//give space to scheduler to force bluetooth to run faster

    }
}

void DeviceScanner::hciEvtProcessWrapper(void *pvParameters) {
    DeviceScanner::getInstance().hciEvtProcess(pvParameters);
}

int DeviceScanner::controllerOutRdy(uint8_t *data, uint16_t len)
{
    hci_data_t queue_data;
    queue_data.timestamp = esp_timer_get_time();  // Get microseconds since ESP boot

    if (len > HCI_EVENT_MAX_SIZE) {
        ESP_LOGD(TAG, "Packet too large.");
        return ESP_FAIL;
    }
    if (uxQueueMessagesWaitingFromISR(_adv_queue) >= HCI_BUFFER_SIZE) {
        ESP_LOGD(TAG, "Failed to enqueue advertising report. Queue full.");
        return ESP_FAIL;
    }
    uint8_t* packet = _hci_buffer + _hci_buffer_idx * HCI_EVENT_MAX_SIZE;
    _hci_buffer_idx = (_hci_buffer_idx + 1) % HCI_BUFFER_SIZE;
    memcpy(packet, data, len);

    queue_data.data = packet;
    queue_data.len = len;
    if (xQueueSendToBackFromISR(_adv_queue, (void*)&queue_data, NULL) != pdTRUE) {
        ESP_LOGD(TAG, "Failed to enqueue advertising report. Queue full.");
    }

    return ESP_OK;
}

int DeviceScanner::controllerOutRdyWrapper(uint8_t *data, uint16_t len) {
    return DeviceScanner::getInstance().controllerOutRdy(data, len);
}

esp_err_t DeviceScanner::mainFunction() {
    ERR_GUARD(transmitStartupTime());
    ERR_GUARD(initNvsFlash());
    ERR_GUARD(initOutputHandler());
    /* Initialise Bluetooth */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(releaseBluetoothClassicHeap());
    ESP_ERROR_CHECK(initBluetoothController(bt_cfg));
    ERR_GUARD(initAdvQueue());
    ERR_GUARD(initHciBuffer());

    // Has to be set before any Bluetooth operations (like sending data, scanning or connecting).
    ERR_GUARD(registerVhciHostCallback());

    // Init Bluetooth procedures in a while loop with vTaskDelay to circumnavigate the watchdog timeouts
    _ble_scan_initialising = true;
    int ble_scan_init_step = 0;
    while (_ble_scan_initialising) {
        if (_ble_scan_initialising && esp_vhci_host_check_send_available()) {
            switch (ble_scan_init_step) {
                case 0:
                    ERR_GUARD(resetBluetoothController());
                    break;
                case 1:
                    ERR_GUARD(applyHciEventMask());
                    break;
                case 2:
                    ERR_GUARD(setUpBleScan());
                    break;
                case 3:
                    ERR_GUARD(setMonitoredChannel(MONITORED_CHANNEL));
                    break;
                case 4:
                    ERR_GUARD(startControlThread());
                    break;
                case 5:
                    ERR_GUARD(startBleScan());
                    break;
                default:
                    _ble_scan_initialising = false;
                    break;
            }
            ble_scan_init_step++;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Watchdog
    }
    return ESP_OK;
}

