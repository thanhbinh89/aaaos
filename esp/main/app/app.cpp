#include "aaa.h"

#include "esp_wifi.h"
#include "nvs_flash.h"

#include "app_log.h"
#include "sys_log.h"
#include "app.h"

#define MAC_FORMAT "%02x%02x%02x%02x%02x%02x"
#define MAC_SPLIT(m) m[0], m[1], m[2], m[3], m[4], m[5]

static const char *TAG = "App";

char MacAddrStr[13];

static void initNVS(void);

void AAATaskInit()
{
    initNVS();
}

void initNVS(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        SYS_ASSERT(nvs_flash_erase() != ESP_OK);
        err = nvs_flash_init();
    }
    SYS_ASSERT(err != ESP_OK);

    uint8_t macAddr[6];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, macAddr);
    sprintf(MacAddrStr, MAC_FORMAT, MAC_SPLIT(macAddr));
    APP_LOGI(TAG, "MAC:%s", MacAddrStr);
}

