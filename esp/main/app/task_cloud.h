#ifndef __APP_CLOUD_H__
#define __APP_CLOUD_H__

#include "esp_err.h"
#include "esp_event.h"

extern void TaskCloudEntry(void *);

extern esp_err_t CloudEventHandler(void *ctx, system_event_t *event);

#endif

/*
==========REPORT==========
Online
pir/up/<MAC>/report/online
{
  "online":true
}

Battery
pir/up/<MAC>/report/battery
{
  "battery": 2990
}

Motion
pir/up/<MAC>/report/motion
{
  "motion": true
}
*/

/*
==========SYNC==========
Device Information
pir/up/<MAC>/sync/info
{
    "manufacturer": "FTI",
    "model": "PIR1",
    "mac":  "ecfabc876ce4",
    "firmware":  "FIOT",
    "version":  "0.0.1"
}
*/

/*
===========OTA================
Request
pir/down/<MAC>/ota/request
{
  "url":  <string>
}

Response
pir/down/<MAC>/ota/response
{
  "success": <boolean>
}
*/

/*
==========ADD DEVICE==========
Response add device
pir/up/<MAC>/device/add
{
  ""mac"":  <string>,
  ""user"":  <string>
}
*/