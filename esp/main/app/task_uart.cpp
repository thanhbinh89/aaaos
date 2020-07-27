#include "aaa.h"

#include "esp_timer.h"
#include "nvs.h"
#include "driver/uart.h"

#include "app.h"
#include "app_log.h"
#include "task_list.h"
#include "task_uart.h"

#define UART_BUFF_SIZE 128

static const char *TAG = "TaskUart";

static uart_config_t uartCfg = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0};
static QueueHandle_t uartQueue;
static uint8_t uartParserBuffer[UART_BUFF_SIZE];

static void uartHandler(void *params);
static void uartParser(uint8_t *data, uint32_t len);

void TaskUartEntry(void *params)
{
    AAAWaitAllTaskStarted();

    APP_LOGI(TAG, "started");

    uart_param_config((uart_port_t)0, &uartCfg);
    uart_driver_install((uart_port_t)0, UART_BUFF_SIZE * 2, UART_BUFF_SIZE * 2, 32, &uartQueue, 0);
    xTaskCreate(uartHandler, "uart", 2048, NULL, 6, NULL);

    void *msg = NULL;
    uint32_t len = 0, sig = 0;
    uint32_t id = *(uint32_t *)params;

    while (AAATaskRecvMsg(id, &sig, &msg, &len) == AAATRUE)
    {
        switch (sig)
        {
        
        default:
            break;
        }
        AAAFreeMsg(msg);
    }
}

void uartHandler(void *params)
{
    uart_event_t event;
    uint32_t len;
    APP_LOGI(TAG, "uartHandler started");

    while (1)
    {
        if (xQueueReceive(uartQueue, (void *)&event, (portTickType)portMAX_DELAY))
        {
            switch (event.type)
            {
            case UART_DATA:
                APP_LOGD(TAG, "data incoming: %d", event.size);
                len = uart_read_bytes((uart_port_t)0, uartParserBuffer, event.size, portMAX_DELAY);
                uartParser(uartParserBuffer, len);                
                break;

            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                APP_LOGW(TAG, "fifo over flow or buffer full");
                uart_flush_input((uart_port_t)0);
                xQueueReset(uartQueue);
                break;

            default:
                break;
            }
        }
    }
}

void uartParser(uint8_t *data, uint32_t len)
{
    
}
