#include <stdlib.h>
#include "nvs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_partition.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "aaa.h"

#include "app.h"
#include "app_log.h"
#include "task_list.h"
#include "task_cloud.h"

#define OTA_URL_INITIAL(u, c, e)                                \
    {                                                           \
        u, NULL, 0, NULL, NULL, HTTP_AUTH_TYPE_NONE,            \
            NULL, NULL, (const char *)c, HTTP_METHOD_GET, 0,    \
            false, 0, e, HTTP_TRANSPORT_UNKNOWN, 0, NULL, false \
    }

static const char *TAG = "TaskCloud";

static char downWildcardTopic[64];

static char deviceAddTopic[64];
static char reportBatteryTopic[64];
static char syncInfoTopic[64];

static char otaRequestTopic[64];
static char otaResponseTopic[64];

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static bool batQueue = false;
static uint16_t batQueueValue = 0;
static User_t userStorage;
static bool syncInfoFlag;
static esp_mqtt_client_config_t mqttCfg;
static esp_mqtt_client_handle_t mqttClient = NULL;

static esp_err_t httpEventHandler(esp_http_client_event_t *evt);
static void initWifi(void);
static esp_err_t mqttEventHandler(esp_mqtt_event_handle_t event);
static int responeAddDevice(User_t *user);
static bool getSyncInfoFlag(void);
static void setSyncInfoFlag(bool flag);

