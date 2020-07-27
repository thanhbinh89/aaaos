#ifndef __APP_H__
#define __APP_H__

#include "aaa.h"

/*****************************************************************************/
/*  task GW_TASK_DEVICE define
 */
/*****************************************************************************/
/* define timer */
/* define signal */
enum
{
	DEVICE_USER_TRIGGER = AAA_USER_DEFINE_SIG,
	DEVICE_APONLINE_START,
	DEVICE_APONLINE_SUCCESS,
	DEVICE_APONLINE_FAIL,
	DEVICE_SMARTLINK_START,
	DEVICE_SMARTLINK_STOP,
	DEVICE_SWITCH_TO_APONLINE
};

/*****************************************************************************/
/*  task GW_TASK_CLOUD define
 */
/*****************************************************************************/
/* define timer */
/* define signal */
enum
{
	CLOUD_CONNECT_PERFORM = AAA_USER_DEFINE_SIG,
	CLOUD_DISCONNECT_PERFORM,
	CLOUD_CONNECTED,
	CLOUD_RESP_ADD_DEVICE,
	CLOUD_RECV_OTA_REQUEST,
	CLOUD_SEND_REPORT_BATTERY,
	CLOUD_SEND_SYNC_INFO,
	CLOUD_SEND_OTA_RESPONSE,
};

/*****************************************************************************/
/*  task GW_TASK_UART define
 */
/*****************************************************************************/
/* define timer */
/* define signal */
// enum
// {
	
// };

/*****************************************************************************/
/*  global define variable
 */
/*****************************************************************************/
#define APP_MANUFACTURER "AAAOS"
#define APP_MODEL "MODEL1"
#define APP_FIRMWARE "AAAOS"
#define APP_FW_VERSION "0.0.1"

#define SMARTLINK_TIMEOUT (120000000)
#define AP_ONLINE_TIMEOUT (120000000)
#define AP_ONLINE_WIFI_SSID_REF "DEV_"
#define AP_ONLINE_WIFI_PASS "123456789"
#define AP_ONLINE_PORT (8899)

#define MQTT_BROKER_URI "mqtt://mqtt.eclipse.org"
#define MQTT_USERNAME ""
#define MQTT_PASSWORK ""

#define UP_TOPIC_PREFIX "dev/up/%s"
#define DOWN_TOPIC_PREFIX "dev/down/%s"

typedef struct tUser
{
	char id[16];
} User_t;

extern char MacAddrStr[13];

#endif // __APP_H__
