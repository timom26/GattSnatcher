#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "bt_hci_common.h"

#include "driver/uart.h"

#define HCI_EVENT_MAX_SIZE (3 + 255) // 3 octet header + 255 bytes of data [Vol. 4, Part E, 5.4]
#define HCI_BUFFER_SIZE 10  // Empirically tested that ESP manages to process messages fast enough,
                            // that 3 items are mostly sufficient

// Logging tag
static const char *TAG = "BLE AD SCANNER";

// Channel to monitor
static const uint8_t CHANNEL = 39;

// UART settings
const uart_port_t uart_num = UART_NUM_0;
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
};

static QueueHandle_t uart_queue;

// Espressif supplied function for customization of BLE scan channel selection
// Location: vendor/libbtdm_app.a
extern void btdm_scan_channel_setting(uint8_t channel);

typedef struct {
    int64_t timestamp;
    uint16_t len;
    uint8_t *data;
} hci_data_t;

typedef struct {
    uint8_t len;
    unsigned char *name; // Maximal size of Advertising Data is 31B (Bluetooth Core 5.4 Vol. 4, Part E, 7.7.65.2)
} local_name_t;

static QueueHandle_t adv_queue;


// Buffer for HCI events; 
static uint8_t *hci_buffer = NULL;
static uint8_t hci_buffer_idx = 0;

/*
 * @brief: Callback function of Bluetooth controller used to notify that the controller has a packet to send to the host.
 */
static int controller_out_rdy(uint8_t *data, uint16_t len)
{
    hci_data_t queue_data;
    queue_data.timestamp = esp_timer_get_time();  // Get microseconds since ESP boot

    if (len > HCI_EVENT_MAX_SIZE) {
        ESP_LOGD(TAG, "Packet too large.");
        return ESP_FAIL;
    }
    if (uxQueueMessagesWaitingFromISR(adv_queue) >= HCI_BUFFER_SIZE) {
        ESP_LOGD(TAG, "Failed to enqueue advertising report. Queue full.");
        return ESP_FAIL;
    }
    uint8_t* packet = hci_buffer + hci_buffer_idx * HCI_EVENT_MAX_SIZE;
    hci_buffer_idx = (hci_buffer_idx + 1) % HCI_BUFFER_SIZE;
    memcpy(packet, data, len);

    queue_data.data = packet;
    queue_data.len = len;
    if (xQueueSendToBackFromISR(adv_queue, (void*)&queue_data, NULL) != pdTRUE) {
        ESP_LOGD(TAG, "Failed to enqueue advertising report. Queue full.");
    }

    return ESP_OK;
}

static esp_vhci_host_callback_t vhci_host_cb = {
    NULL,
    controller_out_rdy
};

/*
 * @brief: Worker process, which processes the BLE packets from adv_queue and transmits it upstream
 */
