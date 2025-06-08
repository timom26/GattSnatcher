
#include "console_print_controller.h"
#include <esp_log.h>
#include <hci_event_parser.h>
#include <cstdio>
#include <device_scanner.h>
#include <iomanip>
#include <ios>

static const char *TAG = "CONSOLEPRINT";

ConsolePrintController* ConsolePrintController::getInstance() {
    static ConsolePrintController instance;
    return &instance;
}

ConsolePrintController::ConsolePrintController() = default;

esp_err_t ConsolePrintController::init(bool isInterrogator) {
    ESP_LOGI(TAG, "using the console print controller");
    return ESP_OK;
}

esp_err_t ConsolePrintController::printAdvertisingSingleReport(const LeAdvertisingSingleReport &report, int64_t timestamp) {
    if (VERBOSE_PRINT) {
        ESP_LOGI(TAG, "Advertising Report:");
        ESP_LOGI(TAG, "  Event Type: 0x%02x", report.adv_event_type);
        ESP_LOGI(TAG, "  Address Type: 0x%02x", report.addr_type);
        ESP_LOGI(TAG, "  Bluetooth Address: %s", report.bdaddr_str);
        ESP_LOGI(TAG, "  Advertising Data Length: %u", report.adv_data_length);

        std::string adv_data_str;
        for (uint8_t i = 0; i < report.adv_data_length; ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", report.adv_data[i]);
            adv_data_str += buf;
        }
        ESP_LOGI(TAG, "  Advertising Data: %s", adv_data_str.c_str());
        ESP_LOGI(TAG, "  RSSI: %d", report.rssi);
        ESP_LOGI(TAG, "-------------------------");
    } else {
        ESP_LOGI(TAG, "%lld,%d,%d,%s,%d,%d",
                 timestamp,
                 report.adv_event_type,
                 report.addr_type,
                 report.bdaddr_str,
                 report.adv_data_length,
                 report.rssi);
    }
    return ESP_OK;
}

esp_err_t ConsolePrintController::printAdvertisingReport(const LeAdvertisingReport &advReport) {
    ESP_LOGI(TAG, "LE Advertising Report:");
    ESP_LOGI(TAG, "Timestamp: %lld", advReport.timestamp);
    ESP_LOGI(TAG, "Number of Reports: %u", advReport.num_reports);
    for (uint8_t i = 0; i < advReport.num_reports; ++i) {
        ERR_GUARD(printAdvertisingSingleReport(advReport.reports[i], advReport.timestamp));
    }
    return ESP_OK;
}

esp_err_t ConsolePrintController::printString(const std::string& string) {
    ESP_LOGI(TAG, "%s", string.c_str());
    return ESP_OK;
}

// esp_err_t ConsolePrintController::printPacketInfo(hci_data_t hciData) {
//     std::string line = "BLE:" + std::to_string(hciData.timestamp) + "," + std::to_string(hciData.len) + ",";
//     for (uint16_t i = 0; i < hciData.len; ++i) {
//         char buf[3];
//         snprintf(buf, sizeof(buf), "%02x", hciData.data[i]);
//         line += buf;
//     }
//     ESP_LOGI(TAG, "%s", line.c_str());
//     return ESP_OK;
// }

void ConsolePrintController::printGattProfileJson(int APP_ID, const gattc_profile_inst* gl_profile_tab) {
    const auto& profile = gl_profile_tab[APP_ID];
    std::ostringstream oss;

    oss << "{\n";
    oss << "  \"remote_bda\": \"";
    for (int i = 0; i < 6; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)profile.remote_bda[i];
        if (i < 5) oss << ":";
    }
    oss << std::dec << "\",\n";
    oss << "  \"advertisement_filename\": \"" << profile.interrogation_request.advertisementFilename << "\",\n";
    oss << "  \"interrogation_timestamp\": " << profile.interrogation_request.timestamp << ",\n";
    oss << "  \"services\": [\n";
    for (size_t si = 0; si < profile.services.size(); ++si) {
        const auto& srv = profile.services[si];
        oss << "    {\n";

        if (srv.service.id.uuid.len == ESP_UUID_LEN_16) {
            oss << "      \"uuid\": \"0x"
                << std::hex << srv.service.id.uuid.uuid.uuid16 << std::dec << "\",\n";
        } else {
            oss << "      \"uuid128\": [";
            for (int b = 0; b < 16; ++b) {
                oss << (int)srv.service.id.uuid.uuid.uuid128[b];
                if (b < 15) oss << ", ";
            }
            oss << "],\n";
        }

        oss << "      \"start_handle\": " << srv.range.start_handle << ",\n";
        oss << "      \"end_handle\": " << srv.range.end_handle << ",\n";

        oss << "      \"characteristics\": [\n";
        for (size_t ci = 0; ci < srv.chars.size(); ++ci) {
            const auto& cw = srv.chars[ci];
            oss << "        {\n";

            if (cw.meta.uuid.len == ESP_UUID_LEN_16) {
                oss << "          \"uuid\": \"0x"
                    << std::hex << cw.meta.uuid.uuid.uuid16 << std::dec << "\",\n";
            } else {
                oss << "          \"uuid128\": [";
                for (int b = 0; b < 16; ++b) {
                    oss << (int)cw.meta.uuid.uuid.uuid128[b];
                    if (b < 15) oss << ", ";
                }
                oss << "],\n";
            }

            oss << "          \"handle\": " << cw.meta.char_handle << ",\n";
            oss << "          \"properties\": " << (int)cw.meta.properties << ",\n";

            oss << "          \"value\": [";
            for (size_t v = 0; v < cw.value.size(); ++v) {
                oss << (int)cw.value[v];
                if (v + 1 < cw.value.size()) oss << ", ";
            }
            oss << "]\n";

            oss << "        }" << (ci + 1 < srv.chars.size() ? "," : "") << "\n";
        }
        oss << "      ]\n";
        oss << "    }" << (si + 1 < profile.services.size() ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";

    ESP_LOGI(TAG, "GATT Profile JSON of APP_ID %d:\n%s",APP_ID, oss.str().c_str());
}