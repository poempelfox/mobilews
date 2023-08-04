#include "sdkconfig.h"
#include <esp_log.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "batsens.h"
#include "button.h"
#include "i2c.h"
#include "lps35hw.h"
#include "ltr390.h"
#include "mobilenet.h"
#include "rgbled.h"
#include "rg15.h"
#include "sen50.h"
#include "sht4x.h"
#include "submit.h"
#include "webserver.h"
#include "wifiap.h"
#include "windsens.h"
#include "wk2132.h"

static const char *TAG = "mobilews";

int nextwifistate = 0;
int curwifistate = 0;

/* Measured values, packaged for export to the WiFi webserver.
 * activeevs marks which one of these has been fully updated. */
struct ev evs[2];
int activeevs = 0;

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
  ltr390_init(I2C_NUM_0);
  wk2132_init(I2C_NUM_0);
  rg15_init(); /* Note: RG15 is connected to wk2132 port 0 */
  windsens_init(1); /* Wind sensor is connected to wk2132 port 1 */
  sen50_init(I2C_NUM_1);
  sen50_startmeas(); /* FIXME: We probably do not want this to run all the time. */
  button_init();
  rgbled_init();
  batsens_init();
  wifiap_init();
  webserver_start();
  ESP_LOGI(TAG, "Early initialization finished, waking LTE module...");
  mn_wakeltemodule();
  /* Send setup commands to the IoT 6 click (uBlox Sara-R412M) module */
  mn_configureltemodule();

  time_t lastmeasts = time(NULL);
  time_t lastsuccsubmit = time(NULL);
  while (1) {
    if (nextwifistate != curwifistate) {
      curwifistate = nextwifistate;
      ESP_LOGI(TAG, "Turning WiFi-AP %s", ((curwifistate == 1) ? "On" : "Off"));
      rgbled_setled(0, 0, curwifistate * 33);
      if (curwifistate == 0) {
        wifiap_off();
      } else {
        wifiap_on();
      }
    }
    if ((time(NULL) - lastmeasts) >= 60) {
      /* Time for an update of all sensors. */
      int naevs = (activeevs == 0) ? 1 : 0;
      lastmeasts = time(NULL);
      evs[naevs].lastupd = lastmeasts;
      sht4x_startmeas();
      lps35hw_startmeas();
      rg15_requestread();
      /* Read UV index and switch to ambient light measurement */
      float uvind = ltr390_readuv();
      ltr390_startalmeas();
      sleep_ms(1111); /* Slightly more than a second is enough for all the sensors above */
      struct sht4xdata temphum;
      sht4x_read(&temphum);
      if (temphum.valid) {
        ESP_LOGI(TAG, "|- temp %.2f   hum %.1f", temphum.temp, temphum.hum);
      } else {
        ESP_LOGW(TAG, "|- no valid temp/hum");
      }
      double press = lps35hw_readpressure();
      ESP_LOGI(TAG, "|- press %.3lfhPa", press);
      float wd = windsens_getwinddir();
      ESP_LOGI(TAG, "|- wind direction: %.1f degrees", wd);
      float ws = windsens_getwindsp_multisample(3);
      ESP_LOGI(TAG, "|- wind speed: %.1f m/s (~%.2f km/h)", ws, (ws * 3.6));
      float bv = batsens_read();
      ESP_LOGI(TAG, "|- battery voltage: %.2fV", bv);
      float rgc = rg15_readraincount();
      if (rgc > -0.01) {
        ESP_LOGI(TAG, "|- rain count: %.2f mm", rgc);
      } else {
        ESP_LOGI(TAG, "|- no valid rain counter data");
      }
      struct sen50data pm;
      sen50_read(&pm);
      if (pm.valid) {
        ESP_LOGI(TAG, "|- PM1.0: %.1f  PM2.5: %.1f  PM4.0: %.1f  PM10.0: %.1f", pm.pm010, pm.pm025, pm.pm040, pm.pm100);
      } else {
        ESP_LOGW(TAG, "|- no valid particulate matter data");
      }
      /* Read Ambient Light in Lux (may block for up to 500 ms!) and switch
       * right back to UV mode */
      float amblight = ltr390_readal();
      ltr390_startuvmeas();
      ESP_LOGI(TAG, "|- UV: %.2f  AmbientLight: %.2f lux", uvind, amblight);
      /* Now send them out via network */
      mn_wakeltemodule();
      mn_waitforltemoduleready();
      rgbled_setled(33, 33, 0); /* Yellow - we're sending */
      mn_repeatcfgcmds();
      mn_sendqueuedcommands();
      mn_waitfornetworkconn(181);
      mn_waitforipaddr(61);
      /* Fetch LTE modem signal info for the webinterface */
      mn_getmninfo(evs[naevs].modemstatus);
      struct wpd tosubmit[15]; /* we'll submit at most 13 values because we have that many sensors */
      int nts = 0; /* Number of values to submit */
      /* Lets define a little helper macro to limit the copy+paste orgies */
      #define QUEUETOSUBMIT(s, v)  tosubmit[nts].sensorid = s; tosubmit[nts].value = v; nts++;
      if (temphum.valid) {
        QUEUETOSUBMIT("74", temphum.temp);
        QUEUETOSUBMIT("75", temphum.hum);
        evs[naevs].temp = temphum.temp;
        evs[naevs].hum = temphum.hum;
      } else {
        evs[naevs].temp = NAN;
        evs[naevs].hum = NAN;
      }
      if (press > 0) {
        QUEUETOSUBMIT("76", press);
        evs[naevs].press = press;
      } else {
        evs[naevs].press = NAN;
      }
      if ((wd > -0.01) && (wd < 360.01)) { /* Valid wind direction measurement */
        QUEUETOSUBMIT("78", wd);
        evs[naevs].winddirdeg = wd;
      } else {
        evs[naevs].winddirdeg = NAN;
      }
      if (ws > -0.01) { /* Valid wind speed measurement */
        QUEUETOSUBMIT("79", ws);
        evs[naevs].windspeed = ws;
      } else {
        evs[naevs].windspeed = NAN;
      }
      if (bv > -0.01) { /* Valid battery measurement */
        QUEUETOSUBMIT("80", bv);
        evs[naevs].batvolt = bv;
      } else {
        evs[naevs].batvolt = NAN;
      }
      if (rgc > -0.01) { /* Valid rain gauge measurement */
        QUEUETOSUBMIT("81", rgc);
        evs[naevs].raingc = rgc;
      } else {
        evs[naevs].raingc = NAN;
      }
      if (pm.valid) { /* Valid particulate matter measurement */
        QUEUETOSUBMIT("82", pm.pm010);
        QUEUETOSUBMIT("83", pm.pm025);
        QUEUETOSUBMIT("84", pm.pm040);
        QUEUETOSUBMIT("85", pm.pm100);
        evs[naevs].pm010 = pm.pm010;
        evs[naevs].pm025 = pm.pm025;
        evs[naevs].pm040 = pm.pm040;
        evs[naevs].pm100 = pm.pm100;
      } else {
        evs[naevs].pm010 = NAN;
        evs[naevs].pm025 = NAN;
        evs[naevs].pm040 = NAN;
        evs[naevs].pm100 = NAN;
      }
      if (uvind > -0.01) { /* Valid UV-Index measurement */
        QUEUETOSUBMIT("86", uvind);
        evs[naevs].uvind = uvind;
      } else {
        evs[naevs].uvind = NAN;
      }
      if (amblight > -0.01) { /* Valid Ambient Light measurement */
        QUEUETOSUBMIT("87", amblight);
        evs[naevs].amblight = amblight;
      } else {
        evs[naevs].amblight = NAN;
      }
      /* Clean up helper macro */
      #undef QUEUETOSUBMIT
      /* mark the updated values as the current ones for the webserver */
      activeevs = naevs;
      if (nts > 0) { /* Is there at least one valid value to submit? */
        ESP_LOGI(TAG, "have %d values to submit...", nts);
        if (submit_to_wpd_multi(nts, tosubmit) == 0) {
          lastsuccsubmit = time(NULL);
        }
      }
      rgbled_setled(0, 0, curwifistate * 33);
    }
    if ((time(NULL) - lastsuccsubmit) > 900) {
      /* We have not successfully submitted any values in 15 minutes.
       * That probably means that our crappy LTE module has once again locked up.
       * So lets try to tell it to reset, and then reset the ESP. */
      ESP_LOGE(TAG, "No successful submit in %lld seconds - about to powercycle the LTE modem and reset.", (time(NULL) - lastsuccsubmit));
      mn_powercycleltemodem();
      ESP_LOGE(TAG, "modem powercycled, now resetting the ESP32...");
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
          button_rtcdetach(); /* needs to be called after sleep to detach the GPIO from the RTC again! */
        }
      }
    }
  }
}