void hci_evt_process(void *pvParameters)
{
    uint8_t report_data_total = 0;
    uint8_t *event_type = NULL, *addr_type = NULL, *report_data_len = NULL;
    int8_t *rssi = NULL;
    bd_addr_t *bdaddr = NULL;
    local_name_t *names = NULL;

    // Maximal number of reports included in a packet
    // Empirical testing revealed that each captured advertising event contained only a single report. 
    // If an event with higher number of reports is received, the buffer will be reallocated.
    uint8_t MAX_REPORT_COUNT = 1;

    hci_data_t* hci_data = (hci_data_t*)malloc(sizeof(hci_data_t));
    if (hci_data == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for HCI data.");
        return;
    }
    memset(hci_data, 0, sizeof(hci_data_t));

    // Allocate a buffer for every needed information of a packet
    event_type = (uint8_t *)malloc(sizeof(uint8_t) * MAX_REPORT_COUNT);
    if (event_type == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for event types.");
        goto free_heap;
    }

    addr_type = (uint8_t *)malloc(sizeof(uint8_t) * MAX_REPORT_COUNT);
    if (addr_type == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for address types.");
        goto free_heap;
    }

    bdaddr = (bd_addr_t *)malloc(sizeof(bd_addr_t) * MAX_REPORT_COUNT);
    if (bdaddr == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for addresses.");
        goto free_heap;
    }

    report_data_len = (uint8_t *)malloc(sizeof(uint8_t) * MAX_REPORT_COUNT);
    if (report_data_len == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for report data lengths.");
        goto free_heap;
    }

    names = (local_name_t *)malloc(sizeof(local_name_t) * MAX_REPORT_COUNT);
    if (names == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for local names.");
        goto free_heap;
    }

    rssi = (int8_t *)malloc(sizeof(int8_t) * MAX_REPORT_COUNT);
    if (rssi == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for RSSIs.");
        goto free_heap;
    }


    /* Read the received packets from the queue and process them */
    while (1) {
        if (xQueueReceive(adv_queue, hci_data, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Error while receiving a packet from HCI queue.");
            continue;
        }

        uint8_t* cursor = hci_data->data;

        if (*cursor != H4_TYPE_EVENT)     // Not a HCI Event (0x04)
            continue;
        
        cursor++;
        if (*cursor != LE_META_EVENTS)     // Not a BLE Event (0x3E)
            continue;

        cursor += 2;
        if (*cursor != HCI_LE_ADV_REPORT)  // Not a BLE Advertising Report (0x02)
            continue;

        cursor++;
        uint8_t report_cnt = *cursor;   // Number of included Advertising Reports in the packet (0x01 - 0x19)
        
        // If the Advertising Reports count is higher than current maximum, realloc buffers
        if (report_cnt > MAX_REPORT_COUNT){
            MAX_REPORT_COUNT = report_cnt;

            event_type = (uint8_t *)realloc(event_type, sizeof(uint8_t) * MAX_REPORT_COUNT);
            if (event_type == NULL) {
                ESP_LOGE(TAG, "Cannot allocate heap for event types.");
                break;
            }
            addr_type = (uint8_t *)realloc(addr_type, sizeof(uint8_t) * MAX_REPORT_COUNT);
            if (addr_type == NULL) {
                ESP_LOGE(TAG, "Cannot allocate heap for address types.");
                break;
            }     
            bdaddr = (bd_addr_t *)realloc(bdaddr, sizeof(bd_addr_t) * MAX_REPORT_COUNT);
            if (bdaddr == NULL) {
                ESP_LOGE(TAG, "Cannot allocate heap for addresses.");
                break;
            }

            report_data_len = (uint8_t *)realloc(report_data_len, sizeof(uint8_t) * MAX_REPORT_COUNT);
            if (report_data_len == NULL) {
                ESP_LOGE(TAG, "Cannot allocate heap for report data lengths.");
                break;
            }

            names = (local_name_t *)realloc(names, sizeof(local_name_t) * MAX_REPORT_COUNT);
            if (names == NULL) {
                ESP_LOGE(TAG, "Cannot allocate heap for local names.");
                break;
            }

            rssi = (int8_t *)realloc(rssi, sizeof(int8_t) * MAX_REPORT_COUNT);
            if (rssi == NULL) {
                ESP_LOGE(TAG, "Cannot allocate heap for RSSIs.");
                break;
            }
        }

        cursor++;

        // Get Advertising Type of every report  
        for (uint8_t i = 0; i < report_cnt; i++) {
            event_type[i] = *cursor++;
        }

        // Get Address Type of every report
        for (uint8_t i = 0; i < report_cnt; i++) {
            addr_type[i] = *cursor++;
        }

        // Get Bluetooth Address of every report
      
        for (uint8_t i = 0; i < report_cnt; i++) {
            for (uint8_t j = 0; j < BD_ADDR_LEN; j++) {
                bdaddr[i][j] = *cursor++;
            }
        }

        // Every report can have data, get their lengths
        for (uint8_t i = 0; i < report_cnt; i++) {
            report_data_len[i] = *cursor;
            report_data_total += *cursor;
            cursor++;
        }

        if (report_data_total > 0) {
            // Get local names from report data
           
            
            for (uint8_t report_idx = 0; report_idx < report_cnt; report_idx++) {
                // First set the name length of current report to 0 (indicating no name was found)
                names[report_idx].len = 0;

                if (report_data_len[report_idx] > 0) {  // There is some data present
                    
                    for (uint8_t data_ptr = 0; data_ptr < report_data_len[report_idx]; data_ptr++) {

                        uint8_t ad_len = *cursor++;
                        if (ad_len == 0) {
                            ESP_LOGE(TAG, "Invalid advertising data with length 0.");
                            continue;
                        }
                        data_ptr += ad_len;

                        uint8_t ad_type = *cursor++;
                        uint8_t ad_data_len = ad_len - 1; // Remove one octet for AD Type
                        switch(ad_type) {
                            case 0x08:  // Shortened Local Name (max 31B)
                                if (names[report_idx].len == 0) {   // Prevent overwriting complete local name
                                    names[report_idx].name = cursor;
                                    names[report_idx].len = ad_data_len;
                                }
                                break;
                            case 0x09:  // Complete Local Name (max 248B, but directly in Advertising Report limited by 31B)
                                names[report_idx].name = cursor;
                                names[report_idx].len = ad_data_len;
                                break;
                        }

                        cursor += ad_data_len;
                    }
                }
            }
        }

        // Get RSSI of every report
        for (uint8_t i = 0; i < report_cnt; i++) {
            rssi[i] = (int8_t)*cursor++;
        }

        // Reset the Watchdog before sending
        esp_task_wdt_reset();

        // Send the report downstream
        //  Format: Adv:{Timestamp},{Address},{Address Type},{Advertising Type},{Channel},{RSSI},{Device Name}
        for (uint8_t i = 0; i < report_cnt; i++) {

//  Text format:
//
//          char bdaddr_str[BD_ADDR_LEN * 3];   // every byte has 2 hex chars + 1 divider (semicolon)
//
//          sprintf(bdaddr_str, 
//                  "%02x:%02x:%02x:%02x:%02x:%02x", 
//                  bdaddr[i][5], bdaddr[i][4], bdaddr[i][3], bdaddr[i][2], bdaddr[i][1], bdaddr[i][0]
//          );
//
//          char nameBuffer[names[i].len + 1]; // +1 for the null-terminating character
//          memcpy(nameBuffer, names[i].name, names[i].len);
//          nameBuffer[names[i].len] = '\0'; // null-terminate the string
//
//          esp_rom_printf("Adv:%lld,%s,%02x,%02x,%u,%d,%s\n",
//                          hci_data->timestamp,
//                          bdaddr_str,
//                          addr_type[i],
//                          event_type[i],
//                          CHANNEL,
//                          rssi[i],
//                          nameBuffer
//                      );


            uart_write_bytes(uart_num, "Adv:", 4);
            uart_write_bytes(uart_num, (const char*)&hci_data->timestamp, 8);
            uart_write_bytes(uart_num, (const char*)&bdaddr[i], BD_ADDR_LEN);
            uart_write_bytes(uart_num, (const char*)&addr_type[i], 1);
            uart_write_bytes(uart_num, (const char*)&event_type[i], 1);
            uart_write_bytes(uart_num, (const char*)&CHANNEL, 1);
            uart_write_bytes(uart_num, (const char*)&rssi[i], 1);
            uart_write_bytes(uart_num, (const char*)&names[i].len, 1);
            uart_write_bytes(uart_num, (const char*)names[i].name, names[i].len);
        }

        // Reset every buffer to 0 to prevent accidental data contamination
        memset(event_type, 0, sizeof(uint8_t) * MAX_REPORT_COUNT);
        memset(addr_type, 0, sizeof(uint8_t) * MAX_REPORT_COUNT);
        memset(bdaddr, 0, sizeof(bd_addr_t) * MAX_REPORT_COUNT);
        memset(report_data_len, 0, sizeof(uint8_t) * MAX_REPORT_COUNT);
        memset(names, 0, sizeof(char*) * MAX_REPORT_COUNT);
        memset(hci_data, 0, sizeof(hci_data_t));
        report_data_total = 0;
    }

free_heap:
    free(event_type); event_type = NULL;
    free(addr_type); addr_type = NULL;
    free(bdaddr); bdaddr = NULL;
    free(report_data_len); report_data_len = NULL;
    free(names); names = NULL;
    free(hci_data->data);
    free(hci_data);
}

void app_main(void)
{
    esp_err_t errCode = ESP_OK;
    static uint8_t hci_message[HCI_EVENT_MAX_SIZE];

    /* Transmit the startup time */
    int64_t start_time = esp_timer_get_time();
    esp_rom_printf("Capture started at: %lu\n", (unsigned long)(start_time / 1000));

    /* Initialise NVS - used to store PHY calibration data */
    errCode = nvs_flash_init();
    if (errCode == ESP_ERR_NVS_NO_FREE_PAGES || errCode == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        errCode = nvs_flash_init();
    }
    ESP_ERROR_CHECK(errCode);

    /* Configure UART */
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, HCI_BUFFER_SIZE * HCI_EVENT_MAX_SIZE, HCI_BUFFER_SIZE * HCI_EVENT_MAX_SIZE, HCI_BUFFER_SIZE, &uart_queue, 0));

    /* Initialise Bluetooth */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    /* Release the heap of Bluetooth Classic as we won't need it */
    errCode = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (errCode != ESP_OK) {
        ESP_LOGI(TAG, "Bluetooth controller release BT CLASSIC memory failed: %s", esp_err_to_name(errCode));
        return;
    }

    /* Set up Bluetooth resources */
    errCode = esp_bt_controller_init(&bt_cfg);
    if (errCode != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller initialisation failed: %s", esp_err_to_name(errCode));
        return;
    }

    /* Set up Bluetooth Low Energy mode and enable controller */
    errCode = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (errCode != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Bluetooth Low Energy controller: %s", esp_err_to_name(errCode));
        return;
    }

    /* A queue for storing the received HCI packets */
    adv_queue = xQueueCreate(15, sizeof(hci_data_t));
    if (adv_queue == NULL) {
        ESP_LOGE(TAG, "Cannot create HCI IN queue");
        return;
    }

    /* Create the HCI buffer */
    hci_buffer = (uint8_t *)malloc(sizeof(uint8_t) * HCI_BUFFER_SIZE * HCI_EVENT_MAX_SIZE);
    if (hci_buffer == NULL) {
        ESP_LOGE(TAG, "Cannot allocate heap for HCI buffer.");
        return;
    }

    esp_vhci_host_register_callback(&vhci_host_cb);

    // Init Bluetooth procedures in a while loop with vTaskDelay to circumnavigate the watchdog timeouts
    bool ble_scan_initialising = true;
    int ble_scan_init_step = 0;
    while (ble_scan_initialising) {
        if (ble_scan_initialising && esp_vhci_host_check_send_available()) {
            uint16_t size;
            switch (ble_scan_init_step) {
                case 0:
                    ESP_LOGI(TAG, "Resetting Bluetooth controller");
                    size = make_cmd_reset(hci_message);
                    esp_vhci_host_send_packet(hci_message, size);
                    break;
                case 1:
                    ESP_LOGI(TAG, "Applying HCI event mask");
                    // Enable only LE Meta events => bit 61
                    uint8_t evt_mask[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
                    size = make_cmd_set_evt_mask(hci_message, evt_mask);
                    esp_vhci_host_send_packet(hci_message, size);
                    break;
                case 2:
                    ESP_LOGI(TAG, "Setting up BLE Scan parameters");
                    // Set up the passive scan
                    uint8_t scan_type = 0x00;

                    // Interval and Window are set in terms of number of slots (625 microseconds)
                    uint16_t scan_interval = 0x50;  // How often to scan
                    uint16_t scan_window = 0x50;    // How long to scan

                    uint8_t own_addr_type = 0x00;   // Public device address
                    uint8_t filter_policy = 0x00;   // Do not further filter any packets

                    size = make_cmd_ble_set_scan_params(hci_message, scan_type, scan_interval, scan_window, own_addr_type, filter_policy);
                    esp_vhci_host_send_packet(hci_message, size);
                    break;
                case 3:
                    ESP_LOGI(TAG, "Locking the BLE Scanning to channel %u", CHANNEL);
                    btdm_scan_channel_setting(CHANNEL);
                    esp_rom_printf("Locked to channel: %u\n", CHANNEL);
                    break;
                case 4: // Start the control thread
                    // FreeRTOS unrestricted task in ESP modification
                    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html
                    // Task stack size - 2048B - taken from ESP HCI example
                    // Priority 6 - taken from ESP HCI example
                    // Pinned to core 0 - taken from ESP HCI example
                    xTaskCreatePinnedToCore(&hci_evt_process, "Process HCI Event", 2048, NULL, 6, NULL, 0);
                    break;
                case 5: // Start BLE Scan
                    ESP_LOGI(TAG, "Starting BLE Scanning");
                    uint8_t scan_enable = 0x01;
                    uint8_t scan_filter_dups = 0x00;    // Disable duplicates filtering
                    size = make_cmd_ble_set_scan_enable(hci_message, scan_enable, scan_filter_dups);
                    esp_vhci_host_send_packet(hci_message, size);
                    ble_scan_initialising = false;
                    break;
                default:
                    ble_scan_initialising = false;
                    break;
            }
            ble_scan_init_step++;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Watchdog
    }
}