void TaskCloudEntry(void *params)
{
    AAAWaitAllTaskStarted();

    APP_LOGI(TAG, "started");

    initWifi();

    sprintf(downWildcardTopic, DOWN_TOPIC_PREFIX "/#", MacAddrStr);

    sprintf(deviceAddTopic, UP_TOPIC_PREFIX "/device/add", MacAddrStr);

    sprintf(reportBatteryTopic, UP_TOPIC_PREFIX "/report/battery", MacAddrStr);

    sprintf(syncInfoTopic, UP_TOPIC_PREFIX "/sync/info", MacAddrStr);

    sprintf(otaRequestTopic, DOWN_TOPIC_PREFIX "/ota/request", MacAddrStr);
    sprintf(otaResponseTopic, UP_TOPIC_PREFIX "/ota/response", MacAddrStr);

    bzero(&mqttCfg, sizeof(mqttCfg));
    mqttCfg.uri = MQTT_BROKER_URI;
    mqttCfg.client_id = MacAddrStr;
    mqttCfg.username = MQTT_USERNAME;
    mqttCfg.password = MQTT_PASSWORK;
    mqttCfg.keepalive = 60;
    mqttCfg.event_handle = mqttEventHandler;
    mqttClient = esp_mqtt_client_init(&mqttCfg);

    bzero(&userStorage, sizeof(userStorage));
    syncInfoFlag = getSyncInfoFlag();

    void *msg = NULL;
    uint32_t len = 0, sig = 0;
    uint32_t id = *(uint32_t *)params;

    while (AAATaskRecvMsg(id, &sig, &msg, &len) == AAATRUE)
    {
        switch (sig)
        {
        case CLOUD_CONNECT_PERFORM:
        {
            APP_LOGI(TAG, "CLOUD_CONNECT_PERFORM");
            if (ESP_OK != esp_mqtt_client_start(mqttClient))
            {
                APP_LOGE(TAG, "can not start mqtt client");
            }
        }
        break;

        case CLOUD_DISCONNECT_PERFORM:
        {
            APP_LOGI(TAG, "CLOUD_DISCONNECT_PERFORM");
            esp_mqtt_client_stop(mqttClient);
            break;
        }

        case CLOUD_CONNECTED:
        {
            APP_LOGI(TAG, "CLOUD_CONNECTED");

            int msgId;
            msgId = esp_mqtt_client_subscribe(mqttClient, downWildcardTopic, 0);
            if (msgId != -1)
            {
                APP_LOGI(TAG, "sent subscribe successful, msgId=%d", msgId);
            }

            /* response user id */
            if (strlen((char *)userStorage.id))
            {
                responeAddDevice(&userStorage);
            }
            bzero(&userStorage, sizeof(userStorage));

            /*reduce trafic */
            if (syncInfoFlag)
            {                
                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_SEND_SYNC_INFO, NULL, 0);
            }

            /* resend */
            if (batQueue && batQueueValue != 0)
            {
                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_SEND_REPORT_BATTERY, &batQueueValue, sizeof(batQueueValue));
            }
        }
        break;

        case CLOUD_RESP_ADD_DEVICE:
        {
            APP_LOGI(TAG, "CLOUD_RESP_ADD_DEVICE");

            User_t *user = (User_t *)msg;
            if (responeAddDevice(user) == -1)
            {
                /* response when mqtt connected */
                memcpy(&userStorage, user, sizeof(*user));
                APP_LOGW(TAG, "response fail, enqueued");
            }
            else
            {
                bzero(&userStorage, sizeof(userStorage));
            }
        }
        break;

        case CLOUD_RECV_OTA_REQUEST:
        {
            APP_LOGI(TAG, "CLOUD_RECV_OTA_REQUEST");
            APP_LOGI(TAG, "%s", (char *)msg);

            cJSON *otaJson = cJSON_Parse((const char *)msg);
            if (otaJson != NULL)
            {
                cJSON *urlJson = cJSON_GetObjectItemCaseSensitive(otaJson, "url");
                if (cJSON_IsString(urlJson) && (urlJson->valuestring != NULL))
                {
                    APP_LOGI(TAG, "url: %s", urlJson->valuestring);

                    const esp_http_client_config_t httpOtaConfig = OTA_URL_INITIAL(urlJson->valuestring, server_cert_pem_start, httpEventHandler);
                    bool success = (esp_https_ota(&httpOtaConfig) == ESP_OK) ? true : false;
                    AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_SEND_OTA_RESPONSE, &success, sizeof(success));
                }
            }
            else
            {
                APP_LOGW(TAG, "Json parser error");
            }
            cJSON_Delete(otaJson);
        }
        break;

        case CLOUD_SEND_REPORT_BATTERY:
        {
            APP_LOGI(TAG, "CLOUD_SEND_REPORT_BATTERY");
            uint16_t *bat = (uint16_t *)msg;
            char batBuff[32];
            int batMsgLen = sprintf(batBuff, "{\"battery\":%d}", *bat);

            int msgId = esp_mqtt_client_publish(mqttClient, reportBatteryTopic, batBuff, batMsgLen, 0, 0);
            if (msgId != -1)
            {
                batQueueValue = 0;
                batQueue = false;
                APP_LOGI(TAG, "sent publish successful, msgId=%d", msgId);
            }
            else
            {
                batQueueValue = *bat;
                batQueue = true;
                APP_LOGW(TAG, "report fail, enqueued");
            }
        }
        break;

        case CLOUD_SEND_SYNC_INFO:
        {
            APP_LOGI(TAG, "CLOUD_SEND_SYNC_INFO");

            char infoBuff[128];
            int infoMsgLen = sprintf(infoBuff, "{\"manufacturer\":\"" APP_MANUFACTURER "\","
                                               "\"model\":\"" APP_MODEL "\","
                                               "\"mac\":\"%s\","
                                               "\"firmware\":\"" APP_FIRMWARE "\","
                                               "\"version\":\"" APP_FW_VERSION "\"}",
                                     MacAddrStr);
            int msgId = esp_mqtt_client_publish(mqttClient, syncInfoTopic, infoBuff, infoMsgLen, 0, 0);
            if (msgId != -1)
            {
                syncInfoFlag = false;
                setSyncInfoFlag(syncInfoFlag);
                APP_LOGI(TAG, "sent publish successful, msgId=%d", msgId);
            }
        }
        break;

        case CLOUD_SEND_OTA_RESPONSE:
        {
            APP_LOGI(TAG, "CLOUD_SEND_OTA_RESPONSE");
            bool *success = (bool *)msg;
            char respBuff[32];
            int respMsgLen = sprintf(respBuff, "{\"success\":%s}", *success ? "true" : "false");
            int msgId = esp_mqtt_client_publish(mqttClient, otaResponseTopic, respBuff, respMsgLen, 0, 0);
            if (msgId != -1)
            {
                APP_LOGI(TAG, "sent publish successful, msgId=%d", msgId);
            }

            if (*success)
            {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                esp_mqtt_client_stop(mqttClient);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                esp_restart();
            }
        }
        break;

        default:
            break;
        }

        AAAFreeMsg(msg);
    }
}

