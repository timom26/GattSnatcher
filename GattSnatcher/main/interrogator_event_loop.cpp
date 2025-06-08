#include "interrogator_event_loop.h"

#include <algorithm>

#include "esp_log.h"
#include "esp_err.h"
#include <cstring>
#include <device_database.h>
#include <device_interrogator.h>




static const char *TAG = "INTER_EV_LOOP";
static void interrogateProfile(void *pvParameters);
bool esp_bt_uuid_cmp(const esp_bt_uuid_t *p_uuid1, const esp_bt_uuid_t *p_uuid2);
static void print_char_properties(uint8_t props);


void InterrogatorEventLoop::gattc_profile_universal_event_handler(int APP_ID, esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{

    // ESP_LOGI(TAG,"loop %d, event=%d",APP_ID,event);
    // static const int APP_ID = PROFILE_A_APP_ID;
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;
    DeviceInterrogator &interrogator = DeviceInterrogator::getInstance();
    auto &profile = interrogator.profileTabs[APP_ID];
    esp_err_t mtu_ret;
    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(TAG, "APP_ID %d: REG_EVT", APP_ID);
        break;
    case ESP_GATTC_CONNECT_EVT:
        // L2CAP-level connection event; no status field available here
        ESP_LOGI(TAG, "APP_ID %d: CONNECT_EVT, conn_id=%d, if=%d",
                 APP_ID,
                 p_data->connect.conn_id,
                 gattc_if);
        break;
    case ESP_GATTC_OPEN_EVT:
        ESP_LOGI(TAG, "APP_ID %d: OPEN_EVT", APP_ID);
        if (p_data->open.status != ESP_GATT_OK){
            ESP_LOGE(TAG, "connect device on profile %d failed, status %d",APP_ID, p_data->open.status);
            esp_err_t res = DeviceInterrogator::getInstance().finalProcedure(APP_ID,false);
            if (res != ESP_OK)
            {
                ESP_LOGE(TAG, "finalProcedure from failed OPEN_EVT on profile %d failed, error %d", APP_ID, res);
            }
            break;
        }
        profile.conn_id = p_data->open.conn_id;
        ESP_LOGI(TAG, "ESP_GATTC_OPEN_EVT conn_id %d, if %d, status %d, mtu %d", p_data->open.conn_id, gattc_if, p_data->open.status, p_data->open.mtu);
        ESP_LOGI(TAG, "REMOTE BDA:");
        esp_log_buffer_hex(TAG, p_data->open.remote_bda, sizeof(esp_bd_addr_t));
        mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->open.conn_id);
        if (mtu_ret){
            ESP_LOGE(TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        ESP_LOGI(TAG, "APP_ID %d: CFG_MTU_EVT", APP_ID);
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(TAG,"Config mtu failed");
        }
        ESP_LOGI(TAG, "Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, nullptr));
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
            ESP_LOGI(TAG, "APP_ID %d: SEARCH RES", APP_ID);
            ESP_LOGI(TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
            ESP_LOGI(TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
            ESP_LOGI(TAG, "Discovered service: start handle %d, end handle %d, is_primary %d, UUID len = %d",
                 p_data->search_res.start_handle,
                 p_data->search_res.end_handle,
                 p_data->search_res.is_primary,
                 p_data->search_res.srvc_id.uuid.len);

                ServiceWrapper sw;
            sw.service.id = p_data->search_res.srvc_id;
            sw.service.is_primary = p_data->search_res.is_primary;
            sw.range = ServiceRange{
                p_data->search_res.start_handle,
                p_data->search_res.end_handle
            };
            profile.services.push_back(std::move(sw));
            if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16) {
                ESP_LOGI(TAG, "Service UUID 16-bit = 0x%04x",
                    p_data->search_res.srvc_id.uuid.uuid.uuid16);
              } else {
                  ESP_LOGI(TAG, "Service UUID 128-bit:");
                  esp_log_buffer_hex(TAG,
                      p_data->search_res.srvc_id.uuid.uuid.uuid128, 16);
              }
            break;
    }
     case ESP_GATTC_SEARCH_CMPL_EVT:
         ESP_LOGI(TAG, "APP_ID %d: SEARCH CMPL", APP_ID);
        if (p_data->search_cmpl.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            // Print any partial results and clean up this profile
            DeviceInterrogator::getInstance().finalProcedure(APP_ID, true);
            break;
        }
        if (profile.services.size() > 0) {
            ESP_LOGI(TAG,"got some services while running the search, lets get attributes");
            uint16_t count = 0;
            for (auto &srv : profile.services) {
                uint16_t count = 0;
                esp_err_t st = esp_ble_gattc_get_attr_count(
                    gattc_if,
                    p_data->search_cmpl.conn_id,
                    ESP_GATT_DB_CHARACTERISTIC,
                    srv.range.start_handle,
                    srv.range.end_handle,
                    INVALID_HANDLE,
                    &count
                );
                if (st != ESP_GATT_OK || count == 0) {
                    ESP_LOGW(TAG, "no characteristics in handles %d..%d",
                             srv.range.start_handle, srv.range.end_handle);
                    continue;
                }
                // fetch declaration/value metadata
                uint16_t req = std::min<uint16_t>(count, MAX_CHARACTERISTICS_IN_SERVICE);
                std::vector<esp_gattc_char_elem_t> metas(req);
                esp_err_t ret = esp_ble_gattc_get_all_char(
                    gattc_if,
                    p_data->search_cmpl.conn_id,
                    srv.range.start_handle,
                    srv.range.end_handle,
                    metas.data(),
                    &req,
                    0
                );
                if (ret != ESP_OK){
                    if (ret == ESP_ERR_INVALID_STATE){
                        ESP_LOGE(TAG," Read failed, connection not established.");
                    }else if (ret == ESP_GATT_INVALID_HANDLE){
                        ESP_LOGE(TAG," Read failed, handle invalid");
                    }else if (ret == ESP_FAIL){
                        ESP_LOGE(TAG," Read failed, other reasons");
                    }
                }
                srv.chars.clear();
                srv.chars.reserve(req);
                for (uint16_t i = 0; i < req; ++i) {
                    CharacteristicWrapper cw;
                    cw.meta = metas[i];
                    srv.chars.push_back(std::move(cw));
                    ESP_LOGI(TAG,
                        "Discovered char UUID 0x%04x, handle %d, props 0x%x",
                        metas[i].uuid.uuid.uuid16,
                        metas[i].char_handle,
                        metas[i].properties
                    );
                    print_char_properties(metas[i].properties);
                }
            }
            //after we get list of all characteristics, we want to query their values (if readable)
            //we, however, cannot query their values while we are getting the list of characteristics,
            //and we also cannot query multiple characteristic values at once.
            //therefore we just create this sort of queue for all the requests
            //and then use a semaphore to not query more at once.
            //semaphore is unlocked in read_char_evt
            for (auto &srv : profile.services) {
                for (auto &cw : srv.chars) {
                    uint8_t props = cw.meta.properties;
                    // only queue if the characteristic is READ-only (no other flags)
                    if ((props & ESP_GATT_CHAR_PROP_BIT_READ) &&
                        (props & ~(ESP_GATT_CHAR_PROP_BIT_READ)) == 0) {
                        ESP_LOGI(TAG, "Queuing pure-Read characteristic handle %d", cw.meta.char_handle);
                        profile.read_char_queue.push_back(cw.meta.char_handle);
                    }
                }
            }
            // ensure our semaphore exists and is initially available
            if (profile.characteristicReadSemaphore == NULL) {
                profile.characteristicReadSemaphore = xSemaphoreCreateBinary();
                // give once so the first take will succeed
                xSemaphoreGive(profile.characteristicReadSemaphore);
            }
            // start first read under semaphore (with timeout)
            // serially consume the queue, with a 10 s timeout per read
            ESP_LOGI(TAG, "characteristics read start");
            while (!profile.read_char_queue.empty()) {
                // wait until the prior read has completed (or timeout)
                // if (xSemaphoreTake(profile.characteristicReadSemaphore, profile.read_timeout_ticks) != pdTRUE) {
                //     ESP_LOGE(TAG, "Read timeout while waiting to start next characteristic value read");
                //     // TODO: your device‐timeout handler here
                //     break;
                // }

                // pop the next handle and fire the read
                uint16_t h = profile.read_char_queue.front();
                ESP_LOGI(TAG, "Reading characteristic handle %d …", h);
                TickType_t now = xTaskGetTickCount();
                profile.pending_requests.push_back({ h, now });
                profile.read_char_queue.pop_front();
                esp_ble_gattc_read_char(
                    gattc_if,
                    profile.conn_id,
                    h,
                    ESP_GATT_AUTH_REQ_NONE
                );
                profile.is_char_scheduled = true;
                // now block again until READ_CHAR_EVT gives us the semaphore
            }
        }else{
            ESP_LOGI(TAG, "No attribute search is going to take place");
        }
        break;
    case ESP_GATTC_READ_CHAR_EVT: {
            ESP_LOGI(TAG, "APP_ID %d: READ_CHAR_EVT", APP_ID);
            auto &r = param->read;
            auto it = std::find_if(//remove from pending_requests
                profile.pending_requests.begin(),
                profile.pending_requests.end(),
                [&](auto &pr){ return pr.handle == r.handle; }
            );
            if (it != profile.pending_requests.end()) {
                profile.pending_requests.erase(it);
            }
            if (r.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Read failed, status %s, handle %d", esp_gatt_status_to_str(r.status), r.handle);
            }else
            {
                // 1) Print the handle and raw value
                ESP_LOGI(TAG, "ESP_GATTC_READ_CHAR_EVT, handle = %d, value_len = %d",
                         r.handle, r.value_len);

                for (auto &srv : profile.services)
                {
                    for (auto &cw : srv.chars) {
                        if (cw.meta.char_handle == r.handle) {
                            cw.value.assign(r.value, r.value + r.value_len);
                            ESP_LOGI(TAG, "Stored %d bytes into CharacteristicWrapper.value", r.value_len);
                            break;
                        }
                    }
                }
            }

            xSemaphoreGive(profile.characteristicReadSemaphore);
            break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        ESP_LOGI(TAG, "APP_ID %d: REG_FOR_NOTIFY_EVT", APP_ID);
            break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_NOTIFY_EVT, Receive notify value:", APP_ID);
        esp_log_buffer_hex(TAG, p_data->notify.value, p_data->notify.value_len);
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_WRITE_DESCR_EVT", APP_ID);
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_WRITE_CHAR_EVT", APP_ID);
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(TAG, "Write char failed, error status = %x", p_data->write.status);
        }else{
            ESP_LOGI(TAG, "Write char success");
        }
        interrogator.stop_scan_done = false;
        interrogator.Isconnecting = false;
        break;
    case ESP_GATTC_SRVC_CHG_EVT:
            ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_SRVC_CHG_EVT", APP_ID);
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:%08x%04x",(bda[0] << 24) + (bda[1] << 16) + (bda[2] << 8) + bda[3],
                 (bda[4] << 8) + bda[5]);
        break;
    // case ESP_GATTC_DISCONNECT_EVT:
    //     ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_DISCONNECT_EVT", APP_ID);
    //     if (memcmp(p_data->disconnect.remote_bda, profile.remote_bda, 6) == 0){
    //         DeviceInterrogator::getInstance().finalProcedure(APP_ID,true);
    //     } else {
    //         ESP_LOGW(TAG, "APP_ID %d: DISCONNECT_EVT with non-matching BD_ADDR", APP_ID);
    //         ESP_LOGW(TAG, "Event remote_bda:");
    //         esp_log_buffer_hex(TAG, p_data->disconnect.remote_bda, sizeof(p_data->disconnect.remote_bda));
    //         ESP_LOGW(TAG, "Profile remote_bda:");
    //         esp_log_buffer_hex(TAG, profile.remote_bda, sizeof(profile.remote_bda));
    //     }
    //     break;§
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_DISCONNECT_EVT, evt_conn_id=%d", APP_ID, p_data->disconnect.conn_id);
        if (p_data->disconnect.conn_id == profile.conn_id
            && memcmp(p_data->disconnect.remote_bda, profile.remote_bda, 6) == 0){

            DeviceInterrogator::getInstance().finalProcedure(APP_ID, true);
        }else{// No matching connection ID: log unexpected event
            if (profile.conn_id != 65535){
                ESP_LOGW(TAG, "APP_ID %d: DISCONNECT_EVT for unexpected conn_id %d (profile.conn_id=%d)",
                         APP_ID, p_data->disconnect.conn_id, profile.conn_id);
            }
        }
        break;
    case ESP_GATTC_CLOSE_EVT:
        ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_CLOSE_EVT", APP_ID);
        if (memcmp(p_data->close.remote_bda, profile.remote_bda, 6) == 0){
            ESP_LOGI(TAG, "device on APP_ID %d closed", APP_ID);
        }
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        ESP_LOGI(TAG, "APP_ID %d: ESP_GATTC_DIS_SRVC_CMPL_EVT", APP_ID);
        break;
    default:
        ESP_LOGE(TAG, "unknown event type %d",event );
        break;
    }
}


