#include <stdio.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "i2c.h"
#include "lps35hw.h"
#include "mobilenet.h"
#include "sht4x.h"

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
  
  sht4x_startmeas();
  lps35hw_startmeas();
  int i = 0;
  while (1) {
/*    char serialbuf[200];
    if (seriallineavailable()) {
      getserialline(serialbuf, sizeof(serialbuf));
      printf("[%d] received serial: '%s'\n", i, serialbuf);
    } else {
      printf("[%d] nothing received serial.\n", i);
    }*/
    struct sht4xdata temphum;
    sht4x_read(&temphum);
    sht4x_startmeas(); // Run the next measurement.
    double press = lps35hw_readpressure();
    lps35hw_startmeas(); // Run the next measurement.
    if (temphum.valid) {
      printf(" [%d] temp %.2f   hum %.1f   press = %.3lfhPa\n", i, temphum.temp, temphum.hum, press);
    } else {
      printf(" [%d] no valid temp/hum\n", i);
    }
    i++;
    sleep_ms(1000);
  }
}
