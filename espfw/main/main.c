#include "sdkconfig.h"
#include <esp_log.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "button.h"
#include "i2c.h"
#include "lps35hw.h"
#include "mobilenet.h"
#include "rgbled.h"
#include "sht4x.h"
#include "submit.h"
#include "wifiap.h"
#include "windsens.h"
#include "wk2132.h"

static const char *TAG = "mobilews";

int nextwifistate = 0;
int curwifistate = 0;

#define sleep_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

void app_main(void)
{
  ESP_LOGI(TAG, "Early initialization starting...");
  /* WiFi will not work without nvs_flash_init. */
  {
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
  }
  mn_init();
  i2c_port_init();
  sht4x_init(I2C_NUM_0);
  lps35hw_init(I2C_NUM_0);
  wk2132_init(I2C_NUM_0);
  wk2132_serialportinit(0, 9600); /* RG15 runs at 9600 */
  windsens_init(1); /* Wind sensor is connected to wk2132 port 1 */
  button_init();
  rgbled_init();
  wifiap_init();
  ESP_LOGI(TAG, "Early initialization finished, waking LTE module...");
  mn_wakeltemodule();
  /* Send setup commands to the IoT 6 click (uBlox Sara-R412M) module */
  mn_configureltemodule();
#if 0
  /* wait for network connection (but with timeout).
   * We don't really care if this succeeds or not, we'll just try to send
   * data anyways. */
  mn_waitfornetworkconn(181);
  mn_waitforipaddr(61);
  // Query status info - this is only for debugging really.
  sendatcmd("AT+CGACT?", 4);
  sendatcmd("AT+CGDCONT?", 4);
#endif

  time_t lastmeasts = time(NULL);
  time_t lastsuccsubmit = time(NULL);
  while (1) {
    if (nextwifistate != curwifistate) {
      curwifistate = nextwifistate;
      ESP_LOGI(TAG, "Turning WiFi-AP %s", ((curwifistate == 1) ? "On" : "Off"));
      rgbled_setled(0, 0, curwifistate * 55);
      if (curwifistate == 0) {
        wifiap_off();
      } else {
        wifiap_on();
      }
    }
    if ((time(NULL) - lastmeasts) >= 60) {
      /* Time for an update of all sensors. */
      lastmeasts = time(NULL);
      sht4x_startmeas();
      lps35hw_startmeas();
      sleep_ms(1111); /* Slightly more than a second is enough for all the sensors above */
      struct sht4xdata temphum;
      sht4x_read(&temphum);
      if (temphum.valid) {
        ESP_LOGI(TAG, "|- temp %.2f   hum %.1f", temphum.temp, temphum.hum);
      } else {
        ESP_LOGI(TAG, "|- no valid temp/hum");
      }
      double press = lps35hw_readpressure();
      ESP_LOGI(TAG, "|- press %.3lfhPa", press);
      float wd = windsens_getwinddir();
      ESP_LOGI(TAG, "|- wind direction: %.1f degrees", wd);
      float ws = windsens_getwindspeed();
      ESP_LOGI(TAG, "|- wind speed: %.1f m/s  %.2f km/h", ws, (ws * 3600.0 / 1000.0));
      /* Now send them out via network */
      mn_wakeltemodule();
      mn_waitforltemoduleready();
      rgbled_setled(55, 55, 0); /* Yellow - we're sending */
      mn_repeatcfgcmds();
      mn_waitfornetworkconn(181);
      mn_waitforipaddr(61);
      if (temphum.valid) {
        struct wpd tosubmit[2];
        tosubmit[0].sensorid = "74";
        tosubmit[0].value = temphum.temp;
        tosubmit[1].sensorid = "75";
        tosubmit[1].value = temphum.hum;
        if (submit_to_wpd_multi(2, tosubmit) == 0) {
          lastsuccsubmit = time(NULL);
        }
      }
      if (press > 0) {
        if (submit_to_wpd("76", press) == 0) {
          lastsuccsubmit = time(NULL);
        }
      }
      rgbled_setled(0, 0, curwifistate * 44);
    }
    if ((time(NULL) - lastsuccsubmit) > 1800) {
      /* We have not successfully submitted any values in 30 minutes.
       * That probably means that our crappy LTE module has once again locked up.
       * So lets try to tell it to reset, and then reset the ESP. */
      ESP_LOGE(TAG, "No successful submit in %lld seconds - will now try to reset.", (time(NULL) - lastsuccsubmit));
      mn_rebootltemodule();
      esp_restart();
    }
    long howmuchtosleep = (lastmeasts + 60) - time(NULL) - 1;
    if (howmuchtosleep > 60) { howmuchtosleep = 60; }
    if (howmuchtosleep > 0) {
      if (curwifistate > 0) {
        /* We cannot sleep if WiFi is on (else that would be unusable) */
        ESP_LOGI(TAG, "will now idle for %ld seconds", howmuchtosleep);
        vTaskDelay(pdMS_TO_TICKS(howmuchtosleep * 1000));
      } else {
        if (button_getstate() == 0) { /* We cannot sleep, we'd be woken up instantly from the GPIO IRQ */
          ESP_LOGI(TAG, "button still pressed...");
          vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
          ESP_LOGI(TAG, "will now enter light sleep mode for %ld seconds", howmuchtosleep);
          /* This is given in microseconds */
          esp_sleep_enable_timer_wakeup(howmuchtosleep * (int64_t)1000000);
          esp_light_sleep_start();
        }
      }
    }
  }
}
