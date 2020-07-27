#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_smartconfig.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "aaa.h"

#include "app.h"
#include "app_log.h"
#include "task_list.h"
#include "task_device.h"

#define AP_ONLINE_REASON_TIMEOUT 255

static const char *TAG = "TaskDevice";

enum eAddDeviceState
{
    ADD_DEVICE_IDLE,
    ADD_DEVICE_SMARTLINK,
    ADD_DEVICE_APONLINE
} addDeviceState;

static void timerTOCallback(void *arg);

const esp_timer_create_args_t timerTOArgs = {
    .callback = &timerTOCallback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "add"};
static esp_timer_handle_t timerTOHandle = NULL;

static int clientSock = -1, serverSock = -1;
static bool serverIsRunning = false;
static User_t user;
static wifi_config_t staCfg;

static esp_err_t APOnlineEventHandler(void *ctx, system_event_t *event);
static void APOnlineServer(void *params);

static esp_err_t smartConfigEventHandler(void *ctx, system_event_t *event);
static void smartConfigCallback(smartconfig_status_t status, void *pdata);

void TaskDeviceEntry(void *params)
{
    AAAWaitAllTaskStarted();

    APP_LOGI(TAG, "started");

    esp_timer_create(&timerTOArgs, &timerTOHandle);

    addDeviceState = ADD_DEVICE_IDLE;

    bzero(&staCfg, sizeof(staCfg));

    void *msg = NULL;
    uint32_t len = 0, sig = 0;
    uint32_t id = *(uint32_t *)params;

    while (AAATaskRecvMsg(id, &sig, &msg, &len) == AAATRUE)
    {
        switch (sig)
        {
        case DEVICE_USER_TRIGGER:
        {
            APP_LOGI(TAG, "DEVICE_USER_TRIGGER");
            switch (addDeviceState)
            {
            case ADD_DEVICE_IDLE:
            {
                AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_SMARTLINK_START, NULL, 0);
            }
            break;

            case ADD_DEVICE_SMARTLINK:
            {
                AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_SWITCH_TO_APONLINE, NULL, 0);
            }
            break;

            case ADD_DEVICE_APONLINE:
            {
                uint8_t reason = WIFI_REASON_UNSPECIFIED;
                AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_APONLINE_FAIL, &reason, sizeof(reason));
            }
            break;

            default:
                break;
            }
        }
        break;

        case DEVICE_APONLINE_START:
        {
            APP_LOGI(TAG, "DEVICE_APONLINE_START");
            if (addDeviceState != ADD_DEVICE_APONLINE)
            {
                addDeviceState = ADD_DEVICE_APONLINE;

                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_DISCONNECT_PERFORM, NULL, 0);

                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if (mode == WIFI_MODE_STA)
                {
                    esp_event_loop_set_cb(APOnlineEventHandler, NULL);
                    wifi_config_t ap_config;
                    bzero(&ap_config, sizeof(ap_config));
                    ap_config.ap.ssid_len = sprintf((char *)ap_config.ap.ssid, AP_ONLINE_WIFI_SSID_REF "%s", MacAddrStr);
                    strcpy((char *)ap_config.ap.password, AP_ONLINE_WIFI_PASS);
                    ap_config.ap.max_connection = 1;
                    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

                    esp_wifi_disconnect();
                    esp_wifi_set_mode(WIFI_MODE_AP);
                    esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config);
                    esp_wifi_start();

                    esp_timer_start_once(timerTOHandle, AP_ONLINE_TIMEOUT);
                }
            }
            else
            {
                APP_LOGW(TAG, "aponline is running");
            }
        }
        break;

        case DEVICE_APONLINE_SUCCESS:
        {
            APP_LOGI(TAG, "DEVICE_APONLINE_SUCCESS");
            if (addDeviceState == ADD_DEVICE_APONLINE)
            {
                esp_timer_stop(timerTOHandle);

                esp_event_loop_set_cb(NULL, NULL);

                if (clientSock != -1)
                {
                    int len = 0;
                    int count = 10;
                    char txBuff[64];
                    int tmp = sprintf(txBuff, "+WIFILINK:OK,\"%s\"\r\n", MacAddrStr);
                    while (--count)
                    {
                        len = send(clientSock, txBuff, tmp, 0);
                        APP_LOGI(TAG, "%d bytes written", len);
                        vTaskDelay(1000 / portTICK_RATE_MS);
                    }
                    shutdown(clientSock, SHUT_RDWR);
                    close(clientSock);
                    clientSock = -1;
                }
                if (serverSock != -1)
                {
                    shutdown(serverSock, SHUT_RDWR);
                    close(serverSock);
                    serverSock = -1;
                }

                esp_event_loop_set_cb(CloudEventHandler, NULL);
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();

                addDeviceState = ADD_DEVICE_IDLE;

                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_CONNECT_PERFORM, NULL, 0);

                if (strlen((char *)user.id))
                {
                    AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_RESP_ADD_DEVICE, &user, sizeof(user));
                }
                bzero(&user, sizeof(user));
            }
            else
            {
                APP_LOGW(TAG, "aponline is done");
            }
        }
        break;

        case DEVICE_APONLINE_FAIL:
        {
            APP_LOGI(TAG, "DEVICE_APONLINE_FAIL");
            uint8_t reason = *(uint8_t *)msg;
            if (addDeviceState == ADD_DEVICE_APONLINE)
            {
                esp_timer_stop(timerTOHandle);

                esp_event_loop_set_cb(NULL, NULL);

                bzero(&user, sizeof(user));
                if (clientSock != -1)
                {
                    int len = 0;
                    int count = 10;
                    switch (reason)
                    {
                    case WIFI_REASON_AUTH_EXPIRE:
                    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                    case WIFI_REASON_BEACON_TIMEOUT:
                    case WIFI_REASON_AUTH_FAIL:
                    case WIFI_REASON_ASSOC_FAIL:
                    case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    {
                        APP_LOGE(TAG, "STA Auth Error");
                        while (--count)
                        {
                            len = send(clientSock, "+WIFI:ERROR_AUTH\r\n", sizeof("+WIFI:ERROR_AUTH\r\n") - 1, 0);
                            APP_LOGI(TAG, "%d bytes written", len);
                            vTaskDelay(1000 / portTICK_RATE_MS);
                        }
                    }
                    break;
                    case WIFI_REASON_NO_AP_FOUND:
                    {
                        APP_LOGE(TAG, "STA AP Not found");
                        while (--count)
                        {
                            len = send(clientSock, "+WIFI:ERROR_AP_NOT_FOUND\r\n", sizeof("+WIFI:ERROR_AP_NOT_FOUND\r\n") - 1, 0);
                            APP_LOGI(TAG, "%d bytes written", len);
                            vTaskDelay(1000 / portTICK_RATE_MS);
                        }
                    }
                    break;
                    case AP_ONLINE_REASON_TIMEOUT:
                    {
                        while (--count)
                        {
                            len = send(clientSock, "+WIFI:ERROR_TIMEOUT\r\n", sizeof("+WIFI:ERROR_TIMEOUT\r\n") - 1, 0);
                            APP_LOGI(TAG, "%d bytes written", len);
                            vTaskDelay(1000 / portTICK_RATE_MS);
                        }
                    }
                    break;
                    default:
                        break;
                    }

                    shutdown(clientSock, SHUT_RDWR);
                    close(clientSock);
                    clientSock = -1;
                }
                if (serverSock != -1)
                {
                    shutdown(serverSock, SHUT_RDWR);
                    close(serverSock);
                    serverSock = -1;
                }

                esp_event_loop_set_cb(CloudEventHandler, NULL);
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_set_config(ESP_IF_WIFI_STA, &staCfg);
                esp_wifi_start();

                addDeviceState = ADD_DEVICE_IDLE;

                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_CONNECT_PERFORM, NULL, 0);
            }
            else
            {
                APP_LOGW(TAG, "aponline is done");
            }

        }
        break;

        case DEVICE_SMARTLINK_START:
        {
            APP_LOGI(TAG, "DEVICE_SMARTLINK_START");

            if (addDeviceState != ADD_DEVICE_SMARTLINK)
            {
                addDeviceState = ADD_DEVICE_SMARTLINK;

                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_DISCONNECT_PERFORM, NULL, 0);

                esp_event_loop_set_cb(smartConfigEventHandler, NULL);
                wifi_mode_t wifi_mode;
                esp_wifi_get_mode(&wifi_mode);
                if (wifi_mode != WIFI_MODE_STA)
                {
                    esp_wifi_set_mode(WIFI_MODE_STA);
                    esp_wifi_start();
                }
                else
                {
                    esp_smartconfig_stop();
                    esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS);
                    esp_smartconfig_start(smartConfigCallback, 0);
                }

                esp_timer_start_once(timerTOHandle, SMARTLINK_TIMEOUT);
            }
            else
            {
                APP_LOGW(TAG, "smartlink is running");
            }
        }
        break;

        case DEVICE_SMARTLINK_STOP:
        {
            APP_LOGI(TAG, "DEVICE_SMARTLINK_STOP");
            bool *success = (bool *)msg;
            if (addDeviceState == ADD_DEVICE_SMARTLINK)
            {
                esp_timer_stop(timerTOHandle);

                esp_smartconfig_stop();
                esp_event_loop_set_cb(CloudEventHandler, NULL);

                if (!(*success))
                {
                    esp_wifi_connect();
                }
                else
                {
                    //todo
                }

                addDeviceState = ADD_DEVICE_IDLE;

                AAATaskPostMsg(AAA_TASK_CLOUD_ID, CLOUD_CONNECT_PERFORM, NULL, 0);
            }
            else
            {
                APP_LOGW(TAG, "smartlink is done");
            }

        }
        break;

        case DEVICE_SWITCH_TO_APONLINE:
        {
            APP_LOGI(TAG, "DEVICE_SWITCH_TO_APONLINE");

            esp_timer_stop(timerTOHandle);

            esp_smartconfig_stop();
            esp_event_loop_set_cb(CloudEventHandler, NULL);

            addDeviceState = ADD_DEVICE_IDLE;

            AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_APONLINE_START, NULL, 0);
        }

        default:
            break;
        }

        AAAFreeMsg(msg);
    }
}