//##############################



void InterrogatorEventLoop::gattc_profile_a_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    InterrogatorEventLoop::gattc_profile_universal_event_handler(PROFILE_A_APP_ID,event, gattc_if, param);
}
void InterrogatorEventLoop::gattc_profile_b_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    InterrogatorEventLoop::gattc_profile_universal_event_handler(PROFILE_B_APP_ID,event, gattc_if, param);
}
void InterrogatorEventLoop::gattc_profile_c_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    InterrogatorEventLoop::gattc_profile_universal_event_handler(PROFILE_C_APP_ID,event, gattc_if, param);
}
void InterrogatorEventLoop::gattc_profile_d_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    InterrogatorEventLoop::gattc_profile_universal_event_handler(PROFILE_D_APP_ID,event, gattc_if, param);
}


bool esp_bt_uuid_cmp(const esp_bt_uuid_t *p_uuid1, const esp_bt_uuid_t *p_uuid2) {
    // Check if the UUID lengths match
    if (p_uuid1->len != p_uuid2->len) {
        return false;
    }

    // Compare the UUIDs based on their length
    switch (p_uuid1->len) {
    case ESP_UUID_LEN_16:
        return (p_uuid1->uuid.uuid16 == p_uuid2->uuid.uuid16);
    case ESP_UUID_LEN_32:
        return (p_uuid1->uuid.uuid32 == p_uuid2->uuid.uuid32);
    case ESP_UUID_LEN_128:
        // For 128-bit UUID, compare the entire 16-byte array
    return (memcmp(p_uuid1->uuid.uuid128, p_uuid2->uuid.uuid128, 16) == 0);
    default:
        return false; // Handle unknown UUID length gracefully
    }
}

