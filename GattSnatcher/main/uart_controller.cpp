#include "uart_controller.h"

#include <constants.h>
#include <cstring>
#include <esp_event.h>
#include <esp_log.h>
#include "driver/uart.h"
#include <string>
#include "collector_utils.h"

static const char *TAG = "UART_CTRL";

UartController * UartController::getInstance()
{
    static UartController instance = {};
    return &instance;
}

esp_err_t UartController::init(bool isInterrogator)  {
    ESP_LOGI(TAG,"using the uart controller");

    ERR_GUARD_LOGE(uart_param_config(_uart_num, &_uartConfig), "uart_param_config failed");
    ERR_GUARD_LOGE(uart_set_pin(
        _uart_num,
#if(CONFIG_DEVICE_ROLE_COLLECTOR)
        CONFIG_UART_PIN_TX,
        CONFIG_UART_PIN_RX,
#elif(CONFIG_DEVICE_ROLE_QUESTIONER)
        CONFIG_UART_PIN_RX,
        CONFIG_UART_PIN_TX,
#endif
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
        ), "uart_set_pin failed");
    ERR_GUARD_LOGE(uart_driver_install(
        _uart_num,
        HCI_BUFFER_SIZE * HCI_EVENT_MAX_SIZE,
        HCI_BUFFER_SIZE * HCI_EVENT_MAX_SIZE,
        HCI_BUFFER_SIZE,
        nullptr,
        0
        ),"uart_driver_install failed");
    return ESP_OK;
}

esp_err_t UartController::printAdvertisingSingleReport(const LeAdvertisingSingleReport &report, int64_t timestamp)  {
        //<timestamp>,<adv_event_type>,<addr_type>,<bdaddr>,<adv_data_length>,<rssi>
        //ADV:1623456789,0,0,AA:BB:CC:DD:EE:FF,0,-65
        char formatted_str[256];
        if (!currentlyUsedFilename)
        {
            snprintf(formatted_str, sizeof(formatted_str), "%lld,%d,%d,%s,%d,%d,-\n",
             timestamp,
             report.adv_event_type,
             report.addr_type,
             report.bdaddr_str,
             report.adv_data_length,
             report.rssi);
        }else
        {
            snprintf(formatted_str, sizeof(formatted_str), "%lld,%d,%d,%s,%d,%d%s\n",
             timestamp,
             report.adv_event_type,
             report.addr_type,
             report.bdaddr_str,
             report.adv_data_length,
             report.rssi,
             currentlyUsedFilename);
        }

        uart_print(_uart_num, formatted_str);
    return ESP_OK;
}

esp_err_t UartController::printAdvertisingReport(const LeAdvertisingReport &advReport)  {

    for (uint8_t i = 0; i < advReport.num_reports; i++) {
        ERR_GUARD(printAdvertisingSingleReport(advReport.reports[i], advReport.timestamp));
    }
    return ESP_OK;
}

esp_err_t UartController::printString(const std::string& string)
{
    uart_print(_uart_num, string.c_str());
    return ESP_OK;
}

// esp_err_t UartController::printPacketInfo(hci_data_t hciData) {
//     uart_print(_uart_num, "BLE:");
//     uart_print(_uart_num, "%lld,", hciData.timestamp);
//     uart_print(_uart_num, "%u,", hciData.len);
//     for (uint16_t i = 0; i < hciData.len; i++) {
//         uart_print(_uart_num, "%02x", hciData.data[i]);
//     }
//     uart_print(_uart_num, "\n");
//     return ESP_OK;
// }

void UartController::setCurrentlyUsedFilename(char * filename)
{
    currentlyUsedFilename = filename;
}