void timerTOCallback(void *arg)
{
    switch (addDeviceState)
    {
    case ADD_DEVICE_SMARTLINK:
    {
        ESP_LOGW(TAG, "Smarlink timeout");
        bool success = false;
        AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_SMARTLINK_STOP, &success, sizeof(success));
    }

    break;

    case ADD_DEVICE_APONLINE:
    {
        APP_LOGW(TAG, "APOnline timeout");
        uint8_t reason = AP_ONLINE_REASON_TIMEOUT;
        AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_APONLINE_FAIL, &reason, sizeof(reason));
    }
    break;

    default:
        break;
    }
}

esp_err_t APOnlineEventHandler(void *ctx, system_event_t *event)
{
    system_event_info_t *info = &event->event_info;

    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
    {
        APP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        esp_wifi_connect();
    }
    break;
    case SYSTEM_EVENT_STA_GOT_IP:
    {
        APP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_APONLINE_SUCCESS, NULL, 0);
    }
    break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
        APP_LOGE(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        uint8_t reason = info->disconnected.reason;
        APP_LOGE(TAG, "Disconnect reason : %d", reason);
        switch (reason)
        {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
        {
            AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_APONLINE_FAIL, &reason, sizeof(reason));
        }
        break;
        default:
            break;
        }
    }
    break;
    case SYSTEM_EVENT_AP_START:
    {
        APP_LOGI(TAG, "SYSTEM_EVENT_AP_START");
        xTaskCreate(APOnlineServer, "apoServer", 2048, NULL, 5, NULL);
    }
    break;
    default:
        break;
    }
    return ESP_OK;
}

