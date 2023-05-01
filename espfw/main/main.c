#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_sleep.h>
#include <esp_log.h>
#include "i2c.h"
#include "lps35hw.h"
#include "mobilenet.h"
#include "sht4x.h"
#include "submit.h"

static const char *TAG = "mobilews";

#define sleep_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

void app_main(void)
{
  ESP_LOGI(TAG, "Early initialization starting...");
  mn_init();
  i2c_port_init();
  sht4x_init(I2C_NUM_0);
  lps35hw_init(I2C_NUM_0);
  ESP_LOGI(TAG, "Early initialization finished, waking LTE module...");
  mn_wakeltemodule();
  /* Send setup commands to the IoT 6 click (uBlox Sara-R412M) module */
  mn_configureltemodule();
  /* wait for network connection (but with timeout).
   * We don't really care if this succeeds or not, we'll just try to send
   * data anyways. */
  mn_waitfornetworkconn(181);
  mn_waitforipaddr(61);
  // Query status info - this is only for debugging really.
  sendatcmd("AT+CGACT?", 4);
  sendatcmd("AT+CGDCONT?", 4);
  // Lets open a network connection
/*
  int sock = mn_opentcpconn("wetter.poempelfox.de", 80, 61);
  if (sock >= 0) {
    char tst[500]; int br;
    strcpy(tst, "GET /api/getlastvalue/11 HTTP/1.1\r\nHost: wetter.poempelfox.de\r\nConnection: close\r\n\r\n");
    mn_writesock(sock, tst, strlen(tst), 30);
    sleep_ms(3000); // FIXME
    while ((br = mn_readsock(sock, tst, sizeof(tst) - 1, 30)) > 0) {
      tst[br] = 0;
      ESP_LOGI(TAG, "Received HTTP reply: %s", tst);
    }
  } else {
    ESP_LOGE(TAG, "Connection to web server failed :( retcode %d", sock);
  } */
  
  time_t lastmeasts = time(NULL);
  while (1) {
    if ((time(NULL) - lastmeasts) >= 60) {
      /* Time for an update of all sensors. */
      lastmeasts = time(NULL);
      sht4x_startmeas();
      lps35hw_startmeas();
      sleep_ms(1111); /* Slightly more than a second is enough for all the sensors above */
      struct sht4xdata temphum;
      sht4x_read(&temphum);
      double press = lps35hw_readpressure();
      if (temphum.valid) {
        printf(" temp %.2f   hum %.1f   press = %.3lfhPa\n", temphum.temp, temphum.hum, press);
      } else {
        printf(" no valid temp/hum, press = %.3lfhPa\n", press);
      }
      /* Now send them out via network */
      mn_wakeltemodule();
      mn_waitforltemoduleready();
      mn_repeatcfgcmds();
      mn_waitfornetworkconn(181);
      mn_waitforipaddr(61);
      if (temphum.valid) {
        struct wpd tosubmit[2];
        tosubmit[0].sensorid = "74";
        tosubmit[0].value = temphum.temp;
        tosubmit[1].sensorid = "75";
        tosubmit[1].value = temphum.hum;
        submit_to_wpd_multi(2, tosubmit);
      }
      if (press > 0) {
        submit_to_wpd("76", press);
      }
    }
    long howmuchtosleep = (lastmeasts + 60) - time(NULL) - 1;
    if (howmuchtosleep > 60) { howmuchtosleep = 60; }
    if (howmuchtosleep > 0) {
      /* FIXME: Not if WiFi is on! */
      ESP_LOGI(TAG, "will now enter sleep mode for %ld seconds", howmuchtosleep);
      /* This is given in microseconds */
      esp_sleep_enable_timer_wakeup(howmuchtosleep * (int64_t)1000000);
      esp_light_sleep_start();
    }
  }
}
