#include "rom_print_controller.h"
#include <cstdio>
#include <cstring>
#include <device_interrogator.h>
#include <string>
#include <sys/stat.h>

#include <esp_log.h>
#include <hci_event_parser.h>
#include <iomanip>
#include <ios>
static const char *TAG = "ROMPRINT";

#define NUMBER_OF_ADVERTISEMENTS_TO_FLASH_AFTER 256

FilePrintController * FilePrintController::getInstance() {
    static FilePrintController instance = {};
    return &instance;
}
// FilePrintController class member variable
FilePrintController::FilePrintController() = default;

esp_err_t FilePrintController::init(bool isInterrogator) {
    ESP_LOGI(TAG,"using the rom print controller");
    const char* base_filename;
    if (isInterrogator)
    {
       base_filename  = "/storage/interrogator_log";
    }else
    {
        base_filename = "/storage/scanner_log";

    }
    const char* extension = ".bin";
    int index = 0;


    do {
        snprintf(filename, sizeof(filename), "%s_%d%s", base_filename, index, extension);
        ESP_LOGI(TAG, "Testing filename: %s", filename);
        struct stat buffer;
        if (stat(filename, &buffer) != 0) {
            break;
        }
        index++;
    } while (index < 1000);

    _outputFile = fopen(filename, "w");
    if (_outputFile == nullptr) {
        ESP_LOGE(TAG, "Failed to create output file");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File successfully opened");
    return ESP_OK;
}

esp_err_t FilePrintController::printAdvertisingSingleReport(const LeAdvertisingSingleReport &report, int64_t timestamp) {
    if (__builtin_expect(_outputFile == nullptr,false))
    {
        ESP_LOGE(TAG, "FILE is not initialized!!");
        return ESP_FAIL;
    }
    static int flip = 0;
    // --- build a 16-byte header ---
    uint8_t hdr[16];
    uint64_t ts = (uint64_t)timestamp;
    // copy only the low 48 bits of ts
    for (int i = 0; i < 6; ++i) {
        hdr[i] = ts & 0xFF;
        ts >>= 8;
    }
    hdr[6] = report.adv_event_type;
    hdr[7] = report.addr_type;
    // raw MAC must be in report.bdaddr[6]
    memcpy(hdr + 8, report.raw_bdaddr, 6);
    hdr[14] = report.adv_data_length;
    hdr[15] = (uint8_t)report.rssi;

    // single atomic write of header
    size_t written = fwrite(hdr, 1/*sizeof(uint8_t)*/, sizeof(hdr), _outputFile);
    if (__builtin_expect(written != sizeof(hdr),false)) {
        ESP_LOGE(TAG, "Failed writing LittleFS record header: expected %u bytes, wrote %u bytes", sizeof(hdr), written);
        return ESP_FAIL;
    }
    fflush(_outputFile);
    if (flip == NUMBER_OF_ADVERTISEMENTS_TO_FLASH_AFTER)
    {
        flip = 0;
        ESP_LOGI(TAG,"fsync!");
        fsync(fileno(_outputFile));
    }else
    {
        flip++;
    }
        // fprintf(_outputFile, "%lld,%d,%d,%s,%d,%d\n",
        //                    timestamp,
        //                    report.adv_event_type,
        //                    report.addr_type,
        //                    report.bdaddr_str,
        //                    report.adv_data_length,
        //                    report.rssi);
    return ESP_OK;
}

esp_err_t FilePrintController::printAdvertisingReport(const LeAdvertisingReport &advReport) {

    for (uint8_t i = 0; i < advReport.num_reports; i++) {
        ERR_GUARD(printAdvertisingSingleReport(advReport.reports[i], advReport.timestamp));
    }
    return ESP_OK;
}

esp_err_t FilePrintController::printString(const std::string& string)
{
    fprintf(_outputFile, "%s", string.c_str());
    fflush(_outputFile);
    return ESP_OK;
}

void FilePrintController::printGattProfileJson(int APP_ID, const gattc_profile_inst* gl_profile_tab)
{
    const auto& profile = gl_profile_tab[APP_ID];

    fprintf(_outputFile, "{\n");
    fprintf(_outputFile, "  \"remote_bda\": \"");
    for (int i = 0; i < 6; ++i) {
        fprintf(_outputFile, "%02x", profile.remote_bda[i]);
        if (i < 5) fprintf(_outputFile, ":");
    }
    fprintf(_outputFile, "\",\n");
    fprintf(_outputFile, "  \"advertisement_filename\": \"%s\",\n", profile.interrogation_request.advertisementFilename);
    fprintf(_outputFile, "  \"interrogation_timestamp\": %lld,\n", profile.interrogation_request.timestamp);
    fprintf(_outputFile, "  \"services\": [\n");

    for (size_t si = 0; si < profile.services.size(); ++si) {
        const auto& srv = profile.services[si];
        fprintf(_outputFile, "    {\n");

        if (srv.service.id.uuid.len == ESP_UUID_LEN_16) {
            fprintf(_outputFile, "      \"uuid\": \"0x%04x\",\n", srv.service.id.uuid.uuid.uuid16);
        } else {
            fprintf(_outputFile, "      \"uuid128\": [");
            for (int b = 0; b < 16; ++b) {
                fprintf(_outputFile, "%d", srv.service.id.uuid.uuid.uuid128[b]);
                if (b < 15) fprintf(_outputFile, ", ");
            }
            fprintf(_outputFile, "],\n");
        }

        fprintf(_outputFile, "      \"start_handle\": %d,\n", srv.range.start_handle);
        fprintf(_outputFile, "      \"end_handle\": %d,\n", srv.range.end_handle);
        fprintf(_outputFile, "      \"characteristics\": [\n");

        for (size_t ci = 0; ci < srv.chars.size(); ++ci) {
            const auto& cw = srv.chars[ci];
            fprintf(_outputFile, "        {\n");

            if (cw.meta.uuid.len == ESP_UUID_LEN_16) {
                fprintf(_outputFile, "          \"uuid\": \"0x%04x\",\n", cw.meta.uuid.uuid.uuid16);
            } else {
                fprintf(_outputFile, "          \"uuid128\": [");
                for (int b = 0; b < 16; ++b) {
                    fprintf(_outputFile, "%d", cw.meta.uuid.uuid.uuid128[b]);
                    if (b < 15) fprintf(_outputFile, ", ");
                }
                fprintf(_outputFile, "],\n");
            }

            fprintf(_outputFile, "          \"handle\": %d,\n", cw.meta.char_handle);
            fprintf(_outputFile, "          \"properties\": %d,\n", cw.meta.properties);

            fprintf(_outputFile, "          \"value\": [");
            for (size_t v = 0; v < cw.value.size(); ++v) {
                fprintf(_outputFile, "%d", cw.value[v]);
                if (v + 1 < cw.value.size()) fprintf(_outputFile, ", ");
            }
            fprintf(_outputFile, "]\n");

            fprintf(_outputFile, "        }%s\n", (ci + 1 < srv.chars.size()) ? "," : "");
        }

        fprintf(_outputFile, "      ]\n");
        fprintf(_outputFile, "    }%s\n", (si + 1 < profile.services.size()) ? "," : "");
        fsync(fileno(_outputFile));
    }

    fprintf(_outputFile, "  ]\n");
    fprintf(_outputFile, "}\n");
    fsync(fileno(_outputFile));
    ESP_LOGI(TAG, "fsync from json complete!");
}
char * FilePrintController::getFilename()
{
    return filename;
}