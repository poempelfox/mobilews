/* Functions for submitting measurements to various APIs/Websites. */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "mobilenet.h"
#include "submit.h"
#include "sdkconfig.h"
#include "secrets.h"

static const char *TAG = "submit.c";

int submit_to_wpd_multi(int arraysize, struct wpd * aowpd)
{
    int res = 1;
    if ((strcmp(WPDTOKEN, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLM123456789") == 0)
     || (strcmp(WPDTOKEN, "") == 0)) {
      ESP_LOGI(TAG, "Not sending data to wetter.poempelfox.de because no valid token has been set.");
      return res;
    }
    int sock = mn_opentcpconn("wetter.poempelfox.de", 80, 61);
    if (sock >= 0) {
      char tmpstr[700];
      strcpy(tmpstr, "POST /api/pushmeasurement/ HTTP/1.1\r\n");
      strcat(tmpstr, "Host: wetter.poempelfox.de\r\n");
      strcat(tmpstr, "Connection: close\r\n");
      strcat(tmpstr, "Content-type: application/json\r\n");
      res = mn_writesock(sock, tmpstr, strlen(tmpstr), 61);
      if (res != 0) {
        return res;
      }
      /* Build the contents of the HTTP POST we will
       * send to wetter.poempelfox.de */
      strcpy(tmpstr, "{\"software_version\":\"mws0.1\",\"sensordatavalues\":[");
      for (int i = 0; i < arraysize; i++) {
        if (i != 0) { strcat(tmpstr, ","); }
        if (strcmp(aowpd[i].sensorid, "") != 0) { // do not try to send empty sensors
          sprintf(&tmpstr[strlen(tmpstr)],
                  "{\"value_type\":\"%s\",\"value\":\"%.3f\"}",
                  aowpd[i].sensorid, aowpd[i].value);
        }
        if (strlen(tmpstr) > (sizeof(tmpstr) - 100)) {
          ESP_LOGW(TAG, "Getting dangerously close to tmpstr size in %s", __FUNCTION__);
        }
      }
      strcat(tmpstr, "]}\n");
      char tmps2[150];
      sprintf(tmps2, "X-Sensor: %s\r\nContent-length: %d\r\n\r\n",
              WPDTOKEN, strlen(tmpstr));
      res = mn_writesock(sock, tmps2, strlen(tmps2), 61);
      if (res != 0) {
        return res;
      }
      res = mn_writesock(sock, tmpstr, strlen(tmpstr), 61);
      if (res != 0) {
        return res;
      }
      vTaskDelay(pdMS_TO_TICKS(3000)); // FIXME - instead wait for reply.
      int br;
      while ((br = mn_readsock(sock, tmpstr, sizeof(tmpstr) - 1, 30)) > 0) {
        tmpstr[br] = 0;
        ESP_LOGI(TAG, "Received HTTP reply: %s", tmpstr);
      }
      mn_closesocket(sock);
      /* FIXME set res depending on the reply. */
      res = 0;
    }
    return res;
}

int submit_to_wpd(char * sensorid, float value)
{
  struct wpd aowpd[1];
  if (strcmp(sensorid, "") == 0) {
    ESP_LOGI(TAG, "Not sending data to wetter.poempelfox.de because sensorid is not set.");
    return 1;
  }
  aowpd[0].sensorid = sensorid;
  aowpd[0].value = value;
  return submit_to_wpd_multi(1, aowpd);
}

