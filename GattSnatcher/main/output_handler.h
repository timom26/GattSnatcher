#pragma once
#include <struct_and_definitions.h>
#include <cstdint>
#include <esp_err.h>
#include <string>

static const bool VERBOSE_PRINT = false;//TODO make into menuconfig param
// Insert the following definition before the class OutputHandler declaration:
typedef struct {
    uint32_t uart_baud_rate;
    uint8_t uart_port;
    bool verbose;
} OutputHandlerConfig;

class OutputHandler {
public:
    virtual ~OutputHandler() {}
    virtual esp_err_t init(bool isInterrogator) = 0;    // Initialize output with configuration
    virtual esp_err_t printAdvertisingSingleReport(const LeAdvertisingSingleReport &report, int64_t timestamp) = 0;     // Print a single advertising report
    virtual esp_err_t printAdvertisingReport(const LeAdvertisingReport &report) = 0;    // Print the entire advertising report (which contains one or more single reports)
    virtual esp_err_t printString(const std::string& string) = 0;
    // virtual esp_err_t printPacketInfo(hci_data_t hciData) = 0;

    // static OutputHandler * getInstance();    // Get the singleton instance (factory based on macro switch)
};
