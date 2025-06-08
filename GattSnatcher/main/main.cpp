#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/queue.h>

#include "struct_and_definitions.h"
#include <esp_littlefs.h>
#include <stdint.h>
#include "device_scanner.h"
#include "device_interrogator.h"
#include "esp_log.h"

#if defined(CONFIG_DEVICE_ROLE_COLLECTOR) && defined(CONFIG_DEVICE_ROLE_QUESTIONER)
#error "Only one of CHIP_ROLE_QUESTIONER or CHIP_ROLE_COLLECTOR may be defined"
#endif

#define USE_UART_OUTPUT //comment out to send output of #1 to ROM
static const char *TAG = "MAIN";

esp_err_t initializeFs() {
    ESP_LOGI(TAG, "Initializing FS ...");

    esp_err_t ret;
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    if ((ret = esp_vfs_littlefs_register(&conf)) != ESP_OK) {
          if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount or format filesystem");
          } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to find LittleFS partition");
          } else {
                  ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
          }
          return ret;
    }
    size_t total = 0, used = 0;
    ERR_GUARD(esp_littlefs_info(conf.partition_label, &total, &used));
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d (%d %%)", total, used, 100 * used / total);

    // ESP_ERROR_CHECK(FileSystemUtils::printFiles(ProjectConfig::DataPartitionMountPoint, true));

    return ESP_OK;
}






extern "C" void app_main(void){
      ESP_LOGI(TAG,"APP MAIN started");
      initializeFs();
#if CONFIG_DEVICE_ROLE_COLLECTOR
      ESP_LOGI(TAG,"CHIP ROLE COLLECTOR started");
      DeviceScanner::getInstance().mainFunction();
#elif CONFIG_DEVICE_ROLE_QUESTIONER
      ESP_LOGI(TAG,"CHIP ROLE QUESTIONER started");
      DeviceInterrogator::getInstance().mainFunction();
#else
#error "No device role selected! Check your Kconfig."
#endif

}

