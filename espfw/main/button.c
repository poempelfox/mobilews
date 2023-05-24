
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include "button.h"

/* The button is on GPIO0 on our board */
#define BUTTONGPIO 0

/* in main.c - we set this to turn on or off WiFi. */
extern int nextwifistate;

static int lastbustate = 1;
static int64_t lastst = 0;

void buttonirq(void * arg)
{
  int curbustate = gpio_get_level(BUTTONGPIO);
  if (curbustate != lastbustate) {
    if (curbustate == 0) { /* The button was pressed */
      /* Record the current time */
      lastst = esp_timer_get_time();
    } else { /* The button was released */
      int64_t timenow = esp_timer_get_time();
      if ((timenow - lastst) > 3000000) { /* Pressed for more than 3 seconds */
        if (nextwifistate > 0) {
          nextwifistate = 0;
        } else {
          nextwifistate = 1;
        }
      }
    }
    lastbustate = curbustate;
  }
}

int button_getstate(void)
{
  return gpio_get_level(BUTTONGPIO);
}

void button_init(void)
{
  gpio_config_t bu = {
    .pin_bit_mask = (1ULL << BUTTONGPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = 1,
    .pull_down_en = 0,
    .intr_type = GPIO_INTR_ANYEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&bu));
  /* FIXME? We would actually want to use ESP_INTR_FLAG_EDGE, but
   * everything but 0 throws an error on the ESP32-S2. */
  esp_err_t iise = gpio_install_isr_service(0);
  if ((iise != ESP_OK) && (iise != ESP_ERR_INVALID_STATE)) {
    ESP_LOGE("button.c", "gpio_install_isr_service returned an error: %s. Cannot handle button presses.",
             esp_err_to_name(iise));
  } else {
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTONGPIO, buttonirq, NULL));
  }
  /* Our pin going low may wake up from light sleep */
  ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BUTTONGPIO, 0));
}

