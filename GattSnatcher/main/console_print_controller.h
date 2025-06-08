#pragma once
#include "struct_and_definitions.h"
#include "output_handler.h"
#include <esp_err.h>

class ConsolePrintController : public OutputHandler {
public:
    static ConsolePrintController* getInstance();
    esp_err_t init(bool isInterrogator) override;
    esp_err_t printAdvertisingSingleReport(const LeAdvertisingSingleReport &report, int64_t timestamp) override;
    esp_err_t printAdvertisingReport(const LeAdvertisingReport &advReport) override;
    esp_err_t printString(const std::string& string) override;
    // esp_err_t printPacketInfo(hci_data_t hciData) override;
    void printGattProfileJson(int APP_ID, const gattc_profile_inst* gl_profile_tab);

private:
    ConsolePrintController();
};
