#ifndef __AAA_H__
#define __AAA_H__

#ifdef __cplusplus
extern "C++"
{
#endif

#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "aaa_macros.h"

#define AAA_VERSION "1.0"

#define AAA_TASK_QUEUE_LENGTH (16)

#define AAA_USER_DEFINE_SIG 10

typedef enum eAAATaskDepth
{
	AAA_TASK_DEPTH_LOW = 1024,
	AAA_TASK_DEPTH_MEDIUM = 2048,
	AAA_TASK_DEPTH_HIGH = 3072,
	AAA_TASK_DEPTH_MAX = 4096,
} AAATaskDepth_t;

typedef enum eAAATaskPriority
{
	AAA_TASK_PRIORITY_LOW = 2,
	AAA_TASK_PRIORITY_NORMAL = 3,
	AAA_TASK_PRIORITY_HIGH = 4,
	AAA_TASK_PRIORITY_MAX = 5,
} AAATaskPriority_t;

typedef struct tAAATask
{
	uint32_t tId;
	void (*tFunc)(void *);
	AAATaskDepth_t tDepth;
	AAATaskPriority_t tPriority;
	const char *const tDesc;
	void *qHandle;
} AAATask_t;

typedef struct tAAAMsg
{
	uint32_t sig;
	uint32_t len;
	void *data;
} AAAMsg_t;

/******************************************************************************
* task function
*******************************************************************************/
/* function is called before create threads */
extern void AAATaskInit();

/* function using to make sure that all task is initialed */
extern void AAAWaitAllTaskStarted();

/* function exchange messages */
extern int AAATaskPostMsg(uint32_t, uint32_t, void *, uint32_t);
extern int AAATaskPostMsgFromISR(uint32_t, uint32_t, void *, uint32_t);
extern int AAATaskRecvMsg(uint32_t, uint32_t *, void **, uint32_t *);
extern void AAAFreeMsg(void *);

#ifdef __cplusplus
}
#endif

#endif