const char* esp_gatt_status_to_str(esp_gatt_status_t status) {
    switch (status) {
    case ESP_GATT_OK:                    return "ESP_GATT_OK";
    case ESP_GATT_INVALID_HANDLE:        return "ESP_GATT_INVALID_HANDLE";
    case ESP_GATT_READ_NOT_PERMIT:       return "ESP_GATT_READ_NOT_PERMIT";
    case ESP_GATT_WRITE_NOT_PERMIT:      return "ESP_GATT_WRITE_NOT_PERMIT";
    case ESP_GATT_INVALID_PDU:           return "ESP_GATT_INVALID_PDU";
    case ESP_GATT_INSUF_AUTHENTICATION:  return "ESP_GATT_INSUF_AUTHENTICATION";
    case ESP_GATT_REQ_NOT_SUPPORTED:     return "ESP_GATT_REQ_NOT_SUPPORTED";
    case ESP_GATT_INVALID_OFFSET:        return "ESP_GATT_INVALID_OFFSET";
    case ESP_GATT_INSUF_AUTHORIZATION:   return "ESP_GATT_INSUF_AUTHORIZATION";
    case ESP_GATT_PREPARE_Q_FULL:        return "ESP_GATT_PREPARE_Q_FULL";
    case ESP_GATT_NOT_FOUND:             return "ESP_GATT_NOT_FOUND";
    case ESP_GATT_NOT_LONG:              return "ESP_GATT_NOT_LONG";
    case ESP_GATT_INSUF_KEY_SIZE:        return "ESP_GATT_INSUF_KEY_SIZE";
    case ESP_GATT_INVALID_ATTR_LEN:      return "ESP_GATT_INVALID_ATTR_LEN";
    case ESP_GATT_ERR_UNLIKELY:          return "ESP_GATT_ERR_UNLIKELY";
    case ESP_GATT_INSUF_ENCRYPTION:      return "ESP_GATT_INSUF_ENCRYPTION";
    case ESP_GATT_UNSUPPORT_GRP_TYPE:    return "ESP_GATT_UNSUPPORT_GRP_TYPE";
    case ESP_GATT_INSUF_RESOURCE:        return "ESP_GATT_INSUF_RESOURCE";
    case ESP_GATT_NO_RESOURCES:          return "ESP_GATT_NO_RESOURCES";
    case ESP_GATT_INTERNAL_ERROR:        return "ESP_GATT_INTERNAL_ERROR";
    case ESP_GATT_WRONG_STATE:           return "ESP_GATT_WRONG_STATE";
    case ESP_GATT_DB_FULL:               return "ESP_GATT_DB_FULL";
    case ESP_GATT_BUSY:                  return "ESP_GATT_BUSY";
    case ESP_GATT_ERROR:                 return "ESP_GATT_ERROR";
    case ESP_GATT_CMD_STARTED:           return "ESP_GATT_CMD_STARTED";
    case ESP_GATT_ILLEGAL_PARAMETER:     return "ESP_GATT_ILLEGAL_PARAMETER";
    case ESP_GATT_PENDING:               return "ESP_GATT_PENDING";
    case ESP_GATT_AUTH_FAIL:             return "ESP_GATT_AUTH_FAIL";
    case ESP_GATT_MORE:                  return "ESP_GATT_MORE";
    case ESP_GATT_INVALID_CFG:           return "ESP_GATT_INVALID_CFG";
    case ESP_GATT_SERVICE_STARTED:       return "ESP_GATT_SERVICE_STARTED";
    // case ESP_GATT_ENCRYPTED_MITM:        return "ESP_GATT_ENCRYPTED_MITM";
    case ESP_GATT_ENCRYPTED_NO_MITM:     return "ESP_GATT_ENCRYPTED_NO_MITM";
    case ESP_GATT_NOT_ENCRYPTED:         return "ESP_GATT_NOT_ENCRYPTED";
    case ESP_GATT_CONGESTED:             return "ESP_GATT_CONGESTED";
    case ESP_GATT_DUP_REG:               return "ESP_GATT_DUP_REG";
    case ESP_GATT_ALREADY_OPEN:          return "ESP_GATT_ALREADY_OPEN";
    case ESP_GATT_CANCEL:                return "ESP_GATT_CANCEL";
    case ESP_GATT_STACK_RSP:             return "ESP_GATT_STACK_RSP";
    case ESP_GATT_APP_RSP:               return "ESP_GATT_APP_RSP";
    case ESP_GATT_UNKNOWN_ERROR:         return "ESP_GATT_UNKNOWN_ERROR";
    case ESP_GATT_CCC_CFG_ERR:           return "ESP_GATT_CCC_CFG_ERR";
    case ESP_GATT_PRC_IN_PROGRESS:       return "ESP_GATT_PRC_IN_PROGRESS";
    case ESP_GATT_OUT_OF_RANGE:          return "ESP_GATT_OUT_OF_RANGE";
    default:                             return "ESP_GATT_STATUS_UNKNOWN";
    }
}

// Print GATT characteristic properties in human-readable form
static void print_char_properties(uint8_t props) {
    std::vector<const char*> names;
    if (props & ESP_GATT_CHAR_PROP_BIT_BROADCAST) {
        names.push_back("Broadcast");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_READ) {
        names.push_back("Read");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) {
        names.push_back("WriteNoResp");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_WRITE) {
        names.push_back("Write");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_NOTIFY) {
        names.push_back("Notify");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_INDICATE) {
        names.push_back("Indicate");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_AUTH) {
        names.push_back("AuthSigned");
    }
    if (props & ESP_GATT_CHAR_PROP_BIT_EXT_PROP) {
        names.push_back("ExtProps");
    }
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        out += names[i];
        if (i + 1 < names.size()) out += "|";
    }
    if (out.empty()) {
        out = "None";
    }
    ESP_LOGI(TAG, "Properties: %s", out.c_str());
}

