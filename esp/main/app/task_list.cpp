#include "task_list.h"

AAATask_t TaskList[] = {
	/* APP TASKS */
	{	AAA_TASK_UART_ID		, TaskUartEntry			, AAA_TASK_DEPTH_LOW	, AAA_TASK_PRIORITY_LOW		, "uart"  		, NULL},
	{	AAA_TASK_DEVICE_ID		, TaskDeviceEntry		, AAA_TASK_DEPTH_MEDIUM	, AAA_TASK_PRIORITY_HIGH	, "device"  	, NULL},	
	{	AAA_TASK_CLOUD_ID		, TaskCloudEntry		, AAA_TASK_DEPTH_MAX	, AAA_TASK_PRIORITY_NORMAL	, "cloud"  		, NULL},
};
