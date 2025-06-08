#pragma once
#include "struct_and_definitions.h"
#include <rom_print_controller.h>
#include <uart_controller.h>
#include <cstring>
#include <esp_bt.h>
#include <mac_cache.h>


#include "driver/uart.h"


static const uint8_t MONITORED_CHANNEL = 37;



class DeviceScanner {
    DeviceScanner();
    QueueHandle_t _uart_queue;
    QueueHandle_t _adv_queue;

    MacCache _macCache;
    // Buffer for HCI events;
    uint8_t *_hci_buffer = NULL;
    uint8_t _hci_buffer_idx = 0;
    hci_data_t _hci_data;
    uint8_t _hci_message[HCI_EVENT_MAX_SIZE];
    uint16_t _size;

    bool _ble_scan_initialising = true;
    UartController * _uart;
    FilePrintController * _rom;

    esp_err_t transmitStartupTime();
    esp_err_t initNvsFlash();
    esp_err_t initOutputHandler();
    esp_err_t releaseBluetoothClassicHeap();
    esp_err_t initBluetoothController(esp_bt_controller_config_t & bt_cfg);
    esp_err_t initAdvQueue();
    esp_err_t initHciBuffer();
    esp_err_t registerVhciHostCallback();
    esp_err_t resetBluetoothController();
    esp_err_t applyHciEventMask();
    esp_err_t startBleScan();
    esp_err_t setMonitoredChannel(uint8_t monitoredChannel);
    esp_err_t startControlThread();
    esp_err_t setUpBleScan();

    /*
 * @brief: Callback function of Bluetooth controller used to notify that the controller has a packet to send to the host.
 */
    int controllerOutRdy(uint8_t *data, uint16_t len);
    static int controllerOutRdyWrapper(uint8_t *data, uint16_t len);
    void hciEvtProcess(void *pvParameters);
    static void hciEvtProcessWrapper(void *pvParameters);
    esp_err_t zeroHciDataMemory();

    char * currentlyUsedFilename;
public:
    DeviceScanner(const DeviceScanner&) = delete;             // Copy ctor
    DeviceScanner(DeviceScanner&&) = delete;                  // Move ctor
    DeviceScanner& operator=(const DeviceScanner&) = delete;  // Copy assignment
    DeviceScanner& operator=(DeviceScanner&&) = delete;       // Move assignment
    static DeviceScanner& getInstance();

    esp_err_t mainFunction();
};