esp_err_t CloudEventHandler(void *ctx, system_event_t *event)
{
    system_event_info_t *info = &event->event_info;

    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        APP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        esp_wifi_connect();
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
        APP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_CONNECT_PERFORM, NULL, 0);
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        APP_LOGE(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        APP_LOGW(TAG, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT)
        {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        }
        esp_wifi_connect();
        break;

    default:
        break;
    }

    return ESP_OK;
}

void initWifi(void)
{
    APP_LOGI(TAG, "Init Wifi");
    tcpip_adapter_init();
    esp_event_loop_init(CloudEventHandler, NULL);
    wifi_init_config_t wifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifiInitCfg);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

esp_err_t mqttEventHandler(esp_mqtt_event_handle_t event)
{
    mqttClient = event->client;
    // your_context_t *context = event->context;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        APP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_CONNECTED, NULL, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        APP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        APP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msgId=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        APP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msgId=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        APP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msgId=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        APP_LOGI(TAG, "MQTT_EVENT_DATA");
        APP_LOGI(TAG, "IN_TOPIC=%.*s", event->topic_len, event->topic);
        APP_LOGI(TAG, "IN_DATA_LEN=%d", event->data_len);
        if (event->topic_len && event->topic && event->data_len && event->data)
        {
            event->data[event->data_len] = '\0';
            event->data_len++;
            if (strncmp(otaRequestTopic, event->topic, event->topic_len) == 0)
            {
                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_RECV_OTA_REQUEST, event->data, event->data_len);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        APP_LOGW(TAG, "MQTT_EVENT_ERROR");
        break;
    }
    return ESP_OK;
}

int responeAddDevice(User_t *user)
{
    char respBuff[64];
    int respMsgLen = sprintf(respBuff, "{\"mac\":\"%s\",\"user\":\"%s\"}",
                             MacAddrStr, user->id);
    int msgId = esp_mqtt_client_publish(mqttClient, deviceAddTopic, respBuff, respMsgLen, 0, 0);
    if (msgId != -1)
    {
        APP_LOGI(TAG, "sent publish successful, msgId=%d", msgId);
    }
    return msgId;
}

esp_err_t httpEventHandler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        APP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        APP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        APP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        APP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        APP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        APP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        APP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

bool getSyncInfoFlag(void)
{
    nvs_handle handle;
    uint8_t u8flag = 0;
    bool result = false;
    if (ESP_OK == nvs_open("SYNC", NVS_READONLY, &handle))
    {
        if (ESP_OK != nvs_get_u8(handle, "info", &u8flag))
        {
            APP_LOGW(TAG, "%s: get fail", __func__);
        }
        else
        {
            result = u8flag ? true : false;
        }
        nvs_close(handle);
    }
    else
    {
        APP_LOGW(TAG, "%s: open fail", __func__);
    }
    return result;
}

void setSyncInfoFlag(bool flag)
{
    nvs_handle handle;
    uint8_t u8flag = flag ? 1 : 0;
    if (ESP_OK == nvs_open("SYNC", NVS_READWRITE, &handle))
    {
        if (ESP_OK != nvs_set_u8(handle, "info", u8flag))
        {
            APP_LOGW(TAG, "%s: set fail", __func__);
        }
        else
        {
            // nvs_commit(handle);
        }
        nvs_close(handle);
    }
    else
    {
        APP_LOGW(TAG, "%s: open fail", __func__);
    }
}
