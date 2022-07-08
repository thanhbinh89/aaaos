#ifndef __SYS_LOG_H__
#define __SYS_LOG_H__

#include "esp_system.h"
#include "esp_log.h"
#include "esp_log_internal.h"

#define SYS_LOG_EN

#ifdef SYS_LOG_EN
#define SYS_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define SYS_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define SYS_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define SYS_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define SYS_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)
#define SYS_LOG_HEXDUM(tag, buff, len) ESP_LOG_BUFFER_HEXDUMP(tag, buff, len, ESP_LOG_DEBUG)
#else
#define SYS_LOGE(tag, format, ...)
#define SYS_LOGW(tag, format, ...)
#define SYS_LOGI(tag, format, ...)
#define SYS_LOGD(tag, format, ...)
#define SYS_LOGV(tag, format, ...)
#define SYS_LOG_HEXDUM(tag, buff, len, level)
#endif

#define SYS_ASSERT_RESTART 0

#define SYS_ASSERT(cond)                                     \
    do                                                       \
    {                                                        \
        if (!(cond))                                            \
        {                                                    \
            SYS_LOGE("ASSERT", "%s:%d", __FILE__, __LINE__); \
            if (SYS_ASSERT_RESTART)                          \
            {                                                \
                esp_restart();                               \
            }                                                \
            do                                               \
            {                                                \
            } while (1);                                     \
        }                                                    \
    } while (0);

#endif