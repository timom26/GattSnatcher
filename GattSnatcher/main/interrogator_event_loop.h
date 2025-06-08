#pragma once
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <hci_event_parser.h>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/queue.h>
#include "hci_event_parser.h"
#include <deque>
#include "freertos/semphr.h"
#define PROFILE_NUM 3
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1
#define PROFILE_C_APP_ID 2
#define PROFILE_D_APP_ID 3
#define INVALID_HANDLE   0
#define MAX_SERVICES_PER_PROFILE 10
#define MAX_CHARACTERISTICS_IN_SERVICE 16
#define MAX_CHARACTERISTIC_VALUE_SIZE 16

const char* esp_gatt_status_to_str(esp_gatt_status_t status);




class InterrogatorEventLoop {
    QueueHandle_t scannedQueue;
public:
    // static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
    // static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
    static void gattc_profile_universal_event_handler(int APP_ID, esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

    static void gattc_profile_a_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
    static void gattc_profile_b_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
    static void gattc_profile_c_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
    static void gattc_profile_d_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

};

