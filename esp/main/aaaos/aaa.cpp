#include "aaa.h"
#include "aaa_log.h"

#include "task_list.h"

static const char *TAG = "AAA";

static SemaphoreHandle_t SemTasksStarted = NULL;
static uint32_t TaskTableLen = AAA_TASK_LIST_LEN;

static void printBanner(void)
{
    printf("\r\n"
           "      _/_/      _/_/      _/_/      _/_/      _/_/_/   \r\n"
           "   _/    _/  _/    _/  _/    _/  _/    _/  _/          \r\n"
           "  _/_/_/_/  _/_/_/_/  _/_/_/_/  _/    _/    _/_/       \r\n"
           " _/    _/  _/    _/  _/    _/  _/    _/        _/      \r\n"
           "_/    _/  _/    _/  _/    _/    _/_/    _/_/_/         \r\n"
           "Author: thanhbinh89\r\n"
           "Build: %s\r\n", __DATE__);
}

extern "C" void app_main()
{
    printBanner();
    AAATaskInit();

    AAA_LOGD(TAG, "TaskTableLen: %d", TaskTableLen);

    SemTasksStarted = xSemaphoreCreateCounting(TaskTableLen, TaskTableLen);
    SYS_ASSERT(SemTasksStarted == NULL);

    for (int idx = 0; idx < TaskTableLen; idx++)
    {
        TaskList[idx].qHandle = xQueueCreate(AAA_TASK_QUEUE_LENGTH, sizeof(AAAMsg_t));
        SYS_ASSERT(TaskList[idx].qHandle == NULL);

        SYS_ASSERT(pdPASS != xTaskCreate(TaskList[idx].tFunc, TaskList[idx].tDesc,
                                         TaskList[idx].tDepth, &TaskList[idx].tId, TaskList[idx].tPriority, NULL));

        AAA_LOGD(TAG, "%s, id:%d, entry:%p, desc:%s, depth:%d, prio:%d",
                 __func__, TaskList[idx].tId, TaskList[idx].tFunc, TaskList[idx].tDesc, TaskList[idx].tDepth, TaskList[idx].tPriority);
    }
}

void AAAWaitAllTaskStarted()
{
    AAA_LOGD(TAG, "%s", __func__);

    SYS_ASSERT(pdTRUE != xSemaphoreTake(SemTasksStarted, portMAX_DELAY));

    while (uxSemaphoreGetCount(SemTasksStarted) > 0)
    {
        // portYIELD();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

int AAATaskPostMsg(uint32_t tId, uint32_t sig, void *msg, uint32_t len)
{
    AAA_LOGD(TAG, "%s, id:%d, sig:%d, msg:%p, len:%d", __func__, tId, sig, msg, len);

    AAAMsg_t aaaMsg = {sig, len, NULL};

    SYS_ASSERT(tId >= TaskTableLen);

    if (len)
    {
        aaaMsg.data = pvPortMalloc(len);
        SYS_ASSERT(aaaMsg.data == NULL);

        memcpy(aaaMsg.data, msg, len);
        AAA_LOGD(TAG, "%s, copy msg:%p", __func__, aaaMsg.data);
    }

    SYS_ASSERT(pdTRUE != xQueueSend(TaskList[tId].qHandle, &aaaMsg, portMAX_DELAY));
    return AAATRUE;
}

int AAATaskPostMsgFromISR(uint32_t tId, uint32_t sig, void *msg, uint32_t len)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    AAAMsg_t aaaMsg = {sig, len, NULL};

    if (tId >= TaskTableLen)
        return AAAFALSE;

    if (len)
    {
        aaaMsg.data = pvPortMalloc(len);
        if (aaaMsg.data == NULL)
            return AAAFALSE;

        memcpy(aaaMsg.data, msg, len);
    }

    if (pdTRUE != xQueueSendFromISR(TaskList[tId].qHandle, &aaaMsg, &xHigherPriorityTaskWoken))
        return AAAFALSE;
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD();
    }
    return AAATRUE;
}

int AAATaskRecvMsg(uint32_t tId, uint32_t *sig, void **msg, uint32_t *len)
{
    SYS_ASSERT(tId >= TaskTableLen);

    AAAMsg_t aaaMsg = {0, 0, NULL};
    SYS_ASSERT(pdTRUE != xQueueReceive(TaskList[tId].qHandle, &aaaMsg, portMAX_DELAY));

    *sig = aaaMsg.sig;
    *msg = aaaMsg.data;
    *len = aaaMsg.len;
    if (msg == NULL)
    {
        ;
    }
    AAA_LOGD(TAG, "%s, id:%d, sig:%d, msg:%p, len:%d", __func__, tId, *sig, *msg, *len);

    return AAATRUE;
}

void AAAFreeMsg(void *msg)
{
    if (msg != NULL)
    {
        vPortFree(msg);
        AAA_LOGD(TAG, "%s, msg:%p", __func__, msg);
    }
}