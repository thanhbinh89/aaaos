#ifndef __TASK_LIST_H__
#define __TASK_LIST_H__

#include "aaa.h"

#include "task_device.h"
#include "task_uart.h"
#include "task_cloud.h"

enum eAAATaskID
{
	/* APP TASKS */
	AAA_TASK_UART_ID,
	AAA_TASK_DEVICE_ID,
	AAA_TASK_CLOUD_ID,
	/* EOT task ID */
	AAA_TASK_LIST_LEN,
};

extern AAATask_t TaskList[];

#endif
