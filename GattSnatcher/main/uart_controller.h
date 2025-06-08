#pragma once
#include "struct_and_definitions.h"
#include <output_handler.h>
#include <string>
#include <hal/uart_types.h>
//---------------------------------------------------------
// UART Controller Implementation
//---------------------------------------------------------
class UartController : public OutputHandler {
public:
    uart_port_t _uart_num;
    // UartController();
    // UartController(const UartController&) = delete;             // Copy ctor
    // UartController(UartController&&) = delete;                  // Move ctor
    // UartController& operator=(const UartController&) = delete;  // Copy assignment
    // UartController& operator=(UartController&&) = delete;       // Move assignment
    static UartController * getInstance();

    esp_err_t init(bool isInterrogator) override;

    esp_err_t printAdvertisingSingleReport(const LeAdvertisingSingleReport &report, int64_t timestamp) override;
    esp_err_t printAdvertisingReport(const LeAdvertisingReport &advReport) override;
    esp_err_t printString(const std::string& string) override;
    // esp_err_t printPacketInfo(hci_data_t hciData) override;
    void setCurrentlyUsedFilename(char * filename);
    char * currentlyUsedFilename;

private:
    UartController() : _uart_num(CONFIG_UART_PORT_NUM) {} // Private constructor for singleton
    uart_config_t _uartConfig = {
        .baud_rate = CONFIG_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
};