static void APOnlineServer(void *params)
{
    if (!serverIsRunning)
    {
        serverIsRunning = true;

        APP_LOGI(TAG, "APOnlineServer");
        char rxBuff[128];

        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(AP_ONLINE_PORT);
        bzero(&(destAddr.sin_zero), 8);
        struct sockaddr_in sourceAddr;
        uint addrLen = sizeof(sourceAddr);
        int err;

        serverSock = -1;
        serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (serverSock < 0)
        {
            APP_LOGE(TAG, "Unable to create socket");
        }
        else
        {
            err = bind(serverSock, (struct sockaddr *)&destAddr, sizeof(destAddr));
            if (err != 0)
            {
                APP_LOGE(TAG, "Socket unable to bind: %d", errno);
            }
            else
            {
                err = listen(serverSock, 1);
                if (err != 0)
                {
                    APP_LOGE(TAG, "Error occured during listen");
                }
                else
                {
                    APP_LOGI(TAG, "Listenning...");

                ACCEPT:
                    clientSock = accept(serverSock, (struct sockaddr *)&sourceAddr, &addrLen);
                    if (clientSock < 0)
                    {
                        APP_LOGE(TAG, "Unable to accept connection");
                    }
                    else
                    {
                        APP_LOGI(TAG, "Accepted");
                        int len = recv(clientSock, rxBuff, sizeof(rxBuff) - 1, 0);
                        if (len <= 0)
                        {
                            APP_LOGE(TAG, "recv failed");
                            shutdown(clientSock, SHUT_RDWR);
                            close(clientSock);
                            goto ACCEPT;
                        }
                        else
                        {
                            rxBuff[len] = 0;
                            bzero(&user, sizeof(user));
                            memset(staCfg.sta.ssid, 0, 32);
                            memset(staCfg.sta.password, 0, 64);

                            /* parser data */
                            bool valid = false;
                            char *token = strtok(rxBuff, "\"");
                            if (token)
                            {
                                token = strtok(NULL, "\"");
                                if (token)
                                {
                                    strcpy((char *)staCfg.sta.ssid, token);
                                    strtok(NULL, "\"");
                                    token = strtok(NULL, "\"");
                                    if (token)
                                    {
                                        strcpy((char *)staCfg.sta.password, token);
                                        strtok(NULL, "\"");
                                        token = strtok(NULL, "\"");
                                        if (token)
                                        {
                                            strcpy(user.id, token);
                                            valid = true;
                                        }
                                    }
                                }
                            }

                            if (valid)
                            {
                                APP_LOGI(TAG, "ssid:%s, size:%d", (char *)staCfg.sta.ssid, strlen((char *)staCfg.sta.ssid));
                                APP_LOGI(TAG, "password:%s, size:%d", (char *)staCfg.sta.password, strlen((char *)staCfg.sta.password));
                                APP_LOGI(TAG, "user.id:%s, size:%d", (char *)user.id, strlen((char *)user.id));
                                send(clientSock, "+WIFI:OK\r\n", sizeof("+WIFI:OK\r\n") - 1, 0);

                                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
                                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &staCfg));
                                ESP_ERROR_CHECK(esp_wifi_start());
                                ESP_ERROR_CHECK(esp_wifi_connect());
                            }
                            else
                            {
                                APP_LOGE(TAG, "Parser error");
                                send(clientSock, "+WIFI:ERROR\r\n", sizeof("+WIFI:ERROR\r\n") - 1, 0);
                                shutdown(clientSock, SHUT_RDWR);
                                close(clientSock);
                                goto ACCEPT;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        APP_LOGW(TAG, "server is running");
    }

    serverIsRunning = false;
    vTaskDelete(NULL);
}

esp_err_t smartConfigEventHandler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
    {
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        esp_smartconfig_stop();
        esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS);
        esp_smartconfig_start(smartConfigCallback, 0);
    }
    break;
    case SYSTEM_EVENT_STA_GOT_IP:
    {
        bool success = true;
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        AAATaskPostMsg(AAA_TASK_DEVICE_ID, DEVICE_SMARTLINK_STOP, &success, sizeof(success));
    }
    break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
        ESP_LOGE(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
    }
    break;
    default:
        break;
    }
    return ESP_OK;
}

void smartConfigCallback(smartconfig_status_t status, void *pdata)
{
    switch (status)
    {
    case SC_STATUS_LINK:
    {
        ESP_LOGI(TAG, "SC_STATUS_LINK");
        esp_wifi_disconnect();
        esp_wifi_set_config(ESP_IF_WIFI_STA, (wifi_config_t *)pdata);
        esp_wifi_connect();
    }
    break;
    case SC_STATUS_LINK_OVER:
    {
        ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
    }
    break;
    default:
        break;
    }
}
