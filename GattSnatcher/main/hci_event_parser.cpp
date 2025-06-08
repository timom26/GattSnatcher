#include <cstdlib>  // for atoi, atoll
#include "struct_and_definitions.h"
#include "hci_event_parser.h"
#include "esp_log.h"
#include <cstring>
#include <device_scanner.h>
#include <esp_err.h>

#include "bt_hci_common.h"

static const char *TAG = "HCI EVENT PARSER";

esp_err_t HciEventParser::fillAdvReport(hci_data_t & hciData, LeAdvertisingReport &advReport) {
    // ESP_LOGI(TAG, "Starting AdvReport");

    if (hciData.data == NULL || hciData.len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *cursor = hciData.data;
    uint8_t *end = hciData.data + hciData.len; // pointer to the end of the data buffer

    // Check for H4_TYPE_EVENT (1 byte)
    // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
    if (*cursor != H4_TYPE_EVENT) {
        return ESP_ERR_NOT_FOUND;
    }
    cursor++; // Skip H4 header

    // Check for LE_META_EVENTS (1 byte)
    // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE
    if (*cursor != LE_META_EVENTS) {
        return ESP_ERR_NOT_FOUND;
    }
    cursor++; // Skip event code

    // Skip parameter total length (1 byte)
    // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
    cursor++;

    // Check for HCI_LE_ADV_REPORT (1 byte)
    // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
    if (*cursor != HCI_LE_ADV_REPORT) {
        return ESP_ERR_NOT_FOUND;
    }
    cursor++; // Skip subevent code

    advReport.timestamp = hciData.timestamp;

    // Get report count (1 byte)
    // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
    uint8_t report_count = *cursor;
    if (report_count < 1 || report_count > MAX_NUM_REPORTS) {
        ESP_LOGE(TAG, "Invalid report count: %u", report_count);
        return ESP_FAIL;
    }
    advReport.num_reports = report_count;
    cursor++; // Skip report count

    for (uint8_t i = 0; i < report_count; i++) {
        // Advertising event type (1 byte)
        // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
        advReport.reports[i].adv_event_type = *cursor;
        cursor++;

        // Address type (1 byte)
        // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
        advReport.reports[i].addr_type = (esp_ble_addr_type_t) *cursor;
        cursor++;

        // Bluetooth address (6 bytes)
        // if (cursor + 6 > end) return ESP_ERR_INVALID_SIZE;
        sprintf(advReport.reports[i].bdaddr_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                cursor[5], cursor[4], cursor[3], cursor[2], cursor[1], cursor[0]);
        // Copy raw 6-byte BD_ADDR
        memcpy(advReport.reports[i].raw_bdaddr, cursor, 6);
        cursor += 6;
        // Advertisement data length (1 byte)
        // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
        advReport.reports[i].adv_data_length = *cursor;
        cursor++;

        // Advertisement data (variable length)
        // if (cursor + advReport.reports[i].adv_data_length > end) return ESP_ERR_INVALID_SIZE;
        if (advReport.reports[i].adv_data_length > sizeof(advReport.reports[i].adv_data)) {
            advReport.reports[i].adv_data_length = sizeof(advReport.reports[i].adv_data);
        }
        memcpy(advReport.reports[i].adv_data, cursor, advReport.reports[i].adv_data_length);
        cursor += advReport.reports[i].adv_data_length;

        // RSSI (1 byte)
        // if (cursor + 1 > end) return ESP_ERR_INVALID_SIZE;
        advReport.reports[i].rssi = (int8_t)*cursor;
        cursor++;
    }

    return ESP_OK;
}


bool HciEventParser::parseAdvReportFromString(const char *adv_str, LeAdvertisingSingleReportWithTimestamp &report) {
    // Copy input since strtok_r modifies the string
    char buf[256];
    strncpy(buf, adv_str, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';

    char *token = nullptr;
    char *rest = buf;

    // Parse timestamp
    token = strtok_r(rest, ",", &rest);
    if (!token) return false;
    report.timestamp = static_cast<int64_t>(atoll(token));

    // Parse adv_event_type
    token = strtok_r(nullptr, ",", &rest);
    if (!token) return false;
    report.adv_event_type = static_cast<uint8_t>(atoi(token));

    // Parse addr_type
    token = strtok_r(nullptr, ",", &rest);
    if (!token) return false;
    report.addr_type = static_cast<esp_ble_addr_type_t>(atoi(token));

    // Parse bdaddr_str
    token = strtok_r(nullptr, ",", &rest);
    if (!token) return false;
    strncpy(report.bdaddr_str, token, sizeof(report.bdaddr_str));
    report.bdaddr_str[sizeof(report.bdaddr_str)-1] = '\0';

    // Parse adv_data_length
    token = strtok_r(nullptr, ",", &rest);
    if (!token) return false;
    report.adv_data_length = static_cast<uint8_t>(atoi(token));

    // Parse rssi
    token = strtok_r(nullptr, ",", &rest);
    if (!token) return false;
    report.rssi = static_cast<int8_t>(atoi(token));

    // Parse optional filename (rest of string, no commas in filename)
    if (rest && *rest != '\0') {
        size_t len = strlen(rest);
        // Trim trailing newline if present
        if (len > 0 && rest[len-1] == '\n') rest[len-1] = '\0';
        strncpy(report.advertisementFilename, rest, sizeof(report.advertisementFilename));
        report.advertisementFilename[sizeof(report.advertisementFilename)-1] = '\0';
    } else {
        report.advertisementFilename[0] = '\0';
    }

    return true;
}