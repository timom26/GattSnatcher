#pragma once
#include <struct_and_definitions.h>
#include <cstdint>
#include <cstdio>
#include <esp_bt_defs.h>
#include <esp_err.h>


class HciEventParser {
public:
    static esp_err_t fillAdvReport(hci_data_t & hciData ,LeAdvertisingReport &advReport);
    static bool parseAdvReportFromString(const char *adv_str, LeAdvertisingSingleReportWithTimestamp &report);

